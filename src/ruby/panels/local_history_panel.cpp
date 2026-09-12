#include "local_history_panel.h"
#include "ruby/git/ruby_git_diff.h"
#include "ruby/core/project_context.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QMessageBox>
#include <QFileInfo>
#include <QFile>
#include <QDir>

namespace ruby::panels {

static QString format_friendly_time(const QDateTime& dt) {
    if (!dt.isValid()) return "Unknown";
    qint64 secs = dt.secsTo(QDateTime::currentDateTime());
    if (secs < 60) return "Just now";
    if (secs < 3600) return QString("%1 minutes ago").arg(std::max<qint64>(1, secs / 60));
    if (secs < 86400) return QString("%1 hours ago").arg(secs / 3600);
    if (secs < 172800) return "Yesterday";
    return dt.toString("yyyy-MM-dd hh:mm");
}

LocalHistoryPanel::LocalHistoryPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(6, 6, 6, 6);
    main_layout->setSpacing(6);

    // Header toolbar
    auto* header_layout = new QHBoxLayout();
    m_file_label = new QLabel("No file selected", this);
    m_file_label->setStyleSheet("font-weight: bold; color: #aaccff;");
    header_layout->addWidget(m_file_label, 1);

    m_refresh_btn = new QPushButton("Refresh", this);
    connect(m_refresh_btn, &QPushButton::clicked, this, &LocalHistoryPanel::refresh);
    header_layout->addWidget(m_refresh_btn);

    m_restore_btn = new QPushButton("Restore Revision", this);
    m_restore_btn->setEnabled(false);
    m_restore_btn->setStyleSheet("background-color: #2b5535; color: white; font-weight: bold;");
    connect(m_restore_btn, &QPushButton::clicked, this, &LocalHistoryPanel::on_restore_clicked);
    header_layout->addWidget(m_restore_btn);

    main_layout->addLayout(header_layout);

    // Splitter: Revision list and Diff viewer
    auto* splitter = new QSplitter(Qt::Vertical, this);

    m_revision_list = new QListWidget(this);
    m_revision_list->setStyleSheet(
        "QListWidget { background: #181a1f; color: #d0d0d0; border: 1px solid #2a2d34; font-size: 11px; }"
        "QListWidget::item:selected { background: #2c4260; color: #ffffff; }"
    );
    connect(m_revision_list, &QListWidget::currentRowChanged, this, &LocalHistoryPanel::on_revision_selected);
    splitter->addWidget(m_revision_list);

    m_diff_viewer = new QPlainTextEdit(this);
    m_diff_viewer->setReadOnly(true);
    m_diff_viewer->setStyleSheet(
        "QPlainTextEdit { background: #131518; color: #c8d3e0; font-family: monospace; font-size: 11px; border: 1px solid #2a2d34; }"
    );
    splitter->addWidget(m_diff_viewer);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    main_layout->addWidget(splitter, 1);
}

void LocalHistoryPanel::set_target_file(const QString& full_path) {
    m_full_path = full_path;

    QString pdir = QString::fromStdString(ruby::core::ProjectContext::instance().project_dir());
    if (!pdir.isEmpty() && full_path.startsWith(pdir)) {
        m_relative_path = QDir(pdir).relativeFilePath(full_path);
    } else {
        m_relative_path = QFileInfo(full_path).fileName();
    }

    m_file_label->setText(QFileInfo(full_path).fileName());
    refresh();
}

void LocalHistoryPanel::refresh() {
    m_revision_list->clear();
    m_diff_viewer->clear();
    m_restore_btn->setEnabled(false);

    if (m_full_path.isEmpty() || m_relative_path.isEmpty()) {
        m_file_label->setText("No file selected");
        return;
    }

    m_revisions = ruby::git::RubyGit::get_history(m_relative_path);
    if (m_revisions.isEmpty()) {
        m_revision_list->addItem("No commit history found for this file.");
        return;
    }

    for (const auto& rev : m_revisions) {
        QString friendly_time = format_friendly_time(rev.timestamp);
        QString time_str = rev.timestamp.toString("yyyy-MM-dd hh:mm:ss");
        QString item_text = QString("[%1] %2 (%3)  -  %4")
            .arg(rev.short_hash)
            .arg(friendly_time)
            .arg(time_str)
            .arg(rev.message);
        m_revision_list->addItem(item_text);
    }

    if (!m_revisions.isEmpty()) {
        m_revision_list->setCurrentRow(0);
    }
}

void LocalHistoryPanel::on_revision_selected(int row) {
    if (row < 0 || row >= m_revisions.size()) {
        m_diff_viewer->clear();
        m_restore_btn->setEnabled(false);
        return;
    }

    const auto& rev = m_revisions[row];
    m_restore_btn->setEnabled(true);

    QByteArray old_blob = ruby::git::RubyGit::get_blob_at_commit(rev.hash, m_relative_path);

    QFile cur_file(m_full_path);
    QByteArray cur_blob;
    if (cur_file.open(QIODevice::ReadOnly)) {
        cur_blob = cur_file.readAll();
    }

    QString diff = ruby::git::generate_semantic_diff(old_blob, cur_blob, m_relative_path);
    m_diff_viewer->setPlainText(diff);
}

void LocalHistoryPanel::on_restore_clicked() {
    int row = m_revision_list->currentRow();
    if (row < 0 || row >= m_revisions.size()) return;

    const auto& rev = m_revisions[row];
    auto reply = QMessageBox::question(
        this,
        "Restore Revision",
        QString("Are you sure you want to revert %1 to revision %2 (%3)?\nAny unsaved edits will be replaced.")
            .arg(m_relative_path)
            .arg(rev.short_hash)
            .arg(rev.timestamp.toString("yyyy-MM-dd hh:mm")),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        if (ruby::git::RubyGit::restore_file(rev.hash, m_relative_path)) {
            emit fileRestored(m_full_path);
            refresh();
            QMessageBox::information(this, "Restored", "File reverted successfully.");
        } else {
            QMessageBox::warning(this, "Restore Failed", "Could not rollback file.");
        }
    }
}

} // namespace ruby::panels

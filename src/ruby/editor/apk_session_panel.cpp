#include "ruby/editor/apk_session_panel.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

namespace ruby::editor {

ApkSessionPanel::ApkSessionPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_label = new QLabel(this);
    m_label->setStyleSheet(
        "QLabel { color:#61afef; background:#1a2230; border:1px solid #2c3a4d;"
        " border-radius:3px; padding:1px 8px; font-size:12px; }");
    layout->addWidget(m_label);

    m_end_btn = new QToolButton(this);
    m_end_btn->setText(tr("End"));
    m_end_btn->setToolTip(tr("End the APK session (the extracted copy stays on disk)."));
    m_end_btn->setCursor(Qt::PointingHandCursor);
    m_end_btn->setStyleSheet(
        "QToolButton { color:#abb2bf; background:#1e2227; border:1px solid #3e4451;"
        " border-radius:3px; padding:1px 8px; font-size:12px; }"
        "QToolButton:hover { background:#2c323c; }");
    connect(m_end_btn, &QToolButton::clicked, this, &ApkSessionPanel::end_session);
    layout->addWidget(m_end_btn);

    refresh_ui();
}

bool ApkSessionPanel::import_apk(QWidget* parent) {
    const QString file = QFileDialog::getOpenFileName(
        parent, tr("Import APK…"), QString(),
        tr("Android APK (*.apk);;All Files (*)"));
    if (file.isEmpty()) return false;

    apk::Session s;
    std::string error;
    if (!apk::import_apk(file.toStdString(), s, error)) {
        emit statusMessage(tr("APK import failed: %1").arg(QString::fromStdString(error)), 8000);
        return false;
    }
    adopt_session(s);
    emit statusMessage(
        tr("Imported APK session: %1 (%2 files extracted). The Asset Browser now points at the extracted tree.")
            .arg(QString::fromStdString(s.session_id)),
        8000);
    return true;
}

void ApkSessionPanel::end_session() {
    if (!has_session()) return;
    m_session = apk::Session{};
    refresh_ui();
    emit sessionChanged(false);
    emit statusMessage(tr("APK session ended — the extracted copy is still on disk."), 6000);
}

void ApkSessionPanel::adopt_session(const apk::Session& s) {
    m_session = s;
    refresh_ui();
    emit sessionChanged(true);
}

void ApkSessionPanel::refresh_ui() {
    if (has_session()) {
        const QString origin = QString::fromStdString(m_session.apk_origin);
        m_label->setText(tr("APK session: %1").arg(QFileInfo(origin).fileName()));
        m_label->setToolTip(origin);
        m_end_btn->setVisible(true);
        setVisible(true);
    } else {
        m_label->clear();
        m_end_btn->setVisible(false);
        setVisible(false);
    }
}

} // namespace ruby::editor
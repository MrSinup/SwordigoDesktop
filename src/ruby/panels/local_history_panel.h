#pragma once

#include <QWidget>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include "ruby/git/ruby_git.h"

namespace ruby::panels {

class LocalHistoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit LocalHistoryPanel(QWidget* parent = nullptr);
    ~LocalHistoryPanel() override = default;

    void set_target_file(const QString& full_path);
    void refresh();

signals:
    void fileRestored(const QString& full_path);

private slots:
    void on_revision_selected(int row);
    void on_restore_clicked();

private:
    QString m_full_path;
    QString m_relative_path;
    QList<ruby::git::Revision> m_revisions;

    QLabel* m_file_label = nullptr;
    QListWidget* m_revision_list = nullptr;
    QPlainTextEdit* m_diff_viewer = nullptr;
    QPushButton* m_restore_btn = nullptr;
    QPushButton* m_refresh_btn = nullptr;
};

} // namespace ruby::panels

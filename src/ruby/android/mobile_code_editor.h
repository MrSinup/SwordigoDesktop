#pragma once
// ============================================================================
// mobile_code_editor.h — Single-Active-File Mobile Code & Markup Editor
//   Portrait-first FileRift and Lua editor for Ruby GG.
//   Strictly handles one active file at a time (no cluttering tabs),
//   with virtualized text editing and a mobile keyboard quick-symbol bar.
// ============================================================================

#include <QWidget>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QString>

namespace ruby::android {

class MobileCodeEditor : public QWidget {
    Q_OBJECT

public:
    explicit MobileCodeEditor(QWidget* parent = nullptr);
    ~MobileCodeEditor() override = default;

    bool load_file(const QString& file_path);
    bool save_file();

    const QString& current_file_path() const { return m_file_path; }
    bool is_dirty() const { return m_is_dirty; }

signals:
    void backToHubRequested();
    void switchToVisualRequested(const QString& file_path);
    void fileDirtyStateChanged(bool dirty);
    void statusMessage(const QString& message);

private slots:
    void on_text_changed();
    void insert_symbol(const QString& symbol);

private:
    void setup_ui();
    void setup_quick_symbol_bar(QVBoxLayout* root_layout);

    QString m_file_path;
    bool m_is_dirty = false;
    bool m_is_loading = false;
    bool m_is_filerift_transcoded = false;

    QLabel* m_lbl_title = nullptr;
    QLabel* m_lbl_dirty = nullptr;
    QPushButton* m_btn_save = nullptr;
    QPushButton* m_btn_visual = nullptr;
    QPlainTextEdit* m_text_edit = nullptr;
};

} // namespace ruby::android

#pragma once
// ============================================================================
// new_file_dialog.h — File Creator Wizard with Preconfigured Headers & Templates
// ============================================================================

#include <QDialog>
#include <QString>
#include <QList>

class QListWidget;
class QLineEdit;
class QCheckBox;
class QPlainTextEdit;
class QLabel;

namespace ruby::editor {

struct FileTemplate {
    QString name;
    QString extension;
    QString default_filename;
    QString category;
    QString description;
    QString boilerplate;
    bool can_compile_binary = false;
};

class NewFileDialog : public QDialog {
    Q_OBJECT

public:
    explicit NewFileDialog(QWidget* parent = nullptr, const QString& initial_dir = QString());
    ~NewFileDialog() override = default;

    QString created_file_path() const { return m_created_path; }

private slots:
    void onTemplateSelected(int row);
    void onBrowseFolder();
    void onCreate();

private:
    void init_templates();
    void update_preview();

    QList<FileTemplate> m_templates;
    QListWidget* m_template_list = nullptr;
    QLabel* m_desc_label = nullptr;
    QLineEdit* m_name_edit = nullptr;
    QLineEdit* m_dir_edit = nullptr;
    QCheckBox* m_compile_binary_check = nullptr;
    QPlainTextEdit* m_preview_edit = nullptr;

    QString m_created_path;
};

} // namespace ruby::editor

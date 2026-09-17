#pragma once
// ============================================================================
// desktop_integration_dialog.h — OS Desktop Entry & File Associations Settings
// ============================================================================

#include <QDialog>
#include <QList>
#include <QString>

class QLabel;
class QPushButton;
class QCheckBox;
class QTableWidget;
class QComboBox;

namespace ruby::editor {

class DesktopIntegrationDialog : public QDialog {
    Q_OBJECT

public:
    explicit DesktopIntegrationDialog(QWidget* parent = nullptr);
    ~DesktopIntegrationDialog() override = default;

private slots:
    void onInstallDesktopEntry();
    void onUninstallDesktopEntry();
    void onRegisterSelected();
    void onUnregisterSelected();
    void onSelectAll(bool select);
    void refresh_status();
    void onThemeChanged(int index);

private:
    void setup_ui();

    QComboBox* m_theme_combo = nullptr;

    QLabel* m_status_icon = nullptr;
    QLabel* m_status_label = nullptr;
    QPushButton* m_btn_install_entry = nullptr;
    QPushButton* m_btn_uninstall_entry = nullptr;

    QTableWidget* m_formats_table = nullptr;
    QList<QCheckBox*> m_format_checkboxes;
    QLabel* m_info_label = nullptr;
};

} // namespace ruby::editor

#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QMap>
#include "change_detector.h"
#include "build_runner.h"
#include "target_registry.h"

namespace build_studio {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString& rootDir, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onRefreshStatus();
    void onPresetSelected(TargetPreset preset);
    void onBuildClicked();
    void onLaunchGameClicked();
    void onLaunchStudioClicked();
    void onLogAppended(const QString& text, LogLevel level);
    void onProgressChanged(int percentage, const QString& stepDescription);
    void onBuildCompleted(bool success, int exitCode);

private:
    void setupUi();
    void applyTheme();
    void updateStatusBadge(const ProjectStatus& status);
    void detectCompilers();

    QString m_rootDir;
    ProjectStatus m_currentStatus;
    BuildRunner* m_runner = nullptr;

    // Header Widgets
    QLabel* m_sysStatusLabel = nullptr;
    QLabel* m_devShieldLed = nullptr;
    QLabel* m_detailsLabel = nullptr;

    // Preset Buttons
    QPushButton* m_btnPresetSmart = nullptr;
    QPushButton* m_btnPresetFullGame = nullptr;
    QPushButton* m_btnPresetStudio = nullptr;
    QPushButton* m_btnPresetAll = nullptr;

    // Target Checkboxes and status labels
    struct TargetRowWidgets {
        QCheckBox* checkbox;
        QLabel* statusBadge;
    };
    QMap<QString, TargetRowWidgets> m_targetRows;

    // Toolchain Widgets
    QComboBox* m_hostCompilerCombo = nullptr;
    QComboBox* m_sreCompilerCombo = nullptr;
    QCheckBox* m_chkStrip = nullptr;
    QCheckBox* m_chkDynarmic = nullptr;
    QCheckBox* m_chkClean = nullptr;

    // Progress & Console
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_progressStepLabel = nullptr;
    QPlainTextEdit* m_consoleOutput = nullptr;

    // Primary Action Buttons
    QPushButton* m_btnBuild = nullptr;
    QPushButton* m_btnLaunchGame = nullptr;
    QPushButton* m_btnLaunchStudio = nullptr;
};

} // namespace build_studio

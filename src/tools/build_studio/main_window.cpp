#include "main_window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStandardItemModel>
#include <QFontDatabase>
#include <QScrollBar>

namespace build_studio {

MainWindow::MainWindow(const QString& rootDir, QWidget* parent)
    : QMainWindow(parent), m_rootDir(rootDir)
{
    setWindowTitle("SWORDIGO // BUILD.SYS STATION-64");
    resize(1020, 740);
    setMinimumSize(850, 620);

    m_runner = new BuildRunner(this);
    connect(m_runner, &BuildRunner::logAppended, this, &MainWindow::onLogAppended);
    connect(m_runner, &BuildRunner::progressChanged, this, &MainWindow::onProgressChanged);
    connect(m_runner, &BuildRunner::buildCompleted, this, &MainWindow::onBuildCompleted);

    setupUi();
    applyTheme();
    detectCompilers();
    onRefreshStatus();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    // ─── 1. Vintage Industrial Header ──────────────────────────────────────────
    auto* headerFrame = new QFrame(central);
    headerFrame->setObjectName("instrumentHeader");
    auto* headerLayout = new QHBoxLayout(headerFrame);
    headerLayout->setContentsMargins(16, 10, 16, 10);

    auto* titleBlock = new QVBoxLayout();
    titleBlock->setSpacing(2);

    auto* brandLabel = new QLabel("SWORDIGO // BUILD.SYS WORKSTATION [v4.5]", headerFrame);
    brandLabel->setObjectName("brandLabel");
    brandLabel->setStyleSheet("font-family: monospace; font-size: 14px; font-weight: bold; color: #ffb454; letter-spacing: 1px;");

    m_detailsLabel = new QLabel("INITIALIZING SENSORS & PROJECT REPOSITORY...", headerFrame);
    m_detailsLabel->setStyleSheet("font-family: monospace; font-size: 11px; color: #8a919e;");

    titleBlock->addWidget(brandLabel);
    titleBlock->addWidget(m_detailsLabel);
    headerLayout->addLayout(titleBlock, 1);

    // LED Annunciators
    auto* annunciatorBox = new QHBoxLayout();
    annunciatorBox->setSpacing(8);

    m_devShieldLed = new QLabel("[ DEV_SYMBOLS: ARMED ]", headerFrame);
    m_devShieldLed->setObjectName("devShieldLed");
    m_devShieldLed->setStyleSheet(
        "font-family: monospace; font-size: 11px; font-weight: bold; padding: 4px 8px; "
        "color: #00ffcc; background: #0b2520; border: 1px solid #00cc99; border-radius: 2px;");
    annunciatorBox->addWidget(m_devShieldLed);

    m_sysStatusLabel = new QLabel("[ SYS_STATUS: SCANNING ]", headerFrame);
    m_sysStatusLabel->setObjectName("sysStatusLabel");
    m_sysStatusLabel->setStyleSheet(
        "font-family: monospace; font-size: 11px; font-weight: bold; padding: 4px 8px; "
        "color: #ffb454; background: #2b1d0c; border: 1px solid #d35400; border-radius: 2px;");
    annunciatorBox->addWidget(m_sysStatusLabel);

    headerLayout->addLayout(annunciatorBox);
    rootLayout->addWidget(headerFrame);

    // ─── 2. Main Workstation Panel (Split Left / Right) ────────────────────────
    auto* mainPanel = new QHBoxLayout();
    mainPanel->setSpacing(10);

    // ── Left: Target Matrix ──
    auto* targetGroup = new QGroupBox("TARGET COMPILATION MATRIX", central);
    auto* targetGroupLayout = new QVBoxLayout(targetGroup);
    targetGroupLayout->setContentsMargins(8, 14, 8, 8);
    targetGroupLayout->setSpacing(6);

    // Preset selector buttons
    auto* presetLayout = new QHBoxLayout();
    presetLayout->setSpacing(4);
    m_btnPresetSmart = new QPushButton("SMART SELECT", targetGroup);
    m_btnPresetFullGame = new QPushButton("FULL GAME", targetGroup);
    m_btnPresetStudio = new QPushButton("STUDIO", targetGroup);
    m_btnPresetAll = new QPushButton("ALL TARGETS", targetGroup);

    connect(m_btnPresetSmart, &QPushButton::clicked, this, [this]() { onPresetSelected(TargetPreset::Smart); });
    connect(m_btnPresetFullGame, &QPushButton::clicked, this, [this]() { onPresetSelected(TargetPreset::FullGame); });
    connect(m_btnPresetStudio, &QPushButton::clicked, this, [this]() { onPresetSelected(TargetPreset::StudioOnly); });
    connect(m_btnPresetAll, &QPushButton::clicked, this, [this]() { onPresetSelected(TargetPreset::AllTargets); });

    presetLayout->addWidget(m_btnPresetSmart);
    presetLayout->addWidget(m_btnPresetFullGame);
    presetLayout->addWidget(m_btnPresetStudio);
    presetLayout->addWidget(m_btnPresetAll);
    targetGroupLayout->addLayout(presetLayout);

    // Target scroll area
    auto* targetScroll = new QScrollArea(targetGroup);
    targetScroll->setWidgetResizable(true);
    targetScroll->setFrameShape(QFrame::NoFrame);

    auto* targetContainer = new QWidget(targetScroll);
    auto* targetLayout = new QVBoxLayout(targetContainer);
    targetLayout->setContentsMargins(2, 2, 2, 2);
    targetLayout->setSpacing(3);

    QString lastCat = "";
    for (const auto& t : getAllProjectTargets()) {
        if (t.category != lastCat) {
            lastCat = t.category;
            auto* catHeader = new QLabel(QString("── %1 ──").arg(lastCat.toUpper()), targetContainer);
            catHeader->setStyleSheet("font-family: monospace; color: #5a6270; font-size: 10px; font-weight: bold; margin-top: 6px;");
            targetLayout->addWidget(catHeader);
        }

        auto* row = new QWidget(targetContainer);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(4, 1, 4, 1);
        rowLayout->setSpacing(8);

        auto* chk = new QCheckBox(t.displayName, row);
        chk->setProperty("targetName", t.name);

        auto* badge = new QLabel("[ ... ]", row);
        badge->setStyleSheet("font-family: monospace; font-size: 10px; font-weight: bold; padding: 2px 5px;");

        rowLayout->addWidget(chk, 1);
        rowLayout->addWidget(badge);

        TargetRowWidgets widgets;
        widgets.checkbox = chk;
        widgets.statusBadge = badge;
        m_targetRows[t.name] = widgets;

        targetLayout->addWidget(row);
    }
    targetLayout->addStretch(1);
    targetScroll->setWidget(targetContainer);
    targetGroupLayout->addWidget(targetScroll, 1);

    mainPanel->addWidget(targetGroup, 3);

    // ── Right: Toolchain & Operation Dials ──
    auto* toolchainGroup = new QGroupBox("TOOLCHAIN & PARAMETERS", central);
    auto* toolchainLayout = new QVBoxLayout(toolchainGroup);
    toolchainLayout->setContentsMargins(12, 14, 12, 10);
    toolchainLayout->setSpacing(8);

    auto* lblHost = new QLabel("HOST NATIVE COMPILER:", toolchainGroup);
    lblHost->setStyleSheet("font-family: monospace; font-size: 10px; color: #a0a6b2;");
    m_hostCompilerCombo = new QComboBox(toolchainGroup);
    toolchainLayout->addWidget(lblHost);
    toolchainLayout->addWidget(m_hostCompilerCombo);

    auto* lblSre = new QLabel("GUEST SRE CROSS-COMPILER (ARM64):", toolchainGroup);
    lblSre->setStyleSheet("font-family: monospace; font-size: 10px; color: #a0a6b2;");
    m_sreCompilerCombo = new QComboBox(toolchainGroup);
    toolchainLayout->addWidget(lblSre);
    toolchainLayout->addWidget(m_sreCompilerCombo);

    toolchainLayout->addSpacing(6);
    auto* flagsLabel = new QLabel("BUILD DISPOSITION FLAGS:", toolchainGroup);
    flagsLabel->setStyleSheet("font-family: monospace; font-size: 10px; color: #a0a6b2;");
    toolchainLayout->addWidget(flagsLabel);

    m_chkStrip = new QCheckBox("STRIP EXECUTABLE SYMBOLS (-s)", toolchainGroup);
    m_chkDynarmic = new QCheckBox("DYNARMIC JIT TRANSLATOR", toolchainGroup);
    m_chkDynarmic->setChecked(true);
    m_chkClean = new QCheckBox("PURGE CACHE (--clean)", toolchainGroup);

    toolchainLayout->addWidget(m_chkStrip);
    toolchainLayout->addWidget(m_chkDynarmic);
    toolchainLayout->addWidget(m_chkClean);

    toolchainLayout->addStretch(1);

    auto* btnRefresh = new QPushButton("RESYNC SCANNER", toolchainGroup);
    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::onRefreshStatus);
    toolchainLayout->addWidget(btnRefresh);

    mainPanel->addWidget(toolchainGroup, 2);
    rootLayout->addLayout(mainPanel, 2);

    // ─── 3. Progress Meter & Hardware Buttons ──────────────────────────────────
    auto* progressRow = new QHBoxLayout();
    progressRow->setSpacing(10);

    m_progressBar = new QProgressBar(central);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFormat("%p% // READY");
    progressRow->addWidget(m_progressBar, 1);

    m_progressStepLabel = new QLabel("STANDBY", central);
    m_progressStepLabel->setStyleSheet("font-family: monospace; font-size: 11px; color: #ffb454;");
    progressRow->addWidget(m_progressStepLabel);
    rootLayout->addLayout(progressRow);

    // CRT Console Viewport
    m_consoleOutput = new QPlainTextEdit(central);
    m_consoleOutput->setReadOnly(true);
    m_consoleOutput->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_consoleOutput->setPlaceholderText("[SWORDIGO // BUILD MONITOR IDLE]");
    rootLayout->addWidget(m_consoleOutput, 3);

    // Chunky Tactile Push-Buttons
    auto* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);

    m_btnBuild = new QPushButton("EXECUTE BUILD", central);
    m_btnBuild->setObjectName("btnBuild");
    m_btnBuild->setFixedHeight(40);
    connect(m_btnBuild, &QPushButton::clicked, this, &MainWindow::onBuildClicked);
    buttonRow->addWidget(m_btnBuild, 2);

    m_btnLaunchGame = new QPushButton("LAUNCH GAME", central);
    m_btnLaunchGame->setObjectName("btnLaunchGame");
    m_btnLaunchGame->setFixedHeight(40);
    m_btnLaunchGame->setEnabled(false);
    connect(m_btnLaunchGame, &QPushButton::clicked, this, &MainWindow::onLaunchGameClicked);
    buttonRow->addWidget(m_btnLaunchGame, 1);

    m_btnLaunchStudio = new QPushButton("LAUNCH STUDIO", central);
    m_btnLaunchStudio->setObjectName("btnLaunchStudio");
    m_btnLaunchStudio->setFixedHeight(40);
    m_btnLaunchStudio->setEnabled(false);
    connect(m_btnLaunchStudio, &QPushButton::clicked, this, &MainWindow::onLaunchStudioClicked);
    buttonRow->addWidget(m_btnLaunchStudio, 1);

    rootLayout->addLayout(buttonRow);
    setCentralWidget(central);
}

void MainWindow::detectCompilers() {
    m_hostCompilerCombo->clear();
    m_hostCompilerCombo->addItem("GCC (GNU C/C++ Toolchain)", "gcc");

    bool hasClang = !QStandardPaths::findExecutable("clang++").isEmpty();
    if (hasClang) {
        m_hostCompilerCombo->addItem("CLANG (LLVM Toolchain)", "clang");
    }

    m_sreCompilerCombo->clear();
    m_sreCompilerCombo->addItem("GCC (aarch64-linux-gnu-gcc) [ACTIVE]", "gcc");
    m_sreCompilerCombo->addItem("CLANG aarch64 [LOCKED - SRE TESTING PENDING]", "clang");

    auto* model = qobject_cast<QStandardItemModel*>(m_sreCompilerCombo->model());
    if (model && model->item(1)) {
        model->item(1)->setEnabled(false);
        model->item(1)->setToolTip("SRE Clang cross-compilation will be unlocked in a future release.");
    }
}

void MainWindow::onRefreshStatus() {
    m_currentStatus = ChangeDetector::inspectProject(m_rootDir);
    updateStatusBadge(m_currentStatus);
}

void MainWindow::updateStatusBadge(const ProjectStatus& status) {
    m_detailsLabel->setText(status.description);

    QString statusColor = "#ffb454"; // amber
    QString statusBg = "#2b1d0c";
    QString statusBorder = "#d35400";

    if (status.mode == ProjectStateMode::Ready) {
        statusColor = "#50fa7b"; // green
        statusBg = "#0e2a1b";
        statusBorder = "#27ae60";
    } else if (status.mode == ProjectStateMode::Installing) {
        statusColor = "#8be9fd"; // cyan
        statusBg = "#0b222a";
        statusBorder = "#2980b9";
    }

    m_sysStatusLabel->setText(QString("[ SYS_STATUS: %1 ]").arg(status.modeTitle));
    m_sysStatusLabel->setStyleSheet(
        QString("font-family: monospace; font-size: 11px; font-weight: bold; padding: 4px 8px; "
                "color: %1; background: %2; border: 1px solid %3; border-radius: 2px;")
                .arg(statusColor, statusBg, statusBorder));

    // Developer Symbol Safety Check
    if (status.symbolState == BinarySymbolState::Unstripped) {
        m_devShieldLed->setText("[ DEV_SYMBOLS: ARMED ]");
        m_devShieldLed->setStyleSheet(
            "font-family: monospace; font-size: 11px; font-weight: bold; padding: 4px 8px; "
            "color: #50fa7b; background: #0e2a1b; border: 1px solid #27ae60; border-radius: 2px;");
        m_devShieldLed->setVisible(true);
        m_chkStrip->setChecked(false);
        m_chkStrip->setToolTip("Unstripped developer binary detected. Stripping automatically disarmed.");
    } else {
        m_devShieldLed->setText("[ CONSUMER_MODE: STRIPPED ]");
        m_devShieldLed->setStyleSheet(
            "font-family: monospace; font-size: 11px; font-weight: bold; padding: 4px 8px; "
            "color: #a0a6b2; background: #1a1c20; border: 1px solid #383d47; border-radius: 2px;");
        m_devShieldLed->setVisible(true);
        m_chkStrip->setChecked(true);
        m_chkStrip->setToolTip("Strip symbols for smaller release distribution size.");
    }

    // Update target checklist badges
    for (const auto& ts : status.targetStatuses) {
        if (m_targetRows.contains(ts.name)) {
            auto& row = m_targetRows[ts.name];
            row.statusBadge->setText(ts.stateLabel);

            if (ts.state == TargetState::UpToDate) {
                row.statusBadge->setStyleSheet("font-family: monospace; font-size: 10px; color: #50fa7b; font-weight: bold;");
            } else if (ts.state == TargetState::Modified) {
                row.statusBadge->setStyleSheet("font-family: monospace; font-size: 10px; color: #ffb454; font-weight: bold;");
            } else {
                row.statusBadge->setStyleSheet("font-family: monospace; font-size: 10px; color: #ff5555; font-weight: bold;");
            }
            row.statusBadge->setToolTip(ts.detail);
        }
    }

    m_btnLaunchGame->setEnabled(status.hasGameBinary);
    m_btnLaunchStudio->setEnabled(status.hasStudioBinary);

    // Initial smart selection: check targets that are NOT UpToDate
    onPresetSelected(TargetPreset::Smart);
}

void MainWindow::onPresetSelected(TargetPreset preset) {
    if (preset == TargetPreset::Smart) {
        // Smart select: only select targets that are Missing or Modified
        for (const auto& ts : m_currentStatus.targetStatuses) {
            if (m_targetRows.contains(ts.name)) {
                bool shouldCheck = (ts.state != TargetState::UpToDate);
                // If everything is up to date, check swordfare by default for convenience
                if (m_currentStatus.mode == ProjectStateMode::Ready && ts.name == "swordfare") {
                    shouldCheck = true;
                }
                m_targetRows[ts.name].checkbox->setChecked(shouldCheck);
            }
        }
    } else {
        QStringList targets = getPresetTargetNames(preset);
        for (auto it = m_targetRows.begin(); it != m_targetRows.end(); ++it) {
            it.value().checkbox->setChecked(targets.contains(it.key()));
        }
    }
}

void MainWindow::onBuildClicked() {
    if (m_runner->isRunning()) {
        m_runner->cancelBuild();
        return;
    }

    BuildOptions opts;
    opts.rootDir = m_rootDir;
    opts.clean = m_chkClean->isChecked();
    opts.strip = m_chkStrip->isChecked();
    opts.useDynarmic = m_chkDynarmic->isChecked();
    opts.hostCompiler = m_hostCompilerCombo->currentData().toString();
    opts.sreCompiler = m_sreCompilerCombo->currentData().toString();

    for (auto it = m_targetRows.begin(); it != m_targetRows.end(); ++it) {
        if (it.value().checkbox->isChecked()) {
            opts.targets.append(it.key());
        }
    }

    if (opts.targets.isEmpty()) {
        onLogAppended("[BUILD.SYS] Notice: No targets selected for compilation.\n", LogLevel::Warning);
        return;
    }

    m_consoleOutput->clear();
    m_progressBar->setValue(0);
    m_progressBar->setFormat("%p% // COMPILING");
    m_btnBuild->setText("ABORT COMPILATION");
    m_btnBuild->setStyleSheet("background-color: #991b1b; color: #ffffff; font-weight: bold; border: 1px solid #dc2626; border-radius: 3px;");

    m_runner->startBuild(opts);
}

void MainWindow::onLaunchGameClicked() {
    QString gamePath = m_rootDir + "/bin/swordfare";
    onLogAppended(QString("[BUILD.SYS] Launching process: %1\n").arg(gamePath), LogLevel::Success);
    QProcess::startDetached(gamePath, {}, m_rootDir);
}

void MainWindow::onLaunchStudioClicked() {
    QString studioPath = m_rootDir + "/bin/ruby_gg";
    onLogAppended(QString("[BUILD.SYS] Launching process: %1\n").arg(studioPath), LogLevel::Success);
    QProcess::startDetached(studioPath, {}, m_rootDir);
}

void MainWindow::onLogAppended(const QString& text, LogLevel level) {
    QString color = "#d8dee9"; // Crisp off-white
    switch (level) {
        case LogLevel::Error:   color = "#ff5555"; break; // Red
        case LogLevel::Warning: color = "#ffb454"; break; // Amber
        case LogLevel::Success: color = "#50fa7b"; break; // Phosphor green
        case LogLevel::Info:    color = "#8a919e"; break;
    }

    m_consoleOutput->appendHtml(QString("<span style='color:%1; font-family: monospace;'>%2</span>").arg(color, text.toHtmlEscaped()));
    m_consoleOutput->verticalScrollBar()->setValue(m_consoleOutput->verticalScrollBar()->maximum());
}

void MainWindow::onProgressChanged(int percentage, const QString& stepDescription) {
    m_progressBar->setValue(percentage);
    m_progressBar->setFormat(QString("%1% // WORKING").arg(percentage));
    m_progressStepLabel->setText(stepDescription);
}

void MainWindow::onBuildCompleted(bool success, int /*exitCode*/) {
    m_btnBuild->setText("EXECUTE BUILD");
    m_btnBuild->setStyleSheet("background-color: #2b313a; color: #ffb454; font-weight: bold; border: 1px solid #4a5260; border-radius: 3px;");
    m_progressBar->setFormat("%p% // FINISHED");

    onRefreshStatus();
}

void MainWindow::applyTheme() {
    setStyleSheet(R"(
        QMainWindow { background-color: #1a1c20; }
        QWidget { color: #d8dee9; font-family: "SF Pro Text", "Segoe UI", Cantarell, Ubuntu, sans-serif; font-size: 11px; }
        
        QFrame#instrumentHeader {
            background-color: #141619;
            border-top: 1px solid #383d47;
            border-left: 1px solid #383d47;
            border-right: 1px solid #0e1012;
            border-bottom: 2px solid #0e1012;
            border-radius: 3px;
        }

        QGroupBox {
            font-family: monospace;
            font-weight: bold;
            font-size: 10px;
            letter-spacing: 1px;
            color: #ffb454;
            border-top: 1px solid #383d47;
            border-left: 1px solid #383d47;
            border-right: 1px solid #0e1012;
            border-bottom: 1px solid #0e1012;
            border-radius: 3px;
            margin-top: 14px;
            padding-top: 14px;
            background-color: #1d2025;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 10px;
            padding: 0 4px;
            background-color: #1d2025;
        }

        QScrollArea { background-color: transparent; }
        
        QCheckBox {
            color: #c0c6d2;
            spacing: 6px;
            font-family: monospace;
            font-size: 11px;
        }
        QCheckBox::indicator {
            width: 14px;
            height: 14px;
            border-top: 1px solid #0e1012;
            border-left: 1px solid #0e1012;
            border-right: 1px solid #383d47;
            border-bottom: 1px solid #383d47;
            border-radius: 2px;
            background-color: #141619;
        }
        QCheckBox::indicator:checked {
            background-color: #ffb454;
            border: 1px solid #e67e22;
        }

        QComboBox {
            background-color: #141619;
            border-top: 1px solid #0e1012;
            border-left: 1px solid #0e1012;
            border-right: 1px solid #383d47;
            border-bottom: 1px solid #383d47;
            border-radius: 2px;
            padding: 5px 8px;
            color: #d8dee9;
            font-family: monospace;
            font-size: 11px;
        }
        QComboBox QAbstractItemView {
            background-color: #141619;
            color: #d8dee9;
            selection-background-color: #ffb454;
            selection-color: #000000;
        }

        QProgressBar {
            background-color: #141619;
            border-top: 1px solid #0e1012;
            border-left: 1px solid #0e1012;
            border-right: 1px solid #383d47;
            border-bottom: 1px solid #383d47;
            border-radius: 2px;
            text-align: center;
            color: #d8dee9;
            font-family: monospace;
            font-weight: bold;
            font-size: 10px;
        }
        QProgressBar::chunk {
            background-color: #27ae60;
            border-radius: 1px;
        }

        QPlainTextEdit {
            background-color: #0c0d10;
            border-top: 2px solid #050608;
            border-left: 2px solid #050608;
            border-right: 1px solid #2a2e36;
            border-bottom: 1px solid #2a2e36;
            border-radius: 2px;
            color: #d8dee9;
            font-family: monospace;
            font-size: 11px;
            padding: 6px;
        }

        QPushButton {
            background-color: #25282f;
            border-top: 1px solid #3f4450;
            border-left: 1px solid #3f4450;
            border-right: 1px solid #101214;
            border-bottom: 1px solid #101214;
            border-radius: 2px;
            padding: 5px 12px;
            color: #d8dee9;
            font-family: monospace;
            font-weight: bold;
            font-size: 11px;
        }
        QPushButton:hover { background-color: #2e323b; }
        QPushButton:pressed {
            border-top: 1px solid #101214;
            border-left: 1px solid #101214;
            border-right: 1px solid #3f4450;
            border-bottom: 1px solid #3f4450;
            background-color: #191b1f;
        }

        QPushButton#btnBuild {
            background-color: #2b313a;
            color: #ffb454;
            border: 1px solid #4a5260;
            font-size: 12px;
        }
        QPushButton#btnBuild:hover { background-color: #373f4b; }

        QPushButton#btnLaunchGame {
            background-color: #173824;
            color: #50fa7b;
            border: 1px solid #27ae60;
            font-size: 12px;
        }
        QPushButton#btnLaunchGame:hover { background-color: #1d472e; }
        QPushButton#btnLaunchGame:disabled {
            background-color: #1a1c20;
            color: #4a5260;
            border-color: #2a2e36;
        }

        QPushButton#btnLaunchStudio {
            background-color: #281d3d;
            color: #bd93f9;
            border: 1px solid #8e44ad;
            font-size: 12px;
        }
        QPushButton#btnLaunchStudio:hover { background-color: #352752; }
        QPushButton#btnLaunchStudio:disabled {
            background-color: #1a1c20;
            color: #4a5260;
            border-color: #2a2e36;
        }
    )");
}

} // namespace build_studio

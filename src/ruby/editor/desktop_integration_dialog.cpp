// ============================================================================
// desktop_integration_dialog.cpp — OS Desktop Entry & File Associations Settings
// ============================================================================

#include "ruby/editor/desktop_integration_dialog.h"
#include "platform/desktop_integration.h"
#include "ruby/theme/ruby_theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileInfo>

namespace ruby::editor {

DesktopIntegrationDialog::DesktopIntegrationDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Desktop Integration & Preferences — Ruby Studio"));
    resize(720, 620);
    setModal(true);
    setup_ui();
    refresh_status();
}

void DesktopIntegrationDialog::setup_ui() {
    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(14, 14, 14, 14);
    root_layout->setSpacing(12);

    // Header Title
    auto* title_label = new QLabel(QStringLiteral("Studio Preferences & Desktop Integration"), this);
    title_label->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold; color: #e5e9f0;"));
    root_layout->addWidget(title_label);

    auto* subtitle_label = new QLabel(
        QStringLiteral("Configure appearance theme and system-level desktop integration for Ruby Studio."), this);
    subtitle_label->setStyleSheet(QStringLiteral("font-size: 11px; color: #9aa2b1;"));
    subtitle_label->setWordWrap(true);
    root_layout->addWidget(subtitle_label);

    // ── Group 0: Appearance & Theming ─────────────────────────────────────────
    auto* group_theme = new QGroupBox(QStringLiteral("Appearance & Theming"), this);
    auto* theme_layout = new QHBoxLayout(group_theme);
    theme_layout->setSpacing(12);

    auto* theme_icon_label = new QLabel(QStringLiteral("🎨"), group_theme);
    theme_icon_label->setStyleSheet(QStringLiteral("font-size: 16px;"));
    theme_layout->addWidget(theme_icon_label);

    auto* theme_title_label = new QLabel(QStringLiteral("Studio Theme:"), group_theme);
    theme_title_label->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 12px;"));
    theme_layout->addWidget(theme_title_label);

    m_theme_combo = new QComboBox(group_theme);
    m_theme_combo->addItem(QStringLiteral("Dark Studio (Default)"), static_cast<int>(ruby::theme::ThemeId::DarkStudio));
    m_theme_combo->addItem(QStringLiteral("Universal White (Light Studio)"), static_cast<int>(ruby::theme::ThemeId::LightStudio));

    ruby::theme::ThemeId current = ruby::theme::get_current_theme();
    int idx = m_theme_combo->findData(static_cast<int>(current));
    if (idx >= 0) {
        m_theme_combo->setCurrentIndex(idx);
    }
    connect(m_theme_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DesktopIntegrationDialog::onThemeChanged);
    theme_layout->addWidget(m_theme_combo, 1);

    root_layout->addWidget(group_theme);

    // ── Group 1: Desktop Entry ────────────────────────────────────────────────
    auto* group_entry = new QGroupBox(QStringLiteral("Desktop Entry & Application Launcher"), this);
    auto* entry_layout = new QVBoxLayout(group_entry);
    entry_layout->setSpacing(8);

    auto* status_row = new QHBoxLayout();
    m_status_icon = new QLabel(group_entry);
    m_status_icon->setFixedSize(16, 16);
    status_row->addWidget(m_status_icon);

    m_status_label = new QLabel(group_entry);
    m_status_label->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 12px;"));
    status_row->addWidget(m_status_label, 1);

    m_btn_install_entry = new QPushButton(QStringLiteral("Install / Update Desktop Entry"), group_entry);
    m_btn_install_entry->setStyleSheet(QStringLiteral("QPushButton { background-color: #2b5b84; color: white; padding: 4px 10px; border-radius: 4px; } QPushButton:hover { background-color: #3b7bb4; }"));
    connect(m_btn_install_entry, &QPushButton::clicked, this, &DesktopIntegrationDialog::onInstallDesktopEntry);
    status_row->addWidget(m_btn_install_entry);

    m_btn_uninstall_entry = new QPushButton(QStringLiteral("Uninstall Entry"), group_entry);
    m_btn_uninstall_entry->setStyleSheet(QStringLiteral("QPushButton { background-color: #4c3333; color: #ff8888; padding: 4px 10px; border-radius: 4px; } QPushButton:hover { background-color: #663333; }"));
    connect(m_btn_uninstall_entry, &QPushButton::clicked, this, &DesktopIntegrationDialog::onUninstallDesktopEntry);
    status_row->addWidget(m_btn_uninstall_entry);

    entry_layout->addLayout(status_row);

    auto* path_info = new QLabel(
        QStringLiteral("Executable: <code style='color:#61afef;'>%1</code>").arg(platform::DesktopIntegration::get_executable_path()),
        group_entry);
    path_info->setTextFormat(Qt::RichText);
    path_info->setStyleSheet(QStringLiteral("font-size: 10px; color: #828997;"));
    entry_layout->addWidget(path_info);

    root_layout->addWidget(group_entry);

    // ── Group 2: File Associations ───────────────────────────────────────────
    auto* group_assoc = new QGroupBox(QStringLiteral("Registered File Types"), this);
    auto* assoc_layout = new QVBoxLayout(group_assoc);
    assoc_layout->setSpacing(8);

    m_formats_table = new QTableWidget(group_assoc);
    m_formats_table->setColumnCount(5);
    m_formats_table->setHorizontalHeaderLabels({
        QStringLiteral("Enable"),
        QStringLiteral("Format"),
        QStringLiteral("Description"),
        QStringLiteral("Category"),
        QStringLiteral("System Status")
    });
    m_formats_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_formats_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_formats_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_formats_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_formats_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_formats_table->verticalHeader()->setVisible(false);
    m_formats_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_formats_table->setAlternatingRowColors(true);
    m_formats_table->setStyleSheet(QStringLiteral(
        "QTableWidget { background-color: #1e2227; gridline-color: #282c34; border: 1px solid #3e4451; border-radius: 4px; }"
        "QTableWidget::item { padding: 4px 6px; }"
    ));

    const auto formats = platform::DesktopIntegration::supported_formats();
    m_formats_table->setRowCount(static_cast<int>(formats.size()));

    for (int row = 0; row < static_cast<int>(formats.size()); ++row) {
        const auto& fmt = formats[row];

        auto* chk = new QCheckBox(m_formats_table);
        chk->setChecked(fmt.default_checked);
        m_format_checkboxes.append(chk);

        auto* chk_widget = new QWidget(m_formats_table);
        auto* chk_layout = new QHBoxLayout(chk_widget);
        chk_layout->setAlignment(Qt::AlignCenter);
        chk_layout->setContentsMargins(0, 0, 0, 0);
        chk_layout->addWidget(chk);
        m_formats_table->setCellWidget(row, 0, chk_widget);

        auto* ext_item = new QTableWidgetItem(QStringLiteral(".%1").arg(fmt.ext));
        ext_item->setTextAlignment(Qt::AlignCenter);
        ext_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        ext_item->setFont(QFont("Monospace", 9, QFont::Bold));
        m_formats_table->setItem(row, 1, ext_item);

        auto* desc_item = new QTableWidgetItem(fmt.description);
        desc_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_formats_table->setItem(row, 2, desc_item);

        auto* cat_item = new QTableWidgetItem(fmt.category);
        cat_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_formats_table->setItem(row, 3, cat_item);

        auto* status_item = new QTableWidgetItem();
        status_item->setTextAlignment(Qt::AlignCenter);
        status_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_formats_table->setItem(row, 4, status_item);
    }

    assoc_layout->addWidget(m_formats_table);

    // Table controls row
    auto* ctrl_row = new QHBoxLayout();
    auto* btn_all = new QPushButton(QStringLiteral("Select All"), group_assoc);
    connect(btn_all, &QPushButton::clicked, this, [this]() { onSelectAll(true); });
    ctrl_row->addWidget(btn_all);

    auto* btn_none = new QPushButton(QStringLiteral("Deselect All"), group_assoc);
    connect(btn_none, &QPushButton::clicked, this, [this]() { onSelectAll(false); });
    ctrl_row->addWidget(btn_none);

    ctrl_row->addStretch();

    auto* btn_register = new QPushButton(QStringLiteral("✦ Register Selected Formats"), group_assoc);
    btn_register->setStyleSheet(QStringLiteral("QPushButton { background-color: #2e7d32; color: white; font-weight: bold; padding: 5px 12px; border-radius: 4px; } QPushButton:hover { background-color: #388e3c; }"));
    connect(btn_register, &QPushButton::clicked, this, &DesktopIntegrationDialog::onRegisterSelected);
    ctrl_row->addWidget(btn_register);

    auto* btn_unregister = new QPushButton(QStringLiteral("Unregister Selected"), group_assoc);
    btn_unregister->setStyleSheet(QStringLiteral("QPushButton { background-color: #5c3535; color: #ffaaaa; padding: 5px 12px; border-radius: 4px; } QPushButton:hover { background-color: #7a4242; }"));
    connect(btn_unregister, &QPushButton::clicked, this, &DesktopIntegrationDialog::onUnregisterSelected);
    ctrl_row->addWidget(btn_unregister);

    assoc_layout->addLayout(ctrl_row);
    root_layout->addWidget(group_assoc);

    // ── Bottom status / dismiss ──────────────────────────────────────────────
    m_info_label = new QLabel(this);
    m_info_label->setStyleSheet(QStringLiteral("color: #61afef; font-size: 11px;"));
    root_layout->addWidget(m_info_label);

    auto* bottom_row = new QHBoxLayout();
    bottom_row->addStretch();
    auto* btn_close = new QPushButton(QStringLiteral("Close"), this);
    btn_close->setFixedWidth(100);
    connect(btn_close, &QPushButton::clicked, this, &QDialog::accept);
    bottom_row->addWidget(btn_close);
    root_layout->addLayout(bottom_row);
}

void DesktopIntegrationDialog::refresh_status() {
    bool installed = platform::DesktopIntegration::is_desktop_entry_installed();
    if (installed) {
        m_status_label->setText(QStringLiteral("Desktop Entry: Installed and Active"));
        m_status_label->setStyleSheet(QStringLiteral("color: #98c379; font-weight: bold;"));
        m_btn_install_entry->setText(QStringLiteral("Update Desktop Entry"));
        m_btn_uninstall_entry->setEnabled(true);
    } else {
        m_status_label->setText(QStringLiteral("Desktop Entry: Not Installed"));
        m_status_label->setStyleSheet(QStringLiteral("color: #e5c07b; font-weight: bold;"));
        m_btn_install_entry->setText(QStringLiteral("Install Desktop Entry"));
        m_btn_uninstall_entry->setEnabled(false);
    }

    const auto formats = platform::DesktopIntegration::supported_formats();
    for (int row = 0; row < static_cast<int>(formats.size()); ++row) {
        const auto& fmt = formats[row];
        bool is_assoc = platform::DesktopIntegration::is_format_associated(fmt.ext);
        auto* status_item = m_formats_table->item(row, 4);
        if (status_item) {
            if (is_assoc) {
                status_item->setText(QStringLiteral("✓ Associated"));
                status_item->setForeground(QColor(0x98, 0xc3, 0x79));
            } else {
                status_item->setText(QStringLiteral("Not Associated"));
                status_item->setForeground(QColor(0x7f, 0x84, 0x8e));
            }
        }
    }
}

void DesktopIntegrationDialog::onInstallDesktopEntry() {
    QString err;
    if (platform::DesktopIntegration::install_desktop_entry(&err)) {
        m_info_label->setText(QStringLiteral("Successfully installed desktop entry and system MIME definitions."));
        refresh_status();
    } else {
        QMessageBox::warning(this, QStringLiteral("Installation Failed"),
                             QStringLiteral("Could not install desktop entry:\n%1").arg(err));
    }
}

void DesktopIntegrationDialog::onUninstallDesktopEntry() {
    QString err;
    if (platform::DesktopIntegration::uninstall_desktop_entry(&err)) {
        m_info_label->setText(QStringLiteral("Desktop entry and MIME registrations removed."));
        refresh_status();
    } else {
        QMessageBox::warning(this, QStringLiteral("Uninstall Failed"),
                             QStringLiteral("Could not remove desktop entry:\n%1").arg(err));
    }
}

void DesktopIntegrationDialog::onSelectAll(bool select) {
    for (auto* chk : m_format_checkboxes) {
        if (chk) chk->setChecked(select);
    }
}

void DesktopIntegrationDialog::onRegisterSelected() {
    const auto formats = platform::DesktopIntegration::supported_formats();
    QStringList to_register;
    for (int i = 0; i < m_format_checkboxes.size() && i < static_cast<int>(formats.size()); ++i) {
        if (m_format_checkboxes[i]->isChecked()) {
            to_register.append(formats[i].ext);
        }
    }

    if (to_register.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Nothing Selected"),
                                QStringLiteral("Please check at least one file format to associate."));
        return;
    }

    QString err;
    if (platform::DesktopIntegration::register_file_associations(to_register, &err)) {
        m_info_label->setText(
            QStringLiteral("Registered %1 file extension(s) with Ruby Studio.").arg(to_register.size()));
        refresh_status();
    } else {
        QMessageBox::warning(this, QStringLiteral("Registration Failed"),
                             QStringLiteral("Failed to register file associations:\n%1").arg(err));
    }
}

void DesktopIntegrationDialog::onUnregisterSelected() {
    const auto formats = platform::DesktopIntegration::supported_formats();
    QStringList to_unregister;
    for (int i = 0; i < m_format_checkboxes.size() && i < static_cast<int>(formats.size()); ++i) {
        if (m_format_checkboxes[i]->isChecked()) {
            to_unregister.append(formats[i].ext);
        }
    }

    if (to_unregister.isEmpty()) return;

    QString err;
    if (platform::DesktopIntegration::unregister_file_associations(to_unregister, &err)) {
        m_info_label->setText(
            QStringLiteral("Unregistered %1 file extension(s).").arg(to_unregister.size()));
        refresh_status();
    } else {
        QMessageBox::warning(this, QStringLiteral("Unregister Failed"),
                             QStringLiteral("Failed to unregister associations:\n%1").arg(err));
    }
}

void DesktopIntegrationDialog::onThemeChanged(int index) {
    if (!m_theme_combo) return;
    int theme_val = m_theme_combo->itemData(index).toInt();
    ruby::theme::apply_theme(static_cast<ruby::theme::ThemeId>(theme_val));
}

} // namespace ruby::editor

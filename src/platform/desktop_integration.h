#pragma once
// ============================================================================
// desktop_integration.h — Cross-Platform Desktop Entry & File Associations
// ============================================================================

#include <QString>
#include <QStringList>
#include <vector>

namespace platform {

struct FileFormatSpec {
    QString ext;              // Extension without leading dot (e.g. "pod")
    QString mime_type;        // MIME type identifier (e.g. "model/x-swordigo-pod")
    QString description;      // User-visible description (e.g. "PowerVR 3D Model")
    QString category;         // Grouping category (e.g. "Swordigo Formats")
    bool default_checked = true;
};

class DesktopIntegration {
public:
    static std::vector<FileFormatSpec> supported_formats();

    // Desktop Entry / App Registration
    static bool is_desktop_entry_installed();
    static bool install_desktop_entry(QString* err_msg = nullptr);
    static bool uninstall_desktop_entry(QString* err_msg = nullptr);

    // File Associations
    static bool is_format_associated(const QString& ext);
    static bool register_file_associations(const QStringList& exts, QString* err_msg = nullptr);
    static bool unregister_file_associations(const QStringList& exts, QString* err_msg = nullptr);

    // Helpers
    static QString get_executable_path();
    static QString get_icon_source_path();
    static QString get_installed_desktop_file_path();
};

} // namespace platform

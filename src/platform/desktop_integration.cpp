#include "platform/desktop_integration.h"
#include "platform/os_external.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#endif

namespace platform {

std::vector<FileFormatSpec> DesktopIntegration::supported_formats() {
    return {
        // Swordigo native formats
        {"pod",   "model/x-swordigo-pod",       "PowerVR 3D Model",           "Swordigo Formats", true},
        {"pvr",   "image/x-pvr",                "PowerVR Compressed Texture", "Swordigo Formats", true},
        {"tex",   "image/x-pvr",                "PowerVR Texture",            "Swordigo Formats", true},
        {"scl",   "application/x-swordigo-scl", "Object Library Template",    "Swordigo Formats", true},
        {"scene", "application/x-swordigo-scene","Swordigo Binary Scene",     "Swordigo Formats", true},
        {"scn",   "application/x-swordigo-scene","Swordigo Binary Scene",     "Swordigo Formats", true},
        {"rbm",   "model/x-rubymesh",           "RubyMesh Geometry",          "Swordigo Formats", true},
        {"fr",    "application/x-filerift",     "FileRift Markup File",       "Swordigo Formats", true},

        // Universal 3D formats
        {"glb",   "model/gltf-binary",          "glTF Binary 3D Model",       "Universal 3D Formats", true},
        {"gltf",  "model/gltf+json",            "glTF Standard 3D Model",     "Universal 3D Formats", true},
        {"fbx",   "model/x-fbx",                "Autodesk FBX 3D Model",      "Universal 3D Formats", true},
        {"obj",   "model/obj",                  "Wavefront 3D Model",         "Universal 3D Formats", false}
    };
}

QString DesktopIntegration::get_executable_path() {
    QString exe = QCoreApplication::applicationFilePath();
    if (!exe.isEmpty() && QFileInfo::exists(exe)) {
        return QDir::cleanPath(exe);
    }
    std::string fallback = os_external::exe_dir();
    if (!fallback.empty()) {
#ifdef _WIN32
        return QDir::cleanPath(QString::fromStdString(fallback + "/ruby_gg.exe"));
#else
        return QDir::cleanPath(QString::fromStdString(fallback + "/ruby_gg"));
#endif
    }
    return QStringLiteral("ruby_gg");
}

QString DesktopIntegration::get_icon_source_path() {
    // 1. Try dev root from os_external
    std::string dev_root = os_external::dev_root_dir();
    if (!dev_root.empty()) {
        QString cand = QString::fromStdString(dev_root + "/src/assets/icon_app.png");
        if (QFileInfo::exists(cand)) return QDir::cleanPath(cand);
    }

    // 2. Search upwards from application dir
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 5; ++i) {
        QString cand = dir.filePath("src/assets/icon_app.png");
        if (QFileInfo::exists(cand)) return QDir::cleanPath(cand);
        cand = dir.filePath("packaging/deb/icon.png");
        if (QFileInfo::exists(cand)) return QDir::cleanPath(cand);
        if (!dir.cdUp()) break;
    }

    return QString();
}

QString DesktopIntegration::get_installed_desktop_file_path() {
#ifdef _WIN32
    return QString();
#else
    QString apps_dir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    return apps_dir + QStringLiteral("/ruby-studio-gg.desktop");
#endif
}

bool DesktopIntegration::is_desktop_entry_installed() {
#ifdef _WIN32
    HKEY hKey = nullptr;
    LONG res = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\RubyStudio.Asset", 0, KEY_READ, &hKey);
    if (res == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
#else
    QString desktop_path = get_installed_desktop_file_path();
    return !desktop_path.isEmpty() && QFileInfo::exists(desktop_path);
#endif
}

#ifdef _WIN32
static bool win_set_reg_str(HKEY root, const std::wstring& subkey, const std::wstring& value_name, const std::wstring& data) {
    HKEY hKey = nullptr;
    LONG res = RegCreateKeyExW(root, subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (res != ERROR_SUCCESS) return false;
    res = RegSetValueExW(hKey, value_name.empty() ? nullptr : value_name.c_str(), 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(data.c_str()),
                         static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    return res == ERROR_SUCCESS;
}
#endif

bool DesktopIntegration::install_desktop_entry(QString* err_msg) {
    const QString exe_path = get_executable_path();
    if (exe_path.isEmpty() || !QFileInfo::exists(exe_path)) {
        if (err_msg) *err_msg = "Executable path could not be resolved: " + exe_path;
        return false;
    }

#ifdef _WIN32
    std::wstring w_exe = QDir::toNativeSeparators(exe_path).toStdWString();
    std::wstring w_progid = L"Software\\Classes\\RubyStudio.Asset";

    // 1. Set ProgID description
    if (!win_set_reg_str(HKEY_CURRENT_USER, w_progid, L"", L"Swordigo Asset / 3D Model")) {
        if (err_msg) *err_msg = "Failed to create Registry ProgID key.";
        return false;
    }

    // 2. Set DefaultIcon
    std::wstring w_icon = L"\"" + w_exe + L"\",0";
    win_set_reg_str(HKEY_CURRENT_USER, w_progid + L"\\DefaultIcon", L"", w_icon);

    // 3. Set shell\open\command
    std::wstring w_cmd = L"\"" + w_exe + L"\" \"%1\"";
    win_set_reg_str(HKEY_CURRENT_USER, w_progid + L"\\shell\\open\\command", L"", w_cmd);

    // 4. Register application in Classes\Applications\ruby_gg.exe
    std::wstring w_app = L"Software\\Classes\\Applications\\ruby_gg.exe\\shell\\open\\command";
    win_set_reg_str(HKEY_CURRENT_USER, w_app, L"", w_cmd);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;

#else
    // 1. Ensure ~/.local/share/applications exists
    QString apps_dir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (apps_dir.isEmpty()) {
        apps_dir = QDir::homePath() + "/.local/share/applications";
    }
    QDir().mkpath(apps_dir);

    // 2. Install icon
    QString icon_arg = QStringLiteral("ruby-studio-gg");
    QString src_icon = get_icon_source_path();
    if (!src_icon.isEmpty()) {
        QString icon_dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                           "/icons/hicolor/256x256/apps";
        if (QDir().mkpath(icon_dir)) {
            QString dest_icon = icon_dir + "/ruby-studio-gg.png";
            QFile::remove(dest_icon);
            if (QFile::copy(src_icon, dest_icon)) {
                icon_arg = dest_icon;
            } else {
                icon_arg = src_icon;
            }
        } else {
            icon_arg = src_icon;
        }
    }

    // 3. Construct desktop entry
    QString desktop_path = get_installed_desktop_file_path();
    QFile file(desktop_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (err_msg) *err_msg = "Cannot open destination desktop file: " + desktop_path;
        return false;
    }

    QTextStream out(&file);
    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    out << "Name=Ruby Studio GG\n";
    out << "GenericName=Swordigo 3D & Scene Editor\n";
    out << "Comment=Editor and IDE for Swordigo reverse-engineering, modding, and 3D assets\n";
    out << "Exec=\"" << exe_path << "\" %F\n";
    out << "Icon=" << icon_arg << "\n";
    out << "Terminal=false\n";
    out << "Categories=Development;Graphics;3DGraphics;\n";
    out << "StartupWMClass=ruby_gg\n";
    out << "MimeType=model/x-swordigo-pod;image/x-pvr;application/x-swordigo-scl;application/x-swordigo-scene;model/gltf-binary;model/gltf+json;model/x-fbx;model/obj;model/x-rubymesh;application/x-filerift;\n";
    file.close();

    // 4. Install custom MIME XML package
    QString mime_dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/mime/packages";
    if (QDir().mkpath(mime_dir)) {
        QFile mime_file(mime_dir + "/ruby-studio-gg.xml");
        if (mime_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream m_out(&mime_file);
            m_out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            m_out << "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n";
            m_out << "  <mime-type type=\"model/x-swordigo-pod\">\n";
            m_out << "    <comment>PowerVR 3D Model</comment>\n";
            m_out << "    <glob pattern=\"*.pod\"/>\n";
            m_out << "    <glob pattern=\"*.POD\"/>\n";
            m_out << "  </mime-type>\n";
            m_out << "  <mime-type type=\"image/x-pvr\">\n";
            m_out << "    <comment>PowerVR Compressed Texture</comment>\n";
            m_out << "    <glob pattern=\"*.pvr\"/>\n";
            m_out << "    <glob pattern=\"*.PVR\"/>\n";
            m_out << "    <glob pattern=\"*.tex\"/>\n";
            m_out << "    <glob pattern=\"*.TEX\"/>\n";
            m_out << "  </mime-type>\n";
            m_out << "  <mime-type type=\"application/x-swordigo-scl\">\n";
            m_out << "    <comment>Swordigo Object Library Template</comment>\n";
            m_out << "    <glob pattern=\"*.scl\"/>\n";
            m_out << "    <glob pattern=\"*.SCL\"/>\n";
            m_out << "  </mime-type>\n";
            m_out << "  <mime-type type=\"application/x-swordigo-scene\">\n";
            m_out << "    <comment>Swordigo Binary Scene</comment>\n";
            m_out << "    <glob pattern=\"*.scene\"/>\n";
            m_out << "    <glob pattern=\"*.SCENE\"/>\n";
            m_out << "    <glob pattern=\"*.scn\"/>\n";
            m_out << "    <glob pattern=\"*.SCN\"/>\n";
            m_out << "  </mime-type>\n";
            m_out << "  <mime-type type=\"model/x-rubymesh\">\n";
            m_out << "    <comment>RubyMesh Geometry</comment>\n";
            m_out << "    <glob pattern=\"*.rbm\"/>\n";
            m_out << "    <glob pattern=\"*.rbc\"/>\n";
            m_out << "  </mime-type>\n";
            m_out << "</mime-info>\n";
            mime_file.close();

            // Run update-mime-database
            QProcess::execute("update-mime-database", {QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/mime"});
        }
    }

    // Run update-desktop-database
    QProcess::execute("update-desktop-database", {apps_dir});

    return true;
#endif
}

bool DesktopIntegration::uninstall_desktop_entry(QString* err_msg) {
#ifdef _WIN32
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegDeleteTreeW(hKey, L"RubyStudio.Asset");
        RegDeleteTreeW(hKey, L"Applications\\ruby_gg.exe");
        RegCloseKey(hKey);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
#else
    QString desktop_path = get_installed_desktop_file_path();
    if (QFileInfo::exists(desktop_path)) {
        if (!QFile::remove(desktop_path)) {
            if (err_msg) *err_msg = "Could not remove desktop file: " + desktop_path;
            return false;
        }
    }

    QString mime_file = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/mime/packages/ruby-studio-gg.xml";
    if (QFileInfo::exists(mime_file)) {
        QFile::remove(mime_file);
        QProcess::execute("update-mime-database", {QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/mime"});
    }

    QString apps_dir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    QProcess::execute("update-desktop-database", {apps_dir});
    return true;
#endif
}

bool DesktopIntegration::is_format_associated(const QString& ext) {
    const QString clean_ext = ext.startsWith('.') ? ext.mid(1) : ext;

#ifdef _WIN32
    std::wstring subkey = L"Software\\Classes\\." + clean_ext.toStdWString();
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[256];
        DWORD buf_bytes = sizeof(buf);
        DWORD type = 0;
        LONG r = RegQueryValueExW(hKey, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &buf_bytes);
        RegCloseKey(hKey);
        if (r == ERROR_SUCCESS && type == REG_SZ) {
            std::wstring val(buf);
            return val == L"RubyStudio.Asset";
        }
    }
    return false;
#else
    // Check ~/.config/mimeapps.list
    QString config_dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QString mimeapps_path = config_dir + "/mimeapps.list";
    if (!QFileInfo::exists(mimeapps_path)) {
        mimeapps_path = QDir::homePath() + "/.local/share/applications/mimeapps.list";
    }
    if (!QFileInfo::exists(mimeapps_path)) return false;

    // Find format spec for this extension
    QString target_mime;
    for (const auto& fmt : supported_formats()) {
        if (fmt.ext.compare(clean_ext, Qt::CaseInsensitive) == 0) {
            target_mime = fmt.mime_type;
            break;
        }
    }
    if (target_mime.isEmpty()) return false;

    QFile file(mimeapps_path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.startsWith(target_mime + "=") && line.contains("ruby-studio-gg.desktop")) {
                return true;
            }
        }
    }
    return false;
#endif
}

bool DesktopIntegration::register_file_associations(const QStringList& exts, QString* err_msg) {
    if (!is_desktop_entry_installed()) {
        if (!install_desktop_entry(err_msg)) {
            return false;
        }
    }

#ifdef _WIN32
    for (const QString& ext : exts) {
        const QString clean = ext.startsWith('.') ? ext.mid(1) : ext;
        std::wstring subkey = L"Software\\Classes\\." + clean.toStdWString();
        win_set_reg_str(HKEY_CURRENT_USER, subkey, L"", L"RubyStudio.Asset");
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;

#else
    // Determine target MIME types for specified extensions
    QStringList mimes;
    for (const QString& ext : exts) {
        const QString clean = ext.startsWith('.') ? ext.mid(1) : ext;
        for (const auto& fmt : supported_formats()) {
            if (fmt.ext.compare(clean, Qt::CaseInsensitive) == 0) {
                if (!mimes.contains(fmt.mime_type)) {
                    mimes.append(fmt.mime_type);
                }
                break;
            }
        }
    }

    if (mimes.isEmpty()) return true;

    // 1. Direct update into ~/.config/mimeapps.list
    QString config_dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir().mkpath(config_dir);
    QString mimeapps_path = config_dir + "/mimeapps.list";

    QStringList lines;
    QFile infile(mimeapps_path);
    if (infile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&infile);
        while (!in.atEnd()) {
            lines.append(in.readLine());
        }
        infile.close();
    }

    auto update_section = [&](const QString& sec_header) {
        int sec_idx = -1;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines[i].trimmed() == sec_header) {
                sec_idx = i;
                break;
            }
        }
        if (sec_idx == -1) {
            lines.append(QString());
            lines.append(sec_header);
            sec_idx = lines.size() - 1;
        }

        // Add or update mime entries
        for (const QString& m : mimes) {
            bool found = false;
            for (int j = sec_idx + 1; j < lines.size(); ++j) {
                if (lines[j].trimmed().startsWith("[") && lines[j].trimmed().endsWith("]")) {
                    break;
                }
                if (lines[j].trimmed().startsWith(m + "=")) {
                    // Update entry
                    lines[j] = m + "=ruby-studio-gg.desktop";
                    found = true;
                    break;
                }
            }
            if (!found) {
                lines.insert(sec_idx + 1, m + "=ruby-studio-gg.desktop");
            }
        }
    };

    update_section("[Default Applications]");
    update_section("[Added Associations]");

    QFile outfile(mimeapps_path);
    if (outfile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream out(&outfile);
        for (const QString& l : lines) {
            out << l << "\n";
        }
        outfile.close();
    }

    // 2. Also invoke xdg-mime default if available
    for (const QString& m : mimes) {
        QProcess::execute("xdg-mime", {"default", "ruby-studio-gg.desktop", m});
    }

    return true;
#endif
}

bool DesktopIntegration::unregister_file_associations(const QStringList& exts, QString* err_msg) {
#ifdef _WIN32
    for (const QString& ext : exts) {
        const QString clean = ext.startsWith('.') ? ext.mid(1) : ext;
        std::wstring subkey = L"Software\\Classes\\." + clean.toStdWString();
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ | KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            wchar_t buf[256];
            DWORD buf_bytes = sizeof(buf);
            DWORD type = 0;
            if (RegQueryValueExW(hKey, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &buf_bytes) == ERROR_SUCCESS) {
                if (std::wstring(buf) == L"RubyStudio.Asset") {
                    RegDeleteValueW(hKey, nullptr);
                }
            }
            RegCloseKey(hKey);
        }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;

#else
    QStringList mimes;
    for (const QString& ext : exts) {
        const QString clean = ext.startsWith('.') ? ext.mid(1) : ext;
        for (const auto& fmt : supported_formats()) {
            if (fmt.ext.compare(clean, Qt::CaseInsensitive) == 0) {
                if (!mimes.contains(fmt.mime_type)) {
                    mimes.append(fmt.mime_type);
                }
                break;
            }
        }
    }

    QString mimeapps_path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/mimeapps.list";
    if (!QFileInfo::exists(mimeapps_path)) return true;

    QStringList lines;
    QFile infile(mimeapps_path);
    if (infile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&infile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            bool remove = false;
            for (const QString& m : mimes) {
                if (line.trimmed().startsWith(m + "=") && line.contains("ruby-studio-gg.desktop")) {
                    remove = true;
                    break;
                }
            }
            if (!remove) {
                lines.append(line);
            }
        }
        infile.close();
    }

    QFile outfile(mimeapps_path);
    if (outfile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream out(&outfile);
        for (const QString& l : lines) {
            out << l << "\n";
        }
        outfile.close();
    }

    return true;
#endif
}

} // namespace platform

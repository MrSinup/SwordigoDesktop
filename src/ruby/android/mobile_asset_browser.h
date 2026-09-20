#pragma once
// ============================================================================
// mobile_asset_browser.h — ZArchiver-Grade Mobile File & Asset Manager
//   Full direct storage access across /sdcard, external SD, and app sandbox.
//   Provides advanced file operations: Cut, Copy, Paste, Rename, Delete,
//   Zip extraction/compression, New Folder/File, Storage Root switcher,
//   and search filtering.
// ============================================================================

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QDir>
#include <QString>
#include <vector>

namespace zip {
struct OutEntry;
}

namespace ruby::android {

class MobileAssetBrowser : public QWidget {
    Q_OBJECT

public:
    enum class SortMode { Name, Date, Size, Type };

    explicit MobileAssetBrowser(QWidget* parent = nullptr);
    ~MobileAssetBrowser() override = default;

    void set_root_path(const QString& dir_path);
    QString current_path() const { return m_current_dir.absolutePath(); }

public slots:
    void refresh();

signals:
    void sceneOpenRequested(const QString& file_path, bool visual_mode);
    void codeOpenRequested(const QString& file_path);
    void modelOpenRequested(const QString& file_path);
    void statusMessage(const QString& message);

private slots:
    void on_item_clicked(QListWidgetItem* item);
    void on_item_long_press(const QPoint& pos);
    void on_navigate_up();
    void on_path_entered();
    void on_storage_roots_menu();
    void on_sort_menu();
    void on_new_folder();
    void on_new_file();
    void on_clipboard_paste();
    void on_extract_archive(const QString& archive_path, bool to_subfolder = false);
    void on_view_archive(const QString& archive_path);
    void on_compress_zip(const QString& target_path);
    void on_rename_item(const QString& target_path);
    void on_delete_item(const QString& target_path);
    void on_show_properties(const QString& target_path);

private:
    void setup_ui();
    void populate_files();
    QString format_file_size(qint64 bytes) const;
    std::vector<QString> detect_storage_roots();

    static bool copy_dir_recursive(const QString& src, const QString& dst);
    static bool zip_folder_recursive(const QString& root_dir, const QString& current_dir, std::vector<zip::OutEntry>& entries);
    static qint64 calculate_dir_size(const QString& dir_path, int& out_files, int& out_dirs);

    QDir m_current_dir;
    QString m_filter_category = QStringLiteral("all");
    SortMode m_sort_mode = SortMode::Name;
    bool m_sort_ascending = true;
    bool m_show_hidden = false;

    // Clipboard for Cut/Copy/Paste
    struct ClipboardItem {
        QString path;
        bool is_cut = false;
    };
    ClipboardItem m_clipboard;

    // UI elements
    QLineEdit* m_path_edit = nullptr;
    QLineEdit* m_search_edit = nullptr;
    QListWidget* m_file_list = nullptr;
    QPushButton* m_btn_roots = nullptr;
    QPushButton* m_btn_up = nullptr;
    QPushButton* m_btn_paste = nullptr;
    QPushButton* m_btn_menu = nullptr;
    QPushButton* m_btn_sort = nullptr;
    QLabel* m_status_lbl = nullptr;
};

} // namespace ruby::android

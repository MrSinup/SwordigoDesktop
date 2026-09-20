#pragma once
// ============================================================================
// mobile_main_window.h — Mobile-First Main Window for Ruby GG Android
//   Root controller coordinating the Portrait Hub / Asset Browser,
//   the Single-Active-File Code Editor, and the Landscape 3D Viewport.
// ============================================================================

#include <QMainWindow>
#include <QStackedWidget>
#include <QString>
#include <memory>

namespace ruby::android {

class MobileAssetBrowser;
class MobileCodeEditor;
class MobileViewportWidget;

class MobileMainWindow : public QMainWindow {
    Q_OBJECT

public:
    enum PageIndex {
        PageHub = 0,
        PageCodeEditor = 1,
        PageVisualViewport = 2
    };

    explicit MobileMainWindow(QWidget* parent = nullptr);
    ~MobileMainWindow() override = default;

    void open_file(const QString& file_path, bool prefer_visual = false);

public slots:
    void show_hub();
    void show_code_editor(const QString& file_path = QString());
    void show_visual_viewport(const QString& file_path = QString());
    void on_storage_permission_updated();

private slots:
    void on_status_message(const QString& msg);

private:
    void request_orientation(bool landscape);
    bool check_save_dirty_file();

    QStackedWidget* m_stack = nullptr;
    MobileAssetBrowser* m_hub = nullptr;
    MobileCodeEditor* m_code_editor = nullptr;
    MobileViewportWidget* m_viewport = nullptr;

    QString m_active_file_path;
};

} // namespace ruby::android

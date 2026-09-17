#pragma once
// ============================================================================
// asset_browser_panel.h — Asset & Project Tree Browser
// ============================================================================

#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QFileSystemWatcher>

class QTimer;

namespace ruby::panels {

class AssetBrowserPanel : public QWidget {
    Q_OBJECT

public:
    explicit AssetBrowserPanel(QWidget* parent = nullptr);
    ~AssetBrowserPanel() override = default;

    void set_root_path(const QString& path);
    QString current_selected_folder() const;

    // Quick-sync: force the tree to re-list the root directory NOW. Used both
    // by the internal file watcher (debounced) and by host code that just
    // wrote assets (Ground Mesh Studio / FileRift / exporters) so brand-new
    // files appear immediately without waiting for the OS watcher.
    void refresh_now();

    // Full file browser navigation API
    void navigate_to(const QString& path, bool record_history = true);
    void navigate_back();
    void navigate_forward();
    void navigate_up();
    void navigate_home();
    void select_and_reveal_file(const QString& file_path);

signals:
    void fileSelected(const QString& file_path);
    void newFileRequested(const QString& target_folder);
    void convertModelRequested(const QString& file_path);
    void addModelToSceneRequested(const QString& file_path);
    void directoryNavigated(const QString& new_dir);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onItemDoubleClicked(const QModelIndex& index);
    void onFilterChanged(const QString& filter);
    void onCustomContextMenu(const QPoint& pt);
    void onDirectoryChanged(const QString& path);
    void onDebouncedRefresh();
    void onRootPoll();
    void onPathBarReturnPressed();

private:
    void update_column_widths();
    void rewatch_root();      // (re)point the QFileSystemWatcher at the root
    void schedule_refresh(const QString& path);
    void update_nav_buttons_state();

    QFileSystemModel* m_file_model = nullptr;
    QTreeView* m_tree_view = nullptr;
    QLineEdit* m_search_box = nullptr;

    // Navigation UI
    class QToolButton* m_btn_back = nullptr;
    class QToolButton* m_btn_fwd = nullptr;
    class QToolButton* m_btn_up = nullptr;
    class QToolButton* m_btn_home = nullptr;
    QLineEdit* m_path_bar = nullptr;

    QStringList m_history_back;
    QStringList m_history_forward;
    QString m_home_path;

    // Quick-sync machinery: an explicit watcher + debounced refresh guarantee
    // files created outside the model (other tools, file managers, the engine)
    // appear in the browser immediately. Also retries a root path that did not
    // exist when set_root_path() was called (the model alone never recovers
    // from that).
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_refresh_timer = nullptr;   // debounce bursty fs events
    QTimer* m_root_poll = nullptr;       // wait for a late-appearing root
    QString m_pending_refresh;
};

} // namespace ruby::panels

// asset_sync_test.cpp — quick-sync acceptance test for AssetBrowserPanel.
// Covers the two gaps QFileSystemModel alone cannot handle:
//   1. the root directory appears AFTER set_root_path() (model never recovers)
//   2. files created by an external process must appear without manual refresh
#include <QApplication>
#include <QTimer>
#include <QTreeView>
#include <QFileSystemModel>
#include <QDir>
#include <QFileInfo>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "ruby/panels/asset_browser_panel.h"

namespace fs = std::filesystem;

// True when the panel's tree model has an entry for `path` right now.
static bool model_has(const ruby::panels::AssetBrowserPanel& panel, const QString& path) {
    const QTreeView* tree = panel.findChild<QTreeView*>();
    if (!tree || !tree->model()) return false;
    auto* fs_model = qobject_cast<const QFileSystemModel*>(tree->model());
    if (!fs_model) return false;
    return fs_model->index(path).isValid();
}

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    const QString root = "/tmp/qsync_panel_test";
    fs::remove_all(root.toStdString());

    ruby::panels::AssetBrowserPanel panel;
    panel.resize(400, 500);
    panel.show();
    app.processEvents();

    // Root does NOT exist yet when the panel is pointed at it — the scenario
    // QFileSystemModel alone would never recover from.
    panel.set_root_path(root);
    app.processEvents();
    const bool rootListed = model_has(panel, root);
    std::printf("root set while missing: listed=%d exists=%d\n",
                (int)rootListed, (int)QFileInfo::exists(root));

    // After 1s an external process creates the tree.
    QTimer::singleShot(1000, &app, [&]() {
        fs::create_directories((root + "/sub").toStdString());
        std::ofstream((root + "/town.scene").toStdString()) << "scene";
        std::ofstream((root + "/sub/hero.pod").toStdString()) << "pod";
    });

    // At 3s the panel must show everything without any manual refresh.
    QTimer::singleShot(3000, &app, [&]() {
        app.processEvents();
        const bool lateRoot = model_has(panel, root + "/town.scene");
        const bool nested = model_has(panel, root + "/sub/hero.pod");
        std::printf("late root file in model=%d nested file in model=%d\n",
                    (int)lateRoot, (int)nested);
        app.exit(lateRoot && nested ? 0 : 1);
    });

    const int rc = app.exec();
    std::printf(rc == 0 ? "PASS: asset browser quick sync works\n"
                        : "FAIL: quick-sync assertion(s) failed\n");
    fs::remove_all(root.toStdString());
    return rc;
}
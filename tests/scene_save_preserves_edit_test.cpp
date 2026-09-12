// scene_save_preserves_edit_test.cpp — regression test for the classic
// "I moved something and Ctrl+S reverted my change" bug.
//
// Scenario reproduced here:
//   1. open a .scene document (3D viewport)
//   2. view the scene's FileRift markup in the Script IDE (so the IDE is
//      "showing" the scene when the edit happens)
//   3. go back to the 3D viewport and move an object (structured RAM edit)
//   4. press Ctrl+S (onSaveFile)
//
// The bug: sync_scene_text_buffer() refreshed the cached FileRift buffer but
// NOT the IDE's visible text, so scene_has_user_text_edits() compared the
// fresh buffer against the stale visible text and misread our own sync
// artifact as user typing — routing the save through the stale text writer,
// which reverted the edit on disk.
//
// The test drives the real RubyMainWindow (offscreen Qt + Mesa swrast GL) via
// its meta-object-invokable private slots, then asserts the moved position
// survives on disk after onSaveFile. It fails on the pre-fix code.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <functional>
#include <string>

#include "ruby/editor/ruby_main_window.h"
#include "ruby/editor/script_ide_widget.h"
#include "ruby/viewport/viewport_3d_widget.h"
#include "tools/scene_loader.h"

std::string g_instance_assets_dir = "assets";

static QString find_source_scene() {
#ifdef SWORDIGO_SRC_DIR
    const QString a = QString::fromUtf8(SWORDIGO_SRC_DIR) + "/assets/v3_Wasteland.scene";
    if (QFileInfo::exists(a)) return a;
#endif
    if (QFileInfo::exists("src/assets/v3_Wasteland.scene")) return "src/assets/v3_Wasteland.scene";
    return QString();
}

static bool wait_for(const std::function<bool()>& cond, int ms = 8000) {
    const int step = 20;
    for (int waited = 0; waited < ms; waited += step) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (cond()) return true;
        QThread::msleep(step);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return cond();
}

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    const QString src = find_source_scene();
    if (src.isEmpty()) {
        std::printf("SKIP: v3_Wasteland.scene not found\n");
        return 0;
    }
    // Never touch the shipped asset: work on a copy.
    const QString work = "/tmp/ruby_save_edit_test.scene";
    {
        QFile in(src);
        QFile out(work);
        if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::printf("FAIL: cannot stage scene copy\n");
            return 1;
        }
        out.write(in.readAll());
    }

    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
    };

    // Leak intentionally: the QOpenGLWidget destructor segfaults on offscreen
    // teardown when the GL function table is gone (pre-existing, app-only).
    auto* win = new ruby::RubyMainWindow();
    win->resize(1280, 800);
    win->show();
    QCoreApplication::processEvents();

    // 1) Open the scene document through the real asset-browser path.
    QMetaObject::invokeMethod(win, "onFileSelectedInBrowser",
                              Qt::DirectConnection, Q_ARG(QString, work));
    check(wait_for([&]() {
        return win->findChild<ruby::viewport::Viewport3DWidget*>() != nullptr &&
               win->findChild<ruby::viewport::Viewport3DWidget*>()->has_scene() &&
               win->findChild<ruby::viewport::Viewport3DWidget*>()->scene().filepath == work.toStdString();
    }), "scene loaded into viewport");

    // The window hosts several QTabWidgets (tools, engine panel); pick the
    // central MODE tabs (the one with the 3D Viewport / Script IDE pages).
    QTabWidget* tabs = nullptr;
    for (QTabWidget* t : win->findChildren<QTabWidget*>()) {
        bool has_3d = false, has_ide = false;
        for (int i = 0; i < t->count(); ++i) {
            if (t->tabText(i).contains("3D Viewport")) has_3d = true;
            if (t->tabText(i).contains("Script IDE")) has_ide = true;
        }
        if (has_3d && has_ide) { tabs = t; break; }
    }
    auto* vp = win->findChild<ruby::viewport::Viewport3DWidget*>();
    check(tabs != nullptr && vp != nullptr, "found mode tabs + viewport");

    // 2) Peek at the FileRift markup in the Script IDE (IDE now shows the scene).
    if (tabs) tabs->setCurrentIndex(1);   // Script IDE
    // Wait for the ASYNC decode to fully land — current_file_path() is set
    // synchronously but the text arrives via the worker thread. Editing before
    // that (the late-decode race) is itself a separate bug vector.
    auto* ide = win->findChild<ruby::editor::ScriptIDEWidget*>();
    check(wait_for([&]() {
        return ide != nullptr && ide->current_file_path() == work &&
               ide->full_text().size() > 0;
    }), "scene markup loaded into Script IDE");

    // 3) Back to the 3D viewport; move object 0 by +50 in X (structured RAM edit).
    if (tabs) tabs->setCurrentIndex(0);
    const float original_x = vp && vp->has_scene() ? vp->scene().objects[0].pos_x : -9999.0f;
    check(original_x > -9990.0f, "scene has object 0");
    if (vp && vp->has_scene() && !vp->scene().objects.empty()) {
        vp->editable_scene().objects[0].pos_x += 50.0f;
        vp->refresh_edited_object();
        vp->update();
        // Mark the doc dirty + sync the text view the way a gizmo commit does.
        QMetaObject::invokeMethod(win, "on_viewport_scene_edited", Qt::DirectConnection);
        check(wait_for([&]() { return vp->scene().objects[0].pos_x > original_x + 49.0f; }),
              "viewport RAM holds the moved position");
    }

    // 4) Ctrl+S. Must take the STRUCTURED save path and persist the move.
    QMetaObject::invokeMethod(win, "onSaveFile", Qt::DirectConnection);
    QCoreApplication::processEvents();

    const av::SceneData saved = av::scene_load(work.toStdString());
    check(!saved.objects.empty(), "saved file parses");
    if (!saved.objects.empty()) {
        check(std::fabs(saved.objects[0].pos_x - (original_x + 50.0f)) < 0.5f,
              "moved position survived the save (bug: Ctrl+S reverted the edit)");
        std::printf("  object0 pos_x: before=%.1f expected=%.1f on-disk=%.1f\n",
                    (double)original_x, (double)(original_x + 50.0f),
                    (double)saved.objects[0].pos_x);
    }

    std::printf(failures == 0 ? "PASS: Ctrl+S preserves the viewport edit\n"
                              : "FAIL: %d assertion(s)\n", failures);
    QFile::remove(work);
    return failures == 0 ? 0 : 1;
}
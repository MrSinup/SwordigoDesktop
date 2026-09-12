// scene_perf_driver.cpp — TEMPORARY diagnostic: load the user's real large
// .scene into the real RubyMainWindow (offscreen Qt + swrast) and time every
// step of the reported freeze flow: load → select → gizmo-style transform
// burst → debounce flush → save. Prints per-phase wall-clock ms to stdout.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPixmap>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>

#include "ruby/core/project_context.h"
#include "ruby/editor/ruby_main_window.h"
#include "ruby/editor/script_ide_widget.h"
#include "ruby/panels/scene_hierarchy_panel.h"
#include "ruby/panels/template_palette_panel.h"
#include "ruby/viewport/viewport_3d_widget.h"
#include "tools/scene_loader.h"

std::string g_instance_assets_dir = "assets";

static double ms(std::chrono::steady_clock::time_point& t0) {
    const auto t1 = std::chrono::steady_clock::now();
    const double d = std::chrono::duration<double, std::milli>(t1 - t0).count();
    t0 = t1;
    return d;
}

static bool wait_for(const std::function<bool()>& cond, int ms = 30000) {
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
    if (qEnvironmentVariableIsSet("RUBY_GG_FORCE_OFFSCREEN"))
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    const char* def = "/home/quantumcreeper/.local/share/swordigo-desktop/assets/resources/mountain.scene";
    const QString src = argc > 1 ? argv[1] : def;
    if (!QFileInfo::exists(src)) { std::printf("SKIP: %s not found\n", qPrintable(src)); return 0; }
    const QString work = "/tmp/ruby_perf_driver.scene";
    {
        QFile in(src); QFile out(work);
        if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::printf("FAIL: cannot stage copy\n"); return 1;
        }
        out.write(in.readAll());
    }
    std::printf("staged %s (%.1f MB) -> %s\n", qPrintable(src),
                (double)QFileInfo(work).size() / 1048576.0, qPrintable(work));

    auto* win = new ruby::RubyMainWindow();
    win->resize(1280, 800);
    win->show();
    QCoreApplication::processEvents();

    auto* vp = win->findChild<ruby::viewport::Viewport3DWidget*>();
    std::printf("viewport found: %s\n", vp ? "yes" : "NO");

    // ── 1. open the scene document (asset-browser path) ─────────────────────
    auto t0 = std::chrono::steady_clock::now();
    QMetaObject::invokeMethod(win, "onFileSelectedInBrowser",
                              Qt::DirectConnection, Q_ARG(QString, work));
    const bool loaded = wait_for([&]() {
        return vp && vp->has_scene() && vp->scene().filepath == work.toStdString();
    });
    std::printf("[load] scene ready: %s | wall: %.0f ms (includes async scene load)\n",
                loaded ? "yes" : "NO", ms(t0));

    if (!loaded || !vp || !vp->has_scene() || vp->scene().objects.empty()) {
        std::printf("FAIL: no scene\n"); return 1;
    }
    std::printf("[load] objects=%zu ground_tris=~%.0f\n", vp->scene().objects.size(),
                [] (const av::SceneData& s) {
                    double t = 0; for (const auto& o : s.objects)
                        for (const auto& gm : o.ground_meshes)
                            t += gm.indices.empty() ? gm.positions.size() / 9 : gm.indices.size() / 3;
                    return t;
                }(vp->scene()));

    // ── 2. select object 0 (the click path) ─────────────────────────────────
    t0 = std::chrono::steady_clock::now();
    QMetaObject::invokeMethod(win, "set_selected_object", Qt::DirectConnection, Q_ARG(int, 0));
    QCoreApplication::processEvents();
    std::printf("[select] set_selected_object(0) + event loop: %.1f ms\n", ms(t0));

    // ── 3. transform burst (gizmo drag): 30 mutations, then flush ───────────
    t0 = std::chrono::steady_clock::now();
    float base_x = vp->scene().objects[0].pos_x;
    for (int i = 0; i < 30; ++i) {
        vp->editable_scene().objects[0].pos_x = base_x + i;
        vp->refresh_edited_object();
        vp->update();
        QMetaObject::invokeMethod(win, "on_viewport_scene_edited", Qt::DirectConnection);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    std::printf("[drag] 30 per-move edits (mutate+refresh+sceneEdited+events): %.1f ms\n", ms(t0));

    // ── 4. debounce flush pieces (drag-end freeze) ──────────────────────────
    // sync_scene_text_buffer main-thread part: the SceneData deep copy.
    t0 = std::chrono::steady_clock::now();
    QMetaObject::invokeMethod(win, "sync_scene_text_buffer", Qt::DirectConnection,
                              Q_ARG(QString, work), Q_ARG(bool, false));
    std::printf("[flush] sync_scene_text_buffer (copy + spawn): %.1f ms\n", ms(t0));
    QCoreApplication::processEvents();

    // Hierarchy rebuild.
    t0 = std::chrono::steady_clock::now();
    auto* hier = win->findChild<ruby::panels::SceneHierarchyPanel*>();
    hier->set_scene(vp->scene());
    hier->select_object(0);
    std::printf("[flush] hierarchy set_scene + select: %.1f ms\n", ms(t0));
    QCoreApplication::processEvents();

    // Template inspector selection refresh (cheap path).
    t0 = std::chrono::steady_clock::now();
    QMetaObject::invokeMethod(win, "refresh_scene_templates", Qt::DirectConnection,
                              Q_ARG(bool, false));
    std::printf("[flush] refresh_scene_templates(false): %.1f ms\n", ms(t0));
    QCoreApplication::processEvents();

    // Replicate refresh_scene_templates(true) — the exact body — with timers.
    {
        const av::SceneData& scene = vp->scene();
        std::printf("[flush] imported_library_paths (%zu):\n", scene.imported_library_paths.size());
        for (const auto& p : scene.imported_library_paths)
            std::printf("    %s\n", p.c_str());
        QStringList roots;
        const std::string project_dir = ruby::core::ProjectContext::instance().project_dir();
        if (!project_dir.empty()) roots << QString::fromStdString(project_dir);
        const QString scene_path = QString::fromStdString(scene.filepath);
        roots << QFileInfo(scene_path).absolutePath();
        roots << QFileInfo(scene_path).absolutePath() + "/resources";
        for (const auto& lib : scene.imported_library_paths) {
            if (lib.empty()) continue;
            roots << QString::fromStdString(fs::path(lib).parent_path().string());
        }
        const QString home = QDir::homePath();
        roots << home + "/resources"
              << home + "/SwordigoDesktop/assets"
              << home + "/SwordigoDesktop/resources"
              << home + "/SwordigoRefresh/assets/resources"
              << home + "/.local/share/swordigo-desktop/assets";
        std::vector<std::string> root_list;
        for (const QString& q : roots) root_list.push_back(q.toStdString());

        t0 = std::chrono::steady_clock::now();
        auto* palette = win->findChild<ruby::panels::TemplatePalettePanel*>();
        palette->refresh(scene, roots);
        std::printf("[flush] palette refresh (scan + tree build): %.1f ms\n", ms(t0));

        t0 = std::chrono::steady_clock::now();
        const auto scanned = av::scan_template_sources(root_list, scene);
        std::printf("[flush] scan_template_sources alone: %.1f ms (%zu entries)\n", ms(t0), scanned.size());

        t0 = std::chrono::steady_clock::now();
        size_t absorbed = 0;
        for (const auto& lib : scene.object_libraries) {
            for (auto& e : av::scl_load_templates(lib)) { ++absorbed; (void)e; }
        }
        for (const auto& lib : scene.external_libraries) {
            for (auto& e : av::scl_load_templates(lib)) { ++absorbed; (void)e; }
        }
        std::printf("[flush] absorb scene libs: %.1f ms (%zu)\n", ms(t0), absorbed);

        t0 = std::chrono::steady_clock::now();
        size_t read_scl = 0;
        for (const auto& src : scanned) {
            if (src.kind != av::TemplateSourceEntry::Template || src.source_path.empty()) continue;
            std::ifstream in(src.source_path, std::ios::binary);
            if (!in) continue;
            std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            for (auto& e : av::scl_load_templates(bytes)) { ++read_scl; (void)e; }
        }
        std::printf("[flush] absorb scanned .scl files: %.1f ms (%zu templates)\n", ms(t0), read_scl);
    }

    // Commit of the coalesced undo entry (serialize×2).
    t0 = std::chrono::steady_clock::now();
    QMetaObject::invokeMethod(win, "commit_pending_scene_undo", Qt::DirectConnection);
    std::printf("[flush] commit_pending_scene_undo: %.1f ms\n", ms(t0));
    QCoreApplication::processEvents();

    // The queued async apply (worker decode result lands on the main thread).
    t0 = std::chrono::steady_clock::now();
    QThread::msleep(2000);
    QCoreApplication::processEvents();
    std::printf("[flush] queued apply settle (2s): %.1f ms\n", ms(t0));

    // ── 4b. one full real repaint (paintGL) ─────────────────────────────────
    t0 = std::chrono::steady_clock::now();
    QPixmap px = vp->grab();
    std::printf("[paint] grabFramebuffer full repaint: %.1f ms\n", ms(t0));
    px.size();

    // ── 5. save pieces (Ctrl+S freeze) ──────────────────────────────────────
    t0 = std::chrono::steady_clock::now();
    const av::SceneData sc_copy = vp->scene();
    std::printf("[save] SceneData deep copy: %.1f ms\n", ms(t0));
    t0 = std::chrono::steady_clock::now();
    std::string serr;
    const bool wrote = av::scene_save(work.toStdString(), sc_copy, &serr);
    std::printf("[save] scene_save write: %.1f ms (%s)\n", ms(t0), wrote ? "ok" : "ERR");
    t0 = std::chrono::steady_clock::now();
    const bool applied = vp->apply_scene_data(sc_copy, work.toStdString());
    std::printf("[save] apply_scene_data in-place: %.1f ms (%s)\n", ms(t0), applied ? "ok" : "FAIL");
    QCoreApplication::processEvents();
    t0 = std::chrono::steady_clock::now();
    QMetaObject::invokeMethod(win, "onSaveFile", Qt::DirectConnection);
    QCoreApplication::processEvents();
    QThread::msleep(300);
    QCoreApplication::processEvents();
    std::printf("[save] onSaveFile whole: %.1f ms\n", ms(t0));

    const av::SceneData saved = av::scene_load(work.toStdString());
    std::printf("[save] on-disk object0 pos_x: %.1f (expected %.1f) %s\n",
                saved.objects.empty() ? -9999.0 : (double)saved.objects[0].pos_x,
                (double)(base_x + 29.0f),
                (!saved.objects.empty() && std::fabs(saved.objects[0].pos_x - (base_x + 29.0f)) < 0.5f)
                    ? "OK" : "MISMATCH");

    std::printf("done\n");
    QFile::remove(work);
    return 0;
}
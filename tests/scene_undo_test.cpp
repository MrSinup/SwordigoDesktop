// scene_undo_test.cpp — scene-wide snapshot undo (master TODO 1.2) and the
// component-paste wiring (1.1), driven through the real RubyMainWindow.
//
// 1.2: every structural/component edit lands on the per-scene QUndoStack as a
// full scene-binary snapshot (web-editor model). Undo/redo restores the whole
// scene — structural changes through a rebuild from the snapshot bytes,
// structure-preserving ones (visibility, component ops) through the fast
// in-place apply that keeps the camera.
//
// 1.1: the inspector's Paste button emits componentPasted(object, component);
// the window must insert it with a fresh type id AND make it undoable. The
// signal is invoked here end-to-end (registering the custom type for the
// meta-call) and the paste is asserted visible + undoable.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMetaType>
#include <QThread>
#include <QTimer>
#include <QPushButton>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

#include "ruby/editor/ruby_main_window.h"
#include "ruby/panels/inspector_panel.h"
#include "ruby/viewport/viewport_3d_widget.h"
#include "tools/scene_loader.h"

// Required by libswcore (asset resolution global; other tests define it too).
std::string g_instance_assets_dir = "assets";

static QString find_source_scene() {
#ifdef SWORDIGO_SRC_DIR
    const QString a = QString::fromUtf8(SWORDIGO_SRC_DIR) + "/assets/v3_Wasteland.scene";
    if (QFileInfo::exists(a)) return a;
#endif
    if (QFileInfo::exists("src/assets/v3_Wasteland.scene")) return "src/assets/v3_Wasteland.scene";
    return QString();
}

static bool wait_for(const std::function<bool()>& cond, int ms = 15000) {
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
    qRegisterMetaType<av::SceneComponent>("av::SceneComponent");

    const QString src = find_source_scene();
    if (src.isEmpty()) {
        std::printf("SKIP: v3_Wasteland.scene not found\n");
        return 0;
    }
    const QString work = "/tmp/ruby_undo_test.scene";
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

    QMetaObject::invokeMethod(win, "onFileSelectedInBrowser",
                              Qt::DirectConnection, Q_ARG(QString, work));
    auto* vp = win->findChild<ruby::viewport::Viewport3DWidget*>();
    check(wait_for([&]() {
        return vp != nullptr && vp->has_scene() && vp->scene().filepath == work.toStdString();
    }), "scene loaded into viewport");
    if (failures) { QFile::remove(work); return 1; }

    const size_t base = vp->scene().objects.size();
    check(base > 2, "scene has enough objects");

    // ── 1.2a structural undo: duplicate → undo → redo (full-rebuild path) ──
    vp->duplicate_scene_object(0);
    check(wait_for([&]() { return vp->has_scene() && vp->scene().objects.size() == base + 1; }),
          "duplicate added an object");
    vp->undo();
    check(wait_for([&]() { return vp->has_scene() && vp->scene().objects.size() == base; }),
          "undo removed the duplicate");
    vp->redo();
    check(wait_for([&]() { return vp->has_scene() && vp->scene().objects.size() == base + 1; }),
          "redo restored the duplicate");

    // ── 1.2b delete → undo → redo ──────────────────────────────────────────
    vp->delete_scene_object(static_cast<int>(base));   // the duplicate
    check(wait_for([&]() { return vp->has_scene() && vp->scene().objects.size() == base; }),
          "delete removed the object");
    vp->undo();
    check(wait_for([&]() { return vp->has_scene() && vp->scene().objects.size() == base + 1; }),
          "undo restored the deleted object");

    // ── 1.2c visibility toggle: fast in-place path (structure preserved) ────
    check(!vp->scene().objects[0].hidden, "object 0 starts visible");
    vp->set_scene_object_visibility(0, false);
    check(vp->scene().objects[0].hidden, "visibility toggle applied");
    vp->undo();
    check(wait_for([&]() { return vp->has_scene() && !vp->scene().objects[0].hidden; }),
          "undo restored visibility");
    vp->redo();
    check(wait_for([&]() { return vp->has_scene() && vp->scene().objects[0].hidden; }),
          "redo re-hid the object");

    // ── 1.1 component paste wiring: signal → fresh-id insert → undoable ────
    auto* inspector = win->findChild<ruby::panels::InspectorPanel*>();
    check(inspector != nullptr, "inspector panel present");
    if (inspector) {
        const auto& obj0 = vp->scene().objects[0];
        check(!obj0.components.empty(), "object 0 has components to copy");
        if (!obj0.components.empty()) {
            const av::SceneComponent source = obj0.components[0];
            const size_t comp_base = vp->scene().objects[0].components.size();
            QMetaObject::invokeMethod(inspector, "componentPasted",
                                      Qt::DirectConnection,
                                      Q_ARG(int, 0),
                                      Q_ARG(av::SceneComponent, source));
            check(wait_for([&]() {
                return vp->has_scene() &&
                       vp->scene().objects[0].components.size() == comp_base + 1;
            }), "componentPasted inserted a component");
            // The fresh-id rule: the pasted component's type id must not
            // collide with an existing one on the object.
            const auto& comps = vp->scene().objects[0].components;
            bool id_collides = false;
            for (size_t i = 0; i + 1 < comps.size(); ++i)
                for (size_t j = i + 1; j < comps.size(); ++j)
                    if (comps[i].type_id == comps[j].type_id) id_collides = true;
            check(!id_collides, "pasted component got a fresh type id");

            vp->undo();
            check(wait_for([&]() {
                return vp->has_scene() &&
                       vp->scene().objects[0].components.size() == comp_base;
            }), "undo removed the pasted component");
            vp->redo();
            check(wait_for([&]() {
                return vp->has_scene() &&
                       vp->scene().objects[0].components.size() == comp_base + 1;
            }), "redo re-pasted the component");
        }
    }

    // ── 2.2 rotation presets: 90° button sets rot_y through the spinbox path
    // and is undoable (snapshot undo on objectTransformChanged) ────────────
    if (inspector) {
        inspector->inspect_scene_object(vp->scene(), 0);   // select object 0
        QCoreApplication::processEvents();
        check(inspector->current_object_index() == 0, "inspector inspects object 0");
        const float orig_rot_y = vp->scene().objects[0].rot_y;
        auto* preset90 = inspector->findChild<QPushButton*>(QStringLiteral("rotPreset90"));
        check(preset90 != nullptr, "90° rotation preset button exists");
        if (preset90 && inspector->current_object_index() == 0) {
            preset90->click();
            check(wait_for([&]() {
                return vp->has_scene() &&
                       std::fabs(vp->scene().objects[0].rot_y - 90.0f) < 0.01f;
            }), "90° preset set rot_y to 90");
            vp->undo();
            check(wait_for([&]() {
                return vp->has_scene() &&
                       std::fabs(vp->scene().objects[0].rot_y - orig_rot_y) < 0.01f;
            }), "undo restored rot_y after the preset");
        }
    }

    // ── 2.3 template palette: add a scene-embedded template object, verify
    // it resolves + is undoable; retarget it to another template ────────────
    if (vp->has_scene()) {
        const auto templates = av::scene_list_templates(vp->scene());
        check(!templates.empty(), "scene exposes templates from its libraries");
        if (!templates.empty()) {
            const QString tpl = QString::fromStdString(templates.front().name);
            const size_t before = vp->scene().objects.size();
            const int added = vp->add_template_object(tpl);
            check(added >= 0, "add_template_object added an object");
            check(wait_for([&]() {
                return vp->has_scene() && vp->scene().objects.size() == before + 1;
            }), "template object present in the scene");
            if (added >= 0) {
                const auto& obj = vp->scene().objects[added];
                check(obj.template_name == tpl.toStdString(),
                      "added object is linked to the template");
                check(!obj.resolved_components.empty(),
                      "template components resolved for rendering");
            }
            vp->undo();
            check(wait_for([&]() {
                return vp->has_scene() && vp->scene().objects.size() == before;
            }), "undo removed the template object");
            vp->redo();
            check(wait_for([&]() {
                return vp->has_scene() && vp->scene().objects.size() == before + 1;
            }), "redo restored the template object");

            // Retarget the freshly re-added object to a second template.
            const QString tpl2 = templates.size() > 1
                                     ? QString::fromStdString(templates[1].name)
                                     : QString();
            if (!tpl2.isEmpty() && tpl2 != tpl) {
                const int idx = static_cast<int>(before);
                vp->set_scene_object_template(idx, tpl2);
                check(vp->scene().objects[idx].template_name == tpl2.toStdString(),
                      "object retargeted to the second template");
                vp->undo();
                check(wait_for([&]() {
                    return vp->has_scene() &&
                           vp->scene().objects[static_cast<size_t>(idx)].template_name ==
                               tpl.toStdString();
                }), "undo restored the first template");
                vp->redo();
                check(wait_for([&]() {
                    return vp->has_scene() &&
                           vp->scene().objects[static_cast<size_t>(idx)].template_name ==
                               tpl2.toStdString();
                }), "redo re-applied the second template");
            }
        }
    }

    std::printf(failures == 0 ? "PASS: snapshot undo + component paste + rotation presets + template palette work\n"
                              : "FAIL: %d assertion(s)\n", failures);
    QFile::remove(work);
    return failures == 0 ? 0 : 1;
}
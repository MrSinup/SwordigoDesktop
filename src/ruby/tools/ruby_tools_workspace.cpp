#include "ruby_tools_workspace.h"

#include "ruby/tools/ground_mesh_studio.h"
#include "ruby/core/project_context.h"

#include "platform/pvr_loader.h"
#include "tools/batch_converter.h"
#include "tools/boulder.h"
#include "tools/filerift.h"
#include "tools/gltf_glb.h"
#include "tools/obj_loader.h"
#include "tools/pod_loader.h"
#include "tools/pod_writer.h"
#include "tools/rubymesh.h"
#include "tools/scene_creator.h"
#include "tools/scene_generator.h"
#include "tools/scene_generator_v2.h"
#include "tools/scene_generator_v2_3d.h"
#include "tools/scene_generator_v3.h"
#include "tools/scene_loader.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <climits>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace ruby::tools {
namespace {

enum class PathDialogMode {
    Directory,
    OpenFile,
    SaveFile
};

static QString getSessionDir() {
    // 1. Check ProjectContext's active project/session dir
    const std::string& pdir = ruby::core::ProjectContext::instance().project_dir();
    if (!pdir.empty()) {
        QString qp = QString::fromStdString(pdir);
        if (QDir(qp).exists()) {
            if (QDir(qp + "/scenes").exists()) return qp + "/scenes";
            if (QDir(qp + "/Levels").exists()) return qp + "/Levels";
            if (QDir(qp + "/assets/scenes").exists()) return qp + "/assets/scenes";
            if (QDir(qp + "/assets/resources").exists()) return qp + "/assets/resources";
            return qp;
        }
    }

    // 2. Check active scene file's directory
    const std::string& afile = ruby::core::ProjectContext::instance().active_file();
    if (!afile.empty()) {
        QFileInfo fi(QString::fromStdString(afile));
        if (fi.exists() || fi.dir().exists()) {
            return fi.dir().path();
        }
    }

    // 3. Fallback: application current working directory (never random home directory!)
    return QDir::currentPath();
}

QPushButton* browseButton(QWidget* p, QLineEdit* edit, PathDialogMode mode, const QString& filter = QString()) {
    auto* b = new QPushButton("Browse...", p);
    QObject::connect(b, &QPushButton::clicked, p, [edit, mode, filter, p] {
        QString initial = edit->text().trimmed();
        if (initial.isEmpty()) initial = getSessionDir();
        QString value;
        if (mode == PathDialogMode::Directory) {
            value = QFileDialog::getExistingDirectory(p, "Select directory", initial);
        } else if (mode == PathDialogMode::SaveFile) {
            value = QFileDialog::getSaveFileName(p, "Select output file", initial,
                                                 filter.isEmpty() ? "Scene files (*.scene);;All files (*.*)" : filter);
        } else {
            value = QFileDialog::getOpenFileName(p, "Select file", initial,
                                                filter.isEmpty() ? "All files (*.*)" : filter);
        }
        if (!value.isEmpty()) edit->setText(value);
    });
    return b;
}

QWidget* pathRow(QWidget* p, QLineEdit** out, PathDialogMode mode, const QString& filter = QString()) {
    auto* w = new QWidget(p); auto* l = new QHBoxLayout(w); l->setContentsMargins(0, 0, 0, 0);
    *out = new QLineEdit(w); l->addWidget(*out, 1); l->addWidget(browseButton(w, *out, mode, filter)); return w;
}

QPushButton* browseButton(QWidget* p, QLineEdit* edit, bool directory) {
    return browseButton(p, edit, directory ? PathDialogMode::Directory : PathDialogMode::OpenFile);
}

QWidget* pathRow(QWidget* p, QLineEdit** out, bool directory) {
    return pathRow(p, out, directory ? PathDialogMode::Directory : PathDialogMode::OpenFile);
}
QWidget* buttons(QWidget* p, std::initializer_list<QPushButton*> bs) {
    auto* w = new QWidget(p); auto* l = new QHBoxLayout(w); l->setContentsMargins(0, 0, 0, 0);
    for (auto* b : bs) l->addWidget(b); l->addStretch(); return w;
}
// Wrap an inner widget in a scroll area so narrow windows never clip forms.
QScrollArea* scrollPage(QWidget* inner) {
    auto* area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(inner);
    return area;
}
QGroupBox* section(const QString& title, QWidget* parent) {
    auto* box = new QGroupBox(title, parent);
    box->setStyleSheet("QGroupBox { font-weight: bold; margin-top: 8px; }"
                       "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }");
    return box;
}
} // namespace

class RubyToolsWorkspace::Impl {
public:
    RubyToolsWorkspace* host;
    QTabWidget* tabs = new QTabWidget;
    QPlainTextEdit* batchLog = nullptr;
    batch::BatchState batch;
    QTimer poll;
    QLineEdit* rbmPath = nullptr;
    rbm::RubyMesh mesh;
    QTableWidget* zones = nullptr;
    ruby::tools::GroundMeshStudio* ground_studio = nullptr;

    explicit Impl(RubyToolsWorkspace* h) : host(h) {
        batch.pvrtextool_path[0] = '\0';

        auto* root = new QVBoxLayout(host);
        root->setContentsMargins(8, 6, 8, 8);
        root->setSpacing(4);

        auto* header = new QWidget(host);
        auto* header_row = new QHBoxLayout(header);
        header_row->setContentsMargins(6, 0, 6, 0);
        auto* title = new QLabel("Authoring & Production Tools", header);
        title->setStyleSheet("font-size: 15px; font-weight: 700; color: #e4e8ef;");
        auto* subtitle = new QLabel("Full-area workspaces for ground meshes, collision zones, scene generation and asset pipelines.", header);
        subtitle->setStyleSheet("color: #7d8492;");
        header_row->addWidget(title);
        header_row->addSpacing(12);
        header_row->addWidget(subtitle, 1);
        root->addWidget(header);

        tabs->setDocumentMode(true);
        root->addWidget(tabs, 1);

        ground_studio = new ruby::tools::GroundMeshStudio(host);
        QObject::connect(ground_studio, &GroundMeshStudio::groundMeshSaved,
                         host, &RubyToolsWorkspace::groundMeshApplied);
        QObject::connect(ground_studio, &GroundMeshStudio::groundMeshAddToScene,
                         host, &RubyToolsWorkspace::groundMeshAddToScene);
        tabs->addTab(ground_studio, "Ground Mesh Studio");
        tabs->addTab(rubymesh(), "RubyMesh");
        tabs->addTab(sclObjects(), "SCL / Objects");
        tabs->addTab(generators(), "Scene Generators");
        tabs->addTab(batchWidget(), "Batch Converter");
        tabs->addTab(blender(), "Blender");
        tabs->addTab(settings(), "Settings");

        poll.setInterval(150);
        QObject::connect(&poll, &QTimer::timeout, host, [this] { updateBatch(); });
    }

    // ── RubyMesh (.rbm collision-zone manifest) ─────────────────────────────
    QWidget* rubymesh() {
        auto* outer = new QWidget;
        auto* v = new QVBoxLayout(outer);
        v->setContentsMargins(10, 10, 10, 10);
        v->setSpacing(8);

        auto* info = new QLabel("Manually-authored collision zones for one POD, stored as a portable .rbm "
                                "sidecar. Each zone becomes a Swordigo ground mesh when applied to a scene.", outer);
        info->setWordWrap(true);
        v->addWidget(info);

        auto* file_box = section("Zone file (.rbm)", outer);
        auto* file_form = new QFormLayout(file_box);
        auto* row = pathRow(file_box, &rbmPath, false);
        file_form->addRow("File", row);
        v->addWidget(file_box);

        auto* table_box = section("Zones", outer);
        auto* table_v = new QVBoxLayout(table_box);
        zones = new QTableWidget(0, 5, table_box);
        zones->setHorizontalHeaderLabels({"Enabled", "Zone", "Material", "Vertices", "World Z"});
        zones->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        zones->verticalHeader()->setVisible(false);
        zones->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_v->addWidget(zones);
        v->addWidget(table_box, 1);

        auto* load = new QPushButton("Load .rbm", outer);
        auto* save = new QPushButton("Save .rbm", outer);
        auto* apply = new QPushButton("Apply zones to scene (writes ground meshes)", outer);
        apply->setObjectName("brandButton");
        v->addWidget(buttons(outer, {load, save, apply}));

        auto refresh = [this] {
            zones->setRowCount(int(mesh.zones.size()));
            for (int i = 0; i < int(mesh.zones.size()); ++i) {
                auto& z = mesh.zones[size_t(i)];
                zones->setItem(i, 0, new QTableWidgetItem(z.enabled ? "Yes" : "No"));
                zones->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(z.name)));
                zones->setItem(i, 2, new QTableWidgetItem(QString::number(int(z.material))));
                zones->setItem(i, 3, new QTableWidgetItem(QString::number(int(z.vertices.size()))));
                zones->setItem(i, 4, new QTableWidgetItem(QString::number(z.world_z)));
            }
        };
        QObject::connect(load, &QPushButton::clicked, outer, [this, refresh] {
            std::string err;
            if (rbm::rbm_load(rbmPath->text().toStdString(), mesh, err)) refresh();
            else QMessageBox::warning(host, "RubyMesh", QString::fromStdString(err));
        });
        QObject::connect(save, &QPushButton::clicked, outer, [this] {
            std::string err;
            if (!rbm::rbm_save(mesh, rbmPath->text().toStdString(), err))
                QMessageBox::warning(host, "RubyMesh", QString::fromStdString(err));
        });
        QObject::connect(apply, &QPushButton::clicked, outer, [this] {
            emit host->collisionApplied(rbmPath->text());
        });
        return scrollPage(outer);
    }

    // ── SCL / Objects decoding studio ───────────────────────────────────────
    QWidget* sclObjects() {
        auto* outer = new QWidget;
        auto* v = new QVBoxLayout(outer);
        v->setContentsMargins(10, 10, 10, 10);
        v->setSpacing(8);

        auto* info = new QLabel("Decode binary FileRift/SCL object libraries into editable markup, recode them, "
                                "and import POD/OBJ/GLB objects into the current project.", outer);
        info->setWordWrap(true);
        v->addWidget(info);

        auto* file_box = section("Source file", outer);
        auto* file_form = new QFormLayout(file_box);
        QLineEdit* path = nullptr;
        file_form->addRow("File", pathRow(file_box, &path, false));
        v->addWidget(file_box);

        auto* body = new QHBoxLayout;
        auto* list_box = section("Templates in library", outer);
        auto* list_v = new QVBoxLayout(list_box);
        auto* list = new QListWidget(list_box);
        list_v->addWidget(list);
        body->addWidget(list_box, 1);

        auto* edit_box = section("Markup (decoded FileRift source)", outer);
        auto* edit_v = new QVBoxLayout(edit_box);
        auto* edit = new QPlainTextEdit(edit_box);
        edit->setPlaceholderText("Decoded FileRift SCL source");
        edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        edit_v->addWidget(edit);
        body->addWidget(edit_box, 2);
        v->addLayout(body, 1);

        auto* open = new QPushButton("Decode / Open", outer);
        auto* save = new QPushButton("Recode / Save", outer);
        auto* import = new QPushButton("Import asset (POD / OBJ / GLB)...", outer);
        import->setObjectName("brandButton");
        v->addWidget(buttons(outer, {open, save, import}));

        QObject::connect(open, &QPushButton::clicked, outer, [path, list, edit] {
            QFile f(path->text());
            if (!f.open(QIODevice::ReadOnly)) return;
            const auto bytes = f.readAll();
            const auto decoded = filerift::decode_protobuf(std::string(bytes.constData(), size_t(bytes.size())), "scl");
            edit->setPlainText(QString::fromStdString(decoded));
            list->clear();
            for (const auto& t : av::scl_load_templates(std::string(bytes.constData(), size_t(bytes.size()))))
                list->addItem(QString::fromStdString(t.name));
        });
        QObject::connect(save, &QPushButton::clicked, outer, [path, edit] {
            const auto encoded = filerift::recode_markup(edit->toPlainText().toStdString(), "scl");
            if (encoded.empty()) return;
            QFile f(path->text());
            if (f.open(QIODevice::WriteOnly)) f.write(encoded.data(), qint64(encoded.size()));
        });
        QObject::connect(import, &QPushButton::clicked, outer, [this] {
            const auto p = QFileDialog::getOpenFileName(host, "Import object", QString(),
                                                        "Models (*.pod *.obj *.glb);;All files (*)");
            if (!p.isEmpty()) emit host->objectImported(p);
        });
        return scrollPage(outer);
    }

    // ── Scene Generators ────────────────────────────────────────────────────
    QWidget* generators() {
        auto* outer = new QWidget;
        auto* v = new QVBoxLayout(outer);
        v->setContentsMargins(10, 10, 10, 10);
        v->setSpacing(8);

        auto* info = new QLabel("Generate full Swordigo scene binaries from biome presets harvested from the "
                                "shipped levels. Pick a generator family, tune the terrain, then write the .scene.", outer);
        info->setWordWrap(true);
        v->addWidget(info);

        auto* body = new QHBoxLayout;
        auto* left = new QWidget(outer);
        auto* lf = new QVBoxLayout(left);
        lf->setContentsMargins(0, 0, 0, 0);

        auto* gen_box = section("Generator", left);
        auto* gen_form = new QFormLayout(gen_box);
        auto* type = new QComboBox(gen_box);
        type->addItems({"Scene Creator", "Procedural V1", "Procedural V2", "Procedural V3", "Procedural V3-DB", "Procedural V2-3D"});
        auto* biome = new QComboBox(gen_box);
        for (int i = 0; i < int(sgen::Biome::Count); ++i) biome->addItem(sgen::biome_name(sgen::Biome(i)), i);
        auto* name_edit = new QLineEdit("procedural_world", gen_box);
        name_edit->setPlaceholderText("scene/level name");
        auto* seed = new QSpinBox(gen_box);
        seed->setRange(0, INT_MAX); seed->setValue(1337);
        gen_form->addRow("Family", type);
        gen_form->addRow("Biome", biome);
        gen_form->addRow("Scene name", name_edit);
        gen_form->addRow("Seed", seed);
        lf->addWidget(gen_box);

        auto* terrain_box = section("Terrain", left);
        auto* terrain_form = new QFormLayout(terrain_box);
        auto* width = new QDoubleSpinBox(terrain_box);
        width->setRange(200.0, 500000.0); width->setValue(4200.0); width->setSingleStep(100.0);
        auto* height = new QDoubleSpinBox(terrain_box);
        height->setRange(20.0, 50000.0); height->setValue(900.0); height->setSingleStep(50.0);
        auto* platforms = new QSpinBox(terrain_box);
        platforms->setRange(0, 200); platforms->setValue(6);
        auto* octaves = new QSpinBox(terrain_box);
        octaves->setRange(1, 12); octaves->setValue(4);
        auto* roughness = new QDoubleSpinBox(terrain_box);
        roughness->setRange(0.1, 8.0); roughness->setValue(1.0); roughness->setSingleStep(0.1);
        auto* deco = new QDoubleSpinBox(terrain_box);
        deco->setRange(0.0, 2.0); deco->setValue(1.0); deco->setSingleStep(0.1);
        terrain_form->addRow("World width", width);
        terrain_form->addRow("Height range", height);
        terrain_form->addRow("Platform strips", platforms);
        terrain_form->addRow("Noise octaves", octaves);
        terrain_form->addRow("Roughness", roughness);
        terrain_form->addRow("Decoration density", deco);
        lf->addWidget(terrain_box);

        auto* feat_box = section("Features", left);
        auto* feat_form = new QFormLayout(feat_box);
        auto* water = new QCheckBox("Valley water mesh", feat_box); water->setChecked(true);
        auto* torches = new QCheckBox("Torch + light spill", feat_box); torches->setChecked(true);
        auto* mountains = new QCheckBox("Ridged mountain profile", feat_box);
        auto* islands = new QCheckBox("Disconnected island hats", feat_box);
        auto* portal = new QCheckBox("Add travel portal hub", feat_box);
        feat_form->addRow(water);
        feat_form->addRow(torches);
        feat_form->addRow(mountains);
        feat_form->addRow(islands);
        feat_form->addRow(portal);
        lf->addWidget(feat_box);
        lf->addStretch();

        auto* right = new QWidget(outer);
        auto* rf = new QVBoxLayout(right);
        rf->setContentsMargins(0, 0, 0, 0);
        auto* out_box = section("Output", right);
        auto* out_form = new QFormLayout(out_box);
        QLineEdit* output = nullptr;
        out_form->addRow("Scene file", pathRow(out_box, &output, PathDialogMode::SaveFile, "Scene files (*.scene);;All files (*.*)"));

        auto update_output_path = [output](const QString& scene_name) {
            if (!output) return;
            QString dir = getSessionDir();
            if (!output->text().trimmed().isEmpty()) {
                QFileInfo cur_fi(output->text());
                if (cur_fi.dir().exists() && cur_fi.dir().path() != "." && cur_fi.dir().path() != QDir::homePath()) {
                    dir = cur_fi.dir().path();
                }
            }
            QString clean = scene_name.trimmed();
            if (clean.isEmpty()) clean = "procedural_world";
            if (!clean.endsWith(".scene")) clean += ".scene";
            output->setText(QDir(dir).filePath(clean));
        };

        update_output_path(name_edit->text());

        QObject::connect(name_edit, &QLineEdit::textChanged, outer, [update_output_path](const QString& txt) {
            update_output_path(txt);
        });

        QObject::connect(&ruby::core::ProjectContext::instance(), &ruby::core::ProjectContext::projectChanged, outer,
            [update_output_path, name_edit](const QString&) {
                update_output_path(name_edit->text());
            });

        auto* generate = new QPushButton("Generate Scene", out_box);
        generate->setObjectName("brandButton");
        generate->setMinimumHeight(38);
        out_form->addRow(generate);
        rf->addWidget(out_box);
        auto* log_box = section("Generation report", right);
        auto* log_v = new QVBoxLayout(log_box);
        auto* log = new QPlainTextEdit(log_box);
        log->setReadOnly(true);
        log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        log_v->addWidget(log);
        rf->addWidget(log_box, 1);

        body->addWidget(left, 3);
        body->addWidget(right, 2);
        v->addLayout(body, 1);

        auto append = [log](const QString& text) { log->appendPlainText(text); };
        QObject::connect(generate, &QPushButton::clicked, outer,
            [this, type, biome, name_edit, seed, width, height, platforms, octaves, roughness,
             deco, water, torches, mountains, islands, portal, output, append] {
            const auto out = output->text().toStdString();
            if (out.empty()) { append("No output path set."); return; }
            const int n = type->currentIndex();
            if (n == 0) {
                scenecreate::Options o;
                o.output_path = out;
                o.level_name = QFileInfo(output->text()).baseName().toStdString();
                scenecreate::Result r;
                std::string e;
                append(scenecreate::create(o, r, e)
                       ? "Scene Creator: wrote " + QString::fromStdString(r.scene_path)
                       : "Scene Creator failed: " + QString::fromStdString(e));
                return;
            }
            sgen::TerrainOptions o;
            o.biome = sgen::Biome(biome->currentData().toInt());
            o.seed = uint32_t(seed->value());
            o.scene_name = name_edit->text().trimmed().toStdString();
            o.width = float(width->value());
            o.height = float(height->value());
            o.platform_count = platforms->value();
            o.octaves = octaves->value();
            o.roughness = float(roughness->value());
            o.deco_density = float(deco->value());
            o.add_water = water->isChecked();
            o.spill_torches = torches->isChecked();
            o.mountains = mountains->isChecked();
            o.islands = islands->isChecked();
            o.add_portal = portal->isChecked();

            sgen::Result r;
            if (n == 1) r = sgen::generate_biome_scene(o);
            else if (n == 2) { sgen::v2::TerrainOptionsV2 x; static_cast<sgen::TerrainOptions&>(x) = o; r = sgen::v2::generate_biome_scene_v2(x); }
            else if (n == 3 || n == 4) r = sgen::v3::generate_biome_scene_v3(o);
            else { sgen::v2_3d::TerrainOptions3D x; static_cast<sgen::TerrainOptions&>(x) = o; r = sgen::v2_3d::generate_biome_scene_v2_3d(x); }

            if (!r.ok()) { append("Generation failed: " + QString::fromStdString(r.error)); return; }
            QFile f(output->text());
            if (!f.open(QIODevice::WriteOnly)) { append("Could not write " + output->text()); return; }
            f.write(r.scene_bytes.data(), qint64(r.scene_bytes.size()));
            append(QString("Generated %1 objects (%2 bytes) -> %3")
                .arg(int(r.objects)).arg(int(r.scene_bytes.size())).arg(output->text()));
        });
        return scrollPage(outer);
    }

    // ── Batch texture conversion ────────────────────────────────────────────
    QWidget* batchWidget() {
        auto* outer = new QWidget;
        auto* v = new QVBoxLayout(outer);
        v->setContentsMargins(10, 10, 10, 10);
        v->setSpacing(8);

        auto* info = new QLabel("Bulk-convert textures to/from game formats. Exporting needs a configured "
                                "PVRTexTool; importing decodes with the built-in software path.", outer);
        info->setWordWrap(true);
        v->addWidget(info);

        auto* controls = new QWidget(outer);
        auto* f = new QFormLayout(controls);
        QLineEdit* src = nullptr; QLineEdit* dst = nullptr;
        f->addRow("Input folder", pathRow(controls, &src, true));
        f->addRow("Output folder", pathRow(controls, &dst, true));
        auto* mode = new QComboBox(controls);
        mode->addItems({"Export to PNG", "Import to game"});
        auto* fmt = new QComboBox(controls);
        fmt->addItems({"ETC1", "PVRTC 4bpp", "PVRTC 2bpp", "RGBA8888"});
        auto* recurse = new QCheckBox("Recurse subdirectories", controls);
        recurse->setChecked(true);
        f->addRow("Mode", mode);
        f->addRow("Compression", fmt);
        f->addRow(recurse);
        auto* body = new QHBoxLayout;
        auto* cw = new QWidget(outer); auto* cl = new QVBoxLayout(cw); cl->setContentsMargins(0, 0, 0, 0);
        cl->addWidget(controls);
        auto* progress = new QProgressBar(cw);
        cl->addWidget(progress);
        auto* start = new QPushButton("Start Batch", cw); start->setObjectName("brandButton");
        auto* cancel = new QPushButton("Cancel", cw);
        cl->addWidget(buttons(cw, {start, cancel}));
        cl->addStretch();
        body->addWidget(cw, 0);

        batchLog = new QPlainTextEdit(outer);
        batchLog->setReadOnly(true);
        batchLog->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        body->addWidget(batchLog, 1);
        v->addLayout(body, 1);

        QObject::connect(start, &QPushButton::clicked, outer,
            [this, src, dst, mode, fmt, recurse, progress] {
                std::snprintf(batch.src_dir, sizeof(batch.src_dir), "%s", src->text().toUtf8().constData());
                std::snprintf(batch.dst_dir, sizeof(batch.dst_dir), "%s", dst->text().toUtf8().constData());
                batch.mode = mode->currentIndex() ? batch::Mode::IMPORT_TO_GAME : batch::Mode::EXPORT_TO_PNG;
                batch.compress_fmt = batch::CompressFmt(fmt->currentIndex());
                batch.recurse_subdirs = recurse->isChecked();
                start_batch_job(batch, 0);
                poll.start();
                progress->setValue(0);
            });
        QObject::connect(cancel, &QPushButton::clicked, outer, [this] { cancel_batch_job(batch); });
        return outer;
    }

    void updateBatch() {
        if (batchLog) {
            std::lock_guard<std::mutex> lock(batch.log_mutex);
            batchLog->clear();
            for (auto& e : batch.log) batchLog->appendPlainText(QString::fromStdString(e.text));
        }
        if (!batch.running && batch.finished) poll.stop();
    }

    // ── Blender bridge ──────────────────────────────────────────────────────
    QWidget* blender() {
        auto* outer = new QWidget;
        auto* f = new QFormLayout(outer);
        f->setContentsMargins(10, 10, 10, 10);
        auto* info = new QLabel("Round-trip POD <-> GLB through Blender. Configure your executable here instead "
                                "of relying on a machine-specific path.", outer);
        info->setWordWrap(true);
        f->addRow(info);
        QLineEdit* exe = nullptr; QLineEdit* ext = nullptr;
        f->addRow("Blender executable", pathRow(outer, &exe, false));
        f->addRow("Extension folder", pathRow(outer, &ext, true));
        auto* install = new QPushButton("Install extension", outer);
        auto* launch = new QPushButton("Launch Blender", outer);
        f->addRow(buttons(outer, {install, launch}));
        QSettings s;
        exe->setText(s.value("ruby_gg/blenderExecutable").toString());
        ext->setText(s.value("ruby_gg/blenderExtension").toString());
        QObject::connect(launch, &QPushButton::clicked, outer, [exe] { QProcess::startDetached(exe->text(), {}); });
        QObject::connect(install, &QPushButton::clicked, outer, [exe, ext, outer] {
            if (exe->text().isEmpty()) { QMessageBox::warning(outer, "Blender", "Set the Blender executable first."); return; }
            QProcess::startDetached(exe->text(), {"--background", "--python", ""});
            QMessageBox::information(outer, "Blender",
                "Configure the selected extension in Blender, then use the GLB bridge from the asset browser.");
        });
        QObject::connect(exe, &QLineEdit::editingFinished, outer, [exe] { QSettings().setValue("ruby_gg/blenderExecutable", exe->text()); });
        QObject::connect(ext, &QLineEdit::editingFinished, outer, [ext] { QSettings().setValue("ruby_gg/blenderExtension", ext->text()); });
        return outer;
    }

    // ── Renderer settings ───────────────────────────────────────────────────
    QWidget* settings() {
        auto* outer = new QWidget;
        auto* f = new QFormLayout(outer);
        f->setContentsMargins(10, 10, 10, 10);
        auto* info = new QLabel("Viewport display options. Quality maps to the renderer's detail ladder "
                                "(0 = lowest, 3 = highest).", outer);
        info->setWordWrap(true);
        f->addRow(info);
        auto* q = new QSpinBox(outer);
        q->setRange(0, 3);
        auto* wire = new QCheckBox("Wireframe rendering", outer);
        auto* grid = new QCheckBox("World grid", outer);
        QSettings s;
        q->setValue(s.value("ruby_gg/quality", 2).toInt());
        wire->setChecked(s.value("ruby_gg/wireframe", false).toBool());
        grid->setChecked(s.value("ruby_gg/grid", true).toBool());
        f->addRow("Renderer quality", q);
        f->addRow(wire);
        f->addRow(grid);
        auto emitSettings = [this, q, wire, grid] {
            QSettings s;
            s.setValue("ruby_gg/quality", q->value());
            s.setValue("ruby_gg/wireframe", wire->isChecked());
            s.setValue("ruby_gg/grid", grid->isChecked());
            emit host->rendererQualityChanged(q->value(), wire->isChecked(), grid->isChecked());
        };
        QObject::connect(q, &QSpinBox::valueChanged, outer, emitSettings);
        QObject::connect(wire, &QCheckBox::toggled, outer, emitSettings);
        QObject::connect(grid, &QCheckBox::toggled, outer, emitSettings);
        return outer;
    }
};

RubyToolsWorkspace::RubyToolsWorkspace(QWidget* p) : QWidget(p), m_impl(std::make_unique<Impl>(this)) {
    setObjectName("RubyAuthoringTools");
    setWindowTitle("Authoring & Production Tools");
}

RubyToolsWorkspace::~RubyToolsWorkspace() { shutdown_batch(m_impl->batch); }

void RubyToolsWorkspace::set_scene_camera_focus(double x, double y) {
    if (m_impl && m_impl->ground_studio) {
        m_impl->ground_studio->set_scene_camera_focus(x, y);
    }
}
} // namespace ruby::tools

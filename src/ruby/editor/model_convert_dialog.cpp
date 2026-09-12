// ============================================================================
// model_convert_dialog.cpp — 3D Model to Game POD Converter Implementation
// ============================================================================

#include "model_convert_dialog.h"
#include "tools/pod_convert.h"
#include "tools/pod_loader.h"
#include "tools/gltf_glb.h"
#include "tools/fbx_import.h"
#include "tools/obj_loader.h"
#include "ruby/core/project_context.h"
#include "ruby/theme/ruby_theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QProgressBar>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <QScrollArea>
#include <QFrame>
#include <algorithm>
#include <cmath>

namespace ruby::editor {

ModelConvertDialog::ModelConvertDialog(QWidget* parent, const QString& initial_source_path)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Convert 3D Model to Game POD — Ruby Studio"));
    resize(680, 640);
    setMinimumSize(560, 520);
    setModal(true);

    setup_ui();

    if (!initial_source_path.isEmpty()) {
        set_source_path(initial_source_path);
    }
}

void ModelConvertDialog::setup_ui() {
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(16, 16, 16, 16);
    main_layout->setSpacing(12);

    // Header title & description
    auto* header_layout = new QVBoxLayout();
    header_layout->setSpacing(4);
    auto* title_lbl = new QLabel(QStringLiteral("Convert 3D Model to Game POD"), this);
    title_lbl->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold; color: #e5e9f0;"));
    auto* sub_lbl = new QLabel(QStringLiteral(
        "Export glTF / GLB / FBX / OBJ assets into native Swordigo PowerVR .POD format "
        "with geometry scaling and ETC1 .pvr texture compression."), this);
    sub_lbl->setStyleSheet(QStringLiteral("font-size: 11px; color: #9aa2b1;"));
    sub_lbl->setWordWrap(true);
    header_layout->addWidget(title_lbl);
    header_layout->addWidget(sub_lbl);
    main_layout->addLayout(header_layout);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));

    auto* content_widget = new QWidget(scroll);
    auto* layout = new QVBoxLayout(content_widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    // ── Group 1: Files & Paths ───────────────────────────────────────────────
    auto* grp_files = new QGroupBox(QStringLiteral("File Paths"), content_widget);
    grp_files->setStyleSheet(QStringLiteral("QGroupBox { font-weight: bold; color: #e5e9f0; }"));
    auto* files_layout = new QGridLayout(grp_files);
    files_layout->setSpacing(8);

    auto* src_label = new QLabel(QStringLiteral("Source Model:"), grp_files);
    m_source_edit = new QLineEdit(grp_files);
    m_source_edit->setPlaceholderText(QStringLiteral("Path to .glb, .gltf, .fbx, or .obj..."));
    m_source_browse_btn = new QPushButton(QStringLiteral("Browse..."), grp_files);
    m_source_browse_btn->setCursor(Qt::PointingHandCursor);

    auto* dst_label = new QLabel(QStringLiteral("Output Game POD:"), grp_files);
    m_output_edit = new QLineEdit(grp_files);
    m_output_edit->setPlaceholderText(QStringLiteral("Path to output .POD file..."));
    m_output_browse_btn = new QPushButton(QStringLiteral("Browse..."), grp_files);
    m_output_browse_btn->setCursor(Qt::PointingHandCursor);

    files_layout->addWidget(src_label, 0, 0);
    files_layout->addWidget(m_source_edit, 0, 1);
    files_layout->addWidget(m_source_browse_btn, 0, 2);

    files_layout->addWidget(dst_label, 1, 0);
    files_layout->addWidget(m_output_edit, 1, 1);
    files_layout->addWidget(m_output_browse_btn, 1, 2);

    // Inspection labels
    m_dim_label = new QLabel(QStringLiteral("Source Dimensions: 0.00 W × 0.00 H × 0.00 D units"), grp_files);
    m_dim_label->setStyleSheet(QStringLiteral("color: #abb2bf; font-size: 11px; margin-top: 4px;"));
    files_layout->addWidget(m_dim_label, 2, 0, 1, 3);

    layout->addWidget(grp_files);

    // ── Group 2: Scale & Geometry ────────────────────────────────────────────
    auto* grp_scale = new QGroupBox(QStringLiteral("Scale & Geometry"), content_widget);
    grp_scale->setStyleSheet(QStringLiteral("QGroupBox { font-weight: bold; color: #e5e9f0; }"));
    auto* scale_layout = new QVBoxLayout(grp_scale);
    scale_layout->setSpacing(8);

    // Scale multiplier row
    auto* mult_layout = new QHBoxLayout();
    auto* mult_lbl = new QLabel(QStringLiteral("Scale Multiplier:"), grp_scale);
    m_scale_spin = new QDoubleSpinBox(grp_scale);
    m_scale_spin->setRange(0.0001, 10000.0);
    m_scale_spin->setDecimals(4);
    m_scale_spin->setSingleStep(0.1);
    m_scale_spin->setValue(1.0000);
    m_scale_spin->setSuffix(QStringLiteral(" x"));
    m_scale_spin->setFixedWidth(140);
    mult_layout->addWidget(mult_lbl);
    mult_layout->addWidget(m_scale_spin);
    mult_layout->addStretch(1);
    scale_layout->addLayout(mult_layout);

    // Quick presets row
    auto* quick_layout = new QHBoxLayout();
    auto* quick_lbl = new QLabel(QStringLiteral("Quick Presets:"), grp_scale);
    quick_lbl->setStyleSheet(QStringLiteral("color: #7d8492; font-size: 11px;"));
    quick_layout->addWidget(quick_lbl);

    m_btn_raw       = new QPushButton(QStringLiteral("1.0x (Raw)"), grp_scale);
    m_btn_m_to_ft   = new QPushButton(QStringLiteral("3.28x (m → ft)"), grp_scale);
    m_btn_sketchfab = new QPushButton(QStringLiteral("80.0x (Sketchfab)"), grp_scale);
    m_btn_cm_to_m   = new QPushButton(QStringLiteral("100.0x (cm → m)"), grp_scale);

    for (auto* btn : {m_btn_raw, m_btn_m_to_ft, m_btn_sketchfab, m_btn_cm_to_m}) {
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral("padding: 2px 8px; font-size: 11px;"));
        quick_layout->addWidget(btn);
    }
    quick_layout->addStretch(1);
    scale_layout->addLayout(quick_layout);

    // Auto-Fit Presets row
    auto* autofit_title = new QLabel(QStringLiteral("Auto-Fit to Swordigo In-Game Reference Scales:"), grp_scale);
    autofit_title->setStyleSheet(QStringLiteral("color: #7d8492; font-size: 11px; margin-top: 4px;"));
    scale_layout->addWidget(autofit_title);

    auto* autofit_layout = new QHBoxLayout();
    m_btn_hero  = new QPushButton(QStringLiteral("Hero/NPC ~70u"), grp_scale);
    m_btn_prop  = new QPushButton(QStringLiteral("Prop/Door ~100u"), grp_scale);
    m_btn_decor = new QPushButton(QStringLiteral("Decor/Tree ~250u"), grp_scale);
    m_btn_large = new QPushButton(QStringLiteral("Large/Boss ~500u"), grp_scale);

    for (auto* btn : {m_btn_hero, m_btn_prop, m_btn_decor, m_btn_large}) {
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral("padding: 2px 8px; font-size: 11px;"));
        btn->setEnabled(false);
        autofit_layout->addWidget(btn);
    }
    autofit_layout->addStretch(1);
    scale_layout->addLayout(autofit_layout);

    // Resulting Game Bounds
    m_bounds_label = new QLabel(grp_scale);
    m_bounds_label->setStyleSheet(QStringLiteral("font-size: 12px; margin-top: 6px;"));
    scale_layout->addWidget(m_bounds_label);

    auto* ref_label = new QLabel(QStringLiteral("(Reference: Hero=74u, Knight=61u, Statue=101u, Door=103u, Tree=459u | 1 unit ≈ 1 inch)"), grp_scale);
    ref_label->setStyleSheet(QStringLiteral("color: #5c6370; font-size: 11px; font-style: italic;"));
    scale_layout->addWidget(ref_label);

    layout->addWidget(grp_scale);

    // ── Group 3: Texture & Coordinates ───────────────────────────────────────
    auto* grp_tex = new QGroupBox(QStringLiteral("Texture & Coordinates"), content_widget);
    grp_tex->setStyleSheet(QStringLiteral("QGroupBox { font-weight: bold; color: #e5e9f0; }"));
    auto* tex_layout = new QVBoxLayout(grp_tex);
    tex_layout->setSpacing(8);

    m_flip_v_check = new QCheckBox(QStringLiteral("Flip V UVs (glTF/FBX top-origin → Game bottom-origin)"), grp_tex);
    m_flip_v_check->setChecked(true);
    m_flip_v_check->setToolTip(QStringLiteral("The Swordigo game engine samples texture UVs with V=0 at the bottom. Standard 3D formats use top-origin."));
    tex_layout->addWidget(m_flip_v_check);

    m_convert_tex_check = new QCheckBox(QStringLiteral("Convert textures to game .pvr (ETC1 compressed)"), grp_tex);
    m_convert_tex_check->setChecked(true);
    m_convert_tex_check->setToolTip(QStringLiteral("Re-encode referenced diffuse textures into raw PVR v3 ETC1 (.pvr) container used by the game engine."));
    tex_layout->addWidget(m_convert_tex_check);

    // PVR Resolution Row
    m_pvr_res_container = new QWidget(grp_tex);
    auto* pvr_row = new QHBoxLayout(m_pvr_res_container);
    pvr_row->setContentsMargins(20, 0, 0, 0);
    pvr_row->setSpacing(8);

    auto* res_lbl = new QLabel(QStringLiteral("PVR Texture Resolution:"), m_pvr_res_container);
    m_pvr_res_combo = new QComboBox(m_pvr_res_container);
    m_pvr_res_combo->addItem(QStringLiteral("Original (Source Resolution - Default)"), 0);
    m_pvr_res_combo->addItem(QStringLiteral("512 × 512 (Standard)"), 512);
    m_pvr_res_combo->addItem(QStringLiteral("1024 × 1024 (HD)"), 1024);
    m_pvr_res_combo->addItem(QStringLiteral("2048 × 2048 (Ultra HD)"), 2048);
    m_pvr_res_combo->addItem(QStringLiteral("4096 × 4096 (4K Cinema)"), 4096);
    m_pvr_res_combo->setFixedWidth(280);

    m_pvr_mode_badge = new QLabel(QStringLiteral("[Default 1:1]"), m_pvr_res_container);
    m_pvr_mode_badge->setStyleSheet(QStringLiteral("color: #98c379; font-weight: bold; font-size: 11px;"));

    pvr_row->addWidget(res_lbl);
    pvr_row->addWidget(m_pvr_res_combo);
    pvr_row->addWidget(m_pvr_mode_badge);
    pvr_row->addStretch(1);
    tex_layout->addWidget(m_pvr_res_container);

    m_filter_normals_check = new QCheckBox(QStringLiteral("Filter out non-diffuse maps (Normal maps, Roughness, Metallic, AO)"), grp_tex);
    m_filter_normals_check->setChecked(true);
    m_filter_normals_check->setToolTip(QStringLiteral("Swordigo only renders single-diffuse textures. Removes purple normal maps, roughness, and metallic maps so models render cleanly in-game."));
    tex_layout->addWidget(m_filter_normals_check);

    m_smart_naming_check = new QCheckBox(QStringLiteral("Smart Texture Naming (Replace generic 'texture0' with model name)"), grp_tex);
    m_smart_naming_check->setChecked(true);
    m_smart_naming_check->setToolTip(QStringLiteral("Replaces generic GLB texture names (texture0, texture1...) with the model name prefix so multiple models don't overwrite each other. Explicit texture names are kept as-is."));
    tex_layout->addWidget(m_smart_naming_check);

    m_textures_info_label = new QLabel(grp_tex);
    m_textures_info_label->setWordWrap(true);
    m_textures_info_label->setStyleSheet(QStringLiteral("font-size: 11px; color: #abb2bf; background: #21252b; border: 1px solid #3e4451; border-radius: 4px; padding: 6px; margin-top: 4px;"));
    m_textures_info_label->setVisible(false);
    tex_layout->addWidget(m_textures_info_label);

    layout->addWidget(grp_tex);

    // ── Group 4: Compatibility & Advanced ────────────────────────────────────
    auto* grp_compat = new QGroupBox(QStringLiteral("Swordigo Engine Compatibility"), content_widget);
    grp_compat->setStyleSheet(QStringLiteral("QGroupBox { font-weight: bold; color: #e5e9f0; }"));
    auto* compat_layout = new QVBoxLayout(grp_compat);
    compat_layout->setSpacing(8);

    m_rigid_skin_check = new QCheckBox(QStringLiteral("Rigid Skinning (Max-weight dominant bone bake for engine parity)"), grp_compat);
    m_rigid_skin_check->setChecked(true);
    m_rigid_skin_check->setToolTip(QStringLiteral("Swordigo's bone animator reads one bone index per vertex. Bakes vertex influences to the highest-weight bone."));
    compat_layout->addWidget(m_rigid_skin_check);

    auto* fps_row = new QHBoxLayout();
    auto* fps_lbl = new QLabel(QStringLiteral("Animation Resample FPS:"), grp_compat);
    m_anim_fps_spin = new QDoubleSpinBox(grp_compat);
    m_anim_fps_spin->setRange(1.0, 120.0);
    m_anim_fps_spin->setDecimals(1);
    m_anim_fps_spin->setValue(24.0);
    m_anim_fps_spin->setSuffix(QStringLiteral(" fps"));
    m_anim_fps_spin->setFixedWidth(100);
    m_anim_fps_spin->setToolTip(QStringLiteral("The engine's PODLoader hardcodes 24.0 FPS. Clips resampled at 24.0 fps match in-game timing."));
    fps_row->addWidget(fps_lbl);
    fps_row->addWidget(m_anim_fps_spin);
    fps_row->addStretch(1);
    compat_layout->addLayout(fps_row);

    m_overwrite_check = new QCheckBox(QStringLiteral("Overwrite existing files without prompting"), grp_compat);
    m_overwrite_check->setChecked(true);
    compat_layout->addWidget(m_overwrite_check);

    layout->addWidget(grp_compat);

    content_widget->setLayout(layout);
    scroll->setWidget(content_widget);
    main_layout->addWidget(scroll, 1);

    // ── Status & Progress ────────────────────────────────────────────────────
    m_progress_bar = new QProgressBar(this);
    m_progress_bar->setRange(0, 0); // Indeterminate
    m_progress_bar->setFixedHeight(4);
    m_progress_bar->setTextVisible(false);
    m_progress_bar->setVisible(false);
    main_layout->addWidget(m_progress_bar);

    m_status_label = new QLabel(this);
    m_status_label->setWordWrap(true);
    m_status_label->setStyleSheet(QStringLiteral("font-size: 11px; padding: 2px;"));
    main_layout->addWidget(m_status_label);

    // ── Action Buttons ───────────────────────────────────────────────────────
    auto* btn_layout = new QHBoxLayout();
    btn_layout->setSpacing(8);

    m_open_btn = new QPushButton(QStringLiteral("Open in Viewport"), this);
    m_open_btn->setCursor(Qt::PointingHandCursor);
    m_open_btn->setVisible(false);
    m_open_btn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #3e4451; color: #ffffff; border: 1px solid #4b5263; "
        "border-radius: 4px; padding: 6px 14px; font-weight: bold; } "
        "QPushButton:hover { background: #4b5263; }"));

    btn_layout->addWidget(m_open_btn);
    btn_layout->addStretch(1);

    m_cancel_btn = new QPushButton(QStringLiteral("Close"), this);
    m_cancel_btn->setCursor(Qt::PointingHandCursor);
    m_cancel_btn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #282c34; color: #abb2bf; border: 1px solid #3e4451; "
        "border-radius: 4px; padding: 6px 16px; font-weight: 600; } "
        "QPushButton:hover { background: #323742; color: #e5e9f0; }"));

    m_convert_btn = new QPushButton(QStringLiteral("✦ Convert Now"), this);
    m_convert_btn->setCursor(Qt::PointingHandCursor);
    m_convert_btn->setStyleSheet(QStringLiteral(
        "QPushButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #2962ff, stop:1 #00b0ff); "
        "color: #ffffff; border: none; border-radius: 4px; padding: 6px 20px; font-weight: bold; font-size: 12px; } "
        "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #3d74ff, stop:1 #1ec0ff); } "
        "QPushButton:disabled { background: #3e4451; color: #7d8492; }"));

    btn_layout->addWidget(m_cancel_btn);
    btn_layout->addWidget(m_convert_btn);
    main_layout->addLayout(btn_layout);

    // ── Signal Connections ───────────────────────────────────────────────────
    connect(m_source_browse_btn, &QPushButton::clicked, this, &ModelConvertDialog::onBrowseSource);
    connect(m_output_browse_btn, &QPushButton::clicked, this, &ModelConvertDialog::onBrowseOutput);
    connect(m_source_edit, &QLineEdit::textChanged, this, &ModelConvertDialog::onSourcePathEdited);

    connect(m_scale_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ModelConvertDialog::onScaleChanged);

    connect(m_btn_raw, &QPushButton::clicked, this, [this]() { m_scale_spin->setValue(1.0); });
    connect(m_btn_m_to_ft, &QPushButton::clicked, this, [this]() { m_scale_spin->setValue(3.28084); });
    connect(m_btn_sketchfab, &QPushButton::clicked, this, [this]() { m_scale_spin->setValue(80.0); });
    connect(m_btn_cm_to_m, &QPushButton::clicked, this, [this]() { m_scale_spin->setValue(100.0); });

    connect(m_btn_hero, &QPushButton::clicked, this, [this]() {
        if (m_cur_h > 0.0001f) m_scale_spin->setValue(70.0 / m_cur_h);
    });
    connect(m_btn_prop, &QPushButton::clicked, this, [this]() {
        if (m_cur_h > 0.0001f) m_scale_spin->setValue(100.0 / m_cur_h);
    });
    connect(m_btn_decor, &QPushButton::clicked, this, [this]() {
        if (m_cur_h > 0.0001f) m_scale_spin->setValue(250.0 / m_cur_h);
    });
    connect(m_btn_large, &QPushButton::clicked, this, [this]() {
        if (m_cur_h > 0.0001f) m_scale_spin->setValue(500.0 / m_cur_h);
    });

    connect(m_convert_tex_check, &QCheckBox::toggled, this, &ModelConvertDialog::onTexConvertToggled);
    connect(m_filter_normals_check, &QCheckBox::toggled, this, [this]() {
        analyze_source_model(m_source_edit->text());
    });
    connect(m_smart_naming_check, &QCheckBox::toggled, this, [this]() {
        analyze_source_model(m_source_edit->text());
    });
    connect(m_pvr_res_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ModelConvertDialog::onPvrResChanged);

    connect(m_convert_btn, &QPushButton::clicked, this, &ModelConvertDialog::onConvertClicked);
    connect(m_cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_open_btn, &QPushButton::clicked, this, &ModelConvertDialog::onOpenConvertedClicked);

    update_resulting_bounds();
}

void ModelConvertDialog::set_source_path(const QString& path) {
    m_source_edit->setText(path);
    onSourcePathEdited(path);
}

QString ModelConvertDialog::output_pod_path() const {
    return m_output_edit ? m_output_edit->text().trimmed() : QString();
}

void ModelConvertDialog::onBrowseSource() {
    const QString start_dir = m_source_edit->text().isEmpty()
        ? QString() : QFileInfo(m_source_edit->text()).absolutePath();

    const QString selected = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Select 3D Model"),
        start_dir,
        QStringLiteral("3D Models (*.glb *.gltf *.fbx *.obj *.pod);;glTF Binary (*.glb);;glTF JSON (*.gltf);;Autodesk FBX (*.fbx);;Wavefront OBJ (*.obj);;PowerVR POD (*.pod);;All Files (*.*)")
    );
    if (!selected.isEmpty()) {
        m_source_edit->setText(selected);
    }
}

void ModelConvertDialog::onBrowseOutput() {
    const QString start_dir = m_output_edit->text().isEmpty()
        ? QString() : QFileInfo(m_output_edit->text()).absolutePath();

    const QString selected = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Choose Output Game POD File"),
        start_dir,
        QStringLiteral("Game POD Model (*.POD *.pod);;All Files (*.*)")
    );
    if (!selected.isEmpty()) {
        m_output_edit->setText(selected);
    }
}

void ModelConvertDialog::onSourcePathEdited(const QString& text) {
    const QString clean = text.trimmed();
    if (clean.isEmpty()) {
        m_cur_w = m_cur_h = m_cur_d = 0.0f;
        m_mesh_count = m_vert_count = 0;
        m_dim_label->setText(QStringLiteral("Source Dimensions: 0.00 W × 0.00 H × 0.00 D units"));
        m_btn_hero->setEnabled(false);
        m_btn_prop->setEnabled(false);
        m_btn_decor->setEnabled(false);
        m_btn_large->setEnabled(false);
        update_resulting_bounds();
        return;
    }

    // Propose matching .POD output name next to the source
    QFileInfo fi(clean);
    QString out_pod = fi.absolutePath() + QDir::separator() + fi.completeBaseName() + QStringLiteral(".POD");
    m_output_edit->setText(out_pod);

    analyze_source_model(clean);
}

void ModelConvertDialog::analyze_source_model(const QString& file_path) {
    if (!QFile::exists(file_path)) {
        m_dim_label->setText(QStringLiteral("<span style='color:#e06c75;'>Source file not found</span>"));
        return;
    }

    const std::string path_std = file_path.toStdString();
    const std::string ext = QFileInfo(file_path).suffix().toLower().toStdString();

    av::PODModel model;
    std::string err;
    bool ok = false;

    if (ext == "glb") {
        std::vector<av::GLTFImageBuffer> imgs;
        ok = av::gltf_import_glb(path_std, model, imgs, &err);
    } else if (ext == "gltf") {
        std::vector<av::GLTFImageBuffer> imgs;
        ok = av::gltf_import_gltf(path_std, model, imgs, &err);
    } else if (ext == "fbx") {
        model = av::fbx_load(path_std);
        ok = !model.meshes.empty();
    } else if (ext == "obj") {
        ok = av::obj_load(path_std, model, &err);
    } else if (ext == "pod") {
        model = av::pod_load(path_std, "");
        ok = !model.meshes.empty();
    }

    if (!ok || model.meshes.empty()) {
        m_dim_label->setText(QStringLiteral("<span style='color:#e5c07b;'>Could not measure geometry bounds</span>"));
        m_btn_hero->setEnabled(false);
        m_btn_prop->setEnabled(false);
        m_btn_decor->setEnabled(false);
        m_btn_large->setEnabled(false);
        if (m_textures_info_label) m_textures_info_label->setVisible(false);
        return;
    }

    float mx0 = 1e30f, mx1 = -1e30f;
    float my0 = 1e30f, my1 = -1e30f;
    float mz0 = 1e30f, mz1 = -1e30f;
    int total_verts = 0;

    for (const auto& m : model.meshes) {
        total_verts += static_cast<int>(m.positions.size() / 3);
        for (size_t i = 0; i + 2 < m.positions.size(); i += 3) {
            mx0 = std::min(mx0, m.positions[i]);     mx1 = std::max(mx1, m.positions[i]);
            my0 = std::min(my0, m.positions[i + 1]); my1 = std::max(my1, m.positions[i + 1]);
            mz0 = std::min(mz0, m.positions[i + 2]); mz1 = std::max(mz1, m.positions[i + 2]);
        }
    }

    if (mx0 <= mx1 && my0 <= my1 && mz0 <= mz1) {
        m_cur_w = mx1 - mx0;
        m_cur_h = my1 - my0;
        m_cur_d = mz1 - mz0;
    } else {
        m_cur_w = m_cur_h = m_cur_d = 0.0f;
    }
    m_mesh_count = static_cast<int>(model.meshes.size());
    m_vert_count = total_verts;

    m_dim_label->setText(QString(
        "<b>Source Dimensions:</b> %1 W  ×  %2 H  ×  %3 D units &nbsp;&nbsp;"
        "<span style='color:#7d8492;'>(Meshes: %4, Verts: %5)</span>")
        .arg(QString::number(m_cur_w, 'f', 2))
        .arg(QString::number(m_cur_h, 'f', 2))
        .arg(QString::number(m_cur_d, 'f', 2))
        .arg(m_mesh_count)
        .arg(m_vert_count));

    if (m_cur_h > 0.0001f) {
        float s_hero  = 70.0f / m_cur_h;
        float s_prop  = 100.0f / m_cur_h;
        float s_decor = 250.0f / m_cur_h;
        float s_large = 500.0f / m_cur_h;

        m_btn_hero->setText(QString("Hero/NPC ~70u (%1x)").arg(QString::number(s_hero, 'f', 2)));
        m_btn_prop->setText(QString("Prop/Door ~100u (%1x)").arg(QString::number(s_prop, 'f', 2)));
        m_btn_decor->setText(QString("Decor/Tree ~250u (%1x)").arg(QString::number(s_decor, 'f', 2)));
        m_btn_large->setText(QString("Large/Boss ~500u (%1x)").arg(QString::number(s_large, 'f', 2)));

        m_btn_hero->setEnabled(true);
        m_btn_prop->setEnabled(true);
        m_btn_decor->setEnabled(true);
        m_btn_large->setEnabled(true);
    } else {
        m_btn_hero->setEnabled(false);
        m_btn_prop->setEnabled(false);
        m_btn_decor->setEnabled(false);
        m_btn_large->setEnabled(false);
    }

    if (m_textures_info_label) {
        if (model.texture_filenames.empty()) {
            m_textures_info_label->setText(QStringLiteral("<i>No referenced textures found (untextured or vertex-colored).</i>"));
            m_textures_info_label->setVisible(true);
        } else {
            QStringList lines;
            const std::string model_stem = QFileInfo(file_path).completeBaseName().toStdString();
            bool filter_normals = m_filter_normals_check && m_filter_normals_check->isChecked();
            bool smart_naming = m_smart_naming_check && m_smart_naming_check->isChecked();

            lines.append(QString("<b>Detected Textures (%1):</b>").arg(model.texture_filenames.size()));
            for (size_t i = 0; i < model.texture_filenames.size(); ++i) {
                const std::string& tname = model.texture_filenames[i];
                if (tname.empty()) continue;
                std::string tstem = tname;
                size_t dot = tstem.find_last_of('.');
                if (dot != std::string::npos) tstem = tstem.substr(0, dot);

                if (filter_normals && av::is_non_diffuse_texture_name(tstem)) {
                    lines.append(QString("• <span style='color:#7d8492;'>%1</span> → <span style='color:#e5c07b;'>[Filtered Non-Diffuse Map]</span>")
                        .arg(QString::fromStdString(tname)));
                } else {
                    std::string out_stem = av::resolve_output_texture_stem(model_stem, tstem, i, smart_naming);
                    std::string out_name = out_stem + ".pvr";
                    bool renamed = (out_stem != tstem);
                    lines.append(QString("• %1 → <b style='color:%2;'>%3</b>%4")
                        .arg(QString::fromStdString(tname))
                        .arg(renamed ? QStringLiteral("#98c379") : QStringLiteral("#61afef"))
                        .arg(QString::fromStdString(out_name))
                        .arg(renamed ? QStringLiteral(" <span style='color:#98c379;'>(Smart Renamed)</span>") : QString()));
                }
            }
            m_textures_info_label->setText(lines.join(QStringLiteral("<br/>")));
            m_textures_info_label->setVisible(true);
        }
    }

    update_resulting_bounds();
}

void ModelConvertDialog::onScaleChanged(double) {
    update_resulting_bounds();
}

void ModelConvertDialog::update_resulting_bounds() {
    float scale = static_cast<float>(m_scale_spin->value());
    float est_w = m_cur_w * scale;
    float est_h = m_cur_h * scale;
    float est_d = m_cur_d * scale;

    m_bounds_label->setText(QString(
        "<span style='color:#e5c07b; font-weight:bold;'>Resulting Game Bounds:</span> "
        "<span style='color:#ffffff; font-weight:600;'>%1 W  ×  %2 H  ×  %3 D units</span>")
        .arg(QString::number(est_w, 'f', 1))
        .arg(QString::number(est_h, 'f', 1))
        .arg(QString::number(est_d, 'f', 1)));
}

void ModelConvertDialog::onTexConvertToggled(bool checked) {
    if (m_pvr_res_container) m_pvr_res_container->setEnabled(checked);
    if (m_filter_normals_check) m_filter_normals_check->setEnabled(checked);
    if (m_smart_naming_check) m_smart_naming_check->setEnabled(checked);
    if (m_textures_info_label) m_textures_info_label->setEnabled(checked);
}

void ModelConvertDialog::onPvrResChanged(int index) {
    int val = m_pvr_res_combo->itemData(index).toInt();
    if (val == 0) {
        m_pvr_mode_badge->setText(QStringLiteral("[Default 1:1]"));
        m_pvr_mode_badge->setStyleSheet(QStringLiteral("color: #98c379; font-weight: bold; font-size: 11px;"));
    } else {
        m_pvr_mode_badge->setText(QStringLiteral("[HD Mode]"));
        m_pvr_mode_badge->setStyleSheet(QStringLiteral("color: #61afef; font-weight: bold; font-size: 11px;"));
    }
}

void ModelConvertDialog::set_working(bool working) {
    m_convert_btn->setEnabled(!working);
    m_source_browse_btn->setEnabled(!working);
    m_output_browse_btn->setEnabled(!working);
    m_progress_bar->setVisible(working);
}

void ModelConvertDialog::onConvertClicked() {
    const QString src = m_source_edit->text().trimmed();
    const QString dst = m_output_edit->text().trimmed();

    if (src.isEmpty() || !QFile::exists(src)) {
        m_status_label->setText(QStringLiteral("<span style='color:#e06c75;'>Error: Source 3D model file does not exist.</span>"));
        return;
    }
    if (dst.isEmpty()) {
        m_status_label->setText(QStringLiteral("<span style='color:#e06c75;'>Error: Please specify the target .POD output path.</span>"));
        return;
    }

    av::PodConvertOptions opts;
    opts.overwrite            = m_overwrite_check->isChecked();
    opts.scale                = static_cast<float>(m_scale_spin->value());
    opts.flip_v               = m_flip_v_check->isChecked();
    opts.convert_textures     = m_convert_tex_check->isChecked();
    opts.filter_non_diffuse   = m_filter_normals_check->isChecked();
    opts.smart_texture_naming = m_smart_naming_check->isChecked();
    opts.pvr_resolution       = m_pvr_res_combo->currentData().toInt();
    opts.output_pvr           = true;
    opts.rigid_skin           = m_rigid_skin_check->isChecked();
    opts.anim_fps             = static_cast<float>(m_anim_fps_spin->value());

    const std::string ext = QFileInfo(src).suffix().toLower().toStdString();
    const std::string src_std = src.toStdString();
    const std::string dst_std = dst.toStdString();

    set_working(true);
    m_open_btn->setVisible(false);
    m_status_label->setText(QStringLiteral("<span style='color:#61afef;'>Converting geometry and encoding ETC1 textures...</span>"));

    // Run conversion on a background thread so the GUI stays responsive during heavy texture compression
    QThread* worker = QThread::create([this, src_std, dst_std, ext, opts, dst]() {
        std::vector<std::string> written_tex;
        std::vector<std::string> written_clips;
        std::string err;
        bool ok = false;

        if (ext == "glb" || ext == "gltf") {
            ok = av::glb_to_pod(src_std, dst_std, opts, &written_tex, &written_clips, &err);
        } else if (ext == "obj") {
            ok = av::obj_to_pod(src_std, dst_std, opts, &written_tex, &err);
        } else {
            ok = av::fbx_to_pod(src_std, dst_std, opts, &written_tex, &err);
        }

        const size_t tex_count = written_tex.size();
        const size_t clip_count = written_clips.size();

        QMetaObject::invokeMethod(this, [this, ok, err, dst, opts, tex_count, clip_count]() {
            set_working(false);
            if (ok) {
                m_last_written_pod = dst;
                m_open_btn->setVisible(true);

                QString msg = QString(
                    "<span style='color:#98c379; font-weight:bold;'>✓ Successfully converted to Game POD!</span><br/>"
                    "<span style='color:#e5e9f0;'>Scale: %1x &nbsp;|&nbsp; Textures encoded: %2 &nbsp;|&nbsp; Clips: %3</span><br/>"
                    "<span style='color:#abb2bf;'>Saved to: %4</span>")
                    .arg(QString::number(opts.scale, 'f', 3))
                    .arg(tex_count)
                    .arg(clip_count)
                    .arg(QFileInfo(dst).fileName());

                m_status_label->setText(msg);
                ruby::core::ProjectContext::instance().set_status(
                    "Model converted to POD: " + dst);
                emit conversionSucceeded(dst);
            } else {
                QString err_msg = err.empty() ? QStringLiteral("Unknown conversion failure") : QString::fromStdString(err);
                m_status_label->setText(QString("<span style='color:#e06c75; font-weight:bold;'>✗ Conversion failed:</span> %1").arg(err_msg));
            }
        }, Qt::QueuedConnection);
    });

    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void ModelConvertDialog::onOpenConvertedClicked() {
    if (!m_last_written_pod.isEmpty() && QFile::exists(m_last_written_pod)) {
        emit conversionSucceeded(m_last_written_pod);
        accept();
    }
}

} // namespace ruby::editor

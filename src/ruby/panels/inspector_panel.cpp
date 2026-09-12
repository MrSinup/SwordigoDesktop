// ============================================================================
// inspector_panel.cpp — Professional Object & Asset Properties Inspector for Ruby GG
// ============================================================================

#include "inspector_panel.h"
#include <QFileInfo>
#include <QHeaderView>
#include <QScrollArea>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include <QComboBox>
#include <QToolButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QFont>
#include <cmath>
#include <optional>

namespace ruby::panels {

static av::SceneComponent s_component_clipboard;
static bool s_has_component_clipboard = false;

// ── Raw-bytes hex helpers (unknown / undocumented protobuf fields) ──────────
static QString hex_encode(const std::string& bytes) {
    static const char* digits = "0123456789ABCDEF";
    QString out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        out.append(QChar(digits[c >> 4]));
        out.append(QChar(digits[c & 0xF]));
    }
    return out;
}

static std::optional<std::string> hex_decode(const QString& text) {
    const QString t = text.trimmed();
    if (t.size() % 2 != 0) return std::nullopt;
    std::string out;
    out.reserve(static_cast<size_t>(t.size() / 2));
    bool ok = true;
    for (int i = 0; i < t.size(); i += 2) {
        bool hi_ok = false, lo_ok = false;
        const int hi = t.mid(i, 1).toInt(&hi_ok, 16);
        const int lo = t.mid(i + 1, 1).toInt(&lo_ok, 16);
        if (!hi_ok || !lo_ok) { ok = false; break; }
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    if (!ok) return std::nullopt;
    return out;
}

// VARINT fields the game treats as booleans — shown as checkboxes (web editor
// `control: "toggle"` set + the obvious bool siblings). Everything else stays
// a plain integer spinbox.
static bool is_bool_field(const av::SceneComponentField& f) {
    static const char* kBoolNames[] = {
        "Hidden", "Locked", "Closed", "IsGround", "Collides",
        "ReceivesDamage", "InflictsDamage", "Enabled", "UnsafeGround",
        "ExecuteOnce", "StandAlone"
    };
    for (const char* n : kBoolNames)
        if (f.name == n) return true;
    return false;
}

// SpecialType is the one documented enum (web editor dropdown):
//   0:None 1:Pickup 2:Portal 3:Collectable 4:Use 5:Blocks Damage
//   6:Grabbable 7:Pushable
static bool is_special_type(const av::SceneComponentField& f) {
    return f.name == "SpecialType";
}

// One field row → schema-matched control. Every control eventually emits
// componentFieldChanged with a mutated copy of the field, which the window
// persists via scene_set_component_field + snapshot undo.
static QWidget* make_field_editor(ruby::panels::InspectorPanel* panel, QWidget* parent,
                                  int object_index, int component_index,
                                  av::SceneComponentField f) {
    const QString label = QString::fromStdString(f.name);

    // Program scripts → "Edit Lua" button opening a small Lua editor dialog.
    if (f.is_message && f.class_name == "Program") {
        auto* btn = new QPushButton("Edit Lua…", parent);
        btn->setToolTip("Open the embedded Lua program in an editor");
        QObject::connect(btn, &QPushButton::clicked, panel, [panel, object_index, component_index, f]() mutable {
            QDialog dlg(panel);
            dlg.setWindowTitle("Edit Program — " + QString::fromStdString(f.name));
            dlg.resize(560, 420);
            auto* lay = new QVBoxLayout(&dlg);
            auto* editor = new QPlainTextEdit(&dlg);
            QFont mono("Monospace");
            mono.setStyleHint(QFont::TypeWriter);
            editor->setFont(mono);
            editor->setPlainText(QString::fromStdString(av::scene_program_source(f.bytes_value)));
            lay->addWidget(editor);
            auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
            QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            lay->addWidget(btns);
            if (dlg.exec() == QDialog::Accepted) {
                std::string new_bytes = f.bytes_value;
                std::string lua_error;
                const bool ok = av::scene_set_program_source(
                    new_bytes, editor->toPlainText().toStdString(), &lua_error);
                // Always persist: the source is stored even when it does not
                // compile, and the stale bytecode is dropped so the game can
                // never execute an older chunk than the editor shows.
                f.bytes_value = std::move(new_bytes);
                emit panel->componentFieldChanged(object_index, component_index, f);
                if (!ok && !lua_error.empty())
                    QMessageBox::warning(panel, "Lua compile error",
                                         QString::fromStdString(lua_error));
            }
        });
        return btn;
    }

    // Nested messages → group box with an editable hex view (recursive field
    // enumeration is out of scope; the bytes round-trip untouched).
    if (f.is_message) {
        const QString cn = QString::fromStdString(f.class_name.empty() ? f.name : f.class_name);
        auto* box = new QGroupBox(cn + " (nested, " + QString::number(f.bytes_value.size()) + " B)", parent);
        auto* bl = new QVBoxLayout(box);
        bl->setContentsMargins(4, 4, 4, 4);
        auto* hex = new QLineEdit(hex_encode(f.bytes_value), box);
        QFont mono("Monospace");
        mono.setStyleHint(QFont::TypeWriter);
        hex->setFont(mono);
        hex->setToolTip("Raw nested-message bytes (hex). Editable — the change round-trips.");
        QObject::connect(hex, &QLineEdit::editingFinished, panel,
                         [panel, object_index, component_index, f, hex]() mutable {
            auto decoded = hex_decode(hex->text());
            if (decoded) {
                f.bytes_value = std::move(*decoded);
                emit panel->componentFieldChanged(object_index, component_index, f);
            } else {
                hex->setText(hex_encode(f.bytes_value));   // invalid hex → revert
            }
        });
        bl->addWidget(hex);
        return box;
    }

    if (f.wire_type == proto::WIRE_VARINT) {
        if (is_special_type(f)) {
            auto* combo = new QComboBox(parent);
            combo->addItem("0: None", 0);
            combo->addItem("1: Pickup", 1);
            combo->addItem("2: Portal", 2);
            combo->addItem("3: Collectable", 3);
            combo->addItem("4: Use", 4);
            combo->addItem("5: Blocks Damage", 5);
            combo->addItem("6: Grabbable", 6);
            combo->addItem("7: Pushable", 7);
            const int idx = combo->findData(static_cast<int>(f.varint_value));
            if (idx >= 0) combo->setCurrentIndex(idx);
            QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), panel,
                             [panel, object_index, component_index, f, combo](int i) mutable {
                f.varint_value = static_cast<uint64_t>(combo->itemData(i).toInt());
                emit panel->componentFieldChanged(object_index, component_index, f);
            });
            return combo;
        }
        if (is_bool_field(f)) {
            auto* cb = new QCheckBox(parent);
            cb->setChecked(f.varint_value != 0);
            QObject::connect(cb, &QCheckBox::toggled, panel,
                             [panel, object_index, component_index, f](bool on) mutable {
                f.varint_value = on ? 1 : 0;
                emit panel->componentFieldChanged(object_index, component_index, f);
            });
            return cb;
        }
        auto* sb = new QSpinBox(parent);
        sb->setRange(0, 2147483647);
        sb->setValue(static_cast<int>(f.varint_value));
        QObject::connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), panel,
                         [panel, object_index, component_index, f](int v) mutable {
            f.varint_value = static_cast<uint64_t>(v);
            emit panel->componentFieldChanged(object_index, component_index, f);
        });
        return sb;
    }

    if (f.wire_type == proto::WIRE_I32) {
        auto* sb = new QDoubleSpinBox(parent);
        sb->setRange(-999999.0, 999999.0);
        sb->setValue(f.float_value);
        sb->setDecimals(3);
        QObject::connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), panel,
                         [panel, object_index, component_index, f](double v) mutable {
            f.float_value = static_cast<float>(v);
            emit panel->componentFieldChanged(object_index, component_index, f);
        });
        return sb;
    }

    if (f.wire_type == proto::WIRE_I64) {
        auto* sb = new QDoubleSpinBox(parent);
        sb->setRange(-999999.0, 999999.0);
        sb->setValue(f.double_value);
        sb->setDecimals(4);
        QObject::connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), panel,
                         [panel, object_index, component_index, f](double v) mutable {
            f.double_value = v;
            emit panel->componentFieldChanged(object_index, component_index, f);
        });
        return sb;
    }

    // LEN: printable string → line edit; binary → editable hex (unknown fields
    // from newer game versions still round-trip).
    if (f.bytes_value.find('\0') == std::string::npos &&
        f.bytes_value.size() < 2048 && !f.bytes_value.empty()) {
        auto* edit = new QLineEdit(QString::fromStdString(f.bytes_value), parent);
        QObject::connect(edit, &QLineEdit::editingFinished, panel,
                         [panel, object_index, component_index, f, edit]() mutable {
            f.bytes_value = edit->text().toStdString();
            emit panel->componentFieldChanged(object_index, component_index, f);
        });
        return edit;
    }
    if (f.bytes_value.size() < 4096) {
        auto* hex = new QLineEdit(hex_encode(f.bytes_value), parent);
        QFont mono("Monospace");
        mono.setStyleHint(QFont::TypeWriter);
        hex->setFont(mono);
        hex->setToolTip(QString("Raw bytes (hex, %1 B). Unknown field — still editable and round-trips.")
                            .arg(f.bytes_value.size()));
        QObject::connect(hex, &QLineEdit::editingFinished, panel,
                         [panel, object_index, component_index, f, hex]() mutable {
            auto decoded = hex_decode(hex->text());
            if (decoded) {
                f.bytes_value = std::move(*decoded);
                emit panel->componentFieldChanged(object_index, component_index, f);
            } else {
                hex->setText(hex_encode(f.bytes_value));   // invalid hex → revert
            }
        });
        return hex;
    }
    auto* lbl = new QLabel(QString("<%1 bytes>").arg(f.bytes_value.size()), parent);
    lbl->setToolTip("Field too large to edit inline; hex-edit at the component level is not supported.");
    return lbl;
}

InspectorPanel::InspectorPanel(QWidget* parent) : QWidget(parent) {
    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    m_scroll_area = new QScrollArea(this);
    m_scroll_area->setWidgetResizable(true);
    m_scroll_area->setFrameShape(QFrame::NoFrame);
    m_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    root_layout->addWidget(m_scroll_area);

    m_container = new QWidget(m_scroll_area);
    m_container_layout = new QVBoxLayout(m_container);
    m_container_layout->setContentsMargins(8, 8, 8, 8);
    m_container_layout->setSpacing(8);
    m_scroll_area->setWidget(m_container);

    // ── Header (Title & Subtitle) ──
    auto* header_widget = new QWidget(m_container);
    auto* header_layout = new QVBoxLayout(header_widget);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(2);

    m_title_label = new QLabel("INSPECTOR", header_widget);
    QFont title_font = m_title_label->font();
    title_font.setPointSize(11);
    title_font.setBold(true);
    m_title_label->setFont(title_font);
    m_title_label->setStyleSheet("color: #f1f5f9;");
    header_layout->addWidget(m_title_label);

    m_subtitle_label = new QLabel("No selection", header_widget);
    m_subtitle_label->setStyleSheet("color: #94a3b8; font-size: 11px;");
    m_subtitle_label->setWordWrap(true);
    header_layout->addWidget(m_subtitle_label);

    m_container_layout->addWidget(header_widget);

    // ── Asset Stats Group ──
    m_stats_group = new QGroupBox("Asset Properties", m_container);
    auto* stats_layout = new QFormLayout(m_stats_group);
    stats_layout->setContentsMargins(8, 8, 8, 8);
    stats_layout->setSpacing(6);
    m_mesh_count_label = new QLabel("-", m_stats_group);
    m_vert_count_label = new QLabel("-", m_stats_group);
    stats_layout->addRow("Meshes:", m_mesh_count_label);
    stats_layout->addRow("Vertices:", m_vert_count_label);
    m_container_layout->addWidget(m_stats_group);
    m_stats_group->hide();

    // ── Object Identity Group ──
    m_identity_group = new QGroupBox("Identity", m_container);
    auto* identity_layout = new QFormLayout(m_identity_group);
    identity_layout->setContentsMargins(8, 8, 8, 8);
    identity_layout->setSpacing(6);

    m_name_edit = new QLineEdit(m_identity_group);
    m_name_edit->setPlaceholderText("(unnamed object)");
    m_template_edit = new QLineEdit(m_identity_group);
    m_template_edit->setPlaceholderText("(no template)");
    m_hidden_check = new QCheckBox("Hidden from viewport", m_identity_group);

    identity_layout->addRow("Name:", m_name_edit);
    identity_layout->addRow("Template:", m_template_edit);
    identity_layout->addRow("", m_hidden_check);

    connect(m_name_edit, &QLineEdit::editingFinished, this, &InspectorPanel::on_identity_changed);
    connect(m_template_edit, &QLineEdit::editingFinished, this, &InspectorPanel::on_identity_changed);
    connect(m_hidden_check, &QCheckBox::toggled, this, &InspectorPanel::on_hidden_toggled);
    m_container_layout->addWidget(m_identity_group);

    // ── Transform Group ──
    m_transform_group = new QGroupBox("Transform", m_container);
    auto* transform_layout = new QVBoxLayout(m_transform_group);
    transform_layout->setContentsMargins(8, 8, 8, 8);
    transform_layout->setSpacing(6);

    auto make_sb = [this](QDoubleSpinBox*& sb, double min_v, double max_v, double step, int dec, double def_v) {
        sb = new QDoubleSpinBox(m_transform_group);
        sb->setRange(min_v, max_v);
        sb->setSingleStep(step);
        sb->setDecimals(dec);
        sb->setValue(def_v);
        sb->setButtonSymbols(QAbstractSpinBox::PlusMinus);
        connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &InspectorPanel::on_transform_spinbox_changed);
    };

    // Position (X, Y, Z)
    auto* pos_box = new QWidget(m_transform_group);
    auto* pos_layout = new QHBoxLayout(pos_box);
    pos_layout->setContentsMargins(0, 0, 0, 0);
    pos_layout->setSpacing(4);
    make_sb(m_pos_x, -99999.0, 99999.0, 1.0, 2, 0.0);
    make_sb(m_pos_y, -99999.0, 99999.0, 1.0, 2, 0.0);
    make_sb(m_pos_z, -99999.0, 99999.0, 0.5, 3, 0.0);
    auto* pos_lbl = new QLabel("Pos:", pos_box);
    pos_lbl->setFixedWidth(30);
    pos_layout->addWidget(pos_lbl);
    pos_layout->addWidget(new QLabel("X", pos_box)); pos_layout->addWidget(m_pos_x);
    pos_layout->addWidget(new QLabel("Y", pos_box)); pos_layout->addWidget(m_pos_y);
    pos_layout->addWidget(new QLabel("Z", pos_box)); pos_layout->addWidget(m_pos_z);
    transform_layout->addWidget(pos_box);

    // Rotation (Rot X, Rot Z, Rot Y)
    auto* rot_box = new QWidget(m_transform_group);
    auto* rot_layout = new QHBoxLayout(rot_box);
    rot_layout->setContentsMargins(0, 0, 0, 0);
    rot_layout->setSpacing(4);
    make_sb(m_rot_x, -360.0, 360.0, 15.0, 1, 0.0);
    make_sb(m_rot_z, -360.0, 360.0, 15.0, 1, 0.0);
    make_sb(m_rot_y, -360.0, 360.0, 15.0, 1, 0.0);
    auto* rot_lbl = new QLabel("Rot:", rot_box);
    rot_lbl->setFixedWidth(30);
    rot_layout->addWidget(rot_lbl);
    rot_layout->addWidget(new QLabel("X", rot_box)); rot_layout->addWidget(m_rot_x);
    rot_layout->addWidget(new QLabel("Z", rot_box)); rot_layout->addWidget(m_rot_z);
    rot_layout->addWidget(new QLabel("Y", rot_box)); rot_layout->addWidget(m_rot_y);
    transform_layout->addWidget(rot_box);

    // Quick-angle rotation presets (ImGui inspector parity): 0/90/180/270° on
    // the Y (facing) axis, routed through the existing spinbox path so the
    // change is undoable exactly like a typed rotation.
    auto* preset_box = new QWidget(m_transform_group);
    auto* preset_layout = new QHBoxLayout(preset_box);
    preset_layout->setContentsMargins(0, 0, 0, 0);
    preset_layout->setSpacing(4);
    auto* preset_lbl = new QLabel("Y:", preset_box);
    preset_lbl->setFixedWidth(30);
    preset_layout->addWidget(preset_lbl);
    for (double a : {0.0, 90.0, 180.0, 270.0}) {
        auto* b = new QPushButton(QString("%1°").arg(static_cast<int>(a)), preset_box);
        b->setObjectName(QString("rotPreset%1").arg(static_cast<int>(a)));
        b->setStyleSheet("font-size: 10px; padding: 2px 6px;");
        b->setToolTip("Set Y rotation to " + QString::number(static_cast<int>(a)) + "°");
        connect(b, &QPushButton::clicked, this, [this, a]() {
            if (m_block_signals || m_inspected_object_idx < 0) return;
            m_rot_y->setValue(a);   // valueChanged → on_transform_spinbox_changed
        });
        preset_layout->addWidget(b);
    }
    preset_layout->addStretch();
    transform_layout->addWidget(preset_box);

    // Scale (X, Y, Z) + Uniform
    auto* scl_box = new QWidget(m_transform_group);
    auto* scl_layout = new QHBoxLayout(scl_box);
    scl_layout->setContentsMargins(0, 0, 0, 0);
    scl_layout->setSpacing(4);
    make_sb(m_scale_x, 0.001, 1000.0, 0.1, 3, 1.0);
    make_sb(m_scale_y, 0.001, 1000.0, 0.1, 3, 1.0);
    make_sb(m_scale_z, 0.001, 1000.0, 0.1, 3, 1.0);
    auto* scl_lbl = new QLabel("Scl:", scl_box);
    scl_lbl->setFixedWidth(30);
    scl_layout->addWidget(scl_lbl);
    scl_layout->addWidget(new QLabel("X", scl_box)); scl_layout->addWidget(m_scale_x);
    scl_layout->addWidget(new QLabel("Y", scl_box)); scl_layout->addWidget(m_scale_y);
    scl_layout->addWidget(new QLabel("Z", scl_box)); scl_layout->addWidget(m_scale_z);
    transform_layout->addWidget(scl_box);

    m_uniform_scale = new QCheckBox("Uniform Scaling (lock XYZ)", m_transform_group);
    m_uniform_scale->setChecked(true);
    transform_layout->addWidget(m_uniform_scale);

    m_container_layout->addWidget(m_transform_group);

    // ── Asset References Group ──
    m_refs_group = new QGroupBox("Asset References", m_container);
    auto* refs_layout = new QFormLayout(m_refs_group);
    refs_layout->setContentsMargins(8, 8, 8, 8);
    refs_layout->setSpacing(4);
    m_mesh_name_label = new QLabel("-", m_refs_group);
    m_texture_name_label = new QLabel("-", m_refs_group);
    m_bg_name_label = new QLabel("-", m_refs_group);
    refs_layout->addRow("Mesh:", m_mesh_name_label);
    refs_layout->addRow("Texture:", m_texture_name_label);
    refs_layout->addRow("Background:", m_bg_name_label);
    m_container_layout->addWidget(m_refs_group);

    // ── Components Group ──
    m_components_group = new QGroupBox("Components", m_container);
    m_components_layout = new QVBoxLayout(m_components_group);
    m_components_layout->setContentsMargins(8, 8, 8, 8);
    m_components_layout->setSpacing(6);

    // Add / Paste toolbar
    auto* comp_bar = new QHBoxLayout();
    m_add_comp_combo = new QComboBox(m_components_group);
    const auto comp_types = av::scene_component_types();
    for (const auto& t : comp_types) m_add_comp_combo->addItem(QString::fromStdString(t));
    m_add_comp_btn = new QPushButton("+ Add", m_components_group);
    m_paste_comp_btn = new QPushButton("Paste", m_components_group);
    m_paste_comp_btn->setEnabled(s_has_component_clipboard);

    comp_bar->addWidget(m_add_comp_combo, 1);
    comp_bar->addWidget(m_add_comp_btn);
    comp_bar->addWidget(m_paste_comp_btn);
    m_components_layout->addLayout(comp_bar);

    connect(m_add_comp_btn, &QPushButton::clicked, this, [this]() {
        if (m_inspected_object_idx >= 0 && m_add_comp_combo->count() > 0) {
            emit componentAdded(m_inspected_object_idx, m_add_comp_combo->currentText());
        }
    });
    connect(m_paste_comp_btn, &QPushButton::clicked, this, [this]() {
        if (m_inspected_object_idx >= 0 && s_has_component_clipboard) {
            emit componentPasted(m_inspected_object_idx, s_component_clipboard);
        }
    });

    m_container_layout->addWidget(m_components_group);

    // ── Ground Meshes Group ──
    m_ground_mesh_group = new QGroupBox("Ground Meshes", m_container);
    m_ground_mesh_layout = new QVBoxLayout(m_ground_mesh_group);
    m_ground_mesh_layout->setContentsMargins(8, 8, 8, 8);
    m_ground_mesh_layout->setSpacing(6);
    m_container_layout->addWidget(m_ground_mesh_group);

    m_container_layout->addStretch();
    clear_inspection();
}

void InspectorPanel::clear_inspection() {
    m_inspected_object_idx = -1;
    m_title_label->setText("INSPECTOR");
    m_subtitle_label->setText("No selection");
    m_stats_group->hide();
    m_identity_group->hide();
    m_transform_group->hide();
    m_refs_group->hide();
    m_components_group->hide();
    m_ground_mesh_group->hide();
}

void InspectorPanel::inspect_file(const QString& path) {
    clear_inspection();
    QFileInfo fi(path);
    m_title_label->setText(fi.fileName());
    m_subtitle_label->setText(path);
}

void InspectorPanel::inspect_model_info(const QString& name, int meshCount, int vertCount) {
    clear_inspection();
    m_title_label->setText(name);
    m_subtitle_label->setText("3D POD Model");
    m_mesh_count_label->setText(QString::number(meshCount));
    m_vert_count_label->setText(QString::number(vertCount));
    m_stats_group->show();
}

void InspectorPanel::inspect_scene_object(const av::SceneData& scene, int object_index) {
    if (object_index < 0 || object_index >= static_cast<int>(scene.objects.size())) {
        clear_inspection();
        return;
    }

    m_inspected_object_idx = object_index;
    const auto& obj = scene.objects[object_index];

    m_block_signals = true;

    QString obj_name = QString::fromStdString(obj.name);
    m_title_label->setText(obj_name.isEmpty() ? QString("Object #%1").arg(object_index) : obj_name);
    m_subtitle_label->setText(QString("Scene Object #%1 • Template: %2")
        .arg(object_index)
        .arg(obj.template_name.empty() ? "(none)" : QString::fromStdString(obj.template_name)));

    m_name_edit->setText(obj_name);
    m_template_edit->setText(QString::fromStdString(obj.template_name));
    m_hidden_check->setChecked(obj.hidden);

    // Transform
    m_pos_x->setValue(obj.pos_x);
    m_pos_y->setValue(obj.pos_y);
    m_pos_z->setValue(obj.pos_z);

    m_rot_x->setValue(obj.rot_x);
    m_rot_z->setValue(obj.rot_z);
    m_rot_y->setValue(obj.rot_y);

    m_scale_x->setValue(obj.scale_x);
    m_scale_y->setValue(obj.scale_y);
    m_scale_z->setValue(obj.scale_z);

    // References
    m_mesh_name_label->setText(obj.mesh_name.empty() ? "(none)" : QString::fromStdString(obj.mesh_name));
    m_texture_name_label->setText(obj.texture_name.empty() ? "(none)" : QString::fromStdString(obj.texture_name));
    m_bg_name_label->setText(obj.background_name.empty() ? "(none)" : QString::fromStdString(obj.background_name));

    // Show appropriate groups
    m_stats_group->hide();
    m_identity_group->show();
    m_transform_group->show();
    m_refs_group->show();

    // Components & Ground Meshes
    rebuild_components_ui(obj);
    rebuild_ground_meshes_ui(obj);

    m_block_signals = false;
}

void InspectorPanel::update_transform(float px, float py, float pz,
                                      float rx, float rz, float ry,
                                      float sx, float sy, float sz) {
    if (m_inspected_object_idx < 0) return;
    m_block_signals = true;
    m_pos_x->setValue(px);
    m_pos_y->setValue(py);
    m_pos_z->setValue(pz);
    m_rot_x->setValue(rx);
    m_rot_z->setValue(rz);
    m_rot_y->setValue(ry);
    m_scale_x->setValue(sx);
    m_scale_y->setValue(sy);
    m_scale_z->setValue(sz);
    m_block_signals = false;
}

void InspectorPanel::on_transform_spinbox_changed() {
    if (m_block_signals || m_inspected_object_idx < 0) return;

    if (m_uniform_scale->isChecked()) {
        QObject* sender_obj = sender();
        m_block_signals = true;
        if (sender_obj == m_scale_x) {
            double v = m_scale_x->value();
            m_scale_y->setValue(v);
            m_scale_z->setValue(v);
        } else if (sender_obj == m_scale_y) {
            double v = m_scale_y->value();
            m_scale_x->setValue(v);
            m_scale_z->setValue(v);
        } else if (sender_obj == m_scale_z) {
            double v = m_scale_z->value();
            m_scale_x->setValue(v);
            m_scale_y->setValue(v);
        }
        m_block_signals = false;
    }

    emit objectTransformChanged(m_inspected_object_idx,
                                static_cast<float>(m_pos_x->value()),
                                static_cast<float>(m_pos_y->value()),
                                static_cast<float>(m_pos_z->value()),
                                static_cast<float>(m_rot_x->value()),
                                static_cast<float>(m_rot_z->value()),
                                static_cast<float>(m_rot_y->value()),
                                static_cast<float>(m_scale_x->value()),
                                static_cast<float>(m_scale_y->value()),
                                static_cast<float>(m_scale_z->value()));
}

void InspectorPanel::on_identity_changed() {
    if (m_block_signals || m_inspected_object_idx < 0) return;
    emit objectIdentityChanged(m_inspected_object_idx, m_name_edit->text(), m_template_edit->text());
}

void InspectorPanel::on_hidden_toggled(bool checked) {
    if (m_block_signals || m_inspected_object_idx < 0) return;
    emit objectHiddenChanged(m_inspected_object_idx, checked);
}

QGroupBox* InspectorPanel::make_component_widget(const av::SceneComponent& c, int component_index) {
    auto* comp_widget = new QGroupBox(QString("%1 [ID: %2]").arg(QString::fromStdString(c.type_name)).arg(c.type_id), m_components_group);
    comp_widget->setStyleSheet("QGroupBox { background: #1c2028; border: 1px solid #2d3340; border-radius: 4px; margin-top: 10px; padding: 6px; font-size: 11px; }"
                               "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #60a5fa; }");
    auto* comp_layout = new QVBoxLayout(comp_widget);
    comp_layout->setContentsMargins(4, 8, 4, 4);
    comp_layout->setSpacing(4);

    // Fields
    const auto fields = av::scene_component_fields(c);
    if (!fields.empty()) {
        auto* form = new QFormLayout();
        form->setContentsMargins(0, 0, 0, 0);
        form->setSpacing(4);

        for (const av::SceneComponentField& f : fields) {
            QWidget* editor = make_field_editor(this, comp_widget, m_inspected_object_idx,
                                                component_index, f);
            form->addRow(QString::fromStdString(f.name) + ":", editor);
        }
        comp_layout->addLayout(form);
    } else {
        auto* empty_lbl = new QLabel("Payload empty or unavailable", comp_widget);
        empty_lbl->setStyleSheet("color: #64748b; font-style: italic;");
        comp_layout->addWidget(empty_lbl);
    }

    // Action buttons
    auto* btn_row = new QHBoxLayout();
    auto* copy_btn = new QPushButton("Copy", comp_widget);
    copy_btn->setStyleSheet("font-size: 10px; padding: 2px 6px;");
    connect(copy_btn, &QPushButton::clicked, this, [c, this]() {
        s_component_clipboard = c;
        s_has_component_clipboard = true;
        m_paste_comp_btn->setEnabled(true);
    });

    auto* remove_btn = new QPushButton("Remove", comp_widget);
    remove_btn->setStyleSheet("font-size: 10px; padding: 2px 6px; color: #f87171;");
    connect(remove_btn, &QPushButton::clicked, this, [this, component_index]() {
        emit componentRemoved(m_inspected_object_idx, component_index);
    });

    btn_row->addStretch();
    btn_row->addWidget(copy_btn);
    btn_row->addWidget(remove_btn);
    comp_layout->addLayout(btn_row);

    return comp_widget;
}

void InspectorPanel::rebuild_components_ui(const av::SceneObject& obj) {
    // Clear old component widgets (keep top toolbar at index 0)
    while (m_components_layout->count() > 1) {
        auto* item = m_components_layout->takeAt(1);
        if (item->widget()) delete item->widget();
        delete item;
    }

    m_paste_comp_btn->setEnabled(s_has_component_clipboard);
    m_components_group->setTitle(QString("Components (%1)").arg(obj.components.size()));

    for (size_t ci = 0; ci < obj.components.size(); ++ci) {
        m_components_layout->addWidget(make_component_widget(obj.components[ci], static_cast<int>(ci)));
    }
    m_components_group->show();
}

void InspectorPanel::append_component(const av::SceneComponent& comp) {
    if (m_inspected_object_idx < 0) return;
    // The component is already in the scene (the host mutated it before calling
    // us). Count existing component widgets (exclude the toolbar at index 0)
    // to determine the component index for the new widget.
    int component_index = m_components_layout->count() > 1 ? m_components_layout->count() - 1 : 0;
    m_components_layout->addWidget(make_component_widget(comp, component_index));
    m_components_group->setTitle(QString("Components (%1)").arg(component_index + 1));
    m_paste_comp_btn->setEnabled(s_has_component_clipboard);
    m_components_group->show();
}

void InspectorPanel::rebuild_ground_meshes_ui(const av::SceneObject& obj) {
    while (m_ground_mesh_layout->count() > 0) {
        auto* item = m_ground_mesh_layout->takeAt(0);
        if (item->widget()) delete item->widget();
        delete item;
    }

    if (obj.ground_meshes.empty()) {
        m_ground_mesh_group->hide();
        return;
    }

    m_ground_mesh_group->setTitle(QString("Ground Meshes (%1)").arg(obj.ground_meshes.size()));

    auto* regen_btn = new QPushButton("Regenerate Normals", m_ground_mesh_group);
    regen_btn->setStyleSheet("background: #2563eb; color: white; font-size: 11px; padding: 4px; border-radius: 3px;");
    connect(regen_btn, &QPushButton::clicked, this, [this]() {
        emit groundMeshRegenNormals(m_inspected_object_idx);
    });
    m_ground_mesh_layout->addWidget(regen_btn);

    for (size_t mi = 0; mi < obj.ground_meshes.size(); ++mi) {
        const auto& gm = obj.ground_meshes[mi];
        QString tex = (mi < obj.ground_mesh_textures.size() && !obj.ground_mesh_textures[mi].empty())
                          ? QString::fromStdString(obj.ground_mesh_textures[mi]) : "(none)";
        int verts = static_cast<int>(gm.positions.size() / 3);
        int tris = static_cast<int>(gm.indices.empty() ? (gm.positions.size() / 9) : (gm.indices.size() / 3));

        auto* sub_box = new QGroupBox(QString("Submesh #%1").arg(mi), m_ground_mesh_group);
        sub_box->setStyleSheet("QGroupBox { background: #1a1e26; border: 1px solid #2a313e; border-radius: 3px; margin-top: 8px; padding: 4px; font-size: 10px; }");
        auto* form = new QFormLayout(sub_box);
        form->setContentsMargins(4, 6, 4, 4);
        form->setSpacing(2);
        form->addRow("Texture:", new QLabel(tex, sub_box));
        form->addRow("Geometry:", new QLabel(QString("%1 vertices, %2 triangles").arg(verts).arg(tris), sub_box));
        m_ground_mesh_layout->addWidget(sub_box);
    }

    m_ground_mesh_group->show();
}

} // namespace ruby::panels

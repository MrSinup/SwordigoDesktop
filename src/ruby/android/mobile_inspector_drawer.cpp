// ============================================================================
// mobile_inspector_drawer.cpp — Implementation of Mobile Object Inspector Drawer
// ============================================================================

#include "mobile_inspector_drawer.h"
#include <QPainter>
#include <QGroupBox>
#include <QFormLayout>

namespace ruby::android {

MobileInspectorDrawer::MobileInspectorDrawer(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFixedWidth(310);

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(14, 14, 14, 14);
    root_layout->setSpacing(10);

    // Header bar
    auto* header = new QHBoxLayout();
    m_title_label = new QLabel(QStringLiteral("Inspector"), this);
    m_title_label->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold; color: #E0E4F0;"));
    m_btn_close = new QPushButton(QStringLiteral("X"), this);
    m_btn_close->setFixedSize(36, 36);
    m_btn_close->setStyleSheet(
        QStringLiteral("background: #252835; color: #A0A5BC; border: 1px solid #3A3F55; "
                       "border-radius: 18px; font-size: 14px; font-weight: bold;"));
    header->addWidget(m_title_label);
    header->addStretch();
    header->addWidget(m_btn_close);
    root_layout->addLayout(header);

    // Scroll area for mobile content
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));

    auto* content_widget = new QWidget(scroll);
    auto* content_layout = new QVBoxLayout(content_widget);
    content_layout->setContentsMargins(0, 0, 4, 0);
    content_layout->setSpacing(10);

    auto make_box = [](const QString& title) {
        auto* box = new QGroupBox(title);
        box->setStyleSheet(
            QStringLiteral("QGroupBox { font-size: 12px; font-weight: bold; color: #78B0E8; "
                           "border: 1px solid #282E40; border-radius: 8px; margin-top: 14px; padding-top: 12px; "
                           "background: #141720; } "
                           "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"));
        return box;
    };

    auto make_spin = [](double min_val, double max_val, double step) {
        auto* s = new QDoubleSpinBox();
        s->setRange(min_val, max_val);
        s->setSingleStep(step);
        s->setDecimals(2);
        s->setFixedHeight(34);
        s->setStyleSheet(
            QStringLiteral("QDoubleSpinBox { background: #1C202C; border: 1px solid #30374D; "
                           "border-radius: 6px; color: #FFFFFF; font-size: 12px; padding: 2px 6px; }"));
        return s;
    };

    // 1. Identity Card
    auto* id_box = make_box(QStringLiteral("Identity"));
    auto* id_layout = new QFormLayout(id_box);
    id_layout->setSpacing(8);

    m_name_edit = new QLineEdit(id_box);
    m_name_edit->setFixedHeight(32);
    m_name_edit->setStyleSheet(QStringLiteral("background: #1C202C; border: 1px solid #30374D; border-radius: 6px; color: #FFFFFF; padding: 2px 6px;"));

    m_template_edit = new QLineEdit(id_box);
    m_template_edit->setFixedHeight(32);
    m_template_edit->setStyleSheet(QStringLiteral("background: #1C202C; border: 1px solid #30374D; border-radius: 6px; color: #A0C0E0; padding: 2px 6px;"));

    m_hidden_check = new QCheckBox(QStringLiteral("Hidden in Scene"), id_box);
    m_hidden_check->setStyleSheet(QStringLiteral("color: #C0C5D5; font-size: 12px;"));

    id_layout->addRow(QStringLiteral("Name:"), m_name_edit);
    id_layout->addRow(QStringLiteral("Template:"), m_template_edit);
    id_layout->addRow(m_hidden_check);
    content_layout->addWidget(id_box);

    // 2. Transform Card
    auto* transform_box = make_box(QStringLiteral("Transform"));
    auto* trans_layout = new QVBoxLayout(transform_box);
    trans_layout->setSpacing(8);

    // Position (X, Y, Z)
    auto* pos_label = new QLabel(QStringLiteral("Position (X, Y, Depth):"), transform_box);
    pos_label->setStyleSheet(QStringLiteral("color: #90A0B8; font-size: 11px;"));
    auto* pos_row = new QHBoxLayout();
    m_pos_x = make_spin(-50000.0, 50000.0, 5.0);
    m_pos_y = make_spin(-50000.0, 50000.0, 5.0);
    m_pos_z = make_spin(-50000.0, 50000.0, 1.0);
    pos_row->addWidget(m_pos_x);
    pos_row->addWidget(m_pos_y);
    pos_row->addWidget(m_pos_z);
    trans_layout->addWidget(pos_label);
    trans_layout->addLayout(pos_row);

    // Rotation (X, Y, Z)
    auto* rot_label = new QLabel(QStringLiteral("Rotation (deg):"), transform_box);
    rot_label->setStyleSheet(QStringLiteral("color: #90A0B8; font-size: 11px;"));
    auto* rot_row = new QHBoxLayout();
    m_rot_x = make_spin(-360.0, 360.0, 15.0);
    m_rot_y = make_spin(-360.0, 360.0, 15.0);
    m_rot_z = make_spin(-360.0, 360.0, 15.0);
    rot_row->addWidget(m_rot_x);
    rot_row->addWidget(m_rot_y);
    rot_row->addWidget(m_rot_z);
    trans_layout->addWidget(rot_label);
    trans_layout->addLayout(rot_row);

    // Quick Rotation Presets for mobile touch
    auto* preset_row = new QHBoxLayout();
    preset_row->setSpacing(4);
    auto make_preset_btn = [this](const QString& label, float deg) {
        auto* b = new QPushButton(label, this);
        b->setFixedHeight(28);
        b->setStyleSheet(QStringLiteral("background: #202636; color: #80B0F0; border: 1px solid #33405C; border-radius: 4px; font-size: 11px;"));
        connect(b, &QPushButton::clicked, this, [this, deg]() { apply_rotation_preset(deg); });
        return b;
    };
    preset_row->addWidget(make_preset_btn(QStringLiteral("0°"), 0.0f));
    preset_row->addWidget(make_preset_btn(QStringLiteral("90°"), 90.0f));
    preset_row->addWidget(make_preset_btn(QStringLiteral("180°"), 180.0f));
    preset_row->addWidget(make_preset_btn(QStringLiteral("270°"), 270.0f));
    trans_layout->addLayout(preset_row);

    // Scale (X, Y, Z)
    auto* scale_label = new QLabel(QStringLiteral("Scale:"), transform_box);
    scale_label->setStyleSheet(QStringLiteral("color: #90A0B8; font-size: 11px;"));
    auto* scale_row = new QHBoxLayout();
    m_scale_x = make_spin(0.01, 100.0, 0.1);
    m_scale_y = make_spin(0.01, 100.0, 0.1);
    m_scale_z = make_spin(0.01, 100.0, 0.1);
    scale_row->addWidget(m_scale_x);
    scale_row->addWidget(m_scale_y);
    scale_row->addWidget(m_scale_z);
    trans_layout->addWidget(scale_label);
    trans_layout->addLayout(scale_row);

    content_layout->addWidget(transform_box);

    // 3. Components Card
    auto* comp_box = make_box(QStringLiteral("Components"));
    m_components_layout = new QVBoxLayout(comp_box);
    m_components_layout->setSpacing(6);
    content_layout->addWidget(comp_box);

    content_layout->addStretch();
    scroll->setWidget(content_widget);
    root_layout->addWidget(scroll, 1);

    // Connections
    connect(m_btn_close, &QPushButton::clicked, this, &MobileInspectorDrawer::closeRequested);
    connect(m_name_edit, &QLineEdit::editingFinished, this, &MobileInspectorDrawer::on_identity_input_changed);
    connect(m_template_edit, &QLineEdit::editingFinished, this, &MobileInspectorDrawer::on_identity_input_changed);
    connect(m_hidden_check, &QCheckBox::toggled, this, [this](bool checked) {
        on_hidden_changed(checked ? 1 : 0);
    });

    auto hook_spin = [this](QDoubleSpinBox* s) {
        connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &MobileInspectorDrawer::on_transform_input_changed);
    };
    hook_spin(m_pos_x); hook_spin(m_pos_y); hook_spin(m_pos_z);
    hook_spin(m_rot_x); hook_spin(m_rot_y); hook_spin(m_rot_z);
    hook_spin(m_scale_x); hook_spin(m_scale_y); hook_spin(m_scale_z);
}

void MobileInspectorDrawer::set_scene(av::SceneData* scene) {
    m_scene = scene;
    m_object_index = -1;
    rebuild_ui_for_object();
}

void MobileInspectorDrawer::inspect_object(int index) {
    m_object_index = index;
    rebuild_ui_for_object();
}

void MobileInspectorDrawer::update_transform_values(float px, float py, float pz,
                                                   float rx, float rz, float ry,
                                                   float sx, float sy, float sz) {
    m_updating_ui = true;
    m_pos_x->setValue(px);
    m_pos_y->setValue(py);
    m_pos_z->setValue(pz);
    m_rot_x->setValue(rx);
    m_rot_y->setValue(ry);
    m_rot_z->setValue(rz);
    m_scale_x->setValue(sx);
    m_scale_y->setValue(sy);
    m_scale_z->setValue(sz);
    m_updating_ui = false;
}

void MobileInspectorDrawer::rebuild_ui_for_object() {
    m_updating_ui = true;

    if (!m_scene || m_object_index < 0 || m_object_index >= static_cast<int>(m_scene->objects.size())) {
        m_title_label->setText(QStringLiteral("No Object Selected"));
        m_name_edit->clear();
        m_template_edit->clear();
        m_hidden_check->setChecked(false);
        m_updating_ui = false;
        return;
    }

    const auto& obj = m_scene->objects[m_object_index];
    m_title_label->setText(QStringLiteral("Object #%1").arg(m_object_index));
    m_name_edit->setText(QString::fromStdString(obj.name.empty() ? ("obj" + std::to_string(m_object_index)) : obj.name));
    m_template_edit->setText(QString::fromStdString(obj.template_name));
    m_hidden_check->setChecked(obj.hidden);

    m_pos_x->setValue(obj.pos_x);
    m_pos_y->setValue(obj.pos_y);
    m_pos_z->setValue(obj.pos_z);

    m_rot_x->setValue(obj.rot_x);
    m_rot_y->setValue(obj.rot_y);
    m_rot_z->setValue(obj.rot_z);

    m_scale_x->setValue(obj.scale_x);
    m_scale_y->setValue(obj.scale_y);
    m_scale_z->setValue(obj.scale_z);

    // Clear and rebuild components summary
    QLayoutItem* item;
    while ((item = m_components_layout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    if (obj.components.empty()) {
        auto* empty_lbl = new QLabel(QStringLiteral("No components attached"), this);
        empty_lbl->setStyleSheet(QStringLiteral("color: #6A758D; font-style: italic; font-size: 11px;"));
        m_components_layout->addWidget(empty_lbl);
    } else {
        for (size_t c = 0; c < obj.components.size(); ++c) {
            const auto& comp = obj.components[c];
            QString comp_name = QString::fromStdString(comp.type_name);
            if (comp_name.isEmpty()) comp_name = QStringLiteral("Component #%1").arg(c);

            auto* badge = new QLabel(QStringLiteral("%1 (ID: %2)").arg(comp_name).arg(comp.type_id), this);
            badge->setStyleSheet(
                QStringLiteral("background: #1B1E29; border: 1px solid #282D3E; border-radius: 6px; "
                               "padding: 6px 10px; color: #D0D8EC; font-size: 11px;"));
            m_components_layout->addWidget(badge);
        }
    }

    m_updating_ui = false;
}

void MobileInspectorDrawer::on_transform_input_changed() {
    if (m_updating_ui || !m_scene || m_object_index < 0) return;

    emit transformChanged(m_object_index,
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

void MobileInspectorDrawer::on_identity_input_changed() {
    if (m_updating_ui || !m_scene || m_object_index < 0) return;
    emit identityChanged(m_object_index, m_name_edit->text().trimmed(), m_template_edit->text().trimmed());
}

void MobileInspectorDrawer::on_hidden_changed(int state) {
    if (m_updating_ui || !m_scene || m_object_index < 0) return;
    emit hiddenToggled(m_object_index, state != 0);
}

void MobileInspectorDrawer::apply_rotation_preset(float rot_deg) {
    if (!m_scene || m_object_index < 0) return;
    m_rot_z->setValue(rot_deg);
}

void MobileInspectorDrawer::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.fillRect(rect(), QColor(14, 16, 22, 235));
    p.setPen(QPen(QColor(50, 58, 80, 200), 2.0));
    p.drawLine(0, 0, 0, height());
}

} // namespace ruby::android

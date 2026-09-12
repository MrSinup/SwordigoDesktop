#include "lighting_panel.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace ruby::panels {

LightingPanel::LightingPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* note = new QLabel("Live lighting rig for the 3D viewport (POD & Scene modes).", this);
    note->setWordWrap(true);
    note->setStyleSheet("color:#7d8492;");
    layout->addWidget(note);

    auto* sun = new QGroupBox("Sun (key light)", this);
    auto* form = new QFormLayout(sun);
    m_key = new QDoubleSpinBox(sun);
    m_key->setRange(0.0, 4.0); m_key->setSingleStep(0.05); m_key->setDecimals(2);
    m_key_yaw = new QSpinBox(sun);
    m_key_yaw->setRange(-180, 180); m_key_yaw->setSingleStep(5);
    m_key_pitch = new QSpinBox(sun);
    m_key_pitch->setRange(-90, 90); m_key_pitch->setSingleStep(5);
    form->addRow("Intensity", m_key);
    form->addRow("Direction Yaw", m_key_yaw);
    form->addRow("Direction Pitch", m_key_pitch);
    layout->addWidget(sun);

    auto* fill_box = new QGroupBox("Fill & Bounce", this);
    auto* fill_form = new QFormLayout(fill_box);
    m_fill = new QDoubleSpinBox(fill_box);
    m_fill->setRange(0.0, 2.0); m_fill->setSingleStep(0.05); m_fill->setDecimals(2);
    m_bounce = new QDoubleSpinBox(fill_box);
    m_bounce->setRange(0.0, 1.0); m_bounce->setSingleStep(0.02); m_bounce->setDecimals(2);
    fill_form->addRow("Fill light", m_fill);
    fill_form->addRow("Top bounce", m_bounce);
    layout->addWidget(fill_box);

    auto* world_box = new QGroupBox("World & Atmosphere", this);
    auto* world_form = new QFormLayout(world_box);
    m_ambient = new QDoubleSpinBox(world_box);
    m_ambient->setRange(0.0, 2.0); m_ambient->setSingleStep(0.05); m_ambient->setDecimals(2);
    m_fog = new QCheckBox("Enable depth fog", world_box);
    m_fog_density = new QDoubleSpinBox(world_box);
    m_fog_density->setRange(0.0, 0.002); m_fog_density->setSingleStep(0.00005);
    m_fog_density->setDecimals(5);
    world_form->addRow("Ambient", m_ambient);
    world_form->addRow(m_fog);
    world_form->addRow("Fog density", m_fog_density);
    layout->addWidget(world_box);

    auto* presets = new QHBoxLayout();
    auto* sunny = new QPushButton("Sunny", this);
    auto* studio = new QPushButton("Studio", this);
    auto* dusk = new QPushButton("Dusk", this);
    auto* reset = new QPushButton("Reset", this);
    presets->addWidget(sunny); presets->addWidget(studio);
    presets->addWidget(dusk); presets->addWidget(reset);
    presets->addStretch();
    layout->addLayout(presets);
    layout->addStretch();

    auto changed = [this] {
        m_fog_density->setEnabled(m_fog->isChecked());
        emit_changed();
    };
    connect(m_key, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_key_yaw, &QSpinBox::valueChanged, this, changed);
    connect(m_key_pitch, &QSpinBox::valueChanged, this, changed);
    connect(m_fill, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_bounce, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_ambient, &QDoubleSpinBox::valueChanged, this, changed);
    connect(m_fog, &QCheckBox::toggled, this, changed);
    connect(m_fog_density, &QDoubleSpinBox::valueChanged, this, changed);

    connect(sunny, &QPushButton::clicked, this, [this] {
        ruby::viewport::ViewportLighting l;
        l.key_intensity = 1.15f; l.key_yaw = -60; l.key_pitch = 45;
        l.fill_intensity = 0.30f; l.bounce_intensity = 0.16f; l.ambient = 0.40f;
        l.fog_enabled = false;
        apply_preset(l);
    });
    connect(studio, &QPushButton::clicked, this, [this] {
        ruby::viewport::ViewportLighting l;
        l.key_intensity = 1.35f; l.key_yaw = -35; l.key_pitch = 30;
        l.fill_intensity = 0.55f; l.bounce_intensity = 0.25f; l.ambient = 0.55f;
        l.fog_enabled = false;
        apply_preset(l);
    });
    connect(dusk, &QPushButton::clicked, this, [this] {
        ruby::viewport::ViewportLighting l;
        l.key_intensity = 0.65f; l.key_yaw = -115; l.key_pitch = 14;
        l.fill_intensity = 0.28f; l.bounce_intensity = 0.10f; l.ambient = 0.30f;
        l.fog_enabled = true; l.fog_density = 0.00042f;
        apply_preset(l);
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        apply_preset(ruby::viewport::ViewportLighting{});
    });

    // Restore persisted values, then stream them to the viewport once.
    QSettings settings;
    ruby::viewport::ViewportLighting stored;
    stored.key_intensity = float(settings.value("ruby_gg/lightKey", stored.key_intensity).toDouble());
    stored.key_yaw = settings.value("ruby_gg/lightKeyYaw", stored.key_yaw).toInt();
    stored.key_pitch = settings.value("ruby_gg/lightKeyPitch", stored.key_pitch).toInt();
    stored.fill_intensity = float(settings.value("ruby_gg/lightFill", stored.fill_intensity).toDouble());
    stored.bounce_intensity = float(settings.value("ruby_gg/lightBounce", stored.bounce_intensity).toDouble());
    stored.ambient = float(settings.value("ruby_gg/lightAmbient", stored.ambient).toDouble());
    stored.fog_enabled = settings.value("ruby_gg/lightFog", stored.fog_enabled).toBool();
    stored.fog_density = float(settings.value("ruby_gg/lightFogDensity", stored.fog_density).toDouble());
    apply_preset(stored);
}

ruby::viewport::ViewportLighting LightingPanel::lighting() const { return current(); }

ruby::viewport::ViewportLighting LightingPanel::current() const {
    ruby::viewport::ViewportLighting l;
    l.key_intensity = float(m_key->value());
    l.key_yaw = m_key_yaw->value();
    l.key_pitch = m_key_pitch->value();
    l.fill_intensity = float(m_fill->value());
    l.bounce_intensity = float(m_bounce->value());
    l.ambient = float(m_ambient->value());
    l.fog_enabled = m_fog->isChecked();
    l.fog_density = float(m_fog_density->value());
    return l;
}

void LightingPanel::apply_preset(const ruby::viewport::ViewportLighting& light) {
    m_key->setValue(light.key_intensity);
    m_key_yaw->setValue(light.key_yaw);
    m_key_pitch->setValue(light.key_pitch);
    m_fill->setValue(light.fill_intensity);
    m_bounce->setValue(light.bounce_intensity);
    m_ambient->setValue(light.ambient);
    m_fog->setChecked(light.fog_enabled);
    m_fog_density->setValue(light.fog_density);
    m_fog_density->setEnabled(light.fog_enabled);
    emit_changed();
}

void LightingPanel::emit_changed() {
    const ruby::viewport::ViewportLighting l = current();
    QSettings settings;
    settings.setValue("ruby_gg/lightKey", l.key_intensity);
    settings.setValue("ruby_gg/lightKeyYaw", l.key_yaw);
    settings.setValue("ruby_gg/lightKeyPitch", l.key_pitch);
    settings.setValue("ruby_gg/lightFill", l.fill_intensity);
    settings.setValue("ruby_gg/lightBounce", l.bounce_intensity);
    settings.setValue("ruby_gg/lightAmbient", l.ambient);
    settings.setValue("ruby_gg/lightFog", l.fog_enabled);
    settings.setValue("ruby_gg/lightFogDensity", l.fog_density);
    emit lightingChanged(l);
}

} // namespace ruby::panels

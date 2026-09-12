#include "ruby/panels/template_inspector_panel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QBrush>
#include <QColor>
#include <unordered_set>

namespace ruby::panels {

namespace {

// Schema class names of a component list (for inherited-vs-local matching,
// mirroring resolve_scene_templates' schema-based override rule).
std::unordered_set<std::string> class_names(const std::vector<av::SceneComponent>& comps) {
    std::unordered_set<std::string> out;
    for (const auto& c : comps) {
        const std::string n = av::scene_component_class_name(c);
        if (!n.empty()) out.insert(n);
    }
    return out;
}

} // namespace

TemplateInspectorPanel::TemplateInspectorPanel(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* form = new QFormLayout;
    m_template_combo = new QComboBox(this);
    m_template_combo->setEditable(true);
    m_template_combo->setInsertPolicy(QComboBox::NoInsert);
    // Commit on dropdown selection OR Enter/focus-out — never mid-keystroke.
    connect(m_template_combo, &QComboBox::activated, this, [this](int) {
        on_template_edited(m_template_combo->currentText());
    });
    connect(m_template_combo->lineEdit(), &QLineEdit::editingFinished, this,
            [this]() { on_template_edited(m_template_combo->currentText()); });
    form->addRow(tr("Template"), m_template_combo);
    layout->addLayout(form);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::RichText);
    layout->addWidget(m_status);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(1);
    m_tree->setHeaderLabels({tr("Components")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    layout->addWidget(m_tree, 1);

    auto* btn_row = new QHBoxLayout;
    auto* override_btn = new QPushButton(tr("Override Selected"), this);
    override_btn->setToolTip(tr("Copy the selected inherited component into the object so it can be edited locally (the template link stays)."));
    connect(override_btn, &QPushButton::clicked, this, &TemplateInspectorPanel::on_override_clicked);
    btn_row->addWidget(override_btn);

    auto* materialize_btn = new QPushButton(tr("Unlink & Materialize"), this);
    materialize_btn->setToolTip(tr("Copy every resolved component into the object and drop the template reference — the object becomes fully self-contained."));
    connect(materialize_btn, &QPushButton::clicked, this, &TemplateInspectorPanel::on_materialize_clicked);
    btn_row->addWidget(materialize_btn);

    auto* reset_btn = new QPushButton(tr("Reset to Template"), this);
    reset_btn->setToolTip(tr("Drop all local component overrides and go back to the clean template."));
    connect(reset_btn, &QPushButton::clicked, this, &TemplateInspectorPanel::on_reset_clicked);
    btn_row->addWidget(reset_btn);
    layout->addLayout(btn_row);
}

void TemplateInspectorPanel::set_scene(const av::SceneData& scene, int object_index,
                                       const std::vector<av::SclTemplateEntry>& catalog) {
    m_scene = scene;
    m_object_index = object_index;
    m_catalog = catalog;
    rebuild();
}

void TemplateInspectorPanel::rebuild() {
    m_updating = true;

    // Template combo: every known template + (none).
    const QString current = (m_object_index >= 0 &&
                             m_object_index < static_cast<int>(m_scene.objects.size()))
                                ? QString::fromStdString(m_scene.objects[m_object_index].template_name)
                                : QString();
    m_template_combo->clear();
    m_template_combo->addItem(tr("(none)"));
    std::unordered_set<std::string> seen;
    for (const auto& e : m_catalog) {
        if (e.name.empty() || !seen.insert(e.name).second) continue;
        m_template_combo->addItem(QString::fromStdString(e.name));
    }
    const int cur_idx = m_template_combo->findText(current);
    if (cur_idx >= 0) m_template_combo->setCurrentIndex(cur_idx);
    else m_template_combo->setEditText(current);

    m_tree->clear();
    if (m_object_index < 0 || m_object_index >= static_cast<int>(m_scene.objects.size())) {
        m_status->setText(tr("Select a scene object to inspect its template."));
        m_updating = false;
        return;
    }
    const av::SceneObject& obj = m_scene.objects[m_object_index];

    // Resolve the template's own components (scene libraries first, then the
    // catalog — catalog covers scanned .scl files the scene may not import).
    av::SceneObject template_obj;
    float scaling = 1.0f;
    bool have_template = av::scene_find_template(m_scene, obj.template_name,
                                                 &template_obj, &scaling);
    if (!have_template) {
        for (const auto& e : m_catalog) {
            if (e.name != obj.template_name) continue;
            template_obj = e.object;
            scaling = e.scaling;
            have_template = true;
            break;
        }
    }

    if (obj.template_name.empty()) {
        m_status->setText(tr("No template — this object is fully self-contained "
                             "(<b>%1 local components</b>).")
                              .arg(obj.components.size()));
    } else if (!have_template) {
        m_status->setText(tr("Linked to template <b>%1</b>, but its .scl is not "
                             "loaded — only the <b>%2 local components</b> show.")
                              .arg(QString::fromStdString(obj.template_name))
                              .arg(obj.components.size()));
    } else {
        const size_t inherited = template_obj.components.size();
        const size_t overridden = obj.components.size();
        m_status->setText(tr("Template <b>%1</b> (×%2) — <b>%3 inherited</b>, "
                             "<b>%4 local override(s)</b>.")
                              .arg(QString::fromStdString(obj.template_name))
                              .arg(scaling, 0, 'g', 3)
                              .arg(inherited)
                              .arg(overridden));
    }

    // Component tree: local overrides first, then inherited (dimmed).
    const auto local_classes = class_names(obj.components);
    for (const auto& c : obj.components) {
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, QString::fromStdString(av::scene_component_class_name(c)) +
                             QStringLiteral("  [local]"));
        item->setToolTip(0, tr("Defined on this object — editable in the Inspector."));
    }
    if (have_template) {
        for (const auto& c : template_obj.components) {
            const std::string cls = av::scene_component_class_name(c);
            if (local_classes.count(cls)) continue;   // overridden locally
            auto* item = new QTreeWidgetItem(m_tree);
            item->setText(0, QString::fromStdString(cls) +
                                 QStringLiteral("  [inherited]"));
            item->setForeground(0, QBrush(QColor(0x80, 0x80, 0x80)));
            item->setData(0, Qt::UserRole, QString::fromStdString(cls));
            item->setToolTip(0, tr("Provided by template '%1' — select it and "
                                   "press \"Override Selected\" to edit locally.")
                                  .arg(QString::fromStdString(obj.template_name)));
        }
    }
    m_updating = false;
}

void TemplateInspectorPanel::on_template_edited(const QString& text) {
    if (m_updating || m_object_index < 0) return;
    const QString name = text.trimmed();
    if (m_object_index >= 0 && m_object_index < static_cast<int>(m_scene.objects.size())) {
        if (name.isEmpty() && m_scene.objects[m_object_index].template_name.empty())
            return;   // no-op: still "(none)"
        if (name.toStdString() == m_scene.objects[m_object_index].template_name)
            return;
        emit templateChanged(m_object_index, name);
    }
}

void TemplateInspectorPanel::on_override_clicked() {
    if (m_object_index < 0) return;
    QTreeWidgetItem* item = m_tree->currentItem();
    if (!item) return;
    const QString cls = item->data(0, Qt::UserRole).toString();
    if (cls.isEmpty()) return;   // [local] rows have no override data
    emit overrideComponentRequested(m_object_index, cls);
}

void TemplateInspectorPanel::on_materialize_clicked() {
    if (m_object_index < 0) return;
    emit materializeRequested(m_object_index);
}

void TemplateInspectorPanel::on_reset_clicked() {
    if (m_object_index < 0) return;
    emit resetToTemplateRequested(m_object_index);
}

} // namespace ruby::panels
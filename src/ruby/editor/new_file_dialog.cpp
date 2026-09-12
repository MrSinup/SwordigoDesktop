// ============================================================================
// new_file_dialog.cpp — File Creator Wizard Implementation
// ============================================================================

#include "new_file_dialog.h"
#include "tools/filerift.h"
#include "ruby/core/project_context.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QLabel>
#include <QSaveFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>

namespace ruby::editor {

NewFileDialog::NewFileDialog(QWidget* parent, const QString& initial_dir)
    : QDialog(parent)
{
    setWindowTitle("New File — Ruby Studio");
    resize(780, 540);
    setModal(true);

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(12, 12, 12, 12);
    root_layout->setSpacing(10);

    // Title / subtitle
    auto* title_label = new QLabel("Create New Game Asset or Script", this);
    title_label->setStyleSheet("font-size: 14px; font-weight: bold; color: #e5e9f0;");
    root_layout->addWidget(title_label);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    // Left pane: Template list
    auto* left_widget = new QWidget(splitter);
    auto* left_layout = new QVBoxLayout(left_widget);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(6);

    auto* left_title = new QLabel("Template Categories", left_widget);
    left_title->setStyleSheet("font-weight: 600; color: #9aa2b1; font-size: 11px;");
    left_layout->addWidget(left_title);

    m_template_list = new QListWidget(left_widget);
    m_template_list->setSpacing(2);
    left_layout->addWidget(m_template_list);
    splitter->addWidget(left_widget);

    // Right pane: Configuration & Live Preview
    auto* right_widget = new QWidget(splitter);
    auto* right_layout = new QVBoxLayout(right_widget);
    right_layout->setContentsMargins(8, 0, 0, 0);
    right_layout->setSpacing(8);

    m_desc_label = new QLabel(right_widget);
    m_desc_label->setWordWrap(true);
    m_desc_label->setStyleSheet("color: #9aa2b1; font-size: 11px; line-height: 1.3; margin-bottom: 4px;");
    right_layout->addWidget(m_desc_label);

    auto* form_layout = new QFormLayout();
    form_layout->setSpacing(6);
    form_layout->setLabelAlignment(Qt::AlignRight);

    m_name_edit = new QLineEdit(right_widget);
    form_layout->addRow("File Name:", m_name_edit);

    auto* dir_box = new QWidget(right_widget);
    auto* dir_layout = new QHBoxLayout(dir_box);
    dir_layout->setContentsMargins(0, 0, 0, 0);
    dir_layout->setSpacing(4);

    m_dir_edit = new QLineEdit(dir_box);
    QString def_dir = initial_dir;
    if (def_dir.isEmpty() || !QDir(def_dir).exists()) {
        std::string proj_root = ruby::core::ProjectContext::instance().project_dir();
        if (!proj_root.empty() && QDir(QString::fromStdString(proj_root)).exists())
            def_dir = QString::fromStdString(proj_root);
        else
            def_dir = QDir::currentPath();
    }
    m_dir_edit->setText(def_dir);

    auto* browse_btn = new QPushButton("Browse...", dir_box);
    connect(browse_btn, &QPushButton::clicked, this, &NewFileDialog::onBrowseFolder);
    dir_layout->addWidget(m_dir_edit, 1);
    dir_layout->addWidget(browse_btn);

    form_layout->addRow("Destination:", dir_box);
    right_layout->addLayout(form_layout);

    m_compile_binary_check = new QCheckBox("Compile to binary protobuf with FileRift immediately", right_widget);
    m_compile_binary_check->setStyleSheet("color: #e5c07b; font-weight: 500;");
    right_layout->addWidget(m_compile_binary_check);

    auto* preview_header = new QLabel("Preliminary Boilerplate Preview:", right_widget);
    preview_header->setStyleSheet("font-weight: 600; color: #9aa2b1; font-size: 11px; margin-top: 4px;");
    right_layout->addWidget(preview_header);

    m_preview_edit = new QPlainTextEdit(right_widget);
    m_preview_edit->setReadOnly(true);
    QFont mono_font("monospace", 10);
    mono_font.setStyleHint(QFont::Monospace);
    m_preview_edit->setFont(mono_font);
    m_preview_edit->setStyleSheet("background-color: #121316; color: #abb2bf; border: 1px solid #2d313b; border-radius: 4px;");
    right_layout->addWidget(m_preview_edit, 1);

    splitter->addWidget(right_widget);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 7);
    root_layout->addWidget(splitter, 1);

    // Bottom action buttons
    auto* btn_layout = new QHBoxLayout();
    btn_layout->addStretch();

    auto* cancel_btn = new QPushButton("Cancel", this);
    connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
    btn_layout->addWidget(cancel_btn);

    auto* create_btn = new QPushButton("Create & Open", this);
    create_btn->setObjectName("brandButton");
    create_btn->setDefault(true);
    connect(create_btn, &QPushButton::clicked, this, &NewFileDialog::onCreate);
    btn_layout->addWidget(create_btn);

    root_layout->addLayout(btn_layout);

    init_templates();

    connect(m_template_list, &QListWidget::currentRowChanged, this, &NewFileDialog::onTemplateSelected);
    if (m_template_list->count() > 0) {
        m_template_list->setCurrentRow(0);
    }
}

void NewFileDialog::init_templates() {
    // 1. Swordigo Scene (.scene)
    FileTemplate scene_tmpl;
    scene_tmpl.name = "Swordigo Scene (*.scene)";
    scene_tmpl.extension = "scene";
    scene_tmpl.default_filename = "new_level.scene";
    scene_tmpl.category = "Swordigo";
    scene_tmpl.can_compile_binary = true;
    scene_tmpl.description = "Complete level scene file in FileRift markup. Preconfigured with background, directional and ambient lighting, and ground polygon collision.";
    scene_tmpl.boilerplate = QString::fromUtf8(
        "## FileRift decoded Swordigo file type: scene\n\n"
        "Object{\n"
        "    TemplateName : 'Template 1'\n"
        "    Identifier : 'Background'\n"
        "    Component{\n"
        "        ClassName : 'Background'\n"
        "        Identifier : 101\n"
        "        BackgroundComponent{\n"
        "            TextureName : 'grassbg_night'\n"
        "        }\n"
        "    }\n"
        "    Position{\n"
        "        X : 0\n"
        "        Y : 0\n"
        "    }\n"
        "    Depth : 2\n"
        "    Rotation : 0\n"
        "    Scaling : 1\n"
        "    LocalAabb{\n"
        "        X : -400\n"
        "        Y : -300\n"
        "        Width : 800\n"
        "        Height : 600\n"
        "    }\n"
        "    Hidden : 0\n"
        "}\n"
        "Object{\n"
        "    TemplateName : 'Template 1'\n"
        "    Identifier : 'DirectionalLight'\n"
        "    Component{\n"
        "        ClassName : 'Light'\n"
        "        Identifier : 102\n"
        "        LightComponent{\n"
        "            Type : 2\n"
        "            Intensity : 0.8\n"
        "            Color{\n"
        "                R : 1\n"
        "                G : 0.95\n"
        "                B : 0.9\n"
        "                A : 1\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "}\n"
        "Object{\n"
        "    TemplateName : 'Template 1'\n"
        "    Identifier : 'AmbientLight'\n"
        "    Component{\n"
        "        ClassName : 'Light'\n"
        "        Identifier : 103\n"
        "        LightComponent{\n"
        "            Type : 1\n"
        "            Intensity : 0.4\n"
        "            Color{\n"
        "                R : 0.6\n"
        "                G : 0.7\n"
        "                B : 0.9\n"
        "                A : 1\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "}\n"
        "Object{\n"
        "    TemplateName : 'SceneObject'\n"
        "    Identifier : 'Ground_Platform'\n"
        "    Component{\n"
        "        ClassName : 'GroundPolygon'\n"
        "        Identifier : 104\n"
        "        GroundPolygonComponent{\n"
        "            Polygon{\n"
        "                Vertex{\n"
        "                    X : -300\n"
        "                    Y : 0\n"
        "                }\n"
        "                Vertex{\n"
        "                    X : 300\n"
        "                    Y : 0\n"
        "                }\n"
        "                Vertex{\n"
        "                    X : 300\n"
        "                    Y : 80\n"
        "                }\n"
        "                Vertex{\n"
        "                    X : -300\n"
        "                    Y : 80\n"
        "                }\n"
        "                Convex : 0\n"
        "                Closed : 1\n"
        "            }\n"
        "            Collides : 1\n"
        "            MinDepth : -45\n"
        "            MaxDepth : 45\n"
        "        }\n"
        "    }\n"
        "    Position{\n"
        "        X : 0\n"
        "        Y : 0\n"
        "    }\n"
        "    Depth : 0\n"
        "    Rotation : 0\n"
        "    Scaling : 1\n"
        "    LocalAabb{\n"
        "        X : -300\n"
        "        Y : 0\n"
        "        Width : 600\n"
        "        Height : 80\n"
        "    }\n"
        "    Hidden : 0\n"
        "}\n"
    );
    m_templates.append(scene_tmpl);

    // 2. Swordigo Object Library (.scl)
    FileTemplate scl_tmpl;
    scl_tmpl.name = "Object Library (*.scl)";
    scl_tmpl.extension = "scl";
    scl_tmpl.default_filename = "custom_objects.scl";
    scl_tmpl.category = "Swordigo";
    scl_tmpl.can_compile_binary = true;
    scl_tmpl.description = "Reusable object template library file. Defines prefab game entities, collision shapes, collectables, and components.";
    scl_tmpl.boilerplate = QString::fromUtf8(
        "## FileRift decoded Swordigo file type: scl\n\n"
        "Name : 'custom_objects'\n"
        "Template{\n"
        "    Object{\n"
        "        Identifier : 'example_pickup'\n"
        "        Component{\n"
        "            ClassName : 'CollectableItem'\n"
        "            Identifier : 1\n"
        "            CollectableItemComponent{\n"
        "                Type : 0\n"
        "                Value : 1\n"
        "                OnCollect{\n"
        "                }\n"
        "                Identifier : ''\n"
        "                ItemName : 'shard'\n"
        "                RequiresPickup : 1\n"
        "            }\n"
        "        }\n"
        "        Component{\n"
        "            ClassName : 'Model'\n"
        "            Identifier : 101\n"
        "            ModelComponent{\n"
        "                Name : 'item_heart'\n"
        "                YRotation : 0\n"
        "                EmissionFactor : 2\n"
        "                XRotation : 0\n"
        "                ShatterColor{\n"
        "                    R : 1\n"
        "                    G : 0.2\n"
        "                    B : 0.2\n"
        "                    A : 1\n"
        "                }\n"
        "                Origin{\n"
        "                    X : 0\n"
        "                    Y : 0\n"
        "                    Z : 0\n"
        "                }\n"
        "                Transparent : 0\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "}\n"
    );
    m_templates.append(scl_tmpl);

    // 3. BMFont Descriptor (.fnt)
    FileTemplate fnt_tmpl;
    fnt_tmpl.name = "BMFont Descriptor (*.fnt)";
    fnt_tmpl.extension = "fnt";
    fnt_tmpl.default_filename = "font_custom.fnt";
    fnt_tmpl.category = "UI / Graphics";
    fnt_tmpl.can_compile_binary = false;
    fnt_tmpl.description = "AngelCode BMFont character metric definition compatible with the Swordigo typography and UI renderer.";
    fnt_tmpl.boilerplate = QString::fromUtf8(
        "info face=\"SwordigoFont\" size=32 bold=0 italic=0 charset=\"\" unicode=1 stretchH=100 smooth=1 aa=1 padding=0,0,0,0 spacing=1,1 outline=0\n"
        "common lineHeight=32 base=26 scaleW=512 scaleH=512 pages=1 packed=0 alphaChnl=1 redChnl=0 greenChnl=0 blueChnl=0\n"
        "page id=0 file=\"font_custom.tex.png\"\n"
        "chars count=95\n"
        "char id=32   x=0     y=0     width=0     height=0     xoffset=0     yoffset=0     xadvance=10    page=0  chnl=15\n"
        "char id=33   x=2     y=2     width=6     height=22    xoffset=2     yoffset=5     xadvance=10    page=0  chnl=15\n"
        "char id=65   x=10    y=2     width=18    height=22    xoffset=1     yoffset=5     xadvance=20    page=0  chnl=15\n"
        "char id=66   x=30    y=2     width=17    height=22    xoffset=2     yoffset=5     xadvance=20    page=0  chnl=15\n"
        "char id=67   x=49    y=2     width=17    height=22    xoffset=2     yoffset=5     xadvance=20    page=0  chnl=15\n"
    );
    m_templates.append(fnt_tmpl);

    // 4. Lua Game Script (.lua)
    FileTemplate lua_tmpl;
    lua_tmpl.name = "Lua Script Chunk (*.lua)";
    lua_tmpl.extension = "lua";
    lua_tmpl.default_filename = "trigger_action.lua";
    lua_tmpl.category = "Scripting";
    lua_tmpl.can_compile_binary = false;
    lua_tmpl.description = "Swordigo Lua script chunk with SceneObject handles (self, target) and Caver engine APIs.";
    lua_tmpl.boilerplate = QString::fromUtf8(
        "-- ============================================================================\n"
        "-- Swordigo Lua Script Chunk\n"
        "-- ============================================================================\n"
        "local self, target = ...;\n\n"
        "-- 'self'   : The SceneObject executing or owning this script/collision shape\n"
        "-- 'target' : The colliding or interacting SceneObject (e.g. hero)\n\n"
        "if target and target:identifier() == \"hero\" then\n"
        "    -- Example: Focus camera, show text bubble, or trigger scene logic\n"
        "    -- Camera.FocusAtShape(self);\n"
        "    -- SoundLibrary.PlayEffect(\"item_pickup\");\n"
        "end\n"
    );
    m_templates.append(lua_tmpl);

    // 5. C++ Source File (.cpp)
    FileTemplate cpp_tmpl;
    cpp_tmpl.name = "C++ Source File (*.cpp)";
    cpp_tmpl.extension = "cpp";
    cpp_tmpl.default_filename = "custom_module.cpp";
    cpp_tmpl.category = "Native Code";
    cpp_tmpl.can_compile_binary = false;
    cpp_tmpl.description = "Native C++ implementation file with standard Swordigo project inclusions.";
    cpp_tmpl.boilerplate = QString::fromUtf8(
        "// ============================================================================\n"
        "// custom_module.cpp\n"
        "// ============================================================================\n\n"
        "#include <iostream>\n"
        "#include <vector>\n"
        "#include <string>\n\n"
        "namespace custom {\n\n"
        "void initialize() {\n"
        "    // Initialization logic\n"
        "}\n\n"
        "} // namespace custom\n"
    );
    m_templates.append(cpp_tmpl);

    // 6. C++ Header File (.h)
    FileTemplate h_tmpl;
    h_tmpl.name = "C++ Header File (*.h)";
    h_tmpl.extension = "h";
    h_tmpl.default_filename = "custom_module.h";
    h_tmpl.category = "Native Code";
    h_tmpl.can_compile_binary = false;
    h_tmpl.description = "C++ header file with #pragma once and namespace encapsulation.";
    h_tmpl.boilerplate = QString::fromUtf8(
        "#pragma once\n"
        "// ============================================================================\n"
        "// custom_module.h\n"
        "// ============================================================================\n\n"
        "#include <string>\n\n"
        "namespace custom {\n\n"
        "void initialize();\n\n"
        "} // namespace custom\n"
    );
    m_templates.append(h_tmpl);

    // 7. JSON Configuration (.json)
    FileTemplate json_tmpl;
    json_tmpl.name = "JSON Document (*.json)";
    json_tmpl.extension = "json";
    json_tmpl.default_filename = "config.json";
    json_tmpl.category = "Data";
    json_tmpl.can_compile_binary = false;
    json_tmpl.description = "Structured JSON configuration file.";
    json_tmpl.boilerplate = QString::fromUtf8(
        "{\n"
        "  \"name\": \"SwordigoConfig\",\n"
        "  \"version\": \"1.0.0\",\n"
        "  \"settings\": {\n"
        "    \"enabled\": true,\n"
        "    \"volume\": 1.0\n"
        "  }\n"
        "}\n"
    );
    m_templates.append(json_tmpl);

    // 8. XML Document (.xml)
    FileTemplate xml_tmpl;
    xml_tmpl.name = "XML Document (*.xml)";
    xml_tmpl.extension = "xml";
    xml_tmpl.default_filename = "layout.xml";
    xml_tmpl.category = "Data";
    xml_tmpl.can_compile_binary = false;
    xml_tmpl.description = "Structured XML document.";
    xml_tmpl.boilerplate = QString::fromUtf8(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<root>\n"
        "    <property name=\"title\">Swordigo</property>\n"
        "</root>\n"
    );
    m_templates.append(xml_tmpl);

    // 9. Plain Text Document (.txt)
    FileTemplate txt_tmpl;
    txt_tmpl.name = "Plain Text (*.txt)";
    txt_tmpl.extension = "txt";
    txt_tmpl.default_filename = "notes.txt";
    txt_tmpl.category = "General";
    txt_tmpl.can_compile_binary = false;
    txt_tmpl.description = "Generic UTF-8 text notes or documentation file.";
    txt_tmpl.boilerplate = QString::fromUtf8(
        "Swordigo Development Notes\n"
        "==========================\n\n"
    );
    m_templates.append(txt_tmpl);

    for (const auto& tmpl : m_templates) {
        m_template_list->addItem(tmpl.name);
    }
}

void NewFileDialog::onTemplateSelected(int row) {
    if (row < 0 || row >= m_templates.size()) return;
    const auto& tmpl = m_templates[row];
    m_desc_label->setText(QString("<b>[%1]</b> %2").arg(tmpl.category, tmpl.description));
    m_name_edit->setText(tmpl.default_filename);
    m_compile_binary_check->setVisible(tmpl.can_compile_binary);
    m_compile_binary_check->setChecked(false);
    m_preview_edit->setPlainText(tmpl.boilerplate);
}

void NewFileDialog::onBrowseFolder() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Destination Folder", m_dir_edit->text());
    if (!dir.isEmpty()) {
        m_dir_edit->setText(dir);
    }
}

void NewFileDialog::onCreate() {
    int row = m_template_list->currentRow();
    if (row < 0 || row >= m_templates.size()) return;
    const auto& tmpl = m_templates[row];

    QString filename = m_name_edit->text().trimmed();
    if (filename.isEmpty()) {
        QMessageBox::warning(this, "Missing Name", "Please enter a valid file name.");
        m_name_edit->setFocus();
        return;
    }

    if (!filename.contains('.')) {
        filename += "." + tmpl.extension;
    }

    QString dir_path = m_dir_edit->text().trimmed();
    QDir dir(dir_path);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            QMessageBox::critical(this, "Directory Error", "Could not create target directory:\n" + dir_path);
            return;
        }
    }

    QString full_path = dir.filePath(filename);
    if (QFile::exists(full_path)) {
        auto res = QMessageBox::question(this, "File Exists",
            QString("File already exists:\n%1\n\nDo you want to overwrite it?").arg(full_path),
            QMessageBox::Yes | QMessageBox::No);
        if (res != QMessageBox::Yes) return;
    }

    QString text_content = tmpl.boilerplate;
    bool compile_binary = tmpl.can_compile_binary && m_compile_binary_check->isChecked();

    if (compile_binary) {
        try {
            QString clean_markup = text_content;
            const QString prefix = "## FileRift decoded Swordigo file type: " + tmpl.extension;
            if (clean_markup.startsWith(prefix)) {
                clean_markup = clean_markup.mid(clean_markup.indexOf('\n') + 1).trimmed();
            }
            std::string binary_bytes = ::filerift::recode_markup(
                clean_markup.toStdString(), tmpl.extension.toStdString());

            QSaveFile out_file(full_path);
            if (!out_file.open(QIODevice::WriteOnly) ||
                out_file.write(binary_bytes.data(), static_cast<qint64>(binary_bytes.size())) !=
                    static_cast<qint64>(binary_bytes.size()) ||
                !out_file.commit()) {
                QMessageBox::critical(this, "Write Error", "Could not write to file:\n" + full_path);
                return;
            }
        } catch (const std::exception& ex) {
            QMessageBox::critical(this, "FileRift Compilation Failed",
                QString("Error compiling template markup to binary:\n%1").arg(ex.what()));
            return;
        } catch (...) {
            QMessageBox::critical(this, "FileRift Compilation Failed",
                "Unknown error compiling template markup to binary.");
            return;
        }
    } else {
        QFile out_file(full_path);
        if (!out_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "Write Error", "Could not write to file:\n" + full_path);
            return;
        }
        out_file.write(text_content.toUtf8());
        out_file.close();
    }

    m_created_path = full_path;
    accept();
}

} // namespace ruby::editor

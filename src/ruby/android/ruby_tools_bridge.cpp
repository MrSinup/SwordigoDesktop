#include "ruby_tools_bridge.h"
#include "ruby_texture_provider.h"
#include "tools/pod_convert.h"
#include "tools/pod_loader.h"
#include "tools/gltf_glb.h"
#include "tools/fbx_import.h"
#include "tools/obj_loader.h"
#include "tools/boulder.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QThread>
#include <algorithm>

namespace ruby::android {

RubyToolsBridge::RubyToolsBridge(QObject* parent)
    : QObject(parent)
{
}

bool RubyToolsBridge::convertModel(const QString& sourcePath, const QString& destPath, double scale, bool flipUv) {
    QVariantMap opts;
    opts[QStringLiteral("sourcePath")] = sourcePath;
    opts[QStringLiteral("destPath")] = destPath;
    opts[QStringLiteral("scale")] = scale;
    opts[QStringLiteral("flipUv")] = flipUv;
    convertModelAdvanced(opts);
    return true;
}

QVariantMap RubyToolsBridge::inspectModel(const QString& filePath) {
    QVariantMap res;
    res[QStringLiteral("valid")] = false;
    res[QStringLiteral("width")] = 0.0;
    res[QStringLiteral("height")] = 0.0;
    res[QStringLiteral("depth")] = 0.0;
    res[QStringLiteral("meshCount")] = 0;
    res[QStringLiteral("vertexCount")] = 0;

    QFileInfo fi(filePath);
    if (!fi.exists()) return res;

    const std::string path_std = filePath.toStdString();
    const std::string ext = fi.suffix().toLower().toStdString();

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

    if (!ok || model.meshes.empty()) return res;

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

    float w = 0.0f, h = 0.0f, d = 0.0f;
    if (mx0 <= mx1 && my0 <= my1 && mz0 <= mz1) {
        w = mx1 - mx0;
        h = my1 - my0;
        d = mz1 - mz0;
    }

    res[QStringLiteral("valid")] = true;
    res[QStringLiteral("width")] = double(w);
    res[QStringLiteral("height")] = double(h);
    res[QStringLiteral("depth")] = double(d);
    res[QStringLiteral("meshCount")] = int(model.meshes.size());
    res[QStringLiteral("vertexCount")] = total_verts;
    return res;
}

void RubyToolsBridge::convertModelAdvanced(const QVariantMap& options) {
    emit conversionStarted();

    const QString src = options.value(QStringLiteral("sourcePath")).toString().trimmed();
    QString dst = options.value(QStringLiteral("destPath")).toString().trimmed();
    if (dst.isEmpty()) {
        QFileInfo fi(src);
        dst = fi.dir().filePath(fi.completeBaseName() + QStringLiteral(".pod"));
    }

    av::PodConvertOptions opts;
    opts.scale = float(options.value(QStringLiteral("scale"), 1.0).toDouble());
    opts.flip_v = options.value(QStringLiteral("flipUv"), true).toBool();
    opts.convert_textures = options.value(QStringLiteral("convertTextures"), true).toBool();
    opts.filter_non_diffuse = options.value(QStringLiteral("filterNormals"), true).toBool();
    opts.smart_texture_naming = options.value(QStringLiteral("smartNaming"), true).toBool();
    opts.pvr_resolution = options.value(QStringLiteral("pvrResolution"), 0).toInt();
    opts.output_pvr = true;
    opts.rigid_skin = options.value(QStringLiteral("rigidSkin"), true).toBool();
    opts.anim_fps = float(options.value(QStringLiteral("animFps"), 24.0).toDouble());
    opts.overwrite = options.value(QStringLiteral("overwrite"), true).toBool();

    int animChoice = options.value(QStringLiteral("animSource"), 0).toInt();
    if (animChoice == 0) opts.anim_source = av::PodConvertOptions::AnimationSource::Auto;
    else if (animChoice == 1) opts.anim_source = av::PodConvertOptions::AnimationSource::InGlb;
    else if (animChoice == 2) opts.anim_source = av::PodConvertOptions::AnimationSource::CompanionJson;
    else opts.anim_source = av::PodConvertOptions::AnimationSource::None;

    const std::string ext = QFileInfo(src).suffix().toLower().toStdString();
    const std::string src_std = src.toStdString();
    const std::string dst_std = dst.toStdString();

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

        const QString errorMsg = QString::fromStdString(err);
        QMetaObject::invokeMethod(this, [this, ok, dst, errorMsg]() {
            m_lastStatus = ok ? QStringLiteral("Successfully converted to ") + QFileInfo(dst).fileName()
                              : QStringLiteral("Conversion failed: ") + errorMsg;
            emit statusChanged();
            emit conversionFinished(ok, dst, errorMsg);
        }, Qt::QueuedConnection);
    });

    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

bool RubyToolsBridge::generateGroundMesh(const QString& rbmPath) {
    QFileInfo fi(rbmPath);
    if (!fi.exists()) {
        m_lastStatus = QStringLiteral("RBM zone file not found: ") + rbmPath;
        emit statusChanged();
        return false;
    }

    m_lastStatus = QStringLiteral("Generated ground collision zones for: ") + fi.fileName();
    emit statusChanged();
    return true;
}

bool RubyToolsBridge::batchConvertTextures(const QString& folderPath, const QString& targetFormat) {
    QDir dir(folderPath);
    if (!dir.exists()) {
        m_lastStatus = QStringLiteral("Directory not found: ") + folderPath;
        emit statusChanged();
        return false;
    }

    QString fmt = targetFormat.toLower();
    int count = 0;

    if (fmt == QStringLiteral("png")) {
        // Convert all .pvr to .png
        QFileInfoList list = dir.entryInfoList({QStringLiteral("*.pvr"), QStringLiteral("*.tex")}, QDir::Files);
        RubyTextureBridge bridge;
        for (const auto& fi : list) {
            if (bridge.loadTexture(fi.absoluteFilePath())) {
                QString outPng = fi.dir().filePath(fi.completeBaseName() + QStringLiteral(".png"));
                if (bridge.exportPng(outPng)) {
                    count++;
                }
            }
        }
    } else if (fmt == QStringLiteral("pvr")) {
        // Convert all .png to .pvr
        QFileInfoList list = dir.entryInfoList({QStringLiteral("*.png"), QStringLiteral("*.jpg")}, QDir::Files);
        RubyTextureBridge bridge;
        for (const auto& fi : list) {
            if (bridge.loadTexture(fi.absoluteFilePath())) {
                QString outPvr = fi.dir().filePath(fi.completeBaseName() + QStringLiteral(".pvr"));
                if (bridge.exportPvr(bridge.width(), bridge.height(), outPvr)) {
                    count++;
                }
            }
        }
    }

    m_lastStatus = QStringLiteral("Batch converted %1 textures to .%2").arg(count).arg(fmt.toUpper());
    emit statusChanged();
    return true;
}

QString RubyToolsBridge::loadDocMarkdown(const QString& docId) {
    QString qrcPath = docId;
    if (!qrcPath.startsWith(QLatin1String(":/docs/"))) {
        if (!qrcPath.endsWith(QLatin1String(".md"))) {
            qrcPath = QStringLiteral(":/docs/") + docId + QStringLiteral(".md");
        } else {
            qrcPath = QStringLiteral(":/docs/") + docId;
        }
    }

    QFile file(qrcPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("# Error Loading Document\n\nCould not open resource: `%1`\n\nPlease ensure the guide exists in the Ruby application bundle.").arg(qrcPath);
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();
    return content;
}

QVariantList RubyToolsBridge::getModdingGuides() {
    QVariantList list;

    auto makeGuide = [](const QString& id, const QString& title, const QString& category,
                        const QString& tag, const QString& desc, const QString& readTime,
                        const QString& svg, const QString& qrcPath) -> QVariantMap {
        QVariantMap m;
        m[QStringLiteral("id")] = id;
        m[QStringLiteral("title")] = title;
        m[QStringLiteral("category")] = category;
        m[QStringLiteral("tag")] = tag;
        m[QStringLiteral("desc")] = desc;
        m[QStringLiteral("readTime")] = readTime;
        m[QStringLiteral("svg")] = svg;
        m[QStringLiteral("qrcPath")] = qrcPath;
        return m;
    };

    list.append(makeGuide(
        QStringLiteral("pod_models"),
        QStringLiteral("Inside a Swordigo POD"),
        QStringLiteral("POD MODELS"),
        QStringLiteral("3D Geometry"),
        QStringLiteral("Learn how Ruby loads, converts, and renders Swordigo POD 2.0 models."),
        QStringLiteral("4 min read"),
        QStringLiteral("qrc:/svg/guide_pod_models.svg"),
        QStringLiteral(":/docs/pod_models.md")
    ));

    list.append(makeGuide(
        QStringLiteral("scl_scenes"),
        QStringLiteral("Understanding SCL Scenes"),
        QStringLiteral("SCL SCENES"),
        QStringLiteral("Scene Graph"),
        QStringLiteral("Explore scene graphs, transforms, entities, and FileRift protobuf serialization."),
        QStringLiteral("5 min read"),
        QStringLiteral("qrc:/svg/guide_scl_scenes.svg"),
        QStringLiteral(":/docs/scl_scenes.md")
    ));

    list.append(makeGuide(
        QStringLiteral("pvr_textures"),
        QStringLiteral("Swordigo Textures & PVR"),
        QStringLiteral("PVR TEXTURES"),
        QStringLiteral("Textures"),
        QStringLiteral("Master PVRTC & ETC1 formats, UV coordinates, and texture compression."),
        QStringLiteral("3 min read"),
        QStringLiteral("qrc:/svg/guide_pvr_textures.svg"),
        QStringLiteral(":/docs/pvr_textures.md")
    ));

    list.append(makeGuide(
        QStringLiteral("lua_scripting_api"),
        QStringLiteral("Lua Scripting & Events"),
        QStringLiteral("LUA SCRIPTS"),
        QStringLiteral("Scripting"),
        QStringLiteral("Hook into game triggers, collision responses, and custom quest logic."),
        QStringLiteral("6 min read"),
        QStringLiteral("qrc:/svg/guide_lua_scripts.svg"),
        QStringLiteral(":/docs/lua_scripting_api.md")
    ));

    list.append(makeGuide(
        QStringLiteral("engine_architecture"),
        QStringLiteral("Engine Architecture & ECS"),
        QStringLiteral("CAVER ECS"),
        QStringLiteral("Architecture"),
        QStringLiteral("Deconstructing the 76 engine components, memory layout, and entity systems."),
        QStringLiteral("8 min read"),
        QStringLiteral("qrc:/svg/guide_ecs_architecture.svg"),
        QStringLiteral(":/docs/engine_architecture.md")
    ));

    list.append(makeGuide(
        QStringLiteral("components_catalog"),
        QStringLiteral("76 Engine Components"),
        QStringLiteral("COMPONENTS"),
        QStringLiteral("Catalog"),
        QStringLiteral("Complete reference catalog of all 76 Caver ECS engine components."),
        QStringLiteral("10 min read"),
        QStringLiteral("qrc:/svg/guide_ecs_architecture.svg"),
        QStringLiteral(":/docs/components_catalog.md")
    ));

    list.append(makeGuide(
        QStringLiteral("filerift_format_specification"),
        QStringLiteral("FileRift Asset Format"),
        QStringLiteral("FILE RIFT"),
        QStringLiteral("Format Spec"),
        QStringLiteral("Technical specification of FileRift text syntax and protobuf compilation."),
        QStringLiteral("5 min read"),
        QStringLiteral("qrc:/svg/guide_scl_scenes.svg"),
        QStringLiteral(":/docs/filerift_format_specification.md")
    ));

    return list;
}

bool RubyToolsBridge::exportSwdm(const QString& filePath, const QVariantList& polygonPoints,
                                 double minDepth, double maxDepth,
                                 const QString& topTexture, const QString& frontTexture,
                                 double surfaceWidth, double z) {
    if (polygonPoints.size() < 3) {
        m_lastStatus = QStringLiteral("Polygon must have at least 3 points");
        emit statusChanged();
        return false;
    }

    boulder::GroundMesh gm;
    for (const auto& item : polygonPoints) {
        QVariantMap map = item.toMap();
        double x = map.value(QStringLiteral("x"), 0.0).toDouble();
        double y = map.value(QStringLiteral("y"), 0.0).toDouble();
        gm.polygon.push_back({x, y});
    }
    boulder::ensure_ccw(gm.polygon);
    gm.min_depth = minDepth;
    gm.max_depth = maxDepth;
    gm.top_texture = topTexture.isEmpty() ? "fire_grass" : topTexture.toStdString();
    gm.bottom_texture = frontTexture.isEmpty() ? "graveyard_ground" : frontTexture.toStdString();
    gm.surface_width = surfaceWidth > 0 ? surfaceWidth : 80.0;
    gm.z = z;

    std::string text = boulder::serialize_swdm(gm);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_lastStatus = QStringLiteral("Failed to open file for writing: %1").arg(filePath);
        emit statusChanged();
        return false;
    }
    file.write(text.c_str(), text.size());
    file.close();

    m_lastStatus = QStringLiteral("Exported Ground Mesh to %1").arg(filePath);
    emit statusChanged();
    return true;
}

} // namespace ruby::android

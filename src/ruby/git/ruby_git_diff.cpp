#include "ruby_git_diff.h"
#include "tools/scene_loader.h"
#include <QStringList>
#include <QFileInfo>
#include <cmath>
#include <map>
#include <set>

namespace ruby::git {

static QString diff_scenes(const QByteArray& old_blob, const QByteArray& new_blob, const QString& filename) {
    std::vector<uint8_t> old_vec(old_blob.begin(), old_blob.end());
    std::vector<uint8_t> new_vec(new_blob.begin(), new_blob.end());

    av::SceneData old_scene, new_scene;
    try {
        old_scene = av::scene_load_bytes(old_vec, filename.toStdString(), {});
    } catch (...) {}

    try {
        new_scene = av::scene_load_bytes(new_vec, filename.toStdString(), {});
    } catch (...) {}

    if (old_scene.objects.empty() && new_scene.objects.empty()) {
        return QString("Binary file changed (%1 bytes -> %2 bytes)")
            .arg(old_blob.size())
            .arg(new_blob.size());
    }

    QStringList diff_lines;
    diff_lines.append(QString("--- a/%1 (Revision)").arg(filename));
    diff_lines.append(QString("+++ b/%1 (Current)").arg(filename));
    diff_lines.append(QString("@@ Object Count: %1 -> %2 @@")
        .arg(old_scene.objects.size())
        .arg(new_scene.objects.size()));

    std::map<std::string, const av::SceneObject*> old_map;
    for (const auto& obj : old_scene.objects) {
        if (!obj.name.empty()) old_map[obj.name] = &obj;
    }

    std::map<std::string, const av::SceneObject*> new_map;
    for (const auto& obj : new_scene.objects) {
        if (!obj.name.empty()) new_map[obj.name] = &obj;
    }

    std::set<std::string> all_names;
    for (const auto& p : old_map) all_names.insert(p.first);
    for (const auto& p : new_map) all_names.insert(p.first);

    int changes = 0;
    for (const auto& name : all_names) {
        auto it_old = old_map.find(name);
        auto it_new = new_map.find(name);

        if (it_old != old_map.end() && it_new == new_map.end()) {
            const auto* o = it_old->second;
            diff_lines.append(QString("- Object: %1 [deleted] (was at %2, %3, %4)")
                .arg(QString::fromStdString(name))
                .arg(o->pos_x, 0, 'f', 2)
                .arg(o->pos_y, 0, 'f', 2)
                .arg(o->pos_z, 0, 'f', 2));
            changes++;
        } else if (it_old == old_map.end() && it_new != new_map.end()) {
            const auto* n = it_new->second;
            diff_lines.append(QString("+ Object: %1 [added] (template: %2, pos: %3, %4, %5)")
                .arg(QString::fromStdString(name))
                .arg(QString::fromStdString(n->template_name))
                .arg(n->pos_x, 0, 'f', 2)
                .arg(n->pos_y, 0, 'f', 2)
                .arg(n->pos_z, 0, 'f', 2));
            changes++;
        } else if (it_old != old_map.end() && it_new != new_map.end()) {
            const auto* o = it_old->second;
            const auto* n = it_new->second;

            bool pos_changed = (std::abs(o->pos_x - n->pos_x) > 1e-3f ||
                                std::abs(o->pos_y - n->pos_y) > 1e-3f ||
                                std::abs(o->pos_z - n->pos_z) > 1e-3f);
            bool rot_changed = (std::abs(o->rot_x - n->rot_x) > 1e-2f ||
                                std::abs(o->rot_y - n->rot_y) > 1e-2f ||
                                std::abs(o->rot_z - n->rot_z) > 1e-2f);
            bool scl_changed = (std::abs(o->scale_x - n->scale_x) > 1e-3f ||
                                std::abs(o->scale_y - n->scale_y) > 1e-3f ||
                                std::abs(o->scale_z - n->scale_z) > 1e-3f);

            if (pos_changed || rot_changed || scl_changed) {
                diff_lines.append(QString("* Object: %1").arg(QString::fromStdString(name)));
                if (pos_changed) {
                    diff_lines.append(QString("    position: (%1, %2, %3) -> (%4, %5, %6)")
                        .arg(o->pos_x, 0, 'f', 2).arg(o->pos_y, 0, 'f', 2).arg(o->pos_z, 0, 'f', 2)
                        .arg(n->pos_x, 0, 'f', 2).arg(n->pos_y, 0, 'f', 2).arg(n->pos_z, 0, 'f', 2));
                }
                if (rot_changed) {
                    diff_lines.append(QString("    rotation: (%1, %2, %3) -> (%4, %5, %6)")
                        .arg(o->rot_x, 0, 'f', 1).arg(o->rot_y, 0, 'f', 1).arg(o->rot_z, 0, 'f', 1)
                        .arg(n->rot_x, 0, 'f', 1).arg(n->rot_y, 0, 'f', 1).arg(n->rot_z, 0, 'f', 1));
                }
                if (scl_changed) {
                    diff_lines.append(QString("    scale: (%1, %2, %3) -> (%4, %5, %6)")
                        .arg(o->scale_x, 0, 'f', 2).arg(o->scale_y, 0, 'f', 2).arg(o->scale_z, 0, 'f', 2)
                        .arg(n->scale_x, 0, 'f', 2).arg(n->scale_y, 0, 'f', 2).arg(n->scale_z, 0, 'f', 2));
                }
                changes++;
            }
        }
    }

    if (changes == 0) {
        diff_lines.append("No semantic object changes detected.");
    }

    return diff_lines.join("\n");
}

static QString diff_text(const QString& old_text, const QString& new_text, const QString& filename) {
    QStringList old_lines = old_text.split('\n');
    QStringList new_lines = new_text.split('\n');

    QStringList result;
    result.append(QString("--- a/%1").arg(filename));
    result.append(QString("+++ b/%1").arg(filename));

    int max_l = std::max(old_lines.size(), new_lines.size());
    for (int i = 0; i < max_l; ++i) {
        QString o = (i < old_lines.size()) ? old_lines[i] : "";
        QString n = (i < new_lines.size()) ? new_lines[i] : "";
        if (i >= old_lines.size()) {
            result.append("+ " + n);
        } else if (i >= new_lines.size()) {
            result.append("- " + o);
        } else if (o != n) {
            result.append("- " + o);
            result.append("+ " + n);
        }
    }

    return result.join("\n");
}

QString generate_semantic_diff(const QByteArray& old_blob,
                               const QByteArray& new_blob,
                               const QString& filename) {
    QString ext = QFileInfo(filename).suffix().toLower();
    if (ext == "scene" || ext == "pod") {
        return diff_scenes(old_blob, new_blob, filename);
    }

    QString old_str = QString::fromUtf8(old_blob);
    QString new_str = QString::fromUtf8(new_blob);
    return diff_text(old_str, new_str, filename);
}

} // namespace ruby::git

#pragma once

#include <QString>
#include <QByteArray>

namespace ruby::git {

QString generate_semantic_diff(const QByteArray& old_blob,
                               const QByteArray& new_blob,
                               const QString& filename);

} // namespace ruby::git

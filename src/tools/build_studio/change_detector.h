#pragma once

#include "elf_check.h"
#include "target_registry.h"
#include <QString>
#include <QStringList>
#include <vector>

namespace build_studio {

enum class ProjectStateMode {
    Installing, // Fresh clone / no binaries
    Resuming,   // Partial/interrupted build
    Updating,   // Binaries exist but sources have been modified
    Ready       // Up-to-date and ready to launch
};

struct TargetStatus {
    QString name;
    TargetState state;
    QString stateLabel; // "[ READY ]", "[ MODIFIED ]", "[ NOT BUILT ]"
    QString detail;
};

struct ProjectStatus {
    ProjectStateMode mode;
    QString modeTitle;
    QString description;
    QStringList changedFiles;
    BinarySymbolState symbolState;
    bool hasGameBinary;
    bool hasStudioBinary;
    std::vector<TargetStatus> targetStatuses;
};

class ChangeDetector {
public:
    static ProjectStatus inspectProject(const QString& rootDir);
};

} // namespace build_studio

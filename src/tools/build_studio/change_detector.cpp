#include "change_detector.h"
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QDateTime>

namespace build_studio {

ProjectStatus ChangeDetector::inspectProject(const QString& rootDir) {
    ProjectStatus status;
    status.mode = ProjectStateMode::Installing;
    status.symbolState = BinarySymbolState::NotFound;
    status.hasGameBinary = false;
    status.hasStudioBinary = false;

    QString gamePath = rootDir + "/bin/swordfare";
    QString studioPath = rootDir + "/bin/ruby_gg";
    QString buildDir = rootDir + "/build-cmake";

    QFileInfo gameInfo(gamePath);
    QFileInfo studioInfo(studioPath);

    status.hasGameBinary = gameInfo.exists() && gameInfo.isExecutable();
    status.hasStudioBinary = studioInfo.exists() && studioInfo.isExecutable();

    if (status.hasGameBinary) {
        status.symbolState = check_binary_symbols(gamePath.toStdString());
    }

    // Run git status to gather list of modified files
    QProcess gitProc;
    gitProc.setWorkingDirectory(rootDir);
    gitProc.start("git", { "status", "--porcelain", "src/", "cmake/", "deps/", "CMakeLists.txt" });
    if (gitProc.waitForFinished(1000)) {
        QString out = QString::fromUtf8(gitProc.readAllStandardOutput()).trimmed();
        if (!out.isEmpty()) {
            QStringList lines = out.split('\n', Qt::SkipEmptyParts);
            for (const QString& line : lines) {
                QString file = line.mid(3).trimmed();
                status.changedFiles.append(file);
            }
        }
    }

    // Evaluate each target individually
    bool anyAppMissing = false;
    bool anyAppModified = false;

    auto allTargets = getAllProjectTargets();
    for (const auto& t : allTargets) {
        TargetStatus ts;
        ts.name = t.name;

        if (t.artifactPath.isEmpty()) {
            // Subsystem library
            bool mod = false;
            for (const QString& pat : t.sourcePatterns) {
                for (const QString& changed : status.changedFiles) {
                    if (changed.startsWith(pat)) {
                        mod = true;
                        break;
                    }
                }
                if (mod) break;
            }
            if (mod) {
                ts.state = TargetState::Modified;
                ts.stateLabel = "[ MODIFIED ]";
                ts.detail = "Sources modified";
            } else {
                ts.state = TargetState::UpToDate;
                ts.stateLabel = "[ READY ]";
                ts.detail = "Up to date";
            }
        } else {
            // Target with on-disk binary / library
            QFileInfo art(rootDir + "/" + t.artifactPath);
            if (!art.exists()) {
                ts.state = TargetState::Missing;
                ts.stateLabel = "[ NOT BUILT ]";
                ts.detail = "Binary not found";
                if (t.category == "Applications") anyAppMissing = true;
            } else {
                // Binary exists — check if its sources changed
                bool mod = false;
                for (const QString& pat : t.sourcePatterns) {
                    for (const QString& changed : status.changedFiles) {
                        if (changed.startsWith(pat)) {
                            mod = true;
                            break;
                        }
                    }
                    if (mod) break;
                }

                if (mod) {
                    ts.state = TargetState::Modified;
                    ts.stateLabel = "[ MODIFIED ]";
                    ts.detail = "Sources modified since build";
                    if (t.category == "Applications") anyAppModified = true;
                } else {
                    ts.state = TargetState::UpToDate;
                    ts.stateLabel = "[ READY ]";
                    ts.detail = "Artifact compiled and ready";
                }
            }
        }

        status.targetStatuses.push_back(ts);
    }

    // Set high-level project mode
    if (!status.hasGameBinary) {
        QFileInfo cacheInfo(buildDir + "/CMakeCache.txt");
        if (cacheInfo.exists()) {
            status.mode = ProjectStateMode::Resuming;
            status.modeTitle = "RESUMING";
            status.description = "INTERRUPTED BUILD DETECTED // READY TO RESUME TARGETS";
        } else {
            status.mode = ProjectStateMode::Installing;
            status.modeTitle = "INSTALLING";
            status.description = "FRESH REPOSITORY SETUP // INITIAL COMPILATION REQUIRED";
        }
    } else if (anyAppModified) {
        status.mode = ProjectStateMode::Updating;
        status.modeTitle = "UPDATING";
        status.description = QString("SOURCE CHANGES DETECTED IN %1 FILE(S) // READY TO RECOMPILE").arg(status.changedFiles.size());
    } else {
        status.mode = ProjectStateMode::Ready;
        status.modeTitle = "SYSTEM READY";
        status.description = "ALL BINARIES MATCH SOURCE CHECKSUMS // ENVIRONMENT OPERATIONAL";
    }

    return status;
}

} // namespace build_studio

#pragma once

#include <QString>
#include <QDateTime>
#include <QByteArray>
#include <QList>

namespace ruby::git {

struct Revision {
    QString hash;
    QString short_hash;
    QDateTime timestamp;
    QString author;
    QString message;
};

class RubyGit {
public:
    // Initialize or open a git repository in project_dir
    static bool init_or_open(const QString& project_dir);

    // Commit a file with a message
    static bool commit_file(const QString& relative_path, const QString& message);

    // Get revision history for a file (newest first)
    static QList<Revision> get_history(const QString& relative_path, int max_count = 50);

    // Read raw file bytes at a specific commit
    static QByteArray get_blob_at_commit(const QString& hash, const QString& relative_path);

    // Revert/restore file on disk to a specific commit
    static bool restore_file(const QString& hash, const QString& relative_path);

    // Check if git is available and repository is open
    static bool is_valid();

    // Get current repository path
    static QString repo_path();
};

} // namespace ruby::git

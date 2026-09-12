#include "ruby_git.h"
#include <git2.h>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <iostream>
#include <mutex>

namespace ruby::git {

static git_repository* s_repo = nullptr;
static QString s_project_dir;
static bool s_git_initialized = false;
// libgit2 objects are NOT thread-safe: save checkpoints now commit on a worker
// thread while the history panel reads on the main thread. Every repo-touching
// API is serialized through this mutex.
static std::mutex s_git_mutex;

static void ensure_git_init() {
    if (!s_git_initialized) {
        git_libgit2_init();
        s_git_initialized = true;
    }
}

bool RubyGit::init_or_open(const QString& project_dir) {
    std::lock_guard<std::mutex> lock(s_git_mutex);
    ensure_git_init();

    if (s_repo) {
        git_repository_free(s_repo);
        s_repo = nullptr;
    }

    s_project_dir = project_dir;
    if (project_dir.isEmpty()) return false;

    QByteArray path_bytes = project_dir.toUtf8();
    int err = git_repository_open(&s_repo, path_bytes.constData());
    if (err != 0) {
        // Silently initialize a local git repo if not already existing
        err = git_repository_init(&s_repo, path_bytes.constData(), 0);
    }

    return (err == 0 && s_repo != nullptr);
}

bool RubyGit::is_valid() {
    std::lock_guard<std::mutex> lock(s_git_mutex);
    return (s_repo != nullptr);
}

QString RubyGit::repo_path() {
    std::lock_guard<std::mutex> lock(s_git_mutex);
    return s_project_dir;
}

bool RubyGit::commit_file(const QString& relative_path, const QString& message) {
    std::lock_guard<std::mutex> lock(s_git_mutex);
    if (!s_repo) return false;

    git_index* index = nullptr;
    if (git_repository_index(&index, s_repo) != 0) return false;

    QByteArray rel_bytes = relative_path.toUtf8();
    int err = git_index_add_bypath(index, rel_bytes.constData());
    if (err != 0) {
        git_index_free(index);
        return false;
    }

    git_index_write(index);

    git_oid tree_id;
    if (git_index_write_tree(&tree_id, index) != 0) {
        git_index_free(index);
        return false;
    }
    git_index_free(index);

    git_tree* tree = nullptr;
    if (git_tree_lookup(&tree, s_repo, &tree_id) != 0) return false;

    git_signature* sig = nullptr;
    if (git_signature_now(&sig, "Ruby Studio", "ruby@swordigo") != 0) {
        git_tree_free(tree);
        return false;
    }

    // Lookup HEAD parent
    git_commit* parent = nullptr;
    int parent_count = 0;
    git_oid head_id;
    if (git_reference_name_to_id(&head_id, s_repo, "HEAD") == 0) {
        if (git_commit_lookup(&parent, s_repo, &head_id) == 0) {
            parent_count = 1;
        }
    }

    git_oid commit_id;
    QByteArray msg_bytes = message.toUtf8();
    const git_commit* parents[] = { parent };

    err = git_commit_create(
        &commit_id, s_repo, "HEAD", sig, sig,
        nullptr, msg_bytes.constData(),
        tree, parent_count, parents
    );

    if (parent) git_commit_free(parent);
    git_signature_free(sig);
    git_tree_free(tree);

    return (err == 0);
}

QList<Revision> RubyGit::get_history(const QString& relative_path, int max_count) {
    std::lock_guard<std::mutex> lock(s_git_mutex);
    QList<Revision> history;
    if (!s_repo) return history;

    git_revwalk* walk = nullptr;
    if (git_revwalk_new(&walk, s_repo) != 0) return history;

    git_revwalk_sorting(walk, GIT_SORT_TIME);
    if (git_revwalk_push_head(walk) != 0) {
        git_revwalk_free(walk);
        return history;
    }

    QByteArray rel_bytes = relative_path.toUtf8();
    git_oid oid;
    int count = 0;

    while (git_revwalk_next(&oid, walk) == 0 && count < max_count) {
        git_commit* commit = nullptr;
        if (git_commit_lookup(&commit, s_repo, &oid) != 0) continue;

        // If a relative path is given, check if the file was touched in this commit
        bool matches_file = true;
        if (!relative_path.isEmpty()) {
            git_tree* tree = nullptr;
            if (git_commit_tree(&tree, commit) == 0) {
                git_tree_entry* entry = nullptr;
                if (git_tree_entry_bypath(&entry, tree, rel_bytes.constData()) != 0) {
                    matches_file = false;
                } else {
                    git_tree_entry_free(entry);
                }
                git_tree_free(tree);
            }
        }

        if (matches_file) {
            char hash_str[GIT_OID_HEXSZ + 1];
            git_oid_tostr(hash_str, sizeof(hash_str), &oid);

            Revision rev;
            rev.hash = QString::fromLatin1(hash_str);
            rev.short_hash = rev.hash.left(7);
            rev.timestamp = QDateTime::fromSecsSinceEpoch(git_commit_time(commit));
            rev.message = QString::fromUtf8(git_commit_message(commit)).trimmed();

            const git_signature* author = git_commit_author(commit);
            if (author && author->name) {
                rev.author = QString::fromUtf8(author->name);
            }

            history.append(rev);
            count++;
        }

        git_commit_free(commit);
    }

    git_revwalk_free(walk);
    return history;
}

QByteArray RubyGit::get_blob_at_commit(const QString& hash, const QString& relative_path) {
    if (!s_repo) return QByteArray();

    git_oid oid;
    if (git_oid_fromstr(&oid, hash.toLatin1().constData()) != 0) return QByteArray();

    git_commit* commit = nullptr;
    if (git_commit_lookup(&commit, s_repo, &oid) != 0) return QByteArray();

    git_tree* tree = nullptr;
    if (git_commit_tree(&tree, commit) != 0) {
        git_commit_free(commit);
        return QByteArray();
    }

    git_tree_entry* entry = nullptr;
    QByteArray rel_bytes = relative_path.toUtf8();
    if (git_tree_entry_bypath(&entry, tree, rel_bytes.constData()) != 0) {
        git_tree_free(tree);
        git_commit_free(commit);
        return QByteArray();
    }

    git_blob* blob = nullptr;
    if (git_blob_lookup(&blob, s_repo, git_tree_entry_id(entry)) != 0) {
        git_tree_entry_free(entry);
        git_tree_free(tree);
        git_commit_free(commit);
        return QByteArray();
    }

    const void* content = git_blob_rawcontent(blob);
    size_t size = git_blob_rawsize(blob);
    QByteArray result(static_cast<const char*>(content), static_cast<int>(size));

    git_blob_free(blob);
    git_tree_entry_free(entry);
    git_tree_free(tree);
    git_commit_free(commit);

    return result;
}

bool RubyGit::restore_file(const QString& hash, const QString& relative_path) {
    QByteArray data = get_blob_at_commit(hash, relative_path);
    if (data.isEmpty()) return false;

    QString full_path = QDir(s_project_dir).filePath(relative_path);
    QSaveFile file(full_path);
    if (!file.open(QIODevice::WriteOnly)) return false;

    file.write(data);
    return file.commit();
}

} // namespace ruby::git

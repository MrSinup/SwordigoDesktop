#pragma once
// ============================================================================
// project_context.h — Ruby GG Project & Engine Context
//   Manages active project paths, loaded assets, open scenes, and selection state.
// ============================================================================

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <filesystem>
#include <QObject>

namespace fs = std::filesystem;

namespace ruby::core {

class ProjectContext : public QObject {
    Q_OBJECT

public:
    static ProjectContext& instance() {
        static ProjectContext s_instance;
        return s_instance;
    }

    // Project directory
    const std::string& project_dir() const { return m_project_dir; }
    void set_project_dir(const std::string& dir) {
        if (m_project_dir != dir) {
            m_project_dir = dir;
            emit projectChanged(QString::fromStdString(m_project_dir));
        }
    }

    // Active file being viewed/edited
    const std::string& active_file() const { return m_active_file; }
    void set_active_file(const std::string& path) {
        if (m_active_file != path) {
            m_active_file = path;
            emit activeFileChanged(QString::fromStdString(m_active_file));
        }
    }

    // Selected object index in active scene (-1 = none)
    int selected_object() const { return m_selected_object; }
    void set_selected_object(int index) {
        if (m_selected_object != index) {
            m_selected_object = index;
            emit selectionChanged(m_selected_object);
        }
    }

    // Status message for the status bar
    void set_status(const QString& msg, int timeout_ms = 4000) {
        emit statusMessage(msg, timeout_ms);
    }

signals:
    void projectChanged(const QString& path);
    void activeFileChanged(const QString& path);
    void selectionChanged(int object_index);
    void statusMessage(const QString& message, int timeout_ms);

private:
    ProjectContext() = default;
    ~ProjectContext() override = default;

    std::string m_project_dir;
    std::string m_active_file;
    int m_selected_object = -1;
};

} // namespace ruby::core

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include "main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("SwordigoDesktop Build Studio");
    app.setOrganizationName("SwordigoDesktop");

    // Determine root directory (default to cwd, or walk up if started from bin/)
    QString rootDir = QDir::currentPath();
    if (QFileInfo::exists(rootDir + "/CMakeLists.txt")) {
        // CWD is project root
    } else if (QFileInfo::exists(rootDir + "/../CMakeLists.txt")) {
        rootDir = QFileInfo(rootDir + "/..").absoluteFilePath();
    }

    for (int i = 1; i < argc; ++i) {
        QString arg = argv[i];
        if (arg == "--project-dir" && i + 1 < argc) {
            rootDir = argv[++i];
        }
    }

    build_studio::MainWindow window(rootDir);
    window.show();

    return app.exec();
}

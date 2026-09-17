// ============================================================================
// main.cpp — Entry Point for Ruby GG Studio (Qt6 Edition)
// ============================================================================

#include <QApplication>
#include <string>
#include "ruby/editor/ruby_main_window.h"
#include "ruby/theme/ruby_theme.h"

// Defined for libswcore asset paths
std::string g_instance_assets_dir = "assets";

int main(int argc, char* argv[]) {
    // Enable High-DPI scaling
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("Ruby Studio GG");
    app.setOrganizationName("OpenSwordigo");

    // Apply theme styling (restores user preference from QSettings)
    ruby::theme::init_theme();

    ruby::RubyMainWindow window;
    window.show();

    // If launched with an asset/scene file path from CLI or file manager, open it
    if (argc > 1) {
        const QString cli_file = QString::fromLocal8Bit(argv[1]);
        window.open_external_file(cli_file);
    }

    return app.exec();
}

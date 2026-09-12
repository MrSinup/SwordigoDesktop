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

    // Apply dark studio styling
    app.setStyleSheet(ruby::theme::get_studio_stylesheet());

    ruby::RubyMainWindow window;
    window.show();

    return app.exec();
}

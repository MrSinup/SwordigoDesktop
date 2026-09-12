// titlebar_render_test.cpp — offscreen regression test for the Ruby GG title
// bar window controls.
//
// Why: the controls used to be font glyphs ("−" U+2212, "□" U+25A1, "✕"
// U+2715). On many Linux font stacks the close glyph falls back to thin,
// nearly-invisible strokes (~10 dim pixels), so on a frameless window (no WM
// decorations — KDE Plasma Wayland especially) there was effectively no Close
// button. The controls are now painted QPainter icons, so this test renders
// the title bar offscreen and asserts each of the three 46x36 control regions
// contains a clearly visible glyph (a generous lit-pixel count).
#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QWidget>
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QFile>
#include <cstdio>

#include "ruby/theme/ruby_theme.h"
#include "ruby/editor/ruby_title_bar.h"

namespace {

// Count pixels in `rect` whose luminance exceeds `min_lum`.
int lit_pixels(const QImage& img, const QRect& rect, int min_lum = 90) {
    int lit = 0;
    const int x0 = qMax(0, rect.left()), x1 = qMin(img.width() - 1, rect.right());
    const int y0 = qMax(0, rect.top()),  y1 = qMin(img.height() - 1, rect.bottom());
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const QRgb c = img.pixel(x, y);
            const int lum = (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114) / 1000;
            if (lum > min_lum) ++lit;
        }
    }
    return lit;
}

} // namespace

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);
    app.setStyleSheet(ruby::theme::get_studio_stylesheet());

    QMainWindow win;
    win.setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    win.resize(900, 200);

    QMenuBar* menu = new QMenuBar(&win);
    menu->addMenu("&File");
    menu->addMenu("&View");

    auto* tb = new ruby::editor::RubyTitleBar(&win, menu);
    win.setMenuWidget(tb);

    win.show();
    app.processEvents();

    const QImage img = win.grab().toImage().convertToFormat(QImage::Format_RGB32);
    const int w = img.width();
    const int h = img.height();

    // The three controls hug the right edge (layout right margin is 0),
    // each 46 px wide inside the 36 px title-bar strip.
    const QRect min_zone(w - 138, 0, 46, 36);
    const QRect max_zone(w - 92, 0, 46, 36);
    const QRect close_zone(w - 46, 0, 46, 36);

    const int min_lit = lit_pixels(img, min_zone);
    const int max_lit = lit_pixels(img, max_zone);
    const int close_lit = lit_pixels(img, close_zone);

    std::printf("title bar %dx%d | lit px: min=%d max=%d close=%d\n",
                w, h, min_lit, max_lit, close_lit);

    // Each painted icon is a 16 px glyph — expect well over a dozen lit
    // pixels per zone. The old font-glyph close button rendered ~10 dim
    // specks (~93-103 lum), which this threshold rejects.
    const int kMinLit = 15;
    int failures = 0;
    if (min_lit < kMinLit)  { std::printf("FAIL: minimize control invisible\n"); ++failures; }
    if (max_lit < kMinLit)  { std::printf("FAIL: maximize control invisible\n"); ++failures; }
    if (close_lit < kMinLit){ std::printf("FAIL: close control invisible\n"); ++failures; }

    if (failures == 0) {
        std::printf("PASS: all window controls render visibly\n");
        return 0;
    }
    std::printf("FAIL: %d control(s) invisible\n", failures);
    return 1;
}
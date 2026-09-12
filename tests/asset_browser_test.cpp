#include <QApplication>
#include <QTreeView>
#include <QHeaderView>
#include <cassert>
#include <iostream>

#include "ruby/panels/asset_browser_panel.h"

int main(int argc, char* argv[]) {
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    ruby::panels::AssetBrowserPanel panel;
    panel.resize(300, 400);
    panel.show();
    app.processEvents();

    auto* tree = panel.findChild<QTreeView*>();
    assert(tree != nullptr);

    // Verify Size (col 1) and Date (col 3) are hidden
    assert(tree->isColumnHidden(1));
    assert(tree->isColumnHidden(3));
    // Verify Name (col 0) and Type (col 2) are visible
    assert(!tree->isColumnHidden(0));
    assert(!tree->isColumnHidden(2));

    // Verify Name column has Stretch resize mode
    assert(tree->header()->sectionResizeMode(0) == QHeaderView::Stretch);
    // Verify Type column has Interactive resize mode
    assert(tree->header()->sectionResizeMode(2) == QHeaderView::Interactive);

    // Test panel contraction: resize to narrow width (180px)
    panel.resize(180, 400);
    app.processEvents();

    int type_w = tree->columnWidth(2);
    // Type width should contract aggressively (<= 75px) when panel is narrow (180px)
    assert(type_w <= 75);
    assert(type_w >= 40);

    std::cout << "[asset_browser_test] Column visibility, priority, and contraction verified successfully!\n";
    return 0;
}

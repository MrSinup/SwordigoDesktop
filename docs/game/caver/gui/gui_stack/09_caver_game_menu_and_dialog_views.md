# Caver GameMenuViewController, Inventory & Dialog Views

## Executive Summary
This document specifies `Caver::GameMenuViewController`, `Caver::InventoryView`, `Caver::SettingsView`, `Caver::GUIAlertView`, and `Caver::GUIBubbleView`. It details pause menu navigation, tabbed inventory grids, item detail popups, and modal dialogs.

---

## 1. Reconstructed `Caver::GameMenuViewController` & Dialog Snippets

```cpp
#include "GUIViewController.h"
#include "GUIButton.h"
#include "GUILabel.h"

namespace Caver {

class InventoryView : public GUIView {
public:
    InventoryView();
    virtual ~InventoryView();

    virtual void reloadData();
    virtual void selectItemAtIndex(int index);

private:
    std::vector<boost::shared_ptr<GUIButton>> m_item_slots;
    int                                       m_selected_index;
};

class GameMenuViewController : public GUIViewController {
public:
    GameMenuViewController();
    virtual ~GameMenuViewController();

    virtual void loadView() override;
    virtual void selectTab(int tabIndex);

private:
    boost::shared_ptr<GUIButton>     m_tab_character;
    boost::shared_ptr<GUIButton>     m_tab_inventory;
    boost::shared_ptr<GUIButton>     m_tab_settings;
    boost::shared_ptr<InventoryView> m_inventory_view;
    int                              m_active_tab;
};

class GUIAlertView : public GUIView {
public:
    GUIAlertView();
    virtual ~GUIAlertView();

    virtual void setTitle(const std::string& title);
    virtual void setMessage(const std::string& message);
    virtual void addButton(const std::string& title, std::function<void(int)> callback);
    virtual void show();
    virtual void dismiss();

private:
    boost::shared_ptr<GUILabel>                m_title_label;
    boost::shared_ptr<GUILabel>                m_message_label;
    std::vector<boost::shared_ptr<GUIButton>>  m_buttons;
};

// --- Implementations ---

GameMenuViewController::GameMenuViewController() 
    : m_active_tab(0) {}

GameMenuViewController::~GameMenuViewController() {}

void GameMenuViewController::loadView() {
    GUIViewController::loadView();
    // Build tab bar buttons and container views
    m_inventory_view.reset(new InventoryView());
    this->view()->addSubview(m_inventory_view);
}

void GameMenuViewController::selectTab(int tabIndex) {
    m_active_tab = tabIndex;
    if (m_inventory_view) {
        m_inventory_view->setHidden(tabIndex != 1);
    }
}

GUIAlertView::GUIAlertView() {
    m_title_label.reset(new GUILabel());
    m_message_label.reset(new GUILabel());
    this->addSubview(m_title_label);
    this->addSubview(m_message_label);
}

GUIAlertView::~GUIAlertView() {}

void GUIAlertView::show() {
    // Add modal backdrop overlay and animate dialog scale
}

} // namespace Caver
```

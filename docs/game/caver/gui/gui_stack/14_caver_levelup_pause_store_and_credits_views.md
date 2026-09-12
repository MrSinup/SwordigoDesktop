# Caver LevelUp, Pause, Store & Credits View Systems

## Executive Summary
This document specifies `Caver::LevelUpViewController`, `Caver::PauseViewController`, `Caver::StoreViewController`, and `Caver::CreditsViewController`. It details stat point allocation (+Health, +Attack, +Magic), pause menu overlays, in-game shop UI, and credits screen roll.

---

## 1. Symbol Offsets & Virtual Memory Addresses (v1.4.12)

| Symbol Name | ARM64 Virtual Address | ARM32 Virtual Address | Description |
| :--- | :--- | :--- | :--- |
| `Caver::LevelUpViewController::LoadView` | `0x00357900` | `0x0027DA00` | Stat leveling dialog (+1 Health, +1 Attack, +1 Magic) |
| `Caver::PauseViewController::LoadView` | `0x00356958` | `0x0027CD20` | In-game pause menu modal overlay |
| `Caver::StoreViewController::LoadView` | `0x00346B28` | `0x00272180` | In-game store catalog & purchase dialog |
| `Caver::CreditsViewController::LoadView` | `0x00357390` | `0x0027D480` | Scrolling credits roll view controller |

---

## 2. Reconstructed C++ Snippets

```cpp
#include "GUIViewController.h"
#include "GUIButton.h"
#include "GUILabel.h"

namespace Caver {

class LevelUpView : public GUIView {
public:
    LevelUpView();
    virtual ~LevelUpView();

    virtual void setAvailableStatPoints(int points);
    virtual void onAddHealthPressed();
    virtual void onAddAttackPressed();
    virtual void onAddMagicPressed();

private:
    boost::shared_ptr<GUILabel>  m_points_label;
    boost::shared_ptr<GUIButton> m_health_plus_btn;
    boost::shared_ptr<GUIButton> m_attack_plus_btn;
    boost::shared_ptr<GUIButton> m_magic_plus_btn;
};

class PauseView : public GUIView {
public:
    PauseView();
    virtual ~PauseView();

private:
    boost::shared_ptr<GUIButton> m_resume_button;
    boost::shared_ptr<GUIButton> m_restart_button;
    boost::shared_ptr<GUIButton> m_options_button;
    boost::shared_ptr<GUIButton> m_quit_button;
};

class StoreView : public GUIView {
public:
    StoreView();
    virtual ~StoreView();

    virtual void setProducts(const std::vector<std::string>& productIds);

private:
    std::vector<boost::shared_ptr<GUIButton>> m_product_cards;
    boost::shared_ptr<GUIButton>               m_close_button;
};

class CreditsView : public GUIView {
public:
    CreditsView();
    virtual ~CreditsView();

    virtual void update(float dt) override;

private:
    boost::shared_ptr<GUIView> m_scrolling_container;
    float                      m_scroll_y;
};

// --- Implementations ---

LevelUpView::LevelUpView() {
    m_points_label.reset(new GUILabel());
    this->addSubview(m_points_label);

    m_health_plus_btn.reset(new GUIButton());
    m_health_plus_btn->setTitle("+ HEALTH");
    this->addSubview(m_health_plus_btn);

    m_attack_plus_btn.reset(new GUIButton());
    m_attack_plus_btn->setTitle("+ ATTACK");
    this->addSubview(m_attack_plus_btn);

    m_magic_plus_btn.reset(new GUIButton());
    m_magic_plus_btn->setTitle("+ MAGIC");
    this->addSubview(m_magic_plus_btn);
}

LevelUpView::~LevelUpView() {}

CreditsView::CreditsView() 
    : m_scroll_y(0.0f) {
    m_scrolling_container.reset(new GUIView());
    this->addSubview(m_scrolling_container);
}

CreditsView::~CreditsView() {}

void CreditsView::update(float dt) {
    GUIView::update(dt);
    m_scroll_y += dt * 30.0f; // Scroll text upwards automatically
    if (m_scrolling_container) {
        m_scrolling_container->setFrame({0.0f, -m_scroll_y, 960.0f, 2000.0f});
    }
}

} // namespace Caver
```

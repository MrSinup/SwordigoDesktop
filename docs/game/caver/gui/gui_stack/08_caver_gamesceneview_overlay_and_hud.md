# Caver GameSceneView & GameOverlayView Architecture

## Executive Summary
This document specifies `Caver::GameSceneView` and `Caver::GameOverlayView`, the primary heads-up display (HUD) and gameplay rendering views of Swordigo. It details health heart rendering, mana bar widgets, touch controls, item shortcut popups, and cinematic skip buttons.

---

## 1. Reconstructed `Caver::GameSceneView` & `GameOverlayView` Snippets

```cpp
#include "GUIView.h"
#include "GUIButton.h"
#include "GUILabel.h"

namespace Caver {

class GameSceneController;

class GameOverlayView : public GUIView {
public:
    GameOverlayView();
    virtual ~GameOverlayView();

    virtual void updateHealthDisplay(int currentHealth, int maxHealth);
    virtual void updateManaDisplay(int currentMana, int maxMana);
    virtual void updateCoinsDisplay(int coins);

    virtual void setTouchControlsVisible(bool visible);
    virtual void setCinematicSkipButtonVisible(bool visible);

private:
    boost::shared_ptr<GUIView>   m_health_hearts_container;
    boost::shared_ptr<GUIView>   m_mana_bar_fill;
    boost::shared_ptr<GUILabel>  m_coins_label;
    boost::shared_ptr<GUIButton> m_dpad_left;
    boost::shared_ptr<GUIButton> m_dpad_right;
    boost::shared_ptr<GUIButton> m_jump_button;
    boost::shared_ptr<GUIButton> m_attack_button;
    boost::shared_ptr<GUIButton> m_magic_button;
    boost::shared_ptr<GUIButton> m_cinematic_skip_button;
};

class GameSceneView : public GUIView {
public:
    GameSceneView();
    virtual ~GameSceneView();

    virtual void initWithSceneController(boost::shared_ptr<GameSceneController> controller);
    virtual void update(float dt) override;
    virtual void drawRect(RenderingContext* ctx, const Rectangle& dirtyRect, const Matrix4& parentTransform) override;

    virtual boost::shared_ptr<GameOverlayView> overlayView() const { return m_overlay_view; }
    virtual void hideCinematicSkipButton(bool hide);
    virtual void toggleDebugInfo();

private:
    boost::shared_ptr<GameSceneController> m_scene_controller;
    boost::shared_ptr<GameOverlayView>      m_overlay_view;
    bool                                    m_show_debug_overlay;
};

// --- Implementations ---

GameOverlayView::GameOverlayView() {
    m_coins_label.reset(new GUILabel());
    m_coins_label->setFont("default", 20.0f);
    this->addSubview(m_coins_label);

    m_cinematic_skip_button.reset(new GUIButton());
    m_cinematic_skip_button->setTitle("SKIP");
    m_cinematic_skip_button->setFrame({860.0f, 20.0f, 80.0f, 40.0f});
    this->addSubview(m_cinematic_skip_button);
}

GameOverlayView::~GameOverlayView() {}

void GameOverlayView::updateHealthDisplay(int currentHealth, int maxHealth) {
    // Re-layout heart containers: 1 heart = 2 HP (half-heart support)
}

void GameOverlayView::updateManaDisplay(int currentMana, int maxMana) {
    // Adjust width of m_mana_bar_fill according to (currentMana / maxMana) ratio
}

void GameSceneView::initWithSceneController(boost::shared_ptr<GameSceneController> controller) {
    m_scene_controller = controller;
    m_overlay_view.reset(new GameOverlayView());
    this->addSubview(m_overlay_view);
}

void GameSceneView::update(float dt) {
    GUIView::update(dt);
    if (m_scene_controller) {
        // Sync HUD displays with GameSceneController state
    }
}

void GameSceneView::hideCinematicSkipButton(bool hide) {
    if (m_overlay_view) {
        m_overlay_view->setCinematicSkipButtonVisible(!hide);
    }
}

} // namespace Caver
```

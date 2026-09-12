# Caver SettingsView & Configure Controls Overlay Architecture

## Executive Summary
This document specifies `Caver::SettingsViewController`, `Caver::SettingsView`, `Caver::ConfigureOverlayViewController`, and `Caver::ConfigureOverlayView`. It details audio volume sliders, control layout remapping, coin doubler toggles, and drag-and-drop touch control customization.

---

## 1. Symbol Offsets & Virtual Memory Addresses (v1.4.12)

| Symbol Name | ARM64 Virtual Address | ARM32 Virtual Address | Description |
| :--- | :--- | :--- | :--- |
| `Caver::ConfigureOverlayViewController::LoadView` | `0x0033DE4C` | `0x0026A120` | Loads touch controls editor overlay |
| `Caver::SettingsViewController::LoadView` | `0x00342C0C` | `0x0026EC80` | Loads audio sliders and options GUI |
| `Caver::GameOptions::SetControlsLayout` | `0x0033E338` | `0x0026A590` | Saves custom button positions to `controls.ini` |

---

## 2. Reconstructed `Caver::SettingsView` C++ Snippets

```cpp
#include "GUIViewController.h"
#include "GUISlider.h"
#include "GUISwitch.h"
#include "GUIButton.h"

namespace Caver {

class SettingsView : public GUIView {
public:
    SettingsView();
    virtual ~SettingsView();

    virtual void setMusicVolume(float volume);
    virtual void setSFXVolume(float volume);

private:
    boost::shared_ptr<GUISlider>  m_music_slider;
    boost::shared_ptr<GUISlider>  m_sfx_slider;
    boost::shared_ptr<GUIButton>  m_configure_controls_button;
    boost::shared_ptr<GUISwitch>  m_cloud_sync_switch;
};

class ConfigureOverlayView : public GUIView {
public:
    ConfigureOverlayView();
    virtual ~ConfigureOverlayView();

    virtual void setButtonScale(float scale);
    virtual void resetToDefaults();

    // Drag-and-drop handling for touch buttons
    virtual bool TouchBegan(const FWTouch& touch) override;
    virtual bool TouchMoved(const FWTouch& touch) override;

private:
    boost::shared_ptr<GUIView>   m_dragged_button;
    boost::shared_ptr<GUIButton> m_reset_button;
    boost::shared_ptr<GUIButton> m_save_button;
};

class ConfigureOverlayViewController : public GUIViewController {
public:
    ConfigureOverlayViewController();
    virtual ~ConfigureOverlayViewController();

    virtual void loadView() override;
    virtual void saveAndDismiss();

private:
    boost::shared_ptr<ConfigureOverlayView> m_configure_view;
};

// --- Implementations ---

ConfigureOverlayView::ConfigureOverlayView() {
    m_reset_button.reset(new GUIButton());
    m_reset_button->setTitle("RESET");
    this->addSubview(m_reset_button);

    m_save_button.reset(new GUIButton());
    m_save_button->setTitle("DONE");
    this->addSubview(m_save_button);
}

ConfigureOverlayView::~ConfigureOverlayView() {}

bool ConfigureOverlayView::TouchBegan(const FWTouch& touch) {
    // Find which virtual touch button was pressed and mark as m_dragged_button
    return GUIView::TouchBegan(touch);
}

bool ConfigureOverlayView::TouchMoved(const FWTouch& touch) {
    if (m_dragged_button) {
        // Update position of m_dragged_button according to touch coordinates
        m_dragged_button->setFrame({touch.x, touch.y, m_dragged_button->frame().width, m_dragged_button->frame().height});
        return true;
    }
    return GUIView::TouchMoved(touch);
}

} // namespace Caver
```

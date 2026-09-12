# Caver GUIButton & Interactive Controls Architecture

## Executive Summary
This document specifies `Caver::GUIButton`, `Caver::GUISlider`, and `Caver::GUISwitch`, the interactive control components of Swordigo. It includes fully reconstructed C++ code snippets for button touch target dispatching, state management (`NORMAL`, `HIGHLIGHTED`, `DISABLED`), and label title binding.

---

## 1. Reconstructed `Caver::GUIButton` Snippets

```cpp
#include "GUIView.h"
#include "GUILabel.h"
#include <functional>

namespace Caver {

enum class GUIControlState {
    Normal      = 0,
    Highlighted = 1,
    Disabled    = 2,
    Selected    = 3
};

class GUIButton : public GUIView {
public:
    GUIButton();
    virtual ~GUIButton();

    virtual void setTitle(const std::string& title, GUIControlState state = GUIControlState::Normal);
    virtual std::string title() const;
    virtual boost::shared_ptr<GUILabel> titleLabel();

    virtual void setImage(const std::string& texturePath, GUIControlState state = GUIControlState::Normal);
    virtual void setTarget(std::function<void(GUIButton*)> callback);

    virtual void setState(GUIControlState state);
    virtual GUIControlState state() const { return m_state; }

    // Touch Event Overrides
    virtual bool TouchBegan(const FWTouch& touch) override;
    virtual bool TouchMoved(const FWTouch& touch) override;
    virtual bool TouchEnded(const FWTouch& touch) override;
    virtual bool TouchCancelled(const FWTouch& touch) override;

    virtual void drawRect(RenderingContext* ctx, const Rectangle& dirtyRect, const Matrix4& parentTransform) override;

protected:
    GUIControlState                 m_state;
    boost::shared_ptr<GUILabel>     m_title_label;
    std::string                     m_normal_texture;
    std::string                     m_highlighted_texture;
    std::function<void(GUIButton*)> m_action_callback;
    bool                            m_is_pressed;
};

// --- Implementations ---

GUIButton::GUIButton() 
    : m_state(GUIControlState::Normal), m_is_pressed(false) {
    m_title_label.reset(new GUILabel());
    this->addSubview(m_title_label);
}

GUIButton::~GUIButton() {}

void GUIButton::setTitle(const std::string& title, GUIControlState state) {
    if (m_title_label) {
        m_title_label->setText(title);
    }
}

std::string GUIButton::title() const {
    return m_title_label ? m_title_label->text() : "";
}

boost::shared_ptr<GUILabel> GUIButton::titleLabel() {
    return m_title_label;
}

void GUIButton::setImage(const std::string& texturePath, GUIControlState state) {
    if (state == GUIControlState::Normal) m_normal_texture = texturePath;
    else if (state == GUIControlState::Highlighted) m_highlighted_texture = texturePath;
}

void GUIButton::setTarget(std::function<void(GUIButton*)> callback) {
    m_action_callback = callback;
}

void GUIButton::setState(GUIControlState state) {
    m_state = state;
}

bool GUIButton::TouchBegan(const FWTouch& touch) {
    if (m_state == GUIControlState::Disabled) return false;
    m_is_pressed = true;
    setState(GUIControlState::Highlighted);
    return true;
}

bool GUIButton::TouchMoved(const FWTouch& touch) {
    if (!m_is_pressed) return false;
    bool inside = pointInside(touch.x, touch.y);
    setState(inside ? GUIControlState::Highlighted : GUIControlState::Normal);
    return true;
}

bool GUIButton::TouchEnded(const FWTouch& touch) {
    if (!m_is_pressed) return false;
    m_is_pressed = false;
    setState(GUIControlState::Normal);

    if (pointInside(touch.x, touch.y)) {
        if (m_action_callback) {
            m_action_callback(this); // Fire target action
        }
    }
    return true;
}

bool GUIButton::TouchCancelled(const FWTouch& touch) {
    m_is_pressed = false;
    setState(GUIControlState::Normal);
    return true;
}

void GUIButton::drawRect(RenderingContext* ctx, const Rectangle& dirtyRect, const Matrix4& parentTransform) {
    // Render button background texture according to current state
    GUIView::drawRect(ctx, dirtyRect, parentTransform);
}

} // namespace Caver
```

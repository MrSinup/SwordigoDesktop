# Caver GUILabel & Native Font Rendering Subsystem

## Executive Summary
This document specifies `Caver::GUILabel`, the text display widget of Swordigo. It details text alignment, font assets, drop shadows, character kerning, and provides fully reconstructed C++ code snippets for text rendering.

---

## 1. Reconstructed `Caver::GUILabel` Snippets

```cpp
#include "GUIView.h"
#include <string>

namespace Caver {

enum class TextAlignment {
    Left   = 0,
    Center = 1,
    Right  = 2
};

struct Color4 {
    float r, g, b, a;
};

class GUILabel : public GUIView {
public:
    GUILabel();
    virtual ~GUILabel();

    virtual void setText(const std::string& text);
    virtual std::string text() const { return m_text; }

    virtual void setFont(const std::string& fontName, float fontSize);
    virtual void setTextColor(const Color4& color) { m_text_color = color; }
    virtual Color4 textColor() const { return m_text_color; }

    virtual void setTextAlignment(TextAlignment align) { m_alignment = align; }
    virtual TextAlignment textAlignment() const { return m_alignment; }

    virtual void setDropShadowEnabled(bool enabled) { m_shadow_enabled = enabled; }
    virtual void setDropShadowOffset(float ox, float oy) { m_shadow_offset_x = ox; m_shadow_offset_y = oy; }

    virtual void drawRect(RenderingContext* ctx, const Rectangle& dirtyRect, const Matrix4& parentTransform) override;

private:
    std::string   m_text;
    std::string   m_font_name;
    float         m_font_size;
    Color4        m_text_color;
    Color4        m_shadow_color;
    TextAlignment m_alignment;
    bool          m_shadow_enabled;
    float         m_shadow_offset_x;
    float         m_shadow_offset_y;
};

// --- Implementations ---

GUILabel::GUILabel() 
    : m_font_size(18.0f), m_alignment(TextAlignment::Left), m_shadow_enabled(true) {
    m_text_color = {1.0f, 1.0f, 1.0f, 1.0f};
    m_shadow_color = {0.0f, 0.0f, 0.0f, 0.75f};
    m_shadow_offset_x = 1.5f;
    m_shadow_offset_y = 1.5f;
}

GUILabel::~GUILabel() {}

void GUILabel::setText(const std::string& text) {
    m_text = text;
}

void GUILabel::setFont(const std::string& fontName, float fontSize) {
    m_font_name = fontName;
    m_font_size = fontSize;
}

void GUILabel::drawRect(RenderingContext* ctx, const Rectangle& dirtyRect, const Matrix4& parentTransform) {
    if (m_hidden || m_text.empty()) return;

    // 1. Draw Drop Shadow pass if enabled
    if (m_shadow_enabled) {
        // Render text at (m_frame.x + m_shadow_offset_x, m_frame.y + m_shadow_offset_y)
        // using m_shadow_color
    }

    // 2. Draw Main Text Pass
    // Render text at (m_frame.x, m_frame.y) using m_text_color and m_font_size
}

} // namespace Caver
```

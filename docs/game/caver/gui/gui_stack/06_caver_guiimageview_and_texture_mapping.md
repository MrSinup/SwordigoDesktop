# Caver GUIImageView & Texture Subsystem Architecture

## Executive Summary
This document specifies `Caver::GUIImageView`, the 2D image sprite widget of Swordigo. It details 9-patch slice rendering for UI dialog frames, UV coordinate bounding, blend modes, and provides reconstructed C++ code snippets.

---

## 1. Reconstructed `Caver::GUIImageView` Snippets

```cpp
#include "GUIView.h"
#include <string>

namespace Caver {

struct UVRect {
    float u0, v0, u1, v1;
};

enum class ImageScaleMode {
    Stretch     = 0,
    AspectFit   = 1,
    NinePatch   = 2
};

class GUIImageView : public GUIView {
public:
    GUIImageView();
    virtual ~GUIImageView();

    virtual void setImageTexture(const std::string& texturePath);
    virtual void setUVRect(const UVRect& uv) { m_uv_rect = uv; }
    virtual UVRect uvRect() const { return m_uv_rect; }

    virtual void setTintColor(const Color4& color) { m_tint_color = color; }
    virtual Color4 tintColor() const { return m_tint_color; }

    virtual void setScaleMode(ImageScaleMode mode) { m_scale_mode = mode; }
    virtual void setNinePatchMargins(float left, float right, float top, float bottom);

    virtual void drawRect(RenderingContext* ctx, const Rectangle& dirtyRect, const Matrix4& parentTransform) override;

private:
    std::string    m_texture_path;
    UVRect         m_uv_rect;
    Color4         m_tint_color;
    ImageScaleMode m_scale_mode;
    float          m_nine_left, m_nine_right, m_nine_top, m_nine_bottom;
};

// --- Implementations ---

GUIImageView::GUIImageView() 
    : m_scale_mode(ImageScaleMode::Stretch), m_nine_left(0), m_nine_right(0), m_nine_top(0), m_nine_bottom(0) {
    m_uv_rect = {0.0f, 0.0f, 1.0f, 1.0f};
    m_tint_color = {1.0f, 1.0f, 1.0f, 1.0f};
}

GUIImageView::~GUIImageView() {}

void GUIImageView::setImageTexture(const std::string& texturePath) {
    m_texture_path = texturePath;
}

void GUIImageView::setNinePatchMargins(float left, float right, float top, float bottom) {
    m_scale_mode = ImageScaleMode::NinePatch;
    m_nine_left = left;
    m_nine_right = right;
    m_nine_top = top;
    m_nine_bottom = bottom;
}

void GUIImageView::drawRect(RenderingContext* ctx, const Rectangle& dirtyRect, const Matrix4& parentTransform) {
    if (m_hidden || m_texture_path.empty()) return;

    if (m_scale_mode == ImageScaleMode::NinePatch) {
        // Draw 9 quad patches to scale borders independently from center
    } else {
        // Draw single textured quad
    }

    GUIView::drawRect(ctx, dirtyRect, parentTransform);
}

} // namespace Caver
```

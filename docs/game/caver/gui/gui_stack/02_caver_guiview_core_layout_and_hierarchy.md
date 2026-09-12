# Caver GUIView Core Layout & View Hierarchy

## Executive Summary
This document specifies `Caver::GUIView`, the core visual render node of Swordigo's native UI system. It details subview management, 2D transform matrices, layout boundaries, hit testing, and provides fully reconstructed C++ class snippets.

---

## 1. Reconstructed `Caver::GUIView` C++ Class Snippet

```cpp
#include "GUIResponder.h"
#include <vector>
#include <boost/shared_ptr.hpp>

namespace Caver {

struct Rectangle {
    float x, y, width, height;
};

struct Matrix4 {
    float m[16];
};

class RenderingContext;

class GUIView : public GUIResponder {
public:
    GUIView();
    virtual ~GUIView();

    // Frame & Geometry
    virtual void setFrame(const Rectangle& rect);
    virtual Rectangle frame() const { return m_frame; }
    virtual void setBounds(const Rectangle& rect);
    virtual Rectangle bounds() const { return m_bounds; }
    virtual Rectangle safeBounds() const;

    // View Hierarchy Management
    virtual void addSubview(boost::shared_ptr<GUIView> subview);
    virtual void removeFromSuperview();
    virtual GUIView* superview() const { return m_superview; }
    virtual const std::vector<boost::shared_ptr<GUIView>>& subviews() const { return m_subviews; }

    // Rendering & Layout Pipeline
    virtual void layoutSubviews();
    virtual void drawRect(RenderingContext* ctx, const Rectangle& dirtyRect, const Matrix4& parentTransform);
    virtual void setHidden(bool hidden) { m_hidden = hidden; }
    virtual bool isHidden() const { return m_hidden; }
    virtual void setAlpha(float alpha) { m_alpha = alpha; }
    virtual float alpha() const { return m_alpha; }

    // Hit Testing
    virtual bool pointInside(float x, float y) const;
    virtual boost::shared_ptr<GUIView> hitTest(float x, float y);

protected:
    Rectangle                               m_frame;
    Rectangle                               m_bounds;
    GUIView*                                m_superview;
    std::vector<boost::shared_ptr<GUIView>> m_subviews;
    Matrix4                                 m_transform;
    float                                   m_alpha;
    bool                                    m_hidden;
    bool                                    m_user_interaction_enabled;
};

// --- Implementations ---

GUIView::GUIView() 
    : m_superview(nullptr), m_alpha(1.0f), m_hidden(false), m_user_interaction_enabled(true) {
    m_frame = {0.0f, 0.0f, 100.0f, 100.0f};
    m_bounds = {0.0f, 0.0f, 100.0f, 100.0f};
}

GUIView::~GUIView() {
    m_subviews.clear();
}

void GUIView::setFrame(const Rectangle& rect) {
    m_frame = rect;
    m_bounds.width = rect.width;
    m_bounds.height = rect.height;
    layoutSubviews();
}

Rectangle GUIView::safeBounds() const {
    return m_bounds;
}

void GUIView::addSubview(boost::shared_ptr<GUIView> subview) {
    if (!subview) return;
    if (subview->m_superview) {
        subview->removeFromSuperview();
    }
    subview->m_superview = this;
    m_subviews.push_back(subview);
    subview->layoutSubviews();
}

void GUIView::removeFromSuperview() {
    if (!m_superview) return;
    auto& sibs = m_superview->m_subviews;
    for (auto it = sibs.begin(); it != sibs.end(); ++it) {
        if (it->get() == this) {
            sibs.erase(it);
            break;
        }
    }
    m_superview = nullptr;
}

void GUIView::layoutSubviews() {
    for (auto& child : m_subviews) {
        child->layoutSubviews();
    }
}

void GUIView::drawRect(RenderingContext* ctx, const Rectangle& dirtyRect, const Matrix4& parentTransform) {
    if (m_hidden || m_alpha <= 0.001f) return;

    // Render children recursively
    for (auto& child : m_subviews) {
        if (!child->isHidden()) {
            child->drawRect(ctx, dirtyRect, m_transform);
        }
    }
}

bool GUIView::pointInside(float x, float y) const {
    return (x >= m_frame.x && x <= m_frame.x + m_frame.width &&
            y >= m_frame.y && y <= m_frame.y + m_frame.height);
}

boost::shared_ptr<GUIView> GUIView::hitTest(float x, float y) {
    if (m_hidden || !m_user_interaction_enabled || m_alpha <= 0.001f) return nullptr;
    if (!pointInside(x, y)) return nullptr;

    // Top-to-bottom subview hit testing
    for (auto it = m_subviews.rbegin(); it != m_subviews.rend(); ++it) {
        auto hit = (*it)->hitTest(x - m_frame.x, y - m_frame.y);
        if (hit) return hit;
    }
    return shared_from_this();
}

} // namespace Caver
```

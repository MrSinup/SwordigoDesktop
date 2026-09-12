# Caver RenderingContext, Matrix Stack & Scissor Clipping

## Executive Summary
This document specifies `Caver::RenderingContext`, the core 2D/3D render state container passed during `drawRect` traversals in Swordigo. It details matrix stack transformations, scissor rectangle clipping, shader program selection, and hardware draw call generation.

---

## 1. Reconstructed `Caver::RenderingContext` C++ Snippets

```cpp
#include "GUIView.h"
#include <stack>

namespace Caver {

class ShaderProgram;
class Texture;

class RenderingContext {
public:
    RenderingContext();
    ~RenderingContext();

    // Matrix Stack Operations
    void pushMatrix();
    void popMatrix();
    void loadIdentity();
    void translate(float x, float y, float z);
    void scale(float sx, float sy, float sz);
    void multiplyMatrix(const Matrix4& mat);
    Matrix4 currentMatrix() const { return m_matrix_stack.top(); }

    // Scissor Clipping Stack
    void pushScissorRect(const Rectangle& rect);
    void popScissorRect();
    Rectangle currentScissorRect() const;

    // State Tracking
    void bindTexture(Texture* tex);
    void setShaderProgram(ShaderProgram* shader);
    void setBlendMode(int blendMode);

    // Hardware Draw Calls
    void drawTexturedQuad(const Rectangle& dstRect, const UVRect& uvRect, const Color4& color);
    void drawColoredQuad(const Rectangle& dstRect, const Color4& color);

private:
    std::stack<Matrix4>   m_matrix_stack;
    std::stack<Rectangle> m_scissor_stack;
    Texture*              m_bound_texture;
    ShaderProgram*        m_active_shader;
    int                   m_current_blend_mode;
};

// --- Implementations ---

RenderingContext::RenderingContext() {
    Matrix4 identity;
    // Initialize identity matrix
    m_matrix_stack.push(identity);
}

RenderingContext::~RenderingContext() {}

void RenderingContext::pushMatrix() {
    m_matrix_stack.push(m_matrix_stack.top());
}

void RenderingContext::popMatrix() {
    if (m_matrix_stack.size() > 1) {
        m_matrix_stack.pop();
    }
}

void RenderingContext::pushScissorRect(const Rectangle& rect) {
    m_scissor_stack.push(rect);
    // Execute glScissor / vkCmdSetScissor
}

void RenderingContext::popScissorRect() {
    if (!m_scissor_stack.empty()) {
        m_scissor_stack.pop();
        // Restore previous scissor rect
    }
}

void RenderingContext::drawTexturedQuad(const Rectangle& dstRect, const UVRect& uvRect, const Color4& color) {
    // Generates 4 vertices (2 triangles) with UVs and color tint, submits to OpenGL/Vulkan pipeline
}

} // namespace Caver
```

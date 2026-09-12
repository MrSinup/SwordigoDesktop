# Caver GUI Stack Architecture & Event Responder System

## Executive Summary
This document provides a reverse-engineering analysis of the core GUI architecture in Swordigo (`libswordigo.so`). It details the base `Caver::GUIResponder` class, event propagation, touch/keyboard routing, and reconstructed C++ snippets for native GUI hooking in SRE.

---

## 1. Class Hierarchy Overview

```
                      Caver::GUIResponder (Base Event Handler)
                               │
            ┌──────────────────┴──────────────────┐
            ▼                                     ▼
     Caver::GUIView                     Caver::GUIViewController
  (Visual Render Node)                   (View Lifecycle Manager)
            │                                     │
   ┌────────┼────────┬────────┐          ┌────────┴────────┐
   ▼        ▼        ▼        ▼          ▼                 ▼
Button    Label  ImageView Window   GameViewCtrl   MenuNavCtrl
```

---

## 2. Reconstructed `Caver::GUIResponder` C++ Snippets

### Class Header Definition (`GUIResponder.h`)
```cpp
namespace Caver {

struct FWTouch {
    int id;
    float x;
    float y;
    int phase; // 0=Began, 1=Moved, 2=Ended, 3=Cancelled
};

struct GUIEvent {
    int type;
    void* sender;
    uint64_t timestamp;
};

struct GUIKeyboardEvent {
    int keycode;
    int scancode;
    bool is_down;
    uint32_t modifiers;
};

class GUIResponder : public boost::enable_shared_from_this<GUIResponder> {
public:
    GUIResponder();
    virtual ~GUIResponder();

    // Virtual Event Responders (Vtable Offsets 0x08 - 0x38)
    virtual GUIResponder* nextResponder() const;
    virtual bool TouchBegan(const FWTouch& touch);
    virtual bool TouchMoved(const FWTouch& touch);
    virtual bool TouchEnded(const FWTouch& touch);
    virtual bool TouchCancelled(const FWTouch& touch);
    virtual bool HandleKeyboardEvent(const GUIKeyboardEvent& event);
    virtual bool CanBecomeFirstResponder() const { return false; }
    virtual bool BecomeFirstResponder();
    virtual bool ResignFirstResponder();
    virtual bool IsFirstResponder() const;

private:
    GUIResponder* m_next_responder;
    bool          m_is_first_responder;
};

} // namespace Caver
```

### Reconstructed Method Implementations (`GUIResponder.cpp`)
```cpp
#include "GUIResponder.h"

namespace Caver {

GUIResponder::GUIResponder() 
    : m_next_responder(nullptr), m_is_first_responder(false) {}

GUIResponder::~GUIResponder() {}

GUIResponder* GUIResponder::nextResponder() const {
    return m_next_responder;
}

bool GUIResponder::TouchBegan(const FWTouch& touch) {
    // Default implementation passes event down the responder chain
    if (m_next_responder) {
        return m_next_responder->TouchBegan(touch);
    }
    return false;
}

bool GUIResponder::TouchMoved(const FWTouch& touch) {
    if (m_next_responder) {
        return m_next_responder->TouchMoved(touch);
    }
    return false;
}

bool GUIResponder::TouchEnded(const FWTouch& touch) {
    if (m_next_responder) {
        return m_next_responder->TouchEnded(touch);
    }
    return false;
}

bool GUIResponder::TouchCancelled(const FWTouch& touch) {
    if (m_next_responder) {
        return m_next_responder->TouchCancelled(touch);
    }
    return false;
}

bool GUIResponder::HandleKeyboardEvent(const GUIKeyboardEvent& event) {
    if (m_next_responder) {
        return m_next_responder->HandleKeyboardEvent(event);
    }
    return false;
}

} // namespace Caver
```

---

## 3. Virtual Memory & Vtable Offsets (ARM64 v1.4.12)

| Symbol / Method | ARM64 Virtual Address | Vtable Slot |
| :--- | :--- | :--- |
| `Caver::GUIResponder::nextResponder` | `0x00350674` | `vtable[1]` |
| `Caver::GUIResponder::TouchBegan` | `0x0034FE90` | `vtable[2]` |
| `Caver::GUIResponder::TouchMoved` | `0x00350358` | `vtable[3]` |
| `Caver::GUIResponder::TouchEnded` | `0x00350364` | `vtable[4]` |
| `Caver::GUIResponder::TouchCancelled` | `0x00350370` | `vtable[5]` |
| `Caver::GUIResponder::HandleKeyboardEvent` | `0x003505A8` | `vtable[6]` |

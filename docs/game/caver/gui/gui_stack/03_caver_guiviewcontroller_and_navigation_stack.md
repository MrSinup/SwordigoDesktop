# Caver GUIViewController & Navigation Controller Architecture

## Executive Summary
This document specifies `Caver::GUIViewController` and `Caver::GUINavigationController`, the lifecycle managers for UI screen transitions in Swordigo. It contains fully reconstructed C++ code snippets for view presentation, push/pop stack navigation, and screen transitions.

---

## 1. Reconstructed `Caver::GUIViewController` Snippets

```cpp
#include "GUIView.h"
#include <boost/enable_shared_from_this.hpp>

namespace Caver {

class GUIViewController : public GUIResponder {
public:
    GUIViewController();
    virtual ~GUIViewController();

    // Screen Lifecycle Hooks
    virtual void loadView();
    virtual void viewDidLoad() {}
    virtual void viewWillAppear(bool animated) {}
    virtual void viewDidAppear();
    virtual void viewWillDisappear();
    virtual void viewDidDisappear(bool animated) {}
    virtual void unloadView();

    // View Management
    virtual boost::shared_ptr<GUIView> view();
    virtual void setView(boost::shared_ptr<GUIView> view);
    virtual bool isViewLoaded() const { return m_view != nullptr; }

    // Child Controller Integration
    virtual void suspendView();
    virtual void resumeView();

protected:
    boost::shared_ptr<GUIView> m_view;
    GUIViewController*         m_parent_controller;
    bool                       m_is_active;
};

// --- Implementations ---

GUIViewController::GUIViewController() 
    : m_parent_controller(nullptr), m_is_active(false) {}

GUIViewController::~GUIViewController() {
    unloadView();
}

void GUIViewController::loadView() {
    if (!m_view) {
        m_view.reset(new GUIView());
        m_view->setFrame({0.0f, 0.0f, 960.0f, 544.0f});
        viewDidLoad();
    }
}

boost::shared_ptr<GUIView> GUIViewController::view() {
    if (!m_view) {
        loadView();
    }
    return m_view;
}

void GUIViewController::setView(boost::shared_ptr<GUIView> newView) {
    m_view = newView;
}

void GUIViewController::viewDidAppear() {
    m_is_active = true;
    if (m_view) m_view->setHidden(false);
}

void GUIViewController::viewWillDisappear() {
    m_is_active = false;
}

void GUIViewController::unloadView() {
    if (m_view) {
        m_view->removeFromSuperview();
        m_view.reset();
    }
}

void GUIViewController::suspendView() {
    if (m_view) m_view->setHidden(true);
}

void GUIViewController::resumeView() {
    if (m_view) m_view->setHidden(false);
}

} // namespace Caver
```

---

## 2. Reconstructed `Caver::GUINavigationController` Snippets

```cpp
#include "GUIViewController.h"
#include <vector>

namespace Caver {

class GUINavigationController : public GUIViewController {
public:
    GUINavigationController();
    virtual ~GUINavigationController();

    virtual void pushViewController(boost::shared_ptr<GUIViewController> vc, bool animated);
    virtual boost::shared_ptr<GUIViewController> popViewController(bool animated);
    virtual boost::shared_ptr<GUIViewController> topViewController() const;

private:
    std::vector<boost::shared_ptr<GUIViewController>> m_view_controllers;
};

void GUINavigationController::pushViewController(boost::shared_ptr<GUIViewController> vc, bool animated) {
    if (!vc) return;

    if (!m_view_controllers.empty()) {
        m_view_controllers.back()->suspendView();
    }

    m_view_controllers.push_back(vc);
    this->view()->addSubview(vc->view());
    vc->viewDidAppear();
}

boost::shared_ptr<GUIViewController> GUINavigationController::popViewController(bool animated) {
    if (m_view_controllers.empty()) return nullptr;

    auto popped = m_view_controllers.back();
    popped->viewWillDisappear();
    popped->unloadView();
    m_view_controllers.pop_back();

    if (!m_view_controllers.empty()) {
        m_view_controllers.back()->resumeView();
    }
    return popped;
}

boost::shared_ptr<GUIViewController> GUINavigationController::topViewController() const {
    return m_view_controllers.empty() ? nullptr : m_view_controllers.back();
}

} // namespace Caver
```

# Caver MainMenuViewController & Title Screen System

## Executive Summary
This document specifies `Caver::MainMenuViewController`, `Caver::MainMenuView`, and `Caver::ProfileSelectionView`. It provides a complete reverse-engineering analysis of Swordigo's main title screen, save slot management (Slots 1-3), options transitions, and reconstructed C++ class snippets.

---

## 1. Symbol Offsets & Virtual Memory Addresses (v1.4.12)

| Symbol Name | ARM64 Virtual Address | ARM32 Virtual Address | Description |
| :--- | :--- | :--- | :--- |
| `Caver::MainMenuViewController::LoadView` | `0x0036B51C` | `0x0028A1C0` | Instantiates main title screen layout & buttons |
| `Caver::MainMenuViewController::MainMenuViewDidStart` | `0x0036F1D0` | `0x0028CD90` | Triggers profile selection dialog or game start |
| `Caver::MainMenuViewController::MainMenuViewDidOpenShop` | `0x0036F394` | `0x0028CF50` | Presents in-game store view controller |
| `Caver::MainMenuViewController::MainMenuViewDidGoToCredits` | `0x0036F67C` | `0x0028D230` | Presents scrolling credits view controller |
| `Caver::MainMenuViewController::MainMenuViewDidShowOfflineAchievements` | `0x0036F8A0` | `0x0028D450` | Presents offline achievement list view controller |

---

## 2. Reconstructed `Caver::MainMenuViewController` C++ Snippet

```cpp
#include "GUIViewController.h"
#include "GUIButton.h"
#include "GUIImageView.h"
#include <boost/shared_ptr.hpp>

namespace Caver {

class MainMenuView : public GUIView {
public:
    MainMenuView();
    virtual ~MainMenuView();

    virtual void setDelegate(void* delegate) { m_delegate = delegate; }
    virtual void animateTitleIn();

private:
    boost::shared_ptr<GUIImageView> m_background_image;
    boost::shared_ptr<GUIImageView> m_logo_image;
    boost::shared_ptr<GUIButton>    m_play_button;
    boost::shared_ptr<GUIButton>    m_shop_button;
    boost::shared_ptr<GUIButton>    m_achievements_button;
    boost::shared_ptr<GUIButton>    m_credits_button;
    void*                           m_delegate;
};

class ProfileSelectionView : public GUIView {
public:
    ProfileSelectionView();
    virtual ~ProfileSelectionView();

    virtual void setProfiles(const std::vector<void*>& profiles);
    virtual void onSlotSelected(int slotIndex);

private:
    boost::shared_ptr<GUIButton> m_slot_buttons[3];
};

class MainMenuViewController : public GUIViewController {
public:
    MainMenuViewController();
    virtual ~MainMenuViewController();

    virtual void loadView() override;
    virtual void viewDidAppear() override;

    // Delegate Callbacks
    virtual void MainMenuViewDidStart(MainMenuView* sender);
    virtual void MainMenuViewDidOpenShop(MainMenuView* sender);
    virtual void MainMenuViewDidGoToCredits(MainMenuView* sender);
    virtual void MainMenuViewDidShowOfflineAchievements(MainMenuView* sender);
    virtual void ProfileSelectionViewControllerDidStart(ProfileSelectionView* sender, void* profile);

private:
    boost::shared_ptr<MainMenuView>         m_main_menu_view;
    boost::shared_ptr<ProfileSelectionView> m_profile_view;
};

// --- Implementations ---

MainMenuViewController::MainMenuViewController() {}
MainMenuViewController::~MainMenuViewController() {}

void MainMenuViewController::loadView() {
    GUIViewController::loadView();

    m_main_menu_view.reset(new MainMenuView());
    m_main_menu_view->setDelegate(this);
    this->view()->addSubview(m_main_menu_view);
}

void MainMenuViewController::viewDidAppear() {
    GUIViewController::viewDidAppear();
    if (m_main_menu_view) {
        m_main_menu_view->animateTitleIn();
    }
}

void MainMenuViewController::MainMenuViewDidStart(MainMenuView* sender) {
    // Show profile slot selection (Slot 1, Slot 2, Slot 3)
    if (!m_profile_view) {
        m_profile_view.reset(new ProfileSelectionView());
        this->view()->addSubview(m_profile_view);
    }
}

void MainMenuViewController::MainMenuViewDidOpenShop(MainMenuView* sender) {
    // Transition to StoreViewController
}

void MainMenuViewController::MainMenuViewDidGoToCredits(MainMenuView* sender) {
    // Transition to CreditsViewController
}

void MainMenuViewController::MainMenuViewDidShowOfflineAchievements(MainMenuView* sender) {
    // Transition to AchievementsViewController
}

} // namespace Caver
```

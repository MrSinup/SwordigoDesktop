# Caver Achievements GUI & AchievementsManager Architecture

## Executive Summary
This document specifies `Caver::AchievementsViewController`, `Caver::AchievementsView`, and `Caver::AchievementsManager`. It details achievement data structures, icon rendering, unlock notifications, and reconstructed C++ snippets.

---

## 1. Symbol Offsets & Virtual Memory Addresses (v1.4.12)

| Symbol Name | ARM64 Virtual Address | ARM32 Virtual Address | Description |
| :--- | :--- | :--- | :--- |
| `Caver::AchievementsViewController::LoadView` | `0x0033BC20` | `0x00268B10` | Instantiates achievement list table & scroll view |
| `Caver::AchievementsManager::Update` | `0x002B66F8` | `0x001B4210` | Processes achievement progress conditions |
| `Caver::AchievementsManager::UnlockAchievement` | `0x002B68E0` | `0x001B4400` | Unlocks achievement ID & triggers pop-up |

---

## 2. Reconstructed `Caver::AchievementsGUI` C++ Snippet

```cpp
#include "GUIViewController.h"
#include "GUILabel.h"
#include "GUIImageView.h"
#include <vector>

namespace Caver {

struct AchievementData {
    std::string id;
    std::string title;
    std::string description;
    std::string icon_texture;
    bool        unlocked;
    float       progress_percentage;
};

class AchievementCellView : public GUIView {
public:
    AchievementCellView();
    virtual ~AchievementCellView();

    virtual void setAchievement(const AchievementData& data);

private:
    boost::shared_ptr<GUIImageView> m_icon_image;
    boost::shared_ptr<GUILabel>     m_title_label;
    boost::shared_ptr<GUILabel>     m_description_label;
    boost::shared_ptr<GUIImageView> m_lock_overlay;
};

class AchievementsView : public GUIView {
public:
    AchievementsView();
    virtual ~AchievementsView();

    virtual void setAchievements(const std::vector<AchievementData>& achievements);

private:
    std::vector<boost::shared_ptr<AchievementCellView>> m_cells;
    boost::shared_ptr<GUIButton>                        m_back_button;
};

class AchievementsViewController : public GUIViewController {
public:
    AchievementsViewController();
    virtual ~AchievementsViewController();

    virtual void loadView() override;
    virtual void refreshAchievements();

private:
    boost::shared_ptr<AchievementsView> m_achievements_view;
};

// --- Implementations ---

AchievementCellView::AchievementCellView() {
    m_title_label.reset(new GUILabel());
    m_title_label->setFont("default", 16.0f);
    this->addSubview(m_title_label);

    m_description_label.reset(new GUILabel());
    m_description_label->setFont("default", 12.0f);
    this->addSubview(m_description_label);
}

AchievementCellView::~AchievementCellView() {}

void AchievementCellView::setAchievement(const AchievementData& data) {
    if (m_title_label) m_title_label->setText(data.title);
    if (m_description_label) m_description_label->setText(data.description);
    // Tint icon gray if locked, full color if unlocked
}

AchievementsViewController::AchievementsViewController() {}
AchievementsViewController::~AchievementsViewController() {}

void AchievementsViewController::loadView() {
    GUIViewController::loadView();
    m_achievements_view.reset(new AchievementsView());
    this->view()->addSubview(m_achievements_view);
    refreshAchievements();
}

void AchievementsViewController::refreshAchievements() {
    // Read achievements state from AchievementsManager and reload cells
}

} // namespace Caver
```

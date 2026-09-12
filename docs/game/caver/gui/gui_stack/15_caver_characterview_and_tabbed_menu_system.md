# Caver CharacterView & Tabbed Menu System Architecture

## Executive Summary
This document specifies `Caver::CharacterView`, `Caver::TabbedMenuView`, and `Caver::TabbedMenuViewController`. It details the character paper doll inventory view, equipped item slots, item stat comparison tooltips, and tabbed menu bar transitions.

---

## 1. Symbol Offsets & Virtual Memory Addresses (v1.4.12)

| Symbol Name | ARM64 Virtual Address | ARM32 Virtual Address | Description |
| :--- | :--- | :--- | :--- |
| `Caver::CharacterView::CharacterView` | `0x00345138` | `0x00270C40` | Character equipment doll & stat overview view |
| `Caver::TabbedMenuView::SelectedTabDidChange` | `0x003437FC` | `0x0026F740` | Handles tab bar switching between Character, Inventory, and Settings |
| `Caver::GameMenuViewController::LoadView` | `0x00342F7C` | `0x0026EFF0` | Main pause/character menu view controller |

---

## 2. Reconstructed C++ Snippets

```cpp
#include "GUIViewController.h"
#include "GUIImageView.h"
#include "GUILabel.h"

namespace Caver {

class CharacterView : public GUIView {
public:
    CharacterView();
    virtual ~CharacterView();

    virtual void setEquippedSword(const std::string& swordId, const std::string& iconPath);
    virtual void setEquippedArmor(const std::string& armorId, const std::string& iconPath);
    virtual void setEquippedSpell(const std::string& spellId, const std::string& iconPath);
    virtual void setEquippedTrinket(int slot, const std::string& trinketId, const std::string& iconPath);

    virtual void updateStats(int attackPower, int defensePower, int magicPower);

private:
    boost::shared_ptr<GUIImageView> m_paper_doll_hero;
    boost::shared_ptr<GUIImageView> m_sword_slot;
    boost::shared_ptr<GUIImageView> m_armor_slot;
    boost::shared_ptr<GUIImageView> m_spell_slot;
    boost::shared_ptr<GUIImageView> m_trinket_slots[3];

    boost::shared_ptr<GUILabel>     m_attack_val_label;
    boost::shared_ptr<GUILabel>     m_defense_val_label;
    boost::shared_ptr<GUILabel>     m_magic_val_label;
};

class TabbedMenuView : public GUIView {
public:
    TabbedMenuView();
    virtual ~TabbedMenuView();

    virtual void setTabTitles(const std::vector<std::string>& titles);
    virtual void setSelectedTab(int index);

private:
    std::vector<boost::shared_ptr<GUIButton>> m_tab_buttons;
    int                                       m_selected_index;
};

// --- Implementations ---

CharacterView::CharacterView() {
    m_paper_doll_hero.reset(new GUIImageView());
    m_paper_doll_hero->setImageTexture("textures/ui/paper_doll_hero.pvr");
    this->addSubview(m_paper_doll_hero);

    m_sword_slot.reset(new GUIImageView());
    m_armor_slot.reset(new GUIImageView());
    m_spell_slot.reset(new GUIImageView());
    this->addSubview(m_sword_slot);
    this->addSubview(m_armor_slot);
    this->addSubview(m_spell_slot);

    m_attack_val_label.reset(new GUILabel());
    m_defense_val_label.reset(new GUILabel());
    m_magic_val_label.reset(new GUILabel());
    this->addSubview(m_attack_val_label);
    this->addSubview(m_defense_val_label);
    this->addSubview(m_magic_val_label);
}

CharacterView::~CharacterView() {}

void CharacterView::setEquippedSword(const std::string& swordId, const std::string& iconPath) {
    if (m_sword_slot) m_sword_slot->setImageTexture(iconPath);
}

void CharacterView::updateStats(int attackPower, int defensePower, int magicPower) {
    if (m_attack_val_label) m_attack_val_label->setText(std::to_string(attackPower));
    if (m_defense_val_label) m_defense_val_label->setText(std::to_string(defensePower));
    if (m_magic_val_label) m_magic_val_label->setText(std::to_string(magicPower));
}

} // namespace Caver
```

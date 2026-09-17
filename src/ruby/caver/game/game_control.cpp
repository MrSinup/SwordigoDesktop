#include "ruby/caver/game/game_control.h"

namespace caver {
namespace game {

void PlayerInput::button_down(GameControlButton button) {
    switch (button) {
        case GameControlButton::MoveLeft:    move_axis = -1.0f; break;
        case GameControlButton::MoveRight:   move_axis =  1.0f; break;
        case GameControlButton::Jump:
            if (!jump_down) jump_pressed = true;
            jump_down = true;
            break;
        case GameControlButton::Attack:      attack_down = true; break;
        case GameControlButton::Use:         use_down = true;    break;
        case GameControlButton::CastSkill:   cast_down = true;   break;
        case GameControlButton::EnterPortal: portal_down = true; break;
        case GameControlButton::Talk:        talk_down = true;   break;
        default: break;
    }
}

void PlayerInput::button_up(GameControlButton button) {
    switch (button) {
        case GameControlButton::MoveLeft:
            if (move_axis < 0.0f) move_axis = 0.0f;
            break;
        case GameControlButton::MoveRight:
            if (move_axis > 0.0f) move_axis = 0.0f;
            break;
        case GameControlButton::Jump:        jump_down = false;   break;
        case GameControlButton::Attack:      attack_down = false; break;
        case GameControlButton::Use:         use_down = false;    break;
        case GameControlButton::CastSkill:   cast_down = false;   break;
        case GameControlButton::EnterPortal: portal_down = false; break;
        case GameControlButton::Talk:        talk_down = false;   break;
        default: break;
    }
}

const char* button_name(GameControlButton button) {
    switch (button) {
        case GameControlButton::MoveLeft:    return "MoveLeft";
        case GameControlButton::MoveRight:   return "MoveRight";
        case GameControlButton::Jump:        return "Jump";
        case GameControlButton::Attack:      return "Attack";
        case GameControlButton::Use:         return "Use";
        case GameControlButton::CastSkill:   return "CastSkill";
        case GameControlButton::Pause:       return "Pause";
        case GameControlButton::Menu:        return "Menu";
        case GameControlButton::Map:         return "Map";
        case GameControlButton::EnterPortal: return "EnterPortal";
        case GameControlButton::Talk:        return "Talk";
        default:                             return "None";
    }
}

} // namespace game
} // namespace caver

#pragma once
// game_control.h — the recovered player input surface.
//
// `Caver::GameControlButton` is the enum the touch/pad layer feeds into
// `GameSceneController::GameControlButtonDown/Up`. The values below are read
// straight off the switch in that function (arm32_13, 0x315314) — each case is
// annotated with the call it makes, so the mapping is evidence, not a guess:
//
//   case 1: case 2:  CharControllerComponent movement axis (left / right)
//   case 3:          CanJump()          -> jump
//   case 4:          !CanPickup() && CanSwing() -> swing (attack)
//   case 5:          CanUse() ? use : (CanPickup() ? pickup : nothing)
//   case 6:          CharacterState.CurrentSkill -> BeginCasting()
//   case 10:         every PortalComponent      -> PortalComponent::Enter()
//   case 11:         every TextBubbleComponent  -> TouchableComponent::Trigger()
//
// 7/8/9 are not handled by the game controller: they are the menu/pause buttons
// the shell consumes before the scene controller ever sees them.
//
// Button-up uses the same numbering and stops the matching action, which is why
// movement is an axis (1/2) rather than two independent buttons.

#include <cstdint>

namespace caver {
namespace game {

enum class GameControlButton : int32_t {
    None        = 0,
    MoveLeft    = 1,
    MoveRight   = 2,
    Jump        = 3,
    Attack      = 4,
    Use         = 5,
    CastSkill   = 6,
    Pause       = 7,
    Menu        = 8,
    Map         = 9,
    EnterPortal = 10,
    Talk        = 11,
};

// Which of the game's own actions a control is currently asking for. This is the
// same set of booleans `GameSceneController::Update` reads out of its control
// state, kept as a plain struct so a keyboard, a pad, or a replay file can all
// drive it.
struct PlayerInput {
    float move_axis = 0.0f;   // -1 left, +1 right (buttons 1/2)
    bool  jump_down = false;
    // The jump is edge-triggered as well as held: CharControllerComponent's state
    // machine starts a jump on the transition, then watches the button to decide
    // how long the rising window stays open.
    bool  jump_pressed = false;
    bool  attack_down = false;
    bool  use_down = false;
    bool  cast_down = false;
    bool  portal_down = false;
    bool  talk_down = false;

    void button_down(GameControlButton button);
    void button_up(GameControlButton button);
};

const char* button_name(GameControlButton button);

} // namespace game
} // namespace caver

// ============================================================================
// rbsrc_test.cpp — the merged .rbsrc format and its Lua lowering.
//
// The two shipped patterns that define what "correct" means here:
//
//   blackhole  (rlsw.scl)        a bounded growth loop that waits on a *stride*
//   item1      (town_shop.scene) one call at t = 0, no motion at all
//
// Plus the API validator, which is what stops the scripter emitting a call the
// game does not publish.
// ============================================================================

#include "tools/rbsrc.h"

#include <algorithm>
#include <iostream>
#include <string>

using namespace rbsrc;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, what)                                                        \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::cout << "  FAIL: " << (what) << "\n"                            \
                      << "        expr: " << #cond << "\n";                      \
        }                                                                        \
    } while (0)

static int count_of(const std::string& haystack, const std::string& needle) {
    int n = 0;
    for (size_t pos = haystack.find(needle); pos != std::string::npos;
         pos = haystack.find(needle, pos + needle.size())) {
        ++n;
    }
    return n;
}

// ── the extracted API ───────────────────────────────────────────────────────

static void test_api_table() {
    std::cout << "[rbsrc_test] Testing the extracted game API table...\n";

    CHECK(kGameLuaApiCount > 200, "the API table should carry the whole surface");
    CHECK(kGameLuaApiCount == 227, "the API table should be exactly 227 entries");

    // Namespaces the hand-written reconstruction never had.
    CHECK(find_api("Character", "AddQuest") != nullptr, "Character.AddQuest must exist");
    CHECK(find_api("Game", "SetCinematicMode") != nullptr, "Game.SetCinematicMode must exist");
    CHECK(find_api("Camera", "Rumble") != nullptr, "Camera.Rumble must exist");
    CHECK(find_api("EntityController", "SetMoveSpeed") != nullptr,
          "EntityController.SetMoveSpeed must exist");

    // The setters the timeline lowers to.
    CHECK(find_api("SceneObject", "setScaling") != nullptr, "SceneObject.setScaling");
    CHECK(find_api("SceneObject", "setPosition") != nullptr, "SceneObject.setPosition");
    CHECK(find_api("SceneObject", "setRotation") != nullptr, "SceneObject.setRotation");
    CHECK(find_api("SceneObject", "setDepth") != nullptr, "SceneObject.setDepth");
    CHECK(find_api("SceneObject", "setHidden") != nullptr, "SceneObject.setHidden");
    CHECK(find_api("SceneObject", "setVelocity") != nullptr, "SceneObject.setVelocity");

    // Class constructors and instance members come from the RegisterClass pair.
    CHECK(find_api("Vector3", "New") != nullptr, "Vector3.New");
    CHECK(find_api("Vector3", "FromAngle") != nullptr, "Vector3.FromAngle");
    CHECK(find_api("Vector3", ":x") != nullptr, "Vector3:x");
    CHECK(find_api("Vector3", ":normalized") != nullptr, "Vector3:normalized");
    CHECK(find_api("Vector3", ":__add") != nullptr, "Vector3.__add");
    CHECK(find_api("Rectangle", ":top") != nullptr, "Rectangle:top");

    // Mutating detection drives the marker warnings.
    const ApiFunction* coins = find_api("Character", "SetNumCoins");
    CHECK(coins && coins->mutating, "SetNumCoins must be flagged mutating");
    const ApiFunction* len = find_api("Vector3", ":length");
    CHECK(len && !len->mutating, "Vector3:length must not be flagged mutating");
}

static void test_validator() {
    std::cout << "[rbsrc_test] Testing call validation...\n";

    std::string err;
    CHECK(validate_call("Scene.Find", &err), "Scene.Find should validate");
    CHECK(validate_call("Program.Wait", &err), "Program.Wait should validate");
    CHECK(validate_call("Vector3.New", &err), "Vector3.New should validate");
    CHECK(validate_call("SoundLibrary.PlayEffect", &err), "SoundLibrary.PlayEffect");
    CHECK(validate_call("v:x", &err), "an instance member should validate");
    CHECK(validate_call("spray:setRotation", &err), "SceneObject:setRotation should validate");

    // Typos and inventions must be caught at author time, not at runtime.
    CHECK(!validate_call("Scene.Fnd", &err), "a typo must be rejected");
    CHECK(err.find("Scene.Fnd") != std::string::npos, "the error should name the call");
    CHECK(!validate_call("Player.AddXP", &err), "a hallucinated namespace must be rejected");
    CHECK(!validate_call("v:lengtht", &err), "a typo'd instance member must be rejected");
    CHECK(!validate_call("AddQuest", &err), "a bare function name must be rejected");
}

static void test_easing() {
    std::cout << "[rbsrc_test] Testing the shared interpolation curve...\n";

    CHECK(apply_easing(Easing::Linear, 0.5) == 0.5, "linear midpoint");
    CHECK(apply_easing(Easing::EaseIn, 0.5) < 0.5, "ease-in lags at the midpoint");
    CHECK(apply_easing(Easing::EaseOut, 0.5) > 0.5, "ease-out leads at the midpoint");
    CHECK(apply_easing(Easing::Linear, -1.0) == 0.0, "clamped below");
    CHECK(apply_easing(Easing::Linear, 2.0) == 1.0, "clamped above");

    for (int i = 0; i <= 10; ++i) {
        const double t = i / 10.0;
        for (Easing e : {Easing::Linear, Easing::EaseIn, Easing::EaseOut, Easing::EaseInOut}) {
            const double v = apply_easing(e, t);
            CHECK(v >= 0.0 && v <= 1.0, "every easing stays in [0,1]");
        }
    }
    CHECK(apply_easing(Easing::EaseIn, 0.0) == 0.0, "ease-in starts at 0");
    CHECK(apply_easing(Easing::EaseIn, 1.0) == 1.0, "ease-in ends at 1");
    CHECK(apply_easing(Easing::EaseOut, 0.0) == 0.0, "ease-out starts at 0");
    CHECK(apply_easing(Easing::EaseOut, 1.0) == 1.0, "ease-out ends at 1");
}

// ── the fire_spirit example from the design doc ─────────────────────────────

static const char* kFireSpirit = R"RBSRC(-- rbsrc version 1
-- rbsrc: source-object fire_spirit

archetype fire_spirit
  scaling 1
  model "firespray" as body
  glow  as halo { color #3b5a33, size 27 }

  timeline halo
    hook   OnLoad
    step   0.02
    track  scaling
      key  0.000  0.10  linear
      key  1.200  2.00  ease-out
    track  position
      key  0.000  (0, 0, 0)
      key  0.600  (0, 68, -95)
    marker 0.500  SoundLibrary.PlayEffect("enemy_die")

  on load(self)
    Program.Wait(2.0)
  end
)RBSRC";

static void test_parse_and_lower() {
    std::cout << "[rbsrc_test] Testing parse of the design-doc example...\n";

    const ParseResult pr = parse(kFireSpirit);
    CHECK(pr.ok(), "the example must parse without errors");
    if (!pr.ok()) std::cout << pr.errors();

    CHECK(pr.document.archetype == "fire_spirit", "archetype name");
    CHECK(pr.document.source_object == "fire_spirit", "provenance comment");
    CHECK(pr.document.structure.size() == 2, "two structure declarations");
    CHECK(pr.document.timelines.size() == 1, "one timeline");
    CHECK(pr.document.hooks.size() == 1, "one behaviour hook");
    CHECK(pr.document.hooks[0].event == "load", "hook event");
    CHECK(pr.document.hooks[0].params == "self", "hook params");
    CHECK(pr.document.hooks[0].body.find("Program.Wait(2.0)") != std::string::npos,
          "hook body is kept verbatim");

    const Timeline* tl = pr.document.find_timeline("halo");
    CHECK(tl != nullptr, "find_timeline");
    if (!tl) return;

    CHECK(tl->hook == "OnLoad", "hook binding");
    CHECK(tl->tracks.size() == 2, "two tracks");
    CHECK(tl->markers.size() == 1, "one marker");
    CHECK(tl->tracks[0].property == "scaling", "first track property");
    CHECK(tl->tracks[0].keys.size() == 2, "two keyframes");
    CHECK(tl->tracks[0].keys[1].easing == Easing::EaseOut, "easing parsed");
    CHECK(tl->tracks[1].is_vector(), "the position track is a vector track");
    CHECK(tl->tracks[1].keys[1].value.vector.y == 68.0, "vector component parsed");
    CHECK(std::abs(tl->duration() - 1.2) < 1e-9, "duration is the last key time");

    // ── lowering ────────────────────────────────────────────────────────────
    const LowerResult lr = lower(pr.document, *tl);
    CHECK(lr.ok(), "the example must lower cleanly");
    if (!lr.ok()) std::cout << lr.errors();

    const std::string& lua = lr.lua;

    // The chunk's arguments arrive through `...`, per the hook.
    CHECK(lua.find("local self = ...") != std::string::npos,
          "the generated chunk must take self from ...");
    CHECK(lua.find("SceneObject.setScaling(self, ") != std::string::npos,
          "scalar track lowers to the real setter");
    CHECK(lua.find("SceneObject.setPosition(self, Vector3.New(") != std::string::npos,
          "vector track lowers to Vector3.New");
    CHECK(lua.find("__ease_out(") != std::string::npos, "ease-out reaches the Lua");
    CHECK(lua.find("Program.Wait(0.02)") != std::string::npos, "tick wait");
    CHECK(lua.find("for __frame = 0, 60 do") != std::string::npos,
          "61 ticks over 1.2s at 0.02s");

    // Markers are edge-gated so they fire exactly once, and their arguments are
    // emitted verbatim — the corpus spells `self` out itself, so the emitter must
    // not inject a receiver.
    CHECK(count_of(lua, "SoundLibrary.PlayEffect(\"enemy_die\")") == 1,
          "the marker must appear exactly once, with its arguments untouched");
    CHECK(lua.find("if __t >= 0.5 and __t < 0.52 then") != std::string::npos,
          "the marker must be gated to a single tick");

    // No stray braces: the generated chunk should be balanced enough to compile.
    CHECK(count_of(lua, "for ") == count_of(lua, " do\n") ,
          "every loop opens with a matching do");
}

// ── blackhole: the wait-stride pattern ──────────────────────────────────────

static void test_blackhole_stride() {
    std::cout << "[rbsrc_test] Testing the blackhole growth loop (wait-stride)...\n";

    // 700 ticks of 0.0001s growth, waiting every 10th tick. Taking the stride
    // seriously is the difference between a 0.007s ramp and a 0.07s one.
    const std::string src = R"RBSRC(archetype blackhole
  timeline grow
    hook        Program
    step        0.0001
    wait-stride 10
    track       scaling
      key 0.0000  0.001  linear
      key 0.0700  40.00  linear
)RBSRC";

    const ParseResult pr = parse(src);
    CHECK(pr.ok(), "the blackhole timeline must parse");
    if (!pr.ok()) { std::cout << pr.errors(); return; }

    const Timeline* tl = pr.document.find_timeline("grow");
    CHECK(tl != nullptr, "find_timeline");
    if (!tl) return;

    CHECK(tl->wait_stride == 10, "wait-stride parsed");
    CHECK(std::abs(tl->step - 0.0001) < 1e-12, "step parsed");

    const LowerResult lr = lower(pr.document, *tl);
    CHECK(lr.ok(), "blackhole must lower");
    if (!lr.ok()) { std::cout << lr.errors(); return; }

    CHECK(lr.lua.find("if (__frame % 10) == 0 then Program.Wait(0.001) end") != std::string::npos,
          "the wait must be strided, not per-tick");

    // 0.07s / 0.0001s = 700 intervals -> 701 frames, waiting 70 times at 0.001s
    // == 0.07s of wall clock, i.e. the authored duration.
    CHECK(lr.lua.find("for __frame = 0, 700 do") != std::string::npos, "701 frames");
    const int waits = count_of(lr.lua, "Program.Wait(");
    CHECK(waits == 1, "one strided wait statement in the loop body");

    // A per-tick wait would be a 10x-too-fast ramp; make that impossible.
    CHECK(lr.lua.find("Program.Wait(0.0001)") == std::string::npos,
          "the un-strided tick wait must never be emitted");
}

// ── item1: the single action marker pattern ─────────────────────────────────

static void test_item1_marker_only() {
    std::cout << "[rbsrc_test] Testing the item1 one-shot marker...\n";

    const std::string src = R"RBSRC(archetype healingpotion
  timeline on_load
    hook   OnLoad
    step   1.0
    marker 0.0  Character.AddItem(self, "healingpotion", 1)
)RBSRC";

    const ParseResult pr = parse(src);
    CHECK(pr.ok(), "a marker-only timeline is valid (this is the item1 shape)");
    if (!pr.ok()) { std::cout << pr.errors(); return; }

    const Timeline* tl = pr.document.find_timeline("on_load");
    CHECK(tl != nullptr, "find_timeline");
    if (!tl) return;

    CHECK(tl->tracks.empty(), "no motion at all");
    CHECK(tl->markers.size() == 1, "one action marker");
    CHECK(tl->duration() == 0.0, "a marker at t=0 has zero duration");

    const LowerResult lr = lower(pr.document, *tl);
    CHECK(lr.ok(), "marker-only timeline must lower");
    if (!lr.ok()) { std::cout << lr.errors(); return; }

    // One frame, one call, no motion.
    CHECK(lr.lua.find("for __frame = 0, 0 do") != std::string::npos,
          "a zero-duration timeline is a single pass");
    CHECK(count_of(lr.lua, "Character.AddItem(self, \"healingpotion\", 1)") == 1,
          "the call must be emitted exactly once");
    CHECK(lr.lua.find("setScaling") == std::string::npos, "no motion setters");

    // A getter as a marker is a warning, not an error.
    const std::string bad = R"RBSRC(archetype x
  timeline t
    hook   OnLoad
    step   1.0
    marker 0.0  Health.CurrentHealth(self)
)RBSRC";
    const ParseResult bpr = parse(bad);
    CHECK(bpr.ok(), "a getter marker still parses");
    const LowerResult blr = lower(bpr.document, bpr.document.timelines[0]);
    CHECK(blr.ok(), "a non-mutating marker is a warning, not a failure");
    CHECK(!blr.diagnostics.empty(), "the getter should be reported");
}

static void test_lower_rejects_unknown_calls() {
    std::cout << "[rbsrc_test] Testing that lowering refuses invented API calls...\n";

    const std::string src = R"RBSRC(archetype x
  timeline t
    hook   OnLoad
    step   1.0
    marker 0.0  Player.AddXP(self, 100)
)RBSRC";
    const ParseResult pr = parse(src);
    CHECK(pr.ok(), "the text itself is well formed");
    const LowerResult lr = lower(pr.document, pr.document.timelines[0]);
    CHECK(!lr.ok(), "lowering must refuse a call the game does not publish");
    CHECK(lr.errors().find("Player.AddXP") != std::string::npos,
          "the error should name the invented call");
}

static void test_round_trip() {
    std::cout << "[rbsrc_test] Testing text round-trip...\n";

    const ParseResult first = parse(kFireSpirit);
    CHECK(first.ok(), "first parse");
    const std::string text = write(first.document);

    const ParseResult second = parse(text);
    CHECK(second.ok(), "the written form must parse back");
    if (!second.ok()) { std::cout << second.errors(); return; }

    // Writing is canonical: a second pass is a no-op, so a save never churns.
    CHECK(write(second.document) == text, "write(parse(write(x))) == write(x)");

    CHECK(second.document.timelines.size() == first.document.timelines.size(),
          "timelines survive the round trip");
    const Timeline* a = first.document.find_timeline("halo");
    const Timeline* b = second.document.find_timeline("halo");
    CHECK(a && b, "the timeline survives by name");
    if (a && b) {
        CHECK(a->tracks.size() == b->tracks.size(), "track count survives");
        CHECK(a->tracks[0].keys.size() == b->tracks[0].keys.size(), "key count survives");
        CHECK(a->markers.size() == b->markers.size(), "marker count survives");
        CHECK(std::abs(a->step - b->step) < 1e-12, "step survives");
        CHECK(a->tracks[1].keys[1].value == b->tracks[1].keys[1].value,
              "vector keyframes survive exactly");
    }

    // Lowering the re-parsed document must produce identical Lua.
    const LowerResult la = lower(first.document, first.document.timelines[0]);
    const LowerResult lb = lower(second.document, second.document.timelines[0]);
    CHECK(la.lua == lb.lua, "the same document must lower to the same Lua");
}

static void test_hook_slots() {
    std::cout << "[rbsrc_test] Testing hook slot ownership...\n";

    CHECK(hook_slots().size() >= 16, "the hook table should cover the observed slots");

    const HookSlot* collide = find_hook_slot("OnCollide");
    CHECK(collide != nullptr, "OnCollide must be a known slot");
    // The point of this table: a handler is owned by a component, not the object.
    CHECK(collide && collide->owner_class == "CollisionShapeComponent",
          "OnCollide belongs to the collision shape, not the object");

    const HookSlot* load = find_hook_slot("OnLoad");
    CHECK(load != nullptr, "OnLoad must be a known slot");
    CHECK(load && load->owner_class == "SceneObject", "OnLoad belongs to the object");

    CHECK(find_hook_slot("OnNonsense") == nullptr, "unknown hooks are not slots");
}

int main() {
    test_api_table();
    test_validator();
    test_easing();
    test_parse_and_lower();
    test_blackhole_stride();
    test_item1_marker_only();
    test_lower_rejects_unknown_calls();
    test_round_trip();
    test_hook_slots();

    std::cout << "[rbsrc_test] " << (g_checks - g_failures) << "/" << g_checks
              << " checks passed";
    if (g_failures > 0) {
        std::cout << " — " << g_failures << " FAILED\n";
        return 1;
    }
    std::cout << " — all good\n";
    return 0;
}

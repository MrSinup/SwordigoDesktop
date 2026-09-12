# Ultimate Swordigo — Comedy/Story Modding Cheat Sheet

> Working notes + concrete recipes for authoring a funnier, more comic Swordigo mod.
> All recipes below are **verified against the decoded vanilla corpus** (163 files:
> 47 `.scl` + 116 `.scene`) decoded with `bin/ruby_cli -d`.
> The mod is **pure resources + Lua** — the engine binary stays byte-identical vanilla.

---

## 0. Where everything lives

| Thing | Path |
|-------|------|
| Vanilla assets (source of truth) | `~/.local/share/swordigo-desktop/assets/resources/` (931 files) |
| **Demo instance (this mod)** | `~/.local/share/swordigo-desktop/inst-UltimateSwordigo/` |
| Instance assets (edit these) | `inst-UltimateSwordigo/resources/` |
| Instance wiring (SRE-hosted) | `engine/custom-UltimateSwordigo/arm64-v8a/instance.ini` (`dependencies = libsre.so`) |
| Working decode/encode area | `inst-UltimateSwordigo/_modwork/{de_in,de_out,re_in}` |
| Decoder/encoder | `/home/quantumcreeper/SwordigoDesktop/bin/ruby_cli` |

The engine binary (`libswordigo.so`, sha256 `f847814d…`) and `libsre.so` are copied
verbatim from `custom-Throndigo`. **Never edit the binary** — mod only `resources/`.

---

## 1. The decode → edit → encode loop

`ruby_cli` scans `de_in/` (decode) and `re_in/` (recode) **relative to the current
working directory**, writing to `de_out/` / `re_out/` with the same filenames.

```sh
RC=/home/quantumcreeper/SwordigoDesktop/bin/ruby_cli
cd ~/.local/share/swordigo-desktop/inst-UltimateSwordigo/_modwork

# DECODE (binary -> readable markup)
cp ../resources/town_part1.scene de_in/
"$RC" -d                       # -> de_out/town_part1.scene (markup)

# ... edit de_out/town_part1.scene ...

# ENCODE (markup -> binary), then drop back into the live instance
mkdir -p re_in re_out
cp de_out/town_part1.scene re_in/
"$RC" -r                       # -> re_out/town_part1.scene (binary)
cp re_out/town_part1.scene ../resources/town_part1.scene
```

- Round-trips are **byte-faithful** (verified by `filerift_smoke`): unknown protobuf
  fields, `Bounds`, `OnLoad`, and embedded Lua bytecode all survive.
- `--both` does decode+recode in one pass; `--decode-stdin` / `--recode-stdin -t <type>`
  are single-file pipe modes.

### IMPORTANT — Lua source vs. bytecode (VERIFIED WORKFLOW)
Each script (`Program` / `EntityAction` / `OnLoad`) component holds **both**:
- `String : $ ... $end`  → **plain-text Lua source** (this is what you edit)
- `Bytes : '\x1bLuaQ...'` → compiled Lua bytecode (a sibling line right after `$end`)

**This native `ruby_cli` (FileRift v5.8.6) does NOT accept `@compile` inside a `Program`
block** — it errors `tag '@compile' is not valid in Program`. Verified.

**The rule that works:** edit the `String` source, then **DELETE the sibling `Bytes : '...'`
line** for that component. On recode, FileRift recompiles fresh bytecode from your `String`.
If you leave the stale `Bytes` line, recode fails with a desync error like
`tag 's' is not valid in Program` (the changed source length breaks the following parse).

```
# BEFORE (vanilla, decoded):
String : $
local self = ...;
ShowTextBubbles("npcBubble", self:position(), { "old line" });
$end
Bytes : '\x1bLuaQ....(old compiled)....'      <-- DELETE this whole line after editing

# AFTER (edited, ready to recode):
String : $
local self = ...;
ShowTextBubbles("npcBubble", self:position(), { "New funny line!" });
$end
```

Then `ruby_cli -r` → `✔ recoded`. Re-decoding the result shows your new text; the
component's bytecode has been regenerated. Apostrophes inside **double-quoted** Lua
strings (`"It's fine"`) are safe; the recode breakage was the stale `Bytes`, not the text.

> Proven end-to-end on `town_part1.scene` (16 comic villager/quest lines): edit String →
> drop sibling Bytes on the 6 edited blocks → `ruby_cli -r` succeeded (153442 B) →
> re-decode confirms new lines, old lines gone → installed into the instance.

---

## 2. Corpus map — the comedy/story insertion points (verified counts)

| Surface | How it works | Where (top files) |
|---------|--------------|-------------------|
| **NPC dialogue** | `ShowTextBubble(...)` / `ShowTextBubbles(...)` Lua calls (31 files) | `florennum_shop`, `florennum_healerhouse`, `plains_house1`, `town_part1` (8), `town_herohouse` (7), `wasteland_town`, `plains_woodkeep3`, jail scenes |
| **Quest-log story text** | `AddQuestText(questName, textArray)` | quest/OnLoad scripts |
| **Scene intro/story cutscenes** | root **`OnLoad`** Lua (56 scenes) | `town_part1`, `town_herohouse` (6), `fire_partBoss`, `icecastle_*`, `plains_part2` |
| **Per-object behavior / events** | `Program` component (474 total, 102 files) | `worldsend_part9` (53), `monsters.scl` (26), jail/boss scenes |
| **Named moves / boss attacks (banter hooks)** | `EntityAction` (180 total) | `monsters.scl` (34), `icecastle_partBoss` (11), `dragonkin.scl` (10), `fire_partBoss` (5) |
| Intro / credits | `cinematic_beginning.scl`, `credits.scene` (4 EntityActions) | intro & ending |

Highest-leverage for "funnier/juicier" with lowest risk:
**dialogue (§3) → OnLoad cutscenes (§4) → comic triggers (§5) → boss banter (§6).**

---

## 3. Recipe: NPC dialogue (funniest, cheapest, safest)

Dialogue is a Lua call, not a static component. Vanilla shapes seen:

```lua
-- multi-line bubble sequence, positioned on an NPC found by name
ShowTextBubbles("npcBubble", Scene.Find("knight1"):position(), {
    "Halt! ...er, actually, carry on.",
    "I only know one line and that was it.",
});

-- single bubble anchored to the caller
ShowTextBubble("npcBubble", self:position(), "Ye seek the sword? It's, uh... around.");

-- shopkeeper / healer variants use their own bubble style + position()
ShowTextBubbles(shopkeeper:identifier(), { "Everything's 200% off. That means I pay YOU. ...wait." });
```

**To rewrite an NPC line:** decode the scene/scl → find the `ShowTextBubble(s)` call in a
`String : $ … $end` block → replace the string literals → add `@compile` → recode.

**To add a brand-new talking NPC:** clone an existing NPC object (a `Model` +
`SoundEffect` + a `Program`/trigger that calls `ShowTextBubbles`), give it a unique
`Identifier`, set `Position{X,Y}`, and write its bubble text. NPCs are composed objects —
there is no single "NPC component".

---

## 4. Recipe: scene-level OnLoad comic cutscene (root field 5)

56 scenes already run an `OnLoad`. Pattern for a self-contained gag intro:

```lua
local self = ...;
Game.SetCinematicMode(true, true);
Camera.FocusAtPoint(Scene.Find("statue"):position());
Program.Wait(1.0);
ShowTextBubbles("npcBubble", Scene.Find("statue"):position(), {
    "Behold the legendary hero...",
    "...who forgot where he parked his sword.",
});
Program.Wait(1.5);
Camera.ResetFocus();
Game.SetCinematicMode(false, false);
```

`Program.Wait(seconds)` is the coroutine sleep that paces every scripted sequence.
Add an OnLoad to a scene that lacks one by inserting a root-level `OnLoad{ String : $ … $end }`
(root field 5) — Ruby preserves/emits it losslessly.

---

## 5. Recipe: comic trigger event (walk-in gag)

1. An object with a `CollisionShape` (the zone).
2. A `Program` component with `Trigger : 1`, `Enabled : 1`.
3. Lua body starting `local self = ...;`.

```lua
local self = ...;
if not Properties.GetProperty(self, "gag_done") then
    Properties.SetProperty(self, "gag_done", true);
    SoundLibrary.PlayEffect("item_get");
    Camera.Rumble();
    ShowTextBubble("npcBubble", self:position(), "You found... absolutely nothing. Congrats!");
end
```

Use `Properties.Get/SetProperty` for one-shot / quest-flag state so gags don't repeat.

---

## 6. Recipe: boss banter

Bosses already run a `Program` state machine and fire moves via
`EntityController.PerformAction(self, <id>)` (ids map to `EntityAction` components).
Inject taunts between attacks — insert into the boss's existing loop:

```lua
if EntityController.IsIdle(self) and timeToAttack < 0 then
    ShowTextBubbles("npcBubble", self:position(), { "Is that all? My grandma hits harder." });
    EntityController.PerformAction(self, 113);
    timeToAttack = 5;
end
```

Non-destructive: you're adding a bubble call inside the existing loop, not changing hitboxes.

---

## 7. Useful Lua API (censused from shipped scripts)

- **Dialogue:** `ShowTextBubble`, `ShowTextBubbles`, `HideTextBubble`, `IsTextFinished`, `AddQuestText`
- **Camera:** `FocusAtPoint`, `FocusAtShape`, `FollowShape`, `ResetFocus`, `JumpToFocus`, `Rumble`, `IsPointVisible` (`Rectangle.New(x,y,w,h)` for constraint rects)
- **Flow:** `Program.Wait(s)`, `Program.Execute(self, id)`, `EntityController.PerformAction/IsIdle`
- **Game/UI:** `Game.SetCinematicMode(bool,bool)`, `Game.Flash()`, `Math.RandomInt(a,b)`
- **Audio:** `SoundLibrary.PlayEffect("name")`, `MusicPlayer.PlayMusic/FadeOut`
- **State:** `Properties.GetProperty/SetProperty(self, key[, val])`
- **Objects:** `Scene.Find("name")`, `Scene.CreateObject(template,name,parent)`, `self:position()/:identifier()/:setHidden/:destroy()`
- **Transform:** `TransformController.TranslateBy/ScaleTo/SetOrigin`, `ModelTransformController.SetRotationSpeed`
- **Physics/Health:** `PhysicsObject.SetGravity*/SetEnabled`, `Health.CurrentHealth/MaxHealth`

---

## 8. Testing the mod

```sh
cd /home/quantumcreeper/SwordigoDesktop
./bin/swordfare        # pick the "UltimateSwordigo" instance in the launcher
```

Verify the changed scene loads and the new dialogue/cutscene fires. The in-game Lua
console (toggle `N`) is available for live inspection.

---

## 9. Safety checklist

- [ ] Only files under `inst-UltimateSwordigo/resources/` were modified.
- [ ] For every edited Lua `String` block, the sibling `Bytes : '...'` line was DELETED (do NOT use `@compile`).
- [ ] Recode succeeded (`✔` in `ruby_cli -r` output) before copying back.
- [ ] A `.vanilla.bak` of each edited resource is kept in the instance; decoded backup in `_modwork/de_out/*.bak`.

---

## 10. Progress log

**Comic dialogue rewritten + installed (11 scenes, ~106 lines), each with a `.vanilla.bak`:**

| Scene | Comic lines | Notes |
|-------|-------------|-------|
| `town_part1.scene` | 16 + NPC logic | guard, villagers, master sword hand-off intro |
| `town_woods_end.scene` | 15 | dead-master discovery + magic tutorial, played for laughs |
| `town_healerhouse.scene` | 21 | wake-up + elder exposition |
| `town_elderhouse.scene` | 9 | the "edge of doom" briefing |
| `town_herohouse.scene` | 6 | bedroom / weird-dream intro |
| `plains_house1.scene` | 7 | the stolen-vase fetch quest |
| `snowy_part1.scene` | 9 | the gold-boulder miner |
| `florennum_shop.scene` | 3 + NPC logic | hidden shopkeeper |
| `florennum_healerhouse.scene` | 3 | healer |
| `plains_woodkeep3.scene` | 5 | mid-game villain monologue |
| `florennum_jail_part1.scene` | 12 | imprisoned-king / shard quest |

**Funny NPC logic added (real script changes, recode-verified):**
- `town_part1` ambient villager: `self["texts"]` swapped for a **10-line random comic pool**
  (`Math.RandomInt` picks a new self-aware gag each bump).
- `florennum_shop` keeper: greeting else-branch turned into a **5-line random pool**
  (`local lines = {...}; text = lines[Math.RandomInt(1,#lines)]`).

**Automation:** reusable drivers in `inst-UltimateSwordigo/_modwork/tools/`
(`comic_edits.json`, `apply_comic.py`, `install_comic.py`, `npc_logic.py`, `shop_logic.py`) —
each does decode → replace → drop sibling `Bytes` → `ruby_cli -r` → verify → install.

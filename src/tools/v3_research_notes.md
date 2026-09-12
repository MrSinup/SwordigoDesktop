# Scene Generator v3 — Vanilla Research Notes (evidence-based)

All values below were extracted directly from the decoded vanilla scenes in
`Scener/data/decoded_scenes/` (readable FileRift text dumps). No values are
guessed; anything not found is marked `UNKNOWN`.

Method: `grep`/`awk` over `Object{...}` blocks for `TextureName`,
`BackgroundComponent`, `LightComponent`, `Depth`, `Position`, `TemplateName`,
`Radius`. Two scenes analysed per biome.

## Format facts (all biomes)
- Background is a real object with `ClassName : 'Background'` → `BackgroundComponent.TextureName`, sitting at `Depth : ~1.72` in every sampled scene (the parallax bg plane).
- A ground platform = `GroundMesh` object carrying **two** `TextureMapping` entries: **tex1 = TOP surface**, **tex2 = FRONT/side cliff**. These are frequently DIFFERENT textures — this is the pairing v1/v2 got wrong.
- Decorations/props/entities are `TemplateName`-reference objects with their own `Depth` (Z layer). Real scenes push distant design elements to large negative Depth (e.g. grove min −1971, icecastle −1236, forest −136) while playable content sits near Depth 0–40.
- DirectionalLight = triple Light components: Type 2 (key), Type 1 (ambient), Type 4 (shadow).

## Per-biome extracted data

### Grasslands (grass_part1, plains_part1)
- Background: `grassbg_night` (grass_part1), `grasslandsbackground_day` (plains_part1). BG Depth 1.72.
- Ground TOP: `grass_grass` / `grass_subtle`; FRONT: `maybegood` (dominant front/rock face). tex1=`maybegood`(front) paired w/ tex2 top grass — note grass uses `maybegood` as the cliff.
- Depth range: min −90 … med 0 … max 190.
- Deco: `bush`(25), `pot`(9), `platformwood0/1`, `point_150_15`/`point_250_15`/`point_300_15` (moving platforms), `torch`, `sign_right`. Entities: `bat_blue_strike`, `bandit`, `varibandit_thrower`, `Stag Beetle`.
- Light: key intensity 1.2, ambient 0.3, white. Torch Radius 350.

### Forest (forest_part1, forest_part2)
- Background: `forest_background`. BG Depth 1.72.
- Ground TOP: `forest_grass`; FRONT: `forest_ground`. (tex1 forest_grass / tex2 forest_ground on the grassy edge; interior meshes both forest_ground.)
- Depth range: min −136 … med 0 … max 260.
- Deco: `bush`(28), `point_250_15`, `torch`(8), `carniplant`, `stonepile`. Entities: `rolling_spirit`, `piikkikonna`, `dwarf`, `chest_right`. Props: `portal`, `sign_left/right`.
- Light: key intensity 3.0, ambient 0.3, white.

### Grove (grove_part1, grove_part2)
- Background: `grove_bg`. BG Depth 1.72.
- Ground TOP: `forest_ground`; FRONT: `forest_grass` (grove reuses forest textures, plus `grove_ground`/`grove_grass`). Note top/front are swapped vs forest.
- Depth range: min −1971 (deep bg elements) … med 15 … max 220.
- Deco: `grove_torch`(17), `bush`(16), `grove_pole2`, `grove_gate`, `shadowblob_fire_little`. Entities: `rolling_spirit`, `forest_spirit`, `piikkikonna`.
- Light: key intensity 1.0, ambient 0.2 (darker). Torch Radius 350 (+ one 250).

### Wasteland (wasteland_part1, wasteland_part2)
- Background: `wasteland_bg`. BG Depth 1.72.
- Ground TOP: `wasteland_ground`; FRONT: `grass_orange`. (`graveyard256` used in part2 for ruins.)
- Depth range: min −95 … med 38 … max 428.
- Deco: `point_250_15`, `shadowblob_fire_little`, `piikkipuska`, `torch`(4). Entities: `beetle_wasteland`, `grasswalker_wasteland`, `variknight_weapon_sword`.
- Light: key intensity 3.0, ambient 0.5. Torch Radius 180.

### IceCastle (icecastle_part1, snowy_part1)
- Background: `atlon` (icecastle), snowy uses `atlon`/snow textures. BG Depth 1.72.
- Ground TOP: `icecastle_ground`; FRONT: `icicle`. (snowy: `snowy_snow` top, `atlon_ground`/`grove_wood` structural.) icecastle_floor for flat floors.
- Depth range: min −1236 (deep bg) … med 10 … max 180.
- Deco: `ice_300_15` (ice moving platforms, 29), `dropping_icicle`, `pushingblock`, `castle_lock/lockdoor`. Entities: `skeleton_spell_cast_frost`.
- Light: key intensity 2.0, ambient 0.2, white.

### Cave (thecave_part1, florennum_cave1)
- Background: `cavesbackground2`. BG Depth 1.72.
- Ground TOP/FRONT: `wasteland_ground` + `wasteland_ground2` (thecave); `florennum_ground` + `cavewalls` + `brown128` (florennum_cave). Cave meshes often use the SAME texture top & front (enclosed rock), unlike open biomes.
- Depth range: thecave min −407 … med 30 … max 702 (tall vertical shafts). florennum_cave has water + `particle_fure` fire particles.
- Deco: `point_250_15`/`point_150_15` (platforms), `torch`(3), `portal`, `chest_right`. Entities: `dire_cavelurker`, `beetle_wasteland`.
- Cave structure: enclosed — ground meshes form both floor and ceiling/walls (top==front texture), tall Depth spread (shafts), sparse torches for lighting, dark bg. NOT a simple hollow.
- Light: key intensity 2.0, ambient 0.3.

### Fire (fire_part1, fire_part2)
- Background: fire bg via `graveyard_ground`-toned scene; `water`+`cauldron` present, `particle_fure_1/2/3` fire particles. BG Depth 1.72.
- Ground TOP: `fire_grass`; FRONT: `graveyard_ground`. (Interior meshes both graveyard_ground.)
- Depth range: min −453 … med 26 … max 813 (very tall vertical fire towers).
- Deco: `fire_150_13`(34, fire moving platforms), `fire_300_15`, `fireball_launcher`(5), `stonepillairs`/3/4. Entities: `magmamonster`.
- Light: key intensity 3.0, ambient 0.4.

### Florennum (florennum_part1, florennum_tower1)
- Background: `florennum_night_bg` (town), `blackbg` (tower interior). BG Depth 1.72.
- Ground TOP/FRONT: `florennum_ground` (town, top≈front stone); tower uses `housetiles_gray` top, `gray_subtle`/`crypt_tiles` structural.
- Depth range: town min −289 … med 0 … max 428.
- Deco: `bush`(5), `torch`(4), `grove_tree1/3`, `florennum_house2/3`, `florennum_tower`, `florennum_housedoor(_open)`, `signpost_blank`. Props: `portal`.
- Light: key intensity 1.2, ambient 0.3, warm town.

## v1/v2 INCONSISTENCIES FOUND (fixed in v3)
1. **Wrong top vs front pairing.** Real forest = top `forest_grass` + front `forest_ground`; fire = top `fire_grass` + front `graveyard_ground`; wasteland = top `wasteland_ground` + front `grass_orange`; icecastle = top `icecastle_ground` + front `icicle`. v1/v2 tables mixed these up / used a single texture for both faces.
2. **Biome-agnostic sameness.** Real biomes use distinct texture sets AND distinct deco palettes (grove_torch/grove_pole vs fire_150_13/fireball_launcher vs ice_300_15/dropping_icicle). v1/v2 reused the same deco/platform layout regardless of biome.
3. **Backgrounds too close / no depth design.** Real scenes place distant design elements at large negative Depth (−136 to −1971) with the bg plane at ~1.72, giving true parallax depth. v1/v2 kept background silhouettes only a few units behind play — no deep design layer.
4. **Generic "cavey" caves.** Real caves are enclosed (same texture top & front, tall Depth shafts −400..+700, sparse torch lighting, dark cavesbackground2, water pools) — not an open platform strip with a dark tint.

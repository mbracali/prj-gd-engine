# GH1 (PS2) character and guitar selection panels: reverse-engineering notes

Sources: the decomp in `analysis/decomp/*.c`, `analysis/strings.tsv`, `analysis/vtables.tsv`, the scripts in `extracted_dta/ghui/gen/{career,multiplayer,endgame,main,config}.dta`, `extracted_dta/config/gen/{guitars,store}.dta` and `extracted_dta/charsys/gen/*.dta`, and the scene files `extracted/ghui/*.gh`, decoded with `tools/ui_panels.py` and `tools/rnd_classes`.

- **[C]** means confirmed from code or data. **[I]** means inferred.
- Addresses are EE virtual addresses.
- String literals were read straight from the ELF (`original_files/SLUS_212.24`).

## 0. Conventions and the shared base class

- The vtable layout is `+0` 0, `+4` typeinfo, `+8` 0, then 8-byte entries starting at `+0xc` (`{short this_adj; short pad; fnptr}`).
  So a call through `vtbl+0x44` is slot 7 and `vtbl+0x5c` is slot 10. **[C]**
- UIPanel slots, all **[C]**:

  | slot | address | meaning |
  |---|---|---|
  | 0 | – | dtor |
  | 1 | – | Handle(msg) |
  | 3 | 0x206b90 | Enter |
  | 4 | 0x206e38 | Exit |
  | 5 / 6 | 0x206a40 / 0x206ac0 | "busy" queries over the components (the screen waits on them during a transition) **[I name]** |
  | 7 | 0x207498 | SetPaused: writes `panel+0x50` |
  | 10 | 0x2070b0 | Poll |
  | 11 | 0x207300 | Draw: draws the panel RndDir at `+0x54` through FUN_002ec850 |
  | 16 | 0x2064d8 | FinishLoad: sends `finish_load` and sets state=2 |

- The Ghidra names `ArenaPanel_v3/v4/v16`, `GamePanel_v6/v11`, `GHPanel_v10` and `StatusPanel_v1` are really the **UIPanel base** implementations, shared by every panel class.
- `set_paused B` (UIPanel handler 0x207748 → FUN_00207d30) calls slot 7, which stores B at `+0x50`. **[C]**
  - UIScreen Poll (0x2085f0) calls a panel's Poll only when `+0x50 == 0`. **[C]**
  - So a paused panel is still drawn, but it is not polled: no animation and no load scheduling.
- Message routing on screens, UIScreen_v1 at 0x208240 **[C]**:
  1. The screen's script handler.
  2. Sinks.
  3. The **focus panel's** Handle (slot 1).
  - Consequence: `{$this set_animating 0 TRUE}` and `{$this animating 0}` in the sel_guitar / multi_sel_guitar *screen* scripts have no screen handler. They fall through to the focus panel, which is `input_guitar` / `multi_input_guitar`, so the **GuitarSelectPanel** handles them. **[C]**
- Message creators:
  - `GH_SELECT_MSG` is FUN_00130958(component, player, screen, button). **[C]**
  - `SCROLL_MSG` is FUN_0012fe90(component, player, screen, direction, done). **[C]**
  - `component` is FUN_0020a798(ui), i.e. the focus panel's `+0x78`, its focused component, or `''` when there is none. **[I]**
  - `screen` is DAT_00369334 (the current screen) **[I]**.
  - Both messages are dispatched to the UIManager (`UIManager_t1_v1(0x3692d0, msg)`), so they go through the normal screen-then-panel chain.
- Button predicates: FUN_0011dbe8 = `is_select_button`, FUN_0011dbb0 = `is_back_button`, FUN_0011dc40 = `is_any_button`. **[C]**
  - select: guitar → R2(1); joypad → X(6) or 8.
  - back: guitar → Circle(5); joypad → Triangle(4).
  - DUp is 12 and DDown is 14.
- Player configs: GameConfig `DAT_002edb88`, `PlayerConfig(i) = *(cfg+8) + i*0x2c` (FUN_001137a8). **[C]**
  - PlayerConfig `+8` = character (set by FUN_00117e50 `set_character`, which also sets the embedded provider's character) **[C]**.
  - `+0xc` = guitar (FUN_00117d98), `+0x10` = guitar skin (FUN_00117df8). **[C]**
  - `+0x14` = an embedded **SelectGuitarProvider** (FUN_00117df0 returns `cfg+0x14`). **[C]**
- Campaign unlock test: FUN_00137b28(campaign `DAT_002edbe8`, sym, difficulty) returns TRUE if `campaign+0x34` (unlock-all) is set, or if the item is unlocked in that difficulty's state. **[C]**
  - FUN_00137b00(campaign, sym) is the same test at the **current** difficulty (`GameConfig+0x90`). **[C]**

---------------------------------------------------------------------------------------------------
## 1. CharsysPanel (`new_charsys_panel`), instances `char_single`, `char_multi`, `char_store`

Registration: FUN_0013fd40. It registers the verb `new_charsys_panel` → factory FUN_0013fdf8 (size 0xd4) → ctor FUN_0013fe48. **[C]**

It also caches `DAT_00343bc0 = (ghui components bg char_states)` from `ghui/config.dta`. The first argument is the config root, then `"components"`, `"bg"`, `"char_states"`. **[C]**

### 1.1 Config data, `ghui/config.dta` (the `ghui` config, file `ghui/gen/config.dtb`) **[C]**

```
(components (bg (char_states
   (none)
   (singleplayer (chars (char (index 0)(view "char_single.view")(placer "char_single.placer"))))
   (multiplayer  (chars (char (index 0)(view "char_multi.view")(placer "char_multi0.placer"))
                        (char (index 1)(view "char_multi.view")(placer "char_multi1.placer"))))
   (store        (chars (char (index 0)(view "char_store.view")(placer "char_store.placer")))))))
```

- Panel props:
  - `char_config` is one of `singleplayer`, `multiplayer` or `store` (stored at `+0xa0`).
  - `load_order` is the character list (`+0xbc`). **[C]** ctor 0x13fe48.
- Script instances:
  - `char_single`: career.dta, `(char_config singleplayer)`, load_order `metal classic alterna hair_metal punk nu_metal hiphop grim`.
  - `char_multi`: multiplayer.dta, `multiplayer`, same order.
  - `char_store`: career.dta, `store`, order `hair_metal grim`.
  - None sets `rnd_file`, so the scenes are `ghui/char_single.gh`, `ghui/char_multi.gh` and `ghui/char_store.gh`. **[C]**

### 1.2 Object layout (ctor 0x13fe48) **[C]**

| offset | contents |
|---|---|
| `+0x80` | current MetaCharState* (0 = none) |
| `+0x84..+0x90` | vector<MetaCharState*>, one per `char_states` entry (built in FinishLoad) |
| `+0x94` | last poll time in seconds (−1 on Enter) |
| `+0x98` | 1, the "animate characters" flag; never changed |
| `+0xa0` | char_config symbol |
| `+0xa4..` | vector of **2 slots** (FUN_0013f5d0, 0x60 bytes each): `slot+0` = character symbol, `+0x10..+0x4c` = a world transform (identity at construction), `+0x50` = scroll direction ±1 (init 1), `+0x54` = panel |
| `+0xb4` | **show flag** (init 1) |
| `+0xb8` | a private object created with `FUN_00236a10(DAT_003688a8)` and dyncast to the same type as `char_store.env`, i.e. a spare **RndEnviron** used as a backup **[I]** |
| `+0xbc` | the `load_order` array |
| `+0xc0..+0xc4` | vector of CacheModel (0x14 bytes) per load_order entry: `+0` = char symbol, `+4` = Character*, `+8` = priority, `+0xc` = last-use time (float, from the EE `Count` register), `+0x10` = loaded flag |
| `+0xd0` | "polled at least once since Enter" (gates Draw) |

MetaCharState (FUN_0014bc20 ctor, FUN_0014bcf0 resolve) **[C]**:
- name, the config array, and a vector of `{index, RndView* view, RndPlacer* placer, panel}`.
- The view and placer are looked up as objects in the `ui` namespace when the panel enters (FUN_0014bcf0).

### 1.3 Messages, handler CharsysPanel_v1 at 0x141488

| msg | args | behaviour | returns |
|---|---|---|---|
| `show_char` | `i sym` | FUN_00141208 → FUN_0013f7a8(slot[i], sym): if sym ≠ slot.char, set `slot.dir` = FUN_00140c78(old,new), then `slot.char=sym`, then FUN_00140ab0 re-prioritises the model cache. No index bounds check (only slots 0 and 1 exist). **[C]** | 0 |
| `are_chars_loaded` | – | FUN_00140f28: TRUE if no state is active, otherwise **only slot 0's** model loaded flag. **[C]** | bool |
| `is_char_loaded` | `i` | FUN_00140f58: TRUE if no state or i ≥ 2, otherwise slot[i]'s loaded flag. **[C]** | bool |
| `char_store_lights_on` | `B` | FUN_00141238. Only acts when char_config == `store` **[C]**. B=TRUE: `ui::char_store.env` copies itself back from the `+0xb8` backup (vfunc slot 7, the Object::Copy-like method) **[I name]**. B=FALSE: removes every light from `char_store.env` and zeroes its ambient colour (env+0x50..0x5c) **[C]**, so the character renders as a black silhouette **[I visual]**. | 0 |
| `hide` | `B` | FUN_00141438: `+0xb4 = !B`. **[C]** | 0 |
| others | | UIPanel handler 0x207748 (`is_up`, `set_paused`, `load`, `unload`, `set_focus`), then the script. **[C]** | |

- Direction rule, FUN_00140c78 **[C]**: with N = load_order size and d = (idxNew − idxOld) mod N, the direction is +1 if 2d < N, else −1. It is +1 when either character is not in the list.

### 1.4 Lifecycle

- **FinishLoad** (slot 16, 0x1406c8) **[C]**:
  - Builds a MetaCharState for each `char_states` child.
  - Sets the current state to `none`.
  - If the config is `store`, backs up `ui::char_store.env` into the `+0xb8` env (`b8->vfunc7(char_store.env, 0)`) **[I: copy semantics]**.
  - Then calls the base.
- **Enter** (slot 3, 0x1404a8) **[C]**:
  1. Base Enter, which runs the script `enter`. The scripts do `hide TRUE`, `show_char 0 {game get_character}` and, for char_multi, `show_char 1 {player2 get_character}`.
  2. Resolve the views and placers of the `char_config` state.
  3. `+0xd0=0`, `+0x94=-1`.
  4. FUN_001409d8: switch state none → config.
     For each char of the new state, FUN_0014bbf0 → FUN_0013f838 copies the **placer's world transform** (placer+0x50 → +0x20, 64 bytes) into `slot[index].xfm`, and applies it to the model if it is already loaded (vfunc slot 7 = SetWorldXfm **[I]**).
  5. FUN_00140ab0 re-prioritises.
- **Exit** (slot 4, 0x140500) **[C]**:
  - Current state = 0; all MetaCharStates are destroyed (they are rebuilt on the next FinishLoad).
  - Every slot's char is set to `''`; the `+0x7c` object is released; tasks are cleared (FUN_00242840(+0x10)); then the base Exit.
- **Busy query** (slot 6, 0x1405d8) **[C]**: while the panel is down (state 2), it reports busy as long as any model is loaded or loading. So screen transitions wait for the panel to unload its models.
- **Poll** (slot 10, 0x1403b0) **[C]**:
  - If state==2 (down): free one loaded or loading model per call (FUN_0013f948), then return.
  - Otherwise:
    1. Base Poll.
    2. Load scheduler FUN_00140fb0.
    3. `dt = now_s − last` (the clock is DAT_00369304 ms × 0.001).
    4. If a state is active and shown: set `+0xd0=1` and run per-char poll FUN_0014c128 → FUN_0013f6d8 for each char of the state.
       That re-applies `slot.xfm` to the model (vfunc slot 7), then FUN_001817c0(0,0,dt,model), the Character poll that advances its AnimDriver in real time **[C call / I name]**.
- **Draw** (slot 11, 0x140660) **[C]**:
  1. If not down, a state is active, shown (`+0xb4`) and polled once (`+0xd0`): FUN_0014c0a8 → for each state char, FUN_0014baf0. That function, only if the model is loaded:
     - resets the view's transform (vfunc on view+0x70, args 0,0,0);
     - draws the **view** (`char_single.view` = `char_single.env` + `char_single.cam` + placer);
     - draws the Character (model vfunc slot 6 = Draw).
  2. Base Draw: the panel RndDir.

So each character is drawn with its own camera and environment from the state's view. **[C]**

### 1.5 Model cache and preloading (the "what is loaded when")

The cache holds one entry per `load_order` entry and **at most 6 loaded models**. **[C]**

Priorities, FUN_00140ab0 **[C]**:
1. Every entry gets priority N.
2. For each slot of the current state whose char is in load_order:
   - Its own entry gets priority 0.
   - One step **against** `slot.dir` gets min(…, 2).
   - Then 4 steps **along** `slot.dir` get 1, 2, 3, 4.
3. Stepping (FUN_00140d50) wraps modulo N. Unless the config is `store`, it **skips characters that `campaign` does not report unlocked at the current difficulty** (FUN_00137b00). **[C]**

Scheduler, FUN_00140fb0, runs every poll **[C]**:
- If any entry is loading, keep polling its load (FUN_0013fa90) and stop there. Only one load is in flight at a time.
- Otherwise pick the best unloaded entry and the worst loaded entry:
  - "better" = lower priority, or equal priority with a more recent use (FUN_0013f9a8).
  - If best.priority < N: start loading it if fewer than 6 are loaded.
  - Otherwise, if best is better than worst, unload worst and load best.

Load, FUN_0013fa90 **[C]**:
1. `new Character(name, (charsys archetypes <name>))` via FUN_0018d260 (an AnimInstance-tagged allocation of 0xf0 bytes).
2. Set the property **`meta` = 1** on it (FUN_00237348("meta", 1)), then start the async load (vfunc +0x24).
3. When IsLoaded (vfunc +0x2c) returns true:
   - mark it loaded and call vfuncs +0x6c and +0x34 (post-load / enter) **[I]**;
   - set the character's shadow light to **`ui::<panelname>_shadow.lit`**, i.e. `char_single_shadow.lit`, `char_multi_shadow.lit` or `char_store_shadow.lit` (sub-object +0xb0 vfunc);
   - write the globals `DAT_00368e40..4c = (0, −1, 0, 8)`, likely a shadow plane or direction for the menu **[I]**.

### 1.6 What the characters do in the menu **[C]** (charsys.dta / charbase.dta / *_anims.dta)

- Because `meta` is set, `charbase.dta` skips the in-game setup (hand drivers, IK servos), calls `my_preload`, and sets `main.drv` to `set_load_any -1 0` then `set_load_any kGuitarUI 1`.
- `my_preload` (each archetype in charsys.dta):
  - `{$this show_shadow "charsys/<c>/<c>_shadow.rnd"}`;
  - if `meta`, `"main.drv" set_anims {find_or_load <C>_UI_ANIMS}`.
- `<C>_UI_ANIMS` contains a single clip, `<c>_idle_ui`, with `(flags kGuitarUI)`, from `charsys/<c>/anims/editor`:
  - clip names: `metal_idle_ui`, `classic_idle_ui`, `alterna_idle_ui`, `hair_idle_ui`, `punk_idle_ui`, `nu_idle_ui`, `hiphop_idle_ui`, `grim_idle_ui`;
  - graph `HERO_UI_GRAPH` = `(play_flags kAnimRealTime)(blend_width 1)`, so it loops in real time;
  - channels `HERO_BASE_CHANNELS`.
- The guitar the menu character holds comes from the archetype outfit's GUITARS category. Nothing in CharsysPanel sets it, so it is the category default **[I: likely the first entry, `guitar_sg`; not traced in OutfitState]**.

### 1.7 Scenes **[C]**, decoded from the .gh files

Coordinates are Z-up and the camera looks along +Y.

| panel | placer world pos (rotation) | camera | env |
|---|---|---|---|
| `char_single.gh` | `char_single.placer` (−49.94, −10, −76), rotated 180° about Z | `char_single.cam` at (−41.81, −135.27, −27.81), identity, fov 0.785, near 90, far 300 | `char_single.env`: ambient (0.224, 0.008, 0.161); lights direct01 (0.988, 1, 0.922), direct02 (0.89, 0.616, 0.51), direct03 (0.012, 0.333, 0.478) |
| `char_multi.gh` | `char_multi0.placer` (−75, 0, −76) and `char_multi1.placer` (−8, 0, −76), both 180° about Z | `char_multi.cam`, same as single | same lights and ambient |
| `char_store.gh` | `char_store.placer` (−68, −50, −72), 180° about Z | `char_store.cam` at (−41.81, **−200**, −27.81) | `char_store.env` + char_store01..03, same colours |

- `*_shadow.lit` is a near-vertical directional light used only for the drop shadow.
- Each `*.view` draws `env`, `cam` and the placer(s).

---------------------------------------------------------------------------------------------------
## 2. GuitarDisplayPanel (`new_guitar_display_panel`)

Registration: FUN_001443c8 → factory FUN_00144400 (0xe0 bytes) → ctor FUN_00143e50. **[C]**

### 2.1 Props and modes (ctor) **[C]**

`(config X)` selects the mode:

| config | mode `+0x80` | slots `+0xd4` | name used in namespaces | attach mesh `+0xdc` |
|---|---|---|---|---|
| (none/other) | 0 single | 1 | `single` | `bone_pos_guitar.mesh` |
| `multiplayer` | 1 | **2** | `multi` | `bone_pos_guitar.mesh` |
| `store` | 2 | 1 | `store` | `bone_pos_guitar.mesh` |
| `endgame` | 3 | 1 | `endgame` | `guitar.mesh` |

- Per slot i there is a namespace `ui_guitar_load_<name>_<i>` (FUN_0023e078), plus a loader (`+0x94`), a top view (`+0xa4`), the current skin entry (`+0xb4`) and the spin anim (`+0xc4`).
- `+0xd8` = show flag (init 1).

Instances:
- `guitar_display` (career): no config → mode single; default rnd `ghui/guitar_display.gh`; `(force_exit TRUE)`.
- `multi_guitar_display` (multiplayer): config multiplayer, `force_exit TRUE`.
- `store_guitar_display`: config store.
- `endgame_guitar_display`: config endgame (win_game screen).

All of them use `ghui/guitar_display.gh`.

### 2.2 Messages, GuitarDisplayPanel_v1 at 0x145718

| msg | args | behaviour | returns |
|---|---|---|---|
| `show_guitar` | `i guitar skin` | FUN_001447b8, described below | 0 |
| `hide` | `B` | `+0xd8 = !B` **[C]** | 0 |
| others | | UIPanel base, then the script | |

`show_guitar i guitar skin`, FUN_001447b8 **[C]**:
1. Ignored if `guitar == ''`, or if there is one slot and i ≥ 1.
2. `cfg = (guitars <guitar>)` from `config/guitars.dta`. If `skin == ''`, skin = the **first** entry of `(skins …)`.
3. `entry = (skins <skin>)`. If it equals the slot's current entry, nothing happens.
4. Otherwise:
   1. Look up the slot's spin anim (FUN_00144cc8) and **reset it to frame 0**: FUN_001abab8(anim, 0, 1.0), RndAnimatable::SetFrame **[I name]**.
   2. Unload the slot (FUN_00144450).
   3. Store the entry.
   4. `outfit = (skins <skin> outfit)`, a kGuitarXXX int.
   5. `char = PlayerConfig(i).character`.
   6. `file = (charsys archetypes <char> outfit category guitars)[outfit+1] (load …)`.
   7. `dir = (charsys archetypes <char> outfit directory)`.
   8. `path = "%s/%s.rnd"`, e.g. `charsys/metal/../guitars/guitar_sg.rnd`, which resolves to `charsys/guitars/gen/guitar_sg.rnd_ps2`.
   9. Start an async RndLoader into the slot namespace (FUN_001bb100).
- The guitars in `config/guitars.dta` are `gibson_sg`, `gibson_lespaul`, `gibson_flying_v`, `gibson_es335`, `gibson_xplorer`, `gibson_firebird`, `gibson_lespaul_special`, `gibson_corvus`, `gibson_moderne`, `gibson_eds`, `battleaxe`, `grim_guitar` and `guitar_red_octane`.
- Each one is `(skins (<skin> (outfit kGuitarX)(mat <material>)) …)`.

### 2.3 Load completion, positioning, skin material

**Poll** (0x144670) **[C]**: base Poll; then, if shown, for each slot:
- If the view is not set yet and the loader is done (FUN_001bb400): finalize with FUN_00144f78.
- If the view is set: **spin**.
  - `f = anim.frame + 0.5`. The limit is 240, or 100 in endgame. If f > limit, f −= limit. Then SetFrame(f).
  - So the spin anim advances **0.5 frames per poll**.

FUN_00144f78, the finalize step **[C]**:
- `slot.view = <ns>::top.view` (the guitar file's top view), and its local transform is set to identity.
- The attach mesh `<ns>::bone_pos_guitar.mesh` (or `guitar.mesh` in endgame) is linked to `top.view` through FUN_001da100 **[I: parent/constraint]**. The top view is parented to `ui::guitar_display.view`.
- The attach mesh's local transform = the **placer's world transform** (`guitar_single.placer`, `guitar_multi<i>.placer`, `guitar_store.placer` or `guitar_axe.placer`, from FUN_00144b68).
- Then FUN_00145218 applies the skin: in the slot namespace, `guitar.mesh` → SetMat(`<(mat X)>.mat`), when that material exists. **[C]**

**Draw** (0x1445a8) **[C]**:
1. Base Draw (the panel dir: `guitar_display.view` with `guitar_setup.cam`, `guitar_setup.env` and the sub-views, whose placers draw nothing **[I]**).
2. If shown, for each slot with a loaded view, FUN_00144e28 re-copies the placer's world transform to the attach mesh. This picks up the animated rotation every frame.
3. Then it draws the slot's `top.view`.

**Enter/Exit** (0x1444d0 / 0x144520) **[C]**:
- Endgame only: Enter swaps `guitar_setup.cam` for `guitar_setup_endgame.cam` in `guitar_display.view`'s draw list; Exit swaps it back.
- Exit also unloads every slot. This matters because of `force_exit TRUE`: the guitar is reloaded on every screen entry.

### 2.4 `ghui/guitar_display.gh` contents **[C]**

`guitar_display.view` draws:
- `guitar_setup.cam`: world (6.58, −216, 0), identity, fov 0.785, near 20, far 1000.
- `guitar_setup.env`: ambient black; lights `guitar01.lit` (0.392, 0.639, 0.941), `guitar02.lit` (0.271, 0.227, 0.341), `guitar03.lit` (0.678, 0.69, 0.624).
- The sub-views `guitar_single.view`, `guitar_p1.view`, `guitar_p2.view`, `guitar_store.view` and `guitar_axe.view`.

The view's anim list holds all the TransAnims.

| object | target | keys |
|---|---|---|
| `guitar_single.placer` | – | world pos (16, −162, −3); `guitar_single.view` at (16, −162, −58) |
| `guitar_single.tnm` (spin) | `guitar_single.view` | rotation keys at 0/80/160/240: quats (0,0,0,1), (0,0,−.866,.5), (0,0,−.866,−.5), (0,0,0,1), a full turn about Z. Filters: ScaleOffset(scale 2), MinMaxLoop(0..240, loop) |
| `guitar_transition.tnm` | `guitar_single.view` translation | frame 0 (20, −121, −68) → frame 10 (16, −162, −58), spline. This is the **skin zoom**; the camera is at y=−216, so frame 10 is closer. |
| `guitar_p1.tnm` / `guitar_p2.tnm` (spin) | `guitar_multi0.placer` / `guitar_multi1.placer` | keys 0/80/160/240 with a 20° tilt; same filters |
| `guitar_transition_p0.tnm` / `_p1.tnm` | `guitar_p1.view` / `guitar_p2.view` | p0: (−13.6, −120, −73) → (−8.5, −150, −62); p1: (26.6, −120, −73) → (20.7, −150, −62), frames 0..10 |
| `guitar_store.tnm` | `guitar_store.placer` | 5 keys over 0..240, same filters |
| `guitar_axe.tnm` | `guitar_axe.placer` | 5 keys over 0..100, MinMaxLoop(0..100) only |

- Placers: `guitar_multi0` (−8.5, −150, −7), `guitar_multi1` (20.7, −150, −7), `guitar_store` (−33, 0, −30), `guitar_axe` (−16.2, 18.1, 9.4). Each carries its own non-uniform rotation and scale; see the decoded matrices in the report data.
- Spin speed:
  - The poll adds 0.5 and ScaleOffset doubles it, so the key frame advances 1 per poll. One full turn takes **240 polls** (4.0 s at 60 Hz **[I: poll = one vsync]**).
  - Endgame: 200 polls per turn.
- **SKIN_ANIM** (career.dta):
  - `SKIN_ANIM_START 0`, `SKIN_ANIM_END 12`, `SKIN_ANIM_PERIOD 250` ms.
  - The script plays `guitar_transition.tnm` from 0→12 (zoom in) or 12→0 over 250 ms with `anim_task`.
  - The keys stop at frame 10, so frames 10..12 hold the end pose **[I: clamp]**.
  - Multiplayer uses `MULTI_SKIN_ANIM_*` = 0, 10, 250 on `guitar_transition_p<player>.tnm`.

---------------------------------------------------------------------------------------------------
## 3. GuitarSelectPanel (`new_guitar_select_panel`), instances `input_guitar` and `multi_input_guitar`

Registration: FUN_001475f0 → FUN_00147628 (0xd4 bytes) → ctor FUN_001458c8. Both instances use `rnd_file ""`, so nothing is drawn. It is a pure input and state panel. **[C]**

### 3.1 State (ctor and Enter) **[C]**

| field | contents |
|---|---|
| `+0x80` | numPlayers: 1, or 2 if the `(multiplayer TRUE)` prop is non-zero |
| `+0x84` | bitvector `animating[p]` |
| `+0x9c` | bitvector `skin_select[p]` |
| `+0xb4` | `guitar[p]` symbol (init `''`) |
| `+0xc4` | `skin[p]` symbol (init `''`) |

Enter (0x147678): for each p, `guitar[p] = PlayerConfig(p).guitar`, `skin[p] = PlayerConfig(p).skin`, and `skin_select[p] = animating[p] = 0`. Then the base Enter. Exit is the base only.

### 3.2 Messages, GuitarSelectPanel_v1 at 0x147ff0

| msg | args | behaviour | returns |
|---|---|---|---|
| `button_down` | $button $player_num | FUN_00147bc0, see 3.3 | handled (1) or unhandled |
| `set_skin_select` | `p B` | `skin_select[p]=B` | 0 |
| `is_skin_select` | `p` | bit | bool |
| `get_num_skins` | `p guitar` | FUN_0011a358(provider(p), guitar): the count of **available** skins | int |
| `set_quickplay` | `p B` | `provider(p).quickplay (+0x10) = B` | 0 |
| `set_animating` | `p B` | `animating[p]=B` | 0 |
| `animating` | `p` | bit | bool |
| `get_selected_guitar` | `p` | `guitar[p]` | symbol |
| `get_selected_skin` | `p` | `skin[p]` | symbol |
| others | | UIPanel base, then the script | |

`provider(p)` is PlayerConfig(p)+0x14.

### 3.3 Input, FUN_00147bc0 **[C]**

1. `slot = (numPlayers==1) ? 0 : $player_num`.
2. If `animating[slot]`, return **handled**. Input is swallowed while the zoom plays.
3. If `is_select_button($button, $player_num)`:
   - send `GH_SELECT_MSG(component, slot, screen, $button)` to the UI chain;
   - return handled.
   - In single player, `$player_num` in the message is therefore **0**.
4. If the button is not DUp(12) or DDown(14), return **unhandled**. Back therefore reaches the screen's auto-back.
5. Otherwise:
   - `old` = the guitar index, or the skin index when in skin-select;
   - `delta` = +1 for **DDown**, −1 for DUp;
   - `new` = FUN_001478e8(delta, slot);
   - if new ≠ old, send `SCROLL_MSG(component, slot, screen, direction=delta, done=1)`;
   - return handled.

Step function FUN_001478e8 **[C]**:
- Guitar mode:
  - `count` = the number of guitars in the provider list; `idx` = the index of `guitar[p]` (−1 if absent).
  - `idx += delta`, wrapping: <0 → count−1, ≥count → 0.
  - `guitar[p] = list[idx]`.
  - `skin[p] = skins(guitar)[max(index of old skin in the new guitar's skin list, 0)]`. In practice this is the first available skin unless the same skin symbol exists.
- Skin mode:
  - `count = numSkins(guitar)`, `idx = index of skin`; same step and wrap.
  - `skin[p] = skins(guitar)[idx]`.

### 3.4 SelectGuitarProvider: lists and lock checks **[C]** (0x119c70 ctor, 0x119ce0 list, 0x11a498 skins)

Provider fields: `+0xc` = character, `+0x10` = quickplay, `+0x14` = the `guitars` config (config/guitars.dta).

**Guitar list** (vfunc 0x54 = SelectGuitarProvider_v9):
- If the character is `grim`, the list is only `grim_guitar`.
- Otherwise:
  - **quickplay FALSE** (career): FUN_00137c18(campaign, 3, 0) → FUN_0013d898, the campaign item list of category 3 = `guitars` for the **current difficulty**, honouring unlock-all **[I: exact ordering = the campaign/store unlock data]**.
  - **quickplay TRUE** (multiplayer): FUN_00138240 iterates `(guitars …)` in config order and keeps each guitar unlocked in **any** of the 4 difficulties.
  - In both cases `grim_guitar` is filtered out.
- `get_text`, `get_symbol` and `list_length` are also exposed (GuitarProvider_v1).

**Skin list**, FUN_0011a498: `(guitars <g> skins)` in config order, keeping each skin that is unlocked:
- quickplay FALSE: at the current difficulty (FUN_00137b00);
- quickplay TRUE: at any difficulty 0..3.

`store.dta` holds the purchasable guitars and skins with prices.

---------------------------------------------------------------------------------------------------
## 4. MultiCharSelPanel (`new_multi_char_sel_panel`), instance `multi_sel_character`

Registration: FUN_0014e108 → FUN_0014e140 (0x9c bytes) → ctor FUN_0014d2d0.

- State: bitvector `done[2]` at `+0x80`; `+0x98` = the `char_multi` panel pointer, set on Enter. **[C]**
- Prop: `(char_navigator (vertical metal classic alterna hair_metal punk nu_metal hiphop grim) [(wrap B)])`. `wrap` defaults to TRUE. **[C]**
- Scene: `ghui/multi_sel_character.gh` (the default rnd), which contains:
  - PictureEx `msc_char_<c>.pic`;
  - `sc_char_<c>_focus_p1/p2.view` and `sc_char_<c>_highlight_p1/p2.mesh`;
  - `sc1_char_nm.lbl` / `sc2_char_nm.lbl`, `msc_loading1/2.lbl`, `sc_ready_p1/p2.mesh`.

### 4.1 Lifecycle **[C]**

- Enter (0x14e190):
  - base Enter;
  - `+0x98 = ui panel char_multi`;
  - `done[0]=done[1]=0`;
  - for each char in `char_navigator vertical` that is **not unlocked at any difficulty** (FUN_0014e7e8 → FUN_00137b28 with d = 0..3): `msc_char_<c>.pic` → SetState(kDisabled=2).
- Exit: `+0x98=0`, then the base Exit.

### 4.2 Messages, MultiCharSelPanel_v1 at 0x14f240

- `is_player_done p` returns `done[p]`. **[C]**
- `button_down`: FUN_0014e850, with `pad = $player_num`. **[C]**
  1. **Select**:
     - If `done[pad]`, return handled (swallowed).
     - Otherwise send `GH_SELECT_MSG(component, pad, screen, $button)` and set `done[pad]=1`.
     - If `done[0] && done[1]`, send the message **`chars_selected`** (no args) through the UI chain; the screen script handles it.
     - Return handled.
  2. **DUp or DDown**:
     - If `done[pad]`, return handled (swallowed).
     - Otherwise run FUN_0014e4a0(forward = button==**DDown**). If it moved, send `SCROLL_MSG(component, pad, screen, direction = DUp ? **+1** : −1, done=1)`.
     - Return handled. Note the direction sign is the opposite of GuitarSelectPanel's.
  3. **Back**:
     - If `!done[pad]`, return **unhandled**, which leads to the auto-back to `main`.
     - Otherwise set `done[pad]=0`, send **`deselect_char`**, and return handled.
  4. Anything else is unhandled.
- The navigation step FUN_0014e4a0 **[C]**:
  1. Find the index of `PlayerConfig(pad).character` in the vertical list.
  2. Step ±1, wrapping (or clamping when `wrap` is FALSE).
  3. Skip the **other player's** current character. Two players can never share a character.
  4. If it comes back to its own character, or hits a clamp end, return unhandled.
  5. Otherwise call `PlayerConfig(pad).set_character(c)`. This also updates that player's guitar provider character, so grim restricts the guitar list to `grim_guitar`.
  6. If c is unlocked at any difficulty, return handled. Otherwise recurse, continuing in the same direction.
- The panel does not call `show_char` itself. The screen script's SCROLL_MSG handler calls `char_multi show_char $playerNum {config get_character}`.

---------------------------------------------------------------------------------------------------
## 5. Script flows (message order)

General ordering **[C]**:
- A screen Enter first runs `Enter()` on each panel that is not already up, in list order. That runs C++ Enter, then the panel's script `enter`. Only after that does the screen's `enter` script run.
- On input, the screen script handler sees `button_down` before the focus panel.

### sel_difficulty (career)
- Panels `meta sel_diff_career helpbar char_single`.
- `char_single` is entered with `hide TRUE` and `show_char 0 {game get_character}`, so it is invisible and only **preloads** the character models.
- SELECT → `campaign new_campaign` ? `sel_character_new` : (`get_character_info`, `career`).

### career
- Panels `meta career helpbar char_single`; char_single stays up and hidden.
- `cm_hero.btn` → `sel_character_edit`; `cm_guitar.btn` → `sel_guitar_edit`; `cm_store.btn` → `store`. The store button is disabled on Easy.

### sel_character_new / sel_character_edit (SEL_CHARACTER_COMMON)
- Panels: `meta sel_character char_single sel_character_loading helpbar`; focus `sel_character`.
- Order:
  1. char_single is shared with the previous screen, so it does not re-enter.
  2. sel_character enter:
     - `animate_forever_30fps sel_character.view`;
     - `char_single show_char 0 {game get_character}`;
     - set the labels;
     - `set_focus sc_char_<c>.pic`, which fires FOCUS_MSG;
     - `init_pic_state` for each char (disabled if not `campaign is_unlocked`);
     - `app set_compact_heap TRUE`.
  3. On **TRANSITION_COMPLETE**: `char_single hide FALSE`. The character only appears after the 500 ms transition.
  4. sel_character_loading poll: `scl_loading.lbl showing = !char_single are_chars_loaded`.
  5. Focus moves through the vertical navigator, which skips disabled pics. On FOCUS_MSG: highlight `sc_char_<c>_focus.view`, set the name and blurb, then `char_single show_char 0 <c>`.
  6. The screen exit runs `char_single hide TRUE`.
- BUTTON_DOWN on the screen, new variant: on select, play `button_select` and `game set_character` if it changed, then go to `sel_guitar`. Back → auto-back.
- BUTTON_DOWN on the screen, edit variant: on select, set the character, then `campaign set_character_info` and go to `career`. On back, go to `career` with backwards_anim.

### sel_guitar / sel_guitar_edit (SEL_GUITAR_COMMON)
- Panels: `meta sel_guitar guitar_display input_guitar helpbar char_single`; focus `input_guitar`.
- Order:
  1. guitar_display Enter (`force_exit`).
  2. input_guitar Enter: copies `game` guitar and skin for p0.
  3. char_single stays up and hidden.
  4. Screen enter:
     - `sg_selectyourguitar.lbl set_text select_guitar`;
     - `input_guitar set_quickplay 0 FALSE`;
     - `input_guitar set_skin_select 0 FALSE`;
     - `update_display`, which runs `get_selected_guitar 0`, `get_selected_skin 0`, `is_skin_select 0`, sets the labels (`game get_guitar_desc` / `get_guitar_skin_desc`), then **`guitar_display show_guitar 0 $guitar $skin`**;
     - `guitar_transition.tnm set_frame 0`;
     - `char_single set_paused TRUE`.
- Input:
  - The screen BUTTON_DOWN runs first:
    - if `{$this animating 0}` (routed to input_guitar), swallow;
    - on back: if `is_skin_select 0`, run `deselect_skin`: `button_back`, label `select_guitar`, `set_skin_select 0 FALSE`, `skin_zoom 12 0`, `update_display`;
    - otherwise, if `auto_back` (TRUE for sel_guitar), return kDataUnhandled → panel unhandled → auto-back to `sel_character_new`;
    - otherwise run `go_back` (edit: `career` + `set_character_info`).
  - Strum → the panel steps and sends SCROLL_MSG → the screen runs `update_display`.
  - Select → the panel sends GH_SELECT_MSG → the screen script:
    - play `button_select`;
    - if already in skin select, run `goto_career`;
    - otherwise, if `get_num_skins $player_num guitar > 1`: label `select_skin`, `set_skin_select 0 TRUE`, `skin_zoom 0 12`, `update_display`;
    - otherwise `goto_career`.
  - `skin_zoom a b` = `set_animating 0 TRUE`, then `anim_task guitar_transition.tnm 250 a b`, then `delay_task 250 {set_animating 0 FALSE}`.
  - `goto_career` = `game set_guitar {get_selected_guitar 0}`, `game set_guitar_skin {get_selected_skin 0}`, `campaign set_character_info`, then `ui goto_screen career`.
- The screen exit runs `char_single set_paused FALSE`.

### multi_sel_character (from main → multiplayer)
- `main` already has `char_multi` up and hidden: its enter runs `show_char 0/1`, which preloads both players' characters.
- Panels: `meta multi_sel_character char_multi helpbar`; focus `multi_sel_character`.
- Screen enter:
  - `animate_forever_30fps`;
  - `$player_zero_done`/`$player_one_done` FALSE;
  - `sel_char_highlight_off 0/1`;
  - `scroll_char_sel 0/1`: label `sc<n>_char_nm.lbl`, show `sc_char_<c>_focus_p<n>.view`, and `char_multi show_char p <c>`;
  - `set_compact_heap TRUE`.
- TRANSITION_COMPLETE → `char_multi hide FALSE`. Exit → `char_multi hide TRUE`.
- Poll:
  - `msc_loading<n>.lbl showing = !char_multi is_char_loaded p`;
  - `sc_ready_p<n>.mesh showing = $this is_player_done p`. `$this` is the screen, so the call routes to the panel.
- Panel messages and the screen's reaction:
  - SCROLL_MSG → `sel_char_highlight_off $player_num` + `scroll_char_sel`.
  - GH_SELECT_MSG → `button_select`.
  - `chars_selected` → `goto_screen multi_sel_guitar`.
  - `deselect_char` → `button_back`.

### multi_sel_guitar
- Panels: `meta multi_sel_guitar multi_input_guitar multi_guitar_display helpbar char_multi`; focus `multi_input_guitar`.
- Screen enter:
  - `player0_done`/`player1_done` FALSE;
  - labels `msg_select_guitar`;
  - `set_skin_select 0/1 FALSE`;
  - **`set_quickplay 0/1 TRUE`**, so the lists contain items unlocked in any difficulty;
  - `update_display`, which runs `show_guitar p guitar skin` for both players;
  - `guitar_transition_p<p>.tnm set_frame 0`;
  - `char_multi set_paused TRUE`.
- Input is routed per pad (`slot = $player_num`):
  - SCROLL_MSG → `update_player_display $player_num`.
  - GH_SELECT_MSG → `handle_select`, then, if both players are done, `set_guitar 0/1` (PlayerConfig `set_guitar` / `set_guitar_skin`) and go to `multi_selsong`.
  - `handle_select`:
    - if in skin select and not done: mark done;
    - otherwise: `set_skin_select p TRUE`; if the guitar has exactly 1 skin, mark done; otherwise set the label to `msg_select_skin` and run `play_skin_anim p 0 10` (sets `animating p` through the screen → panel).
  - BUTTON_DOWN on the screen:
    - back while in skin select: undo it, `set_done FALSE`, and the reverse anim if there is more than 1 skin; returns TRUE;
    - otherwise, if the player is done, swallow (TRUE);
    - otherwise kDataUnhandled. Back while not in skin select → auto-back to `multi_sel_character`.
- The `get_num_guitar_skins` method passes `$player_num` from the dynamic scope **[C script]**.

### store
- Panels: `meta store_back store_guitar_display store_cd store_video char_store store helpbar`.
- store enter:
  - `set_store_mode TRUE` (song and character providers);
  - `hide_models`: `char_store hide TRUE`, `store_guitar_display hide TRUE`;
  - `char_store show_char 0 {character_provider get_symbol 0}`, which preloads.
- SELECT on a category button:
  - `st_guitars`/`st_skins`: `store_guitar_display hide FALSE` and `update_store_item_display`.
    - guitar: `show_guitar 0 <guitar> ''`, i.e. the first skin.
    - skin: `show_guitar 0 {store_skin_provider get_guitar i} <skin>`.
  - `st_characters`: `char_store hide FALSE` and `show_char 0 <char>`.
- The screen BUTTON_DOWN changes `itemIdx` on DUp/DDown (mod `list_length`) and re-runs `update_store_item_display`.
- Store poll:
  - `st_loading.lbl showing = category==character && !char_store are_chars_loaded`;
  - `char_store char_store_lights_on {campaign is_unlocked item}`. Items not yet bought are drawn unlit.
- Back:
  - from screen 1: `autosave_goto career`;
  - from screen 2: `hide_models` + screen 1.

### win_game (endgame)
- `endgame_guitar_display hide TRUE` on enter.
- After the white fade: `hide FALSE` and `show_guitar 0 battleaxe ''`, which uses the axe placer and spin plus the endgame camera.

---------------------------------------------------------------------------------------------------
## 6. Reimplementation checklist

1. **CharsysPanel**
   - 2 slots and a 6-model LRU cache with the priority scheme in 1.5.
   - The character `meta` mode: load `<c>_idle_ui` (kGuitarUI, real time, looping) and no IK.
   - Transform = the placer's world transform, which turns the character 180° about Z to face the camera.
   - Draw with the state's view camera, environment and shadow light.
   - `hide`, `show_char`, `are_chars_loaded` (slot 0 only), `is_char_loaded`, `char_store_lights_on` (lights restored, or no lights plus black ambient).
   - `set_paused` stops the poll, so the character freezes but stays drawn.
2. **GuitarDisplayPanel**
   - Per-slot async load of `charsys/guitars/gen/<file>.rnd_ps2`, chosen through the character archetype's GUITARS list indexed by the skin's `outfit`.
   - Material = `(mat X)`.
   - Mount the guitar's `bone_pos_guitar` (or `guitar.mesh`) at the placer.
   - Spin by +0.5 frame per poll through the tnm filters; reset to frame 0 when the skin changes.
   - Cameras and lights from `guitar_display.gh`; endgame swaps the camera.
   - Unload on Exit.
3. **GuitarSelectPanel**
   - Per-player guitar, skin, skin_select and animating state.
   - Strum steps with wrap (DDown = +1), filtered by unlocks (career = current difficulty, multi/quickplay = any difficulty; grim gets only `grim_guitar`).
   - Select emits GH_SELECT_MSG; strum emits SCROLL_MSG(done=1); input is swallowed while animating; back is left unhandled.
4. **MultiCharSelPanel**
   - Per-pad done flags; select, back and strum semantics as in 4.2.
   - Players cannot share a character; locked characters are skipped and their pics disabled.
   - Emits `chars_selected` and `deselect_char`.

## 7. Open points
- The default guitar held by the menu characters (OutfitState default for the GUITARS category) is not traced.
- The exact meaning of `DAT_00368e40..4c = (0, −1, 0, 8)` (a shadow plane?) is unknown.
- The exact parenting performed by FUN_001da100 in guitar finalize is not confirmed.
- That one poll equals one vsync (60 Hz) is assumed for the spin and idle speeds.
- The order of the career guitar list from FUN_0013d898 (campaign category 3) is not documented here.

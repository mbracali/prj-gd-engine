# gd-game-10: decisions

Each entry: what, why, how to undo.

## D1. gd10 starts from a copy of gd04's engine (2026-11-10)

What: `engine/`, `tools/`, `build_commands/`, `local_build.sh` copied from
gd-game-04's tracked files, renamed gd04 → gd10, GD04 → GD10,
gd-game-04 → gd-game-10, user-facing names → Guitar Hero: Metallica.
The engine version was set to 0.0.0 (matching `game-info.yaml`; not a bump).
Why: the kickoff (section 1); gh4 is GH3's engine one generation later.
Caveat: the copied code's comments keep GH3's **[C] addresses**: they are
GH3 XEX addresses, not Metallica's, until re-confirmed (each is **[I]** for
gd10 until then).
Undo: delete the copy.

## D2. Kickoff owner questions: defaults for the proof of concept (2026-11-10)

What: the run prompt's defaults (kickoff "Owner's answers"). Undo: the owner
answers differently.

## D3. Disc files: CHNK or stored; pak entries may be GZ (2026-11-10)

What: `Disc::read` inflates CHNK chains and returns other DATA/COMPRESSED
files as stored (GH3's whole-file deflate is gone); `Pak` inflates GZ-wrapped
entries at load. Why: every file on the disc is one or the other [C data].
Undo: none needed (GH3 discs are not read by gd10).

## D4. The UI state machine's call order [I] (2026-11-11)

What: `menu_change` leaves the top (ui_destroy_) and pushes (ui_init_,
ui_create_); `menu_replace` and `menu_back` also deinit what they remove;
going back re-creates the uncovered state (ui_create_, not ui_return_);
`menu_add` keeps the state under it; `menu_back` honours `state` (back to
it; nothing when it is already the top) and `num_states`. After ui_create_
the menu named by focus_id / ui_task_menu_default_focus_id gets the focus.
Why: the XEX's task_menu (0x823090D8 .. 0x8230A7C0) only read in part; this
order runs every boot and Quickplay screen. Undo: ui/ui_states.cpp.

## D5. flowState() maps gh4 states for the shared port code (2026-11-11)

What: `FrontEnd::flowState()` returns the UI stack's top; uistate_mainmenu
and uistate_options keep GH3's names (main_menu_fs, options_select_option_fs)
so the port's help-bar items and Esc logic from gd04 work unchanged.
Undo: rename those uses to the gh4 names.

## D6. Sprite dims are a size, not a scale (2026-11-11)

What: GH3 turned dims on a textured sprite into a scale [C GH3]; gh4's descs
give dims (1280 x 720 on a 1024 x 512 picture) and scale (1, 1) and lay
children out in those dims, so gd10 keeps dims as the size [I]. Undo:
ui_system.cpp setTexture / applyProps.

## D7. FSB key: the XEX's generator (2026-11-11)

What: song banks are decrypted with the key 0x820E24F8 makes from the bank's
name ("<song>_<n>"), plain = bit-reverse(stored) XOR key [C]. Previews use
another key (not done). Undo: none.

## D8. The song's pairs and stems [I] (2026-11-11)

What: bank 1 = drums (4 pairs), bank 2 = guitar, bass, vocals, bank 3 =
backing + crowd; guitar -> the guitar stem, bass -> rhythm, the rest -> song.
Why: `--songaudiotest entersandman` levels against the chart (vocals pair
silent until 1:10, bank 3's second pair silent). Undo: kGh4Pairs in
audio/song_stream.cpp, once the XEX's table is found.

## D9. Chart note flags [C] (2026-11-11, confirmed 11-11)

What: (time, packed) pairs: frets 0x1f, 0x20 open, 0x40 the note is a
hammer-on (not a flip): ProcessHammerOns (0x8214EE70, jam songs only)
writes the same bit: single fret (an open note a value of its own), not
the previous note's, within the first beat / default_hammer_on_measure_scale
[C]; on the disc 1267 of 1270 Enter Sandman expert notes match that rule
(the rest forced). Two entries at one time are one chord (frets ORed, the
longer length) [C data; I: the merge, the original keeps a length per
fret]. 0x0f80 / 0x2000 unused for now. Undo: game/chart.cpp,
game/note_track.cpp, game/player.cpp (lastHitMask_ of an open note).

## D10. The Glitches stand-in (2026-11-11)

What: on the first main menu the original's own scripts unlock_all_profiles
and purchase_all_profiles run, and the band is named The Glitches (band_info
name), unless GD10_NO_GLITCHES. Why: rule 14; gh4's save differs from GH3's
and gd04's ensureGlitches does not apply. Undo: ui/front_end.cpp.

## D11. Empty switch cases share the next body [I] (2026-11-11)

What: in `switch`, a matched case whose body is only its jump (then the
next case) runs the next non-empty body (`case p1_quickplay ... case
p4_quickplay <body>`). Why: otherwise one-player Quickplay never starts a
song. To confirm in the XEX's interpreter. Undo: script/vm.cpp kSwitch.

## D12. IsSongAvailable [I] (2026-11-11)

What: true for set-list songs whose first bank is on the disc, minus
tutorials, sync / debug charts and the jam session. Why: the native's rule
is not read yet; this hides the Death Magnetic DLC songs (no audio on the
disc) and test charts. Undo: ui/natives_gh4.cpp.

## D13. The debug highway stands in for gh4's highway (2026-11-11, done 11-11)

Done: the song draws gh4's own highway (highway_guitar1 layout, the CAS
highway skin, gems / buttons / tails from the script-built materials, D15).
The debug highway remains for data without the gh4 tables.

## D14. A descinterface element wraps its desc; the desc's root is its child 0 (2026-11-11)

What: `CreateScreenElement {type = descinterface desc = ...}` makes an
element with the call's own props (pos, just, dims, pos_anchor, z, alpha);
the desc's root element (main_menu_bg, helper_pill_container, hud_root's
tree) is built as its child 0, and alias / prop paths start there. Why:
setup_band_hud resolves `{hud_root child = 0}` as the HUD's root [C script];
make_menu_frontend and 20 other full-screen descs pass just = [center
center] without dims at pos (0, 0), which then moves nothing (applied to
the root it drew the 1280 x 720 picture centred on the top-left corner: the
"skull art over the menus" of the proof of concept). The XEX's
descinterface class is not read yet [I]. Undo: ui/ui_desc.cpp buildDesc /
descPath.

## D15. gh4 highway materials drawn as layered sprites [I] (2026-11-11)

What: guitar_material.qb builds each gem / button / whammy material from a
template (ImmediateMode_AlphaFade_UI_3Pass, AnimatedTexture_UI_3Pass,
WhammyBar_UI) with up to three grey textures (basic_gems.tex) tinted by the
gem set's colours (standard_gems / _stars / _fx) [C data]. MaterialLibrary
(MATL v3) is not decoded, so the passes are drawn in order, colour x
texture, alpha-blended (added for blendmode add), colours clamped to 1; the
whammy glow (m_glowcolor, m_glowintensity) is left out. Undo:
render/gem_materials.cpp, render/highway2d.cpp (gemGh4, matSprite).

## D16. The default guitarist's highway [I] (2026-11-11)

What: the highway skin is the character profile's appearance
CAS_Guitar_Highway desc_id -> CAS_Highways_List (highway_pak,
highway_texture) [C data]; with no character_id (a direct `--song`) the
first Preset_Musician_Profiles_Modifiable entry (James) is used. Undo:
ui/song_screen.cpp highwayTexture.

## D17. gh4's light set: which light takes which register [I] (2026-11-11)

What: the registers are gh4's [C data: constant tables; microcode]; which
light fills them is GH3's light set extended [I: gh4's is VMX128 code not
read]: key light = the first directional light (c161..c163), the next
three the specular slots and the SH, the first four point lights
g_PointLight (c176 + 4i, attenuation c248 / c249), the first five spot
lights g_SpotLight (c192 + 4i, c250..c253, the fifth's c211), cone from
hotspot / field as full angles; the rest into the SH as GH3's; ambient into
c160 and SH0 (no shader reads both). docs/render.md "Lights". Undo:
render/gh3_shader.cpp lightObject.

## D18. gh4's screen effects drawn with GH3's passes [I] (2026-11-11)

What: tod_profiles.qb's bloom2 / dof3 / bright_sat entries [C data] mapped
on the chain gd04 built for GH3: bloom2 = simple_bloom's passes (subtract
as the cutoff, colormodulate as the strength; saturation, thickness,
equation not applied), dof3 = the far blur in metres; noise2, uberscreenfx,
godray, vignette not drawn; GH3's chain order. docs/render.md "Screen
effects". Undo: render/screen_fx.cpp readEntry.

## D19. Lit and soft-edged particles [I] (2026-11-11)

What: a quick material with `lighting` multiplies its colour by the SH's
constant term + 4/17 of the key light at the system (clamped to 1);
`softedge` fades over softedgescale metres in front of the scene depth;
`max` caps the emit rate at max / mean life [C data: the flags; I: the
models]. Undo: render/particles.cpp (lit, softEdge, max), gh3_shader
spriteLight.

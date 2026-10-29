# GH3 (Xbox 360): the two-player modes and boss battles

How co-op, face-off, pro face-off, battle and the career boss battles are
reached and played, as read in the disc's scripts and data and in the XEX.
**[C]** confirmed (script / table name or address), **[I]** inferred. The
code: `engine/src/game/match.*` (rules, rendering-free), `game/player.*`
(`PlayerRules`), `render/hud2p.*` (the 2P HUDs), `render/highway2d.*` (two
highways, battle effects), `ui/song_screen.*`, `ui/natives_game.cpp`
(`start_gem_scroller`), `ui/front_end.cpp` (`songEnded`). Research notes
(scratch, untracked): `agents/scratch/m7/mp_spec.md`, `battle_spec.md`.

## 1. Reaching the modes (GH3's own menus)

| mode | main menu | flow states (walk) |
|---|---|---|
| co-op career (`p2_career`) | CO-OP CAREER | coop_career_select_controllers_fs > choose band / enter name > coop_career_select_difficulty_fs (two lists) > character select / hub (one flow per player, `ui_flow_manager_state[1]`) > venue > setlist > coop_career_choose_part_fs (GUITAR / BASS or LEAD / RHYTHM; the parts always differ) > the song > coop_career_newspaper_fs |
| face-off (`p2_faceoff`), pro face-off (`p2_pro_faceoff`), battle (`p2_battle`) | MULTIPLAYER | mp_select_controller_fs > mp_select_mode_fs (FACE-OFF, PRO FACE-OFF, BATTLE; all three send `select_faceoff`) > mp_faceoff_character_select_fs x2 > hub x2 > venue > setlist > (battle: mp_faceoff_battle_help_fs) > mp_faceoff_select_difficulty_fs (two lists; pro face-off one shared) > the song > mp_faceoff_newspaper_fs |
| boss battle (`boss_battle = 1`) | CAREER | the setlist's boss song (bosstom tier 2, bossslash tier 5, BossDevil) > career_battle_help_fs (BATTLE) > the song > career_newspaper_fs / fail |

- `p2_coop` (quickplay co-op) is online only: CO-OP is added to the mode menu
  only when `is_network_game` [C create_mp_select_mode_menu].
- The controller screen [C menu_select_controller_*]: `GetActiveControllers`
  (4 flags, 0x82103AC8), `GetInputHandlerBotIndex` (player - 1 + 4,
  0x82104468 / 0x820E7D08), `IsGuitarController` (a bot is; 0x82100D00). The
  primary controller's icon moves to the P1 side by itself; player 2 presses
  Down; both press green. The port's controllers are its player slots
  (`Input::padConnected`); the two-player walks turn the split keyboard on.
- `exclusive_device` may be an array (`[player1_device, player2_device]`) [C
  create_select_difficulty_menu]; the UI accepts either.
- `start_gem_scroller` (native) reads `game_mode`, `current_num_players`,
  `current_difficulty(2)`, `playerN_status.part`, and a boss song's `boss`
  (sets `boss_battle`, `current_boss`, two players), and battle's
  `battle_sudden_death` + `save_health` [C script start_gem_scroller,
  bossbattle_init, GuitarEvent_SongWon].

## 2. Two highways [C generate_pos_table, setup_highway, difficulty_setup]

The "...2" tweaks (height 270, top width 170, widthOffsetFactor 1.33, gem
start 0.27, fade 25, tiling 1.4, ...) computed around x = 640, each highway
moved by `x_offset_p2` (225): P1 centre 415, P2 865. Scroll time x
`p2_scroll_time_factor` (0.8). Player 2's highway surface is the global
fallback (the character's highway material: M6) [I].

## 3. Co-op [C unless noted]

- Each player plays his part at his difficulty; scores are kept apart, the
  HUD and the end screens show the sum.
- Multiplier from p1.current_run + p2.current_run (x4 max, x8 in SP)
  [0x8210A748]; a miss / overstrum resets both runs [0x8210C800, 0x8210CB10];
  notes_hit / total_notes rise for both [0x8210BB40].
- Star Power: every gain goes to both (increase_star_power); whammy
  `star_power_whammy_add_coop` 2.5 / beat; drain `star_power_drain_rate_coop`
  16.67 / measure. Activation: a trigger (>= 50) readies that player and
  starts the raise-axe prompt (`show_coop_raise_axe_for_starpower`, ~1.24
  s); both ready while it shows: SP for both [0x82112850, 0x821131E4]. The
  1000 ms `star_power_coop_trigger_ms` is dead code [I]. The prompt is
  spawned with the other player's status, so it shows on the side of the
  player who has not raised: P1's trigger -> P2's side (865, 240), P2's ->
  P1's (415, 240) [C 0x82112850, script; FINDINGS §6]. gd04 draws it on
  P1's side always (render/hud2p.cpp: a follow-up).
- Health per player (may go below 0); fail when (h1 + h2) / 2 <= 0
  [0x8210FD18]; invincibility: both easy 20 %, both <= medium 10 %
  [calc_health_invincible_time].
- The guitar stem mutes on P1's misses, the other part's stem on P2's
  (`Mixer::Track::guitar2`) [I].

## 4. Face-off and pro face-off [C unless noted]

- Face-off: only the notes inside the player's `<song>_faceoffp1 / p2`
  sections enter his input array, and the gems come from it: the other
  player's notes are neither drawn nor judged [0x82104558-0x821045CC]. Star
  Power stays (the HUD has the tubes); a phrase completes on the note that
  is its last in the full chart (input_array word 7 is the next chart
  note's time), so a phrase that ends in the other player's section never
  completes [C 0x82102B70, 0x821044C8].
- Score scale: (sim_bot_score_p1 + sim_bot_score_p2) x 0.5 / own
  (sim_bot_score: the chart's points at a perfect run's multiplier) [0x8210B9A0];
  pro face-off 1.0.
- Never fails [0x8210FD18]. Winner: the higher score ("Player One / Two
  Rocks!", "TIE!") [GuitarEvent_SongWon_Spawned].

## 5. Battle [C unless noted]

- Phrases: `<song>_<part><Diff>_StarBattleMode`; no Star Power [0x8211E704].
  A card (battlemode_creation_selection) is rolled when a phrase gem is
  made while none is pending, carried by every phrase gem, and the last
  gem's is given when the phrase completes; a gem outside the phrases and a
  completed phrase clear it [0x821217A0-0x82121B50, 0x8211E870; phrase test
  0x82102B70] (battlemode_ready: a stack of `max_num_powerups` 3, a full
  hand loses its top card) [0x8211E7B0].
- Choice (battlemode_select): health > 0.5: weight 10 each, except the last
  rolled and the top card (weight_low 0); <= 0.5: Steal / Lefty / Whammy
  only; no Difficulty Up against expert; Death Drain only in sudden death.
- Fire: the SP trigger, no 50 % gate [0x82112D84]; the top card on the
  other player; drain_time + `battle_<diff>_drain_offset` (0 / 1500 / 3000 /
  4500 ms); a second hit of a running attack extends it.

| attack | effect on the victim |
|---|---|
| Amp Overload | gems flicker in runs of [F, 2F-1] events every 3 frames (F 2/2/3/3), still judged; the highway bobs +-8 px [battle_lightning, 0x821195C0] |
| Difficulty Up | after the sustains on the highway (`hold_difficulty_up`), notes come from the next difficulty's chart for the drain; expert: Double Notes [battle_up_difficulty, 0x821213A8] |
| Double Notes | at fill time a fret added per GH3's table, HOPOs off [0x821044C8] |
| Powerup Steal | ~1 s later the victim's top card joins the thief's hand [battle_steal] |
| Lefty Flip | gems mirrored at once, the fret buttons a highway later, back after the drain (visual only) [battle_lefty_notes] |
| Broken String | a random fret (easy 3 / medium 4 / 5 lanes) dead until pressed 6/8/10/12 times [battle_broken_string, 0x82100DA0] |
| Whammy | every fret dead until the whammy passes half 3/4/5/6 times [battle_whammy_attack] |
| Death Drain | one miss's loss every 0.4/0.3/0.25/0.2 s until the victim dies; a second halves it [battle_death_lick] |

- Health: `health_change_*_battle`, x1.35 / x0.85 zones, no loss at 0. KO: a
  player at 0 with the healths unequal -> song_failed -> GuitarEvent_SongWon
  battle_win = 1; winner by health [0x8210FD18, GuitarEvent_SongFailed].
- Both alive at the end: sudden death: health saved, the same song again,
  every new card Death Drain, no invincibility; repeats [GuitarEvent_SongWon,
  restart_song]. (Its `save_current_powerups_p1 = <current_powerups_p1>` reads
  an unset local, so saved hands come back empty [C script; kept].)
- Hit by an attack (`fire_bolt`): the highway is pushed (-70, 50) / (70, 50)
  over 0.1 s and back [hammer_highway].

## 6. Boss battles [C unless noted]

- Props (`Boss_TomMorello_Props`, `Boss_Slash_Props`, `Boss_Devil_Props`):
  gainpernote / losspernote x 0.02 a note (no multiplier), PowerUpMissedNote,
  WhammySpeed, BrokenStringSpeed, BrokenStringMissedNote, PowerUps (both
  players' cards come from this list: bossbattle_select).
- The boss plays the rhythm chart at P1's difficulty (no drain offset). The
  bot (CheckButtonsBoss 0x821096E8): each note at the early edge of the
  window, HOPO runs tapped four times and strummed on the fifth, frets held
  for the length + 1 s; it misses only gems marked by
  ApplyBossBattleGemMisses (the player's attack: PowerUpMissedNote % of the
  gems for the drain, Broken String: BrokenStringMissedNote % until all
  counters < 3) [0x82103EB8, bossbattle_ai_damage] and frets attacks killed;
  it repairs a whammy every WhammySpeed ms, a broken string every
  BrokenStringSpeed ms. It fires its first card after 100 frames, a second
  at once [0x82108948].
- Boss at 0: won (song_wonp2). The player at 0: lost. The song's
  `<song>_scripts` `boss_battle_begin_deathlick` (bosstom 195155 ms,
  bossslash 226504, bossdevil 321466) drains the player every 0.1 s.

## 7. The two-player HUDs [C data, UpdateScoreFastPerFrame 0x821167B0]

`setup_hud` picks `coop_career_hud_2d_elements` (co-op),
`faceoff_hud_2d_elements` (face-off, pro), `battle_hud_2d_elements` (battle,
boss); `render/hud2p.cpp` builds them from the tables as
create_2d_hud_elements does (create_once elements once, `_p1` / `_p2`
positions, `pos_off_p2`, `rot_p2`, flags `p1` / `p2` flips, `battle_pos`,
note-streak bars) and drives: co-op needle 74 x (avg health - 1), zones
24.67 / 55.5, score = sum; face-off needle by the score gap and the leader's
BG / crystal lit; battle a 1P needle per player (+-42, zones 14 / 31.5);
tubes, multiplier lights and nixies per player; the battle hand's card
textures over the default icons [I: slot placement] and the card's name at
`attack_ready_text_pos_p<n>`.

## 8. End of the song

`FrontEnd::songEnded` fills player1_status and player2_status (score,
notes_hit, total_notes, max_notes, best_run, base_score, current_health,
star_power_amount, final_blow_powerup, current_num_powerups) and
current_powerups_p1 / _p2; GH3's scripts take it from there (GuitarEvent_SongWon
/ _SongFailed, the winner text, the newspaper, sudden death's restart).
`stars` is left to Progression_SongWon.

## 9. Drawn, and not done / approximations

Drawn (render/hud2p.cpp, render/highway2d.cpp, data/band.cpp):
- Battle star gems: sys_BattleGEM_* flip-books with the phrase card's icon
  (`<gem>_battle_icon`: card_texture, scale 1.2, alpha 0.5) [C 0x821218A8].
- The hand: battlecard_<i>_<p> in battlemode_container (screen space) at
  rock_pos + card_1_off (64x64), the older cards + card_2_off at
  medium_scale 0.55 / + card_3_off at small_scale 0.4; a new card flies from
  its last hit lane's pos_2d - (0,90) (-/+225) in 0.3 s, alpha 0.5 -> 1; the
  others slide 0.3 s [C scripts battlemode_ready, update_battlecards_add /
  _remove]. Which lane counts as "last_hit_note" for a chord (the lowest
  fret here) is [I].
- attack_bolt (fire_bolt cards; Death Drain's own at 80 deg):
  sys_Big_Bolt01_Red at (640,230) -/+ (330,0), turned -/+ 60, P1 mirrored,
  8 game frames [C script; the red material's tint and the bolt's sparks I /
  not drawn]. hammer_highway now follows every attack, as battle_trigger_on
  spawns it unconditionally [C script].
- Death Drain: the death icon (icon_attack_deth_128; boss songs their own)
  at the victim's side pulsing alpha 0.95 / 0.1 every 0.15 s; the "DEATH
  DRAIN" alert (battle_alert_death + wings + text_a10) at (640 -/+ 225, 430),
  scale 0.9 + 0.3 (1 - health), 220 (1 - health) px lower, turning to a
  random -7..7 deg and flapping every 4 game frames [C scripts
  battle_death_lick, animate_death_icon, death_text, death_text_wing_flap;
  relative_scale read as absolute 1.1 / 1.2 I; the bite sparks and the
  highway's bite push not drawn].
- The attacks in force on a player (add_battle_text: hud_screen_elements'
  texts, text_a6 0.7) at (380,35) / (890,35), older ones lower [C script;
  their order and the x shift of older texts I].
- Boss battles: the death meter (create_battle_death_meter at the HUD's
  move-in, not with devil_finish): slides up to (648,500), its marker
  climbs 150 px and the wings close 40 deg until the song's death-drain time
  (bossdevil 321466, bossslash 226504, bosstom 197733), the marker pulsing
  green / white [C scripts].
- Two players' band (create_band with current_num_players 2): Guitarist =
  the lead player, Bassist = the other through create_guitarist_profile, a
  drummer, no singer; a boss's player 2 is its character_profile, outfit 1,
  style 1 (so the boss stands at the bassist's spot) [C scripts create_band,
  create_guitarist_profile, start_gem_scroller].
- P2's highway: its character's sys_<name>_1_highway (the boss's own) [C].
- Lou's finish: start_gem_scroller's starttime / devil_finish_restart reach
  the native scroller (a mid-song start; FINDINGS "Mid-song starts"); with
  devil_finish player 2's gem container is at x 1000 and its HUD stays off
  screen, no death meter [C scripts setup_highway,
  move_2d_elements_to_default] — not walked (no Lou walk).

Not done / approximations:
- The boss helper popup before a boss from the previous song's win
  (show_boss_helper_screen) is not reached by the native gem scroller [I].
- Card animations beyond the flight (the steal hand, the whammy bar
  training, lefty flip's button slide), band attack / response animations,
  the battle alert frames (create_battle_alert_frames), attack_ready_text's
  exact motion.
- Co-op's raise-axe prompt always on P1's side; time_in_lead is not kept.
- Pro face-off and battle use the guitar part for both players [C menus set
  part = guitar].

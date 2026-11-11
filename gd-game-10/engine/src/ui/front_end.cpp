// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/front_end.h"
#include "ui/natives_gh4.h"
#include "data/disc.h"

#include <string_view>

#include "audio/sound_buss.h"

#include <SDL3/SDL_log.h>

#include "data/qbkey.h"

#include <string>
#include "render/canvas.h"
#include "script/globals.h"
#include "script/natives_core.h"
#include "game/save.h"
#include "script/natives_misc.h"
#include "ui/glitches.h"
#include "ui/menu_scene.h"
#include "ui/natives_game.h"
#include "ui/natives_sound.h"

namespace {
constexpr double kStepMs = 1000.0 / 60.0;
}

FrontEnd::FrontEnd(const Disc& disc, Globals& globals, TextureCache& textures, gfx::RenderDevice& device)
    : disc_(disc), globals_(globals) {
    vm_ = std::make_unique<qs::Vm>(globals_);
    qs::addCoreNatives(*vm_);
    addMiscNatives(*vm_);
    ui_ = std::make_unique<ui::UiSystem>(*vm_, disc_, textures, device);
    ui_->addNatives();
    addGameNatives(*this, disc_, textures);
    flow.saveExists = save::exists();
    addFlowNatives(*vm_, flow);
    installGlitchesStore(*vm_);
    std::string err;
    if (!sfx.open(err)) SDL_Log("sfx: %s", err.c_str());
    sfx.addPak(disc_, "DATA/COMPRESSED/ZONES/global_sfx.pak.xen");
    sfx.addFsb(disc_, "DATA/STREAMS/streamall.fsb.xen", "DATA/STREAMS/streamall.dat.xen");
    addSoundNatives(*vm_, sfx, disc_);
    ui_->addMovieNatives();  // after the game natives' movie stubs
    scene_ = std::make_unique<MenuScene>(disc_, globals_, *vm_, *ui_);
    scene_->addNatives();  // after the flow natives' stubs (PlayIGCCam, KillCamAnim)
    addGh4Natives(*vm_);
    vm_->songAvailable = [this](const std::string& name) { return disc_.exists("DATA/MUSIC/" + name + "_1.fsb.xen"); };
    states = std::make_unique<UiStates>(*vm_);
    states->focus = [this](uint32_t id) { return ui_->focusElement(id); };
    states->addNatives();  // last: the `ui` object's members before the composite objects'
}

void FrontEnd::setZone(uint32_t zone) {
    if (scene_) scene_->setZone(zone);
}

FrontEnd::~FrontEnd() {
    // GD10_UISTATS=1: where the front end stood (scripts running, elements).
    if (const char* d = SDL_getenv("GD10_UIDUMP"); d && *d && *d != '0') ui_->dump();
    if (const char* v = SDL_getenv("GD10_UISTATS"); v && *v && *v != '0') {
        const auto& st = ui_->stats();
        SDL_Log("ui: %llu elements (%llu created, %llu destroyed), %llu events, %llu handlers, %llu sprites / %llu texts drawn, "
                "%llu missing textures",
                (unsigned long long)ui_->count(), (unsigned long long)st.created, (unsigned long long)st.destroyed, (unsigned long long)st.events,
                (unsigned long long)st.handlersRun, (unsigned long long)st.spritesDrawn, (unsigned long long)st.textDrawn,
                (unsigned long long)st.missingTextures);
        for (auto& r : vm_->runs()) {
            std::string stack;
            for (auto& f : r->frames) stack += " > " + qbkey::name(f.code->name) + "@" + std::to_string(f.pc);
            SDL_Log("ui: running %s%s (wait %d frames, until %.0f, waitFor %d)", qbkey::name(r->name).c_str(), stack.c_str(), r->waitFrames,
                    r->waitUntilMs, r->waitFor ? 1 : 0);
        }
    }
}

bool FrontEnd::start() { return vm_->run(qbkey::key("guitar_startup"), qb::Struct{}); }

void FrontEnd::handle(const InputEvent& e) {
    // The held frets as GH3's patterns (green 0x10000 .. orange 0x1) for GetHeldPattern.
    if (isFret(e.action)) {
        const uint32_t bit = 0x10000u >> (4 * fretOf(e.action));
        heldPattern = e.down ? (heldPattern | bit) : (heldPattern & ~bit);
    }
    if (!e.down) return;
    ++pad.makes;
    pad.device = e.player;
    // block_menu_input: no menu events until unblock_menu_input (a choose
    // whose work waits, ui_select_practice_mode_choose: a second press chose
    // TRAINING again and unfocused the tutorials list, GD10-020) [C scripts].
    if (flow.menuInputBlocked) return;
    const char* ev = nullptr;
    switch (e.action) {
    case Action::Up: ev = "pad_up"; break;
    case Action::Down: ev = "pad_down"; break;
    case Action::Left: ev = "pad_left"; break;
    case Action::Right: ev = "pad_right"; break;
    // The keyboard's Enter (Start, bound) is select in the menus and Start on
    // a text entry, where it finishes the name, as in gd02 / gd03 (the
    // owner's rule: Enter is Start; GH3's menus select with pad_choose only,
    // their pad_start shows a gamercard) [C scripts; UiSystem::startFinishesEntry].
    case Action::Confirm: ev = e.enterKey && ui_->startFinishesEntry(e.player) ? "pad_start" : "pad_choose"; break;
    case Action::Back: ev = "pad_back"; break;
    case Action::Start: ev = "pad_start"; break;
    case Action::Fret0: ev = "pad_choose"; break;  // green [I: GH3's button mapping, docs/ui.md]
    case Action::Fret1: ev = "pad_back"; break;    // red
    // Yellow / blue / orange: the 360 guitar's Y / X / LB, which xenon_pad
    // makes pad_option2 / pad_option / pad_l1 (the set list's SETLIST /
    // BONUS / DOWNLOADS tabs: setlist_event_handlers, setlist_show_helperbar's
    // \b6 \b7 \b8) [C data menubuttonremap.qb; I: only these three of the
    // events each button raises (pad_square, pad_space, ... have no menu
    // handler outside debug and online menus)]. GD10-090.
    case Action::Fret2: ev = "pad_option2"; break;
    case Action::Fret3: ev = "pad_option"; break;
    case Action::Fret4: ev = "pad_l1"; break;
    default: break;
    }
    // gh4: one button raises every event xenon_pad maps it to (A: pad_choose
    // and pad_btn_bottom; B: pad_back, pad_circle, pad_btn_right; ...), the
    // way SetButtonEventMappings {xenon = xenon_pad} sets them [C data
    // menubuttonremap.qb; I: the C dispatch order]. Start keeps pad_start.
    const char* button = nullptr;
    switch (e.action) {
    case Action::Up: button = "up"; break;
    case Action::Down: button = "down"; break;
    case Action::Left: button = "left"; break;
    case Action::Right: button = "right"; break;
    case Action::Confirm:
    case Action::Fret0: button = std::string_view(ev ? ev : "") == "pad_start" ? nullptr : "a"; break;
    case Action::Back:
    case Action::Fret1: button = "b"; break;
    case Action::Fret2: button = "y"; break;
    case Action::Fret3: button = "x"; break;
    case Action::Fret4: button = "left_trigger1"; break;
    default: break;
    }
    const qb::Value* map = button ? vm_->global(qbkey::key("xenon_pad")) : nullptr;
    if (map && map->arr) {
        const uint32_t b = qbkey::key(button);
        bool any = false;
        for (const qb::Value& pair : map->arr->values) {
            if (!pair.arr || pair.arr->values.size() != 2 || pair.arr->values[0].key != b) continue;
            ui_->padEvent(pair.arr->values[1].key, e.player);
            any = true;
        }
        if (any) return;
    }
    if (ev) ui_->padEvent(qbkey::key(ev), e.player);
}

bool FrontEnd::attractMode() const {
    const qb::Value* a = vm_->global(qbkey::key("is_attract_mode"));
    return a && a->type == qb::Type::Int && a->i == 1;
}

void FrontEnd::startPressed() {
    // Start in a song: gh4's gameplay state gives root_window the pad_start
    // handler handle_pause_event, which spawns gh3_start_pressed with
    // from_handler: only then does pausegh3 open uistate_pausemenu [C scripts
    // guitar_pause.qb, ui_gameplay]. GH3's discs: gh3_start_pressed itself.
    const uint32_t h = qbkey::key("handle_pause_event");
    if (const qb::Value* g = vm_->global(h); g && g->type == qb::Type::Script) vm_->spawn(h, qb::Struct{});
    else vm_->spawn(qbkey::key("gh3_start_pressed"), qb::Struct{});
}

void FrontEnd::songEnded(const SongResult& r) {
    songPlaying = false;
    using qs::Vm;
    // playerN_status as GH3's gem scroller leaves it [C data: the fields
    // GuitarEvent_SongWon_Spawned, Progression_SongWon and the newspaper
    // read]. stars: GH3's Progression_SongWon computes them from score and
    // base_score [C script; agents/scratch/stars_spec.md].
    auto fill = [&](const char* name, const SongResultPlayer& p, int finalBlow, int cards) {
        const uint32_t ps = qbkey::key(name);
        const qb::Value* cur = vm_->global(ps);
        if (!cur || !cur->st) return;
        qb::Value v = *cur;
        v.st = std::make_shared<qb::Struct>(*cur->st);
        Vm::set(*v.st, qbkey::key("score"), Vm::makeFloat(float(p.score)));
        Vm::set(*v.st, qbkey::key("notes_hit"), Vm::makeInt(p.notesHit));
        Vm::set(*v.st, qbkey::key("total_notes"), Vm::makeInt(p.totalNotes));
        Vm::set(*v.st, qbkey::key("max_notes"), Vm::makeInt(p.maxNotes));
        Vm::set(*v.st, qbkey::key("best_run"), Vm::makeInt(p.bestRun));
        Vm::set(*v.st, qbkey::key("base_score"), Vm::makeFloat(float(p.baseScore)));
        Vm::set(*v.st, qbkey::key("sp_phrases_hit"), Vm::makeInt(p.phrasesHit));
        Vm::set(*v.st, qbkey::key("sp_phrases_total"), Vm::makeInt(p.phrasesTotal));
        Vm::set(*v.st, qbkey::key("current_health"), Vm::makeFloat(p.health));
        Vm::set(*v.st, qbkey::key("star_power_amount"), Vm::makeFloat(p.starPower));
        if (r.twoPlayers) {
            Vm::set(*v.st, qbkey::key("final_blow_powerup"), Vm::makeInt(finalBlow));
            Vm::set(*v.st, qbkey::key("current_num_powerups"), Vm::makeInt(cards));
        }
        vm_->setGlobal(ps, v);
    };
    SongResultPlayer p1;
    p1.score = r.score, p1.notesHit = r.notesHit, p1.totalNotes = r.totalNotes, p1.bestRun = r.bestRun, p1.phrasesHit = r.phrasesHit;
    p1.phrasesTotal = r.phrasesTotal, p1.baseScore = r.baseScore, p1.health = r.health, p1.starPower = r.starPower, p1.maxNotes = r.maxNotes;
    fill("player1_status", p1, r.finalBlow[0], int(r.cards[0].size()));
    if (r.twoPlayers) {
        fill("player2_status", r.p2, r.finalBlow[1], int(r.cards[1].size()));
        // The hands (current_powerups_p1 / _p2, three slots) [C data guitar_battle.qb].
        for (int p = 0; p < 2; ++p) {
            qb::Value a;
            a.type = qb::Type::Array;
            a.arr = std::make_shared<qb::Array>();
            for (size_t i = 0; i < 3; ++i) a.arr->values.push_back(Vm::makeInt(i < r.cards[size_t(p)].size() ? r.cards[size_t(p)][i] : 0));
            vm_->setGlobal(qbkey::key(p ? "current_powerups_p2" : "current_powerups_p1"), a);
        }
    }
    // GH3's own end of song: You Rock / Song Failed, progression (scores,
    // stars, unlocks, encores, bosses), then the flow's action [C scripts
    // GuitarEvent_SongWon / GuitarEvent_SongFailed]. A battle KO goes the
    // way the C code sends it, song_failed (the script then calls
    // GuitarEvent_SongWon battle_win = 1); a battle at the song's end,
    // SongWon (sudden death) [C 0x8210FD18, GuitarEvent_SongFailed].
    vm_->spawn(qbkey::key(r.won && !r.battleKo ? "GuitarEvent_SongWon" : "GuitarEvent_SongFailed"), qb::Struct{});
}

void FrontEnd::frame(Canvas& c, double dtMs) {
    soundBusses().update(dtMs);  // the busses' fades (SetSoundBussParams time = s)
    sfx.update();
    if (!songPlaying && !songOutro && !flow.paused) songClockMs += dtMs;

    // GD10_FLOWLOG=1: each flow state player 1 enters (ui_flow_manager_state[0]).
    static const bool flowLog = SDL_getenv("GD10_FLOWLOG") != nullptr;
    if (flowLog) {
        const uint32_t st = states ? states->top() : 0;
        if (st != lastFlowState_) lastFlowState_ = st, SDL_Log("flow: %s", qbkey::name(st).c_str());
    }
    // The Glitches, once the boot has loaded or created the save (the first main menu).
    if (!glitchesDone_) {
        // gh4 (D10): the original's own unlock-all scripts (GlobalTags_UnlockAll,
        // unlock_all_profiles) stand for The Glitches until its band is built.
        if (states && states->top() == qbkey::key("uistate_mainmenu")) {
            glitchesDone_ = true;
            if (!SDL_getenv("GD10_NO_GLITCHES")) {  // (development aid: off)
                qb::Struct p;
                qs::Vm::set(p, qbkey::key("songs_only"), qs::Vm::makeInt(0));
                vm_->spawn(qbkey::key("unlock_all_profiles"), qb::Struct{});
                vm_->spawn(qbkey::key("purchase_all_profiles"), qb::Struct{});
                // Its band is named The Glitches (current_band_has_band_name
                // reads band_info.name) [C script; the name is the port's].
                qb::Struct& bi = vm_->globalTags[qbkey::key("band_info")];
                const qb::Value* nm = qs::Vm::param(bi, qbkey::key("name"));
                if (!nm || nm->str.empty()) qs::Vm::set(bi, qbkey::key("name"), qs::Vm::makeString("The Glitches"));
                SDL_Log("The Glitches (gh4): unlock_all_profiles, purchase_all_profiles");
            }
        }
    }
    // During a song GH3's scripts keep running (the attract mode's input
    // check, the end-of-song events) and their elements draw over it [C: one VM].
    scene_->setSuspended(songPlaying || songOutro);  // the menus' characters wait while the song's band plays
    accum_ += dtMs;
    int steps = 0;
    // Practice's setslomo: the scripts' clock (Wait n seconds: the intro's
    // sound waits, the song's events) runs at the song's speed while it
    // plays [C script start_gem_scroller; I: GH3's script waits on game time].
    const double vmStep = songPlaying && !songPaused ? kStepMs * double(songRequest.speed) : kStepMs;
    while (accum_ >= kStepMs && steps < 8) {
        accum_ -= kStepMs;
        states->update();
        vm_->update(vmStep);
        ui_->update(kStepMs);
        scene_->update(kStepMs);
        ++steps;
    }
    if (accum_ > kStepMs * 8) accum_ = 0;
    if (!songPlaying && !songOutro) scene_->draw(c);
    ui_->draw(c);
}

std::string FrontEnd::flowState() const {
    // gh4: the UI state machine's top state (ui/ui_states.h); the main menu
    // and the options keep GH3's names for the port's code shared with gd04
    // (help-bar items, Esc on the main menu) (D5). Metallica's
    // ui_flow_manager_state is a leftover constant, not read.
    const uint32_t top = states ? states->top() : 0;
    if (!top) return "";
    if (top == qbkey::key("uistate_mainmenu")) return "main_menu_fs";
    if (top == qbkey::key("uistate_options")) return "options_select_option_fs";
    return qbkey::name(top);
}

void FrontEnd::soundEvent(const char* event) {
    qb::Struct p;
    qs::Vm::set(p, qbkey::key("event"), qs::Vm::makeKey(qbkey::key(event)));
    vm_->run(qbkey::key("SoundEvent"), p);
}

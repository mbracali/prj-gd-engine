// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/front_end.h"

#include <SDL3/SDL_log.h>

#include "data/qbkey.h"

#include <string>
#include "render/canvas.h"
#include "script/globals.h"
#include "script/natives_core.h"
#include "game/save.h"
#include "ui/glitches.h"
#include "ui/natives_game.h"
#include "ui/natives_sound.h"

namespace {
constexpr double kStepMs = 1000.0 / 60.0;
}

FrontEnd::FrontEnd(const Disc& disc, Globals& globals, TextureCache& textures, gfx::RenderDevice& device)
    : disc_(disc), globals_(globals) {
    vm_ = std::make_unique<qs::Vm>(globals_);
    qs::addCoreNatives(*vm_);
    ui_ = std::make_unique<ui::UiSystem>(*vm_, disc_, textures, device);
    ui_->addNatives();
    addGameNatives(*this, disc_, textures);
    flow.saveExists = save::exists();
    addFlowNatives(*vm_, flow);
    std::string err;
    if (!sfx.open(err)) SDL_Log("sfx: %s", err.c_str());
    sfx.addPak(disc_, "DATA/COMPRESSED/ZONES/global_sfx.pak.xen");
    sfx.addFsb(disc_, "DATA/STREAMS/streamall.fsb.xen", "DATA/STREAMS/streamall.dat.xen");
    addSoundNatives(*vm_, sfx);
}

FrontEnd::~FrontEnd() {
    // GD04_UISTATS=1: where the front end stood (scripts running, elements).
    if (const char* d = SDL_getenv("GD04_UIDUMP"); d && *d && *d != '0') ui_->dump();
    if (const char* v = SDL_getenv("GD04_UISTATS"); v && *v && *v != '0') {
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
    const char* ev = nullptr;
    switch (e.action) {
    case Action::Up: ev = "pad_up"; break;
    case Action::Down: ev = "pad_down"; break;
    case Action::Left: ev = "pad_left"; break;
    case Action::Right: ev = "pad_right"; break;
    case Action::Confirm: ev = "pad_choose"; break;
    case Action::Back: ev = "pad_back"; break;
    case Action::Start: ev = "pad_start"; break;
    case Action::Fret0: ev = "pad_choose"; break;  // green [I: GH3's button mapping, docs/ui.md]
    case Action::Fret1: ev = "pad_back"; break;    // red
    default: break;
    }
    if (ev) ui_->padEvent(qbkey::key(ev), e.player);
}

bool FrontEnd::attractMode() const {
    const qb::Value* a = vm_->global(qbkey::key("is_attract_mode"));
    return a && a->type == qb::Type::Int && a->i == 1;
}

void FrontEnd::startPressed() {
    // Start in a song: GH3's gh3_start_pressed (the pause menu's flow action) [C script guitar_pause.qb].
    vm_->spawn(qbkey::key("gh3_start_pressed"), qb::Struct{});
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
    sfx.update();
    if (!songPlaying && !flow.paused) songClockMs += dtMs;
    // GD04_FLOWLOG=1: each flow state player 1 enters (ui_flow_manager_state[0]).
    static const bool flowLog = SDL_getenv("GD04_FLOWLOG") != nullptr;
    if (flowLog) {
        const qb::Value* a = vm_->global(qbkey::key("ui_flow_manager_state"));
        const uint32_t st = a && a->arr && !a->arr->values.empty() ? a->arr->values[0].key : 0;
        if (st != lastFlowState_) lastFlowState_ = st, SDL_Log("flow: %s", qbkey::name(st).c_str());
    }
    // The Glitches, once the boot has loaded or created the save (the first main menu).
    if (!glitchesDone_) {
        const qb::Value* a = vm_->global(qbkey::key("ui_flow_manager_state"));
        if (a && a->arr && !a->arr->values.empty() && a->arr->values[0].key == qbkey::key("main_menu_fs"))
            glitchesDone_ = true, (SDL_getenv("GD04_NO_GLITCHES") ? false : ensureGlitches(*vm_));  // (development aid: off)
    }
    // During a song GH3's scripts keep running (the attract mode's input
    // check, the end-of-song events) and their elements draw over it [C: one VM].
    accum_ += dtMs;
    int steps = 0;
    while (accum_ >= kStepMs && steps < 8) {
        accum_ -= kStepMs;
        vm_->update(kStepMs);
        ui_->update(kStepMs);
        ++steps;
    }
    if (accum_ > kStepMs * 8) accum_ = 0;
    ui_->draw(c);
}

std::string FrontEnd::flowState() const {
    const qb::Value* a = vm_->global(qbkey::key("ui_flow_manager_state"));
    if (!a || a->type != qb::Type::Array || !a->arr || a->arr->values.empty()) return "";
    return qbkey::name(a->arr->values[0].key);
}

void FrontEnd::soundEvent(const char* event) {
    qb::Struct p;
    qs::Vm::set(p, qbkey::key("event"), qs::Vm::makeKey(qbkey::key(event)));
    vm_->run(qbkey::key("SoundEvent"), p);
}

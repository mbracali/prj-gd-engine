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

void FrontEnd::songEnded(const SongResult& r) {
    songPlaying = false;
    // player1_status as GH3's gem scroller leaves it [C data: the fields
    // GuitarEvent_SongWon_Spawned and Progression_SongWon read]; stars by the
    // score over the base score [I: thresholds, OPEN_QUESTIONS].
    const uint32_t ps = qbkey::key("player1_status");
    if (const qb::Value* cur = vm_->global(ps); cur && cur->st) {
        qb::Value v = *cur;
        v.st = std::make_shared<qb::Struct>(*cur->st);
        const double ratio = r.baseScore > 0 ? double(r.score) / r.baseScore : 0;
        const int stars = ratio >= 2.8 ? 5 : ratio >= 2.0 ? 4 : 3;
        using qs::Vm;
        Vm::set(*v.st, qbkey::key("score"), Vm::makeFloat(float(r.score)));
        Vm::set(*v.st, qbkey::key("notes_hit"), Vm::makeInt(r.notesHit));
        Vm::set(*v.st, qbkey::key("total_notes"), Vm::makeInt(r.totalNotes));
        Vm::set(*v.st, qbkey::key("max_notes"), Vm::makeInt(r.totalNotes));
        Vm::set(*v.st, qbkey::key("best_run"), Vm::makeInt(r.bestRun));
        Vm::set(*v.st, qbkey::key("base_score"), Vm::makeFloat(float(r.baseScore)));
        Vm::set(*v.st, qbkey::key("stars"), Vm::makeInt(r.won ? stars : 0));
        Vm::set(*v.st, qbkey::key("sp_phrases_hit"), Vm::makeInt(r.phrasesHit));
        Vm::set(*v.st, qbkey::key("sp_phrases_total"), Vm::makeInt(r.phrasesTotal));
        Vm::set(*v.st, qbkey::key("current_health"), Vm::makeFloat(r.health));
        Vm::set(*v.st, qbkey::key("star_power_amount"), Vm::makeFloat(r.starPower));
        vm_->setGlobal(ps, v);
    }
    // GH3's own end of song: You Rock / Song Failed, progression (scores,
    // stars, unlocks, encores, bosses), then the flow's action [C scripts
    // GuitarEvent_SongWon / GuitarEvent_SongFailed].
    vm_->spawn(qbkey::key(r.won ? "GuitarEvent_SongWon" : "GuitarEvent_SongFailed"), qb::Struct{});
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
            glitchesDone_ = true, ensureGlitches(*vm_);
    }
    if (songPlaying) return;
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

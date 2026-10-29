// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/front_end.h"

#include <SDL3/SDL_log.h>

#include "data/qbkey.h"

#include <string>
#include "render/canvas.h"
#include "script/globals.h"
#include "script/natives_core.h"
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
        SDL_Log("ui: %zu elements (%llu created, %llu destroyed), %llu events, %llu handlers, %llu sprites / %llu texts drawn, "
                "%llu missing textures",
                ui_->count(), (unsigned long long)st.created, (unsigned long long)st.destroyed, (unsigned long long)st.events,
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

void FrontEnd::songEnded(bool won) {
    songPlaying = false;
    qb::Struct p;
    qs::Vm::set(p, qbkey::key("action"), qs::Vm::makeKey(qbkey::key(won ? "win_song" : "fail_song")));
    vm_->spawn(qbkey::key("ui_flow_manager_respond_to_action"), p);
}

void FrontEnd::frame(Canvas& c, double dtMs) {
    sfx.update();
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

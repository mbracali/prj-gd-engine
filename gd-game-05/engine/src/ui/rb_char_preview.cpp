// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/rb_char_preview.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>

#include "char/char_driver.h"
#include "char/char_objects.h"
#include "char/character.h"
#include "char/deform.h"
#include "game/band.h"
#include "render/band_view.h"
#include "render/canvas.h"
#include "render/renderer.h"
#include "render/ui_view.h"
#include "ui/ui_manager.h"

namespace ui {

int RbCharPreview::total_ = 0;
bool RbCharPreview::hidden = false;

RbCharPreview::RbCharPreview() = default;
RbCharPreview::~RbCharPreview() = default;

void RbCharPreview::clear() {
    ch_.reset();
    has_ = false;
}

bool RbCharPreview::set(UiManager& ui, const Look& look) {
    if (has_ && look == look_) return ch_ != nullptr;
    look_ = look, has_ = true, ch_.reset();
    const std::string inst = look.instrument == "bass" ? "guitar" : look.instrument;
    const std::string base = "char/" + look.gender + "/gen/" + look.gender + "_" + inst + ".milo_xbox";
    std::vector<std::pair<std::string, std::string>> pieces(look.outfit.begin(), look.outfit.end());
    // The drum kit: the small club's [I: RB's practice space shows no kit].
    const auto outfits = Band::rbOutfitMilos(ui.data(), look.gender, look.instrument, pieces, "small_club");
    try {
        if (!clips_) clips_ = std::make_unique<chr::ClipLibrary>(ui.data());
        ch_ = std::make_unique<chr::Character>(ui.data(), base, *clips_, outfits);
    } catch (const std::exception& e) {
        SDL_Log("char preview: %s: %s", base.c_str(), e.what());
        return false;
    }
    Band::rbSkin(ui.data(), *ch_, nullptr, look.skin);
    chr::applyDeform(*ch_, look.weight, look.height);
    // The main driver's stand group (a drummer's sit), a clip with the
    // attitude's flag if one has it [I: the shell's attitude groups].
    if (chr::ClipDriver* d = ch_->mainDriver(); d && d->clipSet())
        if (scene::CharClipGroup* g = d->clipSet()->group(inst == "drum" ? "sit" : "stand")) {
            scene::CharClipSamples* clip = look.attitude ? d->clipSet()->nextMatching(*g, look.attitude) : nullptr;
            if (!clip) clip = d->clipSet()->next(*g);
            if (clip) d->playClip(clip, 0);
        }
    if (inst == "vocals") ch_->setDrawOnly("hands.grp", "mic.grp");
    return true;
}

void RbCharPreview::poll(double dtMs) {
    if (!ch_) return;
    const float dt = float(std::clamp(dtMs, 0.0, 100.0) / 1000.0);
    clock_ += dt;
    chr::RigContext ctx;
    ctx.beat = clock_ * 2.0, ctx.dBeats = dt * 2.0f, ctx.dSeconds = dt, ctx.seconds = clock_;
    ch_->poll(ctx);
}

int RbCharPreview::draw(UiManager& ui, gfx::UiView& v, const scene::Xfm& at) {
    if (!ch_ || hidden || ui.headless() || !ui.sceneCache()) return 0;
    if (!view_) view_ = std::make_unique<BandView>(*ui.sceneCache());
    ch_->setXfm(at);
    gfx::Renderer& r = ui.canvas().renderer();
    ui.canvas().flush();
    int vx, vy, vw, vh;
    r.pictureRect(vx, vy, vw, vh);
    r.materials().setFrame(v.camera(), v.environment(), vw, vh);
    view_->resetStats();
    view_->drawCharacter(r.materials(), *ch_, v.camera(), false);
    ui.keepDepthOnce();
    total_ += view_->meshesDrawn();
    return view_->meshesDrawn();
}

}  // namespace ui

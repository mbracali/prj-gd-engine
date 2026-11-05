// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// GH3's texture-slot movies: videos the screen elements draw while the game
// goes on (the loading screen's loading_flying, the win / loss videos, the
// end credits), unlike PlayMovie's modal boot movies (textureslot 0, played
// by the app, ui/natives_game.cpp).
#include <SDL3/SDL_log.h>

#include <string>

#include "data/qbkey.h"
#include "script/vm.h"
#include "ui/ui_movies.h"

namespace ui {

namespace {

uint32_t K(const char* s) { return qbkey::key(s); }

int slotOf(const qb::Struct& p) { return int(qs::Vm::number(qs::Vm::param(p, K("textureslot")), 1)); }

}  // namespace

gfx::TextureId UiSystem::movieTexture(uint32_t key) const {
    for (const auto& [n, s] : movies_)
        if (s->el.texture == key) return s->tex;
    return {};
}

void UiSystem::updateMovies(double dtMs) {
    for (auto it = movies_.begin(); it != movies_.end();) {
        MovieSlot& s = *it->second;
        if (s.started && !s.paused) s.ms += dtMs;
        // no_hold: gone at its end; else its last picture stays [I: the names].
        if (s.started && !s.loop && !s.hold && s.player->streamEnded()) it = movies_.erase(it);
        else ++it;
    }
}

void UiSystem::moviesToDraw(std::vector<const Element*>& out) {
    for (auto& [n, sp] : movies_) {
        MovieSlot& s = *sp;
        if (!s.started) continue;
        int w = 0, h = 0;
        if (s.player->takeFrame(s.ms, s.rgba, w, h)) {
            if (!s.tex || w != s.w || h != s.h) {
                if (s.tex) dev_.destroyTexture(s.tex);
                std::string error;
                const gfx::TexLevel level{s.rgba.data(), s.rgba.size()};
                s.tex = dev_.createTexture({w, h, 1, gfx::TexFormat::RGBA8, gfx::TexKind::Tex2D}, &level, error);
                if (!s.tex) SDL_Log("movie %s: %s", s.name.c_str(), error.c_str());
                s.w = w, s.h = h;
                // Fitted to the 1280 x 720 screen (a few videos are 1280 x 780) [I].
                s.el.w = float(w), s.el.h = float(h), s.el.sx = 1280.0f / float(w), s.el.sy = 720.0f / float(h);
            } else {
                dev_.updateTexture(s.tex, s.rgba.data(), size_t(w) * 4);
            }
        }
        if (!s.tex) continue;
        s.rendered = true;
        out.push_back(&s.el);
    }
}

void UiSystem::addMovieNatives() {
    using qs::ScriptRun;
    using qs::Vm;
    // A slot's movie, opened (not started): its z is texturepri; looping
    // unless no_looping / no_loop; held at its end unless no_hold [I: the
    // flags' names; C scripts].
    auto open = [this](const std::string& name, int slot, const qb::Struct& p) -> MovieSlot* {
        auto s = std::make_unique<MovieSlot>();
        s->dev = &dev_;
        s->name = name;
        s->player = std::make_unique<MoviePlayer>();
        std::string error;
        if (!MoviePlayer::available() || !s->player->open(disc_, "DATA/MOVIES/BIK/" + name + ".bik.xen", error)) {
            if (!error.empty()) SDL_Log("movie %s: %s", name.c_str(), error.c_str());
            movies_.erase(slot);
            return nullptr;
        }
        s->loop = !Vm::flag(p, K("no_looping")) && !Vm::flag(p, K("no_loop"));
        s->hold = !Vm::flag(p, K("no_hold"));
        s->el.type = K("spriteelement");
        s->el.texture = K(("movie_slot_" + std::to_string(slot)).c_str());
        s->el.justX = s->el.justY = -1;
        s->el.z = Vm::number(Vm::param(p, K("texturepri")), 0), s->el.zGiven = true;
        MovieSlot* raw = s.get();
        movies_[slot] = std::move(s);
        return raw;
    };
    auto name = [](const qb::Value* v) {
        std::string n = v ? v->str : std::string();
        // 'movies\bik\loading_flying.bik.xen' -> loading_flying
        if (const size_t b = n.find_last_of("\\/"); b != std::string::npos) n = n.substr(b + 1);
        if (const size_t d = n.find('.'); d != std::string::npos) n = n.substr(0, d);
        return n;
    };
    vm_.addNative("LoadMovieIntoBuffer", [this, name](Vm&, ScriptRun&, qb::Struct& p) {
        movieBuffers_[int(Vm::number(Vm::param(p, K("buffer_slot")), 0))] = name(Vm::param(p, K("movie")));
        return true;
    });
    // PlayMovieFromBuffer {buffer_slot textureslot texturepri [no_hold]
    // [wait_until_rendered]}: plays the buffer's movie; wait_until_rendered
    // holds the script until its first picture is on the screen (the
    // loading screen is up before the song loads) [C create_loading_screen].
    vm_.addNative("PlayMovieFromBuffer", [this, open](Vm&, ScriptRun& run, qb::Struct& p) {
        auto b = movieBuffers_.find(int(Vm::number(Vm::param(p, K("buffer_slot")), 0)));
        if (b == movieBuffers_.end()) return false;
        const int slot = slotOf(p);
        MovieSlot* s = open(b->second, slot, p);
        if (!s || !s->player->startStream(s->loop)) return false;
        s->started = true;
        if (Vm::flag(p, K("wait_until_rendered"))) {
            run.waitFor = [this, slot] {
                auto it = movies_.find(slot);
                return it == movies_.end() || it->second->rendered;
            };
        }
        return true;
    });
    vm_.addNative("PreLoadMovie", [open](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* m = Vm::param(p, K("movie"));
        return m && open(m->str, slotOf(p), p) != nullptr;
    });
    vm_.addNative("IsMoviePreLoaded", [this](Vm&, ScriptRun&, qb::Struct& p) { return movies_.count(slotOf(p)) != 0; });
    vm_.addNative("StartPreLoadedMovie", [this](Vm&, ScriptRun&, qb::Struct& p) {
        auto it = movies_.find(slotOf(p));
        if (it == movies_.end() || it->second->started) return false;
        it->second->started = it->second->player->startStream(it->second->loop);
        return it->second->started;
    });
    // PlayMovie from PlayMovieAndWait {noblack} (create_main_menu's
    // GH3_Intro, the hands flipping the card over the main menu): no black
    // under it, the scripts go on (two frames in, fadetoblack OFF uncovers
    // the menu built just before), so it plays in its texture slot (0,
    // texturepri 1000) with its own alpha over the screen, and the wait loop
    // polls IsMoviePlaying (Start + A kills it) [C scripts PlayMovieAndWait,
    // create_main_menu; data: GH3_Intro is yuva420p, its first frame and
    // last five transparent]. Played modally and opaque, its transparent
    // frames had shown as black between the card and the menu. The other
    // movies stay the app's modal ones (ui/natives_game.cpp).
    {
        const qs::Native modal = vm_.native(K("PlayMovie"));
        vm_.addNative("PlayMovie", [this, open, modal](Vm& v, ScriptRun& run, qb::Struct& p) {
            const qb::Value* m = Vm::param(p, K("movie"));
            if (m && Vm::flag(p, K("noblack")) && MoviePlayer::available() && !SDL_getenv("GD04_NO_MOVIES")) {
                const int slot = int(Vm::number(Vm::param(p, K("textureslot")), 0));
                MovieSlot* s = open(m->str, slot, p);
                if (s && s->player->startStream(s->loop)) {
                    s->started = true;
                    return true;
                }
                movies_.erase(slot);
            }
            return modal ? modal(v, run, p) : false;
        });
    }
    // Slot 0 is otherwise PlayMovie's (modal, over by now): not playing here.
    vm_.addNative("IsMoviePlaying", [this](Vm&, ScriptRun&, qb::Struct& p) {
        auto it = movies_.find(slotOf(p));
        return it != movies_.end() && it->second->started && (it->second->loop || !it->second->player->streamEnded());
    });
    vm_.addNative("KillMovie", [this](Vm&, ScriptRun&, qb::Struct& p) {
        movies_.erase(slotOf(p));
        return true;
    });
    for (const char* n : {"PauseMovie", "ResumeMovie"}) {
        const bool pause = std::string(n) == "PauseMovie";
        vm_.addNative(n, [this, pause](Vm&, ScriptRun&, qb::Struct& p) {
            auto it = movies_.find(slotOf(p));
            if (it == movies_.end()) return false;
            it->second->paused = pause;
            return true;
        });
    }
}

}  // namespace ui

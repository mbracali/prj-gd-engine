// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/walktest.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "data/game_data.h"
#include "data/json.h"
#include "ui/natives.h"
#include "ui/ui_manager.h"

extern const unsigned char kUiStubsTxt[];
extern const std::size_t kUiStubsTxtSize;

namespace {

constexpr int kX = ui::kPad_X, kTri = ui::kPad_Tri, kStart = ui::kPad_Start, kUp = ui::kPad_DUp, kDown = ui::kPad_DDown;

// The STUB whitelist (tests/ui_stubs.txt): prefixes, `*` at the end matches anything.
std::vector<std::string> whitelist() {
    std::vector<std::string> out;
    std::istringstream in(std::string(reinterpret_cast<const char*>(kUiStubsTxt), kUiStubsTxtSize));
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] != '#') out.push_back(line);
    }
    return out;
}

bool whitelisted(const std::string& key, const std::vector<std::string>& wl) {
    for (const std::string& w : wl) {
        if (!w.empty() && w.back() == '*') {
            if (key.compare(0, w.size() - 1, w, 0, w.size() - 1) == 0) return true;
        } else if (key == w) {
            return true;
        }
    }
    return false;
}

}  // namespace

WalkDriver::WalkDriver(const std::string& scenario) : scenario_(scenario) {
    using S = Step;
    auto screen = [&](const char* s, double timeout = 20000) { steps_.push_back({S::Screen, s, 0, timeout}); };
    auto press = [&](int b) { steps_.push_back({S::Press, "", b, 0}); };
    auto wait = [&](double ms) { steps_.push_back({S::Wait, "", 0, ms}); };
    // Each screen: wait until it is up and its transition is over, then a moment.
    auto at = [&](const char* s, double timeout = 20000) {
        screen(s, timeout);
        wait(400);
    };
    auto quickplaySong = [&](int downs) {
        at("main_screen");
        press(kX);  // QUICK PLAY (main_quickspin.btn, the focus)
        at("qp_selsong_screen");
        for (int i = 0; i < downs; ++i) press(kDown), wait(150);
        press(kX);
        at("qp_diff_screen");
        press(kUp);  // Medium (the last difficulty) -> Easy
        wait(150);
        press(kX);
        at("loading_screen");
        at("game_screen", 30000);
    };
    if (scenario == "quickplay") {
        endMs_ = 50000;  // long enough for a high score (the table's lowest is 10000)
        at("splash_screen", 30000);
        press(kX);  // any button
        at("startup_create_save_screen");
        press(kX);  // choose a device (create the save)
        at("saving_screen");
        at("guitar_help_screen");
        press(kX);
        quickplaySong(3);
        at("endgame_screen", 90000);
        wait(600);
        press(kX);  // CONTINUE (me_continue.btn)
        at("highscore_screen");
        press(kStart);  // the name as it is
        wait(300);
        press(kX);
        at("complete_screen");
        press(kX);  // the song list (comp_selsong.btn)
        at("meta_loading_screen");
        at("saving_screen");
        at("qp_selsong_screen");
        press(kTri);  // back
        at("main_screen");
        expect_ = {"cut_scene_screen", "splash_screen", "startup_load_finder_screen", "startup_create_save_screen", "saving_screen",
                   "guitar_help_screen", "main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen",
                   "post_show_screen", "endgame_screen", "highscore_screen", "complete_screen", "meta_loading_screen", "saving_screen",
                   "qp_selsong_screen", "main_screen"};
    } else if (scenario == "lose") {
        autoplay_ = false;
        failMs_ = 3000;
        quickplaySong(0);
        at("lose_screen", 30000);
        press(kDown), wait(150), press(kDown), wait(150);  // QUIT (lose_quit_normal.btn)
        press(kX);
        at("meta_loading_screen");
        at("main_screen");
        expect_ = {"main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen", "lose_screen",
                   "meta_loading_screen", "main_screen"};
    } else if (scenario == "pause") {
        quickplaySong(1);
        wait(2500);
        press(kStart);
        at("pause_screen");
        press(kDown);  // RESTART
        wait(150);
        press(kX);
        at("pause_confirm_restart_screen");
        press(kX);  // restart.btn
        at("game_screen");
        wait(2500);
        press(kStart);
        at("pause_screen");
        press(kUp);  // QUIT (up from RESUME)
        wait(150);
        press(kX);
        at("pause_confirm_quit_screen");
        press(kX);  // quit.btn
        at("meta_loading_screen");
        at("qp_selsong_screen");
        expect_ = {"main_screen", "qp_selsong_screen", "qp_diff_screen", "loading_screen", "game_screen", "pause_screen",
                   "pause_confirm_restart_screen", "game_screen", "pause_screen", "pause_confirm_quit_screen", "meta_loading_screen",
                   "qp_selsong_screen"};
    } else if (scenario == "career") {
        at("splash_screen", 30000);
        press(kX);
        at("startup_create_save_screen");
        press(kX);
        at("saving_screen");
        at("guitar_help_screen");
        press(kX);
        at("main_screen");
        press(kUp);  // CAREER (main_career.btn)
        wait(150);
        press(kX);
        at("nameprof_screen");
        press(kX);  // the band's name: "A"
        wait(150);
        press(kStart);
        at("sel_difficulty_screen");
        press(kX);  // Medium (the profile's last difficulty)
        at("sel_character_new_screen");
        wait(1500);  // the character opens the door
        press(kDown);  // Punk -> Alterna (Judy Nails)
        wait(300);
        press(kX);  // two outfits (alterna2 bought): pick one
        wait(800);
        press(kDown);  // outfit1.btn -> outfit2.btn (alterna2)
        wait(800);
        press(kX);
        at("sel_guitar_new_screen");
        press(kDown);  // Les Paul -> SG
        wait(300);
        press(kX);  // two skins (sg_white bought): pick one
        wait(600);
        press(kDown);  // sg_cherry -> sg_white
        wait(300);
        press(kX);
        at("tour_start_screen");
        press(kX);
        at("career_screen");
        expect_ = {"main_screen", "nameprof_screen", "sel_difficulty_screen", "sel_character_new_screen", "sel_guitar_new_screen",
                   "tour_start_screen", "saving_screen", "career_screen"};
    } else if (scenario == "attract") {
        endMs_ = 20000;
        at("splash_screen");
        at("loading_screen", 70000);  // 60 s idle on the title screen
        at("game_screen", 30000);
        wait(3000);
        press(kX);  // leave the attract mode: a 2 s fade, finish_fade, back to the title
        at("splash_screen", 15000);
        expect_ = {"splash_screen", "loading_screen", "game_screen", "finish_fade", "meta_loading_screen", "splash_screen"};
    }
}

std::string WalkDriver::startScreen() const {
    if (scenario_ == "lose" || scenario_ == "pause") return "main_screen";
    if (scenario_ == "attract") return "splash_screen";
    return "";
}

void WalkDriver::poll(ui::UiManager& ui, double nowMs) {
    if (done_) return;
    if (step_ >= steps_.size()) {
        done_ = true;
        return;
    }
    Step& s = steps_[step_];
    if (stepStart_ < 0) stepStart_ = nowMs;
    // career: the band has bought Judy Nails' second outfit and the SG's
    // white skin in the store (M10), so both choices offer two.
    if (scenario_ == "career" && !seeded_ && ui.currentScreen() == "main_screen") {
        seeded_ = true;
        ui.career().items["alterna2"] = true;
        ui.career().items["sg_white"] = true;
    }
    switch (s.kind) {
    case Step::Screen:
        if (ui.currentScreen() == s.screen && !ui.inTransition()) {
            SDL_Log("walk: %s (%.1f s)", s.screen.c_str(), nowMs / 1000);
            ++step_, stepStart_ = -1;
        } else if (nowMs - stepStart_ > s.ms) {
            error_ = "timed out waiting for " + s.screen + " (on " + ui.currentScreen() + ")";
            failed_ = done_ = true;
        }
        return;
    case Step::Press:
        ui.buttonDown(s.button, 0);
        ui.buttonUp(s.button, 0);
        ++step_, stepStart_ = -1;
        return;
    case Step::Wait:
    case Step::Hold:
        if (nowMs - stepStart_ >= s.ms) ++step_, stepStart_ = -1;
        return;
    }
}

int WalkDriver::finish(ui::UiManager& ui) {
    bool ok = !failed_ && done_;
    std::printf("walktest %s:\n", scenario_.c_str());
    if (!error_.empty()) std::printf("  FAIL %s\n", error_.c_str());
    if (!done_) std::printf("  FAIL the walk did not finish (stopped at step %zu of %zu)\n", step_, steps_.size());
    // The screens, in order.
    std::string seq;
    for (const std::string& s : ui.history()) seq += (seq.empty() ? "" : " > ") + s;
    std::printf("  screens: %s\n", seq.c_str());
    size_t k = 0;
    for (const std::string& s : ui.history())
        if (k < expect_.size() && s == expect_[k]) ++k;
    if (k < expect_.size()) {
        std::printf("  FAIL expected screen %s (in order) was not reached\n", expect_[k].c_str());
        ok = false;
    }
    // The save (quickplay): career.json with the song's high score.
    if (scenario_ == "quickplay") {
        std::ifstream f(ui.savePath());
        std::stringstream ss;
        ss << f.rdbuf();
        bool saved = false;
        try {
            const Json j = Json::parse(ss.str());
            // A score that is not one of the table's defaults (50000 .. 10000).
            for (const auto& [song, t] : j["highscores"].o)
                for (const Json& d : t.a)
                    for (const Json& e : d.a) {
                        const int sc = int(e["score"].num());
                        if (sc > 0 && (sc % 10000 != 0 || sc > 50000)) {
                            saved = true;
                            std::printf("  save: %s: %s %s %d\n", ui.savePath().c_str(), song.c_str(), e["name"].str().c_str(), sc);
                        }
                    }
        } catch (const std::exception& e) {
            std::printf("  save: %s: %s\n", ui.savePath().c_str(), e.what());
        }
        if (!saved) {
            std::printf("  FAIL career.json has no score of the song\n");
            ok = false;
        }
    }
    // The career's look: career.json's first band at Medium (campaign
    // set_character_info), and player 0's config back from it.
    if (scenario_ == "career") {
        std::ifstream f(ui.savePath());
        std::stringstream ss;
        ss << f.rdbuf();
        std::string got;
        try {
            const Json c = Json::parse(ss.str())["profiles"].a.at(0)["characters"].a.at(1);
            got = c["character"].str() + " " + c["outfit"].str() + " " + c["guitar"].str() + " " + c["skin"].str();
        } catch (const std::exception& e) {
            got = e.what();
        }
        const ui::Session::PlayerCfg& p = ui.session().players[0];
        const std::string cfg = p.character + " " + p.outfit + " " + p.guitar + " " + p.skin;
        const std::string want = "alterna alterna2 sg sg_white";
        std::printf("  look: career.json %s, player 0 %s\n", got.c_str(), cfg.c_str());
        if (got != want || cfg != want) std::printf("  FAIL the look is not %s\n", want.c_str()), ok = false;
    }
    // The endgame photo (quickplay): player 0's outfit, one of its four pictures, on the disc.
    if (scenario_ == "quickplay") {
        const auto& photos = ui.session().winnerPhotos;
        const std::string outfit = ui.session().players[0].outfit;
        const std::string p = photos.empty() ? std::string() : photos[0];
        const std::string stem = "image/og/photo_" + outfit;
        const bool shape = photos.size() == 1 && p.size() == stem.size() + 10 && p.compare(0, stem.size(), stem) == 0 &&
                           p[stem.size()] >= '0' && p[stem.size()] <= '3' && p.compare(stem.size() + 1, 9, "_keep.bmp") == 0;
        std::string ark = p;
        if (shape) ark = "ui/image/ng/gen/" + p.substr(9) + "_xbox";
        std::printf("  photo: %s (%s)\n", p.c_str(), ark.c_str());
        if (!shape || !ui.data().exists(ark)) std::printf("  FAIL the endgame photo is not one of %s's\n", outfit.c_str()), ok = false;
    }
    // The script reports.
    Interp& in = ui.interp();
    const auto wl = whitelist();
    for (const auto& [key, n] : in.unhandledCounts()) std::printf("  FAIL UNHANDLED %s (%d)\n", key.c_str(), n), ok = false;
    for (const auto& [key, n] : in.unknownCounts()) std::printf("  FAIL UNKNOWN %s (%d)\n", key.c_str(), n), ok = false;
    for (const auto& [key, n] : in.stubCounts()) {
        const bool w = whitelisted(key, wl);
        std::printf("  %s STUB %s (%d)\n", w ? "ok  " : "FAIL", key.c_str(), n);
        ok = ok && w;
    }
    for (const std::string& e : in.errors()) std::printf("  FAIL script error: %s\n", e.c_str()), ok = false;
    std::printf("walktest %s: %s\n", scenario_.c_str(), ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}

// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "app.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>

#include "app_ids.h"
#include "data/param_sfo.h"
#include "data/umd.h"
#include "devaids/dev_aids.h"
#include "platform/paths.h"

namespace gr = gd::core::render;

namespace {

// Our own screens' colours (dark, Rock Band Unplugged's orange as the accent).
constexpr Rgba kBg{18, 16, 22}, kPanel{30, 27, 36}, kRow{48, 42, 58}, kAccent{240, 140, 40}, kText{236, 228, 214},
    kDim{150, 142, 132}, kMuted{100, 94, 88};
constexpr float kListX = 32, kListY = 112, kRowH = 26;
constexpr int kRows = 13;  // visible song rows

bool envOn(const char* name) {
    const char* v = SDL_getenv(name);
    return v && std::strcmp(v, "1") == 0;
}

}  // namespace

App::App() = default;
App::~App() = default;

bool App::openWindow(std::string& error) {
    // OpenGL 3 through gd-engine-core (the launcher's window as it is); the
    // other core backends come with the real renderer.
    const gr::Backend backend = gr::Backend::OpenGL3;
    gr::prepareWindow(backend, false);
    SDL_WindowFlags flags = SDL_WindowFlags(gr::windowFlags(backend)) | SDL_WINDOW_RESIZABLE;
#ifdef __ANDROID__
    flags |= SDL_WINDOW_FULLSCREEN;
#endif
    window_ = SDL_CreateWindow(app::kWindowTitle, Canvas::kW, Canvas::kH, flags);
    if (!window_) {
        error = SDL_GetError();
        return false;
    }
    if (!canvas_.init(window_, backend, error)) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }
    applyScreenMode();
    return true;
}

// Enhanced > Screen mode. In RecompLauncher's Big Picture mode
// (GD24_FULLSCREEN=1 from the module entry, GD_HOST_FULLSCREEN) Windowed
// counts as Full Screen (No Border) for this run; the saved setting stays.
void App::applyScreenMode() {
    std::string m = settings_.get("screen_mode");
    if (envOn("GD24_FULLSCREEN") && m == "windowed") m = "borderless";
    if (m == "windowed") {
        SDL_SetWindowFullscreen(window_, false);
    } else {
        const SDL_DisplayMode* dm = m == "fullscreen" ? SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window_)) : nullptr;
        SDL_SetWindowFullscreenMode(window_, dm);
        SDL_SetWindowFullscreen(window_, true);
    }
}

void App::reloadSongs() {
    const std::string lang = settings_.get("language");
    songs_ = gd24::readSongList(*umd_, lang);
}

int App::run(const Options& opt) {
    opt_ = opt;
    std::string error;
    umd_ = gd24::Umd::open(opt.disc, &error);
    if (!umd_) {
        SDL_Log("gd24: cannot read the disc %s: %s", opt.disc.c_str(), error.c_str());
        return 1;
    }
    discId_ = umd_->discId();
    if (discId_ != app::kDiscId) SDL_Log("gd24: this is not a Rock Band Unplugged (USA) disc (disc ID %s)", discId_.c_str());
    std::vector<uint8_t> sfoBytes;
    gd24::ParamSfo sfo;
    if (umd_->read("PSP_GAME/PARAM.SFO", sfoBytes) && sfo.parse(sfoBytes, &error)) {
        title_ = sfo.get("TITLE"), discVersion_ = sfo.get("DISC_VERSION");
    } else {
        SDL_Log("gd24: PARAM.SFO: %s", error.c_str());
    }
    if (title_.empty()) title_ = "(no title on the disc)";
    settings_.load(prefDir());
    reloadSongs();
    SDL_Log("gd24 %s: disc %s \"%s\" %s, %zu songs", app::kVersion, discId_.c_str(), title_.c_str(), discVersion_.c_str(),
            songs_.size());

    SDL_SetAppMetadata(app::kDisplayName, app::kVersion, app::kAppId);
    Input::prepareSdl();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("gd24: cannot start SDL: %s", SDL_GetError());
        return 2;
    }
    if (!openWindow(error)) {
        SDL_Log("gd24: cannot open the window: %s", error.c_str());
        SDL_Quit();
        return 2;
    }
    if (!audio_.open(error)) SDL_Log("audio: %s (continuing without sound)", error.c_str());
    applyVolume();
    if (const char* v = SDL_getenv("GD24_VIRTUAL_PAD")) SDL_Log("input: virtual pad %s: id %u", v, unsigned(Input::attachVirtualPad(v)));
    DevAids aids;
    startNs_ = SDL_GetTicksNS();
    while (!quit_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        const uint64_t ms = (SDL_GetTicksNS() - startNs_) / 1000000;
        for (const SDL_Event& k : aids.keysDue(ms)) handleEvent(k);
        pollPreview();
        draw();
        aids.afterDraw(ms, canvas_);
        canvas_.present();
        if (aids.quitDue(ms)) quit_ = true;
    }
    if (decoding_.valid()) decoding_.wait();
    if (screen_ == Screen::Gamepads) gamepads_.exit();
    input_.close();
    audio_.close();
    canvas_.shutdown();
    SDL_DestroyWindow(window_);
    window_ = nullptr;
    SDL_Quit();
    return 0;
}

void App::handleEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_QUIT:
        quit_ = true;
        return;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT) {
            float x = 0, y = 0;
            canvas_.windowToCanvas(e.button.x, e.button.y, x, y);
            click(x, y);
        }
        return;
    case SDL_EVENT_MOUSE_WHEEL:
        if (screen_ == Screen::Status)
            scroll_ = std::clamp(scroll_ + (e.wheel.y < 0 ? 3 : -3), 0, std::max(0, int(songs_.size()) - kRows));
        return;
    default:
        break;
    }
    // Keys and controllers: the input layer's PSP buttons (input/input.h).
    events_.clear();
    input_.handle(e, events_);
    for (const InputEvent& ev : events_) {
        if (!ev.down) continue;
        if (SDL_getenv("GD24_INPUTLOG")) SDL_Log("input: %s", actionName(ev.action));
        switch (ev.action) {
        case Action::Up: menu(Menu::Up); break;
        case Action::Down: menu(Menu::Down); break;
        case Action::Left: menu(Menu::Left); break;
        case Action::Right: menu(Menu::Right); break;
        case Action::Cross: menu(Menu::Confirm); break;
        case Action::Circle: menu(Menu::Back); break;
        case Action::Start: menu(Menu::Start); break;
        case Action::Select: menu(Menu::Select); break;
        default: break;
        }
    }
}

void App::openGamepads() {
    screen_ = Screen::Gamepads;
    gamepads_.enter();
}

void App::menu(Menu m) {
    if (screen_ == Screen::Gamepads) {
        static const GamepadsScreen::Key kKeys[] = {GamepadsScreen::Key::Up, GamepadsScreen::Key::Down, GamepadsScreen::Key::Left,
                                                    GamepadsScreen::Key::Right, GamepadsScreen::Key::Confirm, GamepadsScreen::Key::Back};
        if (m == Menu::Select) return;
        if (m == Menu::Start) m = Menu::Confirm;  // Enter (the keyboard's Start) selects here
        if (!gamepads_.key(kKeys[int(m)])) gamepads_.exit(), screen_ = Screen::Status;
        return;
    }
    if (screen_ == Screen::Status) {
        const int n = int(songs_.size());
        if (m == Menu::Up && n) sel_ = (sel_ + n - 1) % n;
        else if (m == Menu::Down && n) sel_ = (sel_ + 1) % n;
        else if (m == Menu::Left && n) sel_ = std::max(0, sel_ - kRows);
        else if (m == Menu::Right && n) sel_ = std::min(n - 1, sel_ + kRows);
        else if (m == Menu::Start) screen_ = Screen::Enhanced, esel_ = 0;
        else if (m == Menu::Select) openGamepads();
        else if (m == Menu::Confirm && n) playPreview(sel_);
        else if (m == Menu::Back) audio_.stop(), playingSong_ = -1, status_.clear();
        if (sel_ < scroll_) scroll_ = sel_;
        if (sel_ >= scroll_ + kRows) scroll_ = sel_ - kRows + 1;
        return;
    }
    // ENHANCED: the options, then "Gamepads", "Back" and "Quit". Start
    // (the keyboard's Enter) selects as Cross does.
    if (m == Menu::Start) m = Menu::Confirm;
    const auto& opts = Settings::options();
    const int rows = int(opts.size()) + 3;
    if (m == Menu::Up) esel_ = (esel_ + rows - 1) % rows;
    else if (m == Menu::Down) esel_ = (esel_ + 1) % rows;
    else if ((m == Menu::Left || m == Menu::Right || m == Menu::Confirm) && esel_ < int(opts.size())) {
        const Settings::Option& o = opts[size_t(esel_)];
        settings_.cycle(o.key, m == Menu::Left ? -1 : 1);
        settings_.save();
        if (std::strcmp(o.key, "screen_mode") == 0) applyScreenMode();
        if (std::strcmp(o.key, "language") == 0) reloadSongs();
        if (std::strcmp(o.key, "volume") == 0) applyVolume();
    } else if (m == Menu::Confirm && esel_ == int(opts.size())) {
        openGamepads();
    } else if (m == Menu::Back || (m == Menu::Confirm && esel_ == int(opts.size()) + 1)) {
        screen_ = Screen::Status;
    } else if (m == Menu::Confirm && esel_ == int(opts.size()) + 2) {
        quit_ = true;
    }
}

void App::applyVolume() { audio_.setVolume(float(std::atoi(settings_.get("volume").c_str())) / 100.0f); }

// The song's <SONG>_PREVIEW.AT3 (the full .ATM audio is encrypted and out
// of scope, DECISIONS D6), decoded on a worker thread, then looped.
void App::playPreview(int song) {
    const gd24::SongEntry& s = songs_[size_t(song)];
    if (!s.hasPreview) {
        status_ = s.title + ": no preview on the disc";
        return;
    }
    if (decoding_.valid()) decoding_.wait();
    audio_.stop();
    playingSong_ = -1, decodingSong_ = song;
    status_ = "Loading the preview of " + s.title + "...";
    const gd24::Umd* umd = umd_.get();
    const std::string path = s.previewPath();
    decoding_ = std::async(std::launch::async, [umd, path]() -> std::shared_ptr<PcmClip> {
        std::vector<uint8_t> bytes;
        auto clip = std::make_shared<PcmClip>();
        std::string error;
        const uint64_t t0 = SDL_GetTicksNS();
        if (!umd->read(path, bytes) || !decodeAt3(bytes, *clip, error)) {
            SDL_Log("preview %s: %s", path.c_str(), error.empty() ? "cannot read" : error.c_str());
            return nullptr;
        }
        SDL_Log("preview %s: %zu samples (%.2f s), %d errors, decoded in %.0f ms", path.c_str(), clip->frames(), clip->seconds(),
                clip->errors, double(SDL_GetTicksNS() - t0) / 1e6);
        return clip;
    });
}

void App::pollPreview() {
    if (!decoding_.valid() || decoding_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    std::shared_ptr<PcmClip> clip = decoding_.get();
    const int song = decodingSong_;
    decodingSong_ = -1;
    if (song < 0 || song >= int(songs_.size())) return;
    if (!clip) {
        status_ = songs_[size_t(song)].title + ": the preview did not decode" + (at3Available() ? "" : " (no FFmpeg in this build)");
        return;
    }
    if (!audio_.isOpen()) {
        status_ = songs_[size_t(song)].title + ": no sound device";
        return;
    }
    audio_.play(clip, true);
    playingSong_ = song;
    char len[32];
    std::snprintf(len, sizeof len, " (%.0f s)", clip->seconds());
    status_ = "Playing the preview of " + songs_[size_t(song)].title + len;
}

void App::click(float x, float y) {
    if (screen_ == Screen::Gamepads) {
        if (!gamepads_.click(x, y)) gamepads_.exit(), screen_ = Screen::Status;
        return;
    }
    if (screen_ == Screen::Status) {
        if (x >= kListX && x < Canvas::kW - kListX && y >= kListY && y < kListY + kRows * kRowH) {
            const int row = scroll_ + int((y - kListY) / kRowH);
            if (row < int(songs_.size())) {
                sel_ = row;
                menu(Menu::Confirm);
            }
        } else if (y >= Canvas::kH - 40) {
            // The help bar: GAMEPADS and ENHANCED on the right.
            if (x > Canvas::kW * 0.75f) menu(Menu::Start);
            else if (x > Canvas::kW / 2) menu(Menu::Select);
        }
        return;
    }
    const int rows = int(Settings::options().size()) + 3;
    const int row = int((y - 120) / 40);
    if (y >= 120 && row < rows) {
        esel_ = row;
        menu(x > 520 ? Menu::Right : Menu::Confirm);
    }
}

void App::draw() {
    canvas_.clear(kBg);
    if (screen_ == Screen::Status) drawStatus();
    else if (screen_ == Screen::Gamepads) gamepads_.draw(canvas_);
    else drawEnhanced();
}

void App::drawHelp(const std::vector<std::pair<std::string, std::string>>& items) {
    canvas_.fill(0, Canvas::kH - 40, Canvas::kW, 40, kPanel);
    float x = 24;
    for (const auto& [key, what] : items) {
        x += canvas_.text(x, Canvas::kH - 28, key, kAccent, 2) + 8;
        x += canvas_.text(x, Canvas::kH - 28, what, kText, 2) + 28;
    }
}

std::string App::songCounts() const {
    size_t tutorials = 0;
    for (const gd24::SongEntry& s : songs_) tutorials += s.tutorial;
    return std::to_string(songs_.size() - tutorials) + " songs, " + std::to_string(tutorials) + " tutorial tracks";
}

void App::drawStatus() {
    canvas_.text(32, 20, title_, kAccent, 3);
    canvas_.text(32, 52, discId_ + "  v" + discVersion_ + "   " + songCounts(), kDim, 2);
    canvas_.text(32, 76, std::string(app::kProgram) + " " + app::kVersion + ": not playable yet (first boot in progress)", kMuted, 2);
    canvas_.fill(kListX - 8, kListY - 6, Canvas::kW - 2 * kListX + 16, kRows * kRowH + 12, kPanel);
    for (int i = 0; i < kRows && scroll_ + i < int(songs_.size()); ++i) {
        const int idx = scroll_ + i;
        const gd24::SongEntry& s = songs_[size_t(idx)];
        const float y = kListY + float(i) * kRowH;
        if (idx == sel_) canvas_.fill(kListX - 4, y - 2, Canvas::kW - 2 * kListX + 8, kRowH - 2, kRow);
        char num[16];
        std::snprintf(num, sizeof num, "%2d", idx + 1);
        canvas_.text(kListX, y + 4, num, kMuted, 2);
        std::string name = s.title;
        if (!s.artist.empty()) name += "  -  " + s.artist;
        const float folderX = Canvas::kW - kListX - Canvas::textWidth(s.folder, 2);
        const size_t fit = size_t((folderX - 16 - (kListX + 48)) / 16);
        if (name.size() > fit) name = name.substr(0, fit - 1) + ".";
        canvas_.text(kListX + 48, y + 4, name, idx == sel_ ? kAccent : kText, 2);
        canvas_.text(folderX, y + 4, s.folder, kMuted, 2);
    }
    if (!songs_.empty()) {
        const gd24::SongEntry& s = songs_[size_t(sel_)];
        canvas_.text(32, kListY + kRows * kRowH + 12, s.album.empty() ? s.folder : s.album, kDim, 2);
    }
    if (!status_.empty()) canvas_.text(32, kListY + kRows * kRowH + 34, status_, kText, 2);
    if (playingSong_ >= 0 && audio_.playing()) {
        char t[32];
        std::snprintf(t, sizeof t, "%5.1f s", audio_.positionSec());
        canvas_.text(Canvas::kW - 32 - Canvas::textWidth(t, 2), kListY + kRows * kRowH + 34, t, kDim, 2);
    }
    drawHelp({{"Confirm", "preview"}, {"Back", "stop"}, {"Select", "GAMEPADS"}, {"Start", "ENHANCED"}});
}

void App::drawEnhanced() {
    canvas_.text(32, 20, "ENHANCED", kAccent, 3);
    canvas_.text(32, 56, "gd24's settings (saved in enhanced.txt)", kDim, 2);
    const auto& opts = Settings::options();
    const int rows = int(opts.size()) + 3;
    for (int i = 0; i < rows; ++i) {
        const float y = 120 + float(i) * 40;
        if (i == esel_) canvas_.fill(24, y - 6, Canvas::kW - 48, 34, kRow);
        const Rgba c = i == esel_ ? kAccent : kText;
        if (i < int(opts.size())) {
            canvas_.text(40, y + 4, opts[size_t(i)].label, c, 2);
            canvas_.text(520, y + 4, "< " + settings_.shown(opts[size_t(i)]) + " >", c, 2);
        } else {
            const int k = i - int(opts.size());
            canvas_.text(40, y + 4, k == 0 ? "Gamepads" : k == 1 ? "Back" : "Quit", c, 2);
        }
    }
    drawHelp({{"Left/Right", "change"}, {"Back", "return"}});
}

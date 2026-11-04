// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "app.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <exception>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "app_ids.h"
#include "audio/mixer.h"
#include "data/disc.h"
#include "data/qbkey.h"
#include "game/song_db.h"
#include "script/globals.h"
#include "tests/walk.h"
#include "ui/front_end.h"
#include "ui/port_screens.h"
#include "ui/port_settings.h"
#include "data/json.h"
#include "game/save.h"
#include "ui/song_hooks.h"
#include "ui/song_screen.h"
#include "video/movie_player.h"
#include "devaids/menu_keys.h"
#include "devaids/screenshot.h"
#include "input/input.h"
#include "platform/app_icon.h"
#include "platform/disc_picker.h"
#include "platform/paths.h"
#include "render/canvas.h"
#include "render/venue_stage.h"
#include "ui/port_draw.h"
#include "game/chart.h"
#include <array>
#include <initializer_list>
#include "render/texture_cache.h"

// The program's window and main loop. M0 (kickoff): the platform shell
// only: the window, the disc (picked, remembered, opened), input, audio
// device, screenshots and scripted keys. GH3's own front end comes from its
// scripts (M3, script/ and ui/); until then the window shows the disc's
// status. Nothing here is GH3 behaviour yet.
namespace {

constexpr uint64_t kMinFrameNs = 4'000'000;  // ~250 fps cap without vsync (offscreen tests)

bool envOn(const char* name) {
    const char* v = SDL_getenv(name);
    return v && *v && std::string(v) != "0";
}

class App : public WalkHost {
public:
    explicit App(const Options& o) : options_(o) {}
    int run();

    // WalkHost (the port's walks).
    void walkMouse(float x, float y, int button) override { menuMouse(x, y, button); }
    void walkKey(int keycode) override;
    PortScreens* walkPorts() override { return ports_.get(); }
    Input& walkInput() override { return input_; }
    std::string walkSaveDir() override { return saveDir_; }
    std::string walkEnhanced(const std::string& key) override { return enhanced_.get(key); }
    void walkSetEnhanced(const std::string& key, const std::string& value) override { enhanced_.set(key, value), applyEnhanced(key); }

private:
    bool loadData(const std::string& path);
    void handleEvent(const SDL_Event& e);
    void frame();
    void songEvent(const InputEvent& ev);
    // -- the port's own screens and the mouse (ui/port_screens.h)
    // An action for the menus: the port's open screen, its entry keys on
    // GH3's main menu (yellow: GAMEPADS) and options (blue: ENHANCED), else
    // GH3's front end.
    void menuAction(const InputEvent& ev);
    // The mouse in menu units: button 0 a move, 1 a left click, 3 a right click.
    void menuMouse(float x, float y, int button);
    bool menuMouseAllowed() const;
    void updatePointer();
    // An Enhanced setting changed ("" : all of them, at start): applied at once.
    void applyEnhanced(const std::string& key);
    // Enhanced > Export / Load User Files (gd02's App::enhancedAction): the
    // system dialog, answered on its own thread; pollUserFiles acts on it.
    void userFileAction(const std::string& action);
    void pollUserFiles();
    std::string exportUserFiles(const std::string& dir);
    bool importSettings(const std::string& file);
    bool importProgress(const std::string& file);
    std::mutex dialogMutex_;
    std::vector<std::pair<std::string, std::string>> dialogResults_;
    void addVenueScriptCalls();
    // Drawn over every frame: the FPS counter (Enhanced > Show FPS Counter).
    void overlay();
    void dispatchInput(const std::vector<InputEvent>& events);
    void drawMenuBackdrop(double dtMs);
    void prepareMenuBackdrop();
    static constexpr double kBackdropMs = 30000;  // the song's instant the band is caught at
    static constexpr float kBackdropFlank = -1.3f;  // metres to one side of the singer (the other side from the box)
    static constexpr float kBackdropDist = 3.6f;   // metres in front of the singer
    static constexpr float kBackdropTurn = -1.4f;  // the aim moved across the stage (the singer right of centre)
    std::unique_ptr<VenueStage> backdrop_;
    std::unique_ptr<Chart> backdropChart_;
    bool backdropTried_ = false;
    void drawTouchButtons();
    bool windowToMenu(float wx, float wy, float& mx, float& my) const;

    Options options_;
    SDL_Window* window_ = nullptr;
    Canvas canvas_;
    Mixer mixer_;
    Input input_;
    DiscPicker picker_;
    std::unique_ptr<Disc> disc_;
    std::unique_ptr<Globals> globals_;
    SongDb songs_;
    std::unique_ptr<SongScreen> song_;
    std::unique_ptr<TextureCache> textures_;
    std::unique_ptr<FrontEnd> front_;
    bool devDebugMenu_ = false;  // Enhanced > Developer Debug Menu last frame (GD04-041)
    bool hadDevice_[2] = {}, unplugReported_[2] = {};  // controller loss mid-song (GD04-042)
    std::unique_ptr<SongScreen> outroSong_;  // the ended song's venue, drawn during GH3's end transition
    double outroFrom_ = 0, outroWall_ = 0;
    long long outroFrames_ = 0;  // frames drawn with the ended song's venue (logged when it ends)
    // GH3's song scripts over the native core (D10; GD04_NO_HOOKS=1: off, the earlier path).
    std::unique_ptr<SongHooks> hooks_;
    std::unique_ptr<Walk> walk_;
    uint64_t lastFrameNs_ = 0;
    bool pauseSeen_ = false;  // GH3's PauseGame came after Start
    uint64_t pausedAtNs_ = 0;
    std::string message_;
    bool running_ = true;
    int exitCode_ = 0;
    int escapes_ = 0;  // Esc three times on the first screen quits (guidelines)
    uint64_t startNs_ = 0;
    std::unique_ptr<MenuKeys> keys_;
    EnhancedStore enhanced_;
    std::unique_ptr<PortScreens> ports_;
    std::string saveDir_;               // enhanced.json and controllers.json ("" : the pref dir)
    double mouseMovedMs_ = -1e9;        // the pointer shows for 3 s after the mouse moves
    // After a wheel step, hover waits until the pointer really moves (more
    // than 24 menu units), or a slight nudge would focus the row under it
    // again and undo the scroll (#51).
    float mouseX_ = -1e9f, mouseY_ = -1e9f, wheelX_ = 0, wheelY_ = 0;
    bool wheelHold_ = false;
    bool pointerShown_ = true;
    uint64_t fpsStart_ = 0;
    int fpsFrames_ = 0;
    double fps_ = 0;
};

void App::walkKey(int keycode) {
    // A real key through the input layer (bindings, capture), as SDL would send it.
    for (bool down : {true, false}) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.timestamp = SDL_GetTicksNS();
        e.key.key = SDL_Keycode(keycode);
        e.key.down = down;
        e.key.windowID = SDL_GetWindowID(window_);
        handleEvent(e);
    }
}

bool App::windowToMenu(float wx, float wy, float& mx, float& my) const {
    int x, y, w, h;
    const_cast<Canvas&>(canvas_).renderer().pictureRect(x, y, w, h);
    const float density = SDL_GetWindowPixelDensity(window_);
    mx = (wx * density - float(x)) / float(w) * kScreenW;
    my = (wy * density - float(y)) / float(h) * kScreenH;
    return mx >= 0 && my >= 0 && mx <= kScreenW && my <= kScreenH;
}

bool App::menuMouseAllowed() const {
    // Enhanced > Enable Mouse Selection; the menus only (a song takes no mouse).
    return front_ && !song_ && !front_->songPlaying && enhanced_.get("mouse") != "off";
}

void App::menuMouse(float x, float y, int button) {
    if (!menuMouseAllowed()) return;
    if (ports_ && ports_->isOpen()) return ports_->mouse(x, y, button);
    const std::string state = front_->flowState();
    if (button == 3) return menuAction(InputEvent{Action::Back, true, SDL_GetTicksNS(), ControllerMode::Guitar, 0});
    if (ports_) {
        const std::string item = ports_->itemAt(x, y, state);
        if (!item.empty()) {
            if (button == 1) ports_->open(item == "gamepads" ? PortScreens::Id::Gamepads : PortScreens::Id::Enhanced), front_->soundEvent("ui_sfx_select");
            return;
        }
    }
    // GH3's help bar: SELECT and BACK are clickable.
    const std::string help = front_->ui().helperAt(x, y);
    if (!help.empty()) {
        if (button == 1 && (help == "SELECT" || help == "BACK"))
            menuAction(InputEvent{help == "BACK" ? Action::Back : Action::Confirm, true, SDL_GetTicksNS(), ControllerMode::Guitar, 0});
        return;
    }
    // GH3's menus: a hover focuses (not on a list, which takes clicks only),
    // a click focuses and selects.
    bool list = false;
    if (!front_->ui().menuAt(x, y, false, &list)) return;
    if (button == 1 || !list) front_->ui().menuAt(x, y, true);
    if (button == 1) front_->handle(InputEvent{Action::Confirm, true, SDL_GetTicksNS(), ControllerMode::Guitar, 0});
}

void App::menuAction(const InputEvent& ev) {
    if (ports_ && ports_->isOpen()) return ports_->handle(ev);
    if (ports_ && ev.down) {
        const std::string state = front_->flowState();
        // The port's screens from GH3's (guidelines/universal_launcher.md;
        // agents/DECISIONS.md): yellow on the main menu, blue on the options.
        if (state == "main_menu_fs" && ev.action == Action::Fret2) {
            front_->soundEvent("ui_sfx_select");
            return ports_->open(PortScreens::Id::Gamepads);
        }
        if (state == "options_select_option_fs" && ev.action == Action::Fret3) {
            front_->soundEvent("ui_sfx_select");
            return ports_->open(PortScreens::Id::Enhanced);
        }
    }
    front_->handle(ev);
}

void App::updatePointer() {
    // The pointer shows when the mouse moves, hides after 3 s without
    // movement, on a key or a button, and in a song.
    const bool show = menuMouseAllowed() && double(SDL_GetTicksNS()) / 1e6 - mouseMovedMs_ < 3000;
    if (show != pointerShown_) {
        if (show) SDL_ShowCursor();
        else SDL_HideCursor();
        pointerShown_ = show;
    }
}

// The band and camera calls GH3's song-end scripts make on the venue
// (devil_finish_anim, devil_lose_anim, play_win_anims...): `Bassist :
// Obj_SwitchScript Transition_PlayAnim_Spawned params = {Anim [stance]
// [Cycle]}` plays the clip on that member, CameraCuts_SetArrayPrefix
// switches the cameras (cameras_boss_finish, cameras_boss_dead) [C
// scripts]. They reach the song being played, or the ended one still drawn
// under the end transition.
void App::addVenueScriptCalls() {
    using qs::ScriptRun;
    using qs::Vm;
    auto venueSong = [this](double& ms) -> SongScreen* {
        if (song_) return ms = song_->songMs(), song_.get();
        if (outroSong_) return ms = outroFrom_ + double(SDL_GetTicksNS()) / 1e6 - outroWall_, outroSong_.get();
        return nullptr;
    };
    Vm& vm = front_->vm();
    auto next = vm.objectCall;
    vm.objectCall = [venueSong, next](Vm& v, ScriptRun& run, uint32_t obj, uint32_t method, qb::Struct& p, bool& handled) -> bool {
        const bool member = obj == qbkey::key("Guitarist") || obj == qbkey::key("Bassist");
        if (member && method == qbkey::key("Obj_SwitchScript")) {
            const qb::Value* scr = nullptr;
            for (const qb::Item& it : p.items)
                if (!it.name && it.value.key) scr = &it.value;
            const qb::Value* params = Vm::param(p, qbkey::key("params"));
            // play_win_anims / play_lose_anims' play_special_anim {stance Anim = idle}:
            // VenueStage::outro has already put the same stances on at the song's end.
            if (scr && scr->key == qbkey::key("play_special_anim")) return handled = true, true;
            if (scr && scr->key == qbkey::key("Transition_PlayAnim_Spawned") && params && params->st) {
                handled = true;
                double ms = 0;
                if (SongScreen* s = venueSong(ms)) {
                    const qb::Value* anim = params->st->find(qbkey::key("Anim"));
                    const qb::Value* stance = params->st->find(qbkey::key("stance"));
                    const qb::Value* cycle = params->st->find(qbkey::key("Cycle"));
                    s->playClip(obj == qbkey::key("Bassist") ? 1 : 0, anim ? anim->key : 0, stance ? stance->key : 0,
                                cycle && cycle->number() != 0, ms);
                }
                return true;
            }
        }
        // <member> : obj_getboneposition bone = b -> x y z (Do_StarPower_*FX) [C scripts].
        const bool band = member || obj == qbkey::key("Drummer") || obj == qbkey::key("vocalist");
        if (band && method == qbkey::key("obj_getboneposition")) {
            handled = true;
            double ms = 0;
            venue::Vec3 pos{};
            SongScreen* s = venueSong(ms);
            const qb::Value* bone = Vm::param(p, qbkey::key("bone"));
            if (!s || !s->venue() || !bone || !s->venue()->bonePos(obj, bone->key, pos)) return false;
            if (!run.frames.empty() && run.frames.back().locals)
                for (int k = 0; k < 3; ++k) Vm::set(*run.frames.back().locals, qbkey::key(k == 0 ? "x" : k == 1 ? "y" : "z"), Vm::makeFloat(pos[size_t(k)]));
            return true;
        }
        // A scripted particle system by its (mangled) name: <id> : emitrate rate = 0,
        // <id> : destroy ifempty (Kill_StarPower_StageFX, kill_4x_fx) [C scripts].
        if (method == qbkey::key("emitrate") || method == qbkey::key("destroy")) {
            double ms = 0;
            SongScreen* s = venueSong(ms);
            Particles* ps = s && s->venue() ? s->venue()->particles() : nullptr;
            if (ps && ps->hasNamed(obj)) {
                handled = true;
                if (method == qbkey::key("emitrate")) {
                    if (Vm::number(Vm::param(p, qbkey::key("rate")), 0) <= 0) ps->stopEmitting(obj, ms);
                } else if (Vm::number(Vm::param(p, qbkey::key("ifempty")), 0) == 0) {
                    ps->destroyNamed(obj);
                } else {
                    ps->stopEmitting(obj, ms);  // gone when its particles are
                }
                return true;
            }
        }
        return next ? next(v, run, obj, method, p, handled) : false;
    };
    // The song scripts' particle systems (the Star Power stage FX, the 4x
    // finger flames) on the venue's particles; ObjID + bone follow that band
    // member's bone; named systems are kept under MangleChecksums(name, ObjID)
    // as the kill scripts look them up (script/natives_misc.cpp's mangle) [C scripts].
    auto particles = [venueSong](double& ms) -> Particles* {
        SongScreen* s = venueSong(ms);
        return s && s->venue() ? s->venue()->particles() : nullptr;
    };
    auto create = [particles](Vm&, ScriptRun&, qb::Struct& p) {
        double ms = 0;
        Particles* ps = particles(ms);
        if (!ps) return true;
        auto key = [&](const char* n) {
            const qb::Value* v = Vm::param(p, qbkey::key(n));
            return v ? v->key : 0u;
        };
        const uint32_t name = key("name"), obj = key("ObjID");
        const uint32_t id = obj ? name ^ (obj * 0x9E3779B1u + 0x7F4A7C15u) : name;
        venue::Vec3 pos{};
        if (const qb::Value* pv = Vm::param(p, qbkey::key("pos")); pv && pv->type == qb::Type::Vector)
            for (size_t k = 0; k < 3; ++k) pos[k] = pv->f[k];
        ps->createScripted(id, key("groupid"), Vm::param(p, qbkey::key("params_script")), pos, obj, obj ? key("bone") : 0, ms);
        return true;
    };
    vm.addNative("CreateParticleSystem", create);
    vm.addNative("CreateParticleSystem_fast", create);
    vm.addNative("DestroyParticlesByGroupID", [particles](Vm&, ScriptRun&, qb::Struct& p) {
        double ms = 0;
        const qb::Value* g = Vm::param(p, qbkey::key("groupid"));
        if (Particles* ps = particles(ms); ps && g) ps->destroyGroup(g->key);
        return true;
    });
    // GetPakManCurrent / Name map = zones in a song: the zone the venue loaded
    // (the port loads it natively, so the pak manager's may be unset); the
    // Star Power stage FX switch on it [C scripts Do_StarPower_TeslaFX, GH_SFX_Intro_WarmUp].
    for (const char* nm : {"GetPakManCurrent", "GetPakManCurrentName"}) {
        qs::Native prev = vm.native(nm);
        vm.addNative(nm, [venueSong, prev](Vm& v, ScriptRun& run, qb::Struct& p) {
            double ms = 0;
            const qb::Value* map = Vm::param(p, qbkey::key("map"));
            SongScreen* s = venueSong(ms);
            if (map && map->key == qbkey::key("zones") && s && s->venue()) {
                const std::string& zone = s->venue()->venue().zone;
                if (!run.frames.empty() && run.frames.back().locals) {
                    Vm::set(*run.frames.back().locals, qbkey::key("pak"), Vm::makeKey(qbkey::key(zone)));
                    Vm::set(*run.frames.back().locals, qbkey::key("pakname"), Vm::makeString(zone));
                }
                return true;
            }
            return prev ? prev(v, run, p) : false;
        });
    }
    // GetNode <id> -> node (the venue node's struct: pos, name); the Tesla
    // Star Power aims at the zone's TeslaNodes [C script Create_StarPower_TeslaFX].
    vm.addNative("GetNode", [venueSong](Vm&, ScriptRun& run, qb::Struct& p) {
        double ms = 0;
        SongScreen* s = venueSong(ms);
        const qb::Value* id = Vm::positional(p);
        if (!s || !s->venue() || !id) return false;
        const venue::Node* n = s->venue()->venue().node(id->key);
        if (!n) return false;
        qb::Struct node;
        qb::Value pos;
        pos.type = qb::Type::Vector;
        pos.f[0] = n->pos[0], pos.f[1] = n->pos[1], pos.f[2] = n->pos[2];
        Vm::set(node, qbkey::key("pos"), pos);
        Vm::set(node, qbkey::key("name"), Vm::makeKey(n->name));
        if (!run.frames.empty() && run.frames.back().locals) {
            qb::Value nv;
            nv.type = qb::Type::Struct;
            nv.st = std::make_shared<qb::Struct>(std::move(node));
            Vm::set(*run.frames.back().locals, qbkey::key("node"), nv);
        }
        return true;
    });
    // CreateTeslaEffectObject / SetTeslaEffectObject name start end width; DeleteTeslaEffectObject name.
    auto tesla = [particles](Vm&, ScriptRun&, qb::Struct& p) {
        double ms = 0;
        Particles* ps = particles(ms);
        const qb::Value* nm = Vm::param(p, qbkey::key("name"));
        if (!ps || !nm) return true;
        Particles::Tesla& t = ps->teslas[nm->key];
        if (const qb::Value* a = Vm::param(p, qbkey::key("start")); a && a->type == qb::Type::Vector) t.start = {a->f[0], a->f[1], a->f[2]};
        if (const qb::Value* b = Vm::param(p, qbkey::key("end")); b && b->type == qb::Type::Vector) t.end = {b->f[0], b->f[1], b->f[2]};
        t.width = Vm::number(Vm::param(p, qbkey::key("width")), t.width);
        return true;
    };
    vm.addNative("CreateTeslaEffectObject", tesla);
    vm.addNative("SetTeslaEffectObject", tesla);
    vm.addNative("DeleteTeslaEffectObject", [particles](Vm&, ScriptRun&, qb::Struct& p) {
        double ms = 0;
        Particles* ps = particles(ms);
        const qb::Value* nm = Vm::param(p, qbkey::key("name"));
        if (!nm) nm = Vm::positional(p);
        if (ps && nm) ps->teslas.erase(nm->key);
        return true;
    });
    // EngineWind Create = {...} / modify = {name pos} / remove = {name} (the Star Power FX swirl) [C scripts].
    vm.addNative("EngineWind", [particles](Vm&, ScriptRun&, qb::Struct& p) {
        double ms = 0;
        Particles* ps = particles(ms);
        if (!ps) return true;
        auto vec = [](const qb::Value* v, venue::Vec3 d) {
            if (v && v->type == qb::Type::Vector) d = {v->f[0], v->f[1], v->f[2]};
            return d;
        };
        for (const char* op : {"Create", "modify", "remove"}) {
            const qb::Value* a = Vm::param(p, qbkey::key(op));
            if (!a || !a->st) continue;
            const qb::Value* nm = a->st->find(qbkey::key("name"));
            if (!nm) continue;
            if (std::string(op) == "remove") {
                ps->removeWind(nm->key);
            } else if (std::string(op) == "Create") {
                Particles::Wind w;
                w.pos = vec(a->st->find(qbkey::key("pos")), {});
                w.velocity = vec(a->st->find(qbkey::key("velocity")), {});
                w.speed = Vm::number(a->st->find(qbkey::key("Speed")), 0);
                w.tangentWeight = Vm::number(a->st->find(qbkey::key("tangentweight")), 0);
                w.range = Vm::number(a->st->find(qbkey::key("range")), 0);
                ps->setWind(nm->key, w);
            } else if (Particles::Wind* w = ps->wind(nm->key)) {
                w->pos = vec(a->st->find(qbkey::key("pos")), w->pos);
            }
        }
        return true;
    });
    vm.addNative("IsCreated", [particles](Vm&, ScriptRun&, qb::Struct& p) {
        double ms = 0;
        const qb::Value* n = Vm::positional(p);
        Particles* ps = particles(ms);
        return ps && n && ps->hasNamed(n->key);
    });
    vm.addNative("CameraCuts_SetArrayPrefix", [venueSong](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* pre = Vm::param(p, qbkey::key("prefix"));
        double ms = 0;
        // Only the boss endings' prefixes: the star power, walk and transition
        // cameras are the camera director's own (venue_camera.cpp).
        if (!pre || (pre->str != "cameras_boss_finish" && pre->str != "cameras_boss_dead")) return true;
        if (SongScreen* s = venueSong(ms)) s->outroCameras(ms, pre->str);
        return true;
    });
}

namespace {
constexpr const char* kSettingsFile = "GD04-user-settings.json";
constexpr const char* kProgressFile = "GD04-user-progress.json";
constexpr const char* kControllersFile = "GD04-controllers.json";  // the Gamepad menu's Export / Import
struct DialogRequest {
    std::mutex* mutex;
    std::vector<std::pair<std::string, std::string>>* results;
    std::string action;
};
void SDLCALL dialogDone(void* user, const char* const* files, int) {
    std::unique_ptr<DialogRequest> req(static_cast<DialogRequest*>(user));
    std::lock_guard<std::mutex> lock(*req->mutex);
    req->results->push_back({req->action, files && files[0] ? std::string(files[0]) : std::string()});
}
}  // namespace

void App::userFileAction(const std::string& action) {
    // Development aid: GD04_DIALOG=<path> answers the dialog at once (walk
    // tests); a folder answers the load dialogs with its exported file.
    if (const char* forced = SDL_getenv("GD04_DIALOG")) {
        std::string path = forced;
        std::error_code ec;
        if (action != "export" && action != "export_controllers" && std::filesystem::is_directory(path, ec))
            path = (std::filesystem::path(path) / (action == "load_settings"        ? kSettingsFile
                                                   : action == "import_controllers" ? kControllersFile
                                                                                    : kProgressFile))
                       .string();
        std::lock_guard<std::mutex> lock(dialogMutex_);
        dialogResults_.push_back({action, path});
        return;
    }
    auto* req = new DialogRequest{&dialogMutex_, &dialogResults_, action};
    if (action == "export" || action == "export_controllers") {
        SDL_ShowOpenFolderDialog(dialogDone, req, window_, nullptr, false);
    } else {
        static const SDL_DialogFileFilter kJson[] = {{"GD04 user files", "json"}};
        SDL_ShowOpenFileDialog(dialogDone, req, window_, kJson, 1, nullptr, false);
    }
}

void App::pollUserFiles() {
    std::vector<std::pair<std::string, std::string>> done;
    {
        std::lock_guard<std::mutex> lock(dialogMutex_);
        done.swap(dialogResults_);
    }
    for (const auto& [action, path] : done) {
        if (path.empty()) continue;  // cancelled
        std::string status;
        if (action == "export") status = exportUserFiles(path);
        else if (action == "load_settings") status = importSettings(path) ? "Settings loaded" : "Could not load that settings file";
        else if (action == "load_progress") status = importProgress(path) ? "Progress loaded" : "Could not load that progress file";
        else if (action == "export_controllers")  // the Gamepad menu's Export
            status = input_.exportTo((std::filesystem::path(path) / kControllersFile).string()) ? std::string("Exported to ") + kControllersFile
                                                                                               : "Could not write the controllers file";
        else if (action == "import_controllers")  // the Gamepad menu's Import
            status = input_.importFrom(path) ? "Controllers loaded" : "Could not load that controllers file";
        SDL_Log("enhanced options: %s: %s", action.c_str(), status.c_str());
        if (ports_) ports_->context().status = status;
    }
}

// GD04-user-settings.json: every Enhanced Option; GD04-user-progress.json:
// GH3's save (save.json: the global tags, as SaveToMemoryCard writes them).
std::string App::exportUserFiles(const std::string& dir) {
    const std::filesystem::path base(dir);
    Json all = Json::object();
    all.set("format", Json::string("gd04-enhanced"));
    for (const EnhancedSetting& e : enhancedSettings()) all.set(e.key, Json::string(enhanced_.get(e.key)));
    bool ok;
    {
        std::ofstream out(base / kSettingsFile, std::ios::binary | std::ios::trunc);
        out << all.dump(1) << "\n";
        ok = bool(out);
    }
    std::error_code ec;
    if (ok && !save::path().empty() && std::filesystem::exists(save::path(), ec))
        ok = std::filesystem::copy_file(save::path(), base / kProgressFile, std::filesystem::copy_options::overwrite_existing, ec);
    return ok ? "Exported to " + dir : "Could not write to " + dir;
}

// Each known setting with a value it takes is kept; anything odd keeps the
// current value. Applied at once.
bool App::importSettings(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    Json j;
    try {
        j = Json::parse(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
    } catch (const std::exception& ex) {
        SDL_Log("load user settings %s: %s", file.c_str(), ex.what());
        return false;
    }
    if (j.type != Json::Type::Object) return false;
    for (const EnhancedSetting& e : enhancedSettings()) {
        if (!j.has(e.key)) continue;
        const Json& v = j[e.key];
        if (v.type == Json::Type::String && enhancedValid(e, v.str())) enhanced_.set(e.key, v.str());
        else SDL_Log("load user settings: %s is not valid, kept", e.key);
    }
    applyEnhanced("");
    return true;
}

// The progress replaces save.json, read back as LoadFromMemoryCard does
// (the old save is put back if the file is not a GH3 save) and put in effect.
bool App::importProgress(const std::string& file) {
    const std::string savePath = save::path();
    if (savePath.empty() || !front_) return false;
    std::error_code ec;
    const std::string backup = savePath + ".bak";
    const bool had = std::filesystem::exists(savePath, ec);
    if (had) std::filesystem::copy_file(savePath, backup, std::filesystem::copy_options::overwrite_existing, ec);
    if (!std::filesystem::copy_file(file, savePath, std::filesystem::copy_options::overwrite_existing, ec)) return false;
    save::Tags tags;
    save::Sections sections;
    std::string err;
    if (!save::read(tags, sections, err)) {
        SDL_Log("load user progress %s: %s", file.c_str(), err.c_str());
        if (had) std::filesystem::copy_file(backup, savePath, std::filesystem::copy_options::overwrite_existing, ec);
        else std::filesystem::remove(savePath, ec);
        return false;
    }
    qs::Vm& vm = front_->vm();
    for (auto& [k, t] : tags) vm.globalTags[k] = std::move(t);
    for (auto& [k, t] : sections) vm.globalTagSections[k] = std::move(t);
    return true;
}

void App::applyEnhanced(const std::string& key) {
    auto is = [&](const char* k) { return key.empty() || key == k; };
    if (is("screen_mode") && window_) {
        const std::string m = enhanced_.get("screen_mode");
        if (m == "windowed") {
            SDL_SetWindowFullscreen(window_, false);
        } else {
            // Full Screen: the desktop's mode, exclusive; No Border: a window covering the screen.
            const SDL_DisplayMode* dm = m == "fullscreen" ? SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window_)) : nullptr;
            SDL_SetWindowFullscreenMode(window_, dm);
            SDL_SetWindowFullscreen(window_, true);
        }
    }
    if (is("particles")) VenueStage::particlesOn = enhanced_.get("particles") != "none";
    {
        // The picture: Enhanced's chain (render/enhanced_fx) and the world's resolution.
        auto idx = [&](const char* k, std::initializer_list<const char*> vals) {
            int i = 0;
            for (const char* v : vals) {
                if (enhanced_.get(k) == v) return i;
                ++i;
            }
            return 0;
        };
        gfx::PostSettings& ps = VenueStage::post;
        ps.aa = gfx::PostSettings::AA(idx("antialiasing", {"none", "fxaa", "ssaa", "taa"}));
        ps.fsr1 = enhanced_.get("ai_upscaling") == "fsr1";
        ps.fsrScale = 1.0f / std::array<float, 3>{1.5f, 1.7f, 2.0f}[size_t(idx("ai_upscaling_mode", {"quality", "balanced", "performance"}))];
        ps.bloom = idx("bloom", {"none", "original", "2x", "3x"});
        ps.cinematic = enhanced_.on("cinematic_color");
        ps.grade = idx("color_grade", {"off", "warm", "cold", "vintage", "noir", "neon", "sepia"});
        ps.grain = idx("film_grain", {"none", "light", "medium", "strong", "stronger"});
        ps.chromatic = idx("chromatic", {"none", "light", "medium", "strong"});
        Player::forgivingHopos = enhanced_.get("hopo_mode") == "forgiving";
        VenueStage::extras.shadowsAll = enhanced_.on("shadows_all");
        VenueStage::extras.moreLights = idx("more_lights", {"none", "1.5x", "2x", "2.5x"});
        VenueStage::extras.lightDust = idx("light_dust", {"none", "1.5x", "2x", "2.5x"});
        TextureCache::sharp = enhanced_.on("sharp_ui");  // from the next start (the textures load at boot)
        venue::CameraDirector::noShake = enhanced_.on("no_shaky_cam");
        qs::Vm::cutContent = enhanced_.on("cut_content");
        ps.ssao = idx("ssao", {"none", "light", "medium", "strong", "stronger"});
        ps.sharpen = idx("sharpening", {"off", "low", "high"});
        ps.starBlur = idx("star_blur", {"none", "light", "medium", "strong", "stronger"});
        ps.bokeh = idx("bokeh", {"none", "light", "medium", "strong", "stronger"});  // 2: GH3's own
        VenueStage::internalHeight = std::atoi(enhanced_.get("internal_res").c_str());
    }
    if (is("shake_whammy")) input_.setShakeWhammy(enhanced_.on("shake_whammy"));
    // Rock Band guitars and XInput pads always play (universal_mechanisms.md 1.2.2: everything on by default).
    input_.setRockBand(true), input_.setGamepadPlay(true);
    if (is("touch")) input_.setTouch(enhanced_.on("touch"));
    if (is("mouse")) mouseMovedMs_ = -1e9;
}

// The on-screen touch buttons (Input::touchButtons, menu units): a
// translucent disc with an arrow, or START / II (gd02's look).
void App::drawTouchButtons() {
    const std::vector<Input::TouchButton> buttons = input_.touchButtons();
    if (buttons.empty()) return;
    canvas_.flush();
    std::vector<Vertex> v;
    auto tri = [&](float ax, float ay, float bx, float by, float cx, float cy, SDL_Color c) {
        for (auto [x, y] : {std::pair{ax, ay}, std::pair{bx, by}, std::pair{cx, cy}}) v.push_back({x, y, 0, 0, 0, c.r, c.g, c.b, c.a});
    };
    for (const Input::TouchButton& b : buttons) {
        constexpr int kSeg = 32;
        for (int i = 0; i < kSeg; ++i) {
            const float a0 = float(i) / kSeg * 6.2831853f, a1 = float(i + 1) / kSeg * 6.2831853f;
            tri(b.x, b.y, b.x + b.r * std::cos(a0), b.y + b.r * std::sin(a0), b.x + b.r * std::cos(a1), b.y + b.r * std::sin(a1), {0, 0, 0, 110});
        }
        const SDL_Color w{255, 255, 255, 200};
        const float k = b.r * 0.45f;
        if (b.action == Action::Up) tri(b.x - k, b.y + k * 0.5f, b.x + k, b.y + k * 0.5f, b.x, b.y - k * 0.7f, w);
        else if (b.action == Action::Down) tri(b.x - k, b.y - k * 0.5f, b.x + k, b.y - k * 0.5f, b.x, b.y + k * 0.7f, w);
    }
    if (!v.empty()) canvas_.draw(v.data(), v.size(), 0, Blend::Alpha);
    for (const Input::TouchButton& b : buttons)
        if (b.action == Action::Start) canvas_.textCentered(b.x, b.y - 4, 1, {255, 255, 255, 220}, b.y < 100 ? "II" : "START");
}

// The Enhanced / Gamepads backdrop (guidelines/universal_launcher.md "The
// Enhanced menu"): a venue with the band caught mid-song, posed and still
// (advanced to 30 s into a song once, then drawn at that instant), under a
// static close-up of the singer on the right with the whole band in view
// (gd01's Enhanced pose); made once (at the first menu frame) and kept, so
// the page opens at once [I: the shot's distances].
void App::drawMenuBackdrop(double) {
    if (!backdrop_ && !backdropTried_) prepareMenuBackdrop();  // the boot screen was skipped
    PortDraw::backdropDrawn = backdrop_ != nullptr;
    if (!backdrop_) return;
    canvas_.flush();
    backdrop_->draw(kBackdropMs);
}

void App::prepareMenuBackdrop() {
    if (backdrop_ || backdropTried_ || !disc_ || !globals_) return;
    backdropTried_ = true;
    const uint64_t t0 = SDL_GetTicksNS();
    std::string err;
    try {
        backdropChart_ = std::make_unique<Chart>(loadChart(*disc_, "slowride"));
        backdrop_ = std::make_unique<VenueStage>();
        // Started 1.5 s before the instant (a negative lead-in: the band, crowd
        // and particles begin there), so one step reaches it posed.
        if (!backdrop_->start(*disc_, *globals_, VenueStage::chooseZone(*globals_), *backdropChart_, qbkey::key("slowride"), -(kBackdropMs - 1500), nullptr, err) ||
            !backdrop_->attach(canvas_.renderer().device(), *disc_, err)) {
            SDL_Log("menu backdrop: %s", err.c_str());
            backdrop_.reset();
            return;
        }
    } catch (const std::exception& ex) {
        SDL_Log("menu backdrop: %s", ex.what());
        backdrop_.reset();
        return;
    }
    // The band posed mid-song: the venue stepped to the instant, then still.
    for (double t = kBackdropMs - 1500; t <= kBackdropMs; t += 100) backdrop_->advance(t);
    canvas_.flush();
    backdrop_->draw(kBackdropMs);  // the band posed (their bones are known after a draw), unseen
    // The shot: from the stage's front (the drummer behind the singer), a
    // little to one flank, the singer's head on the right of the picture.
    venue::Vec3 head{}, drums{};
    const bool haveHead = backdrop_->bonePos(qbkey::key("vocalist"), qbkey::key("Bone_Head"), head);
    const bool haveDrums = backdrop_->bonePos(qbkey::key("Drummer"), qbkey::key("Bone_Head"), drums);
    if (haveHead) {
        venue::Vec3 front{head[0] - (haveDrums ? drums[0] : head[0]), 0, head[2] - (haveDrums ? drums[2] : head[2] - 1)};
        float fl = std::sqrt(front[0] * front[0] + front[2] * front[2]);
        if (fl < 1e-3f) front = {0, 0, 1}, fl = 1;
        front[0] /= fl, front[2] /= fl;
        const venue::Vec3 side{front[2], 0, -front[0]};  // across the stage
        venue::CamPose cam;
        cam.fov = 58;
        // The band's middle (the members' feet, chest high).
        venue::Vec3 mid{0, 0, 0};
        int nm = 0;
        for (uint32_t m : {qbkey::key("Guitarist"), qbkey::key("Bassist"), qbkey::key("Drummer"), qbkey::key("vocalist")}) {
            venue::Vec3 p;
            if (backdrop_->bonePos(m, qbkey::key("Bone_Head"), p)) mid[0] += p[0], mid[1] += p[1], mid[2] += p[2], ++nm;
        }
        if (nm) mid = {mid[0] / float(nm), mid[1] / float(nm) - 0.4f, mid[2] / float(nm)};
        else mid = head;
        cam.pos = {head[0] + front[0] * kBackdropDist + side[0] * kBackdropFlank, head[1] + 0.1f,
                   head[2] + front[2] * kBackdropDist + side[2] * kBackdropFlank};
        // Aimed between the band's middle and the singer, past the singer: the
        // singer right of centre, the band beside and behind.
        venue::Vec3 look{mid[0] * 0.65f + head[0] * 0.35f, mid[1] * 0.65f + (head[1] - 0.2f) * 0.35f, mid[2] * 0.65f + head[2] * 0.35f};
        for (size_t k : {size_t(0), size_t(2)}) look[k] += side[k] * kBackdropTurn;  // the singer to the picture's right
        venue::Vec3 f{look[0] - cam.pos[0], look[1] - cam.pos[1], look[2] - cam.pos[2]};
        const float l = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        for (float& c : f) c /= l;
        // The camera's basis: +z forward, +y up, -x right (venue::rotate's convention).
        venue::Vec3 r{f[2], 0, -f[0]};  // right = up x forward with up (0,1,0)... as -x
        const float rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
        r[0] /= rl, r[2] /= rl;
        const venue::Vec3 u{f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
        // Columns: local x = r, y = u, z = f (a right-handed frame); to a quaternion.
        const float m00 = r[0], m11 = u[1], m22 = f[2];
        float q[4];
        const float tr = m00 + m11 + m22;
        if (tr > 0) {
            const float sq = std::sqrt(tr + 1.0f) * 2;
            q[3] = 0.25f * sq, q[0] = (u[2] - f[1]) / sq, q[1] = (f[0] - r[2]) / sq, q[2] = (r[1] - u[0]) / sq;
        } else if (m00 > m11 && m00 > m22) {
            const float sq = std::sqrt(1.0f + m00 - m11 - m22) * 2;
            q[3] = (u[2] - f[1]) / sq, q[0] = 0.25f * sq, q[1] = (u[0] + r[1]) / sq, q[2] = (f[0] + r[2]) / sq;
        } else if (m11 > m22) {
            const float sq = std::sqrt(1.0f + m11 - m00 - m22) * 2;
            q[3] = (f[0] - r[2]) / sq, q[0] = (u[0] + r[1]) / sq, q[1] = 0.25f * sq, q[2] = (f[1] + u[2]) / sq;
        } else {
            const float sq = std::sqrt(1.0f + m22 - m00 - m11) * 2;
            q[3] = (r[1] - u[0]) / sq, q[0] = (f[0] + r[2]) / sq, q[1] = (f[1] + u[2]) / sq, q[2] = 0.25f * sq;
        }
        cam.rot = {q[0], q[1], q[2], q[3]};
        SDL_Log("menu backdrop: singer head (%.2f %.2f %.2f), drummer (%.2f %.2f %.2f), band (%.2f %.2f %.2f), camera (%.2f %.2f %.2f)", head[0], head[1],
                head[2], drums[0], drums[1], drums[2], mid[0], mid[1], mid[2], cam.pos[0], cam.pos[1], cam.pos[2]);
        backdrop_->setFixedCamera(&cam);
    }
    // Drawn once now, unseen, so its shaders are ready when the page opens.
    canvas_.flush();
    backdrop_->draw(kBackdropMs);
    SDL_Log("menu backdrop: %s, ready in %.0f ms", backdrop_->venue().zone.c_str(), double(SDL_GetTicksNS() - t0) / 1e6);
}

void App::overlay() {
    // Touch: the lanes of this frame's highways for the fingers, menu taps as clicks, the buttons.
    if (input_.touch()) {
        for (int p = 0; p < 2; ++p) {
            Input::TouchLanes l;
            if (song_ && (p == 0 || song_->twoPlayers())) {
                song_->highwayOf(p).touchLanes(l.nearX, l.nearY, l.farX, l.farY);
                l.active = true;
            }
            input_.setTouchLanes(p, l);
        }
        for (auto [tx, ty] : input_.takeMenuTaps()) menuMouse(tx, ty, 1);
        drawTouchButtons();
    }
    const uint64_t now = SDL_GetTicksNS();
    ++fpsFrames_;
    if (!fpsStart_) fpsStart_ = now;
    if (now - fpsStart_ >= 500'000'000ull) fps_ = fpsFrames_ * 1e9 / double(now - fpsStart_), fpsStart_ = now, fpsFrames_ = 0;
    if (enhanced_.on("fps_counter") && fps_ > 0) {
        // As gd02's: the game's own font, green with a dark shadow, bottom left
        // (guidelines/universal_launcher.md: "Frames a second, bottom left").
        const std::string t = std::to_string(int(std::lround(fps_))) + " FPS";
        if (front_) {
            canvas_.flush();
            const uint32_t font = qbkey::key("text_a5");  // the options' font (gd02: its game's helveticablack)
            front_->ui().drawText(canvas_, font, t, 25, 686, 22, {0, 0, 0, 153}, 0x21);
            front_->ui().drawText(canvas_, font, t, 23, 684, 22, {51, 255, 77, 255}, 0x21);
        }
    }
}

bool App::loadData(const std::string& path) {
    std::string error;
    disc_ = Disc::open(path, error);
    if (!disc_) {
        message_ = error;
        SDL_Log("%s", error.c_str());
        return false;
    }
    disc_->loadNames();
    try {
        globals_ = std::make_unique<Globals>();
        globals_->load(*disc_);
        songs_.load(*globals_);
    } catch (const std::exception& e) {
        message_ = std::string("cannot read the scripts: ") + e.what();
        disc_.reset();
        return false;
    }
    rememberPath(path);
    message_ = "Guitar Hero III disc: " + std::to_string(songs_.songs().size()) + " songs";
    SDL_Log("disc: %s (%s)", path.c_str(), message_.c_str());
    return true;
}

void App::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_QUIT) running_ = false;
    // The mouse in the menus (gd01's): hovering focuses (only while the window
    // has focus), a left click selects, a right click is Back, the wheel moves.
    if (front_ && (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) &&
        (e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.which : e.button.which) != SDL_TOUCH_MOUSEID) {
        const bool click = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        float mx, my;
        const bool inside = windowToMenu(click ? e.button.x : e.motion.x, click ? e.button.y : e.motion.y, mx, my);
        if (menuMouseAllowed()) mouseMovedMs_ = double(SDL_GetTicksNS()) / 1e6;
        const bool focused = (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
        bool held = false;
        if (inside) {
            mouseX_ = mx, mouseY_ = my;
            held = wheelHold_ && !click && std::hypot(mx - wheelX_, my - wheelY_) <= 24.0f;
            if (!held) wheelHold_ = false;
        }
        if (inside && !held && (click || focused) && (!click || e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT))
            menuMouse(mx, my, !click ? 0 : e.button.button == SDL_BUTTON_LEFT ? 1 : 3);
    } else if (front_ && e.type == SDL_EVENT_MOUSE_WHEEL && e.wheel.which != SDL_TOUCH_MOUSEID && e.wheel.y != 0 && menuMouseAllowed()) {
        mouseMovedMs_ = double(SDL_GetTicksNS()) / 1e6;
        wheelHold_ = true, wheelX_ = mouseX_, wheelY_ = mouseY_;
        menuAction(InputEvent{e.wheel.y > 0 ? Action::Up : Action::Down, true, SDL_GetTicksNS(), ControllerMode::Guitar, 0});
    } else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        mouseMovedMs_ = -1e9;  // a key or a button hides the pointer
    }
    // A joypad's Y (its yellow face button) opens GAMEPADS on the main menu.
    if (front_ && ports_ && !song_ && !ports_->isOpen() && e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
        e.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH && input_.joypadDevice(e.gbutton.which) && front_->flowState() == "main_menu_fs" &&
        !input_.capturing()) {
        front_->soundEvent("ui_sfx_select");
        ports_->open(PortScreens::Id::Gamepads);
        std::vector<InputEvent> drop;
        input_.handle(e, drop);  // the input layer still sees it (its state), the menus do not
        return;
    }
    std::vector<InputEvent> events;
    input_.handle(e, events);
    dispatchInput(events);
}

// The input layer's actions to the song or the menus (SDL events, and the
// per-frame poll: touch chords, shake-to-whammy, MIDI).
void App::dispatchInput(const std::vector<InputEvent>& events) {
    for (const InputEvent& ev : events) {
        if (walk_ && ev.down) walk_->countPress(ev.action);
        if (song_) {
            if (!front_ && ev.action == Action::Back && ev.down) running_ = false;  // --song alone: Back quits
            songEvent(ev);
            continue;
        }
        if (front_) {
            menuAction(ev);
            continue;
        }
        if (!ev.down) continue;
        if (ev.action == Action::Back) {
            if (++escapes_ >= 3) running_ = false;
        } else {
            escapes_ = 0;
        }
        if (ev.action == Action::Confirm && !disc_ && !picker_.busy()) picker_.open(window_);
    }
}

// A button in a song: Start opens GH3's pause menu (gh3_start_pressed),
// which then has the buttons until it resumes.
void App::songEvent(const InputEvent& ev) {
    // The attract mode: any button goes to GH3's check_for_attract_mode_input.
    if (front_ && front_->attractMode()) {
        menuAction(ev);
        return;
    }
    if (front_ && front_->songPaused) {
        menuAction(ev);
        return;
    }
    if (front_ && ev.action == Action::Start && ev.down) {
        mixer_.setPaused(true);
        front_->songPaused = true, pauseSeen_ = false, pausedAtNs_ = SDL_GetTicksNS();
        front_->startPressed();
        return;
    }
    song_->handle(ev);
}

void App::frame() {
    pollUserFiles();
    {
        std::vector<InputEvent> polled;  // touch chords, shake-to-whammy, MIDI notes
        input_.poll(SDL_GetTicksNS(), polled);
        if (!polled.empty()) dispatchInput(polled);
    }
    // A controller lost mid-song (GD04-042): the 360's engine calls GH3's
    // controller_unplugged {device_num} while check_for_unplugged_controllers
    // is 1 (controller_pulling.qb; in a song: controller_disconnect_fs, "YOU
    // ARE ROCKING OUT A BIT TOO HARD", RESUME / QUIT) [C script; I: the call
    // site]. As gd-game-01..03, a player is missing when their slot had a
    // device at the song's start and has none now (the keyboard plays any
    // free slot unless the Gamepads screen gave it a player).
    if (song_ && front_) {
        for (int s = 0; s < (song_->twoPlayers() ? 2 : 1); ++s) {
            const bool lost = hadDevice_[s] && !input_.padConnected(s);
            const qb::Value* check = front_->vm().global(qbkey::key("check_for_unplugged_controllers"));
            if (lost && !unplugReported_[s] && check && check->number() != 0) {
                qb::Struct p;
                p.items.push_back({qbkey::key("device_num"), qs::Vm::makeInt(s)});
                front_->vm().spawn(qbkey::key("controller_unplugged"), p);
                unplugReported_[s] = true;
            }
            if (!lost) unplugReported_[s] = false;
        }
    }
    // Enhanced > Developer Debug Menu (GD04-041): GH3's development switch
    // enable_button_cheats (retail 0, set by the boot when NotCD is false):
    // the pause menu's DEBUG MENU and the button cheats.
    if (front_) {
        const bool dev = enhanced_.on("dev_debug_menu");
        if (dev || devDebugMenu_) front_->vm().setGlobal(qbkey::key("enable_button_cheats"), qs::Vm::makeInt(dev ? 1 : 0));
        devDebugMenu_ = dev;
    }
    if (song_) {
        canvas_.begin(SDL_Color{0, 0, 0, 255});
        // An analog bar's real position, not just past its threshold.
        if (song_->twoPlayers()) {
            for (int p = 0; p < 2; ++p)
                if (input_.analogWhammy(p)) song_->setWhammy(p, input_.whammyValue(p));
        } else if (input_.analogWhammy() || input_.shakeWhammyActive()) {
            song_->setWhammy(0, std::max(input_.whammyValue(), input_.whammyValue(0)));
        }
        const bool more = song_->frame(canvas_, mixer_, textures_.get());
        if (front_) front_->songClockMs = song_->songMs();
        if (hooks_) hooks_->setPaused(front_->songPaused), hooks_->frame(*song_, song_->songMs());
        if (front_) {
            // GH3's scripts and elements over the song (the pause menu over the frozen song).
            const uint64_t nowNs = SDL_GetTicksNS();
            const double dt = lastFrameNs_ ? double(nowNs - lastFrameNs_) / 1e6 : 0;
            lastFrameNs_ = nowNs;
            front_->frame(canvas_, dt);
            if (hooks_) hooks_->draw(canvas_, song_->songMs());
            if (front_->songPaused) pauseSeen_ = pauseSeen_ || front_->flow.paused;
        }
        overlay();
        captureMenuScreenshot(canvas_, double(SDL_GetTicksNS() - startNs_) / 1e6, song_->songMs());
        canvas_.end();
        if (front_ && front_->songKill) {
            // Quit or restart from the pause menu: the song is abandoned; a
            // restart's start_gem_scroller follows as a new request.
            front_->songKill = false, front_->songPaused = false, front_->songPlaying = false;
            if (hooks_) hooks_->end(true);
            song_.reset();
            mixer_.stop();
            input_.setInGame(false);
            if (walk_) ++walk_->songsEnded;
            return;
        }
        input_.setMenuOverSong(front_ && (front_->songPaused || front_->attractMode()));
        if (front_ && front_->songPaused &&
            ((pauseSeen_ && !front_->flow.paused) || (!pauseSeen_ && SDL_GetTicksNS() - pausedAtNs_ > 3000000000ull))) {
            // Resumed (UnPauseGame), or the scripts never paused.
            SDL_Log("song: resumed (%s)", pauseSeen_ ? "UnPauseGame" : "the scripts never paused");
            front_->songPaused = false;
            mixer_.setPaused(false);
        }
        if (!more && front_) {
            // Back to GH3's own flow (results, setlist).
            FrontEnd::SongResult r;
            const PlayerStats& st = song_->stats();
            r.won = !song_->failed(), r.score = st.score, r.notesHit = st.notesHit, r.totalNotes = st.totalNotes;
            r.bestRun = st.bestRun, r.phrasesHit = st.phrasesCaptured, r.phrasesTotal = song_->phraseCount();
            r.baseScore = song_->baseScore(), r.health = st.health, r.starPower = st.starPower;
            const Match& m = song_->match();
            r.maxNotes = m.maxNotes(0);
            if (m.players() == 2) {
                const PlayerStats& s2 = m.player(1).stats();
                r.twoPlayers = true;
                r.p2.score = s2.score, r.p2.notesHit = s2.notesHit, r.p2.totalNotes = s2.totalNotes, r.p2.bestRun = s2.bestRun;
                r.p2.phrasesHit = s2.phrasesCaptured, r.p2.phrasesTotal = m.phraseCount(1), r.p2.baseScore = m.baseScore(1);
                r.p2.health = s2.health, r.p2.starPower = s2.starPower, r.p2.maxNotes = m.maxNotes(1);
                r.battleKo = m.mode() == GameMode::Battle && (m.outcome() == Match::Outcome::P1Wins || m.outcome() == Match::Outcome::P2Wins);
                for (int p = 0; p < 2; ++p) r.finalBlow[size_t(p)] = m.battle(p).finalBlow, r.cards[size_t(p)] = m.battle(p).cards;
                // Face-off and battle never fail (battle's KO goes through song_failed: battleKo) [C 0x8210FD18].
                if (m.mode() == GameMode::FaceOff || m.mode() == GameMode::ProFaceOff || m.mode() == GameMode::Battle) r.won = true;
            }
            if (hooks_) hooks_->end(false);
            // GH3's SongWon / SongLost transition runs on over the venue
            // (the band's win / lose stance, cameras_win / _lose) [C data].
            outroSong_ = std::move(song_);
            outroFrom_ = outroSong_->lastMs(), outroWall_ = double(SDL_GetTicksNS()) / 1e6;
            mixer_.stop();
            input_.setInGame(false);
            front_->songEnded(r);
            if (walk_) ++walk_->songsEnded;
            return;
        }
        if (!more) {
            const PlayerStats& s = song_->stats();
            std::printf("song: %s, score %lld, %d / %d notes hit, best streak %d\n", s.failed ? "failed" : "complete",
                        static_cast<long long>(s.score), s.notesHit, s.totalNotes, s.bestRun);
            exitCode_ = s.failed ? 1 : 0;
            running_ = false;
        }
        return;
    }
    if (front_ && front_->songRequest.pending) {
        front_->songRequest.pending = false;
        const SongInfo* info = nullptr;
        for (const SongInfo& s : songs_.songs())
            if (s.checksum == front_->songRequest.song) info = &s;
        std::string err;
        outroSong_.reset();
        song_ = std::make_unique<SongScreen>();
        for (int s = 0; s < 2; ++s) hadDevice_[s] = input_.padConnected(s), unplugReported_[s] = false;
        // Enhanced > CHEATS: Autoplay and No Fail Mode (as --autoplay / --no-fail).
        // The attract mode plays itself (player1_status.bot_play) [C script create_attract_mode].
        const bool autoplay = options_.autoplay || walk_ || SDL_getenv("GD04_AUTOPLAY") || enhanced_.on("autoplay") || front_->attractMode();
        const bool noFail = options_.noFail || enhanced_.on("no_fail");
        // The mode and the two players (start_gem_scroller's globals).
        const FrontEnd::SongRequest& rq = front_->songRequest;
        SongSetup setup;
        setup.mode = rq.players == 2 || rq.boss ? gameModeFromKey(rq.gameMode, rq.boss != 0) : GameMode::Solo;
        setup.difficulty = {rq.difficulty, rq.difficulty2};
        setup.rhythm = {rq.part2Rhythm[0], rq.part2Rhythm[1]};
        setup.bossProps = rq.boss;
        setup.suddenDeath = rq.suddenDeath;
        if (rq.suddenDeath) setup.startHealth = {rq.saveHealth[0], rq.saveHealth[1]};
        setup.startMs = rq.startMs, setup.devilFinish = rq.devilFinish;
        setup.endMs = rq.endMs;
        setup.lefty = {rq.lefty[0], rq.lefty[1]};
        // GH3's lag_calibration plus Enhanced > Calibration's audio delay
        // (judged later by it); the video delay draws the highway ahead.
        setup.lagMs = rq.lagMs + std::atof(enhanced_.get("audio_offset").c_str());
        setup.videoOffsetMs = std::atof(enhanced_.get("video_offset").c_str());
        setup.endCredits = rq.endCredits;
        setup.transition = rq.transition;
        // GD04_TRANSITION=<name>: every song starts with that transition (encore, boss, intro...; development aid).
        if (const char* tr = SDL_getenv("GD04_TRANSITION")) setup.transition = qbkey::key(tr);
        // GD04_START_AT=<song ms>: start every song there (development aid; GH3's starttime).
        if (const char* at = SDL_getenv("GD04_START_AT")) setup.startMs = std::max(0.0, std::atof(at));
        if (setup.mode == GameMode::Boss) setup.difficulty[1] = rq.difficulty;  // the 1P menu sets both [C selected_difficulty]
        // A walk's sudden death ends: player 2's autoplay keeps its Death Drains.
        if (walk_ && rq.suddenDeath) setup.autoplayFires[1] = false;
        if (!info || !song_->start(*disc_, *globals_, *info, setup, autoplay, noFail, mixer_, err, textures_.get())) {
            SDL_Log("song: cannot start %s: %s", qbkey::name(front_->songRequest.song).c_str(), err.c_str());
            song_.reset();
            front_->songEnded(FrontEnd::SongResult{});
        } else {
            if (walk_) {
                song_->walkEndMs_ = options_.walk == "lose" ? -1 : 20000.0;
                song_->walkFailMs_ = options_.walk == "lose" ? 10000.0 : -1;
                // Battle and boss walks play long enough for cards and attacks;
                // sudden death (and a boss) plays on to the KO.
                if (setup.mode == GameMode::Battle) song_->walkEndMs_ = setup.suddenDeath ? -1 : 60000.0;
                if (setup.mode == GameMode::Boss) song_->walkEndMs_ = -1;
            }
            front_->songPlaying = true;
            // Common_Intro_Transition's GH_SFX_Intro_WarmUp: the zone's crowd
            // before the song (the port runs the transition's other entries natively) [C data].
            // The zone's own sounds (its pak's .wav: <zone>_intro, the encore cheers), indexed once a zone [C data].
            if (song_->venue()) {
                static std::set<std::string> zonesHeard;
                const std::string& z = song_->venue()->venue().zone;
                if (zonesHeard.insert(z).second) front_->sfx.addPak(*disc_, "DATA/COMPRESSED/ZONES/" + z + ".pak.xen");
            }
            if (song_->introCameras() == "cameras_intro") front_->vm().spawn(qbkey::key("GH_SFX_Intro_WarmUp"), qb::Struct{});
            // GD04_SP_CHARACTER=<id> (AXEL, JUDY, ...): player 1's character_id for
            // the Star Power stage FX (development aid: Do_StarPower_StageFX's switch).
            if (const char* ch = SDL_getenv("GD04_SP_CHARACTER"))
                if (const qb::Value* ps = front_->vm().global(qbkey::key("player1_status")); ps && ps->st) {
                    qb::Value copy = *ps;
                    copy.st = std::make_shared<qb::Struct>(*ps->st);
                    qs::Vm::set(*copy.st, qbkey::key("character_id"), qs::Vm::makeKey(qbkey::key(ch)));
                    front_->vm().setGlobal(qbkey::key("player1_status"), copy);
                }
            input_.setInGame(true);
            if (hooks_) {
                SongHooks* h = hooks_.get();
                song_->onStep = [h](SongScreen& s, double ms) { h->step(s, ms); };
                hooks_->begin(*song_);
            }
            // The HUD's texts in GH3's fonts (num_a9, num_a7, text_a6): scale x the font's line height.
            ui::UiSystem* ui = &front_->ui();
            song_->setHudText(
                [ui](Canvas& c, uint32_t font, const std::string& t, float x, float y, float scale, const std::array<uint8_t, 4>& rgba,
                     uint32_t align, const Hud2D::TextStyle& st) {
                    ui->drawText(c, font, t, x, y, scale * ui->fontLineHeight(font), rgba, align, ui::UiSystem::TextStyle{st.rot, st.sx, st.spacing});
                },
                [ui](uint32_t font, const std::string& t, float scale, float spacing) {
                    return ui->textWidth(font, t, scale * ui->fontLineHeight(font), spacing);
                });
            if (walk_) ++walk_->songsStarted;
        }
        return;
    }
    if (front_ && !front_->movieRequest.empty()) {
        const std::string movie = front_->movieRequest;
        front_->movieRequest.clear();
        // Walks skip the movies (GD04_WALK_MOVIES=1 plays them).
        // A quit during the movie (the window closed, the launcher's
        // GDUL_GAME_QUIT_AFTER) quits the game too.
        // GD04_NO_MOVIES=1 skips them outside walks too (development aid).
        if ((!walk_ && !SDL_getenv("GD04_NO_MOVIES")) || SDL_getenv("GD04_WALK_MOVIES"))
            if (playMovie(*disc_, movie, canvas_, mixer_, input_) == MoviePlayer::Result::Quit) running_ = false;
        lastFrameNs_ = SDL_GetTicksNS();
        return;
    }
    if (front_ && !front_->songPlaying) {
        const uint64_t nowNs = SDL_GetTicksNS();
        const double dt = lastFrameNs_ ? double(nowNs - lastFrameNs_) / 1e6 : 0;
        lastFrameNs_ = nowNs;
        canvas_.begin(SDL_Color{0, 0, 0, 255});
        // The Enhanced backdrop, made once while the menus run (drawn unseen
        // under the first screen, which covers it).
        if (!backdrop_ && !backdropTried_ && front_->flowState() == "bootup_press_any_button_fs") prepareMenuBackdrop();
        if (ports_ && ports_->isOpen()) {
            // The port's screen over a venue with the band (gd02's backdrop:
            // App::menuBackdrop), GH3's scripts waiting meanwhile.
            drawMenuBackdrop(dt);
            ports_->update(dt);
            ports_->draw(canvas_);
            front_->sfx.update();
        } else {
            // The ended song's venue under GH3's transition (its time from
            // the song's end; the results come after it) [C data Default_SongWon_Transition].
            if (outroSong_) {
                const double el = double(nowNs) / 1e6 - outroWall_;
                if (const qb::Value* t = front_ ? front_->vm().global(qbkey::key("current_transition")) : nullptr)
                    if (t->key == qbkey::key("preencore") || t->key == qbkey::key("preboss"))
                        outroSong_->outroCameras(outroFrom_ + el, t->key == qbkey::key("preencore") ? "cameras_preencore" : "cameras_preboss");
                if (el >= outroSong_->transitionMs() || !outroSong_->drawOutro(canvas_, outroFrom_ + el)) {
                    SDL_Log("song: end transition drawn over the venue for %lld frames", outroFrames_);
                    outroSong_.reset(), outroFrames_ = 0;
                } else {
                    ++outroFrames_;
                }
            }
            front_->frame(canvas_, dt);
            if (ports_) ports_->drawItems(canvas_, front_->flowState());
        }
        overlay();
        captureMenuScreenshot(canvas_, double(nowNs - startNs_) / 1e6, -1e9);  // no song
        canvas_.end();
        return;
    }
    canvas_.begin(SDL_Color{0, 0, 0, 255});
    const SDL_Color white{255, 255, 255, 255}, grey{160, 160, 160, 255};
    canvas_.textCentered(640, 300, 3, white, "Guitar Hero III: Legends of Rock");
    if (!disc_) {
        std::string status = picker_.status();
        canvas_.textCentered(640, 360, 2, grey, status.empty() ? "Press Enter to choose your disc image" : status);
    } else {
        canvas_.textCentered(640, 360, 2, grey, message_);
    }
    canvas_.textCentered(640, 680, 1, grey, std::string(app::kDisplayName) + " " + app::kVersion);
    const double wallMs = double(SDL_GetTicksNS() - startNs_) / 1e6;
    captureMenuScreenshot(canvas_, wallMs, -1e9);  // no song
    canvas_.end();
}

int App::run() {
    SDL_SetAppMetadata(app::kDisplayName, app::kVersion, app::kAppId);
    Input::prepareSdl();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "%s: cannot start SDL: %s\n", app::kProgram, SDL_GetError());
        return 2;
    }
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | gfx::prepareWindow();
#ifdef __ANDROID__
    flags |= SDL_WINDOW_FULLSCREEN;
#endif
    window_ = SDL_CreateWindow(app::kWindowTitle, int(kScreenW), int(kScreenH), flags);
    // Touch: window-normalised positions to menu units (windowToMenu, the 16:9 picture).
    input_.setTouchToMenu([this](float nx, float ny, float& mx, float& my) {
        int ww = 0, wh = 0;
        SDL_GetWindowSize(window_, &ww, &wh);
        windowToMenu(nx * float(ww), ny * float(wh), mx, my);
    });
    setAppWindowIcon(window_);
    installDesktopEntry();
    std::string error;
    if (!window_ || !canvas_.init(window_, error)) {
        std::fprintf(stderr, "%s: cannot open the window: %s\n", app::kProgram, window_ ? error.c_str() : SDL_GetError());
        if (window_) SDL_DestroyWindow(window_);
        SDL_Quit();
        return 2;
    }
    if (!mixer_.open(error)) SDL_Log("audio: %s (continuing without sound)", error.c_str());
    if (const char* k = SDL_getenv("GD04_KEYS")) keys_ = std::make_unique<MenuKeys>(k);
    // Walks keep their own settings (enhanced.json, controllers.json), fresh
    // each run, out of the player's.
    if (!options_.walk.empty() && !prefDir().empty()) {
        saveDir_ = prefDir() + "walktest/";
        std::error_code ec;
        std::filesystem::remove(saveDir_ + "enhanced.json", ec);
        std::filesystem::remove(saveDir_ + "controllers.json", ec);
        // The game's save too (GD04_WALK_KEEP_SAVE=1 keeps it from the last walk).
        if (!SDL_getenv("GD04_WALK_KEEP_SAVE")) {
            std::filesystem::remove(saveDir_ + "save.json", ec);
            std::filesystem::remove(saveDir_ + "save.json.bak", ec);
        }
        std::filesystem::create_directories(saveDir_, ec);
        input_.setSaveDir(saveDir_);
        save::setFolder(saveDir_);
    }
    enhanced_.load(saveDir_.empty() ? prefDir() : saveDir_);
    applyEnhanced("");

    const std::string path = options_.dataPath.empty() ? loadRememberedPath() : options_.dataPath;
    if (!path.empty()) loadData(path);
    if (disc_) {
        // The global pak's textures (the highway, HUD and menu sprites live there) [C data].
        textures_ = std::make_unique<TextureCache>(canvas_.renderer().device());
        const uint64_t t0 = SDL_GetTicksNS();
        const int n = textures_->loadPak(*disc_, "DATA/COMPRESSED/ZONES/global.pak.xen");
        if (textures_->sharpened) SDL_Log("textures: %d sharpened (Sharper Text & Sprites)", textures_->sharpened);
        const int a = textures_->loadPak(*disc_, "DATA/COMPRESSED/PAK/axel_1.pak.xen");  // the default guitarist [I]
        SDL_Log("textures: %d from global.pak, %d from axel_1.pak (%.0f ms)", n, a, double(SDL_GetTicksNS() - t0) / 1e6);
    }
    if ((!options_.song.empty() || !options_.walk.empty()) && !disc_) {
        std::fprintf(stderr, "%s: %s\n", app::kProgram, message_.empty() ? "no disc image given" : message_.c_str());
        running_ = false, exitCode_ = 2;
    } else if (!options_.walk.empty() && !(walk_ = std::make_unique<Walk>(options_.walk))->known()) {
        std::fprintf(stderr, "%s: unknown walk '%s' (quickplay, lose, back, career, pause, glitches, coop, faceoff, proface, battle, boss, boot, mouse, gamepads, stickyaxis)\n", app::kProgram,
                     options_.walk.c_str());
        running_ = false, exitCode_ = 2;
    } else if (!options_.song.empty()) {
        const SongInfo* s = songs_.find(options_.song);
        song_ = std::make_unique<SongScreen>();
        std::string err;
        if (!s) {
            std::fprintf(stderr, "%s: no song '%s'\n", app::kProgram, options_.song.c_str());
            running_ = false, exitCode_ = 2;
            song_.reset();
        } else if (SongSetup one; one.difficulty = {options_.difficulty, options_.difficulty},
                   one.startMs = SDL_getenv("GD04_START_AT") ? std::max(0.0, std::atof(SDL_getenv("GD04_START_AT"))) : 0.0,
                   // The intro quickplay plays (GD04_TRANSITION overrides; fastintro for the 3 s one).
                   one.transition = qbkey::key(SDL_getenv("GD04_TRANSITION") ? SDL_getenv("GD04_TRANSITION") : "intro"),
                   !song_->start(*disc_, *globals_, *s, one, options_.autoplay, options_.noFail, mixer_, err, textures_.get())) {
            std::fprintf(stderr, "%s: %s\n", app::kProgram, err.c_str());
            running_ = false, exitCode_ = 2;
            song_.reset();
        } else {
            input_.setInGame(true);
        }
    } else if (disc_ && textures_ && !SDL_getenv("GD04_NO_FRONTEND")) {
        // GH3's own front end, from its boot script.
        front_ = std::make_unique<FrontEnd>(*disc_, *globals_, *textures_, canvas_.renderer().device());
        if (!front_->start()) {
            SDL_Log("front end: guitar_startup did not start");
            front_.reset();
        } else {
            ports_ = std::make_unique<PortScreens>(input_, *front_, enhanced_, [this](const std::string& k) { applyEnhanced(k); });
            ports_->context().action = [this](const std::string& a) { userFileAction(a); };
            if (!envOn("GD04_NO_HOOKS")) hooks_ = std::make_unique<SongHooks>(*front_, mixer_, *disc_, *textures_);
            addVenueScriptCalls();
        }
    }
    if (walk_ && walk_->twoPlayers()) input_.setSplitKeyboard(true);  // two-player walks: player 2 on the keyboard's other half
    const bool stats = envOn("GD04_STATS");
    uint64_t statsStart = SDL_GetTicksNS();
    int statsFrames = 0;
    // GD04_QUIT_AFTER=<ms>: quits by itself (headless smoke runs).
    const char* quitAfter = SDL_getenv("GD04_QUIT_AFTER");
    startNs_ = SDL_GetTicksNS();
    while (running_) {
        const uint64_t frameStart = SDL_GetTicksNS();
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        if (keys_) {
            for (const MenuKeys::Key& k : keys_->due(double(frameStart - startNs_) / 1e6)) {
                // GD04_KEYS: a scripted button as the input layer would report it.
                if (k.mouse) {  // the mouse, as its events would (menu units)
                    if (k.mouse >= 4) menuAction(InputEvent{k.mouse == 4 ? Action::Up : Action::Down, true, SDL_GetTicksNS(), ControllerMode::Guitar, 0});
                    else menuMouse(k.mx, k.my, k.mouse == 2 ? 1 : k.mouse == 3 ? 3 : 0);
                    continue;
                }
                if (k.keycode) {  // a real key through the input layer
                    walkKey(k.keycode);
                    continue;
                }
                Action a;
                switch (k.button) {
                case 6: a = Action::Confirm; break;
                case 4: a = Action::Back; break;
                case 5: a = Action::Fret2; break;
                case 7: a = Action::Fret3; break;
                case 2: a = Action::Fret4; break;
                case 11: a = Action::Start; break;
                case 8: a = Action::StarPower; break;
                case 12: a = Action::Up; break;
                case 13: a = Action::Right; break;
                case 14: a = Action::Down; break;
                case 15: a = Action::Left; break;
                default: continue;
                }
                for (int edge : {1, -1}) {
                    if ((k.edge == 1 && edge == -1) || (k.edge == -1 && edge == 1)) continue;
                    InputEvent ev{a, edge == 1, SDL_GetTicksNS(), ControllerMode::Guitar, k.player};
                    if (song_) songEvent(ev);  // as a walk's (Start pauses)
                    else if (front_) menuAction(ev);
                }
            }
        }
        if (!disc_)
            if (std::string picked = picker_.takePicked(); !picked.empty()) loadData(picked);
        if (walk_ && front_) {
            for (const Walk::Press& p : walk_->update(*front_, front_->songPlaying, double(frameStart - startNs_) / 1e6, this,
                                                     ports_ ? ports_->current() : std::string()))
                for (bool down : {true, false}) {
                    InputEvent ev{p.a, down, SDL_GetTicksNS(), ControllerMode::Guitar, p.player};
                    if (song_) songEvent(ev);
                    else menuAction(ev);
                }
            if (walk_->done() || walk_->failed()) {
                exitCode_ = walk_->finish(*front_);
                running_ = false;
            }
        }
        if (!running_) break;
        if (front_) {
            // The controllers the scripts see (GetActiveControllers): the player slots with a device.
            uint32_t m = 1;
            for (int i = 1; i < 4; ++i)
                if (input_.padConnected(i)) m |= 1u << i;
            front_->activeControllers = m;
        }
        frame();
        if (quitAfter && double(SDL_GetTicksNS() - startNs_) / 1e6 >= std::atof(quitAfter)) running_ = false;
        updatePointer();
        // Enhanced > In-Game Max FPS (the menus keep the engine's own cap).
        const std::string mf = enhanced_.get("max_fps");
        const uint64_t minNs = !song_ ? kMinFrameNs : mf == "unlimited" ? 0 : uint64_t(1e9 / std::max(30.0, std::atof(mf.c_str())));
        const uint64_t spent = SDL_GetTicksNS() - frameStart;
        if (spent < minNs) SDL_DelayNS(minNs - spent);
        if (stats) {
            ++statsFrames;
            const uint64_t now = SDL_GetTicksNS();
            if (now - statsStart >= 2'000'000'000ull) {
                SDL_Log("stats: %.1f fps", statsFrames * 1e9 / double(now - statsStart));
                statsStart = now, statsFrames = 0;
            }
        }
    }
    song_.reset();
    outroSong_.reset();  // its venue's GPU objects before the device
    backdrop_.reset(), backdropChart_.reset();  // the menu backdrop's too
    ports_.reset();
    hooks_.reset();
    front_.reset();
    textures_.reset();
    mixer_.stop();
    input_.close();
    mixer_.close();
    canvas_.shutdown();
    SDL_DestroyWindow(window_);
    SDL_Quit();
    return exitCode_;
}

}  // namespace

int runApp(const Options& options) {
    App app(options);
    return app.run();
}

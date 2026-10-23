// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "scene/character.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>

#include "data/dta.h"
#include "data/game_data.h"

namespace {

// config/gen/macros.dta (and the system macros it includes).
constexpr uint32_t kTempoMask = 3;            // kTempoMedium 1 | kTempoFast 2
constexpr uint32_t kWalkMask = 0x3fe0;        // kWalkTurn .. kWalkExtreme
constexpr uint32_t kAnimRealTime = 16;

// The integer #defines of the macro files, by name.
std::map<std::string, int> loadMacros(const GameData& data) {
    std::map<std::string, int> out;
    for (const char* path : {"config/gen/macros.dtb", "../../system/run/config/gen/macros.dtb"}) {
        if (!data.exists(path)) continue;
        DataNode root = parseDtb(data.read(path));
        for (size_t i = 0; i + 1 < root.children.size(); ++i) {
            const DataNode& d = root.children[i];
            const DataNode& v = root.children[i + 1];
            if (d.type == DataNode::Type::Define && v.isList() && v.children.size() == 1 &&
                v.children[0].type == DataNode::Type::Int)
                out[d.s] = v.children[0].i;
        }
    }
    return out;
}

const DataNode* defineValue(const DataNode& root, const std::string& name) {
    for (size_t i = 0; i + 1 < root.children.size(); ++i)
        if (root.children[i].type == DataNode::Type::Define && root.children[i].s == name) return &root.children[i + 1];
    return nullptr;
}

const DataNode* entry(const DataNode& list, const std::string& key) {
    for (const DataNode& c : list.children)
        if (c.isList() && !c.children.empty() && c.children[0].isText() && c.children[0].s == key) return &c;
    return nullptr;
}


bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

// config/guitars.dta: a guitar's first skin, or a named skin -> (scene file, material).
// The skin's outfit number indexes the GUITARS list of charsys.dta.
std::pair<std::string, std::string> guitarFiles(const GameData& data, const std::string& guitar,
                                                const std::map<std::string, int>& macros) {
    std::string file = "guitar_sg", mat;
    DataNode guitars = parseDtb(data.read(dtbPath("config/guitars.dta")));
    DataNode charsys = parseDtb(data.read(dtbPath("charsys/charsys.dta")));
    const DataNode* g = entry(guitars, guitar);
    const DataNode* skins = g ? g->find("skins") : nullptr;
    const DataNode* chosen = skins && skins->children.size() > 1 ? &skins->children[1] : nullptr;
    if (!g)  // a skin id
        for (const DataNode& gi : guitars.children)
            if (const DataNode* sk = gi.isList() ? gi.find("skins") : nullptr)
                if (const DataNode* e = entry(*sk, guitar)) chosen = e;
    if (!chosen) return {file, mat};
    const DataNode& skin = *chosen;
    int outfit = -1;
    if (const DataNode* o = skin.find("outfit"); o && o->children.size() > 1) {
        const DataNode& v = o->children[1];
        if (v.type == DataNode::Type::Int) outfit = v.i;
        else if (auto it = macros.find(v.s); it != macros.end()) outfit = it->second;
    }
    if (const DataNode* m = skin.find("mat"); m && m->children.size() > 1) mat = m->textAt(1) + ".mat";
    // GUITARS: ((guitars (sg (load "../guitars/guitar_sg")) ...))
    if (const DataNode* list = defineValue(charsys, "GUITARS"); list && !list->children.empty())
        if (const DataNode* gl = entry(*list, "guitars"); gl && outfit >= 0 && size_t(outfit) + 1 < gl->children.size())
            if (const DataNode* load = gl->children[size_t(outfit) + 1].find("load"); load && load->children.size() > 1) {
                std::string p = load->textAt(1);
                file = p.substr(p.rfind('/') + 1);
            }
    return {file, mat};
}

}  // namespace

std::pair<std::string, std::string> Character::guitarModel(const GameData& data, const std::string& guitarOrSkin) {
    return guitarFiles(data, guitarOrSkin, loadMacros(data));
}

Character::Character(Renderer& renderer, const GameData& data, const Spec& spec) : tempo_(spec.tempo), servoLevel_(spec.servos) {
    renderer_ = &renderer;
    std::map<std::string, int> macros = loadMacros(data);
    const std::string& archetype = spec.model;
    const std::string& folder = spec.folder.empty() ? archetype : spec.folder;
    body_ = std::make_unique<Scene>(data.read("charsys/" + folder + "/gen/" + archetype + ".rnd_ps2"));
    auto [guitarFile, guitarMat] = spec.guitar.empty() ? std::pair<std::string, std::string>()
                                                       : guitarFiles(data, spec.guitar, macros);
    if (std::string gp = "charsys/guitars/gen/" + guitarFile + ".rnd_ps2"; !guitarFile.empty() && data.exists(gp)) {
        guitar_ = std::make_unique<Scene>(data.read(gp));
        if (!guitarMat.empty() && guitar_->find(guitarMat))
            for (SceneObject& o : guitar_->objects())
                if (o.mesh && startsWith(o.name, "guitar") && o.mesh->mat != "guitar_strings.mat") o.mesh->mat = guitarMat;
    }

    // Skeleton: owners (the character's copy of shared bones first), parents
    // from every file's children lists, then the `parent` fields.
    for (Scene* s : {body_.get(), guitar_.get()}) {
        if (!s) continue;
        for (SceneObject& o : s->objects())
            if (o.transformable()) objects_.emplace(o.name, &o);
        for (SceneObject& o : s->objects())
            if (const Transformable* t = o.transformable())
                for (const std::string& c : t->children) parent_.emplace(c, o.name);
    }
    for (auto& [name, o] : objects_) {
        const Transformable& t = *o->transformable();
        if (!t.parent.empty() && t.parent != name) parent_.emplace(name, t.parent);
        bind_[name] = t.local;
    }
    for (const auto& [child, parent] : parent_) children_[parent].push_back(child);

    // Clips: the clip-set macro of the anims file.
    if (std::string animsPath = dtbPath(spec.animsFile); data.exists(animsPath)) {
        DataNode anims = parseDtb(data.read(animsPath));
        for (size_t i = 0; i + 1 < anims.children.size(); ++i) {  // the file's own int #defines (C_BAD, ...)
            const DataNode& d = anims.children[i];
            const DataNode& v = anims.children[i + 1];
            if (d.type == DataNode::Type::Define && v.isList() && v.children.size() == 1 &&
                v.children[0].type == DataNode::Type::Int)
                macros[d.s] = v.children[0].i;
        }
        for (size_t i = 0; i + 1 < anims.children.size(); ++i) {
            const DataNode& d = anims.children[i];
            if (d.type != DataNode::Type::Define || d.s != spec.animsMacro) continue;
            // ((main::x.cset (directory "charsys/x/anims") (animations (clip (flags ...) ...) ...)))
            int setNo = -1;
            for (const DataNode& set : anims.children[i + 1].children) {
                const DataNode* dir = set.find("directory");
                const DataNode* list = set.find("animations");
                if (!dir || !list) continue;
                ++setNo;
                if (setNo == 0 && !set.children.empty() && set.children[0].isText()) {
                    // <directory>/<set name>.acg (FUN_0017f440), e.g. main::hiphop.cset -> hiphop.acg
                    std::string n = set.children[0].s;
                    n = n.substr(n.rfind(':') == std::string::npos ? 0 : n.rfind(':') + 1);
                    n = n.substr(0, n.find('.'));
                    if (std::string g = dir->textAt(1) + "/" + n + ".acg"; data.exists(g)) {
                        Bytes b = data.read(g);
                        ByteReader r(b);
                        r.u32le();  // version
                        graph_.resize(r.u32le());
                        for (auto& edges : graph_) {
                            edges.resize(r.u32le());
                            for (Edge& e : edges) e = {int(r.u32le()), r.f32le(), r.f32le()};
                        }
                    }
                }
                int index = -1;
                for (size_t a = 1; a < list->children.size(); ++a) {
                    const DataNode& clip = list->children[a];
                    if (!clip.isList() || clip.children.empty() || !clip.children[0].isText()) continue;
                    ++index;
                    uint32_t flags = 0;
                    if (const DataNode* f = clip.find("flags"))
                        for (size_t k = 1; k < f->children.size(); ++k) {
                            const DataNode& v = f->children[k];
                            if (v.type == DataNode::Type::Int) flags |= uint32_t(v.i);
                            else if (auto it = macros.find(v.s); it != macros.end()) flags |= uint32_t(it->second);
                        }
                    // Menu characters keep only the kGuitarUI clips; in game those are skipped.
                    if (spec.meta ? !(flags & kGuitarUI) : (flags & kGuitarUI) != 0) continue;
                    if (spec.restrict && !(flags & spec.restrict)) continue;
                    int hand = -1;  // HAND_CLAP / HAND_FIST / HAND_HORN enter events: set_hand 0 / 1 / 2
                    for (const DataNode& e : clip.children) {
                        if (!e.isText()) continue;
                        if (e.s == "HAND_CLAP") hand = 0;
                        else if (e.s == "HAND_FIST") hand = 1;
                        else if (e.s == "HAND_HORN") hand = 2;
                    }
                    std::string path = dir->textAt(1) + "/gen/" + clip.children[0].s + ".acp";
                    if (!data.exists(path)) continue;
                    try {
                        clips_.push_back({std::make_unique<AnimClip>(data.read(path)), flags, hand, setNo, index});
                    } catch (const std::exception& ex) {
                        SDL_Log("character %s: clip %s: %s", archetype.c_str(), path.c_str(), ex.what());
                    }
                }
            }
            break;
        }
        // The hand clips (<X>_HAND_ANIMS, charsys.dta my_preload): one set for
        // both hand drivers, HERO_HAND_ANIMATIONS from hero_graphs.dta (blend
        // width 0.24, no flags, no graph).
        if (spec.hands && !spec.meta) loadHandClips(data, anims, spec);
    }

    // Draw the body's top view without the low-detail LOD, and the guitar's
    // visible geometry (spots and bones are markers).
    if (const SceneObject* top = body_->find("top.view"); top && top->view) {
        for (const std::string& n : top->view->draw.draws)
            if (!startsWith(n, "lod1")) bodyDraws_.push_back(n);
    } else {  // crowd models: every mesh
        for (const SceneObject& o : body_->objects())
            if (o.mesh && !startsWith(o.name, "bone_") && !startsWith(o.name, "spot_")) bodyDraws_.push_back(o.name);
    }
    if (guitar_)
        for (const SceneObject& o : guitar_->objects())
            if (o.mesh && !startsWith(o.name, "spot_") && !startsWith(o.name, "bone_")) guitarDraws_.push_back(o.name);

    auto lookup = [this](const std::string& n) -> const SceneObject* {
        if (const SceneObject* o = find(n)) return o;
        return world_ ? world_(n) : nullptr;
    };
    bodyRenderer_ = std::make_unique<SceneRenderer>(renderer, *body_);
    bodyRenderer_->setLookup(lookup);
    if (guitar_) {
        guitarRenderer_ = std::make_unique<SceneRenderer>(renderer, *guitar_);
        guitarRenderer_->setLookup(lookup);
    }
}

void Character::loadHandClips(const GameData& data, const DataNode& anims, const Spec& spec) {
    std::string macro = spec.animsMacro.substr(0, spec.animsMacro.rfind("_ANIMS")) + "_HAND_ANIMS";
    const DataNode* def = nullptr;
    for (size_t i = 0; i + 1 < anims.children.size(); ++i)
        if (anims.children[i].type == DataNode::Type::Define && anims.children[i].s == macro) def = &anims.children[i + 1];
    if (!def || def->children.empty() || !def->children[0].isList()) return;
    const DataNode& set = def->children[0];
    const DataNode* dir = set.find("directory");
    if (!dir) return;
    DataNode graphs;
    const DataNode* list = set.find("animations");
    if (!list && data.exists(dtbPath("charsys/hero_graphs.dta"))) {
        graphs = parseDtb(data.read(dtbPath("charsys/hero_graphs.dta")));
        for (size_t i = 0; i + 1 < graphs.children.size(); ++i)
            if (graphs.children[i].type == DataNode::Type::Define && graphs.children[i].s == "HERO_HAND_ANIMATIONS")
                list = graphs.children[i + 1].find("animations");
    }
    if (!list) return;
    for (size_t a = 1; a < list->children.size(); ++a) {
        const DataNode& clip = list->children[a];
        if (!clip.isList() || clip.children.empty() || !clip.children[0].isText()) continue;
        std::string path = dir->textAt(1) + "/gen/" + clip.children[0].s + ".acp";
        if (!data.exists(path)) continue;
        try {
            handClips_.push_back({std::make_unique<AnimClip>(data.read(path)), 0, -1, 1, int(a - 1)});
        } catch (const std::exception& ex) {
            SDL_Log("character: hand clip %s: %s", path.c_str(), ex.what());
        }
    }
    // FUN_002862c8: bone_fret is detached from its parent; its world is its local.
    if (!handClips_.empty() && objects_.count("bone_fret.mesh")) {
        if (auto p = parent_.find("bone_fret.mesh"); p != parent_.end()) std::erase(children_[p->second], "bone_fret.mesh");
        parent_.erase("bone_fret.mesh");
        fretDetached_ = true;
    }
}

int Character::handClip(const std::string& name) const {
    for (size_t i = 0; i < handClips_.size(); ++i)
        if (handClips_[i].clip->name() == name) return int(i);
    return -1;
}

float Character::handBlendWidth(const std::string& clip) const {
    int c = handClip(clip);
    return c >= 0 ? handClips_[size_t(c)].clip->blendWidth() : 0.24f;
}

const std::string& Character::handHead(int side) const {
    static const std::string none;
    const auto& chain = hands_[side & 1];
    return chain.empty() ? none : handClips_[size_t(chain.back().clip)].clip->name();
}

// left_hand.drv / right_hand.drv: kAnimNow blends in at once; a pushed clip
// (FUN_0017fd18) waits `delay` beats, then blends in from frame start.
void Character::handPlay(int side, const std::string& clip, int mode, float delay, bool pushed) {
    int c = handClip(clip);
    if (c < 0) return;
    auto& chain = hands_[side & 1];
    float start = handClips_[size_t(c)].clip->startFrame();
    if (chain.empty()) chain.push_back(Driver{c, mode, start, 0, 1});
    else if (pushed) chain.push_back(Driver{c, mode, start, delay, 0});
    else chain.push_back(Driver{c, mode, start, 0, 1e-10f});
    if (chain.size() > 8) chain.erase(chain.begin(), chain.end() - 8);
}

Character::Spec Character::guitaristSpec(const GameData& data, const std::string& archetype, const std::string& guitar,
                                         int tempo, bool meta) {
    Spec spec{archetype, guitar, "", "", tempo, 0, ""};
    std::string stem = archetype;
    if (!data.exists(dtbPath("charsys/" + stem + "_anims.dta"))) stem = stem.substr(0, stem.find('_'));
    spec.animsFile = "charsys/" + stem + "_anims.dta";
    for (char c : stem) spec.animsMacro += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    spec.animsMacro += meta ? "_UI_ANIMS" : "_ANIMS";
    spec.meta = meta;
    spec.hands = !meta;
    return spec;
}

const SceneObject* Character::find(const std::string& name) const {
    if (auto it = objects_.find(name); it != objects_.end()) return it->second;
    if (const SceneObject* o = body_->find(name)) return o;
    return guitar_ ? guitar_->find(name) : nullptr;
}

void Character::setShowing(const std::string& mesh, bool showing) {
    if (SceneObject* o = body_->find(mesh); o && o->drawable()) o->drawable()->showing = showing;
}

int Character::hand() const { return chain_.empty() ? -1 : clips_[size_t(chain_.back().clip)].hand; }

uint32_t Character::clipFlags() const { return chain_.empty() ? 0 : clips_[size_t(chain_.back().clip)].flags; }

const std::string& Character::clipName() const {
    static const std::string none;
    return chain_.empty() ? none : clips_[size_t(chain_.back().clip)].clip->name();
}

namespace {

float smooth(float b) { return 0.5f - 0.5f * std::cos(3.14159265f * std::clamp(b, 0.0f, 1.0f)); }

float modPos(float x, float a) {
    float r = std::fmod(x, a);
    return r < 0 ? r + a : r;
}

}  // namespace

// FUN_0017e9a0: the least-recently-used clip with every wanted flag (ties go
// to the first in dta order); clips of another anim tempo only as a fallback.
int Character::select(uint32_t want) {
    auto pick = [&](uint32_t cat, bool matchTempo) {
        int best = -1;
        for (size_t i = 0; i < clips_.size(); ++i) {
            uint32_t f = clips_[i].flags;
            if ((f & cat) != cat) continue;
            if (matchTempo && (f & kTempoMask) && !(f & kTempoMask & uint32_t(tempo_))) continue;
            if (best < 0 || clips_[i].used < clips_[size_t(best)].used) best = int(i);
        }
        return best;
    };
    int c = pick(want, true);
    for (uint32_t fallback : {want, kGuitarNormal, kBandActive, kGuitarIdle, kBandIdle})
        if (c < 0) c = pick(fallback, false);
    if (c < 0 && !clips_.empty()) c = 0;
    if (c >= 0) clips_[size_t(c)].used = ++clock_;
    return c;
}

void Character::startAt(int clip, int mode, float frame) {
    start(clip, mode);
    touchClip(clip);
    chain_.back().frame = frame;
}

// AnimClipDriver construction (FUN_0018a038 + transition FUN_0017f1d0).
void Character::start(int clip, int mode) {
    const AnimClip& n = *clips_[size_t(clip)].clip;
    if (chain_.empty() || (mode & 7) == kAnimReset) {
        chain_.assign(1, Driver{clip, mode, n.startFrame(), 0, 1});
        return;
    }
    const Driver& prev = chain_.back();
    Transition t = transition(prev.clip, prev.frame, clip, mode);
    Driver d{clip, mode, t.dest, t.src - prev.frame, 0};
    if (d.delay == 0) d.blend = 1e-10f;
    chain_.push_back(d);
}

Character::Transition Character::transition(int fromClip, float fromFrame, int toClip, int mode) const {
    const Clip& pc = clips_[size_t(fromClip)];
    const AnimClip& p = *pc.clip;
    const AnimClip& n = *clips_[size_t(toClip)].clip;
    Transition t;
    int m = mode & 7;
    if ((m == kAnimFirst || m == kAnimLongest) && pc.set == 0 && clips_[size_t(toClip)].set == 0 && pc.index >= 0 &&
        size_t(pc.index) < graph_.size()) {
        float bestCost = 0;
        for (const Edge& e : graph_[size_t(pc.index)]) {
            if (e.dest != clips_[size_t(toClip)].index || e.src <= fromFrame) continue;
            if (m == kAnimFirst) {
                t = {e.src, e.destFrame, true};
                break;
            }
            float cost = (p.endFrame() - e.src) * (p.endFrame() - e.src) + (e.destFrame - n.startFrame()) * (e.destFrame - n.startFrame());
            if (!t.found || cost < bestCost) t = {e.src, e.destFrame, true}, bestCost = cost;
        }
    }
    if (!t.found) {
        t.src = fromFrame;
        if (m == kAnimLongest) t.src = std::max(fromFrame, p.endFrame() - n.blendWidth() * 0.5f);
        t.dest = n.startFrame();
        if (int a = int(n.playFlags() & 0xf); a > 0) {
            float d = modPos(t.src, float(a)) - modPos(n.startFrame(), float(a));
            t.dest = n.startFrame() + (d < 0 ? d + float(a) : d);
        }
        t.found = true;  // computed fallback
    }
    return t;
}

Character::Transition Character::explicitEdge(int fromClip, int toClip) const {
    const Clip& pc = clips_[size_t(fromClip)];
    const AnimClip& p = *pc.clip;
    const AnimClip& n = *clips_[size_t(toClip)].clip;
    Transition t;
    if (pc.set != 0 || clips_[size_t(toClip)].set != 0 || pc.index < 0 || size_t(pc.index) >= graph_.size()) return t;
    float bestCost = 0;
    for (const Edge& e : graph_[size_t(pc.index)]) {
        if (e.dest != clips_[size_t(toClip)].index) continue;
        float cost = (p.endFrame() - e.src) * (p.endFrame() - e.src) + (e.destFrame - n.startFrame()) * (e.destFrame - n.startFrame());
        if (!t.found || cost < bestCost) t = {e.src, e.destFrame, true}, bestCost = cost;
    }
    return t;
}

void Character::play(uint32_t flags, int mode) {
    if (int c = select(flags); c >= 0) start(c, mode);
}

void Character::playAt(uint32_t flags, float eventBeat) {
    int c = select(flags);
    if (c < 0) return;
    const AnimClip& n = *clips_[size_t(c)].clip;
    if (chain_.empty()) {
        chain_.assign(1, Driver{c, 0, n.startFrame(), 0, 1});
        return;
    }
    chain_.push_back(Driver{c, 0, n.startFrame(), eventBeat - n.blendWidth() - beat_, 0});
}

// AnimDriver / AnimClipDriver Poll: oldest first, each new clip waiting until
// its predecessor reaches the transition frame; frames advance by the beat
// delta and snap to the beat phase (FUN_0018a9d0).
void Character::pollChain(std::vector<Clip>& clips, std::vector<Driver>& chain, float beat, float dBeat, float dSec) {
    for (size_t i = 0; i < chain.size(); ++i) {
        Driver& d = chain[i];
        const AnimClip& c = *clips[size_t(d.clip)].clip;
        bool realtime = c.playFlags() & kAnimRealTime;
        d.delay -= i > 0 ? chain[i - 1].dEff : (realtime ? dSec : dBeat);
        d.dEff = 0;
        if (d.delay >= 0) continue;
        float step = realtime ? dSec * c.framesPerSec() : dBeat;
        d.frame += step;
        float r = 0;
        if (int a = d.align >= 0 ? d.align : int(c.playFlags() & 0xf); a > 0 && !realtime && !(d.mode & kAnimLoop)) {
            r = modPos(beat - d.frame, float(a));
            if (r > a * 0.5f) r -= float(a);
        }
        d.frame += r;
        d.dEff = step + r;
        if (d.blend < 1) d.blend = c.blendWidth() <= 0 ? 1.0f : std::min(1.0f, d.blend + step / c.blendWidth());
        if ((d.mode & kAnimLoop) && d.frame > c.endFrame()) d.frame = c.startFrame() + (d.frame - c.endFrame());
    }
    // A clip fully blended in drops everything older.
    for (size_t i = chain.size(); i-- > 1;)
        if (chain[i].blend >= 1) {
            chain.erase(chain.begin(), chain.begin() + long(i));
            break;
        }
}

// ---------------------------------------------------------------- face

struct Character::Face {
    std::unique_ptr<Scene> scene;
    struct Bind {
        MeshObj* target;
        std::vector<const MeshObj*> poses;  // pose k = morph frame k
    };
    std::vector<Bind> binds;
    bool singer = false;
    // The blender (FUN_00287d88): current and target pose, progress, 1 / blend_time.
    int cur = 0, tgt = 0;
    float t = 1, rate = 2, timer = 0, poseLength = 1;
    std::vector<FaceEvent> events;
    int curEvent = -1;
    bool open = false;
    std::mt19937 rng{std::random_device{}()};
    int shownCur = -1, shownTgt = -1;
    float shownS = -1;

    // FUN_00287dd8: a target equal to cur while blending reverses the blend.
    void setTarget(int p) {
        if (t != 1 && p == cur) {
            std::swap(cur, tgt);
            t = 1 - t;
            return;
        }
        if ((t != 1 ? tgt : cur) != p) tgt = p, t = 0;
    }
    void advance(float dt) {
        if (t == 1) return;
        t += rate * dt;
        if (t >= 1) t = 1, cur = tgt;
    }
};

namespace {
Character* gFaceOwner = nullptr;  // DAT_002f5868: one hero face changes at a time
}

void Character::swapMaterials(const std::vector<std::vector<std::string>>& sets, int m) {
    if (m <= 0 || size_t(m) >= sets.size()) return;  // mat 0 keeps the authored outfit
    for (auto& [name, o] : objects_) {
        if (!o->mesh) continue;
        for (size_t k = 0; k < sets.size(); ++k) {
            if (int(k) == m) continue;
            auto it = std::find(sets[k].begin(), sets[k].end(), o->mesh->mat);
            if (it != sets[k].end() && size_t(it - sets[k].begin()) < sets[size_t(m)].size()) {
                o->mesh->mat = sets[size_t(m)][size_t(it - sets[k].begin())];
                break;
            }
        }
    }
}

void Character::enableEyes() {
    eyes_ = objects_.count("L-eye.mesh") && objects_.count("R-eye.mesh") && objects_.count("bone_head.mesh");
}

// FUN_0018fd10: a new mode re-targets at once; 0 gives the eyes back to the head.
void Character::setEyeMode(int mode) {
    if (mode == eyeMode_) return;
    eyeMode_ = mode;
    eyeNext_ = -1;
    eyeRotSet_ = false;
}

// FUN_0018fa18 (re-target) and FUN_00190110 (orient): each eye's +Y turns a
// quarter of the way to the target per frame, inside a 22.3 degree cone
// (constraint 0.925) around the head's +Y; LocalRotate sets its world rotation.
void Character::pollEyes() {
    if (!eyes_ || eyeMode_ == 0) return;
    using V = std::array<float, 3>;
    auto norm = [](V v) {
        float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        return l > 0 ? V{v[0] / l, v[1] / l, v[2] / l} : v;
    };
    auto cross = [](const V& a, const V& b) { return V{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; };
    auto dot = [](const V& a, const V& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    auto row = [](const std::array<float, 9>& m, int i) { return V{m[size_t(i * 3)], m[size_t(i * 3 + 1)], m[size_t(i * 3 + 2)]}; };
    Transformable& head = *objects_["bone_head.mesh"]->transformable();
    float now = beat_ * 480;
    if (eyeNext_ < 0 || eyeNext_ <= now) {
        std::uniform_real_distribution<float> u(0, 1);
        auto pm = [&](float m) { return (2 * u(eyeRng_) - 1) * m; };
        if (eyeMode_ == 1) {  // camera: (eye_camera_max_offset 12) (eye_camera_interval 1920 3840)
            eyeTarget_ = {eyeCamera_[0] + pm(12), eyeCamera_[1] + pm(12), eyeCamera_[2]};
            eyeNext_ = now + 1920 + u(eyeRng_) * 1920;
        } else if (eyeMode_ == 2) {  // forward: (off.x, 80, off.z) in the character's space
            Xfm p = placement_;
            V off{pm(12), 80, pm(12)};
            eyeTarget_ = {p.pos[0] + off[0] * p.rot[0] + off[1] * p.rot[3] + off[2] * p.rot[6],
                          p.pos[1] + off[0] * p.rot[1] + off[1] * p.rot[4] + off[2] * p.rot[7],
                          p.pos[2] + off[0] * p.rot[2] + off[1] * p.rot[5] + off[2] * p.rot[8]};
            eyeNext_ = now + 960 + u(eyeRng_) * 1040;
        } else {  // crowd (FUN_00172cc8): a random 3D member's top + 48, lowered 70% toward the root
            std::optional<std::array<float, 3>> m = eyeCrowdTarget ? eyeCrowdTarget() : std::nullopt;
            V t = m ? *m : V{0, 0, 0};
            t[2] += (placement_.pos[2] - t[2]) * 0.7f;
            eyeTarget_ = t;
            eyeNext_ = now + 960 + u(eyeRng_) * 960;
        }
    }
    V hy = norm(row(head.world.rot, 1)), hx = norm(cross(hy, row(head.world.rot, 2))), hz = cross(hx, hy);
    const float cosC = 0.925f, sinC = std::sqrt(1 - cosC * cosC);
    const char* names[2] = {"L-eye.mesh", "R-eye.mesh"};
    for (int e = 0; e < 2; ++e) {
        Transformable& eye = *objects_[names[e]]->transformable();
        V scale{std::sqrt(dot(row(eye.world.rot, 0), row(eye.world.rot, 0))), std::sqrt(dot(row(eye.world.rot, 1), row(eye.world.rot, 1))),
                std::sqrt(dot(row(eye.world.rot, 2), row(eye.world.rot, 2)))};
        V d = norm({eyeTarget_[0] - eye.world.pos[0], eyeTarget_[1] - eye.world.pos[1], eyeTarget_[2] - eye.world.pos[2]});
        V lx = norm(cross(d, {0, 0, 1})), lz = cross(lx, d);
        std::array<float, 9> look{lx[0], lx[1], lx[2], d[0], d[1], d[2], lz[0], lz[1], lz[2]};
        std::array<float, 9> rest = eyeRotSet_ ? eyeRot_[size_t(e)] : look;
        if (!eyeRotSet_) {  // from the head's pose
            V ry = norm(row(eye.world.rot, 1)), rx = norm(cross(ry, row(eye.world.rot, 2))), rz = cross(rx, ry);
            rest = {rx[0], rx[1], rx[2], ry[0], ry[1], ry[2], rz[0], rz[1], rz[2]};
        }
        std::array<float, 9> m;
        quatToRot(nlerp(rotToQuat(rest), rotToQuat(look), 0.25f), m);
        V my = row(m, 1);
        if (dot(hy, my) < cosC) {
            float ax = dot(hx, my), az = dot(hz, my), l = std::sqrt(ax * ax + az * az);
            if (l > 0) ax = ax * sinC / l, az = az * sinC / l;
            else ax = sinC, az = 0;
            V y = norm({ax * hx[0] + cosC * hy[0] + az * hz[0], ax * hx[1] + cosC * hy[1] + az * hz[1], ax * hx[2] + cosC * hy[2] + az * hz[2]});
            V x = norm(cross(y, {0, 0, 1})), z = cross(x, y);
            m = {x[0], x[1], x[2], y[0], y[1], y[2], z[0], z[1], z[2]};
        }
        eyeRot_[size_t(e)] = m;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) eye.world.rot[size_t(r * 3 + c)] = m[size_t(r * 3 + c)] * scale[size_t(r)];
    }
    eyeRotSet_ = true;
}

void Character::loadShadow(const GameData& data, const std::string& shadowFile) {
    size_t slash = shadowFile.rfind('/');
    std::string path = shadowFile.substr(0, slash + 1) + "gen/" + shadowFile.substr(slash + 1) + "_ps2";
    if (!data.exists(path)) return;
    shadow_ = std::make_unique<Scene>(data.read(path));
    shadowRenderer_ = std::make_unique<SceneRenderer>(*renderer_, *shadow_);
    // FUN_00195e10: every bone_* of the shadow follows the same-named bone of the character.
    shadowRenderer_->setLookup([this](const std::string& n) -> const SceneObject* {
        if (n.rfind("bone_", 0) == 0)
            if (auto it = objects_.find(n); it != objects_.end()) return it->second;
        return shadow_->find(n);
    });
    // Rigid pieces (the Les Paul under bone_pos_guitar) hang off a character bone.
    for (SceneObject& o : shadow_->objects())
        if (const Transformable* t = o.transformable(); t && o.name.rfind("bone_", 0) == 0)
            for (const std::string& c : t->children)
                if (SceneObject* co = shadow_->find(c); co && co->mesh && c.rfind("bone_", 0) != 0) shadowRigid_.push_back({co, o.name});
}

namespace {
// ShadowTarget (FUN_001da1a8 case 3): p' = p - D (n.p + d) / (n.D), n = +Z, d = -(root.z + 3).
Mat4 shadowFlatten(const std::array<float, 3>& dir, float rootZ) {
    float d = -(rootZ + 3.0f);
    Mat4 f;
    f.at(0, 2) = -dir[0] / dir[2], f.at(0, 3) = -dir[0] * d / dir[2];
    f.at(1, 2) = -dir[1] / dir[2], f.at(1, 3) = -dir[1] * d / dir[2];
    f.at(2, 2) = 0, f.at(2, 3) = -d;
    return f;
}
}  // namespace

void Character::drawShadow(const Mat4& viewProj, const std::array<float, 3>& dir) {
    if (!shadow_ || std::fabs(dir[2]) < 1e-3f) return;
    for (auto& [o, bone] : shadowRigid_)
        if (auto it = objects_.find(bone); it != objects_.end())
            o->mesh->trans.world = compose(o->mesh->trans.local, it->second->transformable()->world);
    shadowRenderer_->draw("top.view", viewProj, shadowFlatten(dir, placement_.pos[2]), nullptr);
}

void Character::drawBodyShadow(const Mat4& viewProj, const std::array<float, 3>& dir, const MatObj* shadowMat) {
    if (!shadowMat || std::fabs(dir[2]) < 1e-3f) return;
    Mat4 f = shadowFlatten(dir, placement_.pos[2]);
    bodyRenderer_->setMaterialOverride(shadowMat);
    for (const std::string& n : bodyDraws_) bodyRenderer_->draw(n, viewProj, f, nullptr);
    bodyRenderer_->setMaterialOverride(nullptr);
}

const MatObj* Character::shadowMaterial() const {
    const SceneObject* m = shadow_ ? shadow_->find("charsys_shadow.mat") : nullptr;
    return m && m->mat ? m->mat.get() : nullptr;
}

Character::~Character() {
    if (gFaceOwner == this) gFaceOwner = nullptr;
}

void Character::loadFace(const GameData& data, const std::string& faceFile, bool singer) {
    // "charsys/metal/metal_face.rnd" -> "charsys/metal/gen/metal_face.rnd_ps2"
    size_t slash = faceFile.rfind('/');
    std::string path = faceFile.substr(0, slash + 1) + "gen/" + faceFile.substr(slash + 1) + "_ps2";
    if (!data.exists(path)) {
        SDL_Log("face %s: missing", path.c_str());
        return;
    }
    auto f = std::make_unique<Face>();
    f->scene = std::make_unique<Scene>(data.read(path));
    f->singer = singer;
    // HERO_FACES (blend_time 0.5) (pose_length 1.0); SINGER_FACES (blend_time 0.1).
    f->rate = singer ? 10.0f : 2.0f;
    for (const SceneObject& o : f->scene->objects()) {
        if (!o.morph) continue;
        // FUN_00287608: the target is "<morph name>.mesh" in the character.
        std::string target = o.name.substr(0, o.name.rfind('.')) + ".mesh";
        auto it = objects_.find(target);
        if (it == objects_.end() || !it->second->mesh) continue;
        MeshObj* t = it->second->mesh.get();
        if (!t->geomOwner.empty() && t->geomOwner != target)
            if (auto g = objects_.find(t->geomOwner); g != objects_.end() && g->second->mesh) t = g->second->mesh.get();
        Face::Bind b{t, {}};
        for (const MorphObj::Pose& p : o.morph->poses) {
            const SceneObject* pm = f->scene->find(p.mesh);
            b.poses.push_back(pm && pm->mesh ? pm->mesh.get() : nullptr);
        }
        f->binds.push_back(std::move(b));
    }
    if (!f->binds.empty()) face_ = std::move(f);
}

void Character::setFaceEvents(std::vector<FaceEvent> events) {
    if (face_) face_->events = std::move(events), face_->curEvent = -1, face_->open = false;
}

// FUN_002879d8: the excitement path (heroes) or the singer events, then the blender.
void Character::pollFace(float dSec, float ms) {
    Face& f = *face_;
    if (f.singer) {  // FUN_00287a80: 257 ms early (event_offset)
        float now = ms + 257.0f;
        int ev = -1;
        for (int i = 0; i < int(f.events.size()) && f.events[size_t(i)].startMs <= now; ++i) ev = i;
        if (ev >= 0 && ev != f.curEvent) {
            f.curEvent = ev, f.open = true;
            f.setTarget(1);  // "open"
        }
        if (f.curEvent >= 0 && f.events[size_t(f.curEvent)].endMs < now && f.open) {
            f.setTarget(0);
            f.open = false;
        }
    } else {  // FUN_00287b70
        // charsys.dta HERO_FACES excitement_poses: poses ref 0, bad01..03 1..3, good01..05 4..8.
        static const std::vector<int> kPoses[5] = {{1, 2, 3}, {1, 2, 3}, {0, 4}, {4, 5, 6, 7, 8}, {4, 5, 6, 7, 8}};
        f.timer += dSec;
        if (f.timer > f.poseLength && (!gFaceOwner || gFaceOwner == this)) {
            gFaceOwner = this;
            const auto& list = kPoses[std::clamp(faceExcitement_, 0, 4)];
            size_t i = std::uniform_int_distribution<size_t>(0, list.size() - 1)(f.rng);
            int p = list[i];
            if (p == f.cur) p = list[i + 1 < list.size() ? i + 1 : 0];
            f.timer = 0;
            f.setTarget(p);
        }
    }
    f.advance(dSec);
    if (f.t == 1 && gFaceOwner == this) gFaceOwner = nullptr;
    // RndMorph SetFrame(cur, 1) then SetFrame(tgt, S(t)): lerp of the two poses.
    float S = f.t == 1 ? 0.0f : 0.5f - 0.5f * std::cos(f.t * 3.14159265f);
    if (f.cur == f.shownCur && f.tgt == f.shownTgt && S == f.shownS) return;
    f.shownCur = f.cur, f.shownTgt = f.tgt, f.shownS = S;
    for (Face::Bind& b : f.binds) {
        const MeshObj* a = size_t(f.cur) < b.poses.size() ? b.poses[size_t(f.cur)] : nullptr;
        const MeshObj* c = size_t(f.tgt) < b.poses.size() ? b.poses[size_t(f.tgt)] : nullptr;
        if (!a) continue;
        if (!c) c = a;
        size_t n = std::min({b.target->verts.size(), a->verts.size(), c->verts.size()});
        for (size_t v = 0; v < n; ++v) {
            MeshVertex& o = b.target->verts[v];
            float len = 0;
            for (int k = 0; k < 3; ++k) {
                o.pos[k] = a->verts[v].pos[k] + (c->verts[v].pos[k] - a->verts[v].pos[k]) * S;
                o.normal[k] = a->verts[v].normal[k] + (c->verts[v].normal[k] - a->verts[v].normal[k]) * S;
                len += o.normal[k] * o.normal[k];
            }
            if (len > 0)
                for (int k = 0; k < 3; ++k) o.normal[k] /= std::sqrt(len);
        }
        ++b.target->revision;
    }
}

void Character::poll(float rawBeat, float ms, float frameScale) {
    float beat = rawBeat * frameScale;
    float dBeat = polled_ ? std::max(0.0f, rawBeat - rawBeat_) * frameScale : 0;
    float dSec = polled_ ? std::max(0.0f, (ms - ms_) * 0.001f) : 0;
    if (face_) pollFace(dSec, ms);
    polled_ = true;
    beat_ = beat, ms_ = ms, rawBeat_ = rawBeat;
    if (realign_) realign(beat);
    pollChain(clips_, chain_, beat, dBeat, dSec);
    // kAnimGraphLoop: re-queue the same clip through its graph self-edge.
    if (chain_.size() == 1 && (chain_.back().mode & kAnimGraphLoop)) start(chain_.back().clip, kAnimLongest | kAnimGraphLoop);
    for (auto& h : hands_) pollChain(handClips_, h, beat, dBeat, dSec);
    pose();
}

// AnimDriver_v0 @0x17ff18 (realign): when the (scaled) beat crosses more
// than one beat boundary, the old drivers stop phase-snapping and the
// current clip replays at once, phase-aligned (docs/character_polish_plan.md §2).
void Character::realign(float beat) {
    if (chain_.empty()) return;
    if (realignLast_ == 1e30f) realignLast_ = beat;
    if (std::floor(beat) != std::floor(realignLast_)) {
        int h = -1;
        for (size_t i = chain_.size(); i-- > 0;)
            if (chain_[i].blend != 0) {
                h = int(i);
                break;
            }
        if (h >= 0) {
            auto alignOf = [&](const Driver& d) { return d.align >= 0 ? d.align : int(clips_[size_t(d.clip)].clip->playFlags() & 0xf); };
            int ah = alignOf(chain_[size_t(h)]), a = 0;
            for (int i = 0; i <= h; ++i) a = std::max(a, alignOf(chain_[size_t(i)]));
            if (a - 1 >= 1) {
                int x = int(std::floor(beat)) ^ (int(std::floor(realignLast_)) + 1);
                if (x & (a - 1)) {
                    for (int i = 0; i <= h; ++i) chain_[size_t(i)].align = 0;
                    if (ah - 1 > 0 && (x & (ah - 1))) start(chain_[size_t(h)].clip, kAnimNow | kAnimGraphLoop);
                }
            }
        }
    }
    realignLast_ = beat;
}

Character::DriverInfo Character::newestStarted() const {
    for (size_t i = chain_.size(); i-- > 0;)
        if (chain_[i].blend > 0) return {chain_[i].clip, chain_[i].frame, chain_[i].delay, chain_[i].blend};
    return {};
}

bool Character::pendingAfterNewest(DriverInfo* pending, DriverInfo* before) const {
    for (size_t i = chain_.size(); i-- > 0;)
        if (chain_[i].blend > 0) {
            if (i + 1 >= chain_.size()) return false;
            const Driver& p = chain_[i + 1];
            const Driver& b = chain_[i];
            if (pending) *pending = {p.clip, p.frame, p.delay, p.blend};
            if (before) *before = {b.clip, b.frame, b.delay, b.blend};
            return true;
        }
    return false;
}

void Character::queueClip(int clip, float frame, float delay) {
    Driver d{clip, kAnimLongest, frame, delay, delay == 0 ? 1e-10f : 0};
    chain_.push_back(d);
}

void Character::popUnstarted() {
    if (chain_.size() > 1 && chain_.back().blend == 0) chain_.pop_back();
}

float Character::clipBlendWidth(int clip) const { return clips_[size_t(clip)].clip->blendWidth(); }

void Character::update(float beat, float ms, uint32_t category, bool oneShot) {
    if (!clips_.empty()) {
        if (chain_.empty() || category != category_) play(category, chain_.empty() ? kAnimReset : kAnimNow);
        else if (!blending() && !oneShot) play(category, kAnimLongest);
        category_ = category;
    }
    poll(beat, ms);
}

// FUN_00180518: how much of the main clips carry `flag`.
float Character::flagWeight(uint32_t flag) const {
    float acc = 0, rem = 1;
    for (size_t i = chain_.size(); i-- > 0;) {
        float sb = smooth(chain_[i].blend);
        if (clips_[size_t(chain_[i].clip)].flags & flag) acc += rem * sb;
        rem *= 1 - sb;
    }
    return acc;
}

// The servo buffer (FUN_001896f8): per driver, the bones its clips drive
// become (1 - W) * buffer + sum of S(blend)-weighted samples, W = the chain
// weight times the driver's weight scale; quats hemisphere-aligned, summed
// and normalised. Bones no clip drives keep their buffer value.
void Character::blendChain(const std::vector<Clip>& clips, const std::vector<Driver>& chain, float weight) {
    struct Acc {
        std::array<float, 3> pos{};
        Quat q{};
        float wPos = 0, wRot = 0;
    };
    std::map<std::string, Acc> acc;
    float remaining = weight;
    for (size_t i = chain.size(); i-- > 0 && remaining > 1e-6f;) {
        const Driver& d = chain[i];
        float w = remaining * smooth(d.blend);
        remaining -= w;
        if (w <= 0) continue;
        clips[size_t(d.clip)].clip->sample(d.frame, scratch_);
        for (const AnimClip::Value& v : scratch_) {
            auto b = buffer_.find(*v.bone);
            if (b == buffer_.end()) continue;
            if (rootMotion_ && v.bone->rfind("bone_facing", 0) == 0) continue;  // use_delta: the dummy bone
            Acc& a = acc[*v.bone];
            if (v.kind == AnimClip::Kind::Pos) {
                for (int k = 0; k < 3; ++k) a.pos[size_t(k)] += w * v.v[size_t(k)];
                a.wPos += w;
                continue;
            }
            std::array<float, 9> m;
            Quat q;
            if (v.kind == AnimClip::Kind::Quat) q = {v.v[0], v.v[1], v.v[2], v.v[3]};
            else {
                if (v.kind == AnimClip::Kind::RotX) rotXToRot(v.v[0], m);
                else if (v.kind == AnimClip::Kind::RotY) rotYToRot(v.v[0], m);
                else rotZToRot(v.v[0], m);
                q = rotToQuat(m);
            }
            Quat ref = a.wRot == 0 ? rotToQuat(b->second.rot) : a.q;
            float dot = q[0] * ref[0] + q[1] * ref[1] + q[2] * ref[2] + q[3] * ref[3];
            float sgn = dot < 0 ? -1.0f : 1.0f;
            for (int k = 0; k < 4; ++k) a.q[size_t(k)] += w * sgn * q[size_t(k)];
            a.wRot += w;
        }
    }
    for (auto& [name, a] : acc) {
        Xfm& x = buffer_[name];
        if (a.wPos > 0)
            for (int k = 0; k < 3; ++k) x.pos[size_t(k)] = a.pos[size_t(k)] + (1 - a.wPos) * x.pos[size_t(k)];
        if (a.wRot > 0) {
            Quat q = a.q;
            if (a.wRot < 1) {
                Quat bq = rotToQuat(x.rot);
                float dot = q[0] * bq[0] + q[1] * bq[1] + q[2] * bq[2] + q[3] * bq[3];
                float sgn = dot < 0 ? -1.0f : 1.0f;
                for (int k = 0; k < 4; ++k) q[size_t(k)] += (1 - a.wRot) * sgn * bq[size_t(k)];
            }
            float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            if (len > 0) {
                for (float& c : q) c /= len;
                quatToRot(q, x.rot);
            }
        }
    }
}

// FUN_001817c0: bone.servo (main, then the hand drivers scaled by last frame's
// IK flag weights), world update, right then left IK, the twist servos, and
// the weights for the next frame (FUN_00286f58).
void Character::pose() {
    if (buffer_.empty()) buffer_ = bind_;
    if (rootMotion_) applyRootMotion();
    blendChain(clips_, chain_, 1);
    if (hasHands()) {
        blendChain(handClips_, hands_[kLeftHand], wL_);
        blendChain(handClips_, hands_[kRightHand], wR_);
    }
    for (auto& [name, x] : buffer_) objects_[name]->transformable()->local = x;
    if (hasFretBone_)
        if (auto it = objects_.find("bone_fret.mesh"); it != objects_.end()) it->second->transformable()->local = fretBone_;
    updateWorld();
    if (hasHands()) {
        ik("R", "bone_strum_hand.mesh", wR_);
        ik("L", "bone_fret_hand.mesh", wL_);
    }
    servos();
    updateWorld();
    if (hasHands()) {
        wL_ = flagWeight(0x400000);  // kGuitarLeftIK
        wR_ = flagWeight(0x800000);  // kGuitarRightIK
    }
    pollEyes();
}

void Character::updateSubtree(const std::string& name) {
    auto it = objects_.find(name);
    if (it == objects_.end()) return;
    Transformable& t = *it->second->transformable();
    auto p = parent_.find(name);
    const Transformable* pt = p != parent_.end() && objects_.count(p->second) ? objects_[p->second]->transformable() : nullptr;
    t.world = fretDetached_ && name == "bone_fret.mesh" ? t.local : compose(t.local, pt ? pt->world : placement_);
    if (auto c = children_.find(name); c != children_.end())
        for (const std::string& ch : c->second) updateSubtree(ch);
}

namespace {

float len3(const std::array<float, 3>& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
std::array<float, 3> sub3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
// v in the frame of rows R: (v . R.row0, v . R.row1, v . R.row2).
std::array<float, 3> inFrame(const std::array<float, 3>& v, const std::array<float, 9>& r) {
    return {v[0] * r[0] + v[1] * r[1] + v[2] * r[2], v[0] * r[3] + v[1] * r[4] + v[2] * r[5],
            v[0] * r[6] + v[1] * r[7] + v[2] * r[8]};
}

}  // namespace

// AnimServoIK (FUN_00184198; docs/character_animation.md 2.3): two-bone IK of
// upperArm / foreArm so the hand reaches `dest`, with align_quat and stretch.
void Character::ik(const char* side, const std::string& dest, float frac) {
    std::string b = std::string("bone_") + side + "-";
    auto get = [&](const std::string& n) -> Transformable* {
        auto it = objects_.find(n);
        return it == objects_.end() ? nullptr : it->second->transformable();
    };
    Transformable *hand = get(b + "hand.mesh"), *fore = get(b + "foreArm.mesh"), *upper = get(b + "upperArm.mesh"),
                  *target = get(dest);
    if (!hand || !fore || !upper || !target) return;
    IkState& st = ik_[side[0] == 'L' ? 0 : 1];
    if (!st.init) {  // AnimServoIK_v6 setup
        st.init = true;
        st.handPos = hand->local.pos;
        float a = len3(sub3(fore->world.pos, hand->world.pos)), bl = len3(fore->local.pos);
        st.a2b2 = a * a + bl * bl;
        st.inv2ab = a > 0 && bl > 0 ? 1.0f / (2 * a * bl) : 0;
    }
    hand->local.pos = st.handPos;  // stretch: back to bind each frame
    if (frac <= 0) {
        updateSubtree(b + "hand.mesh");
        return;
    }
    updateSubtree(b + "hand.mesh");
    std::array<float, 3> t;
    for (int k = 0; k < 3; ++k)
        t[size_t(k)] = hand->world.pos[size_t(k)] + (target->world.pos[size_t(k)] - hand->world.pos[size_t(k)]) * frac;
    // Elbow: law of cosines, clamped.
    std::array<float, 3> d = sub3(upper->world.pos, t);
    float c = std::clamp((d[0] * d[0] + d[1] * d[1] + d[2] * d[2] - st.a2b2) * st.inv2ab, -0.985f, 0.985f);
    float s = std::sqrt(1 - c * c);
    fore->local.rot = {c, -s, 0, s, c, 0, 0, 0, 1};
    updateSubtree(b + "foreArm.mesh");
    // Aim the upper arm: the shortest arc from the hand to the target, in its frame.
    std::array<float, 3> v1 = inFrame(sub3(hand->world.pos, upper->world.pos), upper->world.rot);
    std::array<float, 3> v2 = inFrame(sub3(t, upper->world.pos), upper->world.rot);
    float l1 = len3(v1), l2 = len3(v2);
    if (l1 > 0 && l2 > 0) {
        float cosv = (v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2]) / (l1 * l2);
        float w = std::sqrt(std::max(0.0f, 0.5f + 0.5f * cosv));
        if (w > 1e-4f) {
            float k = 1.0f / (2 * l1 * l2 * w);
            Quat q{(v1[1] * v2[2] - v1[2] * v2[1]) * k, (v1[2] * v2[0] - v1[0] * v2[2]) * k, (v1[0] * v2[1] - v1[1] * v2[0]) * k, w};
            std::array<float, 9> qm;
            quatToRot(q, qm);
            upper->local.rot = compose(Xfm{qm, {}}, Xfm{upper->local.rot, {}}).rot;
            updateSubtree(b + "upperArm.mesh");
        }
    }
    // Finish: hand rotation from the target (align_quat), wrist on T (stretch).
    Xfm inv = inverse(fore->world);
    std::array<float, 9> r = target->world.rot;
    if (frac < 1) {
        Quat qa = rotToQuat(hand->world.rot), qb = rotToQuat(target->world.rot);
        quatToRot(nlerp(qa, qb, frac), r);
    }
    hand->local.rot = compose(Xfm{r, {}}, Xfm{inv.rot, {}}).rot;
    hand->local.pos = compose(Xfm{{1, 0, 0, 0, 1, 0, 0, 0, 1}, t}, inv).pos;
    updateSubtree(b + "hand.mesh");
}

namespace {

std::array<float, 9> mul3(const std::array<float, 9>& a, const std::array<float, 9>& b) {
    std::array<float, 9> r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) r[size_t(i * 3 + j)] += a[size_t(i * 3 + k)] * b[size_t(k * 3 + j)];
    return r;
}

// The twist of rotation R about its X axis: remove the swing taking R's X
// row onto +X (arcQuat, FUN_0024b1b0), then the angle of the Y row in YZ.
float twistAboutX(const std::array<float, 9>& r) {
    float vx = r[0], vy = r[1], vz = r[2];
    float len = std::sqrt(vx * vx + vy * vy + vz * vz);
    if (len <= 0) return 0;
    vx /= len, vy /= len, vz /= len;
    float w = std::sqrt(std::max(0.0f, 0.5f + 0.5f * vx));
    if (w < 1e-4f) return 0;
    Quat q{0, vz / (2 * w), -vy / (2 * w), w};
    std::array<float, 9> qm;
    quatToRot(q, qm);
    float y1 = r[3] * qm[1] + r[4] * qm[4] + r[5] * qm[7];
    float y2 = r[3] * qm[2] + r[4] * qm[5] + r[5] * qm[8];
    return std::atan2(y2, y1);
}

float wrapPi(float a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

}  // namespace

// charbase.dta / bandbase.dta servos, priority 1000 (after the clips):
// AnimServoForeTwist (FUN_00183a20) and AnimServoUpperTwist (FUN_001878a8).
void Character::servos() {
    auto local = [&](const std::string& n) -> Transformable* {
        auto it = objects_.find(n);
        return it == objects_.end() ? nullptr : it->second->transformable();
    };
    for (const char* side : {"L", "R"}) {
        std::string b = std::string("bone_") + side + "-";
        if (servoLevel_ >= 2) {
            Transformable *fore = local(b + "foreArm.mesh"), *t1 = local(b + "foreTwist1.mesh"),
                          *t2 = local(b + "foreTwist2.mesh"), *hand = local(b + "hand.mesh");
            if (fore && t1 && t2 && hand) {
                float offset = (side[0] == 'L' ? 90.0f : -90.0f) * 3.14159265f / 180.0f;
                float t = wrapPi(twistAboutX(hand->local.rot) + offset) / 3;
                std::array<float, 9> rx;
                rotXToRot(t, rx);
                t2->local.rot = rx;
                t1->local.rot = mul3(rx, fore->local.rot);
                t2->local.pos[0] = hand->local.pos[0] * 0.5f;
            }
        }
        if (servoLevel_ >= 1) {
            Transformable *upper = local(b + "upperArm.mesh"), *t1 = local(b + "upperTwist1.mesh"),
                          *t2 = local(b + "upperTwist2.mesh");
            if (upper && t1 && t2) {
                float t = twistAboutX(upper->local.rot);
                std::array<float, 9> half;
                rotXToRot(-t / 2, half);
                t2->local.rot = mul3(half, upper->local.rot);
                t1->local.rot = mul3(half, t2->local.rot);
            }
        }
    }
}

void Character::updateWorld() {
    std::map<std::string, char> done;
    std::function<const Xfm&(const std::string&, int)> world = [&](const std::string& name, int depth) -> const Xfm& {
        SceneObject* o = objects_[name];
        Transformable& t = *o->transformable();
        if (done[name]) return t.world;
        done[name] = 1;
        auto p = parent_.find(name);
        bool hasParent = p != parent_.end() && objects_.count(p->second) && depth < 64;
        if (fretDetached_ && name == "bone_fret.mesh") t.world = t.local;  // detached: world = local
        else t.world = compose(t.local, hasParent ? world(p->second, depth + 1) : placement_);
        return t.world;
    };
    for (auto& [name, o] : objects_) world(name, 0);
    // The guitar file's own copies of shared bones follow the character's.
    if (guitar_)
        for (SceneObject& o : guitar_->objects())
            if (Transformable* t = o.transformable(); t && objects_[o.name] != &o)
                t->world = objects_[o.name]->transformable()->world;
}

void Character::draw(const Mat4& viewProj, const EnvironObj* env) {
    for (const std::string& n : bodyDraws_) bodyRenderer_->draw(n, viewProj, Mat4{}, env);
    if (guitarRenderer_)
        for (const std::string& n : guitarDraws_) guitarRenderer_->draw(n, viewProj, Mat4{}, env);
}

std::vector<int> Character::clipsWith(uint32_t flags, bool matchTempo) const {
    std::vector<int> out;
    for (size_t i = 0; i < clips_.size(); ++i) {
        uint32_t f = clips_[i].flags;
        if ((f & flags) != flags) continue;
        if (matchTempo && (f & kTempoMask) && !(f & kTempoMask & uint32_t(tempo_))) continue;
        out.push_back(int(i));
    }
    return out;
}

void Character::playClip(int clip, int mode) {
    clips_[size_t(clip)].used = ++clock_;
    start(clip, mode);
}

int Character::currentClip() const {
    for (size_t i = chain_.size(); i-- > 0;)
        if (chain_[i].blend > 0) return chain_[i].clip;
    return chain_.empty() ? -1 : chain_.front().clip;
}

bool Character::facingAt(const AnimClip& c, float frame, std::array<float, 3>& pos, float& rotz) const {
    std::vector<AnimClip::Value> v;
    c.sample(frame, v);
    bool found = false;
    for (const AnimClip::Value& x : v) {
        if (*x.bone != "bone_facing.mesh") continue;
        if (x.kind == AnimClip::Kind::Pos) pos = {x.v[0], x.v[1], x.v[2]}, found = true;
        else if (x.kind == AnimClip::Kind::RotZ) rotz = x.v[0];
    }
    return found;
}

void Character::rootDelta(int clip, float f0, float f1, std::array<float, 3>& pos, float& yaw) const {
    const AnimClip& c = *clips_[size_t(clip)].clip;
    std::array<float, 3> p0{}, p1{};
    float r0 = 0, r1 = 0;
    pos = {0, 0, 0}, yaw = 0;
    if (!facingAt(c, f0, p0, r0) || !facingAt(c, f1, p1, r1)) return;
    // Rotated by -rotz(f0) into the clip's frame at f0.
    float dx = p1[0] - p0[0], dy = p1[1] - p0[1], cs = std::cos(r0), sn = std::sin(r0);
    pos = {dx * cs + dy * sn, -dx * sn + dy * cs, p1[2] - p0[2]};
    yaw = wrapPi(r1 - r0);
}

// FUN_0017bb90 + bone.servo use_delta: each driver adds w * (rotz(t) -
// rotz(tPrev)) and w * (P(t) - P(tPrev)) rotated by -rotz(t); the root's
// rows turn by the angle, then the root moves by the delta in its frame.
void Character::applyRootMotion() {
    float dTheta = 0;
    std::array<float, 3> dPos{};
    float remaining = 1;
    for (size_t i = chain_.size(); i-- > 0 && remaining > 1e-6f;) {
        const Driver& d = chain_[i];
        float w = remaining * smooth(d.blend);
        remaining -= w;
        if (w <= 0 || d.dEff == 0) continue;
        const AnimClip& c = *clips_[size_t(d.clip)].clip;
        std::array<float, 3> p0{}, p1{};
        float r0 = 0, r1 = 0;
        if (!facingAt(c, d.frame - d.dEff, p0, r0) || !facingAt(c, d.frame, p1, r1)) continue;
        dTheta += w * wrapPi(r1 - r0);
        float dx = p1[0] - p0[0], dy = p1[1] - p0[1], cs = std::cos(r1), sn = std::sin(r1);
        dPos[0] += w * (dx * cs + dy * sn);
        dPos[1] += w * (-dx * sn + dy * cs);
        dPos[2] += w * (p1[2] - p0[2]);
    }
    if (dTheta != 0) {
        std::array<float, 9> rz;
        rotZToRot(dTheta, rz);
        std::array<float, 9> r = placement_.rot;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                placement_.rot[size_t(row * 3 + col)] = r[size_t(row * 3)] * rz[size_t(col)] + r[size_t(row * 3 + 1)] * rz[size_t(3 + col)] +
                                                       r[size_t(row * 3 + 2)] * rz[size_t(6 + col)];
    }
    for (int k = 0; k < 3; ++k)
        placement_.pos[size_t(k)] += dPos[0] * placement_.rot[size_t(k)] + dPos[1] * placement_.rot[size_t(3 + k)] +
                                     dPos[2] * placement_.rot[size_t(6 + k)];
}

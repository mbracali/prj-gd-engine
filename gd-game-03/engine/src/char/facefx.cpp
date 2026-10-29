// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "char/facefx.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>

#include "data/dta.h"
#include "data/game_data.h"

namespace facefx {

namespace {

// ---- archive reader (little-endian; FaceFX SDK 1.5 serialisation [C]:
// u16 0x823858A8, u32 0x82385A30, f32 0x82385D40, FxString 0x82383C80,
// FxName 0x82389ED8, FxNamedObject 0x82387D40)
class Reader {
public:
    explicit Reader(const Bytes& b) : r_(b) {}
    size_t pos() const { return r_.pos(); }
    size_t size() const { return r_.size(); }
    uint16_t u16() { return r_.u16le(); }
    uint32_t u32() { return r_.u32le(); }
    int32_t i32() { return int32_t(r_.u32le()); }
    float f32() { return r_.f32le(); }
    std::string raw(size_t n) { return r_.str(n); }

    void expect16(uint16_t want, const char* what) {
        uint16_t v = u16();
        if (v != want) fail(std::string(what) + " version " + std::to_string(v));
    }
    // FxString: u16 version (1; 0 in SDK 1200 archives) + u32 length + bytes.
    std::string fxString() {
        uint16_t ver = u16();
        if (ver > 1) fail("FxString version " + std::to_string(ver));
        uint32_t n = u32();
        if (n > r_.remaining()) fail("FxString length");
        return raw(n);
    }
    std::string fxName() {
        expect16(0, "FxName");
        return fxString();
    }
    uint32_t arrayCount() {
        expect16(0, "FxArray");
        uint32_t n = u32();
        if (n > r_.remaining()) fail("FxArray count");
        return n;
    }
    void fxObject() { expect16(0, "FxObject"); }
    std::string named() {
        fxObject();
        expect16(0, "FxNamedObject");
        return fxName();
    }
    Header header() {
        Header h;
        if (raw(4) != "FACE") fail("not a FaceFX archive");
        h.sdk = u32();
        h.licensee = fxString();
        h.project = fxString();
        h.licenseeVersion = u32();
        if ((h.sdk != 1200 && h.sdk != 1500) || h.licenseeVersion != 1000) fail("unexpected SDK / licensee version");
        return h;
    }
    [[noreturn]] void fail(const std::string& what) const {
        throw std::runtime_error("FaceFX: " + what + " at 0x" + hex(r_.pos()));
    }

private:
    static std::string hex(size_t v) {
        char b[24];
        std::snprintf(b, sizeof b, "%zx", v);
        return b;
    }
    ByteReader r_;
};

// FxAnimCurve::Serialize (0x82390E40).
Curve readCurve(Reader& r) {
    Curve c;
    c.name = r.named();
    r.expect16(0, "FxAnimCurve");
    c.interp = r.u32();
    uint32_t n = r.arrayCount();
    c.keys.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        r.expect16(0, "FxAnimKey");  // 0x8239B500
        Key k;
        k.time = r.f32();
        k.value = r.f32();
        k.slopeIn = r.f32();
        k.slopeOut = r.f32();
        c.keys.push_back(k);
    }
    return c;
}

// FxFaceGraphNodeLink::Serialize (0x82391788).
Link readLink(Reader& r) {
    Link l;
    r.fxObject();
    l.version = r.u16();
    l.input = r.fxName();
    l.function = r.fxName();
    r.expect16(0, "FxLinkFnParameters");  // 0x82397E00
    uint32_t n = r.arrayCount();
    for (uint32_t i = 0; i < n; ++i) l.params.push_back(r.f32());
    // The link function object pointer (0x82397B58): -1 on the disc (the
    // function is found by name).
    if (r.i32() != -1) r.fail("link function object");
    return l;
}

// FxFaceGraphNode::Serialize (0x82393528).
void readNodeBase(Reader& r, Node& n) {
    n.name = r.named();
    n.version = r.u16();
    n.min = r.f32();
    n.max = r.f32();
    n.inputOp = r.u32();
    uint32_t links = r.arrayCount();
    for (uint32_t i = 0; i < links; ++i) n.links.push_back(readLink(r));
    if (n.version) {
        uint32_t props = r.arrayCount();
        for (uint32_t i = 0; i < props; ++i) {  // 0x82393498
            UserProperty p;
            p.name = r.named();
            p.version = r.u16();
            p.type = r.u32();
            p.intValue = r.u32();
            p.floatValue = r.f32();
            uint32_t s = r.arrayCount();
            for (uint32_t k = 0; k < s; ++k) p.strings.push_back(r.fxString());
            n.props.push_back(std::move(p));
        }
    }
}

// Object pointer (save path 0x8238C620): u32 index; a new object: FxString
// class name, u32 object size, the object.
Node readNode(Reader& r, std::map<int32_t, bool>& seen) {
    int32_t idx = r.i32();
    if (idx == -1 || seen.count(idx)) r.fail("face graph node pointer");
    seen[idx] = true;
    Node n;
    n.className = r.fxString();
    n.objectSize = r.u32();
    if (n.className == "FxBonePoseNode") {  // 0x8238FF70
        readNodeBase(r, n);
        n.classVersion = r.u16();
        uint32_t bones = r.arrayCount();  // 0x8238FAC0
        for (uint32_t i = 0; i < bones; ++i) {  // FxBone 0x8238ECA8
            Bone b;
            b.name = r.named();
            r.u16();
            for (float& v : b.pos) v = r.f32();
            for (float& v : b.rot) v = r.f32();
            for (float& v : b.scale) v = r.f32();
            n.bones.push_back(std::move(b));
        }
    } else if (n.className == "FxCombinerNode") {  // 0x8238D658
        readNodeBase(r, n);
        n.classVersion = r.u16();
    } else {
        r.fail("unknown face graph node class " + n.className);
    }
    return n;
}

}  // namespace

// ---- curves

float Curve::evaluate(float t, uint32_t& hint) const {
    // FxAnimCurve::Evaluate (0x82390318) [C].
    const uint32_t n = uint32_t(keys.size());
    if (n == 0) return 0.0f;
    if (!(keys[0].time < t)) return keys[0].value;
    if (keys[n - 1].time <= t) return keys[n - 1].value;
    if (n == 1) return keys[0].value;
    const uint32_t last = n - 1;
    uint32_t i = 0;
    if (hint != ~0u && hint < n && keys[hint].time <= t) i = hint;
    for (; i < last; ++i)
        if (keys[i].time <= t && t < keys[i + 1].time) break;
    const Key* a;
    const Key* b;
    if (i == last) {
        b = &keys[i];
        a = b - 1;
    } else {
        a = &keys[i];
        b = a + 1;
    }
    hint = i;
    if (interp == 0) {
        const float v0 = a->value, v1 = b->value;
        const float dt = b->time - a->time;
        const float m0 = a->slopeOut * dt;
        const float m1 = b->slopeIn * dt;
        const float s = (t - a->time) / dt;
        const float c3 = (v0 * 2.0f - v1 * 2.0f) + m1 + m0;
        const float c2 = -(m0 * 2.0f - (v1 * 3.0f - v0 * 3.0f)) - m1;
        return ((c3 * s + c2) * s + m0) * s + v0;
    }
    if (interp == 1) {
        const float s = (t - a->time) / (b->time - a->time);
        return s * (b->value - a->value) + a->value;
    }
    return 0.0f;
}

void Curve::bounds(float& start, float& end) const {
    // 0x823904E0 [C].
    if (keys.empty()) {
        start = end = 0.0f;
        return;
    }
    start = keys.front().time;
    end = keys.back().time;
}

int Anim::find(const std::string& curveName) const {
    for (size_t i = 0; i < curves.size(); ++i)
        if (curves[i].name == curveName) return int(i);
    return -1;
}

int FaceGraph::find(const std::string& nodeName) const {
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].name == nodeName) return int(i);
    return -1;
}

// ---- parsers

Anim parseAnim(const Bytes& file) {
    Reader r(file);
    Anim a;
    a.header = r.header();
    // FxAnim::Serialize (0x82385468).
    a.name = r.named();
    a.version = r.u16();
    if (a.version != 0 && a.version != 3) r.fail("FxAnim version " + std::to_string(a.version));
    const uint32_t end = r.u32();  // absolute offset of the object's end
    uint32_t n = r.arrayCount();
    a.curves.reserve(n);
    for (uint32_t i = 0; i < n; ++i) a.curves.push_back(readCurve(r));
    if (r.arrayCount() != 0) r.fail("FxBoneCurve (none on the disc)");
    a.blendInTime = r.f32();
    a.blendOutTime = r.f32();
    if (a.version > 1) {
        uint32_t w = r.arrayCount();
        for (uint32_t i = 0; i < w; ++i) {  // 0x82382980
            r.expect16(0, "bone weight");
            std::string name = r.fxName();
            a.boneWeights.emplace_back(name, r.f32());
        }
    }
    if (a.version > 0) {
        a.audioPath = r.fxString();
        a.analysisText = r.fxString();
    }
    if (a.version > 2) a.u68 = r.u32();
    if (r.pos() != end || r.pos() != r.size()) r.fail("FxAnim does not end at its end offset");
    // FxAnim::FindBounds (0x82382E50).
    float lo = 3.4028235e+38f, hi = 1.1754944e-38f;
    for (const Curve& c : a.curves) {
        float s, e;
        c.bounds(s, e);
        lo = s < lo ? s : lo;  // (lo <= s) ? lo : s
        hi = hi < e ? e : hi;
    }
    if (lo == 3.4028235e+38f && hi == 1.1754944e-38f) lo = 0.0f, hi = 1.0f;
    a.startTime = lo;
    a.endTime = hi;
    return a;
}

FaceGraph parseFaceGraph(const Bytes& file) {
    Reader r(file);
    FaceGraph g;
    g.header = r.header();
    // FxFaceGraph::Serialize (0x8238D250).
    r.fxObject();
    r.expect16(0, "FxFaceGraph");
    r.expect16(0, "FxFaceGraph nodes");
    uint32_t n = r.u32();
    std::map<int32_t, bool> seen;
    for (uint32_t i = 0; i < n; ++i) g.nodes.push_back(readNode(r, seen));
    if (r.pos() != r.size()) r.fail("trailing data after the face graph");
    return g;
}

Anim loadAnim(const GameData& data, const std::string& arkPath) { return parseAnim(data.read(arkPath)); }
FaceGraph loadFaceGraph(const GameData& data, const std::string& arkPath) { return parseFaceGraph(data.read(arkPath)); }

float evaluateLink(const Link& link, float x) {
    // FxLinearLinkFn::Evaluate (0x823964C8) [C].
    const auto& p = link.params;
    if (p.size() == 1) return p[0] * x;
    if (p.size() == 2) return std::fma(p[0], x, p[1]);
    return x;
}

// ---- paths

const char* const kBlinkTrackPath = "songs/_blinktrack/_blinktrack.voc";

std::string singerVocPath(const std::string& midiFile) {
    // 0x82275D68 (folder) + 0x82275EF0 (base name up to its first '.').
    size_t slash = midiFile.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? "" : midiFile.substr(0, slash);
    std::string base = slash == std::string::npos ? midiFile : midiFile.substr(slash + 1);
    size_t dot = base.find('.');
    if (dot != std::string::npos) base.resize(dot);
    return (dir.empty() ? "." : dir) + "/" + base + ".voc";
}

std::string unknownSongVocPath(const std::string& song) { return "songs/" + song + "/" + song + ".voc"; }

std::string resolveFacePath(const GameData& data, const std::string& miloArkPath, const std::string& rel) {
    std::vector<std::string> parts;
    auto push = [&](const std::string& path) {
        size_t i = 0;
        while (i <= path.size()) {
            size_t j = path.find_first_of("/\\", i);
            if (j == std::string::npos) j = path.size();
            std::string p = path.substr(i, j - i);
            if (p == "..") {
                if (!parts.empty()) parts.pop_back();
            } else if (!p.empty() && p != ".") {
                parts.push_back(p);
            }
            i = j + 1;
        }
    };
    size_t slash = miloArkPath.find_last_of('/');
    if (slash != std::string::npos) push(miloArkPath.substr(0, slash));
    push(rel);
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : "/") + p;
    if (data.exists(out)) return out;
    std::string q = out;
    for (size_t i = q.find("/og/"); i != std::string::npos; i = q.find("/og/", i + 4)) q.replace(i, 4, "/ng/");
    if (q.rfind("og/", 0) == 0) q.replace(0, 3, "ng/");
    return data.exists(q) ? q : out;
}

std::vector<std::string> loadServoVisemes(const GameData& data, const std::string& servoType) {
    DataNode root = parseDtb(data.read("char/gen/char_objects.dtb"));
    resolveConditionals(root, {"HX_XBOX", "_SHIP"});
    // Macros apply from their #define on (the file defines VISEMES_LIST twice).
    std::map<std::string, const DataNode*> defines;
    const auto& top = root.children;
    for (size_t i = 0; i < top.size(); ++i) {
        const DataNode& n = top[i];
        if (n.type == DataNode::Type::Define && i + 1 < top.size()) {
            defines[n.s] = &top[++i];
            continue;
        }
        if (!n.isList() || n.children.empty() || !n.children[0].isText() || n.children[0].s != "FaceFxLipSyncServo") continue;
        const DataNode* types = n.find("types");
        const DataNode* type = types ? types->find(servoType) : nullptr;
        const DataNode* vis = type ? type->find("visemes") : nullptr;
        if (!vis || vis->children.size() < 2 || !vis->children[1].isList()) return {};
        // (visemes (LIST)): the macro's contents replace its name.
        std::vector<std::string> out;
        for (const DataNode& c : vis->children[1].children) {
            auto d = c.isText() ? defines.find(c.s) : defines.end();
            if (d != defines.end() && d->second->isList()) {
                for (const DataNode& e : d->second->children)
                    if (e.isText()) out.push_back(e.s);
            } else if (c.isText()) {
                out.push_back(c.s);
            }
        }
        return out;
    }
    return {};
}

float eyeAngle(const float rot[9], uint32_t axis) {
    // rows: x = rot[0..2], y = rot[3..5], z = rot[6..8].
    switch (axis) {
        case 0: return float(std::atan2(rot[5], rot[4]));    // 0x82269988
        case 1: return float(std::atan2(-rot[2], rot[8]));   // 0x822699B8
        case 2: return -float(std::atan2(rot[3], rot[4]));   // 0x822699E8
        default: return 0.0f;
    }
}

// ---- LipSync

void LipSync::setup(const FaceGraph& graph, const std::vector<std::string>& visemes, const std::vector<std::string>& entryNodes) {
    nodes_.clear();
    registers_.clear();
    entryNodes_.clear();
    for (const Node& n : graph.nodes) {
        RNode r;
        r.name = n.name;
        r.bonePose = n.isBonePose();
        r.min = n.min;
        r.max = n.max;
        r.inputOp = n.inputOp;
        for (const Link& l : n.links) {
            if (l.function != "linear") throw std::runtime_error("FaceFX: link function " + l.function + " not supported");
            r.links.push_back({-1, l});
        }
        nodes_.push_back(std::move(r));
    }
    // 0x821A7DD8: the visemes list's names -> bone pose nodes, frame = index.
    for (size_t i = 0; i < visemes.size(); ++i) {
        int k = findNode(visemes[i]);
        if (k < 0) {  // a new FxBonePoseNode (min 0, max 1 [I: the node defaults])
            RNode r;
            r.name = visemes[i];
            r.bonePose = true;
            nodes_.push_back(std::move(r));
            k = int(nodes_.size()) - 1;
        }
        nodes_[size_t(k)].frame = int(i);
    }
    // 0x8218E6B8: the entries' nodes (combiners when missing), registered.
    for (const std::string& name : entryNodes) {
        int k = findNode(name);
        if (k < 0) {
            RNode r;
            r.name = name;
            nodes_.push_back(std::move(r));
            k = int(nodes_.size()) - 1;
        }
        entryNodes_.push_back(k);
    }
    for (RNode& n : nodes_)
        for (RLink& l : n.links) l.input = findNode(l.link.input);
    // FxActorInstance kept across songs; a new setup resets it.
    hasInstance_ = false;
    state_ = 4;
    anim_.reset();
    cur_.clear();
    prev_.clear();
    curStart_ = curEnd_ = prevWeight_ = 0;
    ready_ = true;
}

int LipSync::findNode(const std::string& name) const {
    for (size_t i = 0; i < nodes_.size(); ++i)
        if (nodes_[i].name == name) return int(i);
    return -1;
}

void LipSync::play(std::shared_ptr<const Anim> anim) {
    if (!ready_ || !anim) return;
    hasInstance_ = true;
    anim_ = std::move(anim);
    // Curves -> nodes by name (0x82382DF8 / 0x823901E8).
    curveNode_.assign(anim_->curves.size(), -1);
    curveHint_.assign(anim_->curves.size(), ~0u);
    for (size_t i = 0; i < anim_->curves.size(); ++i) curveNode_[i] = findNode(anim_->curves[i].name);
    // FxAnimPlayer::Play (0x82387748): stop (0x82387490), then state 2.
    started_ = false;
    playerStart_ = kInvalid;
    playerOffset_ = kInvalid;
    playerTime_ = kInvalid;
    state_ = 2;
}

bool LipSync::setRegister(const std::string& node, float value, RegisterOp op, float interp) {
    // 0x82388650 [C].
    const int k = findNode(node);
    if (k < 0) return false;
    Register* r = nullptr;
    for (Register& e : registers_)
        if (e.node == k) r = &e;
    if (!r) {  // 0x8238AAD0: a new register for that node
        registers_.push_back({});
        r = &registers_.back();
        r->node = k;
    } else {
        if (value == kInvalid) r->current = kInvalid;
        else if (r->current == kInvalid) r->current = 0.0f;
        r->start = r->current;
    }
    if (value == kInvalid) r->current = kInvalid;
    r->value = value;
    r->next = kInvalid;
    r->invDuration = interp == 0.0f ? 0.0f : 1.0f / interp;
    r->nextInvDuration = 0.0f;
    r->startTime = kInvalid;
    r->op = uint32_t(op);
    return true;
}

void LipSync::doExpression(const std::string& name, float blendSeconds, float now) {
    // 0x8218F380 [C].
    if (name == cur_) return;
    if (cur_.empty()) {
        prev_.clear();
        prevWeight_ = 0.0f;
    } else {
        prev_ = cur_;
        float f = (now - curStart_) / (curEnd_ - curStart_);
        prevWeight_ = 1.0f <= f ? 1.0f : f;
    }
    cur_ = name;
    curStart_ = now;
    curEnd_ = now + blendSeconds;
}

void LipSync::clearNodes() {
    // 0x8238BE18 -> 0x82391818.
    for (RNode& n : nodes_) n.value = n.track = n.reg = kInvalid;
}

void LipSync::evaluateCurves(float t) {
    // 0x82382CA8: every curve evaluates; a bound one sets its node's track value.
    for (size_t i = 0; i < anim_->curves.size(); ++i) {
        float v = anim_->curves[i].evaluate(t, curveHint_[i]);
        if (curveNode_[i] >= 0) nodes_[size_t(curveNode_[i])].track = v;
    }
}

void LipSync::tickRegisters(float now) {
    // 0x82388098, first loop [C].
    for (Register& r : registers_) {
        if (r.value == kInvalid) continue;
        if (r.startTime == kInvalid) r.startTime = now;
        if (r.invDuration == 0.0f) {
            r.current = r.value;
            continue;
        }
        float f = (now - r.startTime) * r.invDuration;
        if (1.0f <= f && r.next != kInvalid) {
            r.startTime = now;
            r.start = r.value;
            r.value = r.next;
            r.invDuration = r.nextInvDuration;
            r.next = kInvalid;
            f = 0.0f;
        }
        const float a = r.start, b = r.value;
        float v = a;
        if (0.0f < f) v = f < 1.0f ? ((a * 2.0f - b * 2.0f) * f + (b * 3.0f - a * 3.0f)) * f * f + a : b;
        r.current = v;
    }
}

void LipSync::tickPlayer(float uiSeconds, float songSeconds) {
    // FxAnimPlayer tick (0x82387540) [C], state 2 (the only one GH2 reaches).
    clearNodes();
    if (state_ != 2) return;
    if (playerStart_ == double(kInvalid)) {
        const float s = anim_->startTime;
        playerOffset_ = 0.0f <= s ? 0.0f : std::fabs(s);
        playerStart_ = uiSeconds;
    }
    if (0.0f <= songSeconds) {
        playerTime_ = songSeconds;
        playerStart_ = (double(uiSeconds) - songSeconds) - playerOffset_;
    } else {
        playerTime_ = float(uiSeconds - playerStart_) - playerOffset_;
    }
    if (playerTime_ <= anim_->endTime) {
        evaluateCurves(playerTime_);
    } else {  // past the end: stop (0x82387490)
        state_ = 4;
        playerStart_ = kInvalid;
        started_ = false;
        playerTime_ = kInvalid;
        playerOffset_ = kInvalid;
        return;
    }
    if (playerTime_ >= 0.0f && !started_) started_ = true;
}

void LipSync::applyRegisters() {
    // 0x82387F50: node register value and op.
    for (const Register& r : registers_) {
        RNode& n = nodes_[size_t(r.node)];
        n.reg = r.current;
        n.regOp = r.op;
    }
}

float LipSync::getValue(int i) {
    // FxFaceGraphNode::GetValue (0x82391AB8) [C].
    RNode& n = nodes_[size_t(i)];
    if (n.value != kInvalid) return n.value;
    float v;
    if (n.links.empty()) {
        v = n.track;
        if (n.reg != kInvalid) {
            if (v == kInvalid) v = n.regOp != 0 ? 1.0f : 0.0f;
            if (n.regOp == 0) v = n.reg + v;
            else if (n.regOp == 1) v = n.reg * v;
            else if (n.regOp == 2) v = n.reg;
        }
    } else {
        v = n.inputOp == 0 ? 0.0f : 1.0f;
        for (const RLink& l : n.links) {
            // (no corrective link functions on the disc)
            const float x = l.input >= 0 ? getValue(l.input) : 0.0f;
            const float y = l.input >= 0 ? evaluateLink(l.link, x) : 0.0f;
            if (n.inputOp == 0) v = y + v;
            else if (n.inputOp == 1) v = y * v;
        }
        if (n.track != kInvalid) v = n.track + v;
        if (n.reg != kInvalid) {
            if (n.regOp == 0) v = n.reg + v;
            else if (n.regOp == 1) v = n.reg * v;
            else if (n.regOp == 2) v = n.reg;
        }
    }
    if (v == kInvalid) {
        v = 0.0f;
    } else {
        float c = v <= n.max ? v : n.max;
        v = c < n.min ? n.min : c;
    }
    nodes_[size_t(i)].value = v;
    return v;
}

float LipSync::nodeValue(int i) { return getValue(i); }

std::vector<float> LipSync::evaluate(float animTime) {
    std::vector<float> out;
    if (!ready_) return out;
    clearNodes();
    if (anim_) evaluateCurves(animTime);
    applyRegisters();
    for (int i = 0; i < int(nodes_.size()); ++i) out.push_back(getValue(i));
    return out;
}

bool LipSync::poll(float songSeconds, float uiSeconds, const float* entryValues, size_t entryCount, FacePose& out) {
    // 0x8218E7E0 [C].
    out.frames.clear();
    out.residual = 1.0f;
    if (!ready_ || !hasInstance_ || state_ == 4) return false;
    // Entry registers (0x821A71D0): Replace, no interpolation.
    for (size_t i = 0; i < entryNodes_.size() && i < entryCount; ++i)
        setRegister(nodes_[size_t(entryNodes_[i])].name, entryValues[i], RegisterOp::Replace, 0.0f);
    // Expression cross-fade.
    if (!cur_.empty()) {
        float f = (songSeconds - curStart_) / (curEnd_ - curStart_);
        if (1.0f <= f) f = 1.0f;
        if (!prev_.empty()) setRegister(prev_, (1.0f - f) * prevWeight_);
        setRegister(cur_, f);
    }
    // FxActorInstance tick (0x82388098): registers on the UI clock, then the player.
    tickRegisters(uiSeconds);
    tickPlayer(uiSeconds, songSeconds);
    applyRegisters();  // vtable +0x14 (0x82387F50)
    // 0x821A7978 -> 0x8238E1A0: bone pose nodes with a value != 0.
    double residual = 1.0;
    for (int i = 0; i < int(nodes_.size()); ++i) {
        if (!nodes_[size_t(i)].bonePose) continue;
        const float w = getValue(i);
        if (w == 0.0f) continue;
        out.frames.push_back({nodes_[size_t(i)].frame, w, i});
        residual = double(float(residual - std::fabs(double(w))));
    }
    out.residual = float(residual);
    return true;
}

// ---- ExpressionScript (char_objects.dta FaceFxLipSyncServo gh2)

void ExpressionScript::pickExpression(float now) {
    // {if {exists expression_task} {delete expression_task}} {thread_task (units kTaskSeconds) ...}
    task_ = true;
    wake_ = now;
    resumeAtWake_ = false;
}

void ExpressionScript::update(float now, bool gameExists, int excitement, LipSync& servo) {
    if (!task_ || wake_ > now) return;
    // (script {$this do_pick_expression} {$task sleep {random_float 1.5 2.5}} {$task loop})
    const double cursor = resumeAtWake_ ? wake_ : double(now);
    doPickExpression(now, gameExists, excitement, servo);
    const float sleep = randomFloat ? randomFloat(1.5f, 2.5f) : 2.0f;
    wake_ = cursor + (sleep > 0.0f ? sleep : 0.0f);
    resumeAtWake_ = true;
}

void ExpressionScript::overrideExpression(const std::string& name, float now, LipSync& servo) {
    servo.doExpression(name, 1.0f, now);
    override_ = true;
}

void ExpressionScript::resumeRandomExpression(float now, bool gameExists, int excitement, LipSync& servo) {
    if (override_) {
        override_ = false;
        doPickExpression(now, gameExists, excitement, servo);
    }
}

void ExpressionScript::doPickExpression(float now, bool gameExists, int excitement, LipSync& servo) {
    if (!gameExists || override_) return;
    const bool bad = excitement <= 1;  // kExcitementBoot, kExcitementBad
    if (excitement < 0 || excitement > 4) return;  // no switch case
    int rand = randomInt ? randomInt(1, bad ? 3 : 5) : 1;
    if (rand >= last_) ++rand;
    last_ = rand;
    servo.doExpression(std::string(bad ? "expressionBad" : "expressionGood") + std::to_string(rand), 0.25f, now);
}

}  // namespace facefx

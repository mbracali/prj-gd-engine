// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "scene/synth_objects.h"

#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>

namespace scene {

namespace {

[[noreturn]] void bad(const std::string& what) { throw std::runtime_error(what); }

// Little-endian reads at a position of one record.
class Rd {
public:
    Rd(const Bytes& b, size_t& pos) : b_(b), p_(pos) {}
    uint32_t u32() {
        ByteReader r(b_);
        r.seek(p_);
        uint32_t v = r.u32le();
        p_ += 4;
        return v;
    }
    int32_t i32() { return int32_t(u32()); }
    float f() {
        uint32_t u = u32();
        float v;
        std::memcpy(&v, &u, 4);
        return v;
    }
    bool b() {
        if (p_ >= b_.size()) bad("unexpected end of data");
        return b_[p_++] != 0;
    }
    std::string str() {
        uint32_t n = u32();
        if (n > b_.size() - p_) bad("string past the end");
        std::string s(reinterpret_cast<const char*>(b_.data() + p_), n);
        p_ += n;
        return s;
    }
    uint32_t count(size_t minBytes) {
        uint32_t n = u32();
        if (n > (b_.size() - p_) / minBytes) bad("implausible list size");
        return n;
    }
    void version(uint32_t want, const char* what) {
        if (u32() != want) bad(std::string(what) + " version (decoder reads " + std::to_string(want) + ")");
    }
    ObjectHeader header() { return readObjectHeaderAt(b_, p_); }
    size_t pos() const { return p_; }
    void skip(size_t n) {
        if (n > b_.size() - p_) bad("data past the end");
        p_ += n;
    }

private:
    const Bytes& b_;
    size_t& p_;
};

// SampleData::Load @ 0x823005F8 (rev 11): u32 format, u32 sample count,
// u32 rate, u32 size, u8 has-data (rev > 10), then `size` bytes [C].
void readSampleData(Rd& r, SampleData& s) {
    r.version(11, "SampleData");
    s.format = r.u32();
    s.sampleCount = r.u32();
    s.rate = r.u32();
    s.size = r.u32();
    s.hasData = r.b();
    s.dataOffset = r.pos();
    if (s.hasData) r.skip(s.size);
}

// SynthSample::Load @ 0x822FEAB8 (rev 5): Hmx::Object (rev > 1), the file
// path, u8 looping, i32 loop start, i32 loop end (rev > 2), SampleData
// (rev >= 5, when the Synth loads samples from the bank) [C].
void readSynthSample(Rd& r, SynthSample& s) {
    s.header = r.header();
    s.file = r.str();
    s.looping = r.b();
    s.loopStart = r.i32();
    s.loopEnd = r.i32();
    readSampleData(r, s.sample);
}

// ADSR::Load @ 0x822F8E50 (rev 1): five f32 then three u32 [C].
void readAdsr(Rd& r, Adsr& a) {
    r.version(1, "ADSR");
    a.attackRate = r.f();
    a.decayRate = r.f();
    a.sustainRate = r.f();
    a.releaseRate = r.f();
    a.sustainLevel = r.f();
    a.attackMode = r.u32();
    a.sustainMode = r.u32();
    a.releaseMode = r.u32();
}

// Sfx::Load @ 0x822EE8B0 (rev 5): Hmx::Object, the maps (u32 count; each
// @ 0x822FF580: sample name, f32 volume, pan, transpose, i32 fx core,
// ADSR), the send (an FxSend name) and f32 send_volume [C].
void readSfx(Rd& r, Sfx& s) {
    s.header = r.header();
    s.maps.resize(r.count(20));
    for (auto& m : s.maps) {
        m.sample = r.str();
        m.volume = r.f();
        m.pan = r.f();
        m.transpose = r.f();
        m.fxCore = r.i32();
        readAdsr(r, m.adsr);
    }
    s.send = r.str();
    s.sendVolume = r.f();
}

// Sequence::Load @ 0x822EFB30 (rev 1): avg_volume, volume_spread,
// avg_transpose, transpose_spread, avg_pan, pan_spread [C]. The sequences
// of this revision carry no Hmx::Object block.
void readSequence(Rd& r, SequenceParams& p) {
    r.version(1, "Sequence");
    p.avgVolume = r.f();
    p.volumeSpread = r.f();
    p.avgTranspose = r.f();
    p.transposeSpread = r.f();
    p.avgPan = r.f();
    p.panSpread = r.f();
}

// GroupSeq::Load @ 0x822F4C20 (rev 3): Sequence, the children (u32 count,
// names; @ 0x822F1AD8) [C].
void readGroupSeq(Rd& r, GroupSeq& g) {
    r.version(3, "GroupSeq");
    readSequence(r, g.params);
    g.children.resize(r.count(4));
    for (auto& c : g.children) c = r.str();
}

struct SynthDecoder {
    std::function<std::unique_ptr<Object>()> make;
    std::function<void(Rd&, Object&)> read;
    uint32_t version;
};

template <class T> SynthDecoder dec(uint32_t version, void (*fn)(Rd&, T&)) {
    return {[] { return std::unique_ptr<Object>(new T); }, [fn](Rd& r, Object& o) { fn(r, static_cast<T&>(o)); }, version};
}

const std::map<std::string, SynthDecoder>& synthDecoders() {
    static const std::map<std::string, SynthDecoder> m = {
        {"SynthSample", dec<SynthSample>(5, readSynthSample)},
        {"Sfx", dec<Sfx>(5, readSfx)},
        // SfxSeq::Load @ 0x822F1280 (rev 3): Sequence, the child (ObjPtr).
        {"SfxSeq", dec<SfxSeq>(3, [](Rd& r, SfxSeq& s) {
             readSequence(r, s.params);
             s.child = r.str();
         })},
        // WaitSeq::Load @ 0x822EFFE0 (rev 2): Sequence, avg_wait_seconds,
        // wait_spread (rev > 1).
        {"WaitSeq", dec<WaitSeq>(2, [](Rd& r, WaitSeq& s) {
             readSequence(r, s.params);
             s.avgWaitSeconds = r.f();
             s.waitSpread = r.f();
         })},
        // RandomGroupSeq::Load @ 0x822F5100 (rev 1): GroupSeq, num_simul.
        {"RandomGroupSeq", dec<RandomGroupSeq>(1, [](Rd& r, RandomGroupSeq& s) {
             readGroupSeq(r, s);
             s.numSimul = r.u32();
         })},
        // FxSendReverb::Load @ 0x822FC6F0 (rev 1): FxSend::Load
        // @ 0x823000D8 (u32 rev < 2, Hmx::Object, next_send, stage), the
        // environment symbol.
        {"FxSendReverb", dec<FxSendReverb>(1, [](Rd& r, FxSendReverb& s) {
             r.version(1, "FxSend");
             s.header = r.header();
             s.nextSend = r.str();
             s.stage = r.i32();
             s.environment = r.str();
         })},
        // SerialGroupSeq::Load @ 0x822F5200 (rev 1): GroupSeq.
        {"SerialGroupSeq", dec<SerialGroupSeq>(1, [](Rd& r, SerialGroupSeq& s) { readGroupSeq(r, s); })},
    };
    return m;
}

}  // namespace

bool isSynthClass(const std::string& cls) { return synthDecoders().count(cls) != 0; }

std::unique_ptr<Object> makeSynthObject(const std::string& cls) {
    auto it = synthDecoders().find(cls);
    return it != synthDecoders().end() ? it->second.make() : std::make_unique<Object>();
}

void readSynthObject(Object& o, const Bytes& record, size_t& pos) {
    auto it = synthDecoders().find(o.cls);
    if (it == synthDecoders().end()) bad("no sound decoder for " + o.cls);
    if (o.version != it->second.version)
        bad(o.cls + " version " + std::to_string(o.version) + " (decoder reads " + std::to_string(it->second.version) + ")");
    Rd r(record, pos);
    it->second.read(r, o);
}

}  // namespace scene

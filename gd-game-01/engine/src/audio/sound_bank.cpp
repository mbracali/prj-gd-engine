// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/sound_bank.h"

#include <cmath>
#include <stdexcept>

#include "data/bytes.h"
#include "data/game_data.h"

namespace {

// "<dir>/gen/<name>.<ext>" (FUN_00215b50).
std::string genPath(const std::string& path, const char* ext) {
    size_t slash = path.rfind('/');
    std::string dir = slash == std::string::npos ? "" : path.substr(0, slash + 1);
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    return dir + "gen/" + name + "." + ext;
}

}  // namespace

// Bank::Load (FUN_00218358): version, platform, id, then the SampleDesc,
// KeyMap, SfxMap and (PS2) sample vectors.
SoundBank::SoundBank(const GameData& data, const std::string& path) : path_(path) {
    Bytes bnk = data.read(genPath(path, "bnk"));
    ByteReader r(bnk);
    r.u32le();  // version (19)
    if (r.u32le() != 0) throw std::runtime_error("bank " + path + " is not a PS2 bank");
    r.u32le();  // bank id
    descs_.resize(r.u32le());
    for (Desc& d : descs_) {
        d.sample = r.u32le();
        uint16_t fx = r.u16le();
        d.core = fx == 0xFFFF ? -1 : int(fx);
        int16_t transpose = static_cast<int16_t>(r.u16le());
        d.ratio = std::pow(2.0f, (transpose / 128.0f) / 12.0f);
        d.adsr1 = r.u16le();
        d.adsr2 = r.u16le();
        d.volume = r.u16le() / 32767.0f;
        d.pan = static_cast<int16_t>(r.u16le()) / 127.0f;
    }
    for (uint32_t n = r.u32le(); n > 0; --n) r.skip(12);  // keymaps: only for midi_seq, unused in GH1
    for (uint32_t n = r.u32le(); n > 0; --n) {
        uint16_t desc = r.u16le();
        r.u8();
        std::string name = r.str(r.u32le());
        r.u16le(), r.u16le();
        sfx_.emplace(name, desc);
    }
    Bytes nse = data.read(genPath(path, "nse"));
    samples_.resize(r.u32le());
    for (Sample& s : samples_) {
        uint32_t off = r.u32le(), size = r.u32le();
        s.rate = static_cast<int>(r.u32le());
        if (size_t(off) + size > nse.size()) throw std::runtime_error("bank " + path + ": sample outside the .nse");
        s.pcm = std::make_shared<AdpcmSample>(decodeAdpcm(nse.data() + off, size));
    }
    // .seq: u32 0, then a DataArray (sequences (<class> (name n) ...) ...).
    std::string seq = genPath(path, "seq");
    if (data.exists(seq)) {
        Bytes b = data.read(seq);
        if (b.size() > 4) {
            seqRoot_ = parseDataArray(Bytes(b.begin() + 4, b.end()));
            for (const DataNode& def : seqRoot_.children)
                if (def.isList())
                    if (const DataNode* n = def.find("name"); n && n->children.size() > 1)
                        sequences_[n->children[1].s] = &def;
        }
    }
}

std::vector<const SoundBank::Desc*> SoundBank::layers(const std::string& sfx) const {
    std::vector<const Desc*> out;
    auto [a, b] = sfx_.equal_range(sfx);
    for (auto it = a; it != b; ++it)
        if (size_t(it->second) < descs_.size()) out.push_back(&descs_[size_t(it->second)]);
    return out;
}

const DataNode* SoundBank::sequence(const std::string& name) const {
    auto it = sequences_.find(name);
    return it == sequences_.end() ? nullptr : it->second;
}

#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero 1 (PS2) sound bank decoder: .bnk (+ .nse samples, + .seq sequences).

Recovered from SLUS_212.24 (EE) and IOP/SYNTH_S.IRX:
  Bank::Load            FUN_00218358   header + 3 vectors + platform sample table
  SampleDesc reader     FUN_00218130   16 bytes
  ADSR reader           FUN_00218ef0   u16 ADSR1, u16 ADSR2
  vol/pan reader        FUN_00217e48   u16 volume, s16 pan
  KeyMap reader         FUN_002181a0   12 bytes
  SfxMap reader         FUN_00218218   u16 desc, u8, Symbol(u32 len + chars), u16, u16
  SamplePS2 reader      FUN_00217ea8   u32 offset, u32 size, u32 rate (vector via Bank_v2)
  Desc -> AudioSample   FUN_00215ff0   vol = v/32767, pan = p/127, ratio = 2^((t/128)/12)
  IOP play              FUN_00000c70   pitch = (rate<<12)/48000 * speed/1000, ADSR, VMIXE

Usage:
  bnk.py dump  <file.bnk> [...]          print header, descs, keymaps, sfx list
  bnk.py check <file.bnk> [...]          parse only, verify byte-exact consumption
  bnk.py wav   <file.bnk> <sfx_name> <out.wav>   decode the sample(s) of one sfx
  bnk.py wavidx <file.bnk> <sample_index> <out.wav>
  bnk.py seq   <file.seq>                print the sequence DataArray as .dta text
"""
import os
import struct
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = HERE
sys.path.insert(0, TOOLS)

FILTERS = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]
FX = {0xFFFF: "dry", 0: "core0(echo)", 1: "core1(large studio)"}


class R:
    def __init__(self, data):
        self.d, self.p = data, 0

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]
        self.p += 2
        return v

    def s16(self):
        v = struct.unpack_from("<h", self.d, self.p)[0]
        self.p += 2
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def sym(self):
        n = self.u32()
        s = self.d[self.p:self.p + n].decode("latin-1")
        self.p += n
        return s


def parse_bnk(data):
    r = R(data)
    b = {"version": r.u32(), "platform": r.u32(), "id": r.u32()}
    descs = []
    for _ in range(r.u32()):
        descs.append(dict(sample=r.u32(), fx=r.u16(), transpose=r.s16(),
                          adsr1=r.u16(), adsr2=r.u16(), volume=r.u16(), pan=r.s16()))
    keymaps = []
    for _ in range(r.u32()):
        keymaps.append(dict(desc=r.u32(), program=r.s16(), lo=r.u16(), hi=r.u16(), root=r.u16()))
    sfx = []
    for _ in range(r.u32()):
        sfx.append(dict(desc=r.u16(), flag=r.u8(), name=r.sym(), unk8=r.u16(), unk10=r.u16()))
    if b["platform"] != 0:
        raise ValueError("only the PS2 sample table (platform 0) is handled")
    samples = []
    for _ in range(r.u32()):
        samples.append(dict(offset=r.u32(), size=r.u32(), rate=r.u32()))
    b.update(descs=descs, keymaps=keymaps, sfx=sfx, samples=samples, consumed=r.p)
    return b


# ---------------------------------------------------------------- SPU2 ADSR
def adsr_fields(a1, a2):
    return dict(
        attack_exp=a1 >> 15, attack_shift=(a1 >> 10) & 0x1F, attack_step=(a1 >> 8) & 3,
        decay_shift=(a1 >> 4) & 0xF, sustain_level=a1 & 0xF,
        sustain_exp=a2 >> 15, sustain_dec=(a2 >> 14) & 1, sustain_shift=(a2 >> 8) & 0x1F,
        sustain_step=(a2 >> 6) & 3, release_exp=(a2 >> 5) & 1, release_shift=a2 & 0x1F)


def env_tick(level, shift, step, exp, dec, counter):
    """One 48 kHz SPU2 envelope tick (psx-spx rules). Returns (level, counter)."""
    cycles = 1 << max(0, shift - 11)
    st = step << max(0, 11 - shift)
    if exp and not dec and level > 0x6000:
        cycles *= 4
    if exp and dec:
        st = (st * level) >> 15
    counter += 1
    if counter >= cycles:
        counter = 0
        level = max(0, min(0x7FFF, level + st))
    return level, counter


def adsr_times(a1, a2, rate=48000):
    f = adsr_fields(a1, a2)
    # attack 0 -> 0x7fff
    lvl, c, n = 0, 0, 0
    while lvl < 0x7FFF and n < 48000 * 60:
        lvl, c = env_tick(lvl, f["attack_shift"], 7 - f["attack_step"], f["attack_exp"], 0, c)
        n += 1
    attack = n / rate
    # decay 0x7fff -> sustain level
    target = (f["sustain_level"] + 1) * 0x800
    lvl, c, n = 0x7FFF, 0, 0
    while lvl > target and n < 48000 * 60:
        lvl, c = env_tick(lvl, f["decay_shift"], -8, 1, 1, c)
        n += 1
    decay = n / rate
    # release max -> 0
    lvl, c, n = 0x7FFF, 0, 0
    while lvl > 0 and n < 48000 * 60:
        lvl, c = env_tick(lvl, f["release_shift"], -8, f["release_exp"], 1, c)
        n += 1
    release = n / rate
    sus = "%s %s shift %d step %d" % ("exp" if f["sustain_exp"] else "lin",
                                      "dec" if f["sustain_dec"] else "inc",
                                      f["sustain_shift"], f["sustain_step"])
    return dict(attack_s=attack, decay_s=decay, sustain_level=min(target, 0x7FFF) / 0x7FFF,
                sustain=sus, release_s=release, release_mode="exp" if f["release_exp"] else "lin")


# ---------------------------------------------------------------- PS-ADPCM
def adpcm_info(nse, off, size):
    """Scan the blocks of one sample: loop start / end flags, total samples."""
    loop_start = loop_end = None
    end_block = None
    nblk = size // 16
    for i in range(nblk):
        fl = nse[off + i * 16 + 1]
        if fl & 4 and loop_start is None:
            loop_start = i
        if fl & 1:
            end_block = i
            loop_end = i if fl & 2 else None
            break
    return dict(blocks=nblk, loop_start_block=loop_start, end_block=end_block,
                loops=loop_end is not None, flags_last=nse[off + (nblk - 1) * 16 + 1] if nblk else 0)


def decode_adpcm(nse, off, size):
    out, s1, s2 = [], 0, 0
    for i in range(size // 16):
        blk = nse[off + i * 16: off + i * 16 + 16]
        shift, filt = blk[0] & 0x0F, blk[0] >> 4
        f0, f1 = FILTERS[filt] if filt < 5 else (0, 0)
        for byte in blk[2:]:
            for nib in (byte & 0x0F, byte >> 4):
                v = ((nib ^ 8) - 8) << 12 >> shift
                v += (s1 * f0 + s2 * f1 + 32) >> 6
                v = max(-32768, min(32767, v))
                out.append(v)
                s2, s1 = s1, v
        if blk[1] & 1:  # loop-end / end flag: hardware stops (or jumps) here
            break
    return out


def write_wav(path, pcm, rate):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack("<%dh" % len(pcm), *pcm))


# ---------------------------------------------------------------- reports
def spu_pitch(rate, speed=1.0):
    return ((rate << 12) // 48000) * int(speed * 1000) // 1000


def dump(path):
    data = open(path, "rb").read()
    b = parse_bnk(data)
    nse_path = path[:-4] + ".nse"
    nse = open(nse_path, "rb").read() if os.path.exists(nse_path) else None
    print("== %s  (%d bytes, consumed %d)" % (path, len(data), b["consumed"]))
    print("version %d  platform %d  bank_id %d  descs %d  keymaps %d  sfx %d  samples %d"
          % (b["version"], b["platform"], b["id"], len(b["descs"]), len(b["keymaps"]),
             len(b["sfx"]), len(b["samples"])))
    if nse is not None:
        tot = max((s["offset"] + s["size"] for s in b["samples"]), default=0)
        print("nse %d bytes, sample table spans %d bytes" % (len(nse), tot))
    print("-- samples: idx offset size rate pitch dur_s loop")
    for i, s in enumerate(b["samples"]):
        info = adpcm_info(nse, s["offset"], s["size"]) if nse else {}
        nsamp = (s["size"] // 16) * 28
        print("  s%-3d 0x%06x %6d %5d 0x%04x %6.3f %s" % (
            i, s["offset"], s["size"], s["rate"], spu_pitch(s["rate"]), nsamp / s["rate"],
            ("loop@blk%d" % info["loop_start_block"]) if info.get("loops") else "one-shot"))
    print("-- descs: idx sample fx transpose(semi) vol pan adsr")
    for i, d in enumerate(b["descs"]):
        t = adsr_times(d["adsr1"], d["adsr2"])
        print("  d%-3d s%-3d %-20s %+7.3f %5.3f %+5.2f  %04x/%04x A=%.4fs D=%.4fs S=%.2f(%s) R=%.4fs %s" % (
            i, d["sample"], FX.get(d["fx"], hex(d["fx"])), d["transpose"] / 128.0,
            d["volume"] / 32767.0, d["pan"] / 127.0, d["adsr1"], d["adsr2"],
            t["attack_s"], t["decay_s"], t["sustain_level"], t["sustain"], t["release_s"],
            t["release_mode"]))
    if b["keymaps"]:
        print("-- keymaps: desc program lo hi root")
        for k in b["keymaps"]:
            print("  d%-3d prog %d keys %d..%d root %d" % (k["desc"], k["program"], k["lo"], k["hi"], k["root"]))
    print("-- sfx (file order; at load the map is sorted by Symbol pointer; same-name entries layer):")
    names = {}
    for e in b["sfx"]:
        names.setdefault(e["name"], []).append(e)
    for n, es in names.items():
        parts = []
        for e in es:
            d = b["descs"][e["desc"]]
            parts.append("d%d(s%d vol %.2f pan %+.2f tr %+.2f %s)%s" % (
                e["desc"], d["sample"], d["volume"] / 32767.0, d["pan"] / 127.0,
                d["transpose"] / 128.0, FX.get(d["fx"], hex(d["fx"])),
                "" if (e["flag"], e["unk8"], e["unk10"]) == (0, 1000, 0)
                else " [flag %d u8 %d u10 %d]" % (e["flag"], e["unk8"], e["unk10"])))
        print("  %-24s %s" % (n, "; ".join(parts)))
    return b


def check(path):
    data = open(path, "rb").read()
    b = parse_bnk(data)
    ok = b["consumed"] == len(data)
    nse_path = path[:-4] + ".nse"
    nse_ok = ""
    if os.path.exists(nse_path):
        n = os.path.getsize(nse_path)
        end = max((s["offset"] + s["size"] for s in b["samples"]), default=0)
        contiguous = all(b["samples"][i + 1]["offset"] == s["offset"] + s["size"]
                         for i, s in enumerate(b["samples"][:-1]))
        nse_ok = " nse=%d end=%d %s contiguous=%s" % (n, end, "OK" if n == end else "MISMATCH", contiguous)
    bad_desc = [i for i, d in enumerate(b["descs"]) if d["sample"] >= len(b["samples"])]
    bad_sfx = [e["name"] for e in b["sfx"] if e["desc"] >= len(b["descs"])]
    print("%-55s %s (%d/%d)%s%s%s" % (path, "OK" if ok else "LEFTOVER", b["consumed"], len(data), nse_ok,
                                       " BADDESC" + str(bad_desc) if bad_desc else "",
                                       " BADSFX" + str(bad_sfx) if bad_sfx else ""))
    return ok


def seq_text(path):
    import dtb
    data = open(path, "rb").read()
    r = dtb.Reader(data)
    assert r.take("<I") == 0  # version u32 (FUN_00217680 reads and ignores it)
    assert r.take("<B") == 1
    root = r.array(0)
    assert r.pos == len(data), (r.pos, len(data))
    return "\n".join(dtb.render(n) for n in root) + "\n"


def main(argv):
    if len(argv) >= 3 and argv[1] == "dump":
        for p in argv[2:]:
            dump(p)
            print()
    elif len(argv) >= 3 and argv[1] == "check":
        good = all([check(p) for p in argv[2:]])
        sys.exit(0 if good else 1)
    elif len(argv) == 5 and argv[1] == "wav":
        b = parse_bnk(open(argv[2], "rb").read())
        nse = open(argv[2][:-4] + ".nse", "rb").read()
        es = [e for e in b["sfx"] if e["name"] == argv[3]]
        if not es:
            raise SystemExit("no sfx " + argv[3])
        for k, e in enumerate(es):
            d = b["descs"][e["desc"]]
            s = b["samples"][d["sample"]]
            pcm = decode_adpcm(nse, s["offset"], s["size"])
            out = argv[4] if len(es) == 1 else argv[4].replace(".wav", "_%d.wav" % k)
            write_wav(out, pcm, s["rate"])
            print("%s: d%d s%d off 0x%x size %d rate %d -> %d samples %.3fs" % (
                out, e["desc"], d["sample"], s["offset"], s["size"], s["rate"], len(pcm), len(pcm) / s["rate"]))
    elif len(argv) == 5 and argv[1] == "wavidx":
        b = parse_bnk(open(argv[2], "rb").read())
        nse = open(argv[2][:-4] + ".nse", "rb").read()
        s = b["samples"][int(argv[3])]
        pcm = decode_adpcm(nse, s["offset"], s["size"])
        write_wav(argv[4], pcm, s["rate"])
        print("%s: %d samples" % (argv[4], len(pcm)))
    elif len(argv) == 3 and argv[1] == "seq":
        sys.stdout.write(seq_text(argv[2]))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv)

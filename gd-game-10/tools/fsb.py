#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero III (Xbox 360) audio banks: DATA/MUSIC/<song>.fsb.xen (+ .dat.xen)
and DATA/STREAMS/streamall.fsb.xen (+ .dat.xen).

    fsb.py list    <file.fsb.xen>
    fsb.py check   <rip dir>                 every bank parses, sizes add up
    fsb.py extract <file.fsb.xen> <out dir>  each sample's raw XMA data (+ .txt header)

Song banks are encrypted with FMOD's scheme: plain = bitreverse(enc XOR
key[i % len]) with the key "5atu6w4zaw" [C: the script SongLoadFSB in
scripts\\guitar\\guitar_song.qb passes it; every song bank then starts with
"FSB3"]. streamall is not encrypted.

FSB3 (little-endian): "FSB3", u32 samples, u32 sample-header bytes, u32 data
bytes, u32 version (0x00030001), u32 mode; then one header per sample: u16 size,
char name[30], u32 length (samples), u32 stored bytes, u32 loop start, u32 loop
end, u32 mode, s32 rate, u16 volume, s16 pan, u16 priority, u16 channels, f32
min distance, f32 max distance, then (size - 80) bytes of codec data (XMA: the
seek table); zeros pad the headers to a 16-byte boundary. The sample data follows the headers back to back
(24 + headers + data = the file size) [C: every bank].

.dat (big-endian): u32 count, u32 size of the .fsb, then `count` entries of 20
bytes: u32 QB key of the stem name ("<song>_guitar"), u32 sample index in the
bank, 12 zero bytes [C: every bank].
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qbkey import default_table  # noqa: E402

KEY = b"5atu6w4zaw"
REV = bytes(int("{:08b}".format(b)[::-1], 2) for b in range(256))


def decrypt(data, key=KEY):
    # bytes.translate after a repeating XOR, done in slices for speed.
    out = bytearray(len(data))
    n = len(key)
    for j in range(n):
        seg = data[j::n]
        x = bytes(b ^ key[j] for b in seg)
        out[j::n] = x.translate(REV)
    return bytes(out)


class Sample:
    pass


def parse(data):
    if data[:4] != b"FSB3":
        raise ValueError("not FSB3: %r" % data[:4])
    n, shdr, dsize, ver, mode = struct.unpack_from("<5I", data, 4)
    if 24 + shdr + dsize != len(data):
        raise ValueError("sizes do not add up: 24 + %d + %d != %d" % (shdr, dsize, len(data)))
    o = 24
    pos = 24 + shdr
    out = []
    for i in range(n):
        s = Sample()
        s.index = i
        (s.hsize,) = struct.unpack_from("<H", data, o)
        s.name = data[o + 2:o + 32].split(b"\0")[0].decode("latin-1")
        (s.length, s.stored, s.loop_start, s.loop_end, s.mode, s.rate, s.volume, s.pan, s.priority,
         s.channels, s.min_dist, s.max_dist) = struct.unpack_from("<IIIIIiHhHHff", data, o + 32)
        s.extra = data[o + 80:o + s.hsize]
        s.offset = pos
        pos += s.stored
        o += s.hsize
        out.append(s)
    # The headers are padded with zeros to a 16-byte boundary of the file.
    if (o + 15) // 16 * 16 != 24 + shdr or any(data[o:24 + shdr]):
        raise ValueError("sample headers end at %d, not %d" % (o, 24 + shdr))
    if pos != len(data):
        raise ValueError("sample data ends at %d, not %d" % (pos, len(data)))
    return dict(samples=out, version=ver, mode=mode)


def load(path, headers_only=False):
    """The bank, decrypted; with headers_only, only the header part is
    decrypted (the rest stays as stored, for the size checks)."""
    data = open(path, "rb").read()
    if data[:4] != b"FSB3":
        if headers_only:
            shdr = struct.unpack_from("<I", decrypt(data[:24]), 8)[0]
            n = 24 + shdr
            n += (-n) % len(KEY)
            data = decrypt(data[:n]) + data[n:]
        else:
            data = decrypt(data)
    return data


def parse_dat(data):
    count, fsbsize = struct.unpack_from(">II", data, 0)
    if 8 + 20 * count != len(data):
        raise ValueError(".dat size %d for %d entries" % (len(data), count))
    entries = []
    for i in range(count):
        key, index, a, b, c = struct.unpack_from(">5I", data, 8 + 20 * i)
        if a or b or c:
            raise ValueError(".dat entry %d has non-zero padding" % i)
        entries.append((key, index))
    return fsbsize, entries


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    names = default_table()
    if argv[1] == "list":
        b = parse(load(argv[2]))
        for s in b["samples"]:
            print("%2d %-32s %8d samples %9d bytes %5d Hz %d ch mode %08x extra %d" % (
                s.index, s.name, s.length, s.stored, s.rate, s.channels, s.mode, len(s.extra)))
        return 0
    if argv[1] == "extract":
        data = load(argv[2])
        b = parse(data)
        os.makedirs(argv[3], exist_ok=True)
        for s in b["samples"]:
            open(os.path.join(argv[3], s.name + ".raw"), "wb").write(data[s.offset:s.offset + s.stored])
        return 0
    if argv[1] == "check":
        banks = bad = samples = 0
        for sub in ("DATA/MUSIC", "DATA/STREAMS"):
            d = os.path.join(argv[2], sub)
            for f in sorted(os.listdir(d)):
                if not f.endswith(".fsb.xen"):
                    continue
                banks += 1
                try:
                    data = load(os.path.join(d, f), headers_only=True)
                    b = parse(data)
                    samples += len(b["samples"])
                    dat = os.path.join(d, f[:-len(".fsb.xen")].lower() + ".dat.xen")
                    if os.path.exists(dat):
                        size, entries = parse_dat(open(dat, "rb").read())
                        if size != len(data):
                            raise ValueError(".dat gives size %d, bank is %d" % (size, len(data)))
                        idx = sorted(i for _, i in entries)
                        if idx != list(range(len(b["samples"]))):
                            raise ValueError(".dat indices %s" % idx)
                        for key, i in entries:
                            nm = names.get(key)
                            if not nm.startswith("0x") and nm.lower() != b["samples"][i].name.lower().rsplit(".", 1)[0]:
                                raise ValueError(".dat entry %s -> sample %s" % (nm, b["samples"][i].name))
                    else:
                        raise ValueError("no .dat")
                except ValueError as ex:
                    bad += 1
                    print("%s: %s" % (f, ex))
        print("banks: %d, samples: %d, with problems: %d" % (banks, samples, bad))
        return 1 if bad else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero II (Xbox 360) .mogg: a small Harmonix header + a plain Ogg Vorbis stream.

    mogg.py info <file.mogg> [...]       header fields, channels, rate, duration
    mogg.py ogg  <file.mogg> <out.ogg>   strip the header (a normal .ogg results)
    mogg.py scan <dir>                   one line per .mogg under dir (tab separated)

Header (all little endian u32):
    0x00  version         10 = plain Ogg (every GH2 360 mogg). Later Harmonix
                          games use 11+ for AES-encrypted Ogg; refused here.
    0x04  ogg offset      where "OggS" starts (= 20 + 8 * map entry count)
    0x08  map version     14 in every GH2 file
    0x0C  map interval    20000: one map entry per 20000 PCM samples
    0x10  map entry count
    0x14  map entries:    u32 byte offset into the Ogg data, u32 sample.
                          A seek table for a streamer that reads 32 KiB blocks:
                          entry i is the last 32 KiB-aligned offset whose
                          sample is <= i * interval. Offsets are block starts,
                          not page starts (the reader resyncs on "OggS"); the
                          sample is slightly past the granule of the page that
                          straddles the block boundary. Both columns rise
                          monotonically; consecutive entries often repeat.
"""
import os
import struct
import sys

PLAIN_VERSION = 10


def read_header(data):
    if len(data) < 20:
        raise ValueError("too short for a mogg header")
    version, ogg_off, map_ver, interval, count = struct.unpack_from("<5I", data, 0)
    if version != PLAIN_VERSION:
        raise ValueError(f"mogg version {version}: not a plain (v10) mogg"
                         + (" -- likely encrypted" if version > PLAIN_VERSION else ""))
    if 20 + 8 * count != ogg_off:
        raise ValueError(f"map size mismatch: 20 + 8*{count} != {ogg_off}")
    entries = [struct.unpack_from("<2I", data, 20 + 8 * i) for i in range(count)]
    return {"version": version, "ogg_offset": ogg_off, "map_version": map_ver,
            "map_interval": interval, "map_count": count, "map": entries}


def ogg_pages(ogg):
    """Yield (offset, header_type, granule, serial, seqno, body) for each page."""
    o, n = 0, len(ogg)
    while o + 27 <= n:
        if ogg[o:o + 4] != b"OggS":
            raise ValueError(f"lost Ogg sync at 0x{o:x}")
        htype = ogg[o + 5]
        granule, serial, seqno = struct.unpack_from("<qII", ogg, o + 6)
        nseg = ogg[o + 26]
        lacing = ogg[o + 27:o + 27 + nseg]
        body_off = o + 27 + nseg
        size = sum(lacing)
        yield o, htype, granule, serial, seqno, ogg[body_off:body_off + size]
        o = body_off + size


def vorbis_ident(body):
    """Parse a Vorbis identification header packet (type 1)."""
    if len(body) < 30 or body[0] != 1 or body[1:7] != b"vorbis":
        raise ValueError("first packet is not a Vorbis identification header")
    ver, ch, rate, bmax, bnom, bmin = struct.unpack_from("<IBIiii", body, 7)
    bs = body[28]
    return {"vorbis_version": ver, "channels": ch, "rate": rate,
            "bitrate_max": bmax, "bitrate_nominal": bnom, "bitrate_min": bmin,
            "blocksize_0": 1 << (bs & 15), "blocksize_1": 1 << (bs >> 4)}


def analyse(path):
    data = open(path, "rb").read()
    h = read_header(data)
    ogg = memoryview(data)[h["ogg_offset"]:]
    if bytes(ogg[:4]) != b"OggS":
        raise ValueError("no OggS at the ogg offset")
    ident, last_granule, pages, serials = None, 0, 0, set()
    for _, _, granule, serial, _, body in ogg_pages(ogg):
        pages += 1
        serials.add(serial)
        if ident is None:
            ident = vorbis_ident(bytes(body))
        if granule >= 0:
            last_granule = max(last_granule, granule)
    h.update(ident)
    h.update({"ogg_size": len(ogg), "pages": pages, "streams": len(serials),
              "samples": last_granule,
              "duration": last_granule / ident["rate"] if ident["rate"] else 0.0})
    # Map sanity: 32 KiB-aligned, inside the data, monotonic, sample <= i*interval.
    bad, prev = 0, (0, 0)
    for i, (off, smp) in enumerate(h["map"]):
        if (off % 0x8000 or off >= len(ogg) or off < prev[0] or smp < prev[1]
                or smp > i * h["map_interval"]):
            bad += 1
        prev = (off, smp)
    h["map_bad_entries"] = bad
    return h


def cmd_info(paths):
    for p in paths:
        h = analyse(p)
        print(p)
        for k in ("version", "ogg_offset", "map_version", "map_interval",
                  "map_count", "map_bad_entries", "ogg_size", "pages", "streams",
                  "vorbis_version", "channels", "rate", "bitrate_nominal",
                  "blocksize_0", "blocksize_1", "samples"):
            print(f"  {k:16} {h[k]}")
        print(f"  {'duration':16} {h['duration']:.3f} s"
              f" (map covers {h['map_count'] * h['map_interval'] / h['rate']:.1f} s)")
        if h["map"]:
            print(f"  {'map[0..3]':16} {h['map'][:4]}")


def cmd_ogg(src, dst):
    data = open(src, "rb").read()
    h = read_header(data)
    if data[h["ogg_offset"]:h["ogg_offset"] + 4] != b"OggS":
        sys.exit("mogg.py: no OggS at the ogg offset")
    with open(dst, "wb") as f:
        f.write(data[h["ogg_offset"]:])
    print(f"{dst}: {len(data) - h['ogg_offset']} bytes")


def cmd_scan(root):
    print("path\tchannels\trate\tseconds\tnominal_kbps\tmap_count")
    for dp, _, fs in sorted(os.walk(root)):
        for f in sorted(fs):
            if f.endswith(".mogg"):
                p = os.path.join(dp, f)
                try:
                    h = analyse(p)
                except ValueError as e:
                    print(f"{os.path.relpath(p, root)}\tERROR {e}")
                    continue
                print(f"{os.path.relpath(p, root)}\t{h['channels']}\t{h['rate']}\t"
                      f"{h['duration']:.2f}\t{h['bitrate_nominal'] // 1000}\t{h['map_count']}")


def main(argv):
    if len(argv) >= 3 and argv[1] == "info":
        cmd_info(argv[2:])
    elif len(argv) == 4 and argv[1] == "ogg":
        cmd_ogg(argv[2], argv[3])
    elif len(argv) == 3 and argv[1] == "scan":
        cmd_scan(argv[2])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)

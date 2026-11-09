#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Guitar Hero II (Xbox 360) song charts (.mid). See docs/audio_and_charts.md.

    midi_chart.py <file.mid>            tracks, tempo map, per-part difficulty report
    midi_chart.py notes <file.mid>      also list every pitch used per track
    midi_chart.py survey <dir>          every .mid under dir: track names, pitch
                                        usage per track name, text-event vocabulary

Standard MIDI type 1, 480 ticks per beat. Playable parts are tracks named
"PART GUITAR", "PART GUITAR COOP", "PART BASS" and "PART RHYTHM" (GH1 used
"T1 GEMS"; still accepted). Inside a part, difficulty d (0 Easy .. 3 Expert)
uses base = 60 + 12*d, exactly as GH1:
    base + 0..4   green, red, yellow, blue, orange (Easy charts use 3 frets,
                  Medium 4)
    base + 7      Star Power phrase (67, 79, 91, 103)
    base + 9, 10  face-off player 1 / player 2 sections
    40..59        guitarist left-hand fret position (GH1 kept these in "ANIM")
    110           "big note" (config/midi_parsers.dta big_note_parser_*)
Text events in a part: [play] [idle] [wail_on/off] [solo_on/off]
[ow_face_on/off] [*_tempo] [map HandMap_*/StrumMap_*] (character animation).
There are no forced-HOPO notes: HOPO uses a tick threshold (170, or the
song's hopo_threshold in songs.dta). Gems: fret note-ons within 10 ticks
form one chord (GH1 rule, assumed unchanged).
"""
import collections
import os
import struct
import sys

PARTS = ("PART GUITAR", "PART GUITAR COOP", "PART BASS", "PART RHYTHM", "T1 GEMS")
DIFFS = ("easy", "medium", "hard", "expert")
CHORD_TICKS = 10        # GH1 value (gd-game-01 chart.cpp)
SUSTAIN_TICKS = 161     # GH1 value: shorter gems have no tail
BIG_NOTE = 110
KNOWN_OFFSETS = (0, 1, 2, 3, 4, 7, 9, 10)


# --------------------------------------------------------------------------
# Standard MIDI File reader
# --------------------------------------------------------------------------
class Event:
    __slots__ = ("tick", "kind", "a", "b", "data")

    def __init__(self, tick, kind, a=0, b=0, data=b""):
        # kind: "on" / "off" (a = pitch, b = velocity, data = channel), "cc",
        # "meta" (a = meta type, data = bytes), "sysex", "other"
        self.tick, self.kind, self.a, self.b, self.data = tick, kind, a, b, data


class Track:
    def __init__(self):
        self.name, self.events = "", []


def _varlen(d, o):
    v = 0
    while True:
        c = d[o]
        o += 1
        v = (v << 7) | (c & 0x7F)
        if not c & 0x80:
            return v, o


def read_midi(path):
    d = open(path, "rb").read()
    if d[:4] != b"MThd":
        raise ValueError("not a Standard MIDI File")
    hlen = struct.unpack_from(">I", d, 4)[0]
    fmt, ntrk, division = struct.unpack_from(">HHH", d, 8)
    o = 8 + hlen
    tracks = []
    while o + 8 <= len(d) and len(tracks) < ntrk:
        cid, clen = d[o:o + 4], struct.unpack_from(">I", d, o + 4)[0]
        o += 8
        if cid != b"MTrk":
            o += clen
            continue
        end, t, tick, status = o + clen, Track(), 0, 0
        while o < end:
            delta, o = _varlen(d, o)
            tick += delta
            if d[o] & 0x80:
                cur = d[o]
                o += 1
                if cur < 0xF0:
                    status = cur  # only channel messages set running status
            else:
                cur = status
            hi = cur & 0xF0
            if cur == 0xFF:
                mtype = d[o]
                ln, o = _varlen(d, o + 1)
                data = d[o:o + ln]
                o += ln
                t.events.append(Event(tick, "meta", mtype, 0, data))
                if mtype == 0x03 and not t.name:
                    t.name = data.decode("latin-1")
                if mtype == 0x2F:
                    break
            elif cur in (0xF0, 0xF7):
                ln, o = _varlen(d, o)
                t.events.append(Event(tick, "sysex", 0, 0, d[o:o + ln]))
                o += ln
            elif hi in (0x80, 0x90):
                p, v = d[o], d[o + 1]
                o += 2
                on = hi == 0x90 and v > 0
                t.events.append(Event(tick, "on" if on else "off", p, v, status & 15))
            elif hi in (0xA0, 0xB0, 0xE0):
                t.events.append(Event(tick, "cc" if hi == 0xB0 else "other", d[o], d[o + 1]))
                o += 2
            elif hi in (0xC0, 0xD0):
                t.events.append(Event(tick, "other", d[o]))
                o += 1
            else:
                raise ValueError(f"bad status byte 0x{cur:02x} at 0x{o:x}")
        o = end
        tracks.append(t)
    return {"format": fmt, "division": division, "tracks": tracks}


def texts(track):
    """(tick, text) for text-like meta events (1 text, 5 lyric, 6 marker, 7 cue)."""
    return [(e.tick, e.data.decode("latin-1")) for e in track.events
            if e.kind == "meta" and e.a in (1, 5, 6, 7)]


def spans(track, pitch):
    """[(start, end)] for every note-on/off pair of one pitch."""
    out, start = [], None
    for e in track.events:
        if e.a != pitch or e.kind not in ("on", "off"):
            continue
        if e.kind == "on":
            if start is None:
                start = e.tick
        elif start is not None:
            out.append((start, e.tick))
            start = None
    return out


# --------------------------------------------------------------------------
# Tempo map
# --------------------------------------------------------------------------
class TempoMap:
    def __init__(self, midi):
        self.div = midi["division"]
        self.tempos, self.sigs = [], []
        for e in midi["tracks"][0].events:
            if e.kind == "meta" and e.a == 0x51:
                self.tempos.append((e.tick, int.from_bytes(e.data[:3], "big")))
            elif e.kind == "meta" and e.a == 0x58:
                self.sigs.append((e.tick, e.data[0], 1 << e.data[1]))
        if not self.tempos or self.tempos[0][0] != 0:
            self.tempos.insert(0, (0, 500000))
        self.ms_at = [0.0]
        for (t0, us), (t1, _) in zip(self.tempos, self.tempos[1:]):
            self.ms_at.append(self.ms_at[-1] + (t1 - t0) * us / self.div / 1000.0)

    def ms(self, tick):
        i = 0
        while i + 1 < len(self.tempos) and self.tempos[i + 1][0] <= tick:
            i += 1
        t0, us = self.tempos[i]
        return self.ms_at[i] + (tick - t0) * us / self.div / 1000.0


# --------------------------------------------------------------------------
# Part / difficulty analysis
# --------------------------------------------------------------------------
def gems(track, diff):
    """Chord-grouped gems for one difficulty: [(tick, fret_mask, length)]."""
    base = 60 + 12 * diff
    ons = sorted((e.tick, e.a - base) for e in track.events
                 if e.kind == "on" and 0 <= e.a - base <= 4)
    out = []
    for tick, fret in ons:
        if out and tick - out[-1][0] <= CHORD_TICKS:
            out[-1][1] |= 1 << fret
        else:
            out.append([tick, 1 << fret])
    lengths = {}
    for f in range(5):
        for s, e in spans(track, base + f):
            lengths[s] = max(lengths.get(s, 0), e - s)
    return [(t, m, lengths.get(t, 0)) for t, m in out]


def part_report(track, tempo):
    lines = []
    used = collections.Counter(e.a for e in track.events if e.kind == "on")
    for d, dname in enumerate(DIFFS):
        base = 60 + 12 * d
        g = gems(track, d)
        if not g:
            lines.append(f"    {dname:7} (no gems)")
            continue
        chords = sum(1 for _, m, _ in g if m & (m - 1))
        sustains = sum(1 for _, _, ln in g if ln >= SUSTAIN_TICKS)
        sp = spans(track, base + 7)
        p1, p2 = spans(track, base + 9), spans(track, base + 10)
        extra = sorted(p - base for p in used if base <= p < base + 12
                       and p - base not in KNOWN_OFFSETS)
        lines.append(
            f"    {dname:7} notes {base}-{base + 4}: {len(g):4} gems, {chords:3} chords, "
            f"{sustains:3} sustains; SP phrases (note {base + 7}): {len(sp)}; "
            f"face-off P1/P2 ({base + 9}/{base + 10}): {len(p1)}/{len(p2)}"
            + (f"; other offsets used: {extra}" if extra else ""))
        if sp:
            lines.append("            SP at " + ", ".join(
                f"{tempo.ms(s) / 1000:.1f}s" for s, _ in sp))
    big = spans(track, BIG_NOTE)
    lines.append(f"    big notes (note {BIG_NOTE}): {len(big)}")
    fretpos = sum(used[p] for p in range(40, 60))
    lines.append(f"    fret-position notes (40-59): {fretpos}")
    known = {60 + 12 * d + k for d in range(4) for k in KNOWN_OFFSETS} | set(range(40, 60)) | {BIG_NOTE}
    others = sorted(p for p in used if p not in known)
    if others:
        lines.append("    other pitches: " + ", ".join(f"{p} x{used[p]}" for p in others))
    tx = texts(track)
    solo = [t for t, x in tx if x.strip() == "[solo_on]"]
    if solo:
        lines.append(f"    [solo_on] markers (animation): {len(solo)} at "
                     + ", ".join(f"{tempo.ms(t) / 1000:.1f}s" for t in solo))
    tx = texts(track)
    if tx:
        c = collections.Counter(x.strip() for _, x in tx)
        lines.append(f"    text events: {len(tx)}: " + " ".join(f"{k} x{c[k]}" for k in sorted(c)))
    return lines


def report(path, show_notes=False):
    midi = read_midi(path)
    tempo = TempoMap(midi)
    end_tick = max((t.events[-1].tick for t in midi["tracks"] if t.events), default=0)
    print(f"{path}: format {midi['format']}, {len(midi['tracks'])} tracks, "
          f"{midi['division']} ticks/beat, last event {tempo.ms(end_tick) / 1000:.1f}s")
    print("tracks:")
    for i, t in enumerate(midi["tracks"]):
        kinds = collections.Counter(e.kind for e in t.events)
        ons = [e.a for e in t.events if e.kind == "on"]
        rng = f", pitches {min(ons)}-{max(ons)}" if ons else ""
        print(f"  {i:2} {t.name or '(unnamed)':20} {kinds['on']:5} notes, "
              f"{len(texts(t)):4} texts{rng}")
        if show_notes and ons:
            c = collections.Counter(ons)
            print("       " + " ".join(f"{p}:{c[p]}" for p in sorted(c)))
    print(f"tempo map: {len(tempo.tempos)} tempo changes")
    for tick, us in tempo.tempos[:8]:
        print(f"  tick {tick:7} ({tempo.ms(tick) / 1000:7.2f}s) {60e6 / us:7.2f} BPM")
    if len(tempo.tempos) > 8:
        print(f"  ... ({len(tempo.tempos) - 8} more)")
    print("time signatures: " + ", ".join(f"{n}/{dd}@{t}" for t, n, dd in tempo.sigs))
    for t in midi["tracks"]:
        if t.name in PARTS:
            print(f"{t.name}:")
            for ln in part_report(t, tempo):
                print(ln)
    for t in midi["tracks"]:
        if t.name == "EVENTS":
            tx = texts(t)
            print(f"EVENTS: {len(tx)} texts: " + " ".join(x for _, x in tx[:40])
                  + (" ..." if len(tx) > 40 else ""))


def survey(root):
    names = collections.Counter()
    pitches = collections.defaultdict(collections.Counter)
    words = collections.defaultdict(collections.Counter)
    files = 0
    for dp, _, fs in sorted(os.walk(root)):
        for f in sorted(fs):
            if not f.endswith(".mid"):
                continue
            files += 1
            midi = read_midi(os.path.join(dp, f))
            for t in midi["tracks"]:
                names[t.name] += 1
                for e in t.events:
                    if e.kind == "on":
                        pitches[t.name][e.a] += 1
                for _, x in texts(t):
                    if x != t.name:
                        words[t.name][x.split(" ")[0] if not x.startswith("[") else x] += 1
    print(f"{files} files")
    for n, c in names.most_common():
        p = pitches[n]
        print(f"\n== {n or '(unnamed)'}: in {c} files; {sum(p.values())} notes; "
              f"{sum(words[n].values())} texts")
        if p:
            print("  pitches: " + " ".join(f"{k}:{p[k]}" for k in sorted(p)))
        if words[n]:
            w = words[n]
            print(f"  {len(w)} distinct texts: " + " | ".join(
                f"{k} x{w[k]}" for k in sorted(w, key=lambda k: -w[k])[:60]))


def main(argv):
    if len(argv) == 3 and argv[1] == "survey":
        survey(argv[2])
    elif len(argv) == 3 and argv[1] == "notes":
        report(argv[2], True)
    elif len(argv) == 2 and not argv[1].startswith("-"):
        report(argv[1])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)

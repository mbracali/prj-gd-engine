#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Clean a gd04 save.json: every career band but The Glitches back to a new
band, and the Vault's save-wide purchases back to a new save's (GD04-096).

    save_wipe_bands.py <save.json> <fresh save.json> <out.json> [--qb <qb.pak.xen>]

<fresh save.json> is a save the game has just created, the template for
"new": e.g. `XDG_DATA_HOME=<empty dir> gd04 --walktest boot <disc>` leaves
one in <dir>/gd-engine/gd04/walktest/save.json. The input is only read.

Reset to the fresh save's value (absent there: removed):
- every band's career but The Glitches' (the band<i>_info_p<m>_career whose
  name is "The Glitches" is kept): its info tag, its sections
  p<m>_career_band<i>_<difficulty> and its venue tags
  p<m>_career_band<i>_<difficulty><zone>;
- the live copies of the career tags (the tags the sections hold, e.g.
  careertier1, career_song1_tier1; the popped band's state);
- the Vault's tags, save-wide in GH3 [C scripts store_*_buy,
  GlobalTags_UnlockAll]: the ids of Bonus_Guitars, Bonus_Guitar_Finishes,
  Secret_Guitars, Bonus_Basses, Bonus_Bass_Finishes, Secret_Basses,
  Secret_Characters, Bonus_Outfits, Bonus_Styles and Bonus_videos, the
  GH3_Bonus_Songs songs and their bonus_song<n>_tier1, and every
  Guitarist_* / Instrument_* tag (store_buy_character also unlocks the
  character's instrument).
Everything else (options, controller and character settings, Top Rockers,
quick play songs, venue_<zone>, Progression, The Glitches) is kept.
--qb defaults to ../extracted/DATA/COMPRESSED/PAK/qb.pak.xen next to this script.
"""
import argparse
import copy
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pak  # noqa: E402
import qb  # noqa: E402
from qbkey import qbkey  # noqa: E402

GLITCHES = "The Glitches"
STORE_ARRAYS = ["Bonus_Guitars", "Bonus_Guitar_Finishes", "Secret_Guitars", "Bonus_Basses", "Bonus_Bass_Finishes", "Secret_Basses",
                "Secret_Characters", "Bonus_Outfits", "Bonus_Styles", "Bonus_videos"]
DIFFS = ["easy", "medium", "hard", "expert"]


def hexkey(name):
    return "%08x" % qbkey(name)


def field(st, name):
    """A save struct's field (the save writes {"k": <hex key>, "v": ...})."""
    k = hexkey(name)
    for e in st.get("v", []):
        if e.get("k") == k or e.get("name") == name:
            return e.get("v")
    return None


def store_keys(qb_path):
    """The Vault's tag keys from GH3's own data (qb.pak)."""
    want = {qbkey(n): n for n in STORE_ARRAYS + ["GH3_Bonus_Songs"]}
    found = {}
    p = pak.Pak.open(qb_path)
    for e in qb.qb_entries(p):
        try:
            items, _ = qb.decode(p.data(e), e.fullname)
        except (qb.QbError, ValueError, IndexError):
            continue
        for name, _t, v in items:
            if name in want:
                found[want[name]] = v
    keys = set()
    for arr in STORE_ARRAYS:
        v = found.get(arr)
        if not (isinstance(v, tuple) and len(v) == 2):
            raise SystemExit("%s: no %s" % (qb_path, arr))
        for st in v[1]:
            for n, _t, x in st if isinstance(st, list) else []:
                if n == qbkey("id"):
                    keys.add("%08x" % int(x))
    songs = None
    for n, _t, x in found.get("GH3_Bonus_Songs") or []:
        if n == qbkey("tier1"):
            for n2, _t2, x2 in x:
                if n2 == qbkey("songs"):
                    songs = x2[1]
    if songs is None:
        raise SystemExit("%s: no GH3_Bonus_Songs songs" % qb_path)
    for i, s in enumerate(songs):
        keys.add("%08x" % int(s))
        keys.add(hexkey("bonus_song%d_tier1" % (i + 1)))
    return keys


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("save")
    ap.add_argument("fresh")
    ap.add_argument("out")
    ap.add_argument("--qb", default=os.path.join(HERE, "..", "extracted", "DATA", "COMPRESSED", "PAK", "qb.pak.xen"))
    a = ap.parse_args(argv[1:])
    if os.path.abspath(a.out) == os.path.abspath(a.save):
        raise SystemExit("the output must be another file (the input is only read)")
    save = json.load(open(a.save))
    fresh = json.load(open(a.fresh))
    for d, what in ((save, a.save), (fresh, a.fresh)):
        if d.get("format") != "gd04-save":
            raise SystemExit("%s: not a gd04 save" % what)
    out = copy.deepcopy(save)
    tags, ftags = out["tags"], fresh["tags"]
    secs, fsecs = out["sections"], fresh["sections"]

    def reset_tag(k):
        if k in ftags:
            tags[k] = copy.deepcopy(ftags[k])
        else:
            tags.pop(k, None)

    # The bands: every band<i>_info_p<m>_career there is (GH3 makes
    # num_career_bands + 1 slots), The Glitches' kept.
    bands = 0
    for t in list(tags.values()) + list(ftags.values()):
        m = re.fullmatch(r"band(\d+)_info_p[12]_career", t.get("name", ""))
        if m:
            bands = max(bands, int(m.group(1)))
    for t in list(secs.values()) + list(fsecs.values()):
        m = re.fullmatch(r"p[12]_career_band(\d+)_\w+", t.get("name", ""))
        if m:
            bands = max(bands, int(m.group(1)))
    kept, wiped = [], []
    zones = set()
    for t in list(tags.values()) + list(ftags.values()):
        m = re.fullmatch(r"p[12]_career_band\d+_(?:easy|medium|hard|expert)(z_\w+)", t.get("name", ""))
        if m:
            zones.add(m.group(1))
    for p in (1, 2):
        mode = "p%d_career" % p
        for b in range(1, bands + 1):
            info = hexkey("band%d_info_%s" % (b, mode))
            if info in tags and field(tags[info], "name") == GLITCHES:
                kept.append("%s band %d" % (mode, b))
                continue
            wiped.append("%s band %d" % (mode, b))
            reset_tag(info)
            for d in DIFFS:
                s = hexkey("%s_band%d_%s" % (mode, b, d))
                if s in fsecs:
                    secs[s] = copy.deepcopy(fsecs[s])
                else:
                    secs.pop(s, None)
                for z in zones:
                    reset_tag(hexkey("%s_band%d_%s%s" % (mode, b, d, z)))
    # The live copies of the sections' tags (the popped band's).
    live = set()
    for s in list(secs.values()) + list(fsecs.values()):
        live.update(s.get("tags", {}).keys())
    for k in live:
        reset_tag(k)
    # The Vault.
    store = store_keys(a.qb)
    for k, t in list(tags.items()) + list(ftags.items()):
        if t.get("name", "").startswith(("Guitarist_", "Instrument_")):
            store.add(k)
    changed = sum(1 for k in store if tags.get(k) != ftags.get(k))
    for k in store:
        reset_tag(k)
    with open(a.out, "w") as f:
        json.dump(out, f, indent=1)
        f.write("\n")
    print("kept: %s" % (", ".join(kept) or "none"))
    print("reset: %s" % ", ".join(wiped))
    print("Vault tags reset: %d of %d" % (changed, len(store)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Rock Band's front end as its scripts declare it: every screen, its panels,
focus, (back ...), help bar and the screens it goes to.

    frontend.py <dta dir> [screen ...]   one line per screen (or the named ones)
    frontend.py <dta dir> --md           the inventory table of docs/frontend.md

<dta dir> is `extracted/dta` (tools/dtb.py dump): the script files as text.
Reads ui/, tour/, tutorial/ and drum_trainer/. The scripts' #define macros
are expanded by name only (a macro's own screen changes count for every
object that uses it).
"""
import os
import re
import sys

GOTO = re.compile(r"\{\s*(?:ui\s+)?(goto_screen|push_screen|sync_screen|reset_screen|confirm_goto)\s+([A-Za-z_$][\w$]*)")


def objects(text):
    """Yields (class, name, body) for each {new <Class> <name> ...}."""
    for m in re.finditer(r"\{\s*new\s+(\w+)\s+(\w+)", text):
        depth, i = 0, m.start()
        while i < len(text):
            c = text[i]
            if c == '"':
                i = text.index('"', i + 1)
            elif c in "{([":
                depth += 1
            elif c in "})]":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        yield m.group(1), m.group(2), text[m.end():i]


def prop(body, key):
    m = re.search(r"\(\s*" + key + r"\s+([^()]*?(?:\([^()]*\))?[^()]*?)\)", body)
    return " ".join(m.group(1).split()) if m else ""


def load(root):
    defines, objs = {}, {}
    for sub in ("ui", "tour", "tutorial", "drum_trainer"):
        for d, _, files in os.walk(os.path.join(root, sub)):
            for f in sorted(files):
                if not f.endswith(".dta"):
                    continue
                text = open(os.path.join(d, f), encoding="utf-8", errors="replace").read()
                for m in re.finditer(r"#define\s+(\w+)\s*\(", text):
                    defines[m.group(1)] = text[m.end():m.end() + 4000]
                for cls, name, body in objects(text):
                    objs[name] = (cls, body, os.path.relpath(os.path.join(d, f), root))
    return defines, objs


def targets(body, defines, seen=None):
    out = set(g[1] for g in GOTO.findall(body))
    seen = seen or set()
    for name in re.findall(r"\b([A-Z][A-Z0-9_]{3,})\b", body):
        if name in defines and name not in seen:
            seen.add(name)
            out |= targets(defines[name], defines, seen)
    return out


def screens(root):
    defines, objs = load(root)
    rows = []
    for name, (cls, body, path) in objs.items():
        if not (cls.endswith("Screen") or name.endswith("_screen")) or cls in ("UIPanel",):
            continue
        panels = prop(body, "panels").split()
        go = targets(body, defines)
        for p in panels:
            if p in objs:
                go |= targets(objs[p][1], defines)
        rows.append({
            "name": name, "class": cls, "file": path, "panels": panels,
            "focus": prop(body, "focus"), "back": prop(body, "back"),
            "helpbar": prop(body, "helpbar"), "to": sorted(go - {name}),
        })
    return sorted(rows, key=lambda r: (r["file"], r["name"]))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    rows = screens(argv[1])
    if "--md" in argv:
        print("| Screen | File | Panels | Back | Goes to |")
        print("|---|---|---|---|---|")
        for r in rows:
            print("| `%s` | %s | %s | %s | %s |" % (r["name"], r["file"], " ".join(r["panels"]), r["back"] or "-",
                                                  " ".join(r["to"]) or "-"))
        print("\n%d screens" % len(rows))
        return 0
    want = set(argv[2:])
    for r in rows:
        if want and r["name"] not in want:
            continue
        print("%s (%s, %s)\n  panels: %s\n  focus: %s  back: %s  helpbar: %s\n  to: %s" % (
            r["name"], r["class"], r["file"], " ".join(r["panels"]), r["focus"] or "-", r["back"] or "-",
            r["helpbar"] or "-", " ".join(r["to"]) or "-"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

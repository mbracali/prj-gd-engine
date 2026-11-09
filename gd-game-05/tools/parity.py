#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Compute gd05's completeness from docs/parity.md.

Reads the ledger table, checks every row's score against its status
(works 1.0, approximate 0.5, stand-in 0.25, missing 0, times the weight)
and prints the total and the share per area.

    python3 -I tools/parity.py [docs/parity.md]
"""
import sys
from collections import OrderedDict

FACTOR = {"works": 1.0, "approximate": 0.5, "stand-in": 0.25, "missing": 0.0}


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "docs/parity.md"
    rows = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cells) < 8 or not cells[0].isdigit():
                continue
            rows.append(cells)
    total_w = total_s = 0.0
    areas = OrderedDict()
    bad = 0
    for c in rows:
        num, area, feat, w, status, score = c[0], c[1], c[2], float(c[3]), c[4], float(c[5])
        want = w * FACTOR.get(status, -1)
        if abs(want - score) > 1e-6:
            print(f"row {num} ({feat}): status {status} x weight {w:g} = {want:g}, the table says {score:g}")
            bad += 1
        total_w += w
        total_s += want
        a = areas.setdefault(area, [0.0, 0.0])
        a[0] += w
        a[1] += want
    for area, (w, s) in areas.items():
        print(f"{area:10s} {s:6.2f} / {w:5.1f}")
    print(f"rows {len(rows)}, weight {total_w:g}, completeness {100 * total_s / total_w:.2f}%")
    return 1 if bad or abs(total_w - 100) > 1e-6 else 0


if __name__ == "__main__":
    sys.exit(main())

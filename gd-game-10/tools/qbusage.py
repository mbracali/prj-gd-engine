#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Shows how the scripts use a name (a native's call sites).

    qbusage.py <rip dir> <name> [max lines]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pak  # noqa: E402
import qb  # noqa: E402
import qbscript  # noqa: E402
from qbkey import default_table  # noqa: E402


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    names = default_table()
    want = argv[2].lower()
    limit = int(argv[3]) if len(argv) > 3 else 12
    shown = 0
    p = pak.Pak.open(os.path.join(argv[1], "DATA", "COMPRESSED", "PAK", "qb.pak.xen"))
    for e in qb.qb_entries(p):
        items, _ = qb.decode(p.data(e), e.fullname)
        for n, t, v in items:
            if t != qb.T_SCRIPT:
                continue
            for line in qbscript.disassemble(v.code, names).split("\n"):
                words = line.strip().lower().replace("(", " ").split()
                if want in words:
                    print("%-40s %s" % (names.get(n)[:40], line.strip()[:220]))
                    shown += 1
                    if shown >= limit:
                        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

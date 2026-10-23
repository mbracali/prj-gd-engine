#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Lists the GitHub Actions our workflows use whose newer major version exists.
Read-only: it opens no branches or pull requests (we update by hand).

    python3 .github/scripts/check_action_updates.py      (needs the gh CLI, logged in)
"""
import glob
import json
import re
import subprocess

USES = re.compile(r"uses:\s*([\w.-]+/[\w.-]+)(?:/[\w./-]+)?@v(\d+)")


def latest(repo):
    r = subprocess.run(["gh", "api", f"repos/{repo}/releases/latest"], capture_output=True, text=True)
    if r.returncode != 0:
        return None
    return json.loads(r.stdout)["tag_name"]


def main():
    used = {}
    for f in sorted(glob.glob(".github/workflows/*.yml")):
        for repo, major in USES.findall(open(f).read()):
            used.setdefault(repo, set()).add(int(major))
    rows = []
    for repo in sorted(used):
        tag = latest(repo) or "?"
        m = re.match(r"v?(\d+)", tag)
        newest = int(m.group(1)) if m else None
        old = newest is not None and max(used[repo]) < newest
        rows.append((repo, ", ".join(f"v{v}" for v in sorted(used[repo])), tag, "UPDATE" if old else "ok"))
    width = max(len(r[0]) for r in rows)
    lines = [f"{'action'.ljust(width)}  used   latest     status", "-" * (width + 30)]
    lines += [f"{a.ljust(width)}  {u:<6} {t:<10} {s}" for a, u, t, s in rows]
    print("\n".join(lines))
    summary = __import__("os").environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as out:
            out.write("| Action | Used | Latest | |\n|---|---|---|---|\n")
            out.writelines(f"| {a} | {u} | {t} | {'**update**' if s == 'UPDATE' else 'ok'} |\n" for a, u, t, s in rows)


if __name__ == "__main__":
    main()

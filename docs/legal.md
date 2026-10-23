# Legal: what this repo contains, and what it never will

Guitar Dream Engine reimplements rhythm games from scratch. The original games
are owned by their publishers; this project is not affiliated with or endorsed
by any of them. Game names are used only to say which game a folder
reimplements.

## Never in the repo

| What | Why | Where it lives instead |
|---|---|---|
| Disc images and game files (`.iso`, `.ark`, `.hdr`, `.vgs`, `.bik`, `.milo`, ...) and anything extracted from them (textures, audio, models, scripts) | Copyrighted by the game's owners | On the player's machine, ripped from media they own |
| Decompiler or disassembler output (Ghidra projects, `decomp/`, exported pseudo-C) | It *is* the original code, mechanically translated | In the developer's private workbench, used for research only |
| Large vendor binaries (the NVIDIA DLSS SDK) | Size; they have their own distribution terms | Downloaded at build time (`gd-game-NN/tools/fetch_dlss.sh`) |
| Signing keys, passwords, tokens | Security | GitHub Actions secrets |

The **repo guard** (`.github/scripts/repo_guard.py`, run on every push and
pull request) rejects these; run it locally with
`python3 .github/scripts/repo_guard.py`.

## What is allowed

- **New code** written for this project that behaves like the original.
- **Research notes in our own words** (`gd-game-NN/docs/`): how the original
  behaves, file formats, constants, and the addresses of the functions they
  were learned from, as references. Short pseudocode that explains an
  algorithm is fine; pasted decompiler output is not.
- **Tools that read the original formats** from the user's own files.

## Licence

The project is licensed under PolyForm Noncommercial 1.0.0 (`LICENSE`) plus
`LICENSE-ADDITIONAL-TERMS.md`:

- free to use, study, fork and share, for noncommercial purposes only;
- no sale, no paywalled builds, no ads;
- every copy and fork credits "Based on Guitar Dream Engine,
  https://github.com/mbracali/prj-gd-engine" (README, release notes, in-game credits);
- whoever distributes a modified version publishes its full source, marks each
  changed file and keeps a `CHANGES.md` (template below);
- gameplay videos and streams are fine, monetised or not.

Code in `third_party/` keeps its own licence.

### `CHANGES.md` template for forks

```markdown
# Changes from Guitar Dream Engine

Based on Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine
Forked from: <repo URL> at <commit or tag>, on <date>

| Date | Files | Change |
|---|---|---|
| 2026-11-02 | gd-game-01/engine/src/game/player.cpp | Wider hammer-on window option |
```

And at the top of each changed file:

```cpp
// Modified by <name> on <date>: <what was changed>
```

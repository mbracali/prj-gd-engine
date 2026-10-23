# Contributing

Thanks for helping. Read [legal.md](legal.md) first: no game data and no
decompiler output, ever.

## Branches

| Branch | Purpose |
|---|---|
| `main` | Released code. A merge with a new version publishes a release. |
| `develop` | Integration: every feature and fix lands here first. |
| `feature/<game-id>/<topic>` | New work, e.g. `feature/gd-game-01/star-power-meter` |
| `fix/<game-id>/<topic>` | Bug fixes |
| `docs/<topic>` | Documentation only |
| `hotfix/<game-id>/<topic>` | Urgent fix to a release, from `main` |

Use `repo` as the game id for changes to workflows, docs or the licence
(`feature/repo/release-notes`).

## Commits

- Start the message with the scope: `gd-game-01: fix HOPO finger gaps`,
  `repo: add release workflow`, `gd-core: ...`.
- **Sign off every commit** (`git commit -s`). The sign-off certifies the
  [Developer Certificate of Origin](https://developercertificate.org/): you
  wrote the change (or may submit it), and you license it under the project's
  terms, including clause G of `LICENSE-ADDITIONAL-TERMS.md`.

## Pull requests

1. Branch from `develop`, open the PR against `develop`.
2. Build it locally the way the release does: `gd-game-01/local_build.sh`
   (GitHub builds nothing on pull requests; see docs/release-process.md).
3. Fill in the PR checklist. For gameplay changes, run the self-test with your
   own disc image: `gd01 --selftest "path/to/Guitar Hero (USA).iso"` must
   print `all charts fully hit`. CI cannot run it (it has no game data).
4. Add a line to the game's `CHANGELOG.md` under `[Unreleased]` when players
   will notice the change.

## Labels

`gd-game-01` (one per game), `bug`, `parity` (differs from the original),
`feature`, `enhanced` (PC-only extras), `platform:linux|windows|macos|android`,
`needs-capture` (needs footage of the original).

## Code style

Match the code around you. C++20, 4-space indent, comments explain the
original's behaviour and cite the address it was learned from
(`// FUN_0025b1f0: sets the HOPO flag`), with `[I]` for our own inferences.

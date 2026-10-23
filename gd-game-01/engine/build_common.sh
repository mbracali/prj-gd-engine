# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Status lines shared by the build_*.sh scripts (sourced; also inside the
# build containers), so every step says what it is doing.

say() { printf '   -> %s\n' "$*"; }

# compile <build dir> <name>: builds with CMake/Ninja, saying first whether
# anything needs compiling (a Ninja dry run) and how much.
compile() {
  local n logp="$1/compile.log"
  case "$1" in /*) ;; *) logp="gd-game-01/engine/$logp" ;; esac
  n="$(cmake --build "$1" -- -n 2>/dev/null | grep -c '^\[' || true)"
  if [ "$n" = 0 ]; then
    say "$2: nothing to compile, already up to date"
    return 0
  fi
  say "$2: compiling ($n steps; full compiler output in $logp)..."
  # On screen only the errors, if any; warnings and progress go to the log.
  if ! cmake --build "$1" > "$1/compile.log" 2>&1; then
    grep -E 'error|Error|FAILED' "$1/compile.log" | head -40
    say "$2: compile FAILED (see $logp)"
    return 1
  fi
  say "$2: compiled"
}

# configure <cmake args...>: CMake configure, quiet unless it fails.
configure() {
  local log
  log="$(mktemp)"
  if ! cmake "$@" > "$log" 2>&1; then cat "$log"; rm -f "$log"; return 1; fi
  rm -f "$log"
}

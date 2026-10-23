# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# Shared by the build-*.sh / *.command files: when started by a double-click
# (no terminal), reopens the script in a terminal window; runs local_build.sh
# with the given builds; keeps the window open until Enter.
run_build() {
  local script="$1"; shift
  if [ ! -t 1 ] && [ -z "${GD01_IN_TERMINAL:-}" ]; then
    export GD01_IN_TERMINAL=1
    for t in konsole gnome-terminal kgx ptyxis xfce4-terminal mate-terminal lxterminal alacritty kitty xterm; do
      command -v "$t" >/dev/null || continue
      case "$t" in
        gnome-terminal|kgx|ptyxis) exec "$t" -- bash "$script" ;;
        *) exec "$t" -e bash "$script" ;;
      esac
    done
  fi
  local here rc=0
  here="$(cd "$(dirname "$script")" && pwd)"
  "$here/../local_build.sh" "$@" || rc=$?
  echo
  # local_build.sh ended with the summary (built / up to date / failed per build).
  if [ -t 0 ]; then read -r -p "Press Enter to close..." _; fi
  return "$rc"
}

#!/usr/bin/env bash
# Start the Linux build of the game: the basic flags of upstream's tools/run.ps1 (no -Compat,
# -Dumps or -RecordInput yet). Usage:
#   linux/run-linux.sh [--game-root DIR] [--timeout SECONDS] [extra game flags...]
# Defaults: game root ~/games/fm4/disc1, no timeout. State (saves, cache, log) goes to
# .local/state like run.ps1; the log is .local/state/logs/runtime.log.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
game="$HOME/games/fm4/disc1"
timeout_s=0
extra=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --game-root) game="$2"; shift 2 ;;
    --timeout) timeout_s="$2"; shift 2 ;;
    *) extra+=("$1"); shift ;;
  esac
done
build="$root/out/build/linux-x64-release"
exe="$build/fm4_static_recomp"
[[ -x "$exe" ]] || { echo "No game build at $exe; run: python3 linux/setup-linux.py --stage game --game-root $game" >&2; exit 1; }
[[ -f "$game/default.xex" ]] || { echo "No default.xex in $game" >&2; exit 1; }
state="$root/.local/state"
mkdir -p "$state"/{user,update,cache,logs}
log="$state/logs/runtime.log"
rm -f "$log"
args=("--game_data_root=$game" "--user_data_root=$state/user" "--update_data_root=$state/update"
      "--cache_root=$state/cache" "--log_file=$log" "--gpu_backend=vulkan" "${extra[@]}")
# Audio: the game itself picks SDL's PulseAudio backend on Linux (src/fm4_app.h, OnPreSetup), so
# the launcher gets it too. Set SDL_AUDIO_DRIVER yourself to override. See linux/PORTING.md (bug 3).
cd "$build"  # run.ps1 starts the game in its own folder
echo "Starting fm4_static_recomp; log: $log"
if [[ "$timeout_s" -gt 0 ]]; then
  status=0
  timeout --signal=TERM --kill-after=10 "$timeout_s" "$exe" "${args[@]}" > "$state/logs/stdout.log" 2>&1 || status=$?
  if [[ $status -eq 124 ]]; then echo "Stopped after ${timeout_s}s (still running = no crash)"; else echo "Exited with code $status"; fi
else
  exec "$exe" "${args[@]}"
fi

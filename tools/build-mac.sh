#!/usr/bin/env bash
# Copyright (c) 2026, i2kiselev
# BSD 3-Clause License: see LICENSE in the repository root.
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Share the launcher's pinned SDK, codegen and complete build targets.
exec python3 "${ROOT}/tools/setup-macos.py" \
    --game-root "${FM4_GAME_ROOT:-${ROOT}/game/disc1}" \
    --cache-root "${ROOT}/.local/state/cache" \
    --toolchain-root "${ROOT}/.local/toolchain" "$@"

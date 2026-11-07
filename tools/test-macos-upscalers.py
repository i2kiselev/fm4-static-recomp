#!/usr/bin/env python3
# Copyright (c) 2026, i2kiselev
# BSD 3-Clause License: see LICENSE in the repository root.
"""Verify the actual FSR 2/3 runtime with GPU dispatch/readback, without game assets."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'out/build/mac-arm64-release')
    args = parser.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        parser.error('Use a native Apple Silicon macOS terminal')
    build = args.build_dir.resolve()
    if not (build / 'CMakeCache.txt').is_file():
        parser.error('Configure/build the game first, or pass its configured --build-dir')
    os.environ['PATH'] = '/opt/homebrew/bin:/usr/local/bin:' + os.environ.get('PATH', '')
    log = build / 'upscaler-smoke.log'
    runtime = build / 'upscalers/fidelityfx/1.1.4/libamd_fidelityfx_vk.dylib'
    receipt = dict(created_utc=datetime.now(timezone.utc).isoformat(),
                   macos=platform.mac_ver()[0], machine=platform.machine(), passed=False,
                   test='FSR 2/3 GPU dispatch and uniform-color readback over three frames; context resize',
                   gameplay_validated=False, frame_generation_validated=False)
    try:
        with log.open('w') as output:
            for command in (
                ['cmake', '-S', ROOT, '-B', build, '-DFM4_BUILD_UPSCALER_SMOKE=ON'],
                ['cmake', '--build', build, '--target', 'fm4_upscaler_smoke', '--parallel', '4'],
                [build / 'fm4_upscaler_smoke'],
            ):
                subprocess.run(list(map(str, command)), cwd=build, stdout=output,
                               stderr=subprocess.STDOUT, check=True)
        receipt['passed'] = True
    except (OSError, subprocess.CalledProcessError) as error:
        receipt['error'] = str(error)
    if runtime.is_file():
        receipt['fidelityfx_sha256'] = hashlib.sha256(runtime.read_bytes()).hexdigest()
    (build / 'upscaler-smoke.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS' if receipt['passed'] else 'FAIL', log)
    return 0 if receipt['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())

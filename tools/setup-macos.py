#!/usr/bin/env python3
# Copyright (c) 2026, i2kiselev
# BSD 3-Clause License: see LICENSE in the repository root.
"""Build the current ShiftGlue host for the macOS launcher, with resumable output."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

from mac_sdk import prepare_sdk

ROOT = Path(__file__).resolve().parents[1]


def event(stage, percent, message):
    print('::fm4::' + json.dumps(dict(stage=stage, percent=percent, message=message)), flush=True)


def run(arguments, cwd=ROOT):
    print('+ ' + ' '.join(map(str, arguments)), flush=True)
    subprocess.run(list(map(str, arguments)), cwd=cwd, check=True)


def build(args):
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise ValueError('The macOS launcher build requires Apple Silicon.')
    if sys.version_info < (3, 11):
        raise ValueError('Python 3.11 or newer is required (brew install python).')
    os.environ['PATH'] = '/opt/homebrew/bin:/usr/local/bin:' + os.environ.get('PATH', '')
    event('tools', 0, 'Checking Xcode Command Line Tools, CMake and Ninja.')
    for name in ('xcrun', 'git', 'cmake', 'ninja'):
        if not shutil.which(name):
            raise ValueError(f'{name} is missing. Install Xcode Command Line Tools and brew install cmake ninja python.')
    cc = subprocess.check_output(['xcrun', '--find', 'clang'], text=True).strip()
    cxx = subprocess.check_output(['xcrun', '--find', 'clang++'], text=True).strip()
    sysroot = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
    if not (args.game_root / 'default.xex').is_file():
        raise ValueError(f'default.xex is missing in {args.game_root}')
    jobs = args.jobs or min(os.cpu_count() or 1, 8)
    config = json.loads((ROOT / 'config/macos-toolchain.json').read_text())
    external = os.environ.get('FM4_SDK_ROOT')
    event('sdk', 15, 'Preparing the pinned macOS ShiftGlue SDK.')
    sdk, sdk_identity = prepare_sdk(ROOT, external)
    receipt = ROOT / '.local/logs/macos-sdk.json'
    receipt.parent.mkdir(parents=True, exist_ok=True)
    receipt.write_text(json.dumps(sdk_identity, indent=2) + '\n')
    common = ['-G', 'Ninja', f'-DCMAKE_C_COMPILER={cc}', f'-DCMAKE_CXX_COMPILER={cxx}',
              f'-DCMAKE_OSX_SYSROOT={sysroot}', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_OSX_ARCHITECTURES=arm64',
              f'-DCMAKE_OSX_DEPLOYMENT_TARGET={config["minimum_macos"]}', '-DREXGLUE_USE_VULKAN=ON',
              '-DREXGLUE_USE_D3D12=OFF', '-DREXGLUE_ENABLE_TRACY=OFF', '-DSDL_HIDAPI_LIBUSB=OFF',
              '-DREXGLUE_FFX_API_LOADER=ON', '-DREXGLUE_FIDELITYFX_RUNTIME=ON']
    sdk_build = ROOT / 'out/sdk-mac-arm64'
    run(['cmake', '-S', sdk, '-B', sdk_build, *common,
         f'-DREXGLUE_OUTPUT_DIRECTORY={sdk_build}',
         f'-DCPM_SOURCE_CACHE={ROOT / ".local/dependency-cache"}'])
    run(['cmake', '--build', sdk_build, '--target', 'rexglue', '--parallel', jobs])
    codegen = sdk_build / 'rexglue'
    if not codegen.is_file():
        raise ValueError(f'The SDK did not produce {codegen}')
    event('codegen', 45, 'Resolving the disc release and translating game code.')
    run([sys.executable, ROOT / 'tools/release_analysis.py', '--game-root', args.game_root])
    selector = (ROOT / '.local/generated/code-image.cmake').read_text()
    image = re.search(r'set\(FM4_CODE_IMAGE "([^"]+)"\)', selector).group(1)
    run([codegen, 'codegen', ROOT / '.local/generated' / image / 'fm4_manifest.toml'])
    event('build', 55, 'Compiling the game and its facade modules.')
    output = ROOT / 'out/build/mac-arm64-release'
    run(['cmake', '-S', ROOT, '-B', output, *common, f'-DREXSDK_DIR={sdk}',
         f'-DFM4_STATIC_RECOMP_REXGLUE_CODEGEN={codegen}', '-DFM4_REQUIRE_TEMPORAL_UPSCALERS=ON',
         f'-DCPM_SOURCE_CACHE={ROOT / ".local/dependency-cache"}'])
    # The facade libraries and runtime staging are ALL targets, not dependencies of the host.
    run(['cmake', '--build', output, '--parallel', jobs])
    runtime = output / 'upscalers/fidelityfx/1.1.4/libamd_fidelityfx_vk.dylib'
    if not runtime.is_file():
        raise ValueError(f'FSR 2/3 runtime was not staged: {runtime}')
    print(f'FSR 2/3 runtime ready: {runtime}', flush=True)
    binary = output / 'fm4_static_recomp'
    event('schema', 97, 'Reading graphics settings.')
    run([binary, f'--game_data_root={args.game_root}',
         f'--fm4_write_graphics_schema={output / "graphics-settings.json"}'], cwd=output)
    event('fonts', 100, 'Extracting launcher fonts from the disc.')
    try:
        run([binary, f'--game_data_root={args.game_root}',
             f'--fm4_export_ui_fonts={args.cache_root / "ui-fonts"}'], cwd=output)
    except subprocess.CalledProcessError:
        print('Font export failed; the launcher will use its bundled font.', flush=True)
    def digest(path):
        with path.open('rb') as source:
            return hashlib.file_digest(source, 'sha256').hexdigest()
    provenance = ROOT / 'config/source-provenance.json'
    built = dict(schema_version=1, backend='vulkan', sdk=sdk_identity,
                 minimum_macos=config['minimum_macos'], macos=platform.mac_ver()[0],
                 python=sys.version, code_image=image, game_xex_sha256=digest(args.game_root / 'default.xex'),
                 tools={name: subprocess.check_output(command, text=True).splitlines()[0]
                        for name, command in {'clang': [cxx, '--version'], 'cmake': ['cmake', '--version'],
                                              'ninja': ['ninja', '--version']}.items()},
                 source=json.loads(provenance.read_text()) if provenance.is_file() else None,
                 binaries={str(path.relative_to(output)): digest(path)
                           for path in [binary, *sorted(output.rglob('*.dylib'))] if not path.is_symlink()},
                 gameplay_validated=False)
    (ROOT / '.local/logs/macos-build.json').write_text(json.dumps(built, indent=2) + '\n')
    event('shaders', 100, 'Built. Shaders will compile on the first play.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root', type=Path, required=True)
    parser.add_argument('--cache-root', type=Path, required=True)
    parser.add_argument('--toolchain-root', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=0)
    args = parser.parse_args()
    if not 0 <= args.jobs <= 32:
        parser.error('--jobs must be between 0 (automatic) and 32')
    logs = ROOT / '.local/logs'
    logs.mkdir(parents=True, exist_ok=True)
    failure = logs / 'setup-error.json'
    failure.unlink(missing_ok=True)
    (logs / 'macos-build.json').unlink(missing_ok=True)
    try:
        build(args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        failure.write_text(json.dumps(dict(message=str(error), step='macOS build', exit_code=1,
            error_excerpt=[], hint='See the launcher log for the failing command.', logs=str(logs))) + '\n')
        print(f'Setup failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

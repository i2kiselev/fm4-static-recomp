#!/usr/bin/env python3
"""Build FM4 Static Recomp natively on Linux (x86-64), modelled on upstream's tools/setup-macos.py.

Linux files live in linux/; no PowerShell. Uses the system's clang, CMake and Ninja.

Stages (run in order; each one reuses the previous one's output):
  sdk    fetch the pinned ShiftGlue SDK (same commit as Windows/Mac) and build its `rexglue`
         translator. No disc needed.
  tests  build and run the host unit tests (FM4_TESTS_ONLY). No disc needed; with --game-root the
         tests that read Disc 1 run too (FM4_TEST_GAME_ROOT), otherwise they are skipped.
  game   translate the disc's game code and build the game. Needs --game-root with default.xex.

    python3 linux/setup-linux.py --stage sdk

Without --stage (the launcher's call, as tools/setup-macos.py): sdk then game in one run,
    python3 linux/setup-linux.py --game-root G --cache-root C [--jobs N]
printing `::fm4::{json}` progress lines and writing .local/logs/setup-error.json on failure.

Compiler output goes to .local/logs/linux-<stage>.log; the screen shows progress only, plus the
last error lines if a step fails. That keeps agent sessions from reading huge build logs.
"""
import argparse
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from mac_sdk import prepare_sdk  # noqa: E402  (plain git + Python; platform-neutral)

LOGS = ROOT / '.local/logs'
SDK_BUILD = ROOT / 'out/sdk-linux-x64'
TESTS_BUILD = ROOT / 'out/build/linux-x64-tests'
GAME_BUILD = ROOT / 'out/build/linux-x64-release'
DEPENDENCY_CACHE = ROOT / '.local/dependency-cache'

# Same switches as the Mac build (tools/setup-macos.py 54-58), minus the Apple-only ones, plus
# the Windows preset's -msse4.1 (CMakePresets.json): both are x86-64.
COMMON = ['-G', 'Ninja', '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++',
          '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_FLAGS=-msse4.1', '-DCMAKE_CXX_FLAGS=-msse4.1',
          '-DREXGLUE_USE_VULKAN=ON', '-DREXGLUE_USE_D3D12=OFF', '-DREXGLUE_ENABLE_TRACY=OFF',
          '-DSDL_HIDAPI_LIBUSB=OFF', '-DREXGLUE_FFX_API_LOADER=ON', '-DREXGLUE_FIDELITYFX_RUNTIME=ON',
          f'-DCPM_SOURCE_CACHE={DEPENDENCY_CACHE}']


def say(message):
    print(f'[{time.strftime("%H:%M:%S")}] {message}', flush=True)


def event(stage, percent, message):
    """Progress line for the launcher (SetupEvent.cs); same format as tools/setup-macos.py."""
    print('::fm4::' + json.dumps(dict(stage=stage, percent=percent, message=message)), flush=True)


def run(arguments, log, cwd=ROOT):
    """Run a command with its output appended to `log`; on failure show the last error lines."""
    arguments = [str(a) for a in arguments]
    with log.open('a') as out:
        out.write('\n+ ' + ' '.join(arguments) + '\n')
        out.flush()
        result = subprocess.run(arguments, cwd=cwd, stdout=out, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        lines = log.read_text(errors='replace').splitlines()
        # CMake puts the real message on the lines after "CMake Error at ...": show those.
        cmake = [i for i, l in enumerate(lines) if l.startswith('CMake Error')]
        if cmake:
            shown = lines[cmake[0]:cmake[0] + 8]
        else:
            # Compiler errors and Ninja's "FAILED: <object>" line (names the file being compiled).
            # Not every line containing "error": file names like SDL_error.c matched that before.
            shown = [l for l in lines if l.startswith('FAILED:') or ' error: ' in l
                     or 'fatal error:' in l or l.startswith('In file included from')][-15:] or lines[-15:]
        print('\n'.join(shown), file=sys.stderr)
        raise RuntimeError(f'{Path(arguments[0]).name} failed (exit {result.returncode}). Full log: {log}')


def ninja_step(log):
    """Last `[done/total]` counter Ninja wrote to `log`, or None (reads only the end of the file)."""
    try:
        with log.open('rb') as f:
            f.seek(max(0, f.seek(0, 2) - 8192))
            found = re.findall(rb'^\[(\d+)/(\d+)\]', f.read(), re.M)
    except OSError:
        return None
    return (int(found[-1][0]), int(found[-1][1])) if found else None


class CompileProgress:
    """While a long Ninja build runs, turn its [done/total] counter into launcher progress events
    between `low` and `high` percent, so the bar moves instead of sitting still for many minutes.
    Also tracks the lowest free RAM, reported after the compile."""
    def __init__(self, log, stage, low, high, label):
        self.log, self.stage, self.low, self.high, self.label = log, stage, low, high, label
        self.lowest_ram = available_ram_gb()
        self.done = threading.Event()
        self.thread = threading.Thread(target=self.watch, daemon=True)

    def watch(self):
        shown = self.low
        while not self.done.wait(2):
            self.lowest_ram = min(self.lowest_ram, available_ram_gb())
            step = ninja_step(self.log)
            if step and step[1]:
                percent = self.low + (self.high - self.low) * step[0] // step[1]
                if percent > shown:
                    shown = percent
                    event(self.stage, percent, f'{self.label} ({step[0]} of {step[1]} files).')

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *exc):
        self.done.set()
        self.thread.join()


class PreflightError(RuntimeError):
    """Something the build needs is missing; `hint` is the command that fixes it."""
    def __init__(self, message, hint):
        super().__init__(message)
        self.hint = hint


def check_system():
    spec = importlib.util.spec_from_file_location('check_system', ROOT / 'linux/check-system.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def preflight():
    """linux/check-system.py before a long build: fail in seconds with the fix, not minutes in."""
    problems, warnings, fixes, _ = check_system().check(ROOT)
    for line in warnings:
        say(f'Warning: {line}')
    if problems:
        hint = 'Run: ' + ' && '.join(fixes) if fixes else 'See python3 linux/check-system.py'
        raise PreflightError('Not ready to build: ' + '; '.join(problems), hint)


def check_tools():
    missing = [t for t in ('cmake', 'ninja', 'git') if not shutil.which(t)]
    checks = check_system()
    compilers = checks.find_clang()
    if not compilers:
        missing.insert(0, f'clang {checks.MIN_CLANG}+')
    if missing:
        raise RuntimeError(f'Missing tools: {", ".join(missing)}. Run python3 linux/check-system.py for the install command.')
    # Ubuntu installs newer versions beside the default one as clang-NN; use the one found.
    COMMON[COMMON.index('-DCMAKE_C_COMPILER=clang')] = f'-DCMAKE_C_COMPILER={compilers[0]}'
    COMMON[COMMON.index('-DCMAKE_CXX_COMPILER=clang++')] = f'-DCMAKE_CXX_COMPILER={compilers[1]}'
    if compilers[0] != 'clang':
        say(f'Using {compilers[0]} (the default clang is too old).')


SDK_PATCHES = sorted((ROOT / 'linux/patches').glob('sdk-*.patch'))


def linux_sdk():
    """upstream's prepare_sdk, then our linux/patches/sdk-*.patch on top (linux/PORTING.md, bug 4).
    prepare_sdk refuses a managed SDK with local edits, so take the patches off first and put
    them back after its checks. FM4_SDK_ROOT (your own SDK checkout) is used as is, unpatched."""
    override = os.environ.get('FM4_SDK_ROOT')
    if override:
        return prepare_sdk(ROOT, override)
    managed = ROOT / '.local/shiftglue-sdk-macos'
    for sdk_dir in managed.glob('*/.git'):
        for patch in reversed(SDK_PATCHES):
            applied = subprocess.run(['git', '-C', sdk_dir.parent, 'apply', '-R', '--check', patch],
                                     capture_output=True).returncode == 0
            if applied:
                subprocess.run(['git', '-C', sdk_dir.parent, 'apply', '-R', patch], check=True)
    sdk, identity = prepare_sdk(ROOT, None)
    for patch in SDK_PATCHES:
        subprocess.run(['git', '-C', sdk, 'apply', patch], check=True)
    identity['linux_patches'] = [p.name for p in SDK_PATCHES]
    return sdk, identity


def forget_stale_build(build, setting, expected):
    """CMake refuses a build folder configured for another source, and a game build keeps the
    old REXSDK_DIR. So when upstream moves the SDK pin, start that build folder fresh."""
    cache = build / 'CMakeCache.txt'
    if not cache.is_file():
        return
    match = re.search(rf'^{setting}:\w+=(.*)$', cache.read_text(errors='replace'), re.M)
    if match and Path(match.group(1)).resolve() != Path(expected).resolve():
        say(f'{build.name} was built against another SDK; starting it fresh.')
        shutil.rmtree(build)


def stage_sdk(jobs):
    log = LOGS / 'linux-sdk.log'
    log.unlink(missing_ok=True)
    event('sdk', 15, 'Preparing the pinned ShiftGlue SDK and its translator.')
    say('Fetching the pinned ShiftGlue SDK (first run clones it with submodules; can take a while).')
    sdk, identity = linux_sdk()
    (LOGS / 'linux-sdk.json').write_text(json.dumps(identity, indent=2) + '\n')
    say(f'SDK source: {sdk}')
    say('Configuring the SDK (downloads its dependencies on the first run).')
    forget_stale_build(SDK_BUILD, 'CMAKE_HOME_DIRECTORY', sdk)
    run(['cmake', '-S', sdk, '-B', SDK_BUILD, *COMMON, f'-DREXGLUE_OUTPUT_DIRECTORY={SDK_BUILD}'], log)
    say(f'Building the rexglue translator with {jobs} jobs.')
    with CompileProgress(log, 'sdk', 15, 44, 'Compiling the SDK translator'):
        run(['cmake', '--build', SDK_BUILD, '--target', 'rexglue', '--parallel', jobs], log)
    codegen = SDK_BUILD / 'rexglue'
    if not codegen.is_file():
        raise RuntimeError(f'The SDK build finished but did not produce {codegen}')
    say(f'Done: {codegen}')
    return sdk, codegen


def stage_tests(jobs, game_root):
    """Mirror upstream CI (.github/workflows/ci.yml): FM4_TESTS_ONLY + REXGLUE_BUILD_TESTS, then run
    both suites: fm4_unit_tests (host) and the SDK's unit_tests."""
    log = LOGS / 'linux-tests.log'
    log.unlink(missing_ok=True)
    sdk, _ = linux_sdk()
    codegen = SDK_BUILD / 'rexglue'
    if not codegen.is_file():
        raise RuntimeError('Run --stage sdk first (no rexglue translator yet).')
    say('Configuring the unit tests (no disc, no generated game code).')
    forget_stale_build(TESTS_BUILD, 'REXSDK_DIR', sdk)
    run(['cmake', '-S', ROOT, '-B', TESTS_BUILD, *COMMON, f'-DREXSDK_DIR={sdk}',
         f'-DFM4_STATIC_RECOMP_REXGLUE_CODEGEN={codegen}', '-DFM4_TESTS_ONLY=ON',
         '-DREXGLUE_BUILD_TESTS=ON'], log)
    say(f'Building fm4_unit_tests and the SDK unit_tests with {jobs} jobs (builds the runtime; slow).')
    run(['cmake', '--build', TESTS_BUILD, '--target', 'fm4_unit_tests', 'unit_tests', '--parallel', jobs], log)
    env = dict(os.environ)
    if game_root:
        if not (game_root / 'default.xex').is_file():
            raise ValueError(f'No default.xex in --game-root {game_root}')
        env['FM4_TEST_GAME_ROOT'] = str(game_root)
        say(f'Disc tests read game files from {game_root}.')
    failed = []
    for name, binary in (('fm4_unit_tests', TESTS_BUILD / 'fm4-tests/fm4_unit_tests'),
                         ('unit_tests', TESTS_BUILD / 'rexglue-artifacts/unit_tests')):
        out_log = LOGS / f'linux-{name}.log'
        with out_log.open('w') as out:
            result = subprocess.run([str(binary)], cwd=TESTS_BUILD, env=env, stdout=out, stderr=subprocess.STDOUT)
        lines = out_log.read_text(errors='replace').splitlines()
        summary = [l for l in lines if l.startswith(('All tests passed', 'test cases:', 'assertions:'))]
        say(f'{name}: ' + (' / '.join(summary) or f'exit {result.returncode}, no Catch2 summary'))
        if result.returncode != 0:
            failed.append(name)
            failing = [l for l in lines if 'FAILED' in l or ('TEST_CASE' not in l and l.startswith('  '))]
            print('\n'.join(failing[:20]), file=sys.stderr)
            say(f'Full output: {out_log}')
    if failed:
        raise RuntimeError(f'Failing suites: {", ".join(failed)}')


def available_ram_gb():
    for line in Path('/proc/meminfo').read_text().splitlines():
        if line.startswith('MemAvailable:'):
            return int(line.split()[1]) / 1024 / 1024
    return 0.0


def stage_game(jobs, game_root, fonts_dir=ROOT / '.local/cache/ui-fonts'):
    """Mirror tools/setup-macos.py 67-92: identify the release, translate the game code, build the
    game and facade modules, then run the binary once to write the graphics schema and fonts."""
    if not game_root or not (game_root / 'default.xex').is_file():
        raise ValueError('Stage game needs --game-root pointing at the folder with default.xex.')
    log = LOGS / 'linux-game.log'
    log.unlink(missing_ok=True)
    sdk, _ = linux_sdk()
    codegen = SDK_BUILD / 'rexglue'
    if not codegen.is_file():
        raise RuntimeError('Run --stage sdk first (no rexglue translator yet).')
    event('codegen', 45, 'Resolving the disc release and translating game code.')
    say('Identifying the disc release (release_analysis.py).')
    run([sys.executable, ROOT / 'tools/release_analysis.py', '--game-root', game_root], log)
    selector = (ROOT / '.local/generated/code-image.cmake').read_text()
    image = re.search(r'set\(FM4_CODE_IMAGE "([^"]+)"\)', selector).group(1)
    say(f'Code image: {image}. Translating the Xbox 360 code into C++ (rexglue codegen).')
    run([codegen, 'codegen', ROOT / '.local/generated' / image / 'fm4_manifest.toml'], log)
    say('Configuring the game build.')
    # Upstream links the game to the SDK's `renderdoc` header target on Windows only; the non-Windows
    # branch was written for the Mac, whose code skips that header. Linux includes it like Windows,
    # so add the header folder here instead of editing upstream's CMakeLists.txt (linux/PORTING.md, bug 2).
    # As a standard include directory, not in CMAKE_CXX_FLAGS: CMake quotes it, so a path with
    # spaces works (the launcher's folder can be "FM4 Linux laptop/..."; 2026-11-15 laptop test).
    forget_stale_build(GAME_BUILD, 'REXSDK_DIR', sdk)
    run(['cmake', '-S', ROOT, '-B', GAME_BUILD, *COMMON, f'-DREXSDK_DIR={sdk}',
         f'-DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES={sdk / "thirdparty/renderdoc"}',
         f'-DFM4_STATIC_RECOMP_REXGLUE_CODEGEN={codegen}', '-DFM4_REQUIRE_TEMPORAL_UPSCALERS=ON'], log)
    event('build', 55, 'Compiling the game and its facade modules.')
    say(f'Compiling the game and its facade modules with {jobs} jobs (slow; log: {log}).')
    progress = CompileProgress(log, 'build', 55, 96, 'Compiling the game')
    try:
        with progress:
            # The facade libraries and runtime staging are ALL targets, not dependencies of the host.
            run(['cmake', '--build', GAME_BUILD, '--parallel', jobs], log)
    finally:
        say(f'Lowest free RAM during the compile: {progress.lowest_ram:.1f} GB.')
    runtime = GAME_BUILD / 'upscalers/fidelityfx/1.1.4/libamd_fidelityfx_vk.so'
    if not runtime.is_file():
        raise RuntimeError(f'FSR 2/3 runtime was not staged: {runtime}')
    binary = GAME_BUILD / 'fm4_static_recomp'
    if not binary.is_file():
        raise RuntimeError(f'The build finished but did not produce {binary}')
    say(f'Game built: {binary}')
    event('schema', 97, 'Reading graphics settings.')
    say('Running it once to write the graphics settings list.')
    run([binary, f'--game_data_root={game_root}',
         f'--fm4_write_graphics_schema={GAME_BUILD / "graphics-settings.json"}'], log, cwd=GAME_BUILD)
    event('fonts', 100, 'Extracting launcher fonts from the disc.')
    say('Running it once to extract the menu fonts from the disc.')
    try:
        run([binary, f'--game_data_root={game_root}',
             f'--fm4_export_ui_fonts={fonts_dir}'], log, cwd=GAME_BUILD)
    except RuntimeError:
        say('Font export failed (the Mac script treats this as non-fatal; launcher would use its own font).')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--stage', choices=['sdk', 'tests', 'game'],
                        help='one stage; omit for the launcher\'s full build (sdk then game)')
    parser.add_argument('--jobs', type=int, default=0, help='parallel compile jobs (0 = automatic)')
    parser.add_argument('--game-root', type=Path, help='folder with default.xex (stages tests and game)')
    parser.add_argument('--cache-root', type=Path, help='full build: where the menu fonts go (launcher)')
    args = parser.parse_args()
    if not args.stage and not (args.game_root and args.cache_root):
        parser.error('give --stage, or --game-root and --cache-root for the full build')
    if not 0 <= args.jobs <= 64:
        parser.error('--jobs must be between 0 (automatic) and 64')
    jobs = args.jobs or os.cpu_count() or 1
    if args.stage != 'sdk' and args.stage != 'tests' and not args.jobs:
        jobs = min(jobs, 8)  # as tools/setup-macos.py: big generated files need a lot of RAM each
    game_root = args.game_root.expanduser().resolve() if args.game_root else None
    LOGS.mkdir(parents=True, exist_ok=True)
    failure = LOGS / 'setup-error.json'  # read by the launcher (SetupRunner.ReadFailure)
    failure.unlink(missing_ok=True)
    name = args.stage or 'full build'
    start = time.monotonic()
    try:
        event('tools', 0, 'Checking this PC: build tools, Vulkan, OpenSSL, disk space.')
        check_tools()
        if args.stage != 'sdk':
            preflight()
        if args.stage == 'sdk':
            stage_sdk(jobs)
        elif args.stage == 'tests':
            stage_tests(jobs, game_root)
        elif args.stage == 'game':
            stage_game(jobs, game_root)
        else:
            stage_sdk(args.jobs or os.cpu_count() or 1)
            stage_game(jobs, game_root, args.cache_root.expanduser().resolve() / 'ui-fonts')
            event('shaders', 100, 'Built. Shaders will compile on the first play.')
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        say(f'FAILED after {time.monotonic() - start:.0f}s: {error}')
        failure.write_text(json.dumps(dict(
            message=str(error), step=f'Linux {name}', exit_code=1, error_excerpt=[],
            hint=getattr(error, 'hint', None) or f'See the logs in {LOGS} (linux-sdk.log, linux-game.log).',
            logs=str(LOGS))) + '\n')
        return 1
    say(f'{name.capitalize()} finished in {time.monotonic() - start:.0f}s.')
    return 0

if __name__ == '__main__':
    sys.exit(main())

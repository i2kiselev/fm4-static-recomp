#!/usr/bin/env python3
# Copyright (c) 2026, i2kiselev
# BSD 3-Clause License: see LICENSE in the repository root.
"""Publish the Avalonia Apple Silicon launcher and its game-free source payload."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
# Keep this payload limited to build sources; no game, generated guest code, saves or SDK binaries.
SOURCES = ('CMakeLists.txt', 'CMakePresets.json', 'LICENSE', 'THIRD_PARTY_NOTICES.md',
           'cmake', 'src', 'config/rexglue', 'config/release.json', 'config/release-toolchain.json',
           'config/macos-toolchain.json', 'config/supported-discs.json', 'config/pfp4-1.3.json', 'config/shaders',
           'tools/mac_sdk.py', 'tools/setup-macos.py', 'tools/release_analysis.py', 'tools/code_images.py',
           'tests', 'tools/test-macos-upscalers.py', 'README.md', 'guide/macos.md', 'guide/macos-upscaling.md')


def source_payload(destination, release=None):
    release = release or json.loads((ROOT / 'config/release.json').read_text())
    paths = []
    for name in SOURCES:
        source = ROOT / name
        if not source.exists():
            raise FileNotFoundError(f'Required source payload entry is missing: {source}')
        paths.extend(source.rglob('*') if source.is_dir() else [source])
    with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(paths):
            if not path.is_file() or path.name == 'fm4_manifest.local.toml' or '__pycache__' in path.parts:
                continue
            entry = zipfile.ZipInfo(path.relative_to(ROOT).as_posix(), (2026, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, (json.dumps(release, indent=2) + '\n').encode()
                             if entry.filename == 'config/release.json' else path.read_bytes())
        provenance = dict(schema_version=1, repository=release['repository'],
            commit=subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
            dirty=bool(subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True)))
        archive.writestr('config/source-provenance.json', json.dumps(provenance, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'out/launcher-macos')
    args = parser.parse_args()
    output = args.output_dir.resolve()
    bundle = output / 'FM4 Static Recomp.app'
    if bundle.exists():
        parser.error(f'{bundle} already exists; use a fresh --output-dir')
    for name in ('dotnet', 'codesign', 'ditto'):
        if not shutil.which(name):
            parser.error(f'Required packaging tool is missing: {name}')
    release = json.loads((ROOT / 'config/release.json').read_text())
    minimum_macos = json.loads((ROOT / 'config/macos-toolchain.json').read_text())['minimum_macos']
    executable_dir = bundle / 'Contents/MacOS'
    executable_dir.mkdir(parents=True)
    subprocess.run(['dotnet', 'publish', str(ROOT / 'launcher/Fm4StaticRecomp.Launcher'),
        '-c', 'Release', '-r', 'osx-arm64', '-o', str(executable_dir),
        f'-p:Version={release["version"]}', '-p:PublishSingleFile=true', '-p:NuGetAudit=false'], check=True)
    resources = bundle / 'Contents/Resources'
    resources.mkdir()
    source_payload(resources / 'fm4-static-recomp-source.zip', release)
    for symbols in executable_dir.glob('*.pdb'):
        symbols.rename(resources / symbols.name)
    shutil.copy2(ROOT / 'THIRD_PARTY_NOTICES.md', resources)
    # Include upstream notices shipped in the restored packages.
    assets = json.loads((ROOT / 'launcher/Fm4StaticRecomp.Launcher/obj/project.assets.json').read_text())
    notices = resources / 'notices'
    for folder in assets['packageFolders']:
        for library in assets['libraries'].values():
            package = Path(folder) / library.get('path', '')
            if not package.is_dir():
                continue
            for path in package.iterdir():
                if path.is_file() and path.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE', 'THIRD-PARTY', 'THIRD_PARTY')):
                    target = notices / package.parent.name / path.name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(path, target)
    with (bundle / 'Contents/Info.plist').open('wb') as stream:
        plistlib.dump(dict(CFBundleExecutable='FM4StaticRecompLauncher',
            CFBundleIdentifier='org.fm4staticrecomp.launcher', CFBundleName='FM4 Static Recomp',
            CFBundleDisplayName='FM4 Static Recomp', CFBundlePackageType='APPL',
            CFBundleVersion=release['version'].split('-')[0], CFBundleShortVersionString=release['version'].split('-')[0],
            LSMinimumSystemVersion=minimum_macos, NSHighResolutionCapable=True), stream)
    # Sign native libraries before sealing the bundle. Published managed DLLs are not Mach-O.
    for path in executable_dir.iterdir():
        if path.suffix == '.dylib':
            subprocess.run(['codesign', '--force', '--sign', '-', str(path)], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(bundle)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(bundle)], check=True)
    (output / 'README.txt').write_text(
        'FM4 Static Recomp — macOS Apple Silicon preview\n\n'
        'Keep this app in a writable folder. Open it to install your own verified discs.\n'
        f'Requires macOS {minimum_macos}+, Xcode Command Line Tools, Python 3.11+, CMake and Ninja.\n'
        'Install command line tools with xcode-select --install; brew install python cmake ninja\n'
        'Setup downloads and builds the pinned ShiftGlue SDK and then compiles your game locally.\n'
        'The data folder beside the app contains discs, content, profiles, builds and settings.\n'
        'Keep data when updating. Mac preview updates are manual.\n'
        'Locally signed; not notarized. Experimental Apple Silicon preview.\n'
        'See guide/macos.md in the source payload for testing status and known limits.\n'
        'MetalFX upscaling and frame interpolation are not included.\n'
        'Support and updates: ' + release['repository'] + '\n')
    # ditto preserves bundle file modes and the signature for Finder extraction.
    archive = output / 'FM4StaticRecomp-Launcher-macos-arm64.zip'
    subprocess.run(['/usr/bin/ditto', '-c', '-k', '--keepParent', str(bundle), str(archive)], check=True)
    with zipfile.ZipFile(archive, 'a', zipfile.ZIP_DEFLATED) as packaged:
        packaged.write(output / 'README.txt', 'README.txt')
    archive.with_suffix('.zip.sha256').write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + '  ' + archive.name + '\n')
    print(bundle)
    print(archive)


if __name__ == '__main__':
    main()

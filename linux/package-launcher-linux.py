#!/usr/bin/env python3
"""Package the launcher for Linux x86-64, modelled on upstream's tools/package-launcher-macos.py.

Reuses upstream's source_payload() (imported, not copied) so the payload follows
upstream's file list, plus the Linux build files. No .app bundle or code signing: the launcher
is a single executable with fm4-static-recomp-source.zip beside it (PayloadInfo.cs).

    python3 linux/package-launcher-linux.py [--output-dir out/launcher-linux]

Output: <output-dir>/FM4StaticRecomp-Launcher-linux-x64.tar.gz (+ .sha256). A tarball rather than
a zip because it keeps the launcher's executable permission.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
FOLDER = 'FM4StaticRecomp-Launcher-linux-x64'
# What tools/setup-macos.py needs is in upstream's list already; these are the Linux extras.
LINUX_SOURCES = ('linux/setup-linux.py', 'linux/check-system.py', 'linux/sha256.h', 'linux/patches')
# Shipped beside the launcher: run before installing / add the launcher to the app menu.
BESIDE = ('linux/check-system.py', 'linux/add-to-app-menu.sh')


def check_system():
    spec = importlib.util.spec_from_file_location('check_system', ROOT / 'linux/check-system.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_icon(ico, png):
    """The menu icon: the largest PNG inside the launcher's Windows .ico (all its sizes are PNG)."""
    data = ico.read_bytes()
    count = struct.unpack_from('<H', data, 4)[0]
    entries = [struct.unpack_from('<BBBBHHII', data, 6 + 16 * i) for i in range(count)]
    _, _, _, _, _, _, size, offset = max(entries, key=lambda e: (e[0] or 256))
    image = data[offset:offset + size]
    if not image.startswith(b'\x89PNG'):
        raise ValueError(f'{ico}: largest icon is not a PNG')
    png.write_bytes(image)


def upstream_packager():
    spec = importlib.util.spec_from_file_location('package_launcher_macos',
                                                  ROOT / 'tools/package-launcher-macos.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.SOURCES = tuple(module.SOURCES) + LINUX_SOURCES
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'out/launcher-linux')
    args = parser.parse_args()
    output = args.output_dir.resolve()
    folder = output / FOLDER
    if folder.exists():
        parser.error(f'{folder} already exists; use a fresh --output-dir')
    if not shutil.which('dotnet'):
        parser.error('Required packaging tool is missing: dotnet (dnf install dotnet-sdk-8.0)')
    release = json.loads((ROOT / 'config/release.json').read_text())
    folder.mkdir(parents=True)
    subprocess.run(['dotnet', 'publish', str(ROOT / 'launcher/Fm4StaticRecomp.Launcher'),
        '-c', 'Release', '-r', 'linux-x64', '-o', str(folder),
        f'-p:Version={release["version"]}', '-p:PublishSingleFile=true'], check=True)
    upstream_packager().source_payload(folder / 'fm4-static-recomp-source.zip', release)
    symbols = folder / 'symbols'
    for pdb in folder.glob('*.pdb'):
        symbols.mkdir(exist_ok=True)
        pdb.rename(symbols / pdb.name)
    shutil.copy2(ROOT / 'THIRD_PARTY_NOTICES.md', folder)
    for name in BESIDE:
        shutil.copy2(ROOT / name, folder)
    write_icon(ROOT / 'launcher/Fm4StaticRecomp.Launcher/Assets/launcher.ico', folder / 'fm4-static-recomp.png')
    checks = check_system()
    (folder / 'README.txt').write_text(
        'FM4 Static Recomp: Linux x86-64 preview\n\n'
        '1. Check this PC (says exactly what to install if anything is missing):\n'
        '     python3 check-system.py\n'
        '   On a fresh install that is usually these build tools, plus OpenSSL 3 for the launcher\n'
        '   (newer distributions ship OpenSSL 4):\n'
        + ''.join(f'   {d["name"]}:\n     {d["install"]} {" ".join(d["packages"])}\n'
                  f'     {d["install"]} {d["openssl3"]}\n'
                  + (f'     {d["newer_clang"]}   (only if check-system says clang is too old, e.g. Ubuntu 24.04)\n'
                     if 'newer_clang' in d else '') for d in checks.DISTROS.values()) +
        '2. Optional: put the launcher in your app menu:   ./add-to-app-menu.sh\n'
        '3. Start it:   ./FM4StaticRecompLauncher   and install from your own Disc 1 (folder or .iso).\n'
        '   Setup downloads and builds the pinned ShiftGlue SDK, then compiles the game locally\n'
        '   (about 7 GB of RAM at peak; roughly 6 minutes on a desktop, 15 on a laptop).\n\n'
        'Keep this folder in your home directory (not /tmp). Its data folder holds the build, settings\n'
        'and profiles: keep it when updating. Linux preview updates are manual.\n'
        'Guide: guide/linux.md in the source. Support and updates: ' + release['repository'] + '\n')
    archive = output / f'{FOLDER}.tar.gz'
    with tarfile.open(archive, 'w:gz') as packed:
        packed.add(folder, arcname=FOLDER)
    archive.with_name(archive.name + '.sha256').write_text(
        hashlib.sha256(archive.read_bytes()).hexdigest() + '  ' + archive.name + '\n')
    print(folder)
    print(archive)


if __name__ == '__main__':
    main()

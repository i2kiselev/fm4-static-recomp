#!/usr/bin/env python3
"""Build the Flatpak of the Linux launcher as one installable file.

    python3 linux/flatpak/build-flatpak.py

Needs on the building PC: dotnet-sdk-8.0 (for the launcher, as linux/package-launcher-linux.py),
flatpak, and the Flathub remote. Installs org.flatpak.Builder for this user if it is missing; the
SDK and LLVM extension are fetched from Flathub by the build.

Output: out/flatpak/FM4StaticRecomp.flatpak. Players install it with
    flatpak install --user FM4StaticRecomp.flatpak
which also fetches the Freedesktop SDK runtime from Flathub.
"""
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / 'linux/flatpak'
APP_ID = 'io.github.waterbiscuit5.FM4StaticRecomp'
OUT = ROOT / 'out/flatpak'
FLATHUB = 'https://dl.flathub.org/repo/flathub.flatpakrepo'


def run(arguments):
    print('+', ' '.join(str(a) for a in arguments), flush=True)
    subprocess.run([str(a) for a in arguments], check=True)


def main():
    if not shutil.which('flatpak'):
        sys.exit('flatpak is not installed')
    run(['flatpak', 'remote-add', '--user', '--if-not-exists', 'flathub', FLATHUB])
    builder = subprocess.run(['flatpak', 'info', 'org.flatpak.Builder'], capture_output=True).returncode == 0
    if not builder:
        run(['flatpak', 'install', '--user', '-y', 'flathub', 'org.flatpak.Builder'])

    launcher = OUT / 'launcher'
    shutil.rmtree(launcher, ignore_errors=True)
    run([sys.executable, ROOT / 'linux/package-launcher-linux.py', '--output-dir', launcher])

    # --disable-cache: the builder does not notice a changed local launcher tarball and would reuse
    # the old one (both modules only copy files, so rebuilding them takes seconds).
    run(['flatpak', 'run', 'org.flatpak.Builder', '--user', '--force-clean', '--disable-cache',
         '--install-deps-from=flathub',
         f'--state-dir={OUT / "state"}', f'--repo={OUT / "repo"}', OUT / 'build', HERE / f'{APP_ID}.yml'])
    bundle = OUT / 'FM4StaticRecomp.flatpak'
    run(['flatpak', 'build-bundle', f'--runtime-repo={FLATHUB}', OUT / 'repo', bundle, APP_ID])
    print(bundle)


if __name__ == '__main__':
    main()

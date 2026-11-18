#!/usr/bin/env python3
"""Check that this Linux PC can build and run FM4 Static Recomp, and print the fix for anything missing.

    python3 check-system.py          (from the launcher folder, or linux/check-system.py in the repo)

Exit code 0 = ready (warnings allowed), 1 = something needed is missing. linux/setup-linux.py runs
the same checks before building, and linux/package-launcher-linux.py ships this file beside the
launcher. Package lists exist for Fedora, Arch and Ubuntu (and distributions based on them); elsewhere
only the tool, library, GPU and disk checks run.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

# Per distribution family: install command, the packages the game build needs, and the package
# that adds OpenSSL 3 (the launcher's built-in .NET 8 loads it; Fedora 45+ and Arch ship OpenSSL 4).
# Each list was checked by a full build in a clean install (Arch: container, 2026-11-16).
DISTROS = {
    'fedora': dict(
        name='Fedora', install='sudo dnf install', openssl3="'libssl.so.3()(64bit)'",
        packages='python3 git clang lld cmake ninja-build vulkan-loader-devel vulkan-headers vulkan-tools '
        'libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel wayland-devel '
        'libxkbcommon-devel mesa-libGL-devel libXScrnSaver-devel libXfixes-devel libXtst-devel '
        'alsa-lib-devel pulseaudio-libs-devel pipewire-devel pipewire-jack-audio-connection-kit-devel '
        'dbus-devel ibus-devel systemd-devel mesa-libEGL-devel mesa-libGLES-devel mesa-libgbm-devel '
        'libdrm-devel wayland-protocols-devel libdecor-devel libusb1-devel'.split()),
    'arch': dict(
        name='Arch', install='sudo pacman -S --needed', openssl3='openssl-3',
        packages='python git clang lld cmake ninja pkgconf vulkan-icd-loader vulkan-headers vulkan-tools '
        'vulkan-radeon vulkan-intel libx11 libxext libxrandr libxcursor libxi wayland libxkbcommon mesa '
        'libxss libxfixes libxtst alsa-lib libpulse pipewire pipewire-jack dbus ibus systemd libdrm '
        'wayland-protocols libdecor libusb'.split()),
    'debian': dict(
        name='Ubuntu', install='sudo apt install', openssl3='libssl3t64', newer_clang='sudo apt install clang-20',
        packages='python3 git clang lld cmake ninja-build pkg-config libvulkan-dev vulkan-tools '
        'mesa-vulkan-drivers libx11-dev libx11-xcb-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev '
        'libwayland-dev libxkbcommon-dev libgl-dev libxss-dev libxfixes-dev libxtst-dev libasound2-dev '
        'libpulse-dev libpipewire-0.3-dev libjack-jackd2-dev libdbus-1-dev libibus-1.0-dev libudev-dev '
        'libegl-dev libgles-dev libgbm-dev libdrm-dev wayland-protocols libdecor-0-dev libusb-1.0-0-dev'.split()),
}
MIN_RAM_GB = 12    # the compile peaked at about 7 GB free-RAM use; leave room for the desktop
MIN_DISK_GB = 20   # SDK + generated code + build: about 11 GB on the Fedora 45 test laptop


IN_FLATPAK = Path('/.flatpak-info').exists()


def distro():
    """'fedora', 'arch', 'debian' (Ubuntu and its family) or None, from /etc/os-release ID + ID_LIKE."""
    try:
        fields = dict(line.split('=', 1) for line in Path('/etc/os-release').read_text().splitlines() if '=' in line)
    except OSError:
        return None
    ids = (fields.get('ID', '') + ' ' + fields.get('ID_LIKE', '')).replace('"', '').split()
    return next((family for family in DISTROS if family in ids), None)


# Packages that are really "this command exists" (Fedora may install e.g. git-core instead of git).
COMMANDS = {'python3': 'python3', 'git': 'git', 'clang': 'clang', 'lld': 'ld.lld', 'cmake': 'cmake',
            'ninja-build': 'ninja', 'vulkan-tools': 'vulkaninfo'}


def missing_packages(family, names):
    """Names nothing installed provides. Fedora: `rpm -q --whatprovides` (mesa-libGLES-devel comes
    from libglvnd-devel, for example). Arch: `pacman -T` prints the unsatisfied ones.
    Commands count as present when they are on PATH."""
    names = [n for n in names if not (n in COMMANDS and shutil.which(COMMANDS[n]))]
    if family == 'fedora' and names and shutil.which('rpm'):
        result = subprocess.run(['rpm', '-q', '--whatprovides', *names], capture_output=True, text=True)
        return [line.rsplit(' ', 1)[1] for line in result.stdout.splitlines()
                if line.startswith('no package provides ')]
    if family == 'arch' and names and shutil.which('pacman'):
        result = subprocess.run(['pacman', '-T', *names], capture_output=True, text=True)
        return result.stdout.split()
    if family == 'debian' and names and shutil.which('dpkg-query'):
        result = subprocess.run(['dpkg-query', '-W', '-f=${db:Status-Abbrev} ${Package}\n', *names],
                                capture_output=True, text=True)
        installed = {line.split()[-1].split(':')[0] for line in result.stdout.splitlines() if line.startswith('ii')}
        return [n for n in names if n not in installed]
    return []


MIN_CLANG = 19  # clang 18 + GCC 13's libstdc++ (Ubuntu 24.04) has no usable std::expected


def clang_version(command):
    try:
        out = subprocess.run([command, '--version'], capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.TimeoutExpired):
        return 0
    match = re.search(r'clang version (\d+)', out)
    return int(match.group(1)) if match else 0


def find_clang():
    """(C compiler, C++ compiler) for the newest new-enough clang: plain `clang`, else the highest
    `clang-NN` on PATH (how Ubuntu installs extra versions). None if there is none."""
    versioned = {int(p.name[6:]) for folder in os.environ.get('PATH', '').split(os.pathsep) if folder
                 for p in Path(folder).glob('clang-[0-9]*') if p.name[6:].isdigit()}
    for c, cxx in [('clang', 'clang++')] + [(f'clang-{v}', f'clang++-{v}') for v in sorted(versioned, reverse=True)]:
        if shutil.which(c) and shutil.which(cxx) and clang_version(c) >= MIN_CLANG:
            return c, cxx
    return None


def has_library(soname):
    for folder in ('/usr/lib64', '/usr/lib/x86_64-linux-gnu', '/usr/lib', '/lib64'):
        if Path(folder, soname).exists():
            return True
    return False


def vulkan_gpus():
    """Names of the Vulkan GPUs, without software renderers; None if vulkaninfo is missing."""
    if not shutil.which('vulkaninfo'):
        return None
    try:
        out = subprocess.run(['vulkaninfo', '--summary'], capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.TimeoutExpired):
        return []
    names = [line.split('=', 1)[1].strip() for line in out.splitlines() if 'deviceName' in line]
    return [n for n in names if 'llvmpipe' not in n.lower() and 'lavapipe' not in n.lower()]


def check(folder=Path.cwd()):
    """Return (problems, warnings, fix_commands, gpus); problems block the build or the launcher."""
    problems, warnings, fixes = [], [], []
    family = distro()
    info = DISTROS.get(family)
    if info:
        missing = missing_packages(family, info['packages'])
        if missing:
            problems.append(f'{len(missing)} build package(s) missing: {" ".join(missing)}')
            fixes.append(f'{info["install"]} {" ".join(missing)}')
    else:
        if not IN_FLATPAK:  # the Flatpak brings its own tools (linux/flatpak/)
            warnings.append('Unrecognised distribution: the guide lists Fedora, Arch and Ubuntu packages; install the equivalents.')
        for tool in ('git', 'clang', 'cmake', 'ninja', 'pkg-config'):
            if not shutil.which(tool):
                problems.append(f'{tool} is not installed')
    if shutil.which('clang') and not find_clang():
        problems.append(f'clang {clang_version("clang")} is too old: the build needs clang {MIN_CLANG} or newer')
        fixes.append(info['newer_clang'] if info and 'newer_clang' in info
                     else f'install clang {MIN_CLANG} or newer')
    if not has_library('libssl.so.3'):
        problems.append('OpenSSL 3 (libssl.so.3) is missing; the launcher needs it')
        fixes.append(f'{info["install"]} {info["openssl3"]}' if info else 'install your distribution\'s OpenSSL 3 library')
    gpus = vulkan_gpus()
    if gpus is None:
        if not IN_FLATPAK:  # no vulkaninfo in the Flatpak; the game finds the GPU itself
            warnings.append('vulkaninfo not found, so the GPU was not checked (it comes with vulkan-tools)')
    elif not gpus:
        problems.append('no Vulkan GPU found: install your GPU\'s Vulkan driver (Mesa for AMD/Intel; '
                        'on Arch: vulkan-radeon or vulkan-intel)')
    if not (os.environ.get('WAYLAND_DISPLAY') or os.environ.get('DISPLAY')):
        warnings.append('no desktop session found: the last build step needs a screen connected')
    try:
        ram_gb = os.sysconf('SC_PAGE_SIZE') * os.sysconf('SC_PHYS_PAGES') / 2**30
        if ram_gb < MIN_RAM_GB:
            warnings.append(f'only {ram_gb:.0f} GB of RAM: the build may run out; close other programs '
                            '(or install with fewer build jobs)')
    except (ValueError, OSError):
        pass
    free_gb = shutil.disk_usage(folder).free / 2**30
    if free_gb < MIN_DISK_GB:
        problems.append(f'only {free_gb:.0f} GB free in {folder}; the build needs about {MIN_DISK_GB} GB')
    return problems, warnings, fixes, gpus


def main():
    problems, warnings, fixes, gpus = check()
    if gpus:
        print(f'OK  Vulkan GPU: {", ".join(gpus)}')
    for line in warnings:
        print(f'!   {line}')
    for line in problems:
        print(f'X   {line}')
    if fixes:
        print('\nTo fix, run:')
        for command in fixes:
            print(f'    {command}')
    print('\nReady to install.' if not problems else '\nNot ready yet: fix the X lines above, then run this again.')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())

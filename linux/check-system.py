#!/usr/bin/env python3
"""Check that this Linux PC can build and run FM4 Static Recomp, and print the fix for anything missing.

    python3 check-system.py          (from the launcher folder, or linux/check-system.py in the repo)

Exit code 0 = ready (warnings allowed), 1 = something needed is missing. linux/setup-linux.py runs
the same checks before building, and linux/package-launcher-linux.py ships this file beside the
launcher. Package names are Fedora's; on other distributions the library checks still run.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys

# Fedora packages the game build needs (also printed in the package README).
BUILD_PACKAGES = (
    'python3 git clang lld cmake ninja-build vulkan-loader-devel vulkan-headers vulkan-tools '
    'libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel wayland-devel '
    'libxkbcommon-devel mesa-libGL-devel libXScrnSaver-devel libXfixes-devel libXtst-devel '
    'alsa-lib-devel pulseaudio-libs-devel pipewire-devel pipewire-jack-audio-connection-kit-devel '
    'dbus-devel ibus-devel systemd-devel mesa-libEGL-devel mesa-libGLES-devel mesa-libgbm-devel '
    'libdrm-devel wayland-protocols-devel libdecor-devel libusb1-devel').split()
# The launcher's built-in .NET 8 loads OpenSSL 3; Fedora 45+ ships OpenSSL 4 (openssl3-libs adds 3).
OPENSSL3 = "'libssl.so.3()(64bit)'"
MIN_RAM_GB = 12    # the compile peaked at about 7 GB free-RAM use; leave room for the desktop
MIN_DISK_GB = 20   # SDK + generated code + build: about 11 GB on the Fedora 45 test laptop


def is_fedora():
    try:
        fields = dict(line.split('=', 1) for line in Path('/etc/os-release').read_text().splitlines() if '=' in line)
    except OSError:
        return False
    ids = (fields.get('ID', '') + ' ' + fields.get('ID_LIKE', '')).replace('"', '')
    return 'fedora' in ids.split()


# Packages that are really "this command exists" (Fedora may install e.g. git-core instead of git).
COMMANDS = {'python3': 'python3', 'git': 'git', 'clang': 'clang', 'lld': 'ld.lld', 'cmake': 'cmake',
            'ninja-build': 'ninja', 'vulkan-tools': 'vulkaninfo'}


def missing_rpms(names):
    """Names nothing installed provides (`rpm -q --whatprovides`: mesa-libGLES-devel comes from
    libglvnd-devel, for example). Commands count as present when they are on PATH."""
    names = [n for n in names if not (n in COMMANDS and shutil.which(COMMANDS[n]))]
    if not names or not shutil.which('rpm'):
        return []
    result = subprocess.run(['rpm', '-q', '--whatprovides', *names], capture_output=True, text=True)
    return [line.rsplit(' ', 1)[1] for line in result.stdout.splitlines()
            if line.startswith('no package provides ')]


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
    fedora = is_fedora()
    if fedora:
        missing = missing_rpms(BUILD_PACKAGES)
        if missing:
            problems.append(f'{len(missing)} build package(s) missing: {" ".join(missing)}')
            fixes.append('sudo dnf install ' + ' '.join(missing))
    else:
        warnings.append('Not Fedora: package names in the guide are Fedora\'s; install the equivalents.')
        for tool in ('git', 'clang', 'cmake', 'ninja'):
            if not shutil.which(tool):
                problems.append(f'{tool} is not installed')
    if not has_library('libssl.so.3'):
        problems.append('OpenSSL 3 (libssl.so.3) is missing; the launcher needs it')
        fixes.append(f'sudo dnf install {OPENSSL3}' if fedora else 'install your distribution\'s OpenSSL 3 library')
    gpus = vulkan_gpus()
    if gpus is None:
        warnings.append('vulkaninfo not found, so the GPU was not checked (it comes with vulkan-tools)')
    elif not gpus:
        problems.append('no Vulkan GPU found: install your GPU\'s Vulkan driver (Mesa for AMD/Intel)')
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

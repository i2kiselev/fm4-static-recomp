# Copyright (c) 2026, i2kiselev
# BSD 3-Clause License: see LICENSE in the repository root.
"""Prepare the release SDK, with a temporary Mac candidate override during review."""
import hashlib
import json
from pathlib import Path
import subprocess


def git(source, *arguments, capture=False):
    command = ['git', '-c', 'core.symlinks=true', '-C', str(source), *map(str, arguments)]
    if not capture:
        print('+ ' + ' '.join(command), flush=True)
    result = subprocess.run(command, check=True, text=True, capture_output=capture)
    return result.stdout.strip() if capture else None


def prepare_sdk(root, external=None):
    root = Path(root).resolve()
    mac = json.loads((root / 'config/macos-toolchain.json').read_text())
    # Retire this override once the SDK PR is merged and the maintainer sets the
    # shared release pin. Normal Mac builds then use exactly the Windows revision.
    config = mac.get('sdk') or json.loads((root / 'config/release-toolchain.json').read_text())['sdk']
    revision = config['revision']
    if len(revision) != 40 or any(c not in '0123456789abcdef' for c in revision):
        raise ValueError('The SDK revision must be a full Git commit hash')
    source = Path(external).expanduser().resolve() if external else root / '.local/shiftglue-sdk-macos' / revision
    if not external:
        source.mkdir(parents=True, exist_ok=True)
        if not (source / '.git').exists():
            if any(source.iterdir()):
                raise ValueError(f'Managed SDK folder is not empty and is not a Git checkout: {source}')
            git(source, 'init')
            git(source, 'remote', 'add', 'origin', config['repository'])
        if git(source, 'remote', 'get-url', 'origin', capture=True) != config['repository']:
            raise ValueError(f'Managed SDK origin differs from the release pin: {source}')
        head = subprocess.run(['git', '-C', str(source), 'rev-parse', '--verify', 'HEAD'],
                              text=True, capture_output=True)
        if head.returncode:
            git(source, 'fetch', '--depth=1', 'origin', revision)
            git(source, 'checkout', '--detach', 'FETCH_HEAD')
        actual = git(source, 'rev-parse', 'HEAD', capture=True)
        if actual != revision:
            raise ValueError(f'Managed SDK revision mismatch: expected {revision}, found {actual}')
        git(source, 'submodule', 'update', '--init', '--recursive', '--depth=1')
    if not (source / 'CMakeLists.txt').is_file():
        raise ValueError(f'ShiftGlue source is missing at {source}')
    actual = git(source, 'rev-parse', 'HEAD', capture=True)
    if actual != revision:
        raise ValueError(f'SDK revision mismatch: expected {revision}, found {actual}. '
                         'Use the pinned checkout or remove the development SDK override.')
    submodules = git(source, 'submodule', 'status', '--recursive', capture=True)
    if any(line.startswith(('-', '+', 'U')) for line in submodules.splitlines()):
        raise ValueError(f'SDK submodules are missing or at different revisions: {source}')
    diff = git(source, 'diff', '--binary', '--ignore-submodules=dirty', 'HEAD', capture=True)
    if diff and not external:
        raise ValueError('Managed SDK has local changes; preserve them and use a fresh checkout')
    identity = dict(repository=config['repository'], revision=actual, external=bool(external),
                    submodules=submodules, working_diff_sha256=hashlib.sha256(diff.encode()).hexdigest())
    return source, identity

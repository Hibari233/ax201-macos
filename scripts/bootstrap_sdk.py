#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fetch the pinned SDK. Never reset or overwrite an existing checkout."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    pin = json.loads((ROOT / 'dependencies.lock.json').read_text())['MacKernelSDK']
    target = ROOT / 'MacKernelSDK'
    if not target.exists():
        subprocess.run(['git', 'clone', '--filter=blob:none', '--no-checkout',
                        pin['url'], str(target)], check=True)
        subprocess.run(['git', '-C', str(target), 'checkout', '--detach', pin['commit']], check=True)
    head = subprocess.check_output(['git', '-C', str(target), 'rev-parse', 'HEAD'], text=True).strip()
    changes = subprocess.check_output(['git', '-C', str(target), 'status', '--porcelain',
                                       '--untracked-files=all'], text=True).strip()
    if head != pin['commit'] or changes:
        raise SystemExit('Existing SDK is not the clean pinned version; left unchanged. '
                         'Move your checkout aside before running this script again.')
    print('MacKernelSDK pinned at ' + head)


if __name__ == '__main__':
    main()

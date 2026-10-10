#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build (but do not run) the x86_64 macOS diagnostic client."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    if sys.platform != 'darwin':
        raise SystemExit('This client requires the macOS SDK and IOKit.')
    out = ROOT / 'build' / 'tools'
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run(['xcrun', 'clang++', '-std=c++14', '-O2', '-arch', 'x86_64',
                    '-mmacosx-version-min=15.2', '-I', str(ROOT / 'include'),
                    str(ROOT / 'tools/awdlctl.cpp'), '-framework', 'IOKit',
                    '-framework', 'CoreFoundation', '-o', str(out / 'awdlctl')], check=True)
    print('Built build/tools/awdlctl; hardware diagnostics were not run.')


if __name__ == '__main__':
    main()

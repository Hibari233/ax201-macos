#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run offline tests without loading a kext or accessing Wi-Fi hardware."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-sanitizers', action='store_true')
    args = parser.parse_args()
    out = ROOT / 'build' / 'tests'
    out.mkdir(parents=True, exist_ok=True)
    compiler = shlex.split(os.environ.get('CXX', 'clang++'))
    flags = ['-std=c++14', '-O1', '-g', '-Wall', '-Wextra', '-I', str(ROOT / 'include')]
    if not args.no_sanitizers:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
    env = {**os.environ, 'PYTHONDONTWRITEBYTECODE': '1'}
    for name in ('parse_test', 'schedule_test'):
        binary = out / name
        subprocess.run([*compiler, *flags, str(ROOT / 'tests' / 'awdl' / (name + '.cpp')),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, env=env)
    subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s',
                    str(ROOT / 'tests' / 'awdl'), '-p', 'inspect_test.py', '-v'], check=True, env=env)
    print('Offline checks passed. No hardware, native ABI, WPA3 or AirDrop claim.', flush=True)


if __name__ == '__main__':
    main()

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
    parser.add_argument('--sanitizers', choices=('address,undefined', 'undefined', 'none'), default='address,undefined')
    parser.add_argument('--timeout-seconds', type=int, default=60)
    args = parser.parse_args()
    if args.timeout_seconds < 1:
        parser.error('--timeout-seconds must be positive')
    sanitizers = 'none' if args.no_sanitizers else args.sanitizers
    out = ROOT / 'build' / 'tests'
    out.mkdir(parents=True, exist_ok=True)
    compiler = shlex.split(os.environ.get('CXX', 'clang++'))
    flags = ['-std=c++14', '-O1', '-g', '-Wall', '-Wextra', '-I', str(ROOT / 'include')]
    if sanitizers != 'none':
        flags += ['-fsanitize=' + sanitizers, '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
    env = {**os.environ, 'PYTHONDONTWRITEBYTECODE': '1'}
    for name in ('parse_test', 'schedule_test', 'data_test', 'echo_test', 'native_frame_test'):
        binary = out / name
        print(f'Compile {name} (sanitizers: {sanitizers})', flush=True)
        subprocess.run([*compiler, *flags, str(ROOT / 'tests' / 'awdl' / (name + '.cpp')),
                        '-o', str(binary)], check=True)
        print(f'Run {name}', flush=True)
        try:
            subprocess.run([str(binary)], check=True, env=env, timeout=args.timeout_seconds)
        except subprocess.TimeoutExpired:
            raise SystemExit(f'{name} timed out; this is not a passing test result. '
                             'Check the sanitizer runtime or the test before retrying.')
    subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s',
                    str(ROOT / 'tests' / 'awdl'), '-p', 'inspect_test.py', '-v'],
                   check=True, env=env, timeout=args.timeout_seconds)
    print(f'Offline checks passed (sanitizers: {sanitizers}). '
          'No hardware, native ABI, WPA3 or AirDrop claim.', flush=True)


if __name__ == '__main__':
    main()

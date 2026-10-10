#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reproduce AirportItlwm-Sequoia with the CLT, without changing Xcode.

Source selection comes from the checked-out Xcode project. No driver installation.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import plistlib
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build' / 'wifi'

def run(args, **kw):
    return subprocess.run([str(x) for x in args], check=True, **kw)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--one')
    ap.add_argument('--variant', choices=('wifi', 'awdl'), default='wifi')
    ap.add_argument('--awdl', action='store_true', help='Build the experimental AWDL radio diagnostic client')
    ap.add_argument('--native-abi-test', action='store_true', help='Compile the Sequoia AWDL ABI fixture only; never link a driver')
    ap.add_argument('--out', type=Path)
    args = ap.parse_args()
    if sys.platform != 'darwin':
        ap.error('Building the kext requires macOS and Apple Command Line Tools or Xcode.')
    if args.jobs < 1:
        ap.error('--jobs must be positive')
    args.awdl = args.awdl or args.variant == 'awdl'
    variant = 'awdl' if args.awdl else 'wifi'
    global OUT
    OUT = args.out.resolve() if args.out else ROOT / 'build' / variant
    if OUT == ROOT or OUT in ROOT.parents:
        ap.error('--out must not be the source root or one of its ancestors')
    OUT.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text())
    sdk = ROOT / 'MacKernelSDK'
    if not (sdk / '.git').exists():
        ap.error('Missing MacKernelSDK; run python3 scripts/bootstrap_sdk.py first.')
    sdk_commit = subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip()
    sdk_dirty = bool(subprocess.check_output(['git', '-C', str(sdk), 'status', '--porcelain', '--untracked-files=all'], text=True).strip())
    if sdk_commit != lock['MacKernelSDK']['commit'] or sdk_dirty:
        ap.error('MacKernelSDK must be a clean checkout of the pinned commit.')
    source_commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    source_dirty = bool(subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain', '--untracked-files=all'], text=True).strip())
    revision = source_commit[:12] + ('-dirty' if source_dirty else '')
    project = json.loads(subprocess.check_output(['plutil', '-convert', 'json', '-o', '-', str(ROOT / 'itlwm.xcodeproj/project.pbxproj')]))
    objects = project['objects']
    versions = {o.get('buildSettings', {}).get('MODULE_VERSION') for o in objects.values()}
    versions.discard(None)
    if len(versions) != 1:
        ap.error('Expected one MODULE_VERSION in the Xcode project')
    version = versions.pop()
    paths = {}
    def walk(ref, parent=Path()):
        obj = objects[ref]
        base = Path() if obj.get('sourceTree') == 'SOURCE_ROOT' else parent
        path = base / obj.get('path', '')
        if obj['isa'] == 'PBXFileReference':
            paths[ref] = path
        for child in obj.get('children', []):
            walk(child, path)
    walk(objects[project['rootObject']]['mainGroup'])
    target = next(v for v in objects.values() if v.get('isa') == 'PBXNativeTarget' and v.get('name') == 'AirportItlwm-Sequoia')
    sources = []
    for phase in target['buildPhases']:
        if objects[phase]['isa'] != 'PBXSourcesBuildPhase':
            continue
        for ref in objects[phase]['files']:
            file_ref = objects[ref].get('fileRef')
            if not file_ref:
                continue  # Empty upstream PBXBuildFile.
            path = paths.get(file_ref, Path(objects[file_ref].get('path', '')))
            sources.append(ROOT / path)
    if args.awdl:
        sources += [ROOT / 'AirportItlwm/AWDL/ItlAwdlUserClient.cpp',
                    ROOT / 'AirportItlwm/AWDL/ItlAwdlNativeInterface.cpp',
                    ROOT / 'itlwm/hal_iwx/ItlIwxAWDL.cpp']
    # Regenerate from this checkout, rather than reusing a stale ignored file.
    if not args.native_abi_test:
        run([sys.executable, ROOT / 'scripts/zlib_compress_fw.py', ROOT / 'include/FwBinary.cpp', ROOT / 'itlwm/firmware'])
    clang = Path(subprocess.check_output(['xcrun', '--find', 'clang++'], text=True).strip())
    resource = subprocess.check_output([str(clang), '-print-resource-dir'], text=True).strip()
    flags = ['-target', 'x86_64-apple-macos15.2', '-std=gnu++14', '-x', 'c++', '-O1', '-g',
             '-mkernel', '-fapple-kext', '-fno-exceptions', '-fno-rtti', '-fno-common',
             '-fno-builtin', '-mno-red-zone', '-mno-sse', '-mno-mmx', '-nostdinc',
             '-DKERNEL', '-DKERNEL_PRIVATE', '-DDRIVER_PRIVATE', '-DKPI_PRIVATE',
             '-DAIRPORT_WCL', '-DAIRPORT_VTD', '-DAIRPORT', '-D__IO80211_TARGET=__MAC_15_2',
             '-D__PRIVATE_SPI__', '-DIO80211FAMILY_V2', '-DIEEE80211_STA_ONLY',
             f'-DITLWM_VERSION="{version}"', f'-DGIT_COMMIT="{revision}"',
             f'-fdebug-prefix-map={ROOT}=.', f'-ffile-prefix-map={ROOT}=.',
             '-Wno-deprecated-register', '-Wno-deprecated-declarations', '-Wno-inconsistent-missing-override',
             '-isystem', sdk / 'Headers', '-isystem', Path(resource) / 'include']
    for directory in ['itl80211/openbsd', 'itl80211', 'include']:
        flags += ['-isystem', ROOT / directory]
    # Quoted project includes normally use Xcode's generated headermap.
    header_dirs = sorted({p.parent for name in ('AirportItlwm', 'include', 'itlwm', 'itl80211')
                          for p in (ROOT / name).rglob('*') if p.suffix in ('.h', '.hpp')})
    for directory in header_dirs:
        flags += ['-iquote', directory]
    flags += ['-include', ROOT / 'itlwm/PrivateSPI.pch']
    flags = list(map(str, flags))
    if args.awdl:
        flags += ['-DITLWM_AWDL_EXPERIMENTAL']
    if args.one:
        sources = [p for p in sources if args.one in str(p.relative_to(ROOT))]
        if not sources:
            sys.exit('No matching source')
    if args.native_abi_test:
        sources = [ROOT / 'tests/awdl/native_abi.cpp']
    commands = []
    object_files = []
    def compile_one(src):
        rel = src.relative_to(ROOT)
        obj = OUT / 'obj' / rel.with_suffix('.o')
        obj.parent.mkdir(parents=True, exist_ok=True)
        cmd = [str(clang), *flags, '-c', str(src), '-o', str(obj)]
        commands.append({'directory': str(ROOT), 'file': str(src), 'arguments': cmd})
        # Compiler inputs are intentionally rebuilt until dependency tracking is added.
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        obj.with_suffix('.log').write_text(result.stdout)
        print(('OK   ' if not result.returncode else 'FAIL ') + str(rel), flush=True)
        if result.returncode:
            print(result.stdout[-8000:], flush=True)
        return obj, result.returncode
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(compile_one, sources))
    (OUT / 'compile_commands.json').write_text(json.dumps(sorted(commands, key=lambda c: c['file']), indent=2) + '\n')
    if any(code for _, code in results):
        sys.exit(1)
    if args.one or args.native_abi_test:
        return
    object_files = [obj for obj, _ in results]
    module = OUT / 'module.cpp'
    module.write_text('''#include <mach/mach_types.h>
extern "C" {
extern kern_return_t _start(kmod_info_t *, void *);
extern kern_return_t _stop(kmod_info_t *, void *);
KMOD_EXPLICIT_DECL(com.zxystd.AirportItlwm, "@VERSION@", _start, _stop)
__attribute__((visibility("hidden"))) kmod_start_func_t *_realmain = 0;
__attribute__((visibility("hidden"))) kmod_stop_func_t *_antimain = 0;
__attribute__((visibility("hidden"))) int _kext_apple_cc = __APPLE_CC__;
}
'''.replace('@VERSION@', version))
    module_obj = OUT / 'module.o'
    run([clang, *flags, '-c', module, '-o', module_obj])
    bundle = OUT / 'AirportItlwm.kext'
    binary = bundle / 'Contents/MacOS/AirportItlwm'
    binary.parent.mkdir(parents=True, exist_ok=True)
    run([clang, '-target', 'x86_64-apple-macos15.2', '-nostdlib', '-Xlinker', '-kext',
         '-Xlinker', '-no_compact_unwind', '-Xlinker', '-no_fixup_chains',
         *object_files, module_obj, sdk / 'Library/x86_64/libkmod.a', '-o', binary])
    info = plistlib.loads((ROOT / 'AirportItlwm/WCL/Sequoia-Info.plist').read_bytes())
    values = {'DEVELOPMENT_LANGUAGE': 'en', 'EXECUTABLE_NAME': 'AirportItlwm',
              'PRODUCT_BUNDLE_IDENTIFIER': 'com.zxystd.AirportItlwm',
              'PRODUCT_NAME': 'AirportItlwm', 'PRODUCT_BUNDLE_PACKAGE_TYPE': 'KEXT', 'MODULE_VERSION': version}
    def expand(value):
        if isinstance(value, str):
            for key, text in values.items():
                value = value.replace('$(' + key + ')', text)
        elif isinstance(value, dict):
            value = {k: expand(v) for k, v in value.items()}
        elif isinstance(value, list):
            value = [expand(v) for v in value]
        return value
    info = expand(info)
    info['AX201SourceRevision'] = revision
    info['AX201BuildVariant'] = variant
    (bundle / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
    # Finder/iCloud may attach metadata to generated bundles in Documents.
    run(['/usr/bin/xattr', '-cr', bundle])
    run(['/usr/bin/codesign', '--force', '--sign', '-', '--timestamp=none', bundle])
    run(['/usr/bin/codesign', '--verify', '--strict', bundle])
    binary_sha = hashlib.sha256(binary.read_bytes()).hexdigest()
    manifest = {'source_commit': source_commit, 'source_dirty': source_dirty,
                'sdk_commit': sdk_commit, 'variant': variant, 'module_version': version,
                'target': 'x86_64-apple-macos15.2', 'source_count': len(sources),
                'compiler': subprocess.check_output([str(clang), '--version'], text=True).splitlines()[0],
                'python': sys.version.split()[0], 'zlib': zlib.ZLIB_RUNTIME_VERSION,
                'binary_sha256': binary_sha, 'signing': 'ad-hoc; not notarized',
                'runtime_validated': False, 'native_airdrop': False}
    (OUT / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Built {binary}: SHA256 {binary_sha}')

if __name__ == '__main__':
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare the compile-only AWDL fixture against the target's actual kernel ABI.

Usage: native_abi_test.py fixture.o BootKernelExtensions.kc methods.json
The KC stays local; the report contains names/offsets, never Apple machine code.
This verifies slot order and symbols, not unverified private argument layouts.
"""
import json
import struct
import sys
from pathlib import Path


class MachO:
    def __init__(self, data, offset=0):
        self.data = data
        self.sections = []
        self.segments = []
        self.symbols = []
        self.filesets = []
        if struct.unpack_from('<I', data, offset)[0] != 0xfeedfacf:
            raise ValueError('expected little-endian Mach-O 64')
        cursor = offset + 32
        symtab = None
        for _ in range(struct.unpack_from('<I', data, offset + 16)[0]):
            command, size = struct.unpack_from('<II', data, cursor)
            if command == 0x19:
                va, vs, fo, fs = struct.unpack_from('<QQQQ', data, cursor + 24)
                self.segments.append((va, vs, fo, fs))
                for i in range(struct.unpack_from('<I', data, cursor + 64)[0]):
                    s = cursor + 72 + 80 * i
                    addr, length, fileoff, _, relocoff, nreloc = struct.unpack_from('<QQIIII', data, s + 32)
                    self.sections.append((addr, length, fileoff, relocoff, nreloc))
            elif command == 2:
                symtab = struct.unpack_from('<IIII', data, cursor + 8)
            elif command == 0x80000035:
                self.filesets.append(struct.unpack_from('<Q', data, cursor + 16)[0])
            cursor += size
        if symtab:
            so, count, strings, _ = symtab
            for i in range(count):
                name, kind, section, desc, addr = struct.unpack_from('<IBBHQ', data, so + i * 16)
                start = strings + name
                symbol = data[start:data.index(0, start)].decode() if name else ''
                self.symbols.append((symbol, kind, section, addr))

    def bytes(self, addr, length):
        for va, vs, fo, fs in self.segments:
            if va <= addr and addr + length <= va + fs:
                return self.data[fo + addr - va:fo + addr - va + length]
        raise ValueError('address outside file-backed segments')


def verify(obj_path, kc_path, methods_path):
    obj = MachO(Path(obj_path).read_bytes())
    data = Path(kc_path).read_bytes()
    root = MachO(data)
    images = [root] + [MachO(data, off) for off in root.filesets]
    symbols = {name: addr for image in images for name, kind, section, addr in image.symbols if addr}
    native_name = '__ZTV19IO80211AWDLProtocol'
    native = next(image for image in images if any(s[0] == native_name and s[3] for s in image.symbols))
    native_addr = symbols[native_name]
    fixture_name = '__ZTV18SequoiaAwdlAbiTest'
    _, _, secnum, obj_addr = next(s for s in obj.symbols if s[0] == fixture_name)
    secaddr, _, fileoff, roff, nreloc = obj.sections[secnum - 1]
    relocations = {}
    by_addr = {s[3]: s[0] for s in obj.symbols if s[1] & 0x0e == 0x0e}
    for i in range(nreloc):
        addr, bits = struct.unpack_from('<II', obj.data, roff + i * 8)
        if not obj_addr - secaddr <= addr < obj_addr - secaddr + 0x11a8:
            continue
        if bits & (1 << 27):
            name = obj.symbols[bits & 0xffffff][0]
        else:
            target = struct.unpack_from('<Q', obj.data, fileoff + addr)[0]
            name = by_addr.get(target, '<unresolved local>')
        relocations[secaddr + addr - obj_addr] = name
    expected = {int(m['vptr_offset'], 16): m['names'][0].replace(
        '30AppleBCMWLANProximityInterface', '18SequoiaAwdlAbiTest')
        for m in json.loads(Path(methods_path).read_text())}
    mismatches = []
    checked = 0
    for offset in range(0, 0x1198, 8):
        name = relocations.get(offset + 16, '<missing relocation>')
        raw = struct.unpack('<Q', native.bytes(native_addr + offset + 16, 8))[0]
        target = 0xffffff8000100000 + (raw & 0x3fffffff) if raw else 0
        if offset in (0, 8, 0x38):  # Fixture constructors and its own metaclass.
            continue
        if offset in expected:
            ok = name == expected[offset] and target == symbols.get('___cxa_pure_virtual')
        elif offset == 0xcf8:  # Native pure setMacAddress, implemented only in fixture.
            ok = name == '__ZN18SequoiaAwdlAbiTest13setMacAddressER10ether_addr' and target == symbols.get('___cxa_pure_virtual')
        else:
            ok = target == symbols.get(name)
        checked += 1
        if not ok:
            mismatches.append({'vptr_offset': hex(offset), 'compiled': name,
                               'native_target': hex(target), 'resolved_target': hex(symbols.get(name, 0))})
    return {'target': 'Sequoia 15.8.1 / 24H32 x86_64', 'slots_checked': checked,
            'backend_methods_checked': len(expected), 'mismatches': mismatches,
            'scope': 'symbol/slot layout only; no runtime interface or AirDrop claim'}


if __name__ == '__main__':
    report = verify(*sys.argv[1:])
    print(json.dumps(report, indent=2))
    raise SystemExit(bool(report['mismatches']))

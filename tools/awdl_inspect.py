#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Inspect awdlctl JSONL offline. No device names, MACs or service payloads in output.

This is a protocol diagnostic, not an AirDrop implementation. Field offsets are
from the published AWDL wire format (Wireshark's packet-awdl dissector / OWL).
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct
import zlib


def u16(data, offset):
    return struct.unpack_from('<H', data, offset)[0]


def channel_sequence(data):
    if len(data) < 6:
        raise ValueError('truncated channel-sequence header')
    count, encoding, duplicate, step = data[:4]
    count += 1
    width = {0: 1, 1: 2, 3: 2}.get(encoding)
    if width is None:
        return {'encoding': encoding, 'unsupported_encoding': True}
    if len(data) < 6 + count * width:
        raise ValueError('truncated channel sequence')
    channels = [data[6 + i * width + (encoding == 1)] for i in range(count)]
    result = {'encoding': encoding, 'channels': channels, 'step_count': step,
              'duplicate_count': duplicate, 'fill_channel': u16(data, 4)}
    if encoding == 3:
        result['operating_classes'] = [data[7 + i * 2] for i in range(count)]
    return result


def parse_frame(data, with_fcs=False):
    if not 40 <= len(data) <= (2052 if with_fcs else 2048):
        raise ValueError('invalid frame length')
    if with_fcs:
        if len(data) < 44 or zlib.crc32(data[:-4]) != int.from_bytes(data[-4:], 'little'):
            raise ValueError('incorrect FCS')
        data = data[:-4]
    if data[0] != 0xd0 or data[1] & 0xc3 or data[24:30] != bytes.fromhex('7f0017f20810'):
        raise ValueError('not an unprotected AWDL action')
    if data[30] not in (0, 3) or data[31] != 0:
        raise ValueError('unsupported AWDL action format')
    result = {'subtype': {0: 'PSF', 3: 'MIF'}[data[30]], 'length': len(data), 'tlvs': []}
    cursor = 40
    while cursor < len(data):
        if len(data) - cursor < 3:
            raise ValueError('truncated TLV header')
        kind, length = data[cursor], u16(data, cursor + 1)
        cursor += 3
        if length > len(data) - cursor:
            raise ValueError('truncated TLV value')
        value = data[cursor:cursor + length]
        cursor += length
        result['tlvs'].append({'type': kind, 'length': length})
        if kind == 4:
            if length < 33:
                raise ValueError('truncated synchronization parameters')
            result['sync'] = {
                'next_aw_channel': value[0], 'tx_down_counter_tu': u16(value, 1),
                'master_channel': value[3], 'aw_period_tu': u16(value, 5),
                'af_period_tu': u16(value, 7), 'aw_extension_tu': u16(value, 11),
                'presence_mode': value[27], 'next_aw_sequence': u16(value, 29),
            }
            if length > 33:
                result['sync']['sequence'] = channel_sequence(value[33:])
        elif kind == 18:
            result['sequence'] = channel_sequence(value)
        elif kind == 21 and length >= 2:
            result['protocol_version'] = f'{value[0] >> 4}.{value[0] & 15}'
            result['device_class'] = value[1]
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('jsonl', type=Path)
    ap.add_argument('--with-fcs', action='store_true', help='For the pre-revision-4 rejected-frame diagnostic only')
    args = ap.parse_args()
    counts = Counter()
    first = {}
    final_status = None
    errors = []
    for line_number, line in enumerate(args.jsonl.read_text().splitlines(), 1):
        try:
            row = json.loads(line)
            if 'hex' not in row:
                if 'session' in row:
                    final_status = row
                continue
            decoded = parse_frame(bytes.fromhex(row['hex']), args.with_fcs)
            key = f"channel_{row['channel']}_{decoded['subtype']}"
            counts[key] += 1
            first.setdefault(key, decoded)
        except (ValueError, KeyError, struct.error) as error:
            errors.append({'line': line_number, 'error': str(error)})
    print(json.dumps({'counts': dict(counts), 'first_by_type': first,
                      'status': final_status, 'errors': errors}, indent=2))
    return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())

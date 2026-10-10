#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Summarize a listen-timed capture without MACs, names or service contents."""
import argparse
import json
import math
from collections import Counter
from pathlib import Path


def delta32(newer, older):
    value = (newer - older) & 0xffffffff
    return value if value < 0x80000000 else value - 0x100000000


def distribution(values):
    if not values:
        return None
    absolute = sorted(abs(v) for v in values)
    return {'samples': len(values), 'min_us': min(values), 'max_us': max(values),
            'median_abs_us': absolute[len(absolute) // 2],
            'p95_abs_us': absolute[min(len(absolute) - 1, math.ceil(len(absolute) * .95) - 1)],
            'rms_us': round(math.sqrt(sum(v * v for v in values) / len(values)), 2)}


def report(path):
    rows = [json.loads(line) for line in Path(path).read_text().splitlines() if line.strip()]
    frames = [r for r in rows if r.get('frame')]
    phase, host_phase, delivery = [], [], []
    previous = None
    for row in frames:
        if 'phase_error_us' not in row:
            continue
        if previous and not row['schedule_reset']:
            lag_change = row['ts_us'] - previous['ts_us'] - delta32(row['gp2_us'], previous['gp2_us'])
            phase.append(row['phase_error_us'])
            host_phase.append(row['phase_error_us'] + lag_change)
            delivery.append(lag_change)
        previous = row
    return {'frames': len(frames),
            'parse_results': dict(Counter(str(r.get('schedule_parse')) for r in frames)),
            'hardware_timestamp_frames': sum(bool(r.get('clock_flags', 0) & 1) for r in frames),
            'schedule_resets': sum(bool(r.get('schedule_reset')) for r in frames),
            'schedule_outliers': sum(bool(r.get('schedule_outlier')) for r in frames),
            'longest_coherent_run': max((r.get('coherent_samples', 0) for r in frames), default=0),
            'hardware_phase_error': distribution(phase),
            'host_timestamp_phase_error_for_comparison': distribution(host_phase),
            'cpu_delivery_lag_change': distribution(delivery),
            'final_radio_status': next((r for r in reversed(rows) if 'session' in r), None),
            'limits': 'Passive prediction only; neither scheduled TX nor native AirDrop tested.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture')
    args = parser.parse_args()
    print(json.dumps(report(args.capture), indent=2))

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Summarize bounded radio experiments without publishing raw peer payloads."""
import collections
import json
import math
import sys
from pathlib import Path


def delta(new, old):
    value = (new - old) & 0xffffffff
    return value if value <= 0x7fffffff else value - 0x100000000


def stats(values):
    values = sorted(values)
    if not values:
        return {"samples": 0}
    return {"samples": len(values), "min_us": values[0], "max_us": values[-1],
            "median_us": values[len(values) // 2],
            "p95_us": values[math.ceil(len(values) * 0.95) - 1]}


def report(records):
    frames = [r for r in records if r.get("frame")]
    clocks = [r for r in records if r.get("clock")]
    events = [r for r in records if r.get("run_event")]
    retunes = [e for e in events if e["operation"] == 3 and not e["result"]]
    intervals = []
    for old, new in zip(clocks, clocks[1:]):
        host = ((new["host_begin_us"] + new["host_end_us"]) -
                (old["host_begin_us"] + old["host_end_us"])) / 2
        intervals.append(round(delta(new["gp2_us"], old["gp2_us"]) - host, 1))
    verified_hops = 0
    first_rx = []
    for i, event in enumerate(retunes):
        limit = delta(retunes[i + 1]["wake_gp2_us"], event["finish_gp2_us"]) if i + 1 < len(retunes) else 1000000
        gaps = [delta(f["gp2_us"], event["finish_gp2_us"]) for f in frames
                if f["channel"] == event["channel"] and (f.get("clock_flags", 0) & 1)]
        gaps = [g for g in gaps if 0 <= g < limit]
        if gaps:
            verified_hops += 1
            first_rx.append(min(gaps))
    result = {
        "peer_frames_by_channel": dict(collections.Counter(str(f["channel"]) for f in frames)),
        "live_clock": {"samples": len(clocks),
                       "qualified_samples": sum(c["flags"] == 7 for c in clocks),
                       "registers": sorted({hex(c["register"]) for c in clocks}),
                       "phy_command_versions": sorted({c["phy_command_version"] for c in clocks}),
                       "read_duration": stats([c["host_end_us"] - c["host_begin_us"] for c in clocks]),
                       "gp2_minus_host_interval": stats(intervals)},
        "operations": {},
        "retunes_followed_by_peer_rx": verified_hops,
        "first_peer_rx_after_retune": stats(first_rx),
        "limits": "Timer/probe events measure software wake/submission, not exact RF transmission time. Retune durations measure command completion. Native AWDL discovery and AirDrop file transfer remain unimplemented."
    }
    for kind, name in [(1, "timer_wake"), (2, "fixed_probe_submission"), (3, "passive_retune")]:
        selected = [e for e in events if e["operation"] == kind]
        if selected:
            result["operations"][name] = {
                "count": len(selected), "succeeded": sum(e["result"] == 0 for e in selected),
                "errors": dict(collections.Counter(hex(e["result"]) for e in selected if e["result"])),
                "wake_lateness": stats([e["lateness_us"] for e in selected]),
                "command_duration": stats([e["duration_us"] for e in selected if not e["result"]])}
    for tag in ["run_status", "tx_status", "regulatory_status", "clock_summary"]:
        values = [r for r in records if r.get(tag)]
        if values:
            result[tag] = values[-1]
    statuses = [r for r in records if "session" in r]
    if statuses:
        result["final_radio_status"] = statuses[-1]
    return result


if __name__ == "__main__":
    rows = [json.loads(line) for line in Path(sys.argv[1]).read_text().splitlines() if line.strip()]
    print(json.dumps(report(rows), indent=2))

// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlSchedule.h>
#include <AWDL/AwdlClock.h>
#include <AWDL/AwdlRegulatory.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
using namespace ItlAwdl;
static void put16(std::vector<uint8_t> &b, size_t p, uint16_t v) {
    b[p] = uint8_t(v); b[p + 1] = uint8_t(v >> 8);
}
static std::vector<uint8_t> frame() {
    std::vector<uint8_t> b(114, 0);
    b[0] = 0xd0; b[24] = 127; b[26] = 0x17; b[27] = 0xf2; b[28] = 8; b[29] = 0x10;
    b[40] = 4; put16(b, 41, 33); // Sync starts at 43.
    put16(b, 44, 64); put16(b, 48, 16); b[64] = 2; b[70] = 4;
    b[76] = 18; put16(b, 77, 35); // Simple 16-entry sequence + padding.
    b[79] = 15; b[80] = 0; b[82] = 3; put16(b, 83, 0xffff);
    for (size_t p = 85; p < 101; ++p) b[p] = 6;
    b[32] = 100; // 100 us firmware TX delay.
    return b;
}
int main() {
    auto b = frame(); SyncAnnouncement s = {};
    assert(parseSchedule(b.data(), b.size(), &s) == ScheduleResult::Valid);
    assert(s.aw_period_tu == 16 && s.presence == 4 && s.tx_delay_us == 100);
    assert(s.plan.channels[15] == 6 && s.plan.step_aw == 4);
    ScheduleTracker t; ScheduleSample a = {};
    assert(t.observe(s, 100000, 1000000, &a));
    assert(a.reset && a.coherent_samples == 1 && a.next_boundary_gp2_us == 165436 && a.next_counter == 4);
    s.down_counter_tu = 48; s.aw_counter = 1;
    assert(t.observe(s, 116384, 1018000, &a));
    assert(!a.reset && !a.outlier && a.error_us == 0 && a.coherent_samples == 2);
    assert(!t.observe(s, 116384, 1018001, &a)); // Duplicate timestamp.
    assert(!t.observe(s, 116383, 1018002, &a)); // Reordered RX.
    s.down_counter_tu = 32; s.aw_counter = 2;
    assert(t.observe(s, 132768, 1045000, &a)); // Host delivery jitter is irrelevant.
    assert(a.error_us == 0 && a.coherent_samples == 3);
    assert(t.observe(s, 142768, 1045002, &a));
    assert(a.outlier && a.error_us == 10000 && a.coherent_samples == 1);
    s.master[1] = 1;
    assert(t.observe(s, 150000, 1050000, &a) && a.reset);
    assert(t.observe(s, 0, 4050001, &a) && a.reset); // Radio restart / long gap.
    ScheduleTracker wrap;
    s.aw_counter = 65532; s.down_counter_tu = 64;
    assert(wrap.observe(s, 0xfffff000U, 1000000, &a));
    assert(a.next_counter == 0);
    s.aw_counter = 0;
    assert(wrap.observe(s, uint32_t(0xfffff000U + 65536U), 1065536, &a));
    assert(a.next_counter == 4 && a.error_us == 0 && !a.outlier);
    assert(clockDelta(3, 0xfffffffeU) == 5 && clockDelta(0xfffffffeU, 3) == -5);
    assert(counterDelta(0, 65532) == 4 && counterDelta(65532, 0) == -4);
    // Project onto alternating availability windows, and leave a full guard
    // on either side of a scheduled submission. Both clocks wrap unsigned.
    SyncAnnouncement plan = {}; auto fixture = frame();
    assert(parseSchedule(fixture.data(), fixture.size(), &plan) == ScheduleResult::Valid);
    plan.plan.channels[1] = 149;
    ScheduleTracker projected; ScheduleSlot slot = {};
    assert(projected.observe(plan, 100000, 1000000, &a));
    assert(!projected.project(100000, 100000, 1000000, &slot)); // Need three coherent samples.
    plan.down_counter_tu = 48; plan.aw_counter = 1;
    assert(projected.observe(plan, 116384, 1016384, &a));
    plan.down_counter_tu = 32; plan.aw_counter = 2;
    assert(projected.observe(plan, 132768, 1032768, &a));
    assert(projected.project(132868, 132868, 1032868, &slot));
    assert(slot.counter == 0 && slot.channel == 6 && slot.start_gp2_us == 99900 && slot.end_gp2_us == 165436);
    assert(projected.project(165435, 132868, 1032868, &slot) && slot.channel == 6);
    assert(projected.project(165436, 132868, 1032868, &slot) && slot.channel == 149 && slot.counter == 4);
    uint32_t target = 0;
    assert(projected.nextWindow(150000, 1050000, 6, 150000, &slot, &target) && target == 155000);
    assert(projected.nextWindow(160000, 1060000, 6, 160000, &slot, &target));
    assert(slot.counter == 8 && target == 239164); // Current slot too short; skip 149.
    assert(!projected.nextWindow(160000, 1060000, 44, 160000, &slot, &target));
    auto original_slot = slot;
    assert(!projected.project(132768 + 1000001, 132768 + 1000001, 1032768, &slot));
    assert(!projected.project(132768, 132768, 2032769, &slot));
    assert(!projected.project(132768 + 1000001, 132768, 1032768, &slot));
    assert(!projected.project(132767, 132768, 1032768, &slot));
    assert(!memcmp(&slot, &original_slot, sizeof(slot)));
    ScheduleTracker projected_wrap;
    plan.aw_counter = 65532; plan.down_counter_tu = 64;
    assert(projected_wrap.observe(plan, 0xfffff000U, 1000000, &a));
    plan.aw_counter = 65533; plan.down_counter_tu = 48;
    assert(projected_wrap.observe(plan, uint32_t(0xfffff000U + 16384), 1016384, &a));
    plan.aw_counter = 65534; plan.down_counter_tu = 32;
    assert(projected_wrap.observe(plan, uint32_t(0xfffff000U + 32768), 1032768, &a));
    assert(projected_wrap.project(61340, 28672, 1032768, &slot));
    assert(slot.counter == 0 && slot.channel == 6 && slot.start_gp2_us == 61340);
    ClockCorrelation live;
    assert(live.observe(100000, 1000000, 1000010, true, 99900, 1000000) == 5);
    assert(live.observe(110000, 1010000, 1010010, true, 99900, 1000000) == 5);
    assert(live.observe(120000, 1020000, 1020010, true, 99900, 1000000) == 7);
    assert(live.observe(120000, 1030000, 1030010, true, 99900, 1000000) == 1); // Stuck register fails rate and RX correlation.
    assert(!(live.observe(140000, 1040000, 1040010, true, 1, 1040000) & 4)); // Wrong clock domain.
    assert(live.observe(150000, 1050000, 1052001, true, 149900, 1050000) == 0); // Slow read.
    assert(live.observe(160000, 1060000, 1059999, true, 159900, 1060000) == 0);
    ClockCorrelation live_wrap;
    assert(live_wrap.observe(0xfffff000U, 1000000, 1000010, true, 0xffffeff0U, 1000000) == 5);
    assert(live_wrap.observe(0x1000U, 1008192, 1008202, true, 0xffffeff0U, 1000000) == 5);
    assert(live_wrap.observe(0x3000U, 1016384, 1016394, true, 0xffffeff0U, 1000000) == 7);
    assert(!(live_wrap.observe(0x3000U + 600000, 1616384, 1616394, false, 0, 0) & 2));
    assert(timerDelayUs(100) == 200 && timerDelayUs(3000) == 3000);
    assert(timerDelayUs(5000) == 2000 && timerDelayUs(100000) == 97000);
    assert(timerDelayUs(0xffffffffU) == 97000);
    for (uint32_t remaining = 200; remaining <= 1000000; remaining += 37) {
        auto delay = timerDelayUs(remaining);
        assert(delay >= 200 && delay <= remaining && delay <= 100000);
        if (remaining > 4000) assert(remaining - delay >= 3000);
    }
    assert(countryHintAllowed(0x434e, 5) && countryHintAllowed(0x5457, 5));
    for (uint8_t source : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(6), uint8_t(9), uint8_t(16), uint8_t(17), uint8_t(255)})
        assert(!countryHintAllowed(0x434e, source));
    for (uint16_t country : {uint16_t(0), uint16_t(0x3030), uint16_t(0x5a5a), uint16_t(0x636e), uint16_t(0xffff), uint16_t(0x0043)})
        assert(!countryHintAllowed(country, 5));
    assert(countryHintFresh(100, 100) && countryHintFresh(60000100, 100));
    assert(!countryHintFresh(60000101, 100) && !countryHintFresh(99, 100));
    assert(!countryHintFresh(100, 0) && !countryHintFresh(UINT64_MAX, 100));
    auto saved = s;
    for (size_t n = 0; n < 40; ++n) assert(parseSchedule(b.data(), n, &s) == ScheduleResult::Malformed);
    assert(!memcmp(&s, &saved, sizeof(s))); // Failed parse leaves output untouched.
    b = frame(); b[70] = 3;
    assert(parseSchedule(b.data(), b.size(), &s) == ScheduleResult::Unsupported);
    b = frame(); put16(b, 44, 65);
    assert(parseSchedule(b.data(), b.size(), &s) == ScheduleResult::Malformed);
    b = frame(); put16(b, 48, 0);
    assert(parseSchedule(b.data(), b.size(), &s) == ScheduleResult::Malformed);
    b = frame(); b[80] = 1;
    assert(parseSchedule(b.data(), b.size(), &s) == ScheduleResult::Unsupported);
    b = frame(); b[80] = 3; // Opclass encoding needs 32 bytes; fixture only has 29.
    assert(parseSchedule(b.data(), b.size(), &s) == ScheduleResult::Malformed);
    b = frame(); b.insert(b.end(), {4, 0, 0});
    assert(parseSchedule(b.data(), b.size(), &s) == ScheduleResult::Malformed);
    b = frame(); b[40] = 250;
    assert(parseSchedule(b.data(), b.size(), &s) == ScheduleResult::NoSync);
    uint32_t rng = 0x14700407;
    for (unsigned i = 0; i < 200000; ++i) {
        b = frame(); rng = rng * 1664525u + 1013904223u;
        size_t n = rng % 2100; b.resize(n);
        for (unsigned j = 0; j < 8 && n; ++j) {
            rng = rng * 1664525u + 1013904223u;
            b[rng % n] = uint8_t(rng >> 24);
        }
        if (parseSchedule(b.data(), b.size(), &s) == ScheduleResult::Valid) {
            ScheduleTracker test; assert(test.observe(s, rng, rng, &a));
        }
    }
    puts("AWDL schedule: projection, guarded windows, live clock correlation, phase, host jitter, wraps, restart, malformed bounds and 200000 mutation cases passed");
}

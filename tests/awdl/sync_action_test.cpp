// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlSyncAction.h>
#include <AWDL/AwdlNativeFrame.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

using namespace ItlAwdl;
int main() {
    SyncActionContext c = {{2, 1, 2, 3, 4, 5}, {2, 6, 7, 8, 9, 10}, 0,
                           0x123, 0x12345698, 0x12345678, true};
    SyncActionParameters s = {};
    s.nextChannel = s.masterChannel = 6; s.downCounterTu = 32;
    s.awPeriodTu = s.extensionTu = s.commonTu = 16;
    s.actionPeriodTu = 110; s.flags = 0x1800; s.presence = 4;
    s.minExtension = s.maxMulticastExtension = s.maxUnicastExtension = s.maxActionExtension = 3;
    memcpy(s.master, c.source, 6); s.counter = 0x1234; s.apAlignment = 0xabcd;
    s.plan.step_aw = 4;
    for (auto &channel : s.plan.channels) channel = 6;
    ActionTemplate extra;
    const uint8_t version[] = {21, 2, 0, 0x10, 1};
    assert(extra.replace(version, sizeof(version)));
    uint8_t out[SyncActionMaxLength + 8]; memset(out, 0xa5, sizeof(out));
    auto n = buildSyncAction(c, s, extra, out, sizeof(out));
    assert(n == 105);
    // Independent fixed wire specimen; padding and FCS absence are deliberate.
    const uint8_t header[] = {0xd0,0,0,0,2,6,7,8,9,10,2,1,2,3,4,5,0,0x25,0,0xff,0x94,0x73,
        0x30,0x12,0x7f,0,0x17,0xf2,8,0x10,0,0,0x98,0x56,0x34,0x12,0x78,0x56,0x34,0x12,
        4,57,0};
    const uint8_t fixed[] = {6,32,0,6,0,16,0,110,0,0,0x18,16,0,16,0,0,0,3,3,3,3,
        2,1,2,3,4,5,4,0,0x34,0x12,0xcd,0xab,15,0,0,3,0xff,0xff};
    assert(!memcmp(out, header, sizeof(header)) && !memcmp(out + 43, fixed, sizeof(fixed)));
    for (unsigned i = 82; i < 98; ++i) assert(out[i] == 6);
    assert(!out[98] && !out[99] && !memcmp(out + 100, version, sizeof(version)));
    for (size_t i = n; i < sizeof(out); ++i) assert(out[i] == 0xa5);
    ActionInfo action = {}; SyncAnnouncement sync = {};
    assert(parseAction(out, n, &action) == ParseResult::Valid && action.tlv_count == 2);
    assert(parseSchedule(out, n, &sync) == ScheduleResult::Valid);
    assert(sync.aw_period_tu == 16 && sync.down_counter_tu == 32 && sync.aw_counter == 0x1234);
    assert(sync.tx_delay_us == 32 && equalMac(sync.master, c.source) && equalChannelPlan(sync.plan, s.plan));
    NativeActionMetadata metadata = {}; NativeActionView view = {};
    NativeActionRxContext rx = {6, -50, 100000, true};
    assert(encodeNativeActionMetadata(c.source, rx, &metadata));
    assert(nativeActionView(out, n, metadata.bytes, sizeof(metadata), &view));

    auto rejected = [&](const SyncActionContext &bc, const SyncActionParameters &bs, const ActionTemplate &bt) {
        memset(out, 0xa5, sizeof(out));
        assert(!buildSyncAction(bc, bs, bt, out, sizeof(out)));
        for (auto byte : out) assert(byte == 0xa5);
    };
    for (size_t capacity = 0; capacity < n; ++capacity) {
        memset(out, 0xa5, sizeof(out));
        assert(!buildSyncAction(c, s, extra, out, capacity));
        for (auto byte : out) assert(byte == 0xa5);
    }
    assert(!buildSyncAction(c, s, extra, nullptr, sizeof(out)));
    auto bc = c; bc.clockValid = false; rejected(bc, s, extra);
    bc = c; bc.sequence = 0x1000; rejected(bc, s, extra);
    bc = c; bc.subtype = 1; rejected(bc, s, extra);
    bc = c; bc.source[0] = 1; rejected(bc, s, extra);
    bc = c; memset(bc.destination, 0, 6); rejected(bc, s, extra);
    bc = c; bc.destination[0] = 1; rejected(bc, s, extra);
    bc = c; memcpy(bc.destination, bc.source, 6); rejected(bc, s, extra);
    bc = c; bc.phyTimeUs = bc.targetTimeUs - 1; rejected(bc, s, extra);
    bc = c; bc.phyTimeUs = bc.targetTimeUs + 65536; rejected(bc, s, extra);
    auto bs = s; bs.presence = 0; rejected(c, bs, extra);
    bs = s; bs.presence = 3; rejected(c, bs, extra);
    bs = s; bs.awPeriodTu = 0; rejected(c, bs, extra);
    bs = s; bs.awPeriodTu = 1025; rejected(c, bs, extra);
    bs = s; bs.actionPeriodTu = 0; rejected(c, bs, extra);
    bs = s; bs.downCounterTu = 65; rejected(c, bs, extra);
    bs = s; bs.plan.step_aw = 5; rejected(c, bs, extra);
    bs = s; bs.plan.encoding = 1; rejected(c, bs, extra);
    bs = s; bs.plan.operating_classes[1] = 81; rejected(c, bs, extra);
    bs = s; bs.commonTu = 0; rejected(c, bs, extra);
    bs = s; bs.extensionTu = 65; rejected(c, bs, extra);
    bs = s; bs.remainingTu = 17; rejected(c, bs, extra);
    bs = s; memset(bs.master, 0, 6); rejected(c, bs, extra);
    // Embedded and supplementary channel announcements cannot disagree.
    uint8_t sequence[25] = {18, 22, 0, 15, 0, 0, 3, 0xff, 0xff};
    for (unsigned i = 9; i < sizeof(sequence); ++i) sequence[i] = 6;
    ActionTemplate seq;
    assert(seq.replace(sequence, sizeof(sequence)));
    assert(buildSyncAction(c, s, seq, out, sizeof(out)) == 125);
    sequence[9] = 44;
    assert(seq.replace(sequence, sizeof(sequence))); rejected(c, s, seq);
    uint8_t duplicates[50]; sequence[9] = 6;
    memcpy(duplicates, sequence, 25); memcpy(duplicates + 25, sequence, 25);
    assert(seq.replace(duplicates, sizeof(duplicates))); rejected(c, s, seq);
    const uint8_t injectedSync[] = {4, 0, 0};
    assert(seq.replace(injectedSync, sizeof(injectedSync))); rejected(c, s, seq);
    // Full-capacity supplementary TLV, opclass encoding, empty slots and wraps.
    uint8_t maximum[512] = {0xfe, 0xfd, 1};
    assert(extra.replace(maximum, sizeof(maximum)));
    s.plan.encoding = 3;
    for (auto &opclass : s.plan.operating_classes) opclass = 81;
    s.plan.channels[5] = s.plan.operating_classes[5] = 0;
    c.sequence = 0xfff; c.subtype = 3; memset(c.destination, 0xff, 6);
    c.targetTimeUs = 0xfffffff0; c.phyTimeUs = 0x10; s.counter = 0xffff;
    assert(buildSyncAction(c, s, extra, out, SyncActionMaxLength) == SyncActionMaxLength);
    assert(parseSchedule(out, SyncActionMaxLength, &sync) == ScheduleResult::Valid);
    assert(sync.tx_delay_us == 32 && sync.aw_counter == 0xffff && !sync.plan.channels[5]);
    assert(equalChannelPlan(sync.plan, s.plan));
    bs = s; bs.plan.operating_classes[5] = 81; rejected(c, bs, extra);
    bs = s; bs.plan.operating_classes[0] = 0; rejected(c, bs, extra);
    puts("Sync action encoding checks passed; no firmware TX or native discovery claim.");
}

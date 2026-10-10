// SPDX-License-Identifier: GPL-2.0-or-later
// AWDL v1 PSF/MIF wire encoding. Protocol references: Wireshark packet-awdl.c
// and Open Wireless Link frame.h. This codec never submits a radio packet.
#pragma once
#include "AwdlActionTemplate.h"
#include "AwdlNativeBssid.h"
#include "AwdlSchedule.h"

namespace ItlAwdl {
struct SyncActionParameters {
    uint8_t nextChannel, masterChannel, guardTime;
    uint16_t downCounterTu, awPeriodTu, actionPeriodTu, flags;
    uint16_t extensionTu, commonTu, remainingTu;
    uint8_t minExtension, maxMulticastExtension, maxUnicastExtension, maxActionExtension;
    uint8_t master[6], presence;
    uint16_t counter, apAlignment;
    ChannelPlan plan;
};
struct SyncActionContext {
    uint8_t source[6], destination[6], subtype;
    uint16_t sequence;
    // Caller supplies one coherent local hardware-clock snapshot. In
    // particular, downCounterTu describes targetTimeUs, not a later host read.
    // Encoding cannot account for a future firmware queue/submission delay.
    uint32_t phyTimeUs, targetTimeUs;
    bool clockValid;
};
constexpr size_t SyncActionMaxLength = 40 + 3 + 33 + 6 + 32 + 2 + ActionTemplate::Capacity;

inline void syncActionPut16(uint8_t *p, uint16_t value) {
    p[0] = uint8_t(value); p[1] = uint8_t(value >> 8);
}
inline void syncActionPut32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(value >> (8 * i));
}
inline bool syncActionDestination(const uint8_t *address) {
    if (unicastMac(address)) return true;
    for (unsigned i = 0; i < 6; ++i) if (address[i] != 0xff) return false;
    return true;
}
inline bool equalChannelPlan(const ChannelPlan &a, const ChannelPlan &b) {
    if (a.encoding != b.encoding || a.step_aw != b.step_aw) return false;
    for (unsigned i = 0; i < ScheduleLength; ++i)
        if (a.channels[i] != b.channels[i] ||
            a.operating_classes[i] != b.operating_classes[i]) return false;
    return true;
}

// Returns the complete MPDU length (without FCS), or zero without modifying
// output. A saved native template is supplementary: it cannot replace the
// generated sync TLV or silently advertise a conflicting channel plan.
// Channel/operating-class bytes are encoded, not permission to use them; the
// scheduler must independently validate the live firmware regulatory map.
inline size_t buildSyncAction(const SyncActionContext &context, const SyncActionParameters &sync,
                              const ActionTemplate &supplement, const NativeBssidConfiguration &network,
                              uint8_t *out, size_t capacity) {
    if (!out || !network.ready() || !context.clockValid || !unicastMac(context.source) ||
        !syncActionDestination(context.destination) || equalMac(context.source, context.destination) ||
        (context.subtype != 0 && context.subtype != 3) || context.sequence > 0xfff ||
        !unicastMac(sync.master) || !sync.awPeriodTu || sync.awPeriodTu > 1024 ||
        !sync.actionPeriodTu || !sync.presence || sync.presence > 16 ||
        (sync.presence & (sync.presence - 1)) || sync.plan.step_aw != sync.presence ||
        (sync.plan.encoding != 0 && sync.plan.encoding != 3)) return 0;
    const uint32_t periodTu = uint32_t(sync.awPeriodTu) * sync.presence;
    if (sync.downCounterTu > periodTu || !sync.extensionTu || !sync.commonTu ||
        sync.extensionTu > periodTu || sync.commonTu > periodTu || sync.remainingTu > sync.commonTu ||
        uint32_t(context.phyTimeUs - context.targetTimeUs) >= periodTu * TuUs) return 0;
    for (unsigned i = 0; i < ScheduleLength; ++i) {
        // A zero channel is an unavailable slot. Simple encoding has no
        // operating-class field; do not lose a caller-supplied value silently.
        if ((sync.plan.encoding == 0 && sync.plan.operating_classes[i]) ||
            (!sync.plan.channels[i] && sync.plan.operating_classes[i]) ||
            (sync.plan.encoding == 3 && sync.plan.channels[i] && !sync.plan.operating_classes[i])) return 0;
    }
    const size_t width = sync.plan.encoding == 3 ? 2 : 1;
    const size_t bodyLength = 33 + 6 + ScheduleLength * width + 2;
    const size_t frameLength = 40 + 3 + bodyLength + supplement.length();
    if (frameLength > capacity || frameLength > SyncActionMaxLength) return 0;
    bool hasSequence = false;
    for (size_t pos = 0; pos < supplement.length();) {
        const auto bytes = supplement.bytes();
        const auto kind = bytes[pos];
        const size_t length = le16(bytes + pos + 1);
        pos += 3; // ActionTemplate already validated the complete TLV sequence.
        if (kind == 4) return 0;
        if (kind == 18) {
            ChannelPlan advertised = {};
            if (hasSequence || parsePlan(bytes + pos, length, &advertised) != ScheduleResult::Valid ||
                !equalChannelPlan(sync.plan, advertised)) return 0;
            hasSequence = true;
        }
        pos += length;
    }
    // Build separately so all inputs may be read before a successful write;
    // failures and output/input overlap cannot leave a half-updated MPDU.
    uint8_t frame[SyncActionMaxLength] = {};
    frame[0] = 0xd0;
    const auto bssid = network.awdl();
    for (unsigned i = 0; i < 6; ++i) {
        frame[4 + i] = context.destination[i];
        frame[10 + i] = context.source[i];
        frame[16 + i] = bssid[i];
    }
    syncActionPut16(frame + 22, uint16_t(context.sequence << 4));
    frame[24] = 127; frame[26] = 0x17; frame[27] = 0xf2;
    frame[28] = 8; frame[29] = 0x10; frame[30] = context.subtype;
    syncActionPut32(frame + 32, context.phyTimeUs);
    syncActionPut32(frame + 36, context.targetTimeUs);
    frame[40] = 4; syncActionPut16(frame + 41, uint16_t(bodyLength));
    auto p = frame + 43;
    p[0] = sync.nextChannel;
    syncActionPut16(p + 1, sync.downCounterTu);
    p[3] = sync.masterChannel; p[4] = sync.guardTime;
    syncActionPut16(p + 5, sync.awPeriodTu);
    syncActionPut16(p + 7, sync.actionPeriodTu);
    syncActionPut16(p + 9, sync.flags);
    syncActionPut16(p + 11, sync.extensionTu);
    syncActionPut16(p + 13, sync.commonTu);
    syncActionPut16(p + 15, sync.remainingTu);
    p[17] = sync.minExtension; p[18] = sync.maxMulticastExtension;
    p[19] = sync.maxUnicastExtension; p[20] = sync.maxActionExtension;
    for (unsigned i = 0; i < 6; ++i) p[21 + i] = sync.master[i];
    p[27] = sync.presence;
    syncActionPut16(p + 29, sync.counter);
    syncActionPut16(p + 31, sync.apAlignment);
    p[33] = ScheduleLength - 1; p[34] = sync.plan.encoding;
    p[36] = uint8_t(sync.plan.step_aw - 1);
    syncActionPut16(p + 37, 0xffff);
    for (unsigned i = 0; i < ScheduleLength; ++i) {
        p[39 + i * width] = sync.plan.channels[i];
        if (width == 2) p[40 + i * width] = sync.plan.operating_classes[i];
    }
    const size_t prefix = 43 + bodyLength;
    for (size_t i = 0; i < supplement.length(); ++i) frame[prefix + i] = supplement.bytes()[i];
    for (size_t i = 0; i < frameLength; ++i) out[i] = frame[i];
    return frameLength;
}
}

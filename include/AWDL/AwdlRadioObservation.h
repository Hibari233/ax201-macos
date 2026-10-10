// SPDX-License-Identifier: GPL-2.0-or-later
// Numeric-only observations for a bounded native qualification. Received
// frames are not a CCA busy-time measurement and do not describe undecodable RF.
#pragma once
#include "AwdlSchedule.h"

namespace ItlAwdl {
struct PendingRxObservation {
    uint32_t frames = 0, bytes = 0, management = 0, control = 0, data = 0;
    uint32_t firstOffsetUs = 0, lastOffsetUs = 0, maxNavUs = 0;
    void observe(const uint8_t *frame, size_t length, uint32_t sentGp2, uint32_t rxGp2) {
        const auto delta = clockDelta(rxGp2, sentGp2);
        if (!frame || length < 10 || length > 4096 || (frame[0] & 3) ||
            delta <= 0 || delta > 100000 || frames == 4096) return;
        const auto type = frame[0] & 12;
        if (type != 0 && type != 4 && type != 8) return;
        if (type != 4 && length < 24) return;
        if (!frames || uint32_t(delta) < firstOffsetUs) firstOffsetUs = uint32_t(delta);
        if (uint32_t(delta) > lastOffsetUs) lastOffsetUs = uint32_t(delta);
        ++frames; bytes += uint32_t(length);
        if (type == 0) ++management;
        if (type == 4) ++control;
        if (type == 8) ++data;
        const auto duration = le16(frame + 2);
        // The high bit denotes special Duration/ID encodings, including a
        // PS-Poll association ID. Never present these as medium reservation.
        if (!(duration & 0x8000) && duration > maxNavUs) maxNavUs = duration;
    }
};

struct SyncWireProfile {
    uint8_t version, subtype, nextChannel, masterChannel, guard, presence;
    uint8_t extensions[4];
    uint16_t flags, periodTu, actionPeriodTu, extensionTu, commonTu, remainingTu;
    uint16_t downCounterTu, counter, alignment;
    bool separatePlan;
    ChannelPlan plan;
};

// Called only after selecting the owned transmitter. No identities, payloads
// or timing clock values are retained. Failure leaves the prior profile alone.
inline bool syncWireProfile(const uint8_t *frame, size_t length, SyncWireProfile *out) {
    SyncAnnouncement schedule = {};
    if (!out || parseSchedule(frame, length, &schedule) != ScheduleResult::Valid) return false;
    SyncWireProfile candidate = {};
    candidate.version = frame[29]; candidate.subtype = frame[30];
    candidate.plan = schedule.plan;
    for (size_t pos = 40; pos < length;) {
        const auto kind = frame[pos];
        const size_t count = le16(frame + pos + 1);
        const auto p = frame + pos + 3;
        if (kind == 4) {
            candidate.nextChannel = p[0]; candidate.downCounterTu = le16(p + 1);
            candidate.masterChannel = p[3]; candidate.guard = p[4];
            candidate.periodTu = le16(p + 5); candidate.actionPeriodTu = le16(p + 7);
            candidate.flags = le16(p + 9); candidate.extensionTu = le16(p + 11);
            candidate.commonTu = le16(p + 13); candidate.remainingTu = le16(p + 15);
            for (unsigned i = 0; i < 4; ++i) candidate.extensions[i] = p[17 + i];
            candidate.presence = p[27]; candidate.counter = le16(p + 29);
            candidate.alignment = le16(p + 31);
        }
        if (kind == 18) candidate.separatePlan = true;
        pos += 3 + count;
    }
    *out = candidate;
    return true;
}
}

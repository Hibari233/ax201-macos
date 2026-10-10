// SPDX-License-Identifier: GPL-2.0-or-later
// Passive AWDL schedule tracking. This does not elect a master, hop channels,
// authorize RF transmission, or establish an AWDL/AirDrop session.
// Wire fields: Wireshark packet-awdl.c; TU = 1024 us, AW counter wraps at 2^16.
#ifndef ITL_AWDL_SCHEDULE_H
#define ITL_AWDL_SCHEDULE_H
#include "AwdlFrame.h"

namespace ItlAwdl {
constexpr uint32_t TuUs = 1024;
constexpr unsigned ScheduleLength = 16;
struct ChannelPlan {
    uint8_t channels[ScheduleLength];
    uint8_t operating_classes[ScheduleLength];
    uint16_t step_aw;
    uint8_t encoding;
};
struct SyncAnnouncement {
    uint16_t aw_period_tu, down_counter_tu, aw_counter;
    uint8_t presence, master[6];
    uint32_t tx_delay_us;
    ChannelPlan plan;
};
enum class ScheduleResult { Valid, NoSync, Malformed, Unsupported };

inline ScheduleResult parsePlan(const uint8_t *p, size_t n, ChannelPlan *out) {
    if (n < 6) return ScheduleResult::Malformed;
    if (p[0] != ScheduleLength - 1 || p[2] || le16(p + 4) != 0xffff)
        return ScheduleResult::Unsupported;
    // Legacy encoding 1 contains bonded-channel values. Do not silently treat
    // them as primary channels; prefer the explicit channel/opclass TLV 18.
    if (p[1] != 0 && p[1] != 3) return ScheduleResult::Unsupported;
    const unsigned width = p[1] == 3 ? 2 : 1;
    if (n < 6 + ScheduleLength * width) return ScheduleResult::Malformed;
    ChannelPlan plan = {};
    plan.encoding = p[1]; plan.step_aw = uint16_t(p[3]) + 1;
    for (unsigned i = 0; i < ScheduleLength; ++i) {
        plan.channels[i] = p[6 + i * width];
        plan.operating_classes[i] = width == 2 ? p[7 + i * width] : 0;
    }
    *out = plan;
    return ScheduleResult::Valid;
}

inline ScheduleResult parseSchedule(const uint8_t *frame, size_t n, SyncAnnouncement *out) {
    ActionInfo action = {};
    if (!out || n > 2048 || parseAction(frame, n, &action) != ParseResult::Valid)
        return ScheduleResult::Malformed;
    const uint8_t *sync = nullptr, *sequence = nullptr;
    size_t sync_length = 0, sequence_length = 0;
    for (size_t pos = 40; pos < n;) {
        const uint8_t kind = frame[pos];
        const size_t length = le16(frame + pos + 1);
        pos += 3; // parseAction already verified every TLV boundary.
        if (kind == 4) {
            if (sync) return ScheduleResult::Malformed;
            sync = frame + pos; sync_length = length;
        } else if (kind == 18) {
            if (sequence) return ScheduleResult::Malformed;
            sequence = frame + pos; sequence_length = length;
        }
        pos += length;
    }
    if (!sync) return ScheduleResult::NoSync;
    if (sync_length < 33) return ScheduleResult::Malformed;
    SyncAnnouncement candidate = {};
    candidate.down_counter_tu = le16(sync + 1);
    candidate.aw_period_tu = le16(sync + 5);
    candidate.presence = sync[27];
    candidate.aw_counter = le16(sync + 29);
    candidate.tx_delay_us = uint32_t(action.phy_tx - action.target_tx);
    for (unsigned i = 0; i < 6; ++i) candidate.master[i] = sync[21 + i];
    if (!candidate.aw_period_tu || !candidate.presence || !unicastMac(candidate.master))
        return ScheduleResult::Malformed;
    // Bound arithmetic and support only power-of-two presence modes whose
    // EAW grouping remains unambiguous across the 16-bit counter wrap.
    if (candidate.aw_period_tu > 1024 || candidate.presence > 16 ||
        (candidate.presence & (candidate.presence - 1))) return ScheduleResult::Unsupported;
    const uint32_t period_tu = candidate.aw_period_tu * candidate.presence;
    if (candidate.down_counter_tu > period_tu || candidate.tx_delay_us >= period_tu * TuUs)
        return ScheduleResult::Malformed;
    ScheduleResult result;
    if (sequence) result = parsePlan(sequence, sequence_length, &candidate.plan);
    else if (sync_length > 33) result = parsePlan(sync + 33, sync_length - 33, &candidate.plan);
    else return ScheduleResult::Unsupported;
    if (result != ScheduleResult::Valid) return result;
    if (candidate.plan.step_aw != candidate.presence) return ScheduleResult::Unsupported;
    *out = candidate; // Invalid frames cannot partially change caller state.
    return ScheduleResult::Valid;
}

inline int64_t clockDelta(uint32_t newer, uint32_t older) {
    uint32_t bits = newer - older;
    return bits <= 0x7fffffffU ? int64_t(bits) : int64_t(bits) - 0x100000000LL;
}
inline int32_t counterDelta(uint16_t newer, uint16_t older) {
    uint16_t bits = uint16_t(newer - older);
    return bits <= 0x7fffU ? int32_t(bits) : int32_t(bits) - 0x10000;
}
inline bool sameSchedule(const SyncAnnouncement &a, const SyncAnnouncement &b) {
    if (a.aw_period_tu != b.aw_period_tu || a.presence != b.presence ||
        a.plan.encoding != b.plan.encoding || a.plan.step_aw != b.plan.step_aw ||
        !equalMac(a.master, b.master)) return false;
    for (unsigned i = 0; i < ScheduleLength; ++i)
        if (a.plan.channels[i] != b.plan.channels[i] ||
            a.plan.operating_classes[i] != b.plan.operating_classes[i]) return false;
    return true;
}
struct ScheduleSample {
    uint32_t next_boundary_gp2_us;
    uint16_t next_counter;
    int64_t error_us;
    uint32_t coherent_samples;
    bool reset, outlier;
};
struct ScheduleSlot {
    uint32_t start_gp2_us, end_gp2_us;
    uint16_t counter;
    uint8_t channel, operating_class;
};

class ScheduleTracker {
public:
    // Host time only detects long gaps/restarts; phase calculations use the
    // firmware's RX-on-air GP2 clock, never IRQ delivery or userspace time.
    bool observe(const SyncAnnouncement &announcement, uint32_t gp2, uint64_t host_us,
                 ScheduleSample *sample) {
        if (!sample) return false;
        uint32_t eaw_us = announcement.aw_period_tu * announcement.presence * TuUs;
        if (!eaw_us || !announcement.presence) return false;
        int64_t remaining = int64_t(announcement.down_counter_tu) * TuUs - announcement.tx_delay_us;
        uint16_t next = uint16_t((announcement.aw_counter / announcement.presence + 1) * announcement.presence);
        // A frame can reach the air just after the boundary it describes.
        if (remaining <= 0) { remaining += eaw_us; next = uint16_t(next + announcement.presence); }
        uint32_t boundary = gp2 + uint32_t(remaining);
        bool reset = !valid_ || host_us < host_ || host_us - host_ > 2000000 ||
                     !sameSchedule(announcement, previous_);
        if (!reset && clockDelta(gp2, gp2_) <= 0) return false; // stale/reordered RX
        int64_t error = 0;
        if (!reset) {
            uint32_t predicted = boundary_ + uint32_t(int64_t(counterDelta(next, counter_)) * previous_.aw_period_tu * TuUs);
            error = clockDelta(boundary, predicted);
        }
        const bool outlier = !reset && (error > 3 * TuUs || error < -int64_t(3 * TuUs));
        // Re-seed after a discontinuity; never claim synchronization from a
        // single sample or let a rejected sample extend a previous estimate.
        coherent_ = reset || outlier ? 1 : coherent_ + 1;
        previous_ = announcement; gp2_ = gp2; host_ = host_us;
        boundary_ = boundary; counter_ = next; valid_ = true;
        *sample = {boundary, next, error, coherent_, reset, outlier};
        return true;
    }
    // Projection is deliberately short lived. The radio backend must also
    // validate its live clock and the firmware channel map before using it.
    bool project(uint32_t at_gp2, uint32_t now_gp2, uint64_t host_us, ScheduleSlot *slot) const {
        if (!slot || !valid_ || coherent_ < 3 || host_us < host_ ||
            host_us - host_ > 1000000) return false;
        int64_t age = clockDelta(now_gp2, gp2_);
        int64_t ahead = clockDelta(at_gp2, now_gp2);
        if (age < 0 || age > 1000000 || ahead < 0 || ahead > 1000000) return false;
        const int64_t period = int64_t(previous_.aw_period_tu) * previous_.presence * TuUs;
        const int64_t delta = clockDelta(at_gp2, boundary_);
        // C++ division truncates toward zero; an instant before a boundary
        // belongs to the preceding slot, including across GP2/counter wrap.
        int64_t step = delta / period;
        if (delta < 0 && delta % period) --step;
        uint32_t start = boundary_ + uint32_t(step * period);
        uint16_t counter = uint16_t(counter_ + step * previous_.presence);
        unsigned index = (counter / previous_.presence) % ScheduleLength;
        *slot = {start, start + uint32_t(period), counter,
                 previous_.plan.channels[index], previous_.plan.operating_classes[index]};
        return true;
    }
    bool nextWindow(uint32_t now_gp2, uint64_t host_us, uint8_t channel,
                    uint32_t not_before_gp2, ScheduleSlot *slot, uint32_t *target) const {
        if (!slot || !target) return false;
        uint32_t at = now_gp2;
        for (unsigned i = 0; i <= ScheduleLength; ++i) {
            ScheduleSlot candidate = {};
            if (!project(at, now_gp2, host_us, &candidate)) return false;
            uint32_t earliest = candidate.start_gp2_us + 8 * TuUs;
            if (clockDelta(earliest, now_gp2 + 5000) < 0) earliest = now_gp2 + 5000;
            if (clockDelta(earliest, not_before_gp2) < 0) earliest = not_before_gp2;
            if (candidate.channel == channel &&
                clockDelta(candidate.end_gp2_us - 8 * TuUs, earliest) > 0) {
                *slot = candidate; *target = earliest; return true;
            }
            at = candidate.end_gp2_us;
        }
        return false;
    }
private:
    bool valid_ = false;
    SyncAnnouncement previous_ = {};
    uint32_t gp2_ = 0, boundary_ = 0, coherent_ = 0;
    uint64_t host_ = 0;
    uint16_t counter_ = 0;
};
}
#endif

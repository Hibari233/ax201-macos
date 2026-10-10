// SPDX-License-Identifier: GPL-2.0-or-later
// Owned 24H32 channel configuration. This does not retune or authorize RF TX.
#pragma once
#include "AwdlNativeMetadata.h"
#include "AwdlSchedule.h"

namespace ItlAwdl {
enum class NativeChannelResult { Invalid, Unsupported, Stored };

class NativeChannelSequence {
public:
    static constexpr size_t RequestLength = 400;
    NativeChannelResult replace(const uint8_t *request, size_t length) {
        if (!request || length != RequestLength || le32(request) > 1 ||
            !request[5] || request[5] > 32 || request[6] > 1)
            return NativeChannelResult::Invalid;
        // Native encoding 1 is a backend-dependent bonded-channel format,
        // not wire encoding 1. Keep it unsupported until that conversion and
        // bandwidth policy are qualified. Flags 0x30 select another command.
        const unsigned step = unsigned(request[7]) + 1;
        if (request[5] != ScheduleLength || request[6] || request[8] ||
            request[9] != 0xff || (request[4] & 0xf0) || step > 16 ||
            (step & (step - 1)) || request[10] || request[11])
            return NativeChannelResult::Unsupported;
        for (size_t i = 396; i < RequestLength; ++i)
            if (request[i]) return NativeChannelResult::Unsupported;

        ChannelPlan plan = {};
        plan.step_aw = uint16_t(step);
        uint8_t canonical[RequestLength] = {};
        canonical[0] = 1;
        // Low flag bits select bandwidth only for native encoding 1. The
        // audited encoding-0 builder ignores them; preserve them on readback.
        for (unsigned i = 4; i < 10; ++i) canonical[i] = request[i];
        for (unsigned i = 0; i < ScheduleLength; ++i) {
            const size_t offset = 12 + 12 * i;
            const uint32_t version = le32(request + offset);
            const uint32_t channel = le32(request + offset + 4);
            const uint32_t flags = le32(request + offset + 8);
            if (version > 1 || channel > 255) return NativeChannelResult::Invalid;
            if (!channel) {
                if (flags) return NativeChannelResult::Unsupported;
            } else {
                const uint32_t expected = nativeActionChannelFlags(channel);
                if (!expected || (flags & 0x18) != (expected & 0x18))
                    return NativeChannelResult::Invalid;
                // Only unbonded 2.4/5 GHz records. Regulatory hints are not
                // permissions; the radio must check its current channel map.
                if (flags & ~0x1faU) return NativeChannelResult::Unsupported;
            }
            canonical[offset] = 1;
            nativeMetadataPut32(canonical + offset + 4, channel);
            nativeMetadataPut32(canonical + offset + 8, flags);
            plan.channels[i] = uint8_t(channel);
        }
        // Validate every record before replacing anything. Do not retain
        // producer memory, unused records, padding, or the opaque tail.
        for (size_t i = 0; i < RequestLength; ++i) request_[i] = canonical[i];
        plan_ = plan;
        ready_ = true;
        return NativeChannelResult::Stored;
    }
    bool copyRequest(uint8_t *out, size_t length) const {
        if (!ready_ || !out || length != RequestLength) return false;
        for (size_t i = 0; i < RequestLength; ++i) out[i] = request_[i];
        return true;
    }
    const ChannelPlan &plan() const { return plan_; }
    bool ready() const { return ready_; }
    void clear() { *this = {}; }
private:
    uint8_t request_[RequestLength] = {};
    ChannelPlan plan_ = {};
    bool ready_ = false;
};
}

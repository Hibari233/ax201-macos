// SPDX-License-Identifier: GPL-2.0-or-later
// Audited x86_64 24H32 event representation, not a radio scheduler.
#pragma once
#include "AwdlNativeMetadata.h"

namespace ItlAwdl {
constexpr uint32_t NativeAvailabilityWindowStartEvent = 41;
constexpr uint32_t NativeAvailabilityWindowExtensionStartEvent = 42;
constexpr uint32_t NativeAvailabilityWindowEndEvent = 43;
constexpr uint32_t NativeSyncStateChangedEvent = 46;

enum class NativeSyncRole : uint32_t {
    ElectedMaster = 1,
    Follower = 2,
    NonElectionMaster = 3
};

struct NativeAvailabilityWindowContext {
    uint16_t counter;
    NativeSyncRole role;
    uint8_t master[6], topMaster[6];
    uint32_t primaryChannel;
    // This is the native event's local-clock anchor, not host delivery time
    // or a peer's PHY timestamp. The native peer-timeline consumer subtracts
    // 3500 us from this field. Mapping a hardware AW boundary to that anchor
    // is a backend responsibility, not an implicit adjustment in this codec.
    uint32_t nativeAnchorTimeUs;
    bool nativeAnchorValid;
};
struct alignas(4) NativeAvailabilityWindowStart { uint8_t bytes[36]; };
static_assert(sizeof(NativeAvailabilityWindowStart) == 36, "24H32 AW start event");

inline bool encodeNativeAvailabilityWindowStart(const NativeAvailabilityWindowContext &window,
                                               NativeAvailabilityWindowStart *out) {
    if (!out) return false;
    *out = {};
    const uint32_t role = static_cast<uint32_t>(window.role);
    const auto flags = nativeActionChannelFlags(window.primaryChannel);
    if (role < 1 || role > 3 || !window.nativeAnchorValid || !flags ||
        !unicastMac(window.master) || !unicastMac(window.topMaster)) return false;
    auto bytes = out->bytes;
    nativeMetadataPut32(bytes, window.counter);
    nativeMetadataPut32(bytes + 4, role);
    for (unsigned i = 0; i < 6; ++i) {
        bytes[8 + i] = window.master[i];
        bytes[14 + i] = window.topMaster[i];
    }
    // Unlike action RX metadata, this timestamp is preserved in firmware
    // network byte order by the native producer and bswapped by its consumer.
    for (unsigned i = 0; i < 4; ++i)
        bytes[20 + i] = uint8_t(window.nativeAnchorTimeUs >> (24 - 8 * i));
    nativeMetadataPut32(bytes + 24, 1);
    nativeMetadataPut32(bytes + 28, window.primaryChannel);
    nativeMetadataPut32(bytes + 32, flags);
    return true;
}
}

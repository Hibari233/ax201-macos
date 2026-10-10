// SPDX-License-Identifier: GPL-2.0-or-later
// 24H32 management RX metadata encoding, independent of kernel allocation.
#pragma once
#include "AwdlFrame.h"

namespace ItlAwdl {
constexpr size_t NativeActionMetadataLength = 0x48;
struct alignas(8) NativeActionMetadata { uint8_t bytes[NativeActionMetadataLength]; };
static_assert(sizeof(NativeActionMetadata) == 0x48, "24H32 action RX metadata");

// The caller must supply an on-air receive clock in the SAME microsecond
// domain as its synchronization backend. This encoder does not turn host
// delivery time, remote action timestamps or an overloaded Intel TSF field
// into that clock. A valid zero is allowed at clock wrap.
struct NativeActionRxContext {
    uint32_t primaryChannel;
    int32_t rssiDbm;
    uint32_t localMacTimeUs;
    bool localMacTimeValid;
};

// Initial support is 20 MHz legacy management reception on AX201's 2.4/5 GHz
// bands. This describes an RX channel; it does not authorize a channel or TX.
inline uint32_t nativeActionChannelFlags(uint32_t channel) {
    if (channel >= 1 && channel <= 14) return 0x0a;
    if ((channel >= 36 && channel <= 64 && channel % 4 == 0) ||
        (channel >= 100 && channel <= 144 && channel % 4 == 0) ||
        (channel >= 149 && channel <= 177 && channel % 4 == 1))
        return 0x12 | ((channel >= 52 && channel <= 144) ? 0x100 : 0);
    return 0;
}
inline void nativeMetadataPut32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(value >> (8 * i));
}
inline bool encodeNativeActionMetadata(const uint8_t *source,
    const NativeActionRxContext &rx, NativeActionMetadata *out) {
    if (!out) return false;
    *out = {};
    const auto channelFlags = nativeActionChannelFlags(rx.primaryChannel);
    if (!source || !unicastMac(source) || !channelFlags || !rx.localMacTimeValid ||
        rx.rssiDbm < -127 || rx.rssiDbm > 0) return false;
    auto bytes = out->bytes;
    for (unsigned i = 0; i < 6; ++i) bytes[i] = source[i];
    // The native Broadcom producer converts these fields to host endian.
    // 24H32 x86_64 consumes that representation, not the firmware event wire
    // endian. The channel extent is three uint32_t words starting at 0x0c.
    nativeMetadataPut32(bytes + 0x0c, 1);
    nativeMetadataPut32(bytes + 0x10, rx.primaryChannel);
    nativeMetadataPut32(bytes + 0x14, channelFlags);
    nativeMetadataPut32(bytes + 0x2c, uint32_t(rx.rssiDbm));
    nativeMetadataPut32(bytes + 0x30, rx.localMacTimeUs);
    // The native producer's rate word at 0x28 is left zero until its units and
    // Intel conversion are qualified; do not assume ratespec/PLCP/500 kbps.
    // No chain RSSI is claimed. 0x40 is its validity word, 0x44 contains four
    // signed RSSI bytes; neither is the high half of a receive timestamp.
    return true;
}
}

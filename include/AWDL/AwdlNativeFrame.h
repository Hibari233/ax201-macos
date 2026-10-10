// SPDX-License-Identifier: GPL-2.0-or-later
// Validate the boundary to the 24H32 native management-frame input. This does
// not manufacture native receive timestamps or claim peer negotiation.
#pragma once
#include "AwdlFrame.h"

namespace ItlAwdl {
constexpr size_t NativeActionMetadataLength = 0x48;
constexpr size_t NativeActionMaxWireLength = 2048;
struct NativeActionView { const uint8_t *body; size_t length; };

// Metadata must already come from an audited hardware-to-native conversion.
// Only its length and source-MAC prefix are established here. The remaining
// channel/timestamp semantics are intentionally not guessed by this adapter.
inline bool nativeActionView(const uint8_t *frame, size_t length,
    const void *metadata, size_t metadataLength, NativeActionView *out) {
    if (out) *out = {};
    if (!out || !frame || !metadata || metadataLength != NativeActionMetadataLength ||
        length > NativeActionMaxWireLength || length < 40 ||
        (frame[1] & 4) || (frame[22] & 15) || !unicastMac(frame + 10) ||
        !equalMac(frame + 10, static_cast<const uint8_t *>(metadata)) ||
        parseAction(frame, length, nullptr) != ParseResult::Valid) return false;
    // Native IO80211Buffer contains the action body, without the 24-byte
    // 802.11 header. Source address and receive context travel separately.
    *out = {frame + 24, length - 24};
    return true;
}
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Read-only matching of a bare 802.11 ACK addressed to our native sender.
#pragma once
#include "AwdlClock.h"
#include "AwdlFrame.h"

namespace ItlAwdl {
// Caller has verified RX FCS, channel, contiguous storage and the active
// single-peer TX lease. ACK carries only a receiver address, not a source or
// sequence number: a match is timing evidence, not independent peer identity.
inline bool matchOwnedAck(const uint8_t *frame, size_t length, const uint8_t *local,
    uint32_t txGp2, uint32_t rxGp2, uint32_t *delayUs) {
    if (!frame || !local || !delayUs || length != 10 || frame[0] != 0xd4 ||
        (frame[1] & 0xc7) || !unicastMac(local) || !equalMac(frame + 4, local)) return false;
    const auto delay = clockDelta(rxGp2, txGp2);
    if (delay <= 0 || delay > 100000) return false;
    *delayUs = uint32_t(delay);
    return true;
}
}

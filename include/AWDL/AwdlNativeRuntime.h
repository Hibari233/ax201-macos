// SPDX-License-Identifier: GPL-2.0-or-later
// Clock-domain bridge for the bounded native synchronization engine.
#pragma once
#include "AwdlLocalTimeline.h"
#include "AwdlNativeEvents.h"

namespace ItlAwdl {
constexpr uint32_t NativeRuntimeTxLimit = 48;
constexpr uint32_t NativeRuntimeTickUs = 2000;

// 24H32 monitorPeersTimeline subtracts 3500 us from the AW event timestamp,
// then calculatePeerTimeDiff adds (next peer counter - local counter) * AW
// period. Consequently its input must be the start of the REPORTED counter
// plus 3500, not callback delivery time or the action's target-TX timestamp.
inline bool nativeRuntimeWindow(const LocalSyncBoundary &sample, uint16_t periodTu,
    uint8_t presence, NativeSyncRole role, const uint8_t *master,
    const uint8_t *topMaster, uint32_t channel, NativeAvailabilityWindowContext *out) {
    if (!out || !periodTu || !presence || presence > 16 || (presence & (presence - 1)) ||
        !master || !topMaster || !unicastMac(master) || !unicastMac(topMaster)) return false;
    const uint32_t eawUs = uint32_t(periodTu) * TuUs * presence;
    if (!sample.remainingUs || sample.remainingUs > eawUs ||
        sample.nextBoundaryUs != sample.phyUs + sample.remainingUs) return false;
    NativeAvailabilityWindowContext result = {};
    result.counter = uint16_t(sample.counter - sample.counter % presence);
    result.role = role;
    for (unsigned i = 0; i < 6; ++i) { result.master[i] = master[i]; result.topMaster[i] = topMaster[i]; }
    result.primaryChannel = channel;
    result.nativeAnchorTimeUs = sample.nextBoundaryUs - eawUs + 3500U;
    result.nativeAnchorValid = true;
    NativeAvailabilityWindowStart check = {};
    if (!encodeNativeAvailabilityWindowStart(result, &check)) return false;
    *out = result;
    return true;
}
}

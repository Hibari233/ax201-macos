// SPDX-License-Identifier: GPL-2.0-or-later
// Bounded local-master AW timing. No election, radio permission or TX implied.
#pragma once
#include "AwdlSchedule.h"

namespace ItlAwdl {
struct LocalSyncBoundary {
    uint32_t targetUs, phyUs, nextBoundaryUs, remainingUs;
    uint16_t counter, downCounterTu;
    uint8_t slot;
};

class LocalTimeline {
public:
    // A fresh firmware-clock epoch, aligned to an EAW counter. The caller
    // must first qualify the live clock and invalidate this on radio reset.
    bool start(uint32_t gp2, uint16_t awPeriodTu, uint8_t presence, uint16_t counter = 0) {
        if (!awPeriodTu || awPeriodTu > 1024 || !presence || presence > 16 ||
            (presence & (presence - 1)) || counter % presence) return false;
        origin_ = gp2; elapsed_ = 0; initialCounter_ = counter;
        awUs_ = uint32_t(awPeriodTu) * TuUs;
        presence_ = presence; valid_ = true;
        return true;
    }
    bool sample(uint32_t gp2, LocalSyncBoundary *out) {
        if (!valid_ || !out) return false;
        const auto elapsed = clockDelta(gp2, origin_);
        // The qualification radio lease is <=5 seconds. Keep wrap and long
        // gaps unambiguous even if a caller misses its independent timeout.
        if (elapsed < elapsed_ || elapsed > 30000000) return false;
        const auto eawUs = awUs_ * presence_;
        const auto phase = uint32_t(elapsed) % eawUs;
        const auto fractionalTu = phase % TuUs;
        LocalSyncBoundary value = {};
        value.phyUs = gp2;
        value.targetUs = gp2 - fractionalTu;
        value.remainingUs = eawUs - phase;
        value.nextBoundaryUs = gp2 + value.remainingUs;
        value.counter = uint16_t(initialCounter_ + uint32_t(elapsed) / awUs_);
        value.downCounterTu = uint16_t((value.remainingUs + fractionalTu) / TuUs);
        value.slot = uint8_t((value.counter / presence_) % ScheduleLength);
        // Sub-TU phase lives in phy-target. The receiver can reconstruct the
        // exact boundary without rounding a TU down-counter or using host time.
        *out = value;
        elapsed_ = elapsed;
        return true;
    }
    void clear() { valid_ = false; }
private:
    bool valid_ = false;
    uint32_t origin_ = 0, awUs_ = 0;
    int64_t elapsed_ = 0;
    uint16_t initialCounter_ = 0;
    uint8_t presence_ = 0;
};
}

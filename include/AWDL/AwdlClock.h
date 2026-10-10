// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef ITL_AWDL_CLOCK_H
#define ITL_AWDL_CLOCK_H
#include "AwdlSchedule.h"
namespace ItlAwdl {
// XNU adds timer coalescing slack even when explicit leeway is zero.
// Approach long deadlines in two stages; the final short wait receives much
// less slack. No spin loop, global timer policy change, or priority boost.
inline uint32_t timerDelayUs(uint32_t remaining) {
    uint32_t delay = remaining > 100000 ? 100000 : remaining;
    if (delay > 4000) delay -= 3000;
    return delay < 200 ? 200 : delay;
}
class ClockCorrelation {
public:
    // Bit 0: bounded register read; bit 1: consecutive rate samples agree
    // with host uptime; bit 2: same clock domain as a recent peer RX GP2.
    uint32_t observe(uint32_t gp2, uint64_t begin, uint64_t end,
                     bool has_rx, uint32_t rx_gp2, uint64_t rx_host) {
        if (end < begin || end - begin > 2000 || gp2 == 0xa5a5a5a5U || gp2 == 0xffffffffU) {
            previous_ = false; coherent_ = 0; return 0;
        }
        uint32_t flags = 1;
        uint64_t midpoint = begin + (end - begin) / 2;
        if (previous_ && midpoint > host_ && midpoint - host_ >= 500 && midpoint - host_ <= 500000) {
            int64_t error = clockDelta(gp2, gp2_) - int64_t(midpoint - host_);
            coherent_ = error >= -2000 && error <= 2000 ? coherent_ + 1 : 0;
        } else if (!previous_ || midpoint < host_ || midpoint - host_ > 500000) coherent_ = 0;
        // Very closely spaced reads must not destroy the preceding baseline.
        if (!previous_ || midpoint < host_ || midpoint - host_ >= 500) {
            gp2_ = gp2; host_ = midpoint; previous_ = true;
        }
        if (coherent_ >= 2) flags |= 2;
        if (has_rx && midpoint >= rx_host && midpoint - rx_host <= 1000000) {
            int64_t age = clockDelta(gp2, rx_gp2);
            int64_t delivery = age - int64_t(midpoint - rx_host);
            if (age >= 0 && delivery >= -2000 && delivery <= 50000) flags |= 4;
        }
        return flags;
    }
private:
    bool previous_ = false;
    uint32_t gp2_ = 0, coherent_ = 0;
    uint64_t host_ = 0;
};
}
#endif

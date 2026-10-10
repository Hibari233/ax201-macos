// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>

namespace ItlAwdl {
enum RxClockFlags : uint32_t { RxGP2Valid = 1, RxTSFValid = 2 };
struct RxClockSample { uint32_t flags, gp2Us; uint64_t tsfUs; };
// The Intel RX descriptor's TSF union contains PHY data when overloaded.
// Inputs have already been read from a validated descriptor in host endian.
// GP2 remains an independent on-air clock; neither clock is host IRQ time.
inline RxClockSample receiveClockSample(uint32_t gp2, uint64_t tsfOrPhy,
    bool tsfOverloaded) {
    return {RxGP2Valid | (tsfOverloaded ? 0U : RxTSFValid), gp2,
            tsfOverloaded ? 0 : tsfOrPhy};
}
}

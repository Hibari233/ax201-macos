// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef ITL_AWDL_REGULATORY_H
#define ITL_AWDL_REGULATORY_H
#include <stdint.h>
namespace ItlAwdl {
// Intel LAR source 5 is a firmware Wi-Fi-derived hint. No host country input,
// OEM/test source, reserved source, reset-to-NVM code or world override.
inline bool countryHintAllowed(uint16_t country, uint8_t source) {
    const uint8_t a = uint8_t(country >> 8), b = uint8_t(country);
    return source == 5 && a >= 'A' && a <= 'Z' && b >= 'A' && b <= 'Z' && country != 0x5a5a;
}
inline bool countryHintFresh(uint64_t now_us, uint64_t observed_us) {
    return observed_us && now_us >= observed_us && now_us - observed_us <= 60000000;
}
}
#endif

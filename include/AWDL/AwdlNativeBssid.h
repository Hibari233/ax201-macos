// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "AwdlFrame.h"

namespace ItlAwdl {
constexpr uint8_t DefaultAwdlBssid[6] = {0, 0x25, 0, 0xff, 0x94, 0x73};
constexpr uint8_t DefaultCompanionBssid[6] = {0, 0x25, 0, 0xff, 0x94, 0x74};
enum class BssidResult { Invalid, Unsupported, Stored };

// Own the exact 24H32 default pair supplied by native configure(). Staging it
// while disabled does not create a firmware interface or enable LLW. Custom
// identifiers stay unsupported until all action/data/filter paths support them.
class NativeBssidConfiguration {
public:
    BssidResult replace(uint32_t version, const uint8_t *awdl, const uint8_t *companion) {
        if (version > 1 || !awdl || !companion || !unicastMac(awdl) ||
            !unicastMac(companion) || equalMac(awdl, companion)) return BssidResult::Invalid;
        if (!equalMac(awdl, DefaultAwdlBssid) || !equalMac(companion, DefaultCompanionBssid))
            return BssidResult::Unsupported;
        uint8_t copy[12];
        for (unsigned i = 0; i < 6; ++i) { copy[i] = awdl[i]; copy[i + 6] = companion[i]; }
        for (unsigned i = 0; i < 12; ++i) bytes_[i] = copy[i];
        ready_ = true;
        return BssidResult::Stored;
    }
    void clear() { for (auto &byte : bytes_) byte = 0; ready_ = false; }
    bool ready() const { return ready_; }
    const uint8_t *awdl() const { return bytes_; }
    const uint8_t *companion() const { return bytes_ + 6; }
private:
    uint8_t bytes_[12] = {};
    bool ready_ = false;
};
}

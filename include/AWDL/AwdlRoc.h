// SPDX-License-Identifier: GPL-2.0-or-later
// Session-protection v2 notifications for an exclusive P2P-device RX trial.
#pragma once
#include "AwdlFrame.h"

namespace ItlAwdl {
class P2pSessionProtection {
public:
    static constexpr uint32_t MacId = 1, Configuration = 2;
    uint32_t generation = 0, durationTu = 0, starts = 0, ends = 0, ignored = 0, malformed = 0;
    bool requested = false, active = false, failed = false, ended = false;

    bool arm(uint32_t firmwareGeneration, uint64_t remainingUs) {
        if (requested || remainingUs < 1024 || remainingUs > 5000000) return false;
        *this = {};
        generation = firmwareGeneration; durationTu = uint32_t(remainingUs / 1024);
        requested = true;
        return true;
    }
    bool observe(const uint8_t *bytes, size_t length, uint32_t firmwareGeneration) {
        if (!requested || firmwareGeneration != generation) { ++ignored; return false; }
        if (!bytes || length != 16) { ++malformed; fail(); return false; }
        const auto mac = le32(bytes), status = le32(bytes + 4);
        const auto start = le32(bytes + 8), configuration = le32(bytes + 12);
        if (mac != MacId || configuration != Configuration) { ++ignored; return false; }
        if (status > 1 || start > 1) { ++malformed; fail(); return false; }
        if (ended || failed) { ++ignored; return false; }
        if (!status) { fail(); return true; }
        if (start) { ++starts; active = true; }
        else { ++ends; active = false; ended = true; }
        return true;
    }
    void fail() { failed = true; active = false; }
    void stop() { requested = active = false; }
    bool ready(uint32_t firmwareGeneration) const {
        return requested && active && !failed && !ended && firmwareGeneration == generation;
    }
};
}

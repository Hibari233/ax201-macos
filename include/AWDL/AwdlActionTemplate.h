// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "AwdlFrame.h"

namespace ItlAwdl {
// Native updateSyncFrameTemplate supplies at most 512 bytes of supplementary
// TLVs. It is NOT a complete action frame, a schedule, or permission to send.
// Own the bytes: the native producer's pointer references its stack.
class ActionTemplate {
public:
    static constexpr size_t Capacity = 512;
    bool replace(const uint8_t *input, size_t size) {
        if (!input || size < 3 || size > Capacity) return false;
        for (size_t pos = 0; pos < size;) {
            if (size - pos < 3) return false;
            const size_t count = le16(input + pos + 1);
            pos += 3;
            if (count > size - pos) return false;
            pos += count;
        }
        // Validate completely before changing the old template or revision.
        // memmove-like copy supports an overlapping view of our own bytes.
        uint8_t copy[Capacity] = {};
        for (size_t i = 0; i < size; ++i) copy[i] = input[i];
        for (size_t i = 0; i < Capacity; ++i) bytes_[i] = copy[i];
        length_ = uint32_t(size);
        advance();
        return true;
    }
    void clear() {
        for (auto &byte : bytes_) byte = 0;
        length_ = 0;
        advance();
    }
    const uint8_t *bytes() const { return bytes_; }
    uint32_t length() const { return length_; }
    uint32_t revision() const { return revision_; }
private:
    void advance() { if (!++revision_) ++revision_; }
    uint8_t bytes_[Capacity] = {};
    uint32_t length_ = 0, revision_ = 0;
};
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Read-only Intel TX_RSP_API_S_VER_6 observation inside a completed RX buffer.
#pragma once
#include "AwdlFrame.h"

namespace ItlAwdl {
struct TxCompletionView { uint16_t status, mediaUs, byteCount, nextSequence; };

inline bool findTxCompletion(const uint8_t *bytes, size_t length, uint16_t queue,
    uint16_t nextSequence, uint16_t byteCount, TxCompletionView *out) {
    if (!bytes || !out || length > 8192 || !byteCount || nextSequence > 0xfff) return false;
    for (size_t offset=0; offset+8<=length;) {
        const auto p = bytes+offset;
        const auto header = le32(p);
        if (header == 0x55550000U) return false; // firmware end-of-buffer marker
        const size_t packetLength = size_t(header&0x3fffU)+4;
        if (packetLength<8 || packetLength>length-offset) return false;
        // Command 0x1c, group zero. A single-frame response is 44 bytes,
        // followed by its 32-bit scheduler sequence number (not 802.11 seq).
        if (p[4]==0x1c && p[5]==0 && packetLength>=56) {
            const auto reply=p+8;
            const auto sequence=uint16_t(le32(reply+44)&0xfffU);
            if (reply[0]==1 && le16(reply+36)==queue && sequence==nextSequence &&
                le16(reply+30)==byteCount) {
                TxCompletionView value={uint16_t(le16(reply+40)&0xffU),le16(reply+8),
                    le16(reply+30),sequence};
                *out=value;
                return true;
            }
        }
        const auto advance=(packetLength+63)&~size_t(63);
        if (advance>length-offset) return false;
        offset+=advance;
    }
    return false;
}
}

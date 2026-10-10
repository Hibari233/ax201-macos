// SPDX-License-Identifier: GPL-2.0-or-later
// Bounds-checked AWDL action-frame inspection, shared by driver and tests.
// Wire-format references: Wireshark packet-awdl.c and Open Wireless Link frame.h.
// No packed-structure dereferences or unaligned host-endian loads.
#ifndef ITL_AWDL_FRAME_H
#define ITL_AWDL_FRAME_H
#include <stddef.h>
#include <stdint.h>

namespace ItlAwdl {
enum class ParseResult { NotAwdl, Truncated, Invalid, Valid };
struct ActionInfo {
    uint8_t subtype;
    uint8_t version;
    uint16_t tlv_count;
    uint32_t phy_tx;
    uint32_t target_tx;
};
inline uint16_t le16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
inline uint32_t le32(const uint8_t *p) { return uint32_t(le16(p)) | uint32_t(le16(p + 2)) << 16; }
inline bool equalMac(const uint8_t *a, const uint8_t *b) {
    for (unsigned i = 0; i < 6; ++i) if (a[i] != b[i]) return false;
    return true;
}
inline bool unicastMac(const uint8_t *a) {
    uint8_t bits = 0;
    for (unsigned i = 0; i < 6; ++i) bits |= a[i];
    return bits != 0 && !(a[0] & 1);
}
// A deliberately minimal diagnostic MIF: no synchronization/election/data
// service claims. Used only to test firmware TX/completion to an observed peer.
// This does not establish an AWDL session or provide AirDrop.
constexpr size_t ProbeLength = 45;
inline bool buildProbe(uint8_t *out, size_t capacity, const uint8_t *self,
                       const uint8_t *peer, uint16_t sequence, uint32_t time_us) {
    if (!out || capacity < ProbeLength || !self || !peer || !unicastMac(self) ||
        !unicastMac(peer) || equalMac(self, peer)) return false;
    for (size_t i = 0; i < ProbeLength; ++i) out[i] = 0;
    out[0] = 0xd0;
    const uint8_t bssid[6] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    for (unsigned i = 0; i < 6; ++i) { out[4 + i] = peer[i]; out[10 + i] = self[i]; out[16 + i] = bssid[i]; }
    uint16_t sc = uint16_t(sequence << 4);
    out[22] = uint8_t(sc); out[23] = uint8_t(sc >> 8);
    out[24] = 127; out[26] = 0x17; out[27] = 0xf2; out[28] = 8;
    out[29] = 0x10; out[30] = 3;
    for (unsigned i = 0; i < 4; ++i) out[32 + i] = out[36 + i] = uint8_t(time_us >> (8 * i));
    out[40] = 21; out[41] = 2; out[43] = 0x10; out[44] = 1;
    return true;
}
inline ParseResult parseAction(const uint8_t *p, size_t length, ActionInfo *out) {
    if (!p || length < 24) return ParseResult::Truncated;
    if (p[0] != 0xd0 || (p[1] & 0xc3)) return ParseResult::NotAwdl;
    if (length < 29) return ParseResult::Truncated;
    if (p[24] != 127 || p[25] != 0 || p[26] != 0x17 || p[27] != 0xf2 || p[28] != 8)
        return ParseResult::NotAwdl;
    if (length < 40) return ParseResult::Truncated;
    // Only PSF and MIF are supported. Do not reinterpret other vendor actions.
    if (p[29] != 0x10 || (p[30] != 0 && p[30] != 3) || p[31] != 0)
        return ParseResult::Invalid;
    ActionInfo info = {};
    info.version = p[29]; info.subtype = p[30];
    info.phy_tx = le32(p + 32); info.target_tx = le32(p + 36);
    size_t cursor = 40;
    while (cursor < length) {
        if (length - cursor < 3) return ParseResult::Truncated;
        size_t n = le16(p + cursor + 1);
        cursor += 3;
        if (n > length - cursor) return ParseResult::Truncated;
        cursor += n;
        ++info.tlv_count;
    }
    if (out) *out = info;
    return ParseResult::Valid;
}
}
#endif

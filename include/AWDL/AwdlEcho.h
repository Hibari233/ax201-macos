// SPDX-License-Identifier: GPL-2.0-or-later
// Fixed, owned-peer ICMPv6 diagnostics (RFC 4443 sections 2.3 and 4).
// No arbitrary packet injection, addressing, or application payload.
#ifndef ITL_AWDL_ECHO_H
#define ITL_AWDL_ECHO_H
#include "AwdlData.h"

namespace ItlAwdl {
constexpr size_t EchoMinLength = 64;
inline bool echoLength(size_t n) { return n >= EchoMinLength && n <= DataMtu; }
inline void echoLinkLocal(uint8_t *out, const uint8_t *mac) {
    for (unsigned i = 0; i < 16; ++i) out[i] = 0;
    out[0] = 0xfe; out[1] = 0x80;
    out[8] = mac[0] ^ 2; out[9] = mac[1]; out[10] = mac[2];
    out[11] = 0xff; out[12] = 0xfe;
    out[13] = mac[3]; out[14] = mac[4]; out[15] = mac[5];
}
inline uint8_t echoPayloadByte(size_t offset, uint32_t token) {
    // First four payload bytes identify this run; the rest test integrity.
    return offset < 4 ? uint8_t(token >> (24 - unsigned(offset) * 8)) :
                       uint8_t(offset ^ (token >> ((offset & 3) * 8)));
}
inline bool buildEchoIpv6(uint8_t *out, size_t capacity, const uint8_t *source,
                          const uint8_t *peer, size_t length, uint32_t token,
                          uint16_t sequence) {
    if (!out || !source || !peer || !unicastMac(source) || !unicastMac(peer) ||
        equalMac(source, peer) || !token || !echoLength(length) || capacity < length)
        return false;
    for (size_t i = 0; i < length; ++i) out[i] = 0;
    out[0] = 0x60;
    out[4] = uint8_t((length - 40) >> 8); out[5] = uint8_t(length - 40);
    out[6] = 58; out[7] = 64;
    echoLinkLocal(out + 8, source); echoLinkLocal(out + 24, peer);
    out[40] = 128;
    out[44] = uint8_t(token >> 24); out[45] = uint8_t(token >> 16);
    out[46] = uint8_t(sequence >> 8); out[47] = uint8_t(sequence);
    for (size_t i = 48; i < length; ++i) out[i] = echoPayloadByte(i - 48, token);
    uint32_t sum = uint32_t(length - 40) + 58;
    for (size_t i = 8; i < 40; i += 2) sum += be16(out + i);
    size_t i = 40;
    for (; length - i >= 2; i += 2) sum += be16(out + i);
    if (i < length) sum += uint32_t(out[i]) << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    uint16_t checksum = uint16_t(~sum);
    out[42] = uint8_t(checksum >> 8); out[43] = uint8_t(checksum);
    return true;
}
inline bool matchesEchoReply(const uint8_t *p, size_t length,
                             const uint8_t *self, const uint8_t *peer,
                             size_t expectedLength, uint32_t token,
                             uint16_t sequence) {
    if (!p || !self || !peer || !token || !echoLength(length) ||
        length != expectedLength || !unicastMac(self) || !unicastMac(peer) ||
        equalMac(self, peer) || dataChecksum(p, length) != DataChecksum::Valid ||
        p[6] != 58 || p[40] != 129 || p[41] ||
        be16(p + 44) != uint16_t(token >> 16) || be16(p + 46) != sequence)
        return false;
    uint8_t expected[16];
    echoLinkLocal(expected, peer);
    for (unsigned i = 0; i < 16; ++i) if (p[8 + i] != expected[i]) return false;
    echoLinkLocal(expected, self);
    for (unsigned i = 0; i < 16; ++i) if (p[24 + i] != expected[i]) return false;
    for (size_t i = 48; i < length; ++i)
        if (p[i] != echoPayloadByte(i - 48, token)) return false;
    return true;
}
}
#endif

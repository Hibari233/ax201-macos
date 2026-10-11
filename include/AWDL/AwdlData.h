// SPDX-License-Identifier: GPL-2.0-or-later
// AWDL data wire-format decoder; no allocation, I/O, or packed loads.
// Format references: Wireshark packet-awdl.c (dissect_awdl_data) and
// the Open Wireless Link protocol documentation. Original implementation.
#ifndef ITL_AWDL_DATA_H
#define ITL_AWDL_DATA_H
#include "AwdlFrame.h"

namespace ItlAwdl {
constexpr size_t DataMtu = 1500;
constexpr size_t MaxDataMpdu = 11454;
constexpr size_t MaxDataPackets = 16;
enum class DataResult { NotAwdl, Truncated, Invalid, Unsupported, TooMany, Valid };
struct DataPacket {
    const uint8_t *ipv6, *source, *destination;
    size_t length;
    uint16_t sequence;
};
// Views borrow the MPDU buffer. Consume before releasing/reusing that buffer.
struct DataBatch { size_t count; DataPacket packets[MaxDataPackets]; };
inline uint16_t be16(const uint8_t *p) { return uint16_t(p[0]) << 8 | p[1]; }
inline bool validIpv6(const uint8_t *p, size_t n) {
    return p && n >= 40 && n <= DataMtu && (p[0] >> 4) == 6 &&
           size_t(be16(p + 4)) == n - 40;
}
enum class DataChecksum { Unavailable, Valid, Invalid };
// Diagnostic integrity check for direct UDP/ICMPv6. Extension headers (in
// particular IPv6 fragments) require the real network stack, not this helper.
inline DataChecksum dataChecksum(const uint8_t *p, size_t n) {
    if (!validIpv6(p, n)) return DataChecksum::Invalid;
    const uint8_t protocol = p[6];
    if (protocol != 17 && protocol != 58) return DataChecksum::Unavailable;
    size_t length = n - 40;
    if (protocol == 17 && (length < 8 || be16(p + 44) != length || !be16(p + 46)))
        return DataChecksum::Invalid;
    if (protocol == 58 && length < 4) return DataChecksum::Invalid;
    uint32_t sum = uint32_t(length) + protocol;
    for (size_t i = 8; i < 40; i += 2) sum += be16(p + i);
    size_t cursor = 40;
    for (; n - cursor >= 2; cursor += 2) sum += be16(p + cursor);
    if (cursor < n) sum += uint32_t(p[cursor]) << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return sum == 0xffff ? DataChecksum::Valid : DataChecksum::Invalid;
}
inline DataResult decodeDataPayload(const uint8_t *p, size_t n, DataPacket &out) {
    const uint8_t snap[8] = {0xaa, 0xaa, 3, 0, 0x17, 0xf2, 8, 0};
    if (n < sizeof(snap)) return DataResult::Truncated;
    for (size_t i = 0; i < sizeof(snap); ++i)
        if (p[i] != snap[i]) return DataResult::NotAwdl;
    p += sizeof(snap); n -= sizeof(snap);
    if (n < 8) return DataResult::Truncated;
    if (p[0] != 3 || p[1] != 4) return DataResult::Unsupported;
    size_t cursor = 4;
    if (p[cursor] == 3) {
        // Long form: opening type-3 marker, short TLVs, closing type-3
        // marker. Every iteration consumes at least two bytes.
        bool first = true;
        for (;;) {
            if (n - cursor < 2) return DataResult::Truncated;
            uint8_t tag = p[cursor], length = p[cursor + 1];
            cursor += 2;
            if (length > n - cursor) return DataResult::Truncated;
            cursor += length;
            if (!first && tag == 3) break;
            first = false;
        }
    } else {
        if (p[cursor] || p[cursor + 1]) return DataResult::Unsupported;
        cursor += 2;
    }
    if (n - cursor < 2) return DataResult::Truncated;
    if (be16(p + cursor) != 0x86dd) return DataResult::Unsupported;
    cursor += 2;
    if (n - cursor < 40) return DataResult::Truncated;
    if (!validIpv6(p + cursor, n - cursor)) return DataResult::Invalid;
    out.ipv6 = p + cursor;
    out.length = n - cursor;
    out.sequence = le16(p + 2);
    return DataResult::Valid;
}
// The caller must strip radiotap, RX padding and FCS, and supply the explicit
// firmware deaggregation flag. Never guess deaggregation from packet contents.
// A malformed aggregate produces no output, including no partial delivery.
inline DataResult decodeData(const uint8_t *p, size_t n, bool deaggregated, DataBatch *out) {
    if (!p || n < 24) return DataResult::Truncated;
    if (p[0] != 0x08 && p[0] != 0x88) return DataResult::NotAwdl;
    if (n > MaxDataMpdu) return DataResult::Invalid;
    const uint8_t bssid[6] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    if (!equalMac(p + 16, bssid) || (p[1] & 3)) return DataResult::NotAwdl;
    if ((p[1] & 0x44) || (p[22] & 15)) return DataResult::Unsupported; // encrypted/fragments
    if (!unicastMac(p + 10)) return DataResult::Invalid;
    bool qos = p[0] == 0x88;
    size_t header = qos ? 26 : 24;
    if (p[1] & 0x80) {
        if (!qos) return DataResult::Unsupported;
        header += 4; // HT Control follows QoS Control.
    }
    if (n < header) return DataResult::Truncated;
    if (deaggregated && !qos) return DataResult::Invalid;
    bool aggregate = qos && (p[24] & 0x80) && !deaggregated;
    DataBatch batch = {};
    if (!aggregate) {
        auto &packet = batch.packets[0];
        packet.source = p + 10; packet.destination = p + 4;
        auto result = decodeDataPayload(p + header, n - header, packet);
        if (result != DataResult::Valid) return result;
        batch.count = 1;
    } else {
        size_t cursor = header;
        while (cursor < n) {
            if (n - cursor < 14) return DataResult::Truncated;
            if (batch.count == MaxDataPackets) return DataResult::TooMany;
            auto &packet = batch.packets[batch.count];
            packet.destination = p + cursor;
            packet.source = p + cursor + 6;
            if (!equalMac(packet.source, p + 10)) return DataResult::Invalid;
            size_t length = be16(p + cursor + 12);
            cursor += 14;
            if (length > n - cursor) return DataResult::Truncated;
            auto result = decodeDataPayload(p + cursor, length, packet);
            if (result != DataResult::Valid) return result;
            ++batch.count;
            cursor += length;
            if (cursor == n) break;
            size_t padding = (4 - ((14 + length) & 3)) & 3;
            if (padding >= n - cursor) return DataResult::Truncated;
            cursor += padding;
        }
        if (!batch.count) return DataResult::Truncated;
    }
    if (out) *out = batch;
    return DataResult::Valid;
}
// Short-form encapsulator. Exact in-place payload (ipv6 == out + 40) is safe;
// partially overlapping inputs are not supported. No radio submission here.
inline size_t encodeData(uint8_t *out, size_t capacity, const uint8_t *source,
                         const uint8_t *destination, uint16_t wifiSequence,
                         uint16_t awdlSequence, const uint8_t *ipv6, size_t length) {
    const size_t header = 40;
    if (!out || !source || !destination || !unicastMac(source) ||
        !validIpv6(ipv6, length) || capacity < header + length) return 0;
    uint8_t destBits = 0;
    for (unsigned i = 0; i < 6; ++i) destBits |= destination[i];
    if (!destBits || equalMac(source, destination)) return 0;
    for (size_t i = 0; i < header; ++i) out[i] = 0;
    out[0] = 8;
    const uint8_t bssid[6] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    for (unsigned i = 0; i < 6; ++i) {
        out[4 + i] = destination[i]; out[10 + i] = source[i]; out[16 + i] = bssid[i];
    }
    uint16_t sc = uint16_t(wifiSequence << 4);
    out[22] = uint8_t(sc); out[23] = uint8_t(sc >> 8);
    const uint8_t snap[8] = {0xaa, 0xaa, 3, 0, 0x17, 0xf2, 8, 0};
    for (unsigned i = 0; i < 8; ++i) out[24 + i] = snap[i];
    out[32] = 3; out[33] = 4;
    out[34] = uint8_t(awdlSequence); out[35] = uint8_t(awdlSequence >> 8);
    out[38] = 0x86; out[39] = 0xdd;
    for (size_t i = 0; i < length; ++i) out[header + i] = ipv6[i];
    return header + length;
}
}
#endif

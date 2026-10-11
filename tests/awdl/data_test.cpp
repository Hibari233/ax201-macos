// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlData.h>
#include <AWDL/ItlAwdlABI.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
using namespace ItlAwdl;
using Bytes = std::vector<uint8_t>;
// Synthetic wire fixture, independently laid out (not produced by encodeData).
static Bytes fixture(size_t payload = 4) {
    Bytes b = {8,0,0,0, 0x33,0x33,0,0,0,0xfb, 2,1,2,3,4,5,
               0,0x25,0,0xff,0x94,0x73, 0x30,0x12,
               0xaa,0xaa,3,0,0x17,0xf2,8,0, 3,4,0x34,0x12,0,0,0x86,0xdd};
    b.resize(80 + payload);
    b[40] = 0x60; b[44] = uint8_t(payload >> 8); b[45] = uint8_t(payload);
    b[46] = 59; b[47] = 255; b[48] = 0xfe; b[49] = 0x80;
    b[64] = 0xff; b[65] = 2; b[79] = 0xfb;
    for (size_t i = 0; i < payload; ++i) b[80 + i] = uint8_t(i + 1);
    return b;
}
static Bytes qos(Bytes b, bool ht = false) {
    b[0] = 0x88; b[1] |= ht ? 0x80 : 0;
    b.insert(b.begin() + 24, ht ? 6 : 2, 0);
    return b;
}
static void appendSubframe(Bytes &out, const Bytes &b, bool more) {
    out.insert(out.end(), b.begin() + 4, b.begin() + 16);
    size_t length = b.size() - 24;
    out.push_back(uint8_t(length >> 8)); out.push_back(uint8_t(length));
    out.insert(out.end(), b.begin() + 24, b.end());
    if (more) out.insert(out.end(), (4 - ((14 + length) & 3)) & 3, 0);
}
static void reject(const Bytes &b, bool deaggregated = false) {
    DataBatch untouched; memset(&untouched, 0xa5, sizeof(untouched));
    DataBatch before = untouched;
    assert(decodeData(b.data(), b.size(), deaggregated, &untouched) != DataResult::Valid);
    assert(memcmp(&before, &untouched, sizeof(before)) == 0);
}
int main() {
    auto b = fixture(); DataBatch out = {};
    assert(decodeData(b.data(), b.size(), false, &out) == DataResult::Valid);
    assert(out.count == 1 && out.packets[0].length == 44 && out.packets[0].sequence == 0x1234);
    assert(out.packets[0].ipv6 == b.data() + 40);
    for (size_t n = 0; n < b.size(); ++n) reject(Bytes(b.begin(), b.begin() + n));
    assert(decodeData(nullptr, 100, false, nullptr) == DataResult::Truncated);
    for (auto pair : {std::pair<size_t,uint8_t>{0,0x48}, {1,1}, {1,0x40}, {1,4},
                       {1,0x80}, {16,1}, {22,1}, {25,0}, {27,1}, {32,4},
                       {36,1}, {38,8}, {40,0x40}, {44,0xff}, {45,5}}) {
        auto bad = b; bad[pair.first] = pair.second; reject(bad);
    }
    auto zero = fixture(0); assert(decodeData(zero.data(), zero.size(), false, &out) == DataResult::Valid);
    auto big = fixture(1460); assert(decodeData(big.data(), big.size(), false, &out) == DataResult::Valid);
    reject(fixture(1461));
    auto trailing = b; trailing.push_back(0); reject(trailing);
    for (bool ht : {false, true}) {
        auto q = qos(b, ht);
        assert(decodeData(q.data(), q.size(), false, &out) == DataResult::Valid);
        q[24] = 0x80;
        reject(q); // Hardware-deaggregated must be identified by the descriptor.
        assert(decodeData(q.data(), q.size(), true, &out) == DataResult::Valid);
    }
    reject(b, true);
    auto icmp = fixture(8); icmp[46] = 58;
    const uint8_t echo[8] = {128,0,0x81,0x3b,0,1,0,2};
    memcpy(icmp.data() + 80, echo, sizeof(echo));
    assert(dataChecksum(icmp.data() + 40, 48) == DataChecksum::Valid);
    icmp.back() ^= 1;
    assert(dataChecksum(icmp.data() + 40, 48) == DataChecksum::Invalid);
    icmp[46] = 44;
    assert(dataChecksum(icmp.data() + 40, 48) == DataChecksum::Unavailable);
    auto udp = fixture(8); udp[46] = 17;
    const uint8_t datagram[8] = {0x12,0x34,0x56,0x78,0,8,0x98,0xb3};
    memcpy(udp.data() + 80, datagram, sizeof(datagram));
    assert(dataChecksum(udp.data() + 40, 48) == DataChecksum::Valid);
    udp[86] = udp[87] = 0;
    assert(dataChecksum(udp.data() + 40, 48) == DataChecksum::Invalid);
    udp[84] = 1;
    assert(dataChecksum(udp.data() + 40, 48) == DataChecksum::Invalid);
    auto odd = fixture(9); odd[46] = 17;
    const uint8_t oddDatagram[9] = {0x12,0x34,0x56,0x78,0,9,0xee,0xb0,0xaa};
    memcpy(odd.data() + 80, oddDatagram, sizeof(oddDatagram));
    assert(dataChecksum(odd.data() + 40, 49) == DataChecksum::Valid);
    odd.back() ^= 1;
    assert(dataChecksum(odd.data() + 40, 49) == DataChecksum::Invalid);
    auto lng = b;
    lng.erase(lng.begin() + 36, lng.begin() + 38);
    lng.insert(lng.begin() + 36, {3,2,0xaa,0xbb, 21,1,0xee, 3,0});
    assert(decodeData(lng.data(), lng.size(), false, &out) == DataResult::Valid);
    assert(out.packets[0].length == 44);
    for (size_t n = 36; n < lng.size(); ++n) reject(Bytes(lng.begin(), lng.begin() + n));
    auto badLong = lng; badLong[37] = 255; reject(badLong);
    badLong = lng; badLong[43] = 7; reject(badLong); // Missing closing marker.
    Bytes aggregate = qos(b); aggregate.resize(26); aggregate[24] = 0x80;
    appendSubframe(aggregate, fixture(1), true);
    appendSubframe(aggregate, b, false);
    assert(decodeData(aggregate.data(), aggregate.size(), false, &out) == DataResult::Valid);
    assert(out.count == 2 && out.packets[0].length == 41 && out.packets[1].length == 44);
    auto badAggregate = aggregate; badAggregate.back() = 1; // Contents are opaque IPv6 payload.
    assert(decodeData(badAggregate.data(), badAggregate.size(), false, &out) == DataResult::Valid);
    badAggregate = aggregate; badAggregate.pop_back(); reject(badAggregate);
    badAggregate = aggregate; badAggregate[32] ^= 2; reject(badAggregate); // Foreign subframe source.
    badAggregate = aggregate; badAggregate[38] = 0xff; reject(badAggregate);
    badAggregate = aggregate; badAggregate.push_back(0); reject(badAggregate);
    Bytes many(aggregate.begin(), aggregate.begin() + 26);
    for (unsigned i = 0; i < 17; ++i) appendSubframe(many, b, i != 16);
    assert(decodeData(many.data(), many.size(), false, &out) == DataResult::TooMany);
    uint8_t encoded[1541]; memset(encoded, 0xa5, sizeof(encoded));
    assert(encodeData(encoded, b.size() - 1, b.data() + 10, b.data() + 4,
                      0x123, 0x1234, b.data() + 40, 44) == 0);
    for (auto v : encoded) assert(v == 0xa5);
    assert(encodeData(encoded, sizeof(encoded), b.data() + 10, b.data() + 4,
                      0x123, 0x1234, b.data() + 40, 44) == b.size());
    assert(memcmp(encoded, b.data(), b.size()) == 0 && encoded[b.size()] == 0xa5);
    assert(encodeData(encoded, 1540, big.data() + 10, big.data() + 4,
                      0xffff, 0xffff, big.data() + 40, 1500) == 1540);
    assert(encoded[1540] == 0xa5 && decodeData(encoded, 1540, false, &out) == DataResult::Valid);
    uint32_t rng = 0xace12345;
    for (unsigned k = 0; k < 200000; ++k) {
        Bytes fuzz = (k & 1) ? aggregate : lng;
        auto next = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
        for (unsigned j = 0, count = 1 + next() % 8; j < count; ++j) {
            size_t offset = next() % fuzz.size(); fuzz[offset] = uint8_t(next());
        }
        fuzz.resize(next() % (fuzz.size() + 1));
        if (decodeData(fuzz.data(), fuzz.size(), k & 2, &out) == DataResult::Valid) {
            assert(out.count && out.count <= MaxDataPackets);
            for (size_t j = 0; j < out.count; ++j) {
                auto &v = out.packets[j];
                assert(v.ipv6 >= fuzz.data() && v.ipv6 + v.length <= fuzz.data() + fuzz.size());
                assert(validIpv6(v.ipv6, v.length));
                (void)dataChecksum(v.ipv6, v.length);
            }
        }
    }
    puts("AWDL data codec: wire fixtures, aggregate atomicity, bounds, 200000 mutations passed");
}

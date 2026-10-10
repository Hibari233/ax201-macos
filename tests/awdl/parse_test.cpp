// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlFrame.h>
#include <AWDL/ItlAwdlABI.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <vector>
using namespace ItlAwdl;
static std::vector<uint8_t> valid() {
    std::vector<uint8_t> b(46);
    b[0] = 0xd0; b[24] = 127; b[26] = 0x17; b[27] = 0xf2; b[28] = 8;
    b[29] = 0x10; b[30] = 3; b[32] = 0x78; b[33] = 0x56; b[34] = 0x34; b[35] = 0x12;
    b[40] = 21; b[41] = 3; b[43] = 0x31; b[44] = 4; b[45] = 0;
    return b;
}
int main() {
    auto b = valid(); ActionInfo info = {};
    assert(parseAction(b.data(), b.size(), &info) == ParseResult::Valid);
    assert(info.subtype == 3 && info.tlv_count == 1 && info.phy_tx == 0x12345678);
    for (size_t n = 0; n < 40; ++n) assert(parseAction(b.data(), n, nullptr) == ParseResult::Truncated);
    for (size_t n = 41; n < b.size(); ++n) assert(parseAction(b.data(), n, nullptr) == ParseResult::Truncated);
    b[41] = b[42] = 0xff; assert(parseAction(b.data(), b.size(), nullptr) == ParseResult::Truncated);
    b = valid(); b[25] = 1; assert(parseAction(b.data(), b.size(), nullptr) == ParseResult::NotAwdl);
    b = valid(); b[1] = 0x40; assert(parseAction(b.data(), b.size(), nullptr) == ParseResult::NotAwdl);
    b = valid(); b[30] = 0xff; assert(parseAction(b.data(), b.size(), nullptr) == ParseResult::Invalid);
    b = valid(); b[29] = 0xff; assert(parseAction(b.data(), b.size(), nullptr) == ParseResult::Invalid);
    b = valid(); b[40] = 255; assert(parseAction(b.data(), b.size(), nullptr) == ParseResult::Valid);
    b = valid(); b.insert(b.end(), {255, 0, 0}); assert(parseAction(b.data(), b.size(), &info) == ParseResult::Valid);
    assert(info.tlv_count == 2);
    uint8_t zero[6] = {}, group[6] = {1}, peer[6] = {2, 1, 2, 3, 4, 5};
    assert(!unicastMac(zero) && !unicastMac(group) && unicastMac(peer));
    assert(equalMac(peer, peer) && !equalMac(peer, group));
    uint8_t probe[ProbeLength + 1] = {};
    uint8_t local[6] = {2, 2, 3, 4, 5, 6};
    probe[ProbeLength] = 0xa5;
    assert(!buildProbe(probe, ProbeLength - 1, local, peer, 0, 0));
    assert(!buildProbe(probe, ProbeLength, local, local, 0, 0));
    assert(!buildProbe(probe, ProbeLength, local, group, 0, 0));
    assert(buildProbe(probe, ProbeLength, local, peer, 0xffff, 0x12345678));
    assert(probe[ProbeLength] == 0xa5 && equalMac(probe + 4, peer) && equalMac(probe + 10, local));
    assert(le16(probe + 22) == 0xfff0);
    assert(parseAction(probe, ProbeLength, &info) == ParseResult::Valid);
    assert(info.subtype == 3 && info.tlv_count == 1 && info.target_tx == 0x12345678);
    // Deterministic mutated/truncated corpus; ASan/UBSan catch invalid accesses.
    uint32_t rng = 0x94703;
    for (unsigned i = 0; i < 200000; ++i) {
        b = valid();
        rng = rng * 1664525u + 1013904223u;
        size_t n = rng % 2100;
        b.resize(n);
        for (unsigned j = 0; j < 8 && n; ++j) {
            rng = rng * 1664525u + 1013904223u;
            b[rng % n] = uint8_t(rng >> 24);
        }
        parseAction(b.data(), b.size(), &info);
    }
    puts("AWDL action parser: boundary tests and 200000 mutation cases passed");
}

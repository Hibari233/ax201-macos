// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlEcho.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
using namespace ItlAwdl;
using Bytes = std::vector<uint8_t>;
static const uint8_t self[6] = {2,0x11,0x22,0x33,0x44,0x55};
static const uint8_t peer[6] = {2,0x66,0x77,0x88,0x99,0xaa};
static constexpr uint32_t token = 0x12345678;
// Independently calculated with Python struct/ipaddress and the RFC 4443
// pseudo-header. Odd payload length exercises the final padded octet.
static const char *golden = "6000000000193a40fe80000000000000001122fffe334455fe80000000000000006677fffe8899aa80001e3a12340003123456787c533215705f3e19745b3a1d68";
static Bytes reply(Bytes p) {
    for (unsigned i = 0; i < 16; ++i) { auto a = p[8+i]; p[8+i] = p[24+i]; p[24+i] = a; }
    p[40] = 129;
    // The address sum is unchanged; replacing type 128 with 129 subtracts
    // 0x100 from the one's-complement checksum (with end-around carry).
    uint32_t checksum = uint32_t(be16(p.data()+42)) + 0xfeff;
    checksum = (checksum & 0xffff) + (checksum >> 16);
    p[42] = uint8_t(checksum >> 8); p[43] = uint8_t(checksum);
    return p;
}
int main() {
    Bytes p(65);
    assert(buildEchoIpv6(p.data(), p.size(), self, peer, p.size(), token, 3));
    for (size_t i = 0; i < p.size(); ++i) {
        unsigned byte = 0; assert(sscanf(golden+i*2,"%2x",&byte) == 1); assert(p[i] == byte);
    }
    for (size_t length : {size_t(64),size_t(65),size_t(1280),size_t(1499),size_t(1500)}) {
        p.assign(length, 0xa5);
        assert(buildEchoIpv6(p.data(), p.size(), self, peer, length, token, 3));
        assert(dataChecksum(p.data(), p.size()) == DataChecksum::Valid);
        assert(!matchesEchoReply(p.data(), p.size(), self, peer, length, token, 3));
        auto r = reply(p);
        assert(matchesEchoReply(r.data(), r.size(), self, peer, length, token, 3));
        assert(!matchesEchoReply(r.data(), r.size(), peer, self, length, token, 3));
        assert(!matchesEchoReply(r.data(), r.size(), self, peer, length, token+1, 3));
        assert(!matchesEchoReply(r.data(), r.size(), self, peer, length, token, 2));
        for (size_t n = 0; n < length; ++n)
            assert(!matchesEchoReply(r.data(), n, self, peer, length, token, 3));
        for (size_t i = 8; i < length; ++i) {
            r[i] ^= 1;
            assert(!matchesEchoReply(r.data(), r.size(), self, peer, length, token, 3));
            r[i] ^= 1;
        }
        Bytes packet(40 + length); memcpy(packet.data()+40, p.data(), length);
        assert(encodeData(packet.data(), packet.size(), self, peer, 3, 3,
                          packet.data()+40, length) == packet.size());
        DataBatch decoded = {};
        assert(decodeData(packet.data(), packet.size(), false, &decoded) == DataResult::Valid);
        assert(decoded.count == 1 && decoded.packets[0].length == length);
        assert(memcmp(decoded.packets[0].ipv6, p.data(), length) == 0);
        Bytes guard(length+2, 0xa5), before = guard;
        assert(!buildEchoIpv6(guard.data()+1, length-1, self, peer, length, token, 3));
        assert(guard == before);
        assert(buildEchoIpv6(guard.data()+1, length, self, peer, length, token, 3));
        assert(guard.front() == 0xa5 && guard.back() == 0xa5);
    }
    p.assign(DataMtu+1, 0xa5); auto before = p;
    uint8_t group[6] = {1,2,3,4,5,6};
    assert(!buildEchoIpv6(p.data(),p.size(),self,peer,63,token,0));
    assert(!buildEchoIpv6(p.data(),p.size(),self,peer,1501,token,0));
    assert(!buildEchoIpv6(p.data(),p.size(),self,group,64,token,0));
    assert(!buildEchoIpv6(p.data(),p.size(),self,self,64,token,0));
    assert(!buildEchoIpv6(p.data(),p.size(),self,peer,64,0,0));
    assert(!buildEchoIpv6(nullptr,p.size(),self,peer,64,token,0));
    assert(!matchesEchoReply(nullptr,64,self,peer,64,token,0));
    assert(p == before);
    puts("AWDL echo: independent checksum fixture, odd/even MTU, reply identity/integrity, bounds and in-place encapsulation passed");
}

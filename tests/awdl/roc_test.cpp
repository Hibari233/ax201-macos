// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlRoc.h>
#include <cassert>
#include <cstdio>

static void word(uint8_t *p, uint32_t n) { for (unsigned i=0;i<4;++i) p[i]=uint8_t(n>>(8*i)); }
int main() {
    using ItlAwdl::P2pSessionProtection;
    P2pSessionProtection s;
    uint8_t notification[16] = {};
    word(notification,1); word(notification+4,1); word(notification+8,1); word(notification+12,2);
    assert(!s.arm(3,1023) && !s.arm(3,5000001));
    assert(s.arm(3,3000000) && s.durationTu==2929 && !s.ready(3));
    assert(!s.arm(4,1024)); // A second request must not replace an active lease.
    assert(!s.observe(notification,16,2) && !s.ready(3));
    word(notification,0); assert(!s.observe(notification,16,3) && !s.ready(3));
    word(notification,1); word(notification+12,0);
    assert(!s.observe(notification,16,3) && !s.ready(3));
    word(notification+12,2); assert(s.observe(notification,16,3) && s.ready(3) && !s.ready(4));
    word(notification+8,0); assert(s.observe(notification,16,3) && s.ends==1 && !s.ready(3));
    word(notification+8,1); assert(!s.observe(notification,16,3) && !s.ready(3)); // late start
    s.stop(); assert(!s.observe(notification,16,3) && !s.requested);
    assert(s.arm(4,5000000) && s.durationTu==4882 && s.starts==0);
    word(notification+4,0); assert(s.observe(notification,16,4) && s.failed && !s.ready(4));
    word(notification+4,1); assert(!s.observe(notification,16,4) && !s.ready(4));
    for (size_t length=0;length<32;++length) {
        if (length==16) continue;
        s.stop(); assert(s.arm(5,1024));
        assert(!s.observe(notification,length,5) && s.failed && s.malformed==1);
    }
    s.stop(); assert(s.arm(6,1024));
    assert(!s.observe(nullptr,16,6) && s.failed);
    for (unsigned field=4;field<=8;field+=4) {
        s.stop(); assert(s.arm(7,2048));
        word(notification+field,2);
        assert(!s.observe(notification,16,7) && s.failed && !s.ready(7));
        word(notification+field,1);
    }
    std::puts("P2P session notification: identity, generation, expiry, failure and length boundaries passed");
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlNativeRuntime.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
using namespace ItlAwdl;
int main() {
    const uint8_t mac[6]={2,1,2,3,4,5};
    for (const uint32_t origin : {100000U, 0xfffff000U}) {
        for (const uint8_t presence : {1,2,4,8,16}) {
            LocalTimeline timeline;
            assert(timeline.start(origin,16,presence,0xfff0));
            for(uint32_t elapsed=0;elapsed<5000000;elapsed+=997) {
                LocalSyncBoundary b={};
                assert(timeline.sample(origin+elapsed,&b));
                NativeAvailabilityWindowContext c={};
                assert(nativeRuntimeWindow(b,16,presence,NativeSyncRole::ElectedMaster,mac,mac,6,&c));
                const uint32_t period=16384U*presence;
                // Independent nominal boundary, not software wake time.
                assert(c.nativeAnchorTimeUs-3500U == origin+elapsed/period*period);
                assert(c.counter==uint16_t(0xfff0+elapsed/period*presence));
                // Native calculatePeerTimeDiff's two estimates must agree
                // for an aligned peer at an arbitrary sub-TU receive instant.
                const uint16_t next=uint16_t(b.counter+presence-b.counter%presence);
                const uint32_t localNext=c.nativeAnchorTimeUs-3500U+uint16_t(next-c.counter)*16384U;
                const uint32_t peerNext=b.phyUs+uint32_t(b.downCounterTu)*1024U-(b.phyUs-b.targetUs);
                assert(localNext==peerNext);
                NativeAvailabilityWindowStart event={};
                assert(encodeNativeAvailabilityWindowStart(c,&event));
                const uint32_t clock=(uint32_t(event.bytes[20])<<24)|(uint32_t(event.bytes[21])<<16)|
                    (uint32_t(event.bytes[22])<<8)|event.bytes[23];
                assert(clock==c.nativeAnchorTimeUs);
            }
            timeline.clear(); LocalSyncBoundary b={}; assert(!timeline.sample(origin+5000000,&b));
        }
    }
    LocalTimeline t; assert(t.start(1000,16,4)); LocalSyncBoundary b={}; assert(t.sample(1100,&b));
    NativeAvailabilityWindowContext c={};
    assert(!nativeRuntimeWindow(b,0,4,NativeSyncRole::ElectedMaster,mac,mac,6,&c));
    assert(!nativeRuntimeWindow(b,16,3,NativeSyncRole::ElectedMaster,mac,mac,6,&c));
    assert(!nativeRuntimeWindow(b,16,4,NativeSyncRole::ElectedMaster,nullptr,mac,6,&c));
    assert(!nativeRuntimeWindow(b,16,4,NativeSyncRole::ElectedMaster,mac,mac,0,&c));
    ++b.nextBoundaryUs;
    assert(!nativeRuntimeWindow(b,16,4,NativeSyncRole::ElectedMaster,mac,mac,6,&c));
    puts("Native runtime clock bridge checks passed, including delayed callbacks and both wraps.");
}

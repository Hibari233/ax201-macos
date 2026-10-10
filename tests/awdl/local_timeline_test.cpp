// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlLocalTimeline.h>
#include <AWDL/AwdlSyncAction.h>
#include <assert.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>

using namespace ItlAwdl;

int main() {
    LocalTimeline timeline;
    LocalSyncBoundary output;
    memset(&output, 0x5a, sizeof(output));
    const auto unchanged = output;
    assert(!timeline.sample(123, &output));
    assert(!memcmp(&output, &unchanged, sizeof(output)));
    assert(!timeline.start(1000, 0, 4));
    assert(!timeline.start(1000, 1025, 4));
    for (uint8_t presence : {uint8_t(0),uint8_t(3),uint8_t(5),uint8_t(17)})
        assert(!timeline.start(1000, 16, presence));
    assert(!timeline.start(1000, 16, 4, 1));

    // Independent expected boundaries, including exact and fractional TUs,
    // AW and EAW edges, and a complete 16-slot schedule wrap.
    assert(timeline.start(1000, 16, 4));
    struct Case { uint32_t elapsed, remaining, target; uint16_t counter, down; uint8_t slot; };
    const Case cases[] = {
        {0,65536,1000,0,64,0}, {1,65535,1000,0,64,0},
        {1023,64513,1000,0,64,0}, {1024,64512,2024,0,63,0},
        {16383,49153,16360,0,49,0}, {16384,49152,17384,1,48,0},
        {65535,1,65512,3,1,0}, {65536,65536,66536,4,64,1},
        {524288,65536,525288,32,64,8}, {1048576,65536,1049576,64,64,0}
    };
    for (const auto &c : cases) {
        assert(timeline.sample(1000+c.elapsed,&output));
        assert(output.remainingUs==c.remaining && output.targetUs==c.target);
        assert(output.counter==c.counter && output.downCounterTu==c.down && output.slot==c.slot);
        assert(output.phyUs==1000+c.elapsed && output.nextBoundaryUs==1000+c.elapsed+c.remaining);
    }
    const auto last=output;
    assert(!timeline.sample(1000,&output));
    assert(!memcmp(&last,&output,sizeof(output)));
    assert(!timeline.sample(1000+30000001,&output));
    assert(!timeline.start(3,0,4)); // Rejected reconfiguration preserves epoch.
    assert(timeline.sample(1000+1048577,&output));
    timeline.clear();
    assert(!timeline.sample(1000+1048578,&output));

    // Firmware GP2 and 16-bit AW counters wrap independently.
    assert(timeline.start(0xfffff000,16,4,0xfffc));
    assert(timeline.sample(0x0000f000,&output));
    assert(output.counter==0 && output.slot==0 && output.downCounterTu==64);
    assert(output.phyUs==0xf000 && output.targetUs==0xf000 && output.nextBoundaryUs==0x1f000);

    NativeBssidConfiguration network;
    const uint8_t identifiers[]={0x00,0x25,0x00,0xff,0x94,0x73,0x00,0x25,0x00,0xff,0x94,0x74};
    assert(network.replace(1,identifiers,identifiers+6)==BssidResult::Stored);
    ActionTemplate extra;
    const uint8_t version[]={21,2,0,0x10,1};
    assert(extra.replace(version,sizeof(version)));
    for (uint8_t presence : {uint8_t(1),uint8_t(2),uint8_t(4),uint8_t(8),uint8_t(16)}) {
        assert(timeline.start(0xfffff000,16,presence));
        for (uint32_t elapsed=0; elapsed<=1500000; elapsed+=997) {
            assert(timeline.sample(0xfffff000+elapsed,&output));
            SyncActionContext context={};
            const uint8_t source[]={2,1,2,3,4,5};
            memcpy(context.source,source,6); memset(context.destination,0xff,6);
            context.clockValid=true; context.phyTimeUs=output.phyUs; context.targetTimeUs=output.targetUs;
            SyncActionParameters p={};
            p.awPeriodTu=p.extensionTu=p.commonTu=16; p.actionPeriodTu=110;
            p.presence=presence; p.counter=output.counter; p.downCounterTu=output.downCounterTu;
            memcpy(p.master,source,6); p.plan.step_aw=presence;
            for(unsigned i=0;i<16;i++) p.plan.channels[i]=6;
            p.masterChannel=p.nextChannel=6;
            uint8_t frame[SyncActionMaxLength];
            auto length=buildSyncAction(context,p,extra,network,frame,sizeof(frame));
            assert(length);
            SyncAnnouncement decoded={};
            assert(parseSchedule(frame,length,&decoded)==ScheduleResult::Valid);
            // Receiver's independently decoded down-counter/delay must recover
            // exactly the generated hardware-clock boundary, including wrap.
            const int64_t remaining=int64_t(decoded.down_counter_tu)*1024-decoded.tx_delay_us;
            assert(remaining==output.remainingUs);
            assert(uint32_t(output.phyUs+remaining)==output.nextBoundaryUs);
            assert(decoded.aw_counter==output.counter);
        }
    }
    puts("Local AW timeline: exact boundaries, fractional TUs, counter/clock wrap and wire reconstruction passed");
}

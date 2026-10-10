// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlNativeSyncParameters.h>
#include <AWDL/AwdlLocalTimeline.h>
#include <array>
#include <assert.h>
#include <stdio.h>
#include <string.h>

using namespace ItlAwdl;
using Request = std::array<uint32_t, 6>;
int main() {
    NativeSyncParameters owned;
    auto replace = [&](const Request &r) { return owned.replace(r[0], r[1], r[2], r[3], r[4], r[5]); };
    const Request defaults = {{0, 16, 16, 16, 110, 0}};
    SyncActionParameters sync = {};
    sync.awPeriodTu = 77;
    assert(!owned.ready() && !owned.apply(sync) && sync.awPeriodTu == 77 && !owned.matches(sync));
    auto input = defaults;
    assert(replace(input) == NativeSyncResult::Stored);
    input[2] = 32;
    assert(owned.periodTu() == 16 && owned.apply(sync) && owned.matches(sync));
    for (unsigned field = 0; field < 6; ++field) {
        for (uint32_t value : {0U, 1U, 2U, 15U, 16U, 32U, 109U, 110U, 111U, 65535U, 65536U, 0xffffffffU}) {
            auto bad = defaults; bad[field] = value;
            const bool accepted = bad == defaults || (field == 0 && value == 1);
            const bool invalid = bad[0] > 1 || !bad[1] || !bad[2] || !bad[3] || !bad[4] ||
                bad[1] > 65535 || bad[2] > 65535 || bad[3] > 65535 || bad[4] > 65535;
            assert(replace(bad) == (accepted ? NativeSyncResult::Stored :
                invalid ? NativeSyncResult::Invalid : NativeSyncResult::Unsupported));
            assert(owned.ready() && owned.lengthTu() == 16 && owned.periodTu() == 16 &&
                owned.extensionTu() == 16 && owned.framePeriodTu() == 110 && owned.matches(sync));
        }
    }
    // Independent wire positions and elapsed-clock expectation couple the
    // native timing snapshot to both the encoder and the local AW timeline.
    NativeBssidConfiguration network;
    assert(network.replace(1, DefaultAwdlBssid, DefaultCompanionBssid) == BssidResult::Stored);
    ActionTemplate extra;
    SyncActionContext context = {{2,1,2,3,4,5}, {0xff,0xff,0xff,0xff,0xff,0xff}, 0, 0, 0, 0, true};
    sync.masterChannel = sync.nextChannel = 6;
    sync.presence = sync.plan.step_aw = 4;
    for (auto &channel : sync.plan.channels) channel = 6;
    memcpy(sync.master, context.source, 6);
    LocalTimeline timeline;
    assert(timeline.start(0xfffff000, owned.periodTu(), sync.presence));
    LocalSyncBoundary boundary = {};
    assert(timeline.sample(uint32_t(0xfffff000 + 100000ULL), &boundary));
    assert(boundary.counter == 6 && boundary.slot == 1 && boundary.remainingUs == 31072);
    context.phyTimeUs = boundary.phyUs; context.targetTimeUs = boundary.targetUs;
    sync.counter = boundary.counter; sync.downCounterTu = boundary.downCounterTu;
    uint8_t frame[SyncActionMaxLength]; memset(frame, 0xa5, sizeof(frame));
    assert(buildSyncAction(context, sync, extra, network, frame, sizeof(frame)) == 100);
    assert(frame[100] == 0xa5);
    assert(le16(frame + 48) == 16 && le16(frame + 50) == 110 &&
        le16(frame + 54) == 16 && le16(frame + 56) == 16);
    SyncAnnouncement decoded = {};
    assert(parseSchedule(frame, 100, &decoded) == ScheduleResult::Valid);
    assert(decoded.aw_period_tu == 16 && decoded.aw_counter == 6 &&
        uint32_t(decoded.down_counter_tu) * TuUs - decoded.tx_delay_us == 31072);
    auto preserved = sync;
    owned.clear();
    assert(!owned.ready() && !owned.periodTu() && !owned.apply(sync) && !owned.matches(sync));
    assert(!memcmp(&preserved, &sync, sizeof(sync)));
    puts("Native timing: qualified bounds, transactional preservation, wire and wraparound timeline passed");
}

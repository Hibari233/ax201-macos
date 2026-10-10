// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlNativeChannelSequence.h>
#include <AWDL/AwdlSyncAction.h>
#include <array>
#include <assert.h>
#include <stdio.h>
#include <string.h>

using namespace ItlAwdl;
using Request = std::array<uint8_t, NativeChannelSequence::RequestLength>;

static Request specimen() {
    Request r = {};
    r[5] = 16; r[7] = 3; r[9] = 0xff;
    // Explicit native channel records: version, primary channel, band flags.
    for (unsigned i = 0; i < 16; ++i) {
        r[12 + 12 * i] = 1;
        r[16 + 12 * i] = 6;
        r[20 + 12 * i] = 8;
    }
    return r;
}

int main() {
    NativeChannelSequence state;
    Request out; out.fill(0xa5);
    const auto untouched = out;
    assert(!state.copyRequest(out.data(), out.size()) && out == untouched);
    auto input = specimen();
    assert(state.replace(input.data(), input.size()) == NativeChannelResult::Stored && state.ready());
    input[16] = 1;
    assert(state.plan().channels[0] == 6); // Own the producer's data.
    assert(state.copyRequest(out.data(), out.size()));
    Request expected = specimen(); expected[0] = 1;
    assert(out == expected && state.plan().encoding == 0 && state.plan().step_aw == 4);
    for (unsigned i = 0; i < 16; ++i)
        assert(state.plan().channels[i] == 6 && !state.plan().operating_classes[i]);

    auto reject = [&](const uint8_t *bytes, size_t length, NativeChannelResult result) {
        assert(state.replace(bytes, length) == result);
        Request preserved;
        assert(state.ready() && state.copyRequest(preserved.data(), preserved.size()) && preserved == expected);
    };
    reject(nullptr, 400, NativeChannelResult::Invalid);
    for (size_t n = 0; n < 400; ++n) {
        // Allocate the claimed extent, so ASan detects accidental overreads.
        auto p = new uint8_t[n];
        if (n) memcpy(p, expected.data(), n);
        reject(p, n, NativeChannelResult::Invalid);
        delete[] p;
    }
    reject(expected.data(), 401, NativeChannelResult::Invalid);
    auto bad = expected; bad[0] = 2; reject(bad.data(), 400, NativeChannelResult::Invalid);
    for (uint8_t length : {uint8_t(0), uint8_t(33), uint8_t(255)}) {
        bad = expected; bad[5] = length; reject(bad.data(), 400, NativeChannelResult::Invalid);
    }
    for (uint8_t length : {uint8_t(1), uint8_t(15), uint8_t(17), uint8_t(32)}) {
        bad = expected; bad[5] = length; reject(bad.data(), 400, NativeChannelResult::Unsupported);
    }
    bad = expected; bad[6] = 2; reject(bad.data(), 400, NativeChannelResult::Invalid);
    bad = expected; bad[6] = 1; reject(bad.data(), 400, NativeChannelResult::Unsupported);
    for (unsigned offset : {4U, 7U, 8U, 9U, 10U, 11U, 396U, 397U, 398U, 399U}) {
        bad = expected;
        bad[offset] = offset == 4 ? 0x10 : offset == 7 ? 2 : offset == 9 ? 6 : 1;
        reject(bad.data(), 400, NativeChannelResult::Unsupported);
    }
    for (uint8_t flags : {uint8_t(0x20), uint8_t(0x40), uint8_t(0x80)}) {
        bad = expected; bad[4] = flags; reject(bad.data(), 400, NativeChannelResult::Unsupported);
    }
    // A bad last record must not install the otherwise valid first 15 slots.
    constexpr unsigned last = 12 + 15 * 12;
    bad = expected; bad[last] = 2; reject(bad.data(), 400, NativeChannelResult::Invalid);
    bad = expected; bad[last + 5] = 1; reject(bad.data(), 400, NativeChannelResult::Invalid);
    bad = expected; bad[last + 4] = 15; reject(bad.data(), 400, NativeChannelResult::Invalid);
    bad = expected; bad[last + 8] = 0x10; reject(bad.data(), 400, NativeChannelResult::Invalid);
    bad = expected; bad[last + 8] = 0x0c; reject(bad.data(), 400, NativeChannelResult::Unsupported);
    bad = expected; bad[last + 9] = 0x20; reject(bad.data(), 400, NativeChannelResult::Unsupported);
    bad = expected; bad[last + 4] = 0; reject(bad.data(), 400, NativeChannelResult::Unsupported);

    // Unused native records are not read as active slots or leaked on readback.
    input = specimen();
    memset(input.data() + 204, 0xa5, 192);
    assert(state.replace(input.data(), input.size()) == NativeChannelResult::Stored);
    assert(state.copyRequest(out.data(), out.size()) && out == expected);
    for (unsigned step : {1U, 2U, 4U, 8U, 16U}) {
        input = specimen(); input[7] = uint8_t(step - 1);
        input[4] = 3; // Ignored bandwidth policy in native encoding 0.
        assert(state.replace(input.data(), input.size()) == NativeChannelResult::Stored);
        assert(state.plan().step_aw == step);
    }
    input = specimen();
    const uint8_t channels[] = {0, 1, 6, 14, 36, 44, 52, 64, 100, 144, 149, 165, 177, 0, 6, 6};
    for (unsigned i = 0; i < 16; ++i) {
        input[16 + 12 * i] = channels[i];
        nativeMetadataPut32(input.data() + 20 + 12 * i, nativeActionChannelFlags(channels[i]));
    }
    assert(state.replace(input.data(), input.size()) == NativeChannelResult::Stored);
    assert(!memcmp(state.plan().channels, channels, 16));

    // Independent wire positions ensure 12-byte native records, including
    // unavailable slots, become the 16 channel bytes in synchronization TLV 4.
    NativeBssidConfiguration network;
    assert(network.replace(1, DefaultAwdlBssid, DefaultCompanionBssid) == BssidResult::Stored);
    ActionTemplate extra;
    SyncActionContext context = {{2,1,2,3,4,5}, {0xff,0xff,0xff,0xff,0xff,0xff}, 0, 1, 100000, 100000, true};
    SyncActionParameters sync = {};
    sync.masterChannel = sync.nextChannel = 6; sync.presence = 4;
    sync.awPeriodTu = sync.downCounterTu = sync.extensionTu = sync.commonTu = 16;
    sync.actionPeriodTu = 110; sync.plan = state.plan();
    memcpy(sync.master, context.source, 6);
    uint8_t frame[SyncActionMaxLength]; memset(frame, 0xa5, sizeof(frame));
    assert(buildSyncAction(context, sync, extra, network, frame, sizeof(frame)) == 100);
    const uint8_t wireHeader[] = {15, 0, 0, 3, 0xff, 0xff};
    assert(!memcmp(frame + 76, wireHeader, 6) && !memcmp(frame + 82, channels, 16));
    assert(frame[100] == 0xa5);
    SyncAnnouncement decoded = {};
    assert(parseSchedule(frame, 100, &decoded) == ScheduleResult::Valid);
    assert(equalChannelPlan(decoded.plan, state.plan()));
    out.fill(0xa5);
    assert(!state.copyRequest(nullptr, out.size()));
    assert(!state.copyRequest(out.data(), out.size() - 1) && out == untouched);
    state.clear();
    assert(!state.ready() && !state.plan().step_aw);
    for (auto channel : state.plan().channels) assert(!channel);
    assert(!state.copyRequest(out.data(), out.size()) && out == untouched);
    puts("Native channel sequence: bounds, owned snapshots, preserved failures and wire translation passed");
}

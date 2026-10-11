// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlNativeEvents.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {
    using namespace ItlAwdl;
    NativeAvailabilityWindowContext window = {0x1234, NativeSyncRole::Follower,
        {2, 1, 2, 3, 4, 5}, {2, 6, 7, 8, 9, 10}, 149, 0x12345678, true};
    NativeAvailabilityWindowStart encoded = {};
    assert(encodeNativeAvailabilityWindowStart(window, &encoded));
    // Golden representation from independently audited producer/consumer
    // offsets. The event clock is big endian; both surrounding words are LE.
    const uint8_t expected[36] = {
        0x34, 0x12, 0, 0, 2, 0, 0, 0,
        2, 1, 2, 3, 4, 5, 2, 6, 7, 8, 9, 10,
        0x12, 0x34, 0x56, 0x78,
        1, 0, 0, 0, 149, 0, 0, 0, 0x12, 0, 0, 0
    };
    assert(!memcmp(encoded.bytes, expected, sizeof(expected)));
    for (uint32_t role = 1; role <= 3; ++role) {
        window.role = static_cast<NativeSyncRole>(role);
        assert(encodeNativeAvailabilityWindowStart(window, &encoded));
        assert(le32(encoded.bytes + 4) == role);
    }
    // Neither a counter wrap nor a valid zero hardware clock is absence.
    window.counter = 0; window.nativeAnchorTimeUs = 0;
    window.primaryChannel = 6;
    assert(encodeNativeAvailabilityWindowStart(window, &encoded));
    assert(!le32(encoded.bytes) && !le32(encoded.bytes + 20));
    assert(le32(encoded.bytes + 28) == 6 && le32(encoded.bytes + 32) == 0x0a);
    window.counter = 0xffff; window.nativeAnchorTimeUs = 0xffffffff;
    window.primaryChannel = 52;
    assert(encodeNativeAvailabilityWindowStart(window, &encoded));
    assert(le32(encoded.bytes) == 0xffff && le32(encoded.bytes + 20) == 0xffffffff);
    assert(le32(encoded.bytes + 32) == 0x112);

    auto rejected = [&](const NativeAvailabilityWindowContext &bad) {
        memset(encoded.bytes, 0xa5, sizeof(encoded.bytes));
        assert(!encodeNativeAvailabilityWindowStart(bad, &encoded));
        for (auto byte : encoded.bytes) assert(!byte);
    };
    auto bad = window; bad.nativeAnchorValid = false; rejected(bad);
    bad = window; bad.role = static_cast<NativeSyncRole>(0); rejected(bad);
    bad.role = static_cast<NativeSyncRole>(4); rejected(bad);
    bad.role = static_cast<NativeSyncRole>(0xffffffff); rejected(bad);
    bad = window; bad.primaryChannel = 0; rejected(bad);
    bad.primaryChannel = 15; rejected(bad);
    bad.primaryChannel = 178; rejected(bad);
    bad = window; memset(bad.master, 0, 6); rejected(bad);
    bad = window; bad.master[0] |= 1; rejected(bad);
    bad = window; memset(bad.topMaster, 0, 6); rejected(bad);
    bad = window; bad.topMaster[0] |= 1; rejected(bad);
    assert(!encodeNativeAvailabilityWindowStart(window, nullptr));
    // Input context and its MACs are not borrowed by the resulting event.
    assert(encodeNativeAvailabilityWindowStart(window, &encoded));
    window.master[1] ^= 0xff;
    assert(encoded.bytes[9] == 1);
    puts("Native AW event encoding checks passed; no scheduler/event delivery claim.");
}

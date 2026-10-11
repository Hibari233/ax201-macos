// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlNativeFrame.h>
#include <AWDL/AwdlRxClock.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main() {
    using namespace ItlAwdl;
    uint8_t frame[NativeActionMaxWireLength + 1] = {};
    const uint8_t source[6] = {2, 1, 2, 3, 4, 5}, destination[6] = {2, 6, 7, 8, 9, 10};
    uint8_t metadata[NativeActionMetadataLength] = {};
    memcpy(metadata, source, 6);
    assert(buildProbe(frame, sizeof(frame), source, destination, 7, 42));
    NativeActionView view = {};
    assert(nativeActionView(frame, ProbeLength, metadata, sizeof(metadata), &view));
    assert(view.body == frame + 24 && view.length == ProbeLength - 24);
    for (size_t n = 0; n < 40; ++n) {
        assert(!nativeActionView(frame, n, metadata, sizeof(metadata), &view));
        assert(!view.body && !view.length);
    }
    for (size_t n = 0; n < NativeActionMetadataLength; ++n)
        assert(!nativeActionView(frame, ProbeLength, metadata, n, &view));
    assert(!nativeActionView(frame, ProbeLength, metadata, sizeof(metadata) + 1, &view));
    assert(!nativeActionView(nullptr, ProbeLength, metadata, sizeof(metadata), &view));
    assert(!nativeActionView(frame, ProbeLength, nullptr, sizeof(metadata), &view));
    assert(!nativeActionView(frame, ProbeLength, metadata, sizeof(metadata), nullptr));
    metadata[5] ^= 1;
    assert(!nativeActionView(frame, ProbeLength, metadata, sizeof(metadata), &view));
    metadata[5] ^= 1;
    frame[1] |= 4; // More fragments.
    assert(!nativeActionView(frame, ProbeLength, metadata, sizeof(metadata), &view));
    frame[1] &= ~4;
    frame[22] |= 1; // Non-first fragment.
    assert(!nativeActionView(frame, ProbeLength, metadata, sizeof(metadata), &view));
    frame[22] &= ~15;
    frame[41] = 3; // Truncated TLV body.
    assert(!nativeActionView(frame, ProbeLength, metadata, sizeof(metadata), &view));
    frame[41] = 2;
    frame[10] |= 1; metadata[0] |= 1;
    assert(!nativeActionView(frame, ProbeLength, metadata, sizeof(metadata), &view));
    frame[10] &= ~1; metadata[0] &= ~1;
    // A well-formed opaque TLV at the size limit, and one byte beyond it.
    const unsigned payload = NativeActionMaxWireLength - 43;
    frame[41] = uint8_t(payload); frame[42] = uint8_t(payload >> 8);
    assert(nativeActionView(frame, NativeActionMaxWireLength, metadata, sizeof(metadata), &view));
    assert(view.length == NativeActionMaxWireLength - 24);
    frame[41] = uint8_t(payload + 1); frame[42] = uint8_t((payload + 1) >> 8);
    assert(!nativeActionView(frame, sizeof(frame), metadata, sizeof(metadata), &view));
    assert(!view.body && !view.length);
    // A sync TLV must satisfy the native fixed-header minimum even when the
    // outer TLV bounds pass. Presence and AW period cannot be zero.
    frame[40] = 4; frame[41] = 39; frame[42] = 0;
    frame[48] = 16; frame[70] = 4;
    assert(nativeActionView(frame, 82, metadata, sizeof(metadata), &view));
    frame[70] = 0;
    assert(!nativeActionView(frame, 82, metadata, sizeof(metadata), &view));
    frame[70] = 4; frame[48] = 0;
    assert(!nativeActionView(frame, 82, metadata, sizeof(metadata), &view));
    frame[48] = 16;
    for (unsigned n = 0; n < 39; ++n) {
        frame[41] = uint8_t(n);
        assert(parseAction(frame, 43 + n, nullptr) == ParseResult::Valid);
        assert(!nativeActionView(frame, 43 + n, metadata, sizeof(metadata), &view));
    }
    frame[41] = 39;
    memcpy(frame + 82, frame + 40, 42);
    assert(!nativeActionView(frame, 124, metadata, sizeof(metadata), &view));

    NativeActionRxContext rx = {149, -47, 0x12345678, true};
    NativeActionMetadata encoded = {};
    assert(encodeNativeActionMetadata(source, rx, &encoded));
    uint8_t expected[72] = {};
    memcpy(expected, source, 6);
    expected[12] = 1; expected[16] = 149; expected[20] = 0x12;
    expected[44] = 0xd1; expected[45] = expected[46] = expected[47] = 0xff;
    expected[48] = 0x78; expected[49] = 0x56; expected[50] = 0x34; expected[51] = 0x12;
    assert(!memcmp(encoded.bytes, expected, sizeof(expected)));
    const uint32_t channels[] = {1, 6, 12, 14, 36, 44, 52, 64, 100, 144, 149, 177};
    const uint32_t flags[] = {10, 10, 10, 10, 18, 18, 274, 274, 274, 274, 18, 18};
    for (unsigned i = 0; i < sizeof(channels) / sizeof(channels[0]); ++i) {
        rx.primaryChannel = channels[i];
        assert(encodeNativeActionMetadata(source, rx, &encoded));
        assert(le32(encoded.bytes + 16) == channels[i]);
        assert(le32(encoded.bytes + 20) == flags[i]);
    }
    const uint32_t invalidChannels[] = {0U, 15U, 35U, 65U, 99U, 145U, 178U, 0xffffffffU};
    for (auto channel : invalidChannels) {
        rx.primaryChannel = channel;
        assert(!encodeNativeActionMetadata(source, rx, &encoded));
        for (auto byte : encoded.bytes) assert(!byte);
    }
    rx.primaryChannel = 6;
    rx.localMacTimeUs = 0;
    assert(encodeNativeActionMetadata(source, rx, &encoded)); // Valid wrap.
    rx.localMacTimeValid = false;
    assert(!encodeNativeActionMetadata(source, rx, &encoded));
    rx.localMacTimeValid = true;
    rx.rssiDbm = -128;
    assert(!encodeNativeActionMetadata(source, rx, &encoded));
    rx.rssiDbm = 1;
    assert(!encodeNativeActionMetadata(source, rx, &encoded));
    rx.rssiDbm = -127;
    assert(encodeNativeActionMetadata(source, rx, &encoded));
    assert(!encodeNativeActionMetadata(nullptr, rx, &encoded));
    assert(!encodeNativeActionMetadata(source, rx, nullptr));
    uint8_t badSource[6] = {};
    assert(!encodeNativeActionMetadata(badSource, rx, &encoded));
    badSource[0] = 1;
    assert(!encodeNativeActionMetadata(badSource, rx, &encoded));

    auto clock = receiveClockSample(0xffffffff, 0x123456789abcdef0ULL, false);
    assert(clock.flags == 3 && clock.gp2Us == 0xffffffff && clock.tsfUs == 0x123456789abcdef0ULL);
    clock = receiveClockSample(0, 0x123456789abcdef0ULL, true);
    assert(clock.flags == 1 && !clock.gp2Us && !clock.tsfUs);
    clock = receiveClockSample(0, 0, false);
    assert(clock.flags == 3 && !clock.gp2Us && !clock.tsfUs); // Zero is not invalid.
    puts("Native RX encoding/boundary checks passed; no RF/backend integration claim.");
}

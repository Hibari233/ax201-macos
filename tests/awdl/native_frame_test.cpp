// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlNativeFrame.h>
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
    puts("Native action boundary checks passed; no native metadata/RF execution claim.");
}

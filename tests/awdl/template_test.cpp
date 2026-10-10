// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlActionTemplate.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main() {
    ItlAwdl::ActionTemplate store;
    assert(!store.length() && !store.revision());
    uint8_t producer[] = {21, 2, 0, 0x10, 1, 255, 1, 0, 42};
    const uint8_t expected[] = {21, 2, 0, 0x10, 1, 255, 1, 0, 42};
    assert(store.replace(producer, sizeof(producer)));
    memset(producer, 0xaa, sizeof(producer));
    assert(store.length() == sizeof(expected) && store.revision() == 1);
    assert(!memcmp(store.bytes(), expected, sizeof(expected)));
    for (size_t i = sizeof(expected); i < store.Capacity; ++i) assert(!store.bytes()[i]);
    uint8_t maximum[513] = {255, 0xfd, 1}; // 509-byte opaque TLV payload.
    assert(!store.replace(maximum, sizeof(maximum)));
    assert(!store.replace(nullptr, 5));
    for (size_t n = 0; n < 5; ++n) assert(!store.replace(expected, n));
    for (size_t n = 6; n < sizeof(expected); ++n) assert(!store.replace(expected, n));
    assert(store.revision() == 1 && store.length() == sizeof(expected));
    assert(!memcmp(store.bytes(), expected, sizeof(expected)));
    assert(store.replace(maximum, 512));
    assert(store.length() == 512 && store.revision() == 2);
    assert(!memcmp(store.bytes(), maximum, 512));
    assert(store.replace(expected, sizeof(expected)));
    assert(store.replace(store.bytes() + 5, 4)); // Overlapping view, including clear tail.
    assert(store.revision() == 4 && store.length() == 4);
    assert(!memcmp(store.bytes(), expected + 5, 4));
    for (size_t i = 4; i < store.Capacity; ++i) assert(!store.bytes()[i]);
    const auto revision = store.revision();
    store.clear();
    assert(!store.length() && store.revision() != revision);
    for (size_t i = 0; i < store.Capacity; ++i) assert(!store.bytes()[i]);
    // A malformed later TLV cannot commit an otherwise valid first TLV.
    const uint8_t bad[] = {21, 2, 0, 0x10, 1, 255, 2, 0, 42};
    assert(!store.replace(bad, sizeof(bad)) && !store.length());
    puts("Action template: owned snapshot, bounds, atomic rejection, overlap and clear passed.");
}

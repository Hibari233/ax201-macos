// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/AwdlActionTemplate.h>
#include <AWDL/AwdlTemplateProfile.h>
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
    using namespace ItlAwdl;
    const uint8_t source[6] = {2, 1, 2, 3, 4, 5};
    const uint8_t master[6] = {2, 6, 7, 8, 9, 10};
    TemplateProfile profile = {};
    assert(!profileActionTemplate(store, source, master, 7, &profile));
    // Distinct infra and AWDL addresses catch incorrect conditional offsets.
    const uint8_t dataPath[] = {
        12, 36, 0, 0x17, 0x93, 'X', '0', 0, 0x15, 0,
        2, 20, 21, 22, 23, 24, 6, 0, 2, 30, 31, 32, 33, 34,
        2, 1, 2, 3, 4, 5, 0, 0, 3, 0, 7, 8, 9, 0x40, 0
    };
    static_assert(sizeof(dataPath) == 39, "complete data-path fixture");
    assert(store.replace(dataPath, sizeof(dataPath)));
    assert(profileActionTemplate(store, source, master, 7, &profile));
    assert(profile.length == sizeof(dataPath) && profile.tlvs == 1);
    assert(profile.dataPath.count == 1 && profile.dataPath.prefixValid);
    assert(profile.dataPath.flags == 0x9317 && profile.dataPath.socialChannels == 0x15);
    assert(profile.dataPath.extensionFlags == 0x40 && !profile.dataPath.trailingBytes);
    assert(profile.dataPath.awdlAddressPresent && profile.dataPath.awdlAddressUnicast);
    assert(profile.dataPath.awdlAddressMatchesSource);
    uint8_t shortened[sizeof(dataPath)];
    for (unsigned n = 0; n < 36; ++n) {
        memcpy(shortened, dataPath, sizeof(dataPath)); shortened[1] = uint8_t(n);
        assert(store.replace(shortened, 3 + n)); // Valid outer TLV, short inner fields.
        assert(profileActionTemplate(store, source, master, 7, &profile));
        assert(!profile.dataPath.prefixValid);
    }
    memcpy(shortened, dataPath, sizeof(dataPath));
    shortened[32] = shortened[33] = 0xff; // Oversized variable options.
    assert(store.replace(shortened, sizeof(shortened)));
    assert(profileActionTemplate(store, source, master, 7, &profile));
    assert(!profile.dataPath.prefixValid);
    // No explicit AWDL address is not malformed; unknown extensions stay opaque.
    const uint8_t implicit[] = {12, 6, 0, 0, 0x80, 0x40, 0, 0xaa, 0xbb};
    assert(store.replace(implicit, sizeof(implicit)));
    assert(profileActionTemplate(store, source, master, 7, &profile));
    assert(profile.dataPath.prefixValid && !profile.dataPath.awdlAddressPresent);
    assert(profile.dataPath.trailingBytes == 2 && profile.dataPath.extensionFlags == 0x40);
    uint8_t election[] = {
        24, 40, 0, 2, 6, 7, 8, 9, 10, 2, 1, 2, 3, 4, 5,
        11, 0, 0, 0, 2, 0, 0, 0, 13, 0, 0, 0, 7, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 17, 0, 0, 0,
        21, 2, 0, 0x34, 2, 6, 0, 0, 21, 2, 0, 0x10, 1
    };
    assert(store.replace(election, sizeof(election)));
    assert(profileActionTemplate(store, source, master, 7, &profile));
    const auto &e = profile.election;
    assert(e.prefixValid && e.count == 1 && e.length == 40);
    assert(e.masterUnicast && e.syncUnicast && e.masterMatchesSyncRole);
    assert(!e.masterMatchesSource && e.syncMatchesSource && !e.syncMatchesMaster);
    assert(e.masterCounter == 11 && e.selfCounter == 17 && e.distance == 2);
    assert(e.masterMetric == 13 && e.selfMetric == 7 && e.selfMetricMatchesBackend);
    assert(profile.version.count == 2 && profile.version.prefixValid && profile.version.version == 0x34);
    assert(profile.version.deviceClass == 2 && profile.serviceCount == 1 && !profile.serviceLength);
    assert(profileActionTemplate(store, source, source, 8, &profile));
    assert(!profile.election.masterMatchesSyncRole && !profile.election.selfMetricMatchesBackend);
    for (unsigned n = 0; n < 40; ++n) {
        election[1] = uint8_t(n);
        assert(store.replace(election, 3 + n));
        assert(profileActionTemplate(store, source, master, 7, &profile));
        assert(!profile.election.prefixValid);
    }
    assert(!profileActionTemplate(store, nullptr, master, 7, &profile));
    assert(!profile.length && !profile.tlvs);
    assert(!profileActionTemplate(store, source, nullptr, 7, &profile));
    assert(!profileActionTemplate(store, source, master, 7, nullptr));
    puts("Action template ownership and address-free field profiling checks passed.");
}

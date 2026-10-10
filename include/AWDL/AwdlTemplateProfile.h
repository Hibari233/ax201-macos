// SPDX-License-Identifier: GPL-2.0-or-later
// Address-free inspection of an owned native supplementary template.
// Layout references: OWL frame.h and Wireshark packet-awdl.c. This reports
// known fields, not receiver acceptance or validity of unknown extensions.
#pragma once
#include "AwdlActionTemplate.h"

namespace ItlAwdl {
struct TemplateProfile {
    uint32_t length = 0, tlvs = 0;
    struct DataPath {
        uint32_t count = 0, length = 0, trailingBytes = 0;
        uint16_t flags = 0, socialChannels = 0, extensionFlags = 0;
        bool prefixValid = false, awdlAddressPresent = false;
        bool awdlAddressUnicast = false, awdlAddressMatchesSource = false;
    } dataPath;
    struct Election {
        uint32_t count = 0, length = 0, masterCounter = 0, selfCounter = 0;
        uint32_t distance = 0, masterMetric = 0, selfMetric = 0;
        bool prefixValid = false, masterUnicast = false, syncUnicast = false;
        bool masterMatchesSource = false, masterMatchesSyncRole = false;
        bool syncMatchesSource = false, syncMatchesMaster = false;
        bool selfMetricMatchesBackend = false;
    } election;
    struct Version {
        uint32_t count = 0, length = 0;
        uint8_t version = 0, deviceClass = 0;
        bool prefixValid = false;
    } version;
    uint32_t serviceCount = 0, serviceLength = 0;
};

inline bool profileDataPath(const uint8_t *p, size_t length, const uint8_t *source,
                            TemplateProfile::DataPath &out) {
    if (length < 2) return false;
    out.flags = le16(p);
    size_t cursor = 2;
    const auto take = [&](size_t count) -> const uint8_t * {
        if (count > length - cursor) return nullptr;
        const auto bytes = p + cursor;
        cursor += count;
        return bytes;
    };
    if ((out.flags & 0x0100) && !take(3)) return false; // country
    if (out.flags & 0x0200) {
        const auto bytes = take(2);
        if (!bytes) return false;
        out.socialChannels = le16(bytes);
    }
    if ((out.flags & 0x0001) && !take(8)) return false; // infra BSSID/channel
    if ((out.flags & 0x0002) && !take(6)) return false; // infra MAC, not AWDL
    if (out.flags & 0x0004) {
        out.awdlAddressPresent = true;
        const auto bytes = take(6);
        if (!bytes) return false;
        out.awdlAddressUnicast = unicastMac(bytes);
        out.awdlAddressMatchesSource = equalMac(bytes, source);
    }
    if ((out.flags & 0x0010) && !take(2)) return false; // UMI
    if (out.flags & 0x1000) {
        const auto bytes = take(2);
        if (!bytes || !take(le16(bytes))) return false;
    }
    if (out.flags & 0x8000) {
        const auto bytes = take(2);
        if (!bytes) return false;
        out.extensionFlags = le16(bytes);
    }
    // Extended data after its flags is deliberately opaque. Do not call a
    // known-prefix parse a validation of the entire data-path TLV.
    out.trailingBytes = uint32_t(length - cursor);
    return true;
}

inline bool profileActionTemplate(const ActionTemplate &input, const uint8_t *source,
    const uint8_t *syncMaster, uint32_t backendMetric, TemplateProfile *out) {
    if (!out) return false;
    *out = {};
    if (!input.length() || !source || !syncMaster || !unicastMac(source)) return false;
    out->length = input.length();
    // ActionTemplate owns a fully bounds-checked TLV sequence. Field parsers
    // below still check their own variable payload lengths before every read.
    for (size_t pos = 0; pos < input.length();) {
        const auto kind = input.bytes()[pos];
        const size_t length = le16(input.bytes() + pos + 1);
        const auto p = input.bytes() + pos + 3;
        ++out->tlvs;
        if (kind == 12) {
            auto &d = out->dataPath;
            if (!d.count++) {
                d.length = uint32_t(length);
                d.prefixValid = profileDataPath(p, length, source, d);
            }
        } else if (kind == 24) {
            auto &e = out->election;
            if (!e.count++) {
                e.length = uint32_t(length);
                if (length >= 40) {
                    e.prefixValid = true;
                    e.masterUnicast = unicastMac(p); e.syncUnicast = unicastMac(p + 6);
                    e.masterMatchesSource = equalMac(p, source);
                    e.masterMatchesSyncRole = equalMac(p, syncMaster);
                    e.syncMatchesSource = equalMac(p + 6, source);
                    e.syncMatchesMaster = equalMac(p + 6, p);
                    e.masterCounter = le32(p + 12); e.distance = le32(p + 16);
                    e.masterMetric = le32(p + 20); e.selfMetric = le32(p + 24);
                    e.selfCounter = le32(p + 36);
                    e.selfMetricMatchesBackend = e.selfMetric == backendMetric;
                }
            }
        } else if (kind == 21) {
            auto &v = out->version;
            if (!v.count++) {
                v.length = uint32_t(length);
                if (length >= 2) { v.prefixValid = true; v.version = p[0]; v.deviceClass = p[1]; }
            }
        } else if (kind == 6 && !out->serviceCount++) out->serviceLength = uint32_t(length);
        pos += 3 + length;
    }
    return true;
}
}

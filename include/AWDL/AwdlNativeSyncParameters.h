// SPDX-License-Identifier: GPL-2.0-or-later
// Owned, disabled 24H32 synchronization timing. No firmware enable implied.
#pragma once
#include "AwdlSyncAction.h"

namespace ItlAwdl {
enum class NativeSyncResult { Invalid, Unsupported, Stored };

class NativeSyncParameters {
public:
    NativeSyncResult replace(uint32_t version, uint32_t length, uint32_t period,
        uint32_t extension, uint32_t framePeriod, uint32_t unknown14) {
        if (version > 1 || !length || !period || !extension || !framePeriod ||
            length > 0xffff || period > 0xffff || extension > 0xffff || framePeriod > 0xffff)
            return NativeSyncResult::Invalid;
        // Qualified defaults from 24H32's native parameter initializer and
        // interface-state request. The final word is not a proven timing
        // field; accept only its observed zero value. Other profiles need a
        // separate audit of AW duration, extension and scheduling semantics.
        if (length != 16 || period != 16 || extension != 16 || framePeriod != 110 || unknown14)
            return NativeSyncResult::Unsupported;
        length_ = uint16_t(length); period_ = uint16_t(period);
        extension_ = uint16_t(extension); framePeriod_ = uint16_t(framePeriod);
        ready_ = true;
        return NativeSyncResult::Stored;
    }
    bool apply(SyncActionParameters &out) const {
        if (!ready_) return false;
        out.commonTu = length_; out.awPeriodTu = period_;
        out.extensionTu = extension_; out.actionPeriodTu = framePeriod_;
        return true;
    }
    bool matches(const SyncActionParameters &value) const {
        return ready_ && value.commonTu == length_ && value.awPeriodTu == period_ &&
            value.extensionTu == extension_ && value.actionPeriodTu == framePeriod_;
    }
    uint16_t lengthTu() const { return length_; }
    uint16_t periodTu() const { return period_; }
    uint16_t extensionTu() const { return extension_; }
    uint16_t framePeriodTu() const { return framePeriod_; }
    bool ready() const { return ready_; }
    void clear() { *this = {}; }
private:
    uint16_t length_ = 0, period_ = 0, extension_ = 0, framePeriod_ = 0;
    bool ready_ = false;
};
}

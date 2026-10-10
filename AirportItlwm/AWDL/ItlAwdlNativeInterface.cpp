// SPDX-License-Identifier: GPL-2.0-or-later
#include "ItlAwdlNativeInterface.hpp"
#include "ItlAwdlNativePacket.hpp"
#include "ItlAwdlNativeQueues.hpp"
#include "AirportItlwmV2.hpp"
#include <Airport/macOS15/IO80211ManagementBuffer.h>
#include <AWDL/AwdlNativeFrame.h>
#include <AWDL/AwdlTxCompletion.h>
#include <AWDL/AwdlTemplateProfile.h>
#include <Airport/CCPipe.h>
#include <libkern/version.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/OSAtomic.h>
#include <kern/clock.h>

// Exported non-virtual member in 24H32. The bridge uses only the retained
// OSObject pointer; no guessed peer-manager class layout or private writes.
extern "C" IOReturn nativeAwdlConfigure(OSObject *)
    __asm__("__ZN22IO80211AWDLPeerManager9configureEv");
extern "C" IOReturn nativeAwdlUpdateTemplate(OSObject *, bool)
    __asm__("__ZN22IO80211AWDLPeerManager23updateSyncFrameTemplateEb");
extern "C" void nativeAwdlSelectChannels(OSObject *)
    __asm__("__ZN22IO80211AWDLPeerManager26selectChannelsToUseForAWDLEv");
extern "C" uint32_t nativeAwdlSocialChannels(OSObject *)
    __asm__("__ZN22IO80211AWDLPeerManager28getSupportedSocialChannelMapEv");
extern "C" IOReturn nativeAwdlGetSyncParams(OSObject *, apple80211_awdl_sync_params *)
    __asm__("__ZN22IO80211AWDLPeerManager17getAwdlSyncParamsEP27apple80211_awdl_sync_params");
extern "C" IOReturn nativeAwdlSetSyncParams(OSObject *, apple80211_awdl_sync_params *)
    __asm__("__ZN22IO80211AWDLPeerManager17setAwdlSyncParamsEP27apple80211_awdl_sync_params");

extern "C" IOReturn nativeAwdlEnable(OSObject *)
    __asm__("__ZN22IO80211AWDLPeerManager10enableAWDLEv");
extern "C" void nativeAwdlDisable(OSObject *, uint32_t)
    __asm__("__ZN22IO80211AWDLPeerManager11disableAWDLEj");
extern "C" OSObject *nativeAwdlSupervisor(OSObject *)
    __asm__("__ZN22IO80211AWDLPeerManager16getP2PSupervisorEv");

namespace {
// Native log/service detach may defer the final release until the work queue
// gate opens. Serialize both probe entry points while waiting outside that gate.
volatile UInt32 nativeProbeActive = 0;
// 24H32 PeerManager::free clears shared log buffers without resetting their
// allocation-once counters. A second native manager can panic in a PM callout.
// Keep this latch in the boot's registry root, so closing a client or unloading
// this kext cannot permit recreation. Never patch Apple's private globals.
const char nativePeerAttemptKey[] = "AX201NativePeerManagerAttempted";
bool nativePeerAttempted() {
    auto root = IORegistryEntry::getRegistryRoot();
    return !root || root->getProperty(nativePeerAttemptKey);
}
bool reserveNativePeerAttempt() {
    auto root = IORegistryEntry::getRegistryRoot();
    return root && !root->getProperty(nativePeerAttemptKey) &&
        root->setProperty(nativePeerAttemptKey, kOSBooleanTrue);
}
struct NativeProbeLease {
    bool owned;
    NativeProbeLease() : owned(OSCompareAndSwap(0, 1, &nativeProbeActive)) {}
    ~NativeProbeLease() { if (owned) OSCompareAndSwap(1, 0, &nativeProbeActive); }
};
const OSMetaClass *nativePeerClass() {
    auto name = OSSymbol::withCStringNoCopy("IO80211AWDLPeerManager");
    if (!name) return nullptr;
    auto result = OSMetaClass::getMetaClassWithName(name);
    name->release();
    return result;
}
}

OSDefineMetaClassAndStructors(ItlAwdlNativeInterface, IO80211AWDLProtocol)

IOReturn ItlAwdlNativeInterface::backendResult(BackendIndex index, IOReturn result) {
    OSIncrementAtomic(reinterpret_cast<volatile SInt32 *>(&backendCalls));
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (loop && loop->inGate() && backendTrace) {
        auto &trace = *backendTrace;
        if (trace.trace_count < ITL_AWDL_CONFIGURE_TRACE_COUNT)
            trace.trace[trace.trace_count++] = {0xfa0U + 8U * index, static_cast<uint32_t>(result)};
        else ++trace.trace_dropped;
    }
    return result;
}

IOReturn ItlAwdlNativeInterface::getAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_getAWDL_SYNC_PARAMS, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive()) return finish(kIOReturnNotReady);
    // The native manager falls back to its defaults ONLY for Unsupported.
    // Its wrapper otherwise returns success even for a backend error. Keep
    // unconfigured state distinguishable and leave the caller's buffer intact.
    if (!syncParameters.ready()) return finish(kIOReturnUnsupported);
    apple80211_awdl_sync_params result = {};
    result.version = 1;
    result.availability_window_length = syncParameters.lengthTu();
    result.availability_window_period = syncParameters.periodTu();
    result.extension_length = syncParameters.extensionTu();
    result.synchronization_frame_period = syncParameters.framePeriodTu();
    *data = result;
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::setAWDL_SYNC_PARAMS(apple80211_awdl_sync_params *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_setAWDL_SYNC_PARAMS, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive()) return finish(kIOReturnNotReady);
    if (isInterfaceEnabled() || runtimeSyncEnabled) return finish(kIOReturnBusy);
    static_assert(sizeof(*data) == 24, "24H32 sync parameter request");
    static_assert(offsetof(apple80211_awdl_sync_params, unknown_14) == 20, "24H32 sync parameter tail");
    const auto result = syncParameters.replace(data->version, data->availability_window_length,
        data->availability_window_period, data->extension_length,
        data->synchronization_frame_period, data->unknown_14);
    if (result == ItlAwdl::NativeSyncResult::Invalid) return finish(kIOReturnBadArgument);
    if (result == ItlAwdl::NativeSyncResult::Unsupported) return finish(kIOReturnUnsupported);
    // Disabled staging, consumed by the encoder and qualification timeline.
    // An operational enable still needs a qualified firmware transaction.
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::getAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_getAWDL_SYNC_ENABLED, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    static_assert(sizeof(*data) == 12, "24H32 sync-enabled request");
    bzero(data, sizeof(*data));
    data->version = 1;
    data->enabled = runtimeSyncEnabled && runtimeClockQualified && runtimeRadioReady();
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::setAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_setAWDL_SYNC_ENABLED, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    if (data->version > 1 || data->unk1 || data->enabled > 1) return finish(kIOReturnBadArgument);
    // Preserve the negative preflight contract even with a positive backend.
    if (preflightRejectEnable) return finish(kIOReturnUnsupported);
    auto loop = getWorkLoop();
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive()) return finish(kIOReturnNotReady);
    if (!data->enabled) {
        if (!runtimeRadio) return finish(kIOReturnNotReady);
        synchronizationRadioStopped();
        return finish(kIOReturnSuccess);
    }
    if ((runtimePhase != RuntimeReady && runtimePhase != RuntimeRunning) || !runtimeClockQualified || !runtimeRadioReady())
        return finish(kIOReturnNotReady);
    runtimeSyncEnabled = true;
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::setAWDL_SYNC_FRAME_TEMPLATE(apple80211_awdl_sync_frame_template *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_setAWDL_SYNC_FRAME_TEMPLATE, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive()) return finish(kIOReturnNotReady);
    static_assert(sizeof(*data) == 16, "24H32 template request");
    static_assert(offsetof(apple80211_awdl_sync_frame_template, payload) == 8, "24H32 template pointer");
    if (data->version > 1 || !syncTemplate.replace(
        static_cast<const uint8_t *>(data->payload), data->payload_len)) return finish(kIOReturnBadArgument);
    // The host now owns a validated snapshot for a future radio scheduler.
    // No frame is sent and sync-enabled remains false.
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::setAWDL_BSSID(apple80211_awdl_bssid *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_setAWDL_BSSID, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive()) return finish(kIOReturnNotReady);
    if (isInterfaceEnabled() || runtimeSyncEnabled) return finish(kIOReturnBusy);
    static_assert(sizeof(*data) == 16, "24H32 BSSID pair request");
    static_assert(offsetof(apple80211_awdl_bssid, bssid) == 4, "24H32 AWDL BSSID");
    static_assert(offsetof(apple80211_awdl_bssid, unk_mac) == 10, "24H32 companion BSSID");
    const auto result = networkConfiguration.replace(data->version, data->bssid, data->unk_mac);
    if (result == ItlAwdl::BssidResult::Invalid) return finish(kIOReturnBadArgument);
    if (result == ItlAwdl::BssidResult::Unsupported) return finish(kIOReturnUnsupported);
    // Accepted configuration is owned and used by the action encoder. A
    // future enable transaction must apply its corresponding hardware filter.
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::setAWDL_ELECTION_ID(apple80211_awdl_election_id *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_setAWDL_ELECTION_ID, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive()) return finish(kIOReturnNotReady);
    if (data->version > 1) return finish(kIOReturnBadArgument);
    if (isInterfaceEnabled() || runtimeSyncEnabled) return finish(kIOReturnBusy);
    if (!networkConfiguration.ready()) return finish(kIOReturnNotReady);
    static_assert(sizeof(*data) == 8, "24H32 election-ID request");
    // The native request word is 32 bits. The 24H32 radio backend stores its
    // low 16 bits; its getter zero-extends that word. Preserve that behavior,
    // including zero and all-ones IDs, rather than inventing range rejection.
    electionId = static_cast<uint16_t>(data->election_id);
    electionIdValid = true;
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::getAWDL_ELECTION_ID(apple80211_awdl_election_id *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_getAWDL_ELECTION_ID, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive() ||
        !networkConfiguration.ready() || !electionIdValid) return finish(kIOReturnNotReady);
    data->version = 1;
    data->election_id = electionId;
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::setAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_setAWDL_ELECTION_METRIC, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive()) return finish(kIOReturnNotReady);
    if (data->version > 1) return finish(kIOReturnBadArgument);
    if (isInterfaceEnabled() || runtimeSyncEnabled) return finish(kIOReturnBusy);
    if (!networkConfiguration.ready()) return finish(kIOReturnNotReady);
    static_assert(sizeof(*data) == 12, "24H32 election-metric request");
    // The peer manager resolves override/default policy before this call.
    // The radio setter consumes the resulting full 32-bit word, including 0.
    electionMetric = data->metric;
    electionMetricValid = true;
    setProperty("AX201StagedElectionMetric", electionMetric, 32);
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::getAWDL_ELECTION_METRIC(apple80211_awdl_election_metric *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_getAWDL_ELECTION_METRIC, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive() ||
        !networkConfiguration.ready() || !electionMetricValid) return finish(kIOReturnNotReady);
    apple80211_awdl_election_metric result = {};
    result.version = 1;
    result.metric = electionMetric;
    *data = result;
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::setAWDL_MIN_RATE(apple80211_awdl_min_rate *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_setAWDL_MIN_RATE, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive()) return finish(kIOReturnNotReady);
    if (data->version > 1) return finish(kIOReturnBadArgument);
    if (isInterfaceEnabled() || runtimeSyncEnabled) return finish(kIOReturnBusy);
    if (!networkConfiguration.ready()) return finish(kIOReturnNotReady);
    static_assert(sizeof(*data) == 8, "24H32 minimum-rate request");
    // Native 24H32 doubles this byte for firmware's 500-kbit/s rate units;
    // its default is 12. Limit staging to AX201's legacy OFDM rates. Applying
    // the rate to a native TX path remains part of the enable transaction.
    switch (data->min_rate) {
        case 6: case 9: case 12: case 18: case 24: case 36: case 48: case 54: break;
        default: return finish(kIOReturnUnsupported);
    }
    minimumRateMbps = data->min_rate;
    minimumRateValid = true;
    setProperty("AX201StagedMinRateMbps", minimumRateMbps, 32);
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::getAWDL_MIN_RATE(apple80211_awdl_min_rate *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_getAWDL_MIN_RATE, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive() ||
        !networkConfiguration.ready() || !minimumRateValid) return finish(kIOReturnNotReady);
    apple80211_awdl_min_rate result = {};
    result.version = 1;
    result.min_rate = minimumRateMbps;
    *data = result;
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::setAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_setAWDL_MASTER_CHANNEL, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive()) return finish(kIOReturnNotReady);
    if (data->version > 1) return finish(kIOReturnBadArgument);
    if (isInterfaceEnabled() || runtimeSyncEnabled) return finish(kIOReturnBusy);
    if (!networkConfiguration.ready()) return finish(kIOReturnNotReady);
    static_assert(sizeof(*data) == 8, "24H32 master-channel request");
    const auto channel = data->master_channel;
    // Native 24H32 ignores zero instead of clearing the previous channel.
    if (!channel) return finish(kIOReturnSuccess);
    if (channel > IEEE80211_CHAN_MAX || !ItlAwdl::nativeActionChannelFlags(channel))
        return finish(kIOReturnBadArgument);
    auto parent = OSDynamicCast(AirportItlwm, controller);
    auto radio = parent ? OSDynamicCast(ItlIwx, parent->fHalService) : nullptr;
    auto gate = radio ? radio->getMainCommandGate() : nullptr;
    if (!gate || gate->getWorkLoop() != loop) return finish(kIOReturnNotReady);
    auto ic = radio->get80211Controller();
    if (!ic || !isset(ic->ic_chan_active, channel) || !ic->ic_channels[channel].ic_freq)
        return finish(kIOReturnUnsupported);
    masterChannel = static_cast<uint16_t>(channel);
    masterChannelValid = true;
    setProperty("AX201StagedMasterChannel", masterChannel, 32);
    // Staging is not retuning or permission to transmit. The future enable /
    // scheduler path must revalidate the live firmware regulatory TX flags.
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::getAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_getAWDL_MASTER_CHANNEL, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive() ||
        !networkConfiguration.ready() || !masterChannelValid) return finish(kIOReturnNotReady);
    data->version = 1;
    data->master_channel = masterChannel;
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::setAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_setAWDL_ELECTION_RSSI_THRESHOLDS, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive()) return finish(kIOReturnNotReady);
    if (data->version > 1) return finish(kIOReturnBadArgument);
    if (isInterfaceEnabled() || runtimeSyncEnabled) return finish(kIOReturnBusy);
    if (!networkConfiguration.ready()) return finish(kIOReturnNotReady);
    static_assert(sizeof(*data) == 20, "24H32 election-RSSI request");
    const uint32_t words[] = {data->unk1, data->unk2, data->unk3, data->unk4};
    // The native radio stores low bytes; its getter sign-extends each byte.
    // Sentinel handling belongs to the peer manager before this backend call.
    for (unsigned i = 0; i < 4; ++i) {
        const int32_t low = words[i] & 0xff;
        electionRssiThresholds[i] = low < 0x80 ? low : low - 0x100;
    }
    electionRssiThresholdsValid = true;
    // These are owned disabled-state parameters. Operational enable remains
    // unavailable until the radio scheduler can apply the whole configuration.
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::getAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_getAWDL_ELECTION_RSSI_THRESHOLDS, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive() ||
        !networkConfiguration.ready() || !electionRssiThresholdsValid) return finish(kIOReturnNotReady);
    data->version = 1;
    data->unk1 = static_cast<uint32_t>(electionRssiThresholds[0]);
    data->unk2 = static_cast<uint32_t>(electionRssiThresholds[1]);
    data->unk3 = static_cast<uint32_t>(electionRssiThresholds[2]);
    data->unk4 = static_cast<uint32_t>(electionRssiThresholds[3]);
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::setAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_setAWDL_PRESENCE_MODE, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive()) return finish(kIOReturnNotReady);
    // The native setter accepts the complete 0..64 range. Encoding/scheduling
    // applies its own narrower geometry checks instead of silently clamping it.
    if (data->version > 1 || data->mode > 64) return finish(kIOReturnBadArgument);
    if (isInterfaceEnabled() || runtimeSyncEnabled) return finish(kIOReturnBusy);
    if (!networkConfiguration.ready()) return finish(kIOReturnNotReady);
    presenceMode = data->mode;
    presenceModeValid = true;
    setProperty("AX201StagedPresenceMode", presenceMode, 32);
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::getAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_getAWDL_PRESENCE_MODE, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive() ||
        !networkConfiguration.ready() || !presenceModeValid) return finish(kIOReturnNotReady);
    data->version = 1;
    data->mode = presenceMode;
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive()) return finish(kIOReturnNotReady);
    if (data->version > 1) return finish(kIOReturnBadArgument);
    if (isInterfaceEnabled() || runtimeSyncEnabled) return finish(kIOReturnBusy);
    if (!networkConfiguration.ready()) return finish(kIOReturnNotReady);
    // 24H32 sends these four low bytes and zero-extends firmware readback.
    const uint8_t next[] = {uint8_t(data->unk4), uint8_t(data->unk1),
                           uint8_t(data->unk2), uint8_t(data->unk3)};
    memcpy(extensionCounts, next, sizeof(next));
    extensionCountsValid = true;
    setProperty("AX201StagedExtensionCounts", extensionCounts, sizeof(extensionCounts));
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive() ||
        !networkConfiguration.ready() || !extensionCountsValid) return finish(kIOReturnNotReady);
    data->version = 1;
    data->unk1 = extensionCounts[1];
    data->unk2 = extensionCounts[2];
    data->unk3 = extensionCounts[3];
    data->unk4 = extensionCounts[0];
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive()) return finish(kIOReturnNotReady);
    if (isInterfaceEnabled() || runtimeSyncEnabled) return finish(kIOReturnBusy);
    if (!networkConfiguration.ready()) return finish(kIOReturnNotReady);
    static_assert(sizeof(*data) == ItlAwdl::NativeChannelSequence::RequestLength, "24H32 channel request");
    ItlAwdl::NativeChannelSequence next;
    const auto result = next.replace(reinterpret_cast<const uint8_t *>(data), sizeof(*data));
    if (result == ItlAwdl::NativeChannelResult::Invalid) return finish(kIOReturnBadArgument);
    if (result == ItlAwdl::NativeChannelResult::Unsupported) return finish(kIOReturnUnsupported);
    auto parent = OSDynamicCast(AirportItlwm, controller);
    auto radio = parent ? OSDynamicCast(ItlIwx, parent->fHalService) : nullptr;
    auto gate = radio ? radio->getMainCommandGate() : nullptr;
    if (!gate || gate->getWorkLoop() != loop) return finish(kIOReturnNotReady);
    auto ic = radio->get80211Controller();
    if (!ic) return finish(kIOReturnNotReady);
    for (auto channel : next.plan().channels)
        if (channel && (channel > IEEE80211_CHAN_MAX || !isset(ic->ic_chan_active, channel) ||
            !ic->ic_channels[channel].ic_freq)) return finish(kIOReturnUnsupported);
    channelSequence = next;
    auto property = OSData::withBytes(channelSequence.plan().channels, ItlAwdl::ScheduleLength);
    if (property) {
        setProperty("AX201StagedChannelSequence", property);
        property->release();
    }
    setProperty("AX201StagedChannelStep", channelSequence.plan().step_aw, 32);
    // This owns an encoding-0 plan only. Enabling/hopping must separately
    // revalidate firmware regulatory flags and apply the complete plan.
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive() ||
        !networkConfiguration.ready() || !channelSequence.ready()) return finish(kIOReturnNotReady);
    return finish(channelSequence.copyRequest(reinterpret_cast<uint8_t *>(data), sizeof(*data)) ?
        kIOReturnSuccess : kIOReturnError);
}

IOReturn ItlAwdlNativeInterface::setAWDL_SYNC_STATE(apple80211_awdl_sync_state *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_setAWDL_SYNC_STATE, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive()) return finish(kIOReturnNotReady);
    if (data->version > 1 || data->state < 1 || data->state > 3 ||
        !ItlAwdl::unicastMac(data->master)) return finish(kIOReturnBadArgument);
    // Only the ordinary operation modes with no extra role flags are audited.
    if (data->operation_mode > 1 || data->additional_role_flags)
        return finish(kIOReturnUnsupported);
    if (isInterfaceEnabled()) return finish(kIOReturnBusy);
    if (!networkConfiguration.ready() || !electionIdValid || !masterChannelValid ||
        !electionRssiThresholdsValid || !presenceModeValid || !extensionCountsValid)
        return finish(kIOReturnNotReady);
    static_assert(sizeof(*data) == 24, "24H32 synchronization-role request");
    // Own the requested role, not padding or a producer's stack. The separate
    // synchronization-enable backend must apply it before reporting radio up.
    apple80211_awdl_sync_state next = {};
    next.version = 1;
    next.state = data->state;
    memcpy(next.master, data->master, sizeof(next.master));
    next.operation_mode = data->operation_mode;
    if (runtimeSyncEnabled) {
        if (!runtimeRadioReady()) return finish(kIOReturnNotReady);
        ItlAwdlClockResponse clock = {};
        auto error = runtimeRadio->awdlReadClock(&clock);
        if (error || (clock.flags & 7) != 7) return finish(kIOReturnNotReady);
        if (next.state == 2 && ItlAwdl::equalMac(next.master, runtimeRadio->awdl.peer)) {
            ItlAwdl::ScheduleSlot anchor = {};
            if (!runtimeRadio->awdl.schedule.anchor(clock.gp2_us, clock.host_end_us,
                syncParameters.periodTu(), uint8_t(presenceMode), &anchor)) return finish(kIOReturnNotReady);
            if (!runtimeTimeline.start(anchor.start_gp2_us, syncParameters.periodTu(),
                uint8_t(presenceMode), anchor.counter)) return finish(kIOReturnBadArgument);
        } else if (next.state != 1 || !ItlAwdl::equalMac(next.master, selfAddress.ether_addr_octet))
            return finish(kIOReturnUnsupported);
        ++runtimeResult.role_updates;
        runtimeHadWindow = false;
    }
    stagedSyncRole = next;
    stagedSyncRoleValid = true;
    setProperty("AX201StagedSyncRole", next.state, 32);
    return finish(kIOReturnSuccess);
}

IOReturn ItlAwdlNativeInterface::probeConfiguration(AirportItlwm *parent, ItlIwx *radio,
    const ItlAwdlNativeConfigureRequest *request, ItlAwdlNativeConfigureResponse *out) {
    if (!parent || !radio || !request || !out || request->version != ITL_AWDL_ABI_VERSION ||
        request->operation > ItlAwdlConfigureRun || request->reserved[0] || request->reserved[1])
        return kIOReturnBadArgument;
    if (strcmp(osversion, "24H32")) return kIOReturnUnsupported;
    auto gate = radio->getMainCommandGate();
    if (!gate || !gate->getWorkLoop()) return kIOReturnNotReady;
    NativeProbeLease lease;
    if (!lease.owned) return kIOReturnBusy;
    bzero(out, sizeof(*out));
    out->version = ITL_AWDL_ABI_VERSION;
    struct Call { AirportItlwm *parent; ItlIwx *radio; IOWorkLoop *loop;
        const ItlAwdlNativeConfigureRequest *request; ItlAwdlNativeConfigureResponse *out;
    } call = {parent, radio, gate->getWorkLoop(), request, out};
    return gate->runAction([](OSObject *, void *argument, void *, void *, void *) -> IOReturn {
        auto c = static_cast<Call *>(argument);
        if (c->parent->isInactive()) return kIOReturnNotReady;
        ItlAwdlStatusResponse status = {};
        auto result = c->radio->awdlCommand(c->parent, ItlAwdlStatus, nullptr, &status);
        if (result) return result;
        if (status.device_id != 0x7af0) return kIOReturnUnsupported;
        if (status.session != ItlAwdlIdle) return kIOReturnBusy;
        auto object = c->parent->nativeAwdlInterface;
        if (!object) return c->request->operation == ItlAwdlConfigureInspect ?
            kIOReturnSuccess : kIOReturnNotReady;
        if (c->parent->getAwdlInterface() != object || object->isInactive() ||
            object->resourcesDisposed || !object->startAttempted || object->getWorkLoop() != c->loop)
            return kIOReturnNotReady;
        auto &saved = object->configurationResult;
        if (c->request->operation == ItlAwdlConfigureRun && !saved.attempted) {
            // No BSD callbacks or queues may race the single configuration
            // call. Keep this diagnostic separate from operational enable.
            if (object->bsdPublishRequested || object->getBSDInterface() ||
                object->transportRegistered || object->isInterfaceEnabled()) return kIOReturnBusy;
            auto state = object->nativeStartState();
            auto peerClass = nativePeerClass();
            auto queue = c->parent->getWorkQueue();
            if (!state || !state->peerManager || !peerClass ||
                !state->peerManager->metaCast(peerClass) || !queue || queue->onThread())
                return kIOReturnNotReady;
            saved.attempted = 1;
            saved.backend_before = object->backendCalls;
            object->backendTrace = &saved;
            saved.native_result = static_cast<uint32_t>(nativeAwdlConfigure(state->peerManager));
            object->backendTrace = nullptr;
            saved.backend_after = object->backendCalls;
        }
        memcpy(c->out, &saved, sizeof(*c->out));
        c->out->version = ITL_AWDL_ABI_VERSION;
        c->out->present = 1;
        c->out->registry_id = object->getRegistryEntryID();
        c->out->interface_enabled = object->isInterfaceEnabled();
        c->out->tx_enabled = object->transportTx && object->transportTx->isEnabled();
        c->out->rx_enabled = object->transportRx && object->transportRx->isEnabled();
        c->out->bsd_present = object->getBSDInterface() != nullptr;
        return kIOReturnSuccess;
    }, &call);
}

IOReturn ItlAwdlNativeInterface::probeActivationPreflight(AirportItlwm *parent, ItlIwx *radio,
    const ItlAwdlNativeConfigureRequest *request, ItlAwdlNativeConfigureResponse *out) {
    if (!parent || !radio || !request || !out || request->version != ITL_AWDL_ABI_VERSION ||
        request->operation > ItlAwdlConfigureRun || request->reserved[0] || request->reserved[1])
        return kIOReturnBadArgument;
    if (strcmp(osversion, "24H32")) return kIOReturnUnsupported;
    auto gate = radio->getMainCommandGate();
    if (!gate || !gate->getWorkLoop()) return kIOReturnNotReady;
    NativeProbeLease lease;
    if (!lease.owned) return kIOReturnBusy;
    bzero(out, sizeof(*out));
    out->version = ITL_AWDL_ABI_VERSION;
    struct Call { AirportItlwm *parent; ItlIwx *radio; IOWorkLoop *loop;
        const ItlAwdlNativeConfigureRequest *request; ItlAwdlNativeConfigureResponse *out;
    } call = {parent, radio, gate->getWorkLoop(), request, out};
    return gate->runAction([](OSObject *, void *argument, void *, void *, void *) -> IOReturn {
        auto c = static_cast<Call *>(argument);
        if (c->parent->isInactive()) return kIOReturnNotReady;
        ItlAwdlStatusResponse status = {};
        auto result = c->radio->awdlCommand(c->parent, ItlAwdlStatus, nullptr, &status);
        if (result) return result;
        if (status.device_id != 0x7af0) return kIOReturnUnsupported;
        if (status.session != ItlAwdlIdle || c->radio->awdl.commandBusy ||
            c->radio->awdl.owner || c->radio->awdlNativeRadioOwner) return kIOReturnBusy;
        auto object = c->parent->nativeAwdlInterface;
        if (!object) return c->request->operation == ItlAwdlConfigureInspect ?
            kIOReturnSuccess : kIOReturnNotReady;
        if (c->parent->getAwdlInterface() != object || object->isInactive() ||
            object->resourcesDisposed || !object->startAttempted || object->getWorkLoop() != c->loop)
            return kIOReturnNotReady;
        if (c->request->operation == ItlAwdlConfigureRun && object->runtimeResult.attempted)
            return kIOReturnNotPermitted;
        auto &saved = object->activationPreflightResult;
        if (c->request->operation == ItlAwdlConfigureRun && !saved.attempted) {
            // A native activation rejection trace, not an RF enable path.
            // Require STA power off and no native transport publication.
            auto ic = &c->radio->com.sc_ic;
            if (ic->ic_opmode != IEEE80211_M_STA || ic->ic_state != IEEE80211_S_INIT ||
                (ic->ic_if.if_flags & (IFF_UP | IFF_RUNNING))) return kIOReturnBusy;
            if (!object->configurationResult.attempted || object->configurationResult.native_result ||
                !object->templateResult.call.attempted || object->templateResult.call.native_result ||
                !object->syncParameters.ready()) return kIOReturnNotReady;
            if (object->bsdPublishRequested || object->getBSDInterface() ||
                object->transportRegistered || object->isInterfaceEnabled()) return kIOReturnBusy;
            auto state = object->nativeStartState();
            auto peerClass = nativePeerClass();
            auto queue = c->parent->getWorkQueue();
            if (!state || !state->peerManager || !peerClass ||
                !state->peerManager->metaCast(peerClass) || !queue || queue->onThread())
                return kIOReturnNotReady;
            auto supervisor = nativeAwdlSupervisor(state->peerManager);
            // enableAWDL dereferences this native object before its backend
            // request. Validate it through the exported accessor, no layout writes.
            object->setProperty("AX201ActivationSupervisorPresent", supervisor != nullptr);
            if (!supervisor || !supervisor->metaCast("IO80211P2PSupervisor"))
                return kIOReturnNotReady;
            saved.attempted = 1;
            saved.backend_before = object->backendCalls;
            object->backendTrace = &saved;
            object->preflightRejectEnable = true;
            saved.native_result = static_cast<uint32_t>(nativeAwdlEnable(state->peerManager));
            object->preflightRejectEnable = false;
            object->backendTrace = nullptr;
            saved.backend_after = object->backendCalls;
            // The audited native enable function stops before its enabled
            // transition when setAWDL_SYNC_ENABLED is rejected. That backend
            // is explicitly held Unsupported throughout this preflight. Its earlier
            // staging calls may replace templates; prohibit RF reuse this boot.
            object->setProperty("AX201ActivationPreflightAttempted", kOSBooleanTrue);
        }
        memcpy(c->out, &saved, sizeof(*c->out));
        c->out->version = ITL_AWDL_ABI_VERSION;
        c->out->present = 1;
        c->out->registry_id = object->getRegistryEntryID();
        c->out->interface_enabled = object->isInterfaceEnabled();
        c->out->tx_enabled = object->transportTx && object->transportTx->isEnabled();
        c->out->rx_enabled = object->transportRx && object->transportRx->isEnabled();
        c->out->bsd_present = object->getBSDInterface() != nullptr;
        return kIOReturnSuccess;
    }, &call);
}

IOReturn ItlAwdlNativeInterface::probeTiming(AirportItlwm *parent, ItlIwx *radio,
    const ItlAwdlNativeConfigureRequest *request, ItlAwdlNativeTimingResponse *out) {
    if (!parent || !radio || !request || !out || request->version != ITL_AWDL_ABI_VERSION ||
        request->operation > ItlAwdlConfigureRun || request->reserved[0] || request->reserved[1])
        return kIOReturnBadArgument;
    if (strcmp(osversion, "24H32")) return kIOReturnUnsupported;
    auto gate = radio->getMainCommandGate();
    if (!gate || !gate->getWorkLoop()) return kIOReturnNotReady;
    NativeProbeLease lease;
    if (!lease.owned) return kIOReturnBusy;
    bzero(out, sizeof(*out));
    out->call.version = ITL_AWDL_ABI_VERSION;
    struct Call { AirportItlwm *parent; ItlIwx *radio; IOWorkLoop *loop;
        const ItlAwdlNativeConfigureRequest *request; ItlAwdlNativeTimingResponse *out;
    } call = {parent, radio, gate->getWorkLoop(), request, out};
    return gate->runAction([](OSObject *, void *argument, void *, void *, void *) -> IOReturn {
        auto c = static_cast<Call *>(argument);
        if (c->parent->isInactive()) return kIOReturnNotReady;
        ItlAwdlStatusResponse status = {};
        auto result = c->radio->awdlCommand(c->parent, ItlAwdlStatus, nullptr, &status);
        if (result) return result;
        if (status.device_id != 0x7af0) return kIOReturnUnsupported;
        if (status.session != ItlAwdlIdle) return kIOReturnBusy;
        auto object = c->parent->nativeAwdlInterface;
        if (!object) return c->request->operation == ItlAwdlConfigureInspect ?
            kIOReturnSuccess : kIOReturnNotReady;
        if (c->parent->getAwdlInterface() != object || object->isInactive() ||
            object->resourcesDisposed || !object->startAttempted || object->getWorkLoop() != c->loop)
            return kIOReturnNotReady;
        auto &saved = object->timingResult;
        auto &trace = saved.call;
        if (c->request->operation == ItlAwdlConfigureRun && !trace.attempted) {
            if (object->bsdPublishRequested || object->getBSDInterface() ||
                object->transportRegistered || object->isInterfaceEnabled()) return kIOReturnBusy;
            if (!object->configurationResult.attempted || object->configurationResult.native_result ||
                !object->networkConfiguration.ready() || object->syncParameters.ready() ||
                object->syncTemplate.length() || object->templateResult.call.attempted) return kIOReturnNotReady;
            auto state = object->nativeStartState();
            auto peerClass = nativePeerClass();
            auto queue = c->parent->getWorkQueue();
            if (!state || !state->peerManager || !peerClass ||
                !state->peerManager->metaCast(peerClass) || !queue || queue->onThread())
                return kIOReturnNotReady;
            trace.attempted = 1;
            trace.backend_before = object->backendCalls;
            saved.get_before_result = saved.set_result = saved.get_after_result = kIOReturnNotReady;
            object->backendTrace = &trace;
            const auto observed = [&](uint32_t count, BackendIndex index, IOReturn expected) {
                return trace.trace_count == count && !trace.trace_dropped &&
                    trace.trace[count - 1].vptr_offset == 0xfa0U + 8U * index &&
                    trace.trace[count - 1].result == static_cast<uint32_t>(expected);
            };
            apple80211_awdl_sync_params before, after;
            memset(&before, 0xa5, sizeof(before));
            memset(&after, 0xa5, sizeof(after));
            saved.get_before_result = nativeAwdlGetSyncParams(state->peerManager, &before);
            memcpy(saved.before, &before, sizeof(before));
            ItlAwdl::NativeSyncParameters qualified;
            bool valid = !saved.get_before_result && observed(1, Backend_getAWDL_SYNC_PARAMS, kIOReturnUnsupported) &&
                qualified.replace(before.version, before.availability_window_length,
                    before.availability_window_period, before.extension_length,
                    before.synchronization_frame_period, before.unknown_14) == ItlAwdl::NativeSyncResult::Stored;
            if (valid) {
                auto input = before;
                saved.set_result = nativeAwdlSetSyncParams(state->peerManager, &input);
                valid = !saved.set_result && observed(2, Backend_setAWDL_SYNC_PARAMS, kIOReturnSuccess) &&
                    object->syncParameters.ready();
            }
            if (valid) {
                saved.get_after_result = nativeAwdlGetSyncParams(state->peerManager, &after);
                memcpy(saved.after, &after, sizeof(after));
                valid = !saved.get_after_result && observed(3, Backend_getAWDL_SYNC_PARAMS, kIOReturnSuccess) &&
                    after.version == 1 && !after.unknown_14 &&
                    before.availability_window_length == after.availability_window_length &&
                    before.availability_window_period == after.availability_window_period &&
                    before.extension_length == after.extension_length &&
                    before.synchronization_frame_period == after.synchronization_frame_period;
            }
            object->backendTrace = nullptr;
            trace.native_result = valid ? kIOReturnSuccess : kIOReturnError;
            trace.backend_after = object->backendCalls;
            saved.ready = object->syncParameters.ready();
        }
        memcpy(c->out, &saved, sizeof(*c->out));
        auto &snapshot = c->out->call;
        snapshot.version = ITL_AWDL_ABI_VERSION;
        snapshot.present = 1;
        snapshot.registry_id = object->getRegistryEntryID();
        snapshot.interface_enabled = object->isInterfaceEnabled();
        snapshot.tx_enabled = object->transportTx && object->transportTx->isEnabled();
        snapshot.rx_enabled = object->transportRx && object->transportRx->isEnabled();
        snapshot.bsd_present = object->getBSDInterface() != nullptr;
        return kIOReturnSuccess;
    }, &call);
}

IOReturn ItlAwdlNativeInterface::probeTemplate(AirportItlwm *parent, ItlIwx *radio,
    const ItlAwdlNativeConfigureRequest *request, ItlAwdlNativeTemplateResponse *out) {
    if (!parent || !radio || !request || !out || request->version != ITL_AWDL_ABI_VERSION ||
        request->operation > ItlAwdlConfigureRun || request->reserved[0] || request->reserved[1])
        return kIOReturnBadArgument;
    if (strcmp(osversion, "24H32")) return kIOReturnUnsupported;
    auto gate = radio->getMainCommandGate();
    if (!gate || !gate->getWorkLoop()) return kIOReturnNotReady;
    NativeProbeLease lease;
    if (!lease.owned) return kIOReturnBusy;
    bzero(out, sizeof(*out));
    out->call.version = ITL_AWDL_ABI_VERSION;
    struct Call { AirportItlwm *parent; ItlIwx *radio; IOWorkLoop *loop;
        const ItlAwdlNativeConfigureRequest *request; ItlAwdlNativeTemplateResponse *out;
    } call = {parent, radio, gate->getWorkLoop(), request, out};
    return gate->runAction([](OSObject *, void *argument, void *, void *, void *) -> IOReturn {
        auto c = static_cast<Call *>(argument);
        if (c->parent->isInactive()) return kIOReturnNotReady;
        ItlAwdlStatusResponse status = {};
        auto result = c->radio->awdlCommand(c->parent, ItlAwdlStatus, nullptr, &status);
        if (result) return result;
        if (status.device_id != 0x7af0) return kIOReturnUnsupported;
        if (status.session != ItlAwdlIdle) return kIOReturnBusy;
        auto object = c->parent->nativeAwdlInterface;
        if (!object) return c->request->operation == ItlAwdlConfigureInspect ?
            kIOReturnSuccess : kIOReturnNotReady;
        if (c->parent->getAwdlInterface() != object || object->isInactive() ||
            object->resourcesDisposed || !object->startAttempted || object->getWorkLoop() != c->loop)
            return kIOReturnNotReady;
        auto &saved = object->templateResult;
        auto &trace = saved.call;
        if (c->request->operation == ItlAwdlConfigureRun && !trace.attempted) {
            if (object->bsdPublishRequested || object->getBSDInterface() ||
                object->transportRegistered || object->isInterfaceEnabled()) return kIOReturnBusy;
            if (!object->configurationResult.attempted || object->configurationResult.native_result ||
                !object->networkConfiguration.ready() || !object->electionIdValid ||
                !object->electionMetricValid || !object->minimumRateValid ||
                !object->masterChannelValid || !object->stagedSyncRoleValid ||
                !object->presenceModeValid || !object->extensionCountsValid ||
                !object->channelSequence.ready() || !object->syncParameters.ready() ||
                !object->timingResult.call.attempted || object->timingResult.call.native_result ||
                object->syncTemplate.length()) return kIOReturnNotReady;
            auto state = object->nativeStartState();
            auto peerClass = nativePeerClass();
            auto queue = c->parent->getWorkQueue();
            if (!state || !state->peerManager || !peerClass ||
                !state->peerManager->metaCast(peerClass) || !queue || queue->onThread())
                return kIOReturnNotReady;
            // A successful native initWithInterface requires an initialized
            // P2P supervisor and real-time manager. This exact-build producer
            // uses those retained objects; it only builds supplementary TLVs
            // and calls the owned-template backend, not synchronization enable.
            trace.attempted = 1;
            trace.backend_before = object->backendCalls;
            // Normally called by the native interface-state transition.
            // This unpublished qualification object has not had that event.
            // The audited 24H32 selector only derives native CPU channel
            // state; do not fabricate a bitmap or change controller flags.
            const auto socialBefore = nativeAwdlSocialChannels(state->peerManager);
            nativeAwdlSelectChannels(state->peerManager);
            const auto socialAfter = nativeAwdlSocialChannels(state->peerManager);
            const uint32_t requiredSocial = object->masterChannel == 6 ? 1U :
                object->masterChannel == 44 ? 2U : object->masterChannel == 149 ? 4U : 0U;
            object->backendTrace = &trace;
            trace.native_result = static_cast<uint32_t>(requiredSocial && (socialAfter & requiredSocial) ?
                nativeAwdlUpdateTemplate(state->peerManager, true) : kIOReturnUnsupported);
            object->backendTrace = nullptr;
            trace.backend_after = object->backendCalls;
            saved.template_length = object->syncTemplate.length();
            saved.template_revision = object->syncTemplate.revision();
            object->publishTemplateProfile(socialBefore, socialAfter);
            // ActionTemplate validated every TLV before owning these bytes.
            for (size_t pos = 0; pos < saved.template_length;) {
                const auto p = object->syncTemplate.bytes() + pos;
                ++saved.tlv_count;
                saved.tlv_kinds[p[0] / 32] |= uint32_t(1) << (p[0] % 32);
                pos += 3 + ItlAwdl::le16(p + 1);
            }
            if (!trace.native_result && saved.template_length) {
                // CPU-only integration specimen. The synthetic clock below
                // is never passed to a radio or reported as an
                // observed synchronization boundary. No frame bytes leave.
                uint8_t frame[ItlAwdl::SyncActionMaxLength];
                saved.encoded_length = uint32_t(object->encodeQualificationFrame(1000000, frame, sizeof(frame)));
            }
        }
        memcpy(c->out, &saved, sizeof(*c->out));
        auto &snapshot = c->out->call;
        snapshot.version = ITL_AWDL_ABI_VERSION;
        snapshot.present = 1;
        snapshot.registry_id = object->getRegistryEntryID();
        snapshot.interface_enabled = object->isInterfaceEnabled();
        snapshot.tx_enabled = object->transportTx && object->transportTx->isEnabled();
        snapshot.rx_enabled = object->transportRx && object->transportRx->isEnabled();
        snapshot.bsd_present = object->getBSDInterface() != nullptr;
        return kIOReturnSuccess;
    }, &call);
}

void ItlAwdlNativeInterface::publishTemplateProfile(uint32_t socialBefore, uint32_t socialAfter) {
    ItlAwdl::TemplateProfile p = {};
    removeProperty("AX201NativeTemplateProfile");
    if (!stagedSyncRoleValid || !electionMetricValid ||
        !ItlAwdl::profileActionTemplate(syncTemplate, selfAddress.ether_addr_octet,
            stagedSyncRole.master, electionMetric, &p)) return;
    auto d = OSDictionary::withCapacity(36);
    if (!d) return;
    const auto put = [&](const char *key, uint32_t value) {
        auto n = OSNumber::withNumber(value, 32);
        if (n) { d->setObject(key, n); n->release(); }
    };
    put("template_length", p.length); put("template_revision", syncTemplate.revision());
    put("tlv_count", p.tlvs);
    put("native_social_channels_before", socialBefore);
    put("native_social_channels_after", socialAfter);
    put("data_path_count", p.dataPath.count); put("data_path_length", p.dataPath.length);
    put("data_path_prefix_valid", p.dataPath.prefixValid); put("data_path_flags", p.dataPath.flags);
    put("data_path_social_channels", p.dataPath.socialChannels);
    put("data_path_extension_flags", p.dataPath.extensionFlags);
    put("data_path_unparsed_bytes", p.dataPath.trailingBytes);
    put("data_path_awdl_address_present", p.dataPath.awdlAddressPresent);
    put("data_path_awdl_address_unicast", p.dataPath.awdlAddressUnicast);
    put("data_path_awdl_address_matches_source", p.dataPath.awdlAddressMatchesSource);
    put("election_count", p.election.count); put("election_length", p.election.length);
    put("election_prefix_valid", p.election.prefixValid);
    put("election_master_unicast", p.election.masterUnicast); put("election_sync_unicast", p.election.syncUnicast);
    put("election_master_matches_source", p.election.masterMatchesSource);
    put("election_master_matches_sync_role", p.election.masterMatchesSyncRole);
    put("election_sync_matches_source", p.election.syncMatchesSource);
    put("election_sync_matches_master", p.election.syncMatchesMaster);
    put("election_master_counter", p.election.masterCounter); put("election_self_counter", p.election.selfCounter);
    put("election_distance", p.election.distance); put("election_master_metric", p.election.masterMetric);
    put("election_self_metric", p.election.selfMetric);
    put("election_self_metric_matches_backend", p.election.selfMetricMatchesBackend);
    put("version_count", p.version.count); put("version_length", p.version.length);
    put("version_prefix_valid", p.version.prefixValid); put("version", p.version.version);
    put("device_class", p.version.deviceClass);
    put("service_count", p.serviceCount); put("service_length", p.serviceLength);
    setProperty("AX201NativeTemplateProfile", d); d->release();
}

size_t ItlAwdlNativeInterface::copySyncAction(const ItlAwdl::SyncActionContext &context,
    const ItlAwdl::SyncActionParameters &parameters, uint8_t *output, size_t capacity) {
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (strcmp(osversion, "24H32") || !loop || !loop->inGate() ||
        resourcesDisposed || isInactive() || !syncTemplate.length() ||
        !electionIdValid || !electionMetricValid || !minimumRateValid || !syncParameters.matches(parameters) ||
        !masterChannelValid || parameters.masterChannel != masterChannel ||
        !stagedSyncRoleValid || stagedSyncRole.operation_mode != 1 ||
        !ItlAwdl::equalMac(parameters.master, stagedSyncRole.master) ||
        !ItlAwdl::equalMac(context.source, selfAddress.ether_addr_octet) ||
        !presenceModeValid || parameters.presence != presenceMode || !extensionCountsValid ||
        parameters.minExtension != extensionCounts[0] ||
        parameters.maxMulticastExtension != extensionCounts[1] ||
        parameters.maxUnicastExtension != extensionCounts[2] ||
        parameters.maxActionExtension != extensionCounts[3] || !channelSequence.ready() ||
        !ItlAwdl::equalChannelPlan(parameters.plan, channelSequence.plan())) return 0;
    // The same gate serializes template replacement. The caller owns the
    // returned MPDU bytes and must independently qualify firmware submission.
    return ItlAwdl::buildSyncAction(context, parameters, syncTemplate, networkConfiguration, output, capacity);
}

size_t ItlAwdlNativeInterface::encodeQualificationFrame(uint32_t gp2, uint8_t *frame, size_t capacity,
    const ItlAwdl::LocalSyncBoundary *boundary, uint16_t sequence, const uint8_t *unicastPeer) {
    // Without a boundary this is a CPU-only encoding specimen. A supplied
    // local-master timeline is used only by explicit bounded qualification;
    // it does not represent an enabled native peer-manager synchronization.
    ItlAwdl::SyncActionContext context = {};
    memcpy(context.source, selfAddress.ether_addr_octet, 6);
    memset(context.destination, 0xff, 6);
    if (unicastPeer) {
        if (!ItlAwdl::unicastMac(unicastPeer)) return 0;
        memcpy(context.destination, unicastPeer, 6);
        context.subtype = 3; // MIF; broadcast qualification remains PSF.
    }
    context.clockValid = true;
    context.phyTimeUs = context.targetTimeUs = gp2;
    context.sequence = sequence;
    ItlAwdl::SyncActionParameters parameters = {};
    parameters.masterChannel = parameters.nextChannel = uint8_t(masterChannel);
    if (!syncParameters.apply(parameters)) return 0;
    parameters.flags = ItlAwdl::SyncCompatibilityFlags;
    parameters.presence = uint8_t(presenceMode);
    parameters.downCounterTu = uint16_t(parameters.awPeriodTu * parameters.presence);
    parameters.minExtension = extensionCounts[0];
    parameters.maxMulticastExtension = extensionCounts[1];
    parameters.maxUnicastExtension = extensionCounts[2];
    parameters.maxActionExtension = extensionCounts[3];
    parameters.plan = channelSequence.plan();
    if (boundary) {
        context.phyTimeUs = boundary->phyUs;
        context.targetTimeUs = boundary->targetUs;
        parameters.counter = boundary->counter;
        parameters.downCounterTu = boundary->downCounterTu;
        parameters.nextChannel = parameters.plan.channels[(boundary->slot + 1) % ItlAwdl::ScheduleLength];
    }
    memcpy(parameters.master, stagedSyncRole.master, 6);
    const auto length = copySyncAction(context, parameters, frame, capacity);
    ItlAwdl::ActionInfo action = {};
    ItlAwdl::SyncAnnouncement decoded = {};
    return length && ItlAwdl::parseAction(frame, length, &action) == ItlAwdl::ParseResult::Valid &&
        (runtimeRadio || action.tlv_count == templateResult.tlv_count + 1) &&
        ItlAwdl::parseSchedule(frame, length, &decoded) == ItlAwdl::ScheduleResult::Valid &&
        ItlAwdl::equalChannelPlan(decoded.plan, parameters.plan) &&
        ItlAwdl::equalMac(decoded.master, parameters.master) ? length : 0;
}

IOReturn ItlAwdlNativeInterface::probeRadio(AirportItlwm *parent, ItlIwx *radio,
    const ItlAwdlBeginRequest *request, ItlAwdlNativeRadioResponse *out, ItlAwdlNativeSyncTxResponse *tx,
    bool unicast, bool p2p, bool seriesObservation) {
    if (!parent || !radio || !request || !out || request->version != ITL_AWDL_ABI_VERSION ||
        request->channel || request->reserved || request->padding[0] || request->padding[1] ||
        request->dwell_ms < 1000 || request->dwell_ms > 5000 || !ItlAwdl::unicastMac(request->peer) ||
        (tx && out != &tx->radio) || (unicast && !tx) || (p2p && tx && !unicast && !seriesObservation) ||
        (seriesObservation && (!tx || !p2p)))
        return kIOReturnBadArgument;
    if (strcmp(osversion, "24H32")) return kIOReturnUnsupported;
    auto gate = radio->getMainCommandGate();
    if (!gate || !gate->getWorkLoop() || gate->getWorkLoop()->inGate()) return kIOReturnNotReady;
    NativeProbeLease lease;
    if (!lease.owned) return kIOReturnBusy;
    if (tx) bzero(tx, sizeof(*tx));
    else bzero(out, sizeof(*out));
    out->version = ITL_AWDL_ABI_VERSION;
    // Retain every object used across sleeps. Native teardown can still cancel
    // the radio on controller stop; each subsequent gated step revalidates it.
    parent->retain(); radio->retain(); gate->retain();
    struct Call { AirportItlwm *parent; ItlIwx *radio; ItlAwdlNativeInterface *object;
        const ItlAwdlBeginRequest *request; ItlAwdlNativeRadioResponse *out;
        uint32_t phase; int generation; bool finished;
        ItlAwdlNativeSyncTxResponse *tx; ItlAwdl::LocalTimeline timeline;
        uint64_t nextClockHostUs;
        bool timelineReady, sentWindow, unicast, p2p, seriesObservation; uint16_t lastWindow;
        uint32_t peerWindows[unsigned(ItlAwdl::PeerWindowResult::Count)], overlapWindows;
        uint32_t alignedSamples, alignmentUnavailable, navDeferrals, maxNavRemainingUs;
        uint32_t macIdBefore, macIdDuring, macIdAfter, macContextSamples, macContextMismatches;
        ItlAwdl::PeerTxWindow lastPeerWindow;
    } call = {};
    call.parent = parent; call.radio = radio; call.request = request;
    call.out = out; call.generation = -1; call.tx = tx;
    call.unicast = unicast;
    call.p2p = p2p;
    call.seriesObservation = seriesObservation;
    auto action = [](OSObject *, void *argument, void *, void *, void *) -> IOReturn {
        auto c = static_cast<Call *>(argument);
        auto r = c->radio;
        auto o = c->object;
        auto out = c->out;
        auto &s = r->awdl;
        auto ic = &r->com.sc_ic;
        if (c->phase == 0) {
            if (c->parent->isInactive() || !s.ready) return kIOReturnNotReady;
            if (r->pci.pa_tag->configRead16(kIOPCIConfigDeviceID) != 0x7af0 ||
                r->com.sc_device_family != IWX_DEVICE_FAMILY_AX210) return kIOReturnUnsupported;
            o = c->parent->nativeAwdlInterface;
            if (!o || c->parent->getAwdlInterface() != o || o->isInactive() || o->resourcesDisposed ||
                !o->startAttempted || o->getWorkLoop() != r->getMainWorkLoop()) return kIOReturnNotReady;
            if (o->activationPreflightResult.attempted || o->runtimeResult.attempted) return kIOReturnNotPermitted;
            if (!r->awdlNativeInterfaceActive || r->awdlNativeRadioOwner || s.owner ||
                s.session != ItlAwdlIdle || s.commandBusy || o->bsdPublishRequested ||
                o->getBSDInterface() || o->transportRegistered || o->isInterfaceEnabled()) return kIOReturnBusy;
            if (!o->configurationResult.attempted || o->configurationResult.native_result ||
                !o->templateResult.call.attempted || o->templateResult.call.native_result ||
                !o->templateResult.encoded_length || !o->syncParameters.ready() || !o->masterChannelValid ||
                (o->masterChannel != 6 && o->masterChannel != 44 && o->masterChannel != 149))
                return kIOReturnNotReady;
            if (c->tx && (!o->minimumRateValid || !o->stagedSyncRoleValid ||
                o->stagedSyncRole.state != 1 || o->stagedSyncRole.operation_mode != 1 ||
                !ItlAwdl::equalMac(o->stagedSyncRole.master,o->selfAddress.ether_addr_octet)))
                return kIOReturnNotReady;
            // Do not turn off STA or change its association from this ioctl.
            if (ic->ic_opmode != IEEE80211_M_STA || ic->ic_state != IEEE80211_S_INIT ||
                (ic->ic_if.if_flags & (IFF_UP | IFF_RUNNING))) return kIOReturnBusy;
            if (!ic->ic_bss || reinterpret_cast<iwx_node *>(ic->ic_bss)->in_id)
                return kIOReturnNotReady;
            auto state = o->nativeStartState();
            if (!state || !state->peerManager) return kIOReturnNotReady;
            o->retain(); c->object = o;
            out->registry_id = o->getRegistryEntryID();
            out->backend_before = o->backendCalls;
            out->creation_before = o->peerCreationCalls;
            out->state_before = ic->ic_state; out->mode_before = ic->ic_opmode;
            c->macIdBefore = reinterpret_cast<iwx_node *>(ic->ic_bss)->in_id;
            out->flags_before = ic->ic_if.if_flags; out->generation_before = r->com.sc_generation;
            out->channel = o->masterChannel;
            ItlAwdlBeginRequest begin = *c->request;
            begin.channel = o->masterChannel;
            // A typed, retained in-kernel owner is the sole exception to the
            // native/diagnostic exclusion. It cannot submit diagnostic TX.
            r->awdlNativeRadioOwner = o;
            r->awdlNativeTxQualification = c->tx != nullptr;
            r->awdlNativeTxSeriesObservation = c->seriesObservation;
            r->awdlNativeP2pQualification = c->p2p;
            r->awdlNativeRateMbps = c->tx ? o->minimumRateMbps : 0;
            bzero(r->awdlNativeAddress,sizeof(r->awdlNativeAddress));
            if (c->p2p) memcpy(r->awdlNativeAddress,o->selfAddress.ether_addr_octet,6);
            r->awdlNativeTimingSamples = r->awdlNativeTimingInvalid = r->awdlNativeMaxCompletionUs = 0;
            r->awdlNativeLastRate = r->awdlNativeTxGp2 = 0;
            if (c->tx) {
                __atomic_store_n(&r->awdlNativeTraceArmed,uint32_t(0),__ATOMIC_RELEASE);
                r->awdlNativeTraceCount = r->awdlNativeTraceIndex = 0;
                bzero(r->awdlNativeTrace,sizeof(r->awdlNativeTrace));
                o->removeProperty("AX201NativeTxTiming");
                memcpy(r->awdlNativeAddress,o->selfAddress.ether_addr_octet,6);
                c->tx->min_rate_mbps = o->minimumRateMbps; c->tx->limit = 4;
            }
            out->attempted = 1;
            o->removeProperty("AX201NativePeerWindow");
            o->removeProperty("AX201NativePeerProfile");
            o->removeProperty("AX201NativeP2pRadio");
            o->removeProperty("AX201NativeRadioFault");
            auto result = r->awdlBegin(o, &begin);
            if (c->tx) c->nextClockHostUs=r->awdlObserveNativeTxDma()+20000;
            if (result) { out->last_error = result; c->finished = true; }
            return kIOReturnSuccess;
        }
        if (c->phase == 1) {
            if (!o || c->parent->isInactive() || c->parent->nativeAwdlInterface != o ||
                o->resourcesDisposed || o->isInactive() || !s.ready ||
                r->awdlNativeRadioOwner != o || s.owner != o) {
                out->last_error = kIOReturnAborted; c->finished = true; return kIOReturnSuccess;
            }
            if (!r->awdlActive()) {
                if (s.stopReason != ItlAwdlExpired) out->last_error = s.lastError ? s.lastError : kIOReturnAborted;
                c->finished = true; return kIOReturnSuccess;
            }
            if (s.session != ItlAwdlListening) return kIOReturnSuccess;
            if (c->p2p) {
                if (r->awdlNativeP2pSession.failed) {
                    out->last_error=kIOReturnIOError; c->finished=true; return kIOReturnSuccess;
                }
                if (r->awdlNativeP2pSession.ended) { c->finished=true; return kIOReturnSuccess; }
                // A command ACK is insufficient: require a successful START
                // notification for this MAC/configuration/firmware generation.
                if (!r->awdlNativeP2pSession.ready(r->com.sc_generation)) return kIOReturnSuccess;
            }
            if (c->generation < 0) c->generation = r->com.sc_generation;
            if (c->generation != r->com.sc_generation) {
                out->last_error = kIOReturnAborted; c->finished = true; return kIOReturnSuccess;
            }
            out->radio_ready = 1;
            const auto in = reinterpret_cast<const iwx_node *>(ic->ic_bss);
            c->macIdDuring = in ? in->in_id : UINT32_MAX;
            ++c->macContextSamples;
            const uint32_t required = IWX_FLAG_MAC_ACTIVE | IWX_FLAG_BINDING_ACTIVE | IWX_FLAG_STA_ACTIVE;
            if (!in || in->in_color || c->macIdDuring != (c->tx || c->p2p ? 1U : 0U) ||
                (r->com.sc_flags & required) != required) {
                ++c->macContextMismatches;
                out->last_error = kIOReturnError; c->finished = true; return kIOReturnSuccess;
            }
            if (c->tx) {
                const auto now=r->awdlObserveNativeTxDma();
                if (c->seriesObservation && s.tx.pending && (!s.lastProbeUs || now<s.lastProbeUs ||
                    !ItlAwdl::txCompletionWithinLimit(now-s.lastProbeUs,true))) {
                    out->last_error=kIOReturnTimeout; c->finished=true; return kIOReturnSuccess;
                }
                if (now<c->nextClockHostUs) return kIOReturnSuccess;
                c->nextClockHostUs=now+20000;
            }
            ItlAwdlClockResponse clock = {};
            auto result = r->awdlReadClock(&clock);
            if (result) { out->last_error = result; c->finished = true; return kIOReturnSuccess; }
            ++out->samples;
            if (clock.flags & 4) ++out->rx_clock_valid;
            ItlAwdl::PeerTxWindow peerWindow = {};
            const auto peerResult = ItlAwdl::qualifyPeerWindow(s.schedule, clock.gp2_us,
                clock.host_end_us, uint8_t(s.channel), (clock.flags & 7) == 7, &peerWindow);
            ++c->peerWindows[unsigned(peerResult)];
            if (peerWindow.remainingUs) c->lastPeerWindow = peerWindow;
            if ((clock.flags & 3) == 3) {
                ++out->clock_valid;
                if (!c->timelineReady)
                    c->timelineReady = c->timeline.start(clock.gp2_us,o->syncParameters.periodTu(),uint8_t(o->presenceMode));
                ItlAwdl::LocalSyncBoundary boundary = {};
                if (!c->timelineReady || !c->timeline.sample(clock.gp2_us,&boundary)) {
                    out->last_error = kIOReturnError; c->finished = true; return kIOReturnSuccess;
                }
                // An independently chosen epoch can miss every peer window
                // even when both channel plans contain the same channel.
                // Re-derive this diagnostic boundary only from a fresh,
                // coherent peer schedule with the identical timing profile.
                // The unaligned CPU specimen below is never eligible for TX.
                const bool aligned = ItlAwdl::peerAlignedBoundary(s.schedule, clock.gp2_us,
                    clock.host_end_us, o->syncParameters.periodTu(), uint8_t(o->presenceMode),
                    (clock.flags & 7) == 7, &boundary);
                if (aligned) ++c->alignedSamples;
                else ++c->alignmentUnavailable;
                uint8_t frame[ItlAwdl::SyncActionMaxLength];
                auto length = o->encodeQualificationFrame(clock.gp2_us, frame, sizeof(frame),
                    &boundary,uint16_t(s.tx.submitted),c->unicast ? c->request->peer : nullptr);
                if (!length) { out->last_error = kIOReturnError; c->finished = true; }
                else { ++out->encoded; out->encoded_length = uint32_t(length); }
                const bool overlap = aligned && peerResult == ItlAwdl::PeerWindowResult::Ready &&
                    boundary.remainingUs > 12000 &&
                    o->channelSequence.plan().channels[boundary.slot] == s.channel;
                if (overlap) ++c->overlapWindows;
                const auto navRemaining = c->tx ? r->awdlObservedReservation.remaining(
                    clock.gp2_us,clock.host_end_us) : 0;
                if (c->tx && overlap && navRemaining) {
                    ++c->navDeferrals;
                    if (navRemaining > c->maxNavRemainingUs) c->maxNavRemainingUs = navRemaining;
                }
                if (c->tx && length) {
                    // Bound both modes to four frames and one pending TX.
                    // Transmit only inside a selected-channel EAW, with time
                    // left for firmware submission. The diagnostic does not
                    // claim actual native synchronization has been enabled.
                    const uint16_t window = boundary.counter / uint8_t(o->presenceMode);
                    if (s.tx.failed) {
                        out->last_error = kIOReturnIOError; c->finished = true;
                    } else if (r->awdlNativeTimingInvalid || !ItlAwdl::txCompletionWithinLimit(
                        r->awdlNativeMaxCompletionUs,c->seriesObservation)) {
                        out->last_error = kIOReturnTimeout; c->finished = true;
                    } else if (s.tx.submitted < 4 && !s.tx.pending &&
                        (!c->sentWindow || window != c->lastWindow) && overlap && !navRemaining) {
                        auto sent = r->awdlSubmitNativeSync(o,frame,length,clock.gp2_us,c->unicast);
                        if (!sent) {
                            if (!c->tx->windows_used) c->tx->first_counter = boundary.counter;
                            c->tx->last_counter = boundary.counter; ++c->tx->windows_used;
                            c->sentWindow = true; c->lastWindow = window;
                        } else if (sent != kIOReturnBusy && sent != kIOReturnNotReady) {
                            out->last_error = sent; c->finished = true;
                        }
                    }
                }
            }
            return kIOReturnSuccess;
        }
        // One cleanup for success, radio errors, timeout and controller stop.
        // Snapshot only aggregate RX counters; never expose captured payloads.
        if (o && r->awdlNativeRadioOwner == o) {
            out->rx_total = s.rxTotal; out->rx_awdl = s.rxAwdl; out->rx_peer = s.rxPeer;
            out->rx_dropped = s.rxDropped; out->rx_invalid = s.rxInvalid;
            if (s.owner == o && s.ready) {
                auto result = r->awdlCommand(o, ItlAwdlEnd, nullptr, nullptr);
                if (result && !out->last_error) out->last_error = result;
            }
            r->awdlPublishNativePeerProfile(o);
            r->awdlPublishNativeP2pSession(o);
            r->awdlPublishNativeRadioFault(o);
            r->awdlNativeRadioOwner = nullptr;
            r->awdlNativeTxQualification = false;
            r->awdlNativeTxSeriesObservation = false;
            r->awdlNativeP2pQualification = false;
            bzero(r->awdlNativeAddress,sizeof(r->awdlNativeAddress));
            r->awdlNativeRateMbps = 0;
        }
        if (c->tx) {
            __atomic_store_n(&r->awdlNativeTraceArmed,uint32_t(0),__ATOMIC_RELEASE);
            r->awdlPublishNativeTxTiming(o);
            c->tx->submitted = s.tx.submitted; c->tx->completed = s.tx.completed;
            c->tx->succeeded = s.tx.succeeded; c->tx->failed = s.tx.failed;
            c->tx->pending = s.tx.pending; c->tx->aborted = s.tx.aborted;
            c->tx->timing_samples = r->awdlNativeTimingSamples;
            c->tx->timing_invalid = r->awdlNativeTimingInvalid;
            c->tx->max_completion_delay_us = r->awdlNativeMaxCompletionUs;
            c->tx->last_rate_flags = r->awdlNativeLastRate;
            c->tx->last_firmware_status = s.tx.last_firmware_status;
        }
        out->stop_reason = s.stopReason;
        out->state_after = ic->ic_state; out->mode_after = ic->ic_opmode;
        c->macIdAfter = ic->ic_bss ? reinterpret_cast<iwx_node *>(ic->ic_bss)->in_id : UINT32_MAX;
        if (c->macIdAfter && !out->last_error) out->last_error = kIOReturnError;
        out->flags_after = ic->ic_if.if_flags; out->generation_after = r->com.sc_generation;
        out->radio_stopped = s.session == ItlAwdlIdle && !r->awdlNativeRadioOwner && !s.owner &&
            ic->ic_opmode == IEEE80211_M_STA && !(ic->ic_if.if_flags & (IFF_UP | IFF_RUNNING));
        if (o && !o->resourcesDisposed) {
            auto windows = OSDictionary::withCapacity(14);
            if (windows) {
                auto put = [&](const char *key, uint32_t value) {
                    auto number = OSNumber::withNumber(value, 32);
                    if (number) { windows->setObject(key, number); number->release(); }
                };
                const char *names[] = {"clock_unqualified", "schedule_unavailable",
                    "other_channel", "edge", "ready"};
                static_assert(nitems(names) == unsigned(ItlAwdl::PeerWindowResult::Count), "window results");
                for (unsigned i = 0; i < nitems(names); ++i) put(names[i], c->peerWindows[i]);
                put("samples", out->samples); put("local_overlap", c->overlapWindows);
                put("aligned_samples", c->alignedSamples);
                put("alignment_unavailable", c->alignmentUnavailable);
                put("tx_nav_deferred_samples", c->navDeferrals);
                put("max_observed_nav_remaining_us", c->maxNavRemainingUs);
                put("mac_id_before", c->macIdBefore); put("mac_id_during", c->macIdDuring);
                put("mac_id_after", c->macIdAfter); put("mac_context_samples", c->macContextSamples);
                put("mac_context_mismatches", c->macContextMismatches);
                put("selected_channel", out->channel);
                put("tx_series_observation", c->seriesObservation);
                put("last_peer_channel", c->lastPeerWindow.channel);
                put("last_peer_counter", c->lastPeerWindow.counter);
                put("last_peer_elapsed_us", c->lastPeerWindow.elapsedUs);
                put("last_peer_remaining_us", c->lastPeerWindow.remainingUs);
                o->setProperty("AX201NativePeerWindow", windows); windows->release();
            }
            out->backend_after = o->backendCalls; out->creation_after = o->peerCreationCalls;
            out->interface_enabled = o->isInterfaceEnabled();
            out->tx_enabled = o->transportTx && o->transportTx->isEnabled();
            out->rx_enabled = o->transportRx && o->transportRx->isEnabled();
            out->bsd_present = o->getBSDInterface() != nullptr;
        }
        return kIOReturnSuccess;
    };
    auto result = gate->runAction(action, &call);
    if (!result && call.object) {
        call.phase = 1;
        // The independent radio timer also expires the lease. Sleeps happen
        // outside the gate, allowing RX, firmware completion and power teardown.
        // In TX qualification, inspect completed DMA at 1 ms between the
        // 20 ms clock/RF steps. DMA observation never consumes a response.
        // The TX completion limit is unchanged.
        const uint32_t delayMs=tx ? 1 : 20;
        for (uint32_t elapsed = 0; !call.finished && elapsed < request->dwell_ms + 100; elapsed += delayMs) {
            IOSleep(delayMs);
            auto step = gate->runAction(action, &call);
            if (step) { out->last_error = step; break; }
        }
    }
    if (call.object) {
        call.phase = 2;
        auto cleanup = gate->runAction(action, &call);
        if (cleanup && !out->last_error) out->last_error = cleanup;
        call.object->release();
    }
    gate->release(); radio->release(); parent->release();
    return result;
}

// The bounded root transaction retains the controller, HAL, gate and native
// object. Only this timer operates the radio; sleeping userspace is not a clock.
static uint64_t nativeRuntimeNowUs() {
    uint64_t absolute = 0, nanos = 0;
    clock_get_uptime(&absolute);
    absolutetime_to_nanoseconds(absolute, &nanos);
    return nanos / 1000;
}

bool ItlAwdlNativeInterface::runtimeRadioReady() const {
    auto r = runtimeRadio;
    if (!r || !runtimeTimer || resourcesDisposed || isInactive() || !startAttempted ||
        r->awdlNativeRadioOwner != this || r->awdl.owner != this || !r->awdlNativeRuntime ||
        r->awdl.session != ItlAwdlListening || r->awdl.commandBusy ||
        r->com.sc_generation != runtimeGeneration ||
        !r->awdlNativeP2pSession.ready(runtimeGeneration) ||
        nativeRuntimeNowUs() >= r->awdl.deadlineUs) return false;
    const auto ic = &r->com.sc_ic;
    const auto in = reinterpret_cast<const iwx_node *>(ic->ic_bss);
    const uint32_t required = IWX_FLAG_MAC_ACTIVE | IWX_FLAG_BINDING_ACTIVE | IWX_FLAG_STA_ACTIVE;
    return in && in->in_id == 1 && !in->in_color && ic->ic_state == IEEE80211_S_RUN &&
        ic->ic_opmode == IEEE80211_M_MONITOR && (ic->ic_if.if_flags & IFF_RUNNING) &&
        (r->com.sc_flags & required) == required &&
        !(r->com.sc_flags & (IWX_FLAG_HW_ERR | IWX_FLAG_RFKILL | IWX_FLAG_SHUTDOWN));
}

void ItlAwdlNativeInterface::synchronizationRadioStopped() {
    if (!runtimeRadio || runtimePhase == RuntimeDone || runtimePhase == RuntimeIdle) return;
    runtimeSyncEnabled = runtimeClockQualified = false;
    runtimePhase = RuntimeStopping;
    runtimeTimeline.clear();
    if (runtimeTimer) runtimeTimer->cancelTimeout();
}

void ItlAwdlNativeInterface::runtimeTimeout(OSObject *owner, IOTimerEventSource *) {
    static_cast<ItlAwdlNativeInterface *>(owner)->runtimeTick();
}

void ItlAwdlNativeInterface::runtimeTick() {
    auto r = runtimeRadio;
    if (!r || (runtimePhase != RuntimeStarting && runtimePhase != RuntimeRunning)) return;
    ++runtimeResult.ticks;
    auto fail = [&](IOReturn error) {
        if (!runtimeResult.last_error) runtimeResult.last_error = error;
        synchronizationRadioStopped();
    };
    auto rearm = [&]() {
        if (runtimePhase != RuntimeStarting && runtimePhase != RuntimeRunning) return;
        auto result = runtimeTimer->setTimeoutUS(ItlAwdl::NativeRuntimeTickUs);
        if (result) fail(result);
    };
    if (!r->awdlActive()) { fail(r->awdl.lastError ? r->awdl.lastError : kIOReturnAborted); return; }
    if (runtimePhase == RuntimeStarting) {
        if (r->awdlNativeP2pSession.failed) { fail(kIOReturnIOError); return; }
        if (!r->awdlNativeP2pSession.ready(r->com.sc_generation)) { rearm(); return; }
        runtimeGeneration = r->com.sc_generation;
    }
    if (!runtimeRadioReady()) { fail(kIOReturnAborted); return; }
    ItlAwdlClockResponse clock = {};
    auto error = r->awdlReadClock(&clock);
    if (error) { fail(error); return; }
    // RX GP2 and sampled GP2 must correlate before native RX sees this epoch.
    runtimeClockQualified = (clock.flags & 7) == 7;
    if (runtimePhase == RuntimeRunning && (clock.flags & 3) != 3) { fail(kIOReturnNotReady); return; }
    if (!runtimeClockQualified) { rearm(); return; }
    ++runtimeResult.clock_valid;
    if (runtimePhase == RuntimeStarting) {
        ItlAwdl::ScheduleSlot anchor = {};
        if (!r->awdl.schedule.anchor(clock.gp2_us, clock.host_end_us,
            syncParameters.periodTu(), uint8_t(presenceMode), &anchor)) { rearm(); return; }
        if (!runtimeTimeline.start(anchor.start_gp2_us, syncParameters.periodTu(),
            uint8_t(presenceMode), anchor.counter)) { fail(kIOReturnBadArgument); return; }
        auto state = nativeStartState();
        auto peerClass = nativePeerClass();
        if (!state || !state->peerManager || !peerClass || !state->peerManager->metaCast(peerClass)) {
            fail(kIOReturnNotReady); return;
        }
        auto supervisor = nativeAwdlSupervisor(state->peerManager);
        if (!supervisor || !supervisor->metaCast("IO80211P2PSupervisor")) { fail(kIOReturnNotReady); return; }
        runtimePhase = RuntimeReady;
        runtimeEnableInvoked = true;
        auto &trace = runtimeResult.activation;
        trace.attempted = 1;
        trace.backend_before = backendCalls;
        backendTrace = &trace;
        trace.native_result = nativeAwdlEnable(state->peerManager);
        backendTrace = nullptr;
        trace.backend_after = backendCalls;
        // Native enable may return success without calling SYNC_ENABLED.
        // Require the backend transition and unchanged real radio ownership.
        if (trace.native_result || !runtimeSyncEnabled || !runtimeRadioReady()) {
            fail(trace.native_result ? trace.native_result : kIOReturnNotReady); return;
        }
        runtimeNativeEnabled = true;
        runtimePhase = RuntimeRunning;
        runtimeResult.ready = 1;
    }
    ItlAwdl::LocalSyncBoundary boundary = {};
    if (!runtimeTimeline.sample(clock.gp2_us, &boundary)) { fail(kIOReturnError); return; }
    ItlAwdl::NativeAvailabilityWindowContext context = {};
    if (!ItlAwdl::nativeRuntimeWindow(boundary, syncParameters.periodTu(), uint8_t(presenceMode),
        static_cast<ItlAwdl::NativeSyncRole>(stagedSyncRole.state), stagedSyncRole.master,
        stagedSyncRole.master, r->awdl.channel, &context)) { fail(kIOReturnBadArgument); return; }
    if (!runtimeHadWindow || runtimeWindow != context.counter) {
        ItlAwdl::NativeAvailabilityWindowStart event = {};
        if (!ItlAwdl::encodeNativeAvailabilityWindowStart(context, &event)) { fail(kIOReturnError); return; }
        // false means the controller's gate completed dispatch, not a peer ACK.
        if (reportDataPathEvents(ItlAwdl::NativeAvailabilityWindowStartEvent,
            event.bytes, sizeof(event), true)) {
            ++runtimeResult.aw_event_errors; fail(kIOReturnError); return;
        }
        ++runtimeResult.aw_events;
        runtimeHadWindow = true; runtimeWindow = context.counter;
    }
    if (runtimePhase != RuntimeRunning || !runtimeSyncEnabled || !runtimeRadioReady()) return;
    const auto now = nativeRuntimeNowUs();
    if (r->awdl.tx.failed || r->awdlNativeTimingInvalid) { fail(kIOReturnIOError); return; }
    if (r->awdl.tx.pending && now - r->awdl.lastProbeUs > 100000) { fail(kIOReturnTimeout); return; }
    if (!r->awdl.tx.pending && r->awdl.tx.submitted < ItlAwdl::NativeRuntimeTxLimit &&
        now >= runtimeNextTxUs && boundary.remainingUs > 12000 &&
        channelSequence.plan().channels[boundary.slot] == r->awdl.channel) {
        uint8_t frame[ItlAwdl::SyncActionMaxLength];
        auto length = encodeQualificationFrame(clock.gp2_us, frame, sizeof(frame), &boundary,
            uint16_t(r->awdl.tx.submitted));
        if (!length) { fail(kIOReturnBadArgument); return; }
        error = r->awdlSubmitNativeSync(this, frame, length, clock.gp2_us);
        if (!error) runtimeNextTxUs = now + uint32_t(syncParameters.framePeriodTu()) * ItlAwdl::TuUs;
        else if (error != kIOReturnBusy && error != kIOReturnNotReady) { fail(error); return; }
    }
    rearm();
}

void ItlAwdlNativeInterface::synchronizationReceive(const uint8_t *bytes, size_t length,
    uint32_t channel, int32_t rssi, uint32_t gp2) {
    if (runtimePhase != RuntimeRunning || !runtimeNativeEnabled || !runtimeHadWindow || !runtimeClockQualified ||
        !runtimeSyncEnabled || !runtimeRadioReady() || channel != runtimeRadio->awdl.channel) return;
    const ItlAwdl::NativeActionRxContext rx = {channel, rssi, gp2, true};
    auto error = receiveAction(bytes, length, rx);
    if (error) { ++runtimeResult.rx_rejected; runtimeResult.rx_last_error = error; }
    else ++runtimeResult.rx_handed_off;
}

void ItlAwdlNativeInterface::finishSynchronization() {
    if (!runtimeRadio || runtimePhase == RuntimeDone) return;
    auto r = runtimeRadio;
    synchronizationRadioStopped();
    if (runtimeTimer) {
        auto loop = runtimeTimer->getWorkLoop();
        if (loop) loop->removeEventSource(runtimeTimer);
        OSSafeReleaseNULL(runtimeTimer);
    }
    // Same native manager is disabled and retained, never freed/recreated.
    if (runtimeEnableInvoked && !resourcesDisposed && startAttempted) {
        auto state = nativeStartState();
        if (state && state->peerManager) {
            ++runtimeResult.native_disable_calls;
            nativeAwdlDisable(state->peerManager, 0);
        }
    }
    runtimeNativeEnabled = false;
    if (r->awdlNativeRadioOwner == this) {
        if (r->awdl.owner == this && r->awdl.ready) {
            const auto result = r->awdlCommand(this, ItlAwdlEnd, nullptr, nullptr);
            if (result && !runtimeResult.last_error) runtimeResult.last_error = result;
        }
        r->awdlPublishNativeP2pSession(this);
        r->awdlPublishNativeRadioFault(this);
        r->awdlPublishNativePeerProfile(this);
        r->awdlPublishNativeTxTiming(this);
        r->awdlNativeRadioOwner = nullptr;
        r->awdlNativeRuntime = false;
        r->awdlNativeTxQualification = r->awdlNativeP2pQualification = false;
        r->awdlNativeTxSeriesObservation = false;
        bzero(r->awdlNativeAddress, sizeof(r->awdlNativeAddress));
        r->awdlNativeRateMbps = 0;
    }
    const auto &s = r->awdl;
    runtimeResult.submitted = s.tx.submitted; runtimeResult.completed = s.tx.completed;
    runtimeResult.succeeded = s.tx.succeeded; runtimeResult.failed = s.tx.failed;
    runtimeResult.pending = s.tx.pending; runtimeResult.aborted = s.tx.aborted;
    runtimeResult.timing_samples = r->awdlNativeTimingSamples;
    runtimeResult.timing_invalid = r->awdlNativeTimingInvalid;
    runtimeResult.max_completion_delay_us = r->awdlNativeMaxCompletionUs;
    runtimeResult.stop_reason = s.stopReason;
    if (s.lastError && !runtimeResult.last_error) runtimeResult.last_error = s.lastError;
    runtimeResult.stopped = s.session == ItlAwdlIdle && !s.owner && !r->awdlNativeRadioOwner;
    runtimeResult.sync_enabled_after = runtimeSyncEnabled;
    runtimeResult.activation.interface_enabled = isInterfaceEnabled();
    runtimeResult.activation.tx_enabled = transportTx && transportTx->isEnabled();
    runtimeResult.activation.rx_enabled = transportRx && transportRx->isEnabled();
    runtimeResult.activation.bsd_present = getBSDInterface() != nullptr;
    setProperty("AX201NativeRuntimeResult", &runtimeResult, sizeof(runtimeResult));
    runtimeRadio = nullptr;
    runtimePhase = RuntimeDone;
}

IOReturn ItlAwdlNativeInterface::runSynchronization(AirportItlwm *parent, ItlIwx *radio,
    const ItlAwdlBeginRequest *request, ItlAwdlNativeRuntimeResponse *out) {
    if (!parent || !radio || !request || !out || request->version != ITL_AWDL_ABI_VERSION ||
        request->channel || request->reserved || request->padding[0] || request->padding[1] ||
        request->dwell_ms < 1000 || request->dwell_ms > 5000 || !ItlAwdl::unicastMac(request->peer))
        return kIOReturnBadArgument;
    if (strcmp(osversion, "24H32")) return kIOReturnUnsupported;
    auto gate = radio->getMainCommandGate();
    if (!gate || !gate->getWorkLoop() || gate->getWorkLoop()->inGate()) return kIOReturnNotReady;
    NativeProbeLease lease;
    if (!lease.owned) return kIOReturnBusy;
    parent->retain(); radio->retain(); gate->retain();
    struct Call { AirportItlwm *parent; ItlIwx *radio; const ItlAwdlBeginRequest *request;
        ItlAwdlNativeInterface *object; bool done; } call = {parent, radio, request, nullptr, false};
    auto result = gate->runAction([](OSObject *, void *arg, void *, void *, void *) -> IOReturn {
        auto c = static_cast<Call *>(arg); auto r = c->radio; auto o = c->parent->nativeAwdlInterface;
        auto &s = r->awdl; auto ic = &r->com.sc_ic;
        if (c->parent->isInactive() || !s.ready || !o || c->parent->getAwdlInterface() != o ||
            o->resourcesDisposed || o->isInactive() || !o->startAttempted ||
            o->getWorkLoop() != r->getMainWorkLoop()) return kIOReturnNotReady;
        if (r->pci.pa_tag->configRead16(kIOPCIConfigDeviceID) != 0x7af0 ||
            r->com.sc_device_family != IWX_DEVICE_FAMILY_AX210) return kIOReturnUnsupported;
        if (o->activationPreflightResult.attempted || o->runtimeResult.attempted) return kIOReturnNotPermitted;
        if (!r->awdlNativeInterfaceActive || r->awdlNativeRadioOwner || s.owner || s.commandBusy ||
            s.session != ItlAwdlIdle || o->bsdPublishRequested || o->getBSDInterface() ||
            o->transportRegistered || o->isInterfaceEnabled()) return kIOReturnBusy;
        if (ic->ic_opmode != IEEE80211_M_STA || ic->ic_state != IEEE80211_S_INIT ||
            (ic->ic_if.if_flags & (IFF_UP | IFF_RUNNING))) return kIOReturnBusy;
        if (!o->configurationResult.attempted || o->configurationResult.native_result ||
            !o->templateResult.call.attempted || o->templateResult.call.native_result ||
            !o->syncParameters.ready() || !o->syncTemplate.length() || !o->stagedSyncRoleValid ||
            o->stagedSyncRole.state != 1 || o->stagedSyncRole.operation_mode != 1 ||
            !ItlAwdl::equalMac(o->stagedSyncRole.master, o->selfAddress.ether_addr_octet) ||
            !o->channelSequence.ready() || o->channelSequence.plan().step_aw != o->presenceMode ||
            !o->minimumRateValid || !o->masterChannelValid) return kIOReturnNotReady;
        // First engine supports a real fixed-channel schedule. Reject hopping
        // plans rather than advertising radio visits that never happen.
        for (auto channel : o->channelSequence.plan().channels)
            if (channel != o->masterChannel) return kIOReturnUnsupported;
        o->retain(); c->object = o;
        o->runtimeRadio = r; o->runtimePhase = RuntimeStarting;
        o->runtimeResult = {};
        o->runtimeResult.attempted = 1;
        o->runtimeResult.tx_limit = ItlAwdl::NativeRuntimeTxLimit;
        o->runtimeResult.channel = o->masterChannel;
        o->runtimeResult.activation.version = ITL_AWDL_ABI_VERSION;
        o->runtimeResult.activation.present = 1;
        o->runtimeResult.activation.registry_id = o->getRegistryEntryID();
        o->runtimeTimer = IOTimerEventSource::timerEventSource(o, runtimeTimeout);
        if (!o->runtimeTimer) { o->runtimeResult.last_error = kIOReturnNoMemory; return kIOReturnSuccess; }
        auto error = r->getMainWorkLoop()->addEventSource(o->runtimeTimer);
        if (error) { o->runtimeResult.last_error = error; return kIOReturnSuccess; }
        r->awdlNativeRadioOwner = o;
        r->awdlNativeRuntime = r->awdlNativeTxQualification = r->awdlNativeP2pQualification = true;
        r->awdlNativeTxSeriesObservation = true; // Discovery session, not ManagementTx.
        r->awdlNativeRateMbps = o->minimumRateMbps;
        memcpy(r->awdlNativeAddress, o->selfAddress.ether_addr_octet, 6);
        r->awdlNativeTimingSamples = r->awdlNativeTimingInvalid = r->awdlNativeMaxCompletionUs = 0;
        r->awdlNativeLastRate = r->awdlNativeTxGp2 = 0;
        r->awdlNativeTraceCount = r->awdlNativeTraceIndex = 0;
        bzero(r->awdlNativeTrace, sizeof(r->awdlNativeTrace));
        ItlAwdlBeginRequest begin = *c->request; begin.channel = o->masterChannel;
        error = r->awdlBegin(o, &begin);
        if (!error) error = o->runtimeTimer->setTimeoutUS(ItlAwdl::NativeRuntimeTickUs);
        if (error) o->runtimeResult.last_error = error;
        return kIOReturnSuccess;
    }, &call);
    if (call.object) {
        for (uint32_t elapsed = 0; !result && !call.done && elapsed < request->dwell_ms + 200; elapsed += 20) {
            IOSleep(20); // Object lifetime only; all synchronization runs on the kernel timer.
            result = gate->runAction([](OSObject *, void *arg, void *, void *, void *) -> IOReturn {
                auto c = static_cast<Call *>(arg); auto o = c->object;
                c->done = o->runtimePhase == RuntimeStopping || o->runtimePhase == RuntimeDone ||
                    o->runtimeResult.last_error || c->parent->isInactive();
                return kIOReturnSuccess;
            }, &call);
        }
        auto cleanup = gate->runAction([](OSObject *, void *arg, void *, void *, void *) -> IOReturn {
            static_cast<Call *>(arg)->object->finishSynchronization(); return kIOReturnSuccess;
        }, &call);
        *out = call.object->runtimeResult;
        if (cleanup && !out->last_error) out->last_error = cleanup;
        call.object->release();
    }
    gate->release(); radio->release(); parent->release();
    return result;
}

IOReturn ItlAwdlNativeInterface::receiveAction(const uint8_t *frame, size_t length,
    const ItlAwdl::NativeActionRxContext &rx) {
    ItlAwdl::NativeActionMetadata metadata = {};
    ItlAwdl::NativeActionView view = {};
    if (!frame || length < 24 ||
        !ItlAwdl::encodeNativeActionMetadata(frame + 10, rx, &metadata) ||
        !ItlAwdl::nativeActionView(frame, length, metadata.bytes, sizeof(metadata), &view))
        return kIOReturnBadArgument;
    auto loop = getWorkLoop();
    if (strcmp(osversion, "24H32") || !loop || !loop->inGate() ||
        isInactive() || !startAttempted || !runtimeNativeEnabled || !runtimeHadWindow ||
        runtimePhase != RuntimeRunning || !runtimeRadioReady()) return kIOReturnNotReady;
    // Audited 24H32 bpfTapInput sends management buffers to the native manager
    // before optional BPF/BSD delivery. Require the real engine and native
    // enable transaction; BSD data queues are not a management-RX prerequisite.
    apple80211_awdl_sync_enabled sync = {};
    sync.version = 1;
    if (getAWDL_SYNC_ENABLED(&sync) || !sync.enabled) return kIOReturnNotReady;
    auto pool = getMgmtFramePool();
    if (!pool) return kIOReturnNotReady;
    pool->retain();
    auto buffer = pool->getBuffer();
    if (!buffer) { pool->release(); return kIOReturnNoResources; }
    auto bytes = buffer->getBytesNoCopy();
    if (!bytes || view.length > buffer->getCapacity() || view.length > UINT32_MAX) {
        buffer->returnBuffer();
        pool->release();
        return kIOReturnNoSpace;
    }
    memcpy(bytes, view.body, view.length);
    buffer->setLength(view.length);
    // Native input may queue the buffer and copies 72 metadata bytes in that
    // case. The encoded header is local and mutable, never caller-owned.
    bpfTapInput(buffer, 12, metadata.bytes, sizeof(metadata));
    // The buffer was consumed, including native disabled/drop paths. Its
    // controller-owned pool remains alive through peer-manager teardown.
    pool->release();
    return kIOReturnSuccess; // Handoff only; not a parsed or discovered peer.
}

IOReturn ItlAwdlNativeInterface::enable(unsigned int options) {
    ++bsdEnableCalls;
    // VirtualInterface::enable discards lower-base errors. Keep the native
    // enabled bit false until setInterfaceEnable can start a real backend.
    return IOSkywalkNetworkInterface::enable(options);
}
SInt32 ItlAwdlNativeInterface::setInterfaceEnable(bool enabled) {
    if (enabled) { ++bsdEnableDenied; return kIOReturnNotReady; }
    // No true transition has reached VirtualInterface or the radio yet.
    return IO80211SkywalkInterface::setInterfaceEnable(false);
}
IOReturn ItlAwdlNativeInterface::prepareBSDInterface(ifnet_t interface, unsigned int options) {
    ++bsdPrepareCalls;
    // The native Virtual wrapper continues on a failed lower-base preparation
    // and can overwrite its error. Only configure a successfully prepared ifnet.
    auto result = IO80211SkywalkInterface::prepareBSDInterface(interface, options);
    if (!result) {
        configureIfnet();
        if (!attachToBpf()) result = kIOReturnError;
    }
    bsdPrepareError = static_cast<uint32_t>(result);
    IOLog("AX201 native BSD: prepare result=0x%x\n", result);
    return result;
}

void ItlAwdlNativeInterface::describeBSD(ItlAwdlNativeBSDResponse *out) {
    out->publication_requested = bsdPublishRequested;
    out->publish_calls = bsdPublishCalls;
    out->prepare_calls = bsdPrepareCalls;
    out->prepare_error = bsdPrepareError;
    out->enable_calls = bsdEnableCalls;
    out->enable_denied = bsdEnableDenied;
    out->bsd_present = getBSDInterface() != nullptr;
    out->callback_calls = transportCallbacks;
    out->registry_id = getRegistryEntryID();
    if (out->bsd_present) {
        const auto name = getBSDName();
        if (name) strlcpy(out->bsd_name, name, sizeof(out->bsd_name));
    }
    auto clients = getClientIterator();
    if (clients) {
        while (auto object = clients->getNextObject()) {
            if (object->metaCast("IOSkywalkNetworkBSDClient")) ++out->bsd_clients;
        }
        clients->release();
    }
    if (transportTx) {
        out->tx_initialize_calls = transportTx->initializeCalls;
        out->tx_initialized = transportTx->initialized;
        out->tx_initialize_error = transportTx->initializeError;
        out->tx_kernel_queue = transportTx->getKernNetifQueue() != nullptr;
        out->tx_enabled = transportTx->isEnabled();
    }
    if (transportRx) {
        out->rx_initialize_calls = transportRx->initializeCalls;
        out->rx_initialized = transportRx->initialized;
        out->rx_initialize_error = transportRx->initializeError;
        out->rx_kernel_queue = transportRx->getKernNetifQueue() != nullptr;
        out->rx_enabled = transportRx->isEnabled();
    }
}

IOReturn ItlAwdlNativeInterface::managedBSD(AirportItlwm *parent, ItlIwx *radio,
    const ItlAwdlNativeBSDRequest *request, ItlAwdlNativeBSDResponse *out) {
    if (!parent || !radio || !request || !out || request->version != ITL_AWDL_ABI_VERSION ||
        request->operation > ItlAwdlBSDPublish || request->reserved[0] || request->reserved[1])
        return kIOReturnBadArgument;
    if (strcmp(osversion, "24H32")) return kIOReturnUnsupported;
    auto gate = radio->getMainCommandGate();
    if (!gate || !gate->getWorkLoop()) return kIOReturnNotReady;
    NativeProbeLease lease;
    if (!lease.owned) return kIOReturnBusy;
    bzero(out, sizeof(*out));
    out->version = ITL_AWDL_ABI_VERSION;
    struct Call { AirportItlwm *parent; ItlIwx *radio; IOWorkLoop *loop;
        const ItlAwdlNativeBSDRequest *request; ItlAwdlNativeBSDResponse *out;
        ItlAwdlNativeInterface *publish; } call = {parent, radio, gate->getWorkLoop(), request, out, nullptr};
    auto result = gate->runAction([](OSObject *, void *argument, void *, void *, void *) -> IOReturn {
        auto c = static_cast<Call *>(argument);
        if (c->parent->isInactive()) return kIOReturnNotReady;
        ItlAwdlStatusResponse status = {};
        auto result = c->radio->awdlCommand(c->parent, ItlAwdlStatus, nullptr, &status);
        if (result) return result;
        if (status.device_id != 0x7af0) return kIOReturnUnsupported;
        if (status.session != ItlAwdlIdle) return kIOReturnBusy;
        auto object = c->parent->nativeAwdlInterface;
        if (!object) return c->request->operation == ItlAwdlBSDInspect ? kIOReturnSuccess : kIOReturnNotReady;
        if (c->parent->getAwdlInterface() != object || object->isInactive()) return kIOReturnNotReady;
        if (c->request->operation == ItlAwdlBSDPublish && !object->bsdPublishRequested) {
            if (!object->transportRegistered || !object->startAttempted ||
                object->getWorkLoop() != c->loop || object->transportLoop != c->loop)
                return kIOReturnNotReady;
            object->bsdPublishRequested = true;
            ++object->bsdPublishCalls;
            object->retain();
            c->publish = object;
        }
        object->describeBSD(c->out);
        return kIOReturnSuccess;
    }, &call);
    if (result) return result;
    if (call.publish) {
        // Native matching can start a BSD client which takes the interface
        // work-loop gate. Publish outside that gate, with the object retained.
        auto published = call.publish->deferBSDAttach(false);
        result = gate->runAction([](OSObject *, void *argument, void *, void *, void *) -> IOReturn {
            auto c = static_cast<Call *>(argument);
            c->publish->describeBSD(c->out);
            return kIOReturnSuccess;
        }, &call);
        out->last_error = static_cast<uint32_t>(published ? published : result);
        call.publish->release();
    }
    return kIOReturnSuccess;
}

// These callbacks remain attached to the retained object's lifetime, never a
// user-client stack frame. The native BSD client may initialize real queues;
// radio enable remains blocked, so neither callback may consume packets yet.
unsigned int ItlAwdlNativeInterface::transportDequeue(OSObject *owner,
    IOSkywalkTxSubmissionQueue *, IOSkywalkPacket *const *, unsigned int, void *) {
    ++static_cast<ItlAwdlNativeInterface *>(owner)->transportCallbacks;
    return 0;
}
unsigned int ItlAwdlNativeInterface::transportEnqueue(OSObject *owner,
    IOSkywalkRxCompletionQueue *, IOSkywalkPacket **, unsigned int, void *) {
    ++static_cast<ItlAwdlNativeInterface *>(owner)->transportCallbacks;
    return 0;
}

IOReturn ItlAwdlNativeInterface::clearTransport(bool systemStop) {
    if (bsdPublishRequested && !systemStop) return kIOReturnNotPermitted;
    // Deregistration alone does not detach a live BSD client. This boundary is
    // only qualified before publication; never free beneath an attached ifnet.
    if (getBSDInterface()) return kIOReturnBusy;
    if (transportRx) transportRx->disable();
    if (transportTx) transportTx->disable();
    if (transportRegistrationAttempted) {
        auto result = deregisterEthernetInterface(0);
        if (result) { ++transportCleanupErrors; return result; }
        if (transportRegistered) ++transportDeregisters;
        transportRegistered = transportRegistrationAttempted = false;
        if (mExpansionData->fRegistrationInfo || mExpansionData2->fRegistrationInfo ||
            (transportTx && transportTx->getInterface()) || (transportRx && transportRx->getInterface())) {
            ++transportCleanupErrors;
            return kIOReturnError;
        }
    }
    if (transportRxAdded) {
        auto result = transportLoop->removeEventSource(transportRx);
        if (result) { ++transportCleanupErrors; return result; }
        transportRxAdded = false;
    }
    if (transportTxAdded) {
        auto result = transportLoop->removeEventSource(transportTx);
        if (result) { ++transportCleanupErrors; return result; }
        transportTxAdded = false;
    }
    // LogicalLink::setInterface(nullptr) checks both queues out of the pool.
    // QueueSet::free clears their weak QueueSet pointers before releasing them.
    // No initialize(nullptr) here: a future BSD client must supply real rings.
    OSSafeReleaseNULL(transportRx);
    OSSafeReleaseNULL(transportTx);
    OSSafeReleaseNULL(transportPool);
    OSSafeReleaseNULL(transportLoop);
    return kIOReturnSuccess;
}

IOReturn ItlAwdlNativeInterface::prepareTransport(IOWorkLoop *loop, uint32_t stopAfter) {
    if (transportRegistered) return stopAfter ? kIOReturnBusy : kIOReturnSuccess;
    if (!loop || getBSDInterface()) return kIOReturnNotReady;
    if (transportPool || transportTx || transportRx || transportLoop ||
        mExpansionData->fRegistrationInfo || mExpansionData2->fRegistrationInfo ||
        ItlAwdlNativePacketPool::metaClass->getInstanceCount() ||
        ItlAwdlNativePacket::metaClass->getInstanceCount() ||
        ItlAwdlNativeTxQueue::metaClass->getInstanceCount() ||
        ItlAwdlNativeRxQueue::metaClass->getInstanceCount()) return kIOReturnBusy;
    auto result = deferBSDAttach(true);
    if (result) return result;
    ++transportPrepares;
    transportLastStage = 0;
    transportLoop = loop;
    loop->retain();
    do {
        result = kIOReturnNoMemory;
        transportPool = ItlAwdlNativePacketPool::withOwner(this);
        if (!transportPool) break;
        transportLastStage = 1;
        if (stopAfter == 1) { result = kIOReturnSuccess; break; }
        transportTx = ItlAwdlNativeTxQueue::withPool(transportPool, this, transportDequeue, nullptr);
        if (!transportTx) break;
        transportLastStage = 2;
        if (stopAfter == 2) { result = kIOReturnSuccess; break; }
        transportRx = ItlAwdlNativeRxQueue::withPool(transportPool, this, transportEnqueue, nullptr);
        if (!transportRx) break;
        transportLastStage = 3;
        if (stopAfter == 3) { result = kIOReturnSuccess; break; }
        result = loop->addEventSource(transportTx);
        if (result) break;
        transportTxAdded = true;
        transportLastStage = 4;
        if (stopAfter == 4) break;
        result = loop->addEventSource(transportRx);
        if (result) break;
        transportRxAdded = true;
        transportLastStage = 5;
        if (stopAfter == 5) break;
        IOSkywalkEthernetInterface::RegistrationInfo info = {};
        if (!initRegistrationInfo(&info, 1, sizeof(info))) { result = kIOReturnError; break; }
        // Exact 24H32 registration offsets. Keep generic native defaults for
        // unaudited flags; this unpublished registration is not a BSD contract.
        const uint32_t subfamily = 3, unit = 0, mtu = 1428;
        const char *prefix = getBSDNamePrefix();
        memcpy(info.bytes + 0xc, &subfamily, sizeof(subfamily));
        memcpy(info.bytes + 0x30, &prefix, sizeof(prefix));
        memcpy(info.bytes + 0x38, &unit, sizeof(unit));
        memcpy(info.bytes + 0x48, &mtu, sizeof(mtu));
        memcpy(info.bytes + 0x4c, &mtu, sizeof(mtu));
        memcpy(info.bytes + 0x108, &selfAddress, sizeof(selfAddress));
        IOSkywalkPacketQueue *queues[] = {transportTx, transportRx};
        transportRegistrationAttempted = true; // Includes partial native failure.
        result = registerEthernetInterface(&info, queues, 2, transportPool, transportPool, 0);
        if (result) break;
        transportRegistered = true;
        ++transportRegisters;
        transportLastStage = 6;
        if (!mExpansionData->fRegistrationInfo || !mExpansionData2->fRegistrationInfo ||
            memcmp(info.bytes, mExpansionData->fRegistrationInfo->bytes, 0x108) ||
            memcmp(info.bytes, mExpansionData2->fRegistrationInfo->bytes, sizeof(info)) ||
            transportTx->getInterface() != this || transportRx->getInterface() != this ||
            transportTx->isEnabled() || transportRx->isEnabled() || getBSDInterface() ||
            getBSDUnitNumber() != 0 || strcmp(getBSDNamePrefix(), "awdl")) {
            result = kIOReturnError; break;
        }
        if (!stopAfter) return kIOReturnSuccess;
    } while (false);
    const auto cleaned = clearTransport();
    return result ? result : cleaned;
}

void ItlAwdlNativeInterface::describeTransport(ItlAwdlNativeTransportResponse *out) {
    out->registered = transportRegistered;
    out->last_stage = transportLastStage;
    out->prepare_calls = transportPrepares;
    out->registrations = transportRegisters;
    out->deregistrations = transportDeregisters;
    out->callback_calls = transportCallbacks;
    out->cleanup_errors = transportCleanupErrors;
    out->tx_interface = transportTx && transportTx->getInterface() == this;
    out->rx_interface = transportRx && transportRx->getInterface() == this;
    out->tx_enabled = transportTx && transportTx->isEnabled();
    out->rx_enabled = transportRx && transportRx->isEnabled();
    out->bsd_interfaces = getBSDInterface() != nullptr;
    out->registration_copies = (mExpansionData->fRegistrationInfo != nullptr) +
        (mExpansionData2->fRegistrationInfo != nullptr);
    out->registry_id = getRegistryEntryID();
}

IOReturn ItlAwdlNativeInterface::managedTransport(AirportItlwm *parent, ItlIwx *radio,
    const ItlAwdlNativeTransportRequest *request, ItlAwdlNativeTransportResponse *out) {
    if (!parent || !radio || !request || !out || request->version != ITL_AWDL_ABI_VERSION ||
        request->operation > ItlAwdlTransportClear || request->stop_after > 6 || request->reserved ||
        (request->operation != ItlAwdlTransportPrepare && request->stop_after)) return kIOReturnBadArgument;
    if (strcmp(osversion, "24H32")) return kIOReturnUnsupported;
    auto gate = radio->getMainCommandGate();
    auto resources = IOService::getResourceService();
    if (!gate || !gate->getWorkLoop() || !resources || !resources->getProperty("IOBSD"))
        return kIOReturnNotReady;
    NativeProbeLease lease;
    if (!lease.owned) return kIOReturnBusy;
    bzero(out, sizeof(*out));
    out->version = ITL_AWDL_ABI_VERSION;
    struct Call { AirportItlwm *parent; ItlIwx *radio; IOWorkLoop *loop;
        const ItlAwdlNativeTransportRequest *request; ItlAwdlNativeTransportResponse *out; } call = {
            parent, radio, gate->getWorkLoop(), request, out};
    auto result = gate->runAction([](OSObject *, void *argument, void *, void *, void *) -> IOReturn {
        auto c = static_cast<Call *>(argument);
        if (c->parent->isInactive()) return kIOReturnNotReady;
        ItlAwdlStatusResponse status = {};
        auto result = c->radio->awdlCommand(c->parent, ItlAwdlStatus, nullptr, &status);
        if (result) return result;
        if (status.device_id != 0x7af0) return kIOReturnUnsupported;
        if (status.session != ItlAwdlIdle) return kIOReturnBusy;
        auto object = c->parent->nativeAwdlInterface;
        if (!object) return c->request->operation == ItlAwdlTransportInspect ?
            kIOReturnSuccess : kIOReturnNotReady;
        if (c->parent->getAwdlInterface() != object) return kIOReturnError;
        if (c->request->operation == ItlAwdlTransportPrepare)
            result = object->prepareTransport(c->loop, c->request->stop_after);
        else if (c->request->operation == ItlAwdlTransportClear)
            result = object->clearTransport();
        object->describeTransport(c->out);
        c->out->last_error = static_cast<uint32_t>(result);
        return kIOReturnSuccess;
    }, &call);
    if (result) return result;
    // Only wait after a mutation that released resources. Do not hold the gate
    // needed by the native deferred reference cleanup during this bounded wait.
    if (!out->registered && request->operation != ItlAwdlTransportInspect) {
        unsigned waited = 0;
        while ((ItlAwdlNativePacketPool::metaClass->getInstanceCount() ||
            ItlAwdlNativePacket::metaClass->getInstanceCount() ||
            ItlAwdlNativeTxQueue::metaClass->getInstanceCount() ||
            ItlAwdlNativeRxQueue::metaClass->getInstanceCount()) && waited++ < 250) IOSleep(1);
    }
    out->interface_count = metaClass->getInstanceCount();
    auto peerClass = nativePeerClass();
    out->peer_count = peerClass ? peerClass->getInstanceCount() : 0;
    out->pool_count = ItlAwdlNativePacketPool::metaClass->getInstanceCount();
    out->packet_count = ItlAwdlNativePacket::metaClass->getInstanceCount();
    out->tx_count = ItlAwdlNativeTxQueue::metaClass->getInstanceCount();
    out->rx_count = ItlAwdlNativeRxQueue::metaClass->getInstanceCount();
    if (!out->registered && request->operation != ItlAwdlTransportInspect && !out->last_error &&
        (out->pool_count || out->packet_count || out->tx_count || out->rx_count)) out->last_error = kIOReturnTimeout;
    return kIOReturnSuccess;
}

IOReturn ItlAwdlNativeInterface::probeQueues(AirportItlwm *parent, ItlIwx *radio,
    const ItlAwdlNativeQueueRequest *request, ItlAwdlNativeQueueResponse *out) {
    if (!parent || !radio || !request || !out || request->version != ITL_AWDL_ABI_VERSION ||
        request->iterations < 1 || request->iterations > 2 || request->stop_after > 7 || request->reserved)
        return kIOReturnBadArgument;
    bzero(out, sizeof(*out));
    out->version = ITL_AWDL_ABI_VERSION;
    if (strcmp(osversion, "24H32")) return kIOReturnUnsupported;
    auto gate = radio->getMainCommandGate();
    auto resources = IOService::getResourceService();
    if (!gate || !gate->getWorkLoop() || !resources || !resources->getProperty("IOBSD"))
        return kIOReturnNotReady;
    NativeProbeLease lease;
    if (!lease.owned) return kIOReturnBusy;
    out->pools_before = ItlAwdlNativePacketPool::metaClass->getInstanceCount();
    out->packets_before = ItlAwdlNativePacket::metaClass->getInstanceCount();
    out->tx_before = ItlAwdlNativeTxQueue::metaClass->getInstanceCount();
    out->rx_before = ItlAwdlNativeRxQueue::metaClass->getInstanceCount();
    if (out->pools_before || out->packets_before || out->tx_before || out->rx_before) return kIOReturnBusy;
    struct Call { AirportItlwm *parent; ItlIwx *radio; IOWorkLoop *loop; unsigned int stopAfter;
        ItlAwdlNativeQueueResponse *out; } call = {parent, radio, gate->getWorkLoop(), request->stop_after, out};
    for (uint32_t i = 0; i < request->iterations; ++i) {
        auto result = gate->runAction([](OSObject *, void *argument, void *, void *, void *) -> IOReturn {
            auto call = static_cast<Call *>(argument);
            if (call->parent->isInactive()) return kIOReturnNotReady;
            ItlAwdlStatusResponse status = {};
            auto result = call->radio->awdlCommand(call->parent, ItlAwdlStatus, nullptr, &status);
            if (result) return result;
            if (status.device_id != 0x7af0) return kIOReturnUnsupported;
            if (status.session != ItlAwdlIdle || call->parent->getAwdlInterface()) return kIOReturnBusy;
            return ItlAwdlProbeNativeQueues(call->parent, call->loop, call->stopAfter, call->out);
        }, &call);
        unsigned int waited = 0;
        do {
            out->pools_after = ItlAwdlNativePacketPool::metaClass->getInstanceCount();
            out->packets_after = ItlAwdlNativePacket::metaClass->getInstanceCount();
            out->tx_after = ItlAwdlNativeTxQueue::metaClass->getInstanceCount();
            out->rx_after = ItlAwdlNativeRxQueue::metaClass->getInstanceCount();
            if (!(out->pools_after || out->packets_after || out->tx_after || out->rx_after)) break;
            if (waited++ >= 250) break;
            IOSleep(1); // Let native deferred cleanup run outside the radio gate.
        } while (true);
        out->last_error = static_cast<uint32_t>(result ? result :
            (out->pools_after || out->packets_after || out->tx_after || out->rx_after ?
                kIOReturnTimeout : kIOReturnSuccess));
        if (out->last_error) break;
    }
    return kIOReturnSuccess;
}

IOReturn ItlAwdlNativeInterface::probePackets(AirportItlwm *parent, ItlIwx *radio,
    const ItlAwdlNativePacketRequest *request, ItlAwdlNativePacketResponse *out) {
    if (!parent || !radio || !request || !out || request->version != ITL_AWDL_ABI_VERSION ||
        request->iterations < 1 || request->iterations > 4 || request->failure > 3 || request->reserved)
        return kIOReturnBadArgument;
    bzero(out, sizeof(*out));
    out->version = ITL_AWDL_ABI_VERSION;
    if (strcmp(osversion, "24H32")) return kIOReturnUnsupported;
    auto gate = radio->getMainCommandGate();
    if (!gate) return kIOReturnNotReady;
    // Pool init waits for IOBSD internally; never enter an unbounded resource
    // wait from this diagnostic while holding the wireless work queue gate.
    auto resources = IOService::getResourceService();
    if (!resources || !resources->getProperty("IOBSD")) return kIOReturnNotReady;
    NativeProbeLease lease;
    if (!lease.owned) return kIOReturnBusy;
    out->pools_before = ItlAwdlNativePacketPool::metaClass->getInstanceCount();
    out->packets_before = ItlAwdlNativePacket::metaClass->getInstanceCount();
    if (out->pools_before || out->packets_before) return kIOReturnBusy;
    struct Call { AirportItlwm *parent; ItlIwx *radio; unsigned int failure;
        ItlAwdlNativePacketResponse *out; } call = {parent, radio, request->failure, out};
    for (uint32_t i = 0; i < request->iterations; ++i) {
        auto result = gate->runAction([](OSObject *, void *argument, void *, void *, void *) -> IOReturn {
            auto call = static_cast<Call *>(argument);
            if (call->parent->isInactive()) return kIOReturnNotReady;
            ItlAwdlStatusResponse status = {};
            auto result = call->radio->awdlCommand(call->parent, ItlAwdlStatus, nullptr, &status);
            if (result) return result;
            if (status.device_id != 0x7af0) return kIOReturnUnsupported;
            if (status.session != ItlAwdlIdle || call->parent->getAwdlInterface()) return kIOReturnBusy;
            return ItlAwdlNativePacketPool::probeCycle(call->parent, call->failure, call->out);
        }, &call);
        unsigned int waited = 0;
        while ((ItlAwdlNativePacketPool::metaClass->getInstanceCount() ||
                ItlAwdlNativePacket::metaClass->getInstanceCount()) && waited < 250) {
            IOSleep(1);
            ++waited;
        }
        out->pools_after = ItlAwdlNativePacketPool::metaClass->getInstanceCount();
        out->packets_after = ItlAwdlNativePacket::metaClass->getInstanceCount();
        out->last_error = static_cast<uint32_t>(result ? result :
            (out->pools_after || out->packets_after ? kIOReturnTimeout : kIOReturnSuccess));
        if (out->last_error) break;
    }
    return kIOReturnSuccess;
}

bool ItlAwdlNativeInterface::initializeForProbe(IO80211Controller *parent,
    ether_addr *address, uint32_t stopAfter, ItlAwdlNativeInitResponse *out) {
    syncTemplate = {};
    networkConfiguration = {};
    electionId = 0;
    electionIdValid = false;
    electionMetric = 0;
    electionMetricValid = false;
    minimumRateMbps = 0;
    minimumRateValid = false;
    masterChannel = 0;
    masterChannelValid = false;
    bzero(electionRssiThresholds, sizeof(electionRssiThresholds));
    electionRssiThresholdsValid = false;
    stagedSyncRole = {};
    stagedSyncRoleValid = false;
    presenceMode = 0;
    presenceModeValid = false;
    bzero(extensionCounts, sizeof(extensionCounts));
    extensionCountsValid = false;
    channelSequence = {};
    syncParameters = {};
    virtualInitAttempted = false;
    resourcesDisposed = false;
    bpfDisposed = startAllowed = startAttempted = failPeerCreation = false;
    backendCalls = peerCreationCalls = partialCleanups = 0;
    backendTrace = nullptr;
    bzero(&configurationResult, sizeof(configurationResult));
    bzero(&activationPreflightResult, sizeof(activationPreflightResult));
    runtimePhase = RuntimeIdle; runtimeRadio = nullptr; runtimeTimer = nullptr;
    runtimeResult = {}; runtimeTimeline = {};
    runtimeSyncEnabled = runtimeNativeEnabled = runtimeEnableInvoked = false;
    runtimeHadWindow = runtimeClockQualified = preflightRejectEnable = false;
    runtimeWindow = 0; runtimeGeneration = -1; runtimeNextTxUs = 0;
    bzero(&templateResult, sizeof(templateResult));
    bzero(&timingResult, sizeof(timingResult));
    transportPool = nullptr;
    transportTx = nullptr;
    transportRx = nullptr;
    transportLoop = nullptr;
    transportTxAdded = transportRxAdded = transportRegistrationAttempted = transportRegistered = false;
    bsdPublishRequested = false;
    bsdPublishCalls = bsdPrepareCalls = bsdPrepareError = bsdEnableCalls = bsdEnableDenied = 0;
    transportPrepares = transportRegisters = transportDeregisters = transportCallbacks = 0;
    transportCleanupErrors = transportLastStage = 0;
    selfAddress = *address;
    controller = parent;
    controller->retain();
    if (!IO80211SkywalkInterface::init()) return false;
    ++out->base_initialized;
    if (stopAfter == 1) return true;
    virtualInitAttempted = true;
    if (!IO80211VirtualInterface::init(controller, address, 6, "awdl")) return false;
    ++out->virtual_initialized;
    out->role = static_cast<uint32_t>(getInterfaceRole());
    const bool opened = controller->isOpen(this);
    if (opened) ++out->provider_opened;
    return out->role == 6 && opened;
}

void ItlAwdlNativeInterface::disposeInitializedResources() {
    if (resourcesDisposed) return;
    resourcesDisposed = true;
    syncTemplate.clear();
    networkConfiguration.clear();
    electionId = 0;
    electionIdValid = false;
    electionMetric = 0;
    electionMetricValid = false;
    minimumRateMbps = 0;
    minimumRateValid = false;
    removeProperty("AX201StagedElectionMetric");
    removeProperty("AX201StagedMinRateMbps");
    masterChannel = 0;
    masterChannelValid = false;
    removeProperty("AX201StagedMasterChannel");
    bzero(electionRssiThresholds, sizeof(electionRssiThresholds));
    electionRssiThresholdsValid = false;
    stagedSyncRole = {};
    stagedSyncRoleValid = false;
    removeProperty("AX201StagedSyncRole");
    presenceMode = 0;
    presenceModeValid = false;
    bzero(extensionCounts, sizeof(extensionCounts));
    extensionCountsValid = false;
    removeProperty("AX201StagedPresenceMode");
    removeProperty("AX201StagedExtensionCounts");
    channelSequence.clear();
    removeProperty("AX201StagedChannelSequence");
    removeProperty("AX201StagedChannelStep");
    // VirtualInterface::init registers power interest and opens the controller.
    // The native free() only frees its arrays/expansion. stop() is inappropriate
    // here: Skywalk::stop assumes start() has created a peer manager.
    if (controller && virtualInitAttempted) {
        if (controller->isOpen(this)) controller->close(this);
        controller->deRegisterInterestedDriver(this);
    }
    if (hasNativeVirtualStorage() && !bpfDisposed) {
        bpfDisposed = true;
        freeBpfResources();
    }
}

void ItlAwdlNativeInterface::free() {
    if (startAttempted) stop(controller);
    disposeInitializedResources();
    OSSafeReleaseNULL(controller);
    // Audited 24H32: Skywalk::free dereferences its expansion before testing
    // for null. The lower bases tolerate absent/partially initialized storage.
    if (hasNativeSkywalkStorage()) IO80211AWDLProtocol::free();
    else IOSkywalkEthernetInterface::free();
}

bool ItlAwdlNativeInterface::start(IOService *provider) {
    // Only the synchronous, exact-build probe may enter this stage. In
    // particular, registry matching must never start this unfinished backend.
    if (!startAllowed || startAttempted || resourcesDisposed || provider != controller)
        return false;
    startAttempted = true;
    return IO80211VirtualInterface::start(provider);
}

void *ItlAwdlNativeInterface::createPeerManager() {
    ++peerCreationCalls;
    if (!startAllowed || failPeerCreation) return nullptr;
    // Consume immediately before entering native creation, including its
    // partial failures. The caller holds the lifecycle lease and radio gate.
    if (!reserveNativePeerAttempt()) return nullptr;
    return IO80211VirtualInterface::createPeerManager();
}

void ItlAwdlNativeInterface::stop(IOService *provider) {
    if (!startAttempted) return;
    finishSynchronization();
    // Transport registration belongs to this same once-per-boot peer manager.
    // Clear its references before native stop destroys the interface resources.
    if (clearTransport(true)) IOLog("AX201 native transport: cleanup failed during stop\n");
    startAttempted = false;
    startAllowed = false;
    syncTemplate.clear();
    auto state = nativeStartState();
    if (state && state->peerManager && state->controller) {
        // Native VirtualInterface::stop includes the one BPF free and calls
        // Skywalk::stop, which tears down the peer manager and log owners.
        bpfDisposed = true;
        IO80211VirtualInterface::stop(provider);
    } else {
        ++partialCleanups;
        // Skywalk::stop unconditionally dereferences peerManager. Before it
        // exists, stop only the independently audited log/power resources and
        // let native free() release the retained controller/work queue later.
        PMstop();
        if (state) {
            if (state->logPipe) state->logPipe->stopPipe();
            OSSafeReleaseNULL(state->logPipe);
            OSSafeReleaseNULL(state->logStream);
            OSSafeReleaseNULL(state->macAgent);
        }
        IOSkywalkNetworkInterface::stop(provider);
    }
    if (controller && controller->getAwdlInterface() == this)
        controller->setAwdlInterface(nullptr);
}

IOReturn ItlAwdlNativeInterface::probeStartGated(AirportItlwm *parent, ItlIwx *radio,
    const ItlAwdlNativeStartRequest *request, ItlAwdlNativeStartResponse *out) {
    bzero(out, sizeof(*out));
    out->version = ITL_AWDL_ABI_VERSION;
    if (strcmp(osversion, "24H32")) return kIOReturnUnsupported;
    if (parent->isInactive()) return kIOReturnNotReady;
    ItlAwdlStatusResponse status = {};
    IOReturn result = radio->awdlCommand(parent, ItlAwdlStatus, nullptr, &status);
    if (result) return result;
    if (status.session != ItlAwdlIdle || parent->getAwdlInterface()) return kIOReturnBusy;
    if (status.device_id != 0x7af0) return kIOReturnUnsupported;
    if (!request->fail_peer_creation && nativePeerAttempted())
        return kIOReturnNotPermitted;
    out->live_before = metaClass->getInstanceCount();
    if (out->live_before) return kIOReturnBusy;
    auto peerClass = nativePeerClass();
    if (!peerClass) return kIOReturnUnsupported;
    out->peer_live_before = peerClass->getInstanceCount();
    if (out->peer_live_before) return kIOReturnBusy;
    ether_addr address;
    memcpy(address.ether_addr_octet, status.local_mac, sizeof(address.ether_addr_octet));
    address.ether_addr_octet[0] = (address.ether_addr_octet[0] | 2) & ~1;
    address.ether_addr_octet[5] ^= 0x80;
    for (uint32_t i = 0; i < request->iterations; ++i) {
        auto object = new ItlAwdlNativeInterface;
        if (!object) { result = kIOReturnNoMemory; break; }
        ++out->attempted;
        ItlAwdlNativeInitResponse initialized = {};
        bool ready = object->initializeForProbe(parent, &address, 2, &initialized);
        bool attached = false;
        if (ready) {
            ++out->initialized;
            attached = object->attach(parent);
            if (attached) {
                ++out->attached;
                object->failPeerCreation = request->fail_peer_creation;
                object->startAllowed = true;
                IOLog("AX201 native probe: start, fail-peer=%u\n", request->fail_peer_creation);
                bool started = object->start(parent);
                auto state = object->nativeStartState();
                bool created = state && state->peerManager;
                if (started) ++out->start_succeeded;
                if (!started && request->fail_peer_creation && object->peerCreationCalls == 1 && !created)
                    ++out->expected_start_failures;
                else if (!started || request->fail_peer_creation) result = kIOReturnError;
                if (created) {
                    ++out->peer_managers_created;
                    if (state->peerManager->getMetaClass() != peerClass ||
                        parent->getAwdlInterface() != object || peerClass->getInstanceCount() != 1)
                        result = kIOReturnError;
                }
                if (started && !created) result = kIOReturnError;
                if (object->getBSDInterface()) { ++out->bsd_interfaces; result = kIOReturnError; }
                IOLog("AX201 native probe: stop, started=%u peer=%u\n", started, created);
                object->stop(parent);
                ++out->stopped;
                out->partial_cleanups += object->partialCleanups;
                if (created && !state->peerManager && !peerClass->getInstanceCount())
                    ++out->peer_managers_released;
                if (state && (state->peerManager || state->logPipe || state->logStream || state->macAgent))
                    ++out->cleanup_errors;
                out->peer_creation_calls += object->peerCreationCalls;
            }
        }
        if (!ready || !attached) result = kIOReturnError;
        object->disposeInitializedResources();
        if (parent->isOpen(object)) ++out->provider_open_after;
        if (attached) object->detach(parent);
        out->backend_calls += object->backendCalls;
        object->release();
        ++out->released;
        // Instance counts are settled outside this gate. In 24H32 the last
        // service/log reference is normally released after it opens again.
        if (parent->getAwdlInterface() || out->provider_open_after || out->cleanup_errors)
            result = kIOReturnError;
        if (result) break;
    }
    out->live_after = metaClass->getInstanceCount();
    out->peer_live_after = peerClass->getInstanceCount();
    out->controller_pointer_after = parent->getAwdlInterface() != nullptr;
    out->last_error = static_cast<uint32_t>(result);
    return kIOReturnSuccess;
}

IOReturn ItlAwdlNativeInterface::probeStart(AirportItlwm *parent, ItlIwx *radio,
    const ItlAwdlNativeStartRequest *request, ItlAwdlNativeStartResponse *out) {
    if (!parent || !radio || !out || !request || request->version != ITL_AWDL_ABI_VERSION ||
        request->reserved || !request->iterations || request->iterations > 4 ||
        request->fail_peer_creation > 1 ||
        (!request->fail_peer_creation && request->iterations != 1)) return kIOReturnBadArgument;
    auto gate = radio->getMainCommandGate();
    if (!gate) return kIOReturnNotReady;
    NativeProbeLease lease;
    if (!lease.owned) return kIOReturnBusy;
    auto peerClass = nativePeerClass();
    if (!peerClass) return kIOReturnUnsupported;
    bzero(out, sizeof(*out));
    out->version = ITL_AWDL_ABI_VERSION;
    ItlAwdlNativeStartRequest single = *request;
    single.iterations = 1;
    ItlAwdlNativeStartResponse step = {};
    struct Call { AirportItlwm *parent; ItlIwx *radio;
        const ItlAwdlNativeStartRequest *request; ItlAwdlNativeStartResponse *out; } call = {
            parent, radio, &single, &step};
    for (uint32_t i = 0; i < request->iterations; ++i) {
        IOReturn result = gate->runAction([](OSObject *, void *argument, void *, void *, void *) -> IOReturn {
            auto call = static_cast<Call *>(argument);
            return probeStartGated(call->parent, call->radio, call->request, call->out);
        }, &call);
        if (result) return result;
        // No gate is held during this bounded wait, so native deferred cleanup
        // and ordinary Wi-Fi callbacks can run. Never create another object
        // unless the preceding object's and peer manager's counts are zero.
        unsigned waited = 0;
        while ((metaClass->getInstanceCount() || peerClass->getInstanceCount()) && waited < 250) {
            IOSleep(1);
            ++waited;
        }
        step.live_after = metaClass->getInstanceCount();
        step.peer_live_after = peerClass->getInstanceCount();
        if (!step.peer_live_after) step.peer_managers_released = step.peer_managers_created;
        if (step.live_after || step.peer_live_after) step.last_error = kIOReturnTimeout;
        IOLog("AX201 native probe: cleanup wait=%u ms live=%u peer=%u\n",
              waited, step.live_after, step.peer_live_after);
#define SUM(field) out->field += step.field
        SUM(attempted); SUM(initialized); SUM(attached); SUM(start_succeeded);
        SUM(expected_start_failures); SUM(peer_managers_created); SUM(peer_managers_released);
        SUM(stopped); SUM(partial_cleanups); SUM(released); SUM(provider_open_after);
        SUM(backend_calls); SUM(bsd_interfaces); SUM(cleanup_errors); SUM(peer_creation_calls);
#undef SUM
        if (!i) { out->live_before = step.live_before; out->peer_live_before = step.peer_live_before; }
        out->live_after = step.live_after;
        out->peer_live_after = step.peer_live_after;
        out->controller_pointer_after = step.controller_pointer_after;
        out->last_error = step.last_error;
        if (out->last_error) break;
    }
    return kIOReturnSuccess;
}

void ItlAwdlNativeInterface::checkBackendContract(ItlAwdlNativeInterface *concrete,
    ItlAwdlNativeInitResponse *out) {
    const uint32_t callsBefore = concrete->backendCalls;
    concrete->backendTrace = &concrete->configurationResult;
    IO80211AWDLProtocol *object = concrete;
    // Exercise virtual dispatch through the native base type. Unknown request
    // extents stay opaque: these methods must not dereference even one byte.
    alignas(16) uint8_t sentinel = 0x5a;
#define ITL_NATIVE_REQUEST(name, type) \
    do { \
        ++out->methods_checked; \
        if (object->name(nullptr) != kIOReturnBadArgument) ++out->method_failures; \
        ++out->methods_checked; \
        if (object->name(reinterpret_cast<type *>(&sentinel)) != kIOReturnUnsupported || \
            sentinel != 0x5a) ++out->method_failures; \
    } while (false);
#define ITL_NATIVE_IMPLEMENTED_REQUEST(name, type)
#include "ItlAwdlNativeRequests.def"
#undef ITL_NATIVE_REQUEST
#undef ITL_NATIVE_IMPLEMENTED_REQUEST
    // Audited methods receive real, fully sized structures, never the opaque
    // one-byte sentinel used for methods that must not inspect their payload.
    ++out->methods_checked;
    if (object->getAWDL_SYNC_ENABLED(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    apple80211_awdl_sync_enabled enabled = {0xaaaaaaaa, 0xaaaaaaaa, 0xaaaaaaaa};
    ++out->methods_checked;
    if (object->getAWDL_SYNC_ENABLED(&enabled) || enabled.version != 1 || enabled.unk1 || enabled.enabled)
        ++out->method_failures;
    ++out->methods_checked;
    if (object->setAWDL_SYNC_ENABLED(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    ++out->methods_checked;
    enabled = {1, 0, 1};
    bool enableFailed = object->setAWDL_SYNC_ENABLED(&enabled) != kIOReturnNotReady;
    enabled.version = 2;
    enableFailed |= object->setAWDL_SYNC_ENABLED(&enabled) != kIOReturnBadArgument;
    enabled = {1, 1, 1};
    enableFailed |= object->setAWDL_SYNC_ENABLED(&enabled) != kIOReturnBadArgument;
    enabled = {1, 0, 2};
    enableFailed |= object->setAWDL_SYNC_ENABLED(&enabled) != kIOReturnBadArgument;
    concrete->preflightRejectEnable = true;
    enabled = {0, 0, 1};
    enableFailed |= object->setAWDL_SYNC_ENABLED(&enabled) != kIOReturnUnsupported;
    concrete->preflightRejectEnable = false;
    if (enableFailed) ++out->method_failures;
    ++out->methods_checked;
    if (object->getAWDL_SYNC_PARAMS(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    apple80211_awdl_sync_params timing;
    memset(&timing, 0xa5, sizeof(timing));
    const auto untouchedTiming = timing;
    ++out->methods_checked;
    if (object->getAWDL_SYNC_PARAMS(&timing) != kIOReturnUnsupported ||
        memcmp(&timing, &untouchedTiming, sizeof(timing))) ++out->method_failures;
    ++out->methods_checked;
    if (object->setAWDL_SYNC_PARAMS(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    ++out->methods_checked;
    const apple80211_awdl_sync_params savedTiming = {1, 16, 16, 16, 110, 0};
    timing = savedTiming; timing.version = 0;
    bool timingFailed = object->setAWDL_SYNC_PARAMS(&timing) != kIOReturnSuccess || timing.version != 0;
    timing = untouchedTiming;
    timingFailed |= object->getAWDL_SYNC_PARAMS(&timing) != kIOReturnSuccess ||
        memcmp(&timing, &savedTiming, sizeof(timing));
    timing = savedTiming; timing.version = 2;
    timingFailed |= object->setAWDL_SYNC_PARAMS(&timing) != kIOReturnBadArgument;
    timing = savedTiming; timing.availability_window_period = 0;
    timingFailed |= object->setAWDL_SYNC_PARAMS(&timing) != kIOReturnBadArgument;
    timing = savedTiming; timing.availability_window_length = 32;
    timingFailed |= object->setAWDL_SYNC_PARAMS(&timing) != kIOReturnUnsupported;
    timing = savedTiming; timing.extension_length = 0x10000;
    timingFailed |= object->setAWDL_SYNC_PARAMS(&timing) != kIOReturnBadArgument;
    timing = savedTiming; timing.synchronization_frame_period = 111;
    timingFailed |= object->setAWDL_SYNC_PARAMS(&timing) != kIOReturnUnsupported;
    timing = savedTiming; timing.unknown_14 = 1;
    timingFailed |= object->setAWDL_SYNC_PARAMS(&timing) != kIOReturnUnsupported;
    timingFailed |= object->getAWDL_SYNC_PARAMS(&timing) != kIOReturnSuccess ||
        memcmp(&timing, &savedTiming, sizeof(timing));
    if (timingFailed) ++out->method_failures;
    ++out->methods_checked;
    if (object->setAWDL_BSSID(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    apple80211_awdl_bssid bssid = {0, {0, 0x25, 0, 0xff, 0x94, 0x73},
                                     {0, 0x25, 0, 0xff, 0x94, 0x74}};
    ++out->methods_checked;
    bool bssidFailed = object->setAWDL_BSSID(&bssid) || !concrete->networkConfiguration.ready();
    bssid.bssid[0] = 1;
    bssidFailed |= !ItlAwdl::equalMac(concrete->networkConfiguration.awdl(), ItlAwdl::DefaultAwdlBssid) ||
        object->setAWDL_BSSID(&bssid) != kIOReturnBadArgument;
    bssid.bssid[0] = 2;
    bssidFailed |= object->setAWDL_BSSID(&bssid) != kIOReturnUnsupported;
    bssid.bssid[0] = 0;
    bssid.version = 2;
    bssidFailed |= object->setAWDL_BSSID(&bssid) != kIOReturnBadArgument;
    bssid.version = 1;
    bssidFailed |= object->setAWDL_BSSID(&bssid) != kIOReturnSuccess ||
        !ItlAwdl::equalMac(concrete->networkConfiguration.awdl(), ItlAwdl::DefaultAwdlBssid) ||
        !ItlAwdl::equalMac(concrete->networkConfiguration.companion(), ItlAwdl::DefaultCompanionBssid);
    if (bssidFailed) ++out->method_failures;
    ++out->methods_checked;
    if (object->getAWDL_ELECTION_ID(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    apple80211_awdl_election_id id = {0xaaaaaaaa, 0xaaaaaaaa};
    ++out->methods_checked;
    if (object->getAWDL_ELECTION_ID(&id) != kIOReturnNotReady ||
        id.version != 0xaaaaaaaa || id.election_id != 0xaaaaaaaa) ++out->method_failures;
    ++out->methods_checked;
    if (object->setAWDL_ELECTION_ID(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    ++out->methods_checked;
    bool idFailed = false;
    const uint32_t electionIds[] = {0U, 0xffffU, 0x12345678U, 0xffffffffU};
    for (uint32_t value : electionIds) {
        apple80211_awdl_election_id input = {0, value};
        idFailed |= object->setAWDL_ELECTION_ID(&input) != kIOReturnSuccess ||
            input.version != 0 || input.election_id != value;
        id = {0xaaaaaaaa, 0xaaaaaaaa};
        idFailed |= object->getAWDL_ELECTION_ID(&id) != kIOReturnSuccess ||
            id.version != 1 || id.election_id != static_cast<uint16_t>(value);
    }
    id = {2, 0};
    idFailed |= object->setAWDL_ELECTION_ID(&id) != kIOReturnBadArgument;
    idFailed |= object->getAWDL_ELECTION_ID(&id) != kIOReturnSuccess || id.election_id != 0xffff;
    id = {1, 0x1234};
    idFailed |= object->setAWDL_ELECTION_ID(&id) != kIOReturnSuccess;
    idFailed |= object->getAWDL_ELECTION_ID(&id) != kIOReturnSuccess || id.election_id != 0x1234;
    if (idFailed) ++out->method_failures;
    ++out->methods_checked;
    if (object->getAWDL_ELECTION_METRIC(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    apple80211_awdl_election_metric metric;
    memset(&metric, 0xa5, sizeof(metric));
    auto untouchedMetric = metric;
    ++out->methods_checked;
    if (object->getAWDL_ELECTION_METRIC(&metric) != kIOReturnNotReady ||
        memcmp(&metric, &untouchedMetric, sizeof(metric))) ++out->method_failures;
    ++out->methods_checked;
    if (object->setAWDL_ELECTION_METRIC(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    ++out->methods_checked;
    bool metricFailed = false;
    const uint32_t metrics[] = {0U, UINT32_MAX, 0x12345678U, 65U};
    for (uint32_t value : metrics) {
        metric = {};
        metric.metric = value;
        metric.override_metric = 1;
        metricFailed |= object->setAWDL_ELECTION_METRIC(&metric) != kIOReturnSuccess;
        metric.metric ^= 1; // Producer mutation cannot change the saved word.
        metricFailed |= concrete->electionMetric != value;
        metricFailed |= object->getAWDL_ELECTION_METRIC(&metric) != kIOReturnSuccess ||
            metric.version != 1 || metric.metric != value || metric.override_metric ||
            metric.reserved_09[0] || metric.reserved_09[1] || metric.reserved_09[2];
    }
    metric.version = 2;
    metricFailed |= object->setAWDL_ELECTION_METRIC(&metric) != kIOReturnBadArgument;
    metricFailed |= object->getAWDL_ELECTION_METRIC(&metric) != kIOReturnSuccess || metric.metric != 65;
    if (metricFailed) ++out->method_failures;
    ++out->methods_checked;
    if (object->getAWDL_MIN_RATE(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    apple80211_awdl_min_rate minimumRate;
    memset(&minimumRate, 0xa5, sizeof(minimumRate));
    auto untouchedRate = minimumRate;
    ++out->methods_checked;
    if (object->getAWDL_MIN_RATE(&minimumRate) != kIOReturnNotReady ||
        memcmp(&minimumRate, &untouchedRate, sizeof(minimumRate))) ++out->method_failures;
    ++out->methods_checked;
    if (object->setAWDL_MIN_RATE(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    ++out->methods_checked;
    bool rateFailed = false;
    const uint8_t rates[] = {6, 9, 18, 24, 36, 48, 54, 12};
    for (uint8_t value : rates) {
        minimumRate = {};
        minimumRate.min_rate = value;
        rateFailed |= object->setAWDL_MIN_RATE(&minimumRate) != kIOReturnSuccess;
        memset(&minimumRate, 0xa5, sizeof(minimumRate));
        rateFailed |= object->getAWDL_MIN_RATE(&minimumRate) != kIOReturnSuccess ||
            minimumRate.version != 1 || minimumRate.min_rate != value ||
            minimumRate.reserved_05[0] || minimumRate.reserved_05[1] || minimumRate.reserved_05[2];
    }
    const uint8_t unsupportedRates[] = {0, 1, 11, 17, 255};
    for (uint8_t value : unsupportedRates) {
        minimumRate.min_rate = value;
        rateFailed |= object->setAWDL_MIN_RATE(&minimumRate) != kIOReturnUnsupported;
    }
    minimumRate.version = 2;
    minimumRate.min_rate = 12;
    rateFailed |= object->setAWDL_MIN_RATE(&minimumRate) != kIOReturnBadArgument;
    rateFailed |= object->getAWDL_MIN_RATE(&minimumRate) != kIOReturnSuccess || minimumRate.min_rate != 12;
    if (rateFailed) ++out->method_failures;
    ++out->methods_checked;
    if (object->getAWDL_MASTER_CHANNEL(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    apple80211_awdl_master_channel master = {0xaaaaaaaa, 0xaaaaaaaa};
    ++out->methods_checked;
    if (object->getAWDL_MASTER_CHANNEL(&master) != kIOReturnNotReady ||
        master.version != 0xaaaaaaaa || master.master_channel != 0xaaaaaaaa) ++out->method_failures;
    ++out->methods_checked;
    if (object->setAWDL_MASTER_CHANNEL(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    ++out->methods_checked;
    master = {0, 0};
    bool masterFailed = object->setAWDL_MASTER_CHANNEL(&master) != kIOReturnSuccess ||
        concrete->masterChannelValid || object->getAWDL_MASTER_CHANNEL(&master) != kIOReturnNotReady;
    master = {0, 6}; // Present on this AX201's normal 2.4 GHz channel map.
    masterFailed |= object->setAWDL_MASTER_CHANNEL(&master) != kIOReturnSuccess;
    master = {0xaaaaaaaa, 0xaaaaaaaa};
    masterFailed |= object->getAWDL_MASTER_CHANNEL(&master) != kIOReturnSuccess ||
        master.version != 1 || master.master_channel != 6;
    master = {1, 0};
    masterFailed |= object->setAWDL_MASTER_CHANNEL(&master) != kIOReturnSuccess;
    master = {2, 6};
    masterFailed |= object->setAWDL_MASTER_CHANNEL(&master) != kIOReturnBadArgument;
    master = {0, 15};
    masterFailed |= object->setAWDL_MASTER_CHANNEL(&master) != kIOReturnBadArgument;
    master = {0, UINT32_MAX};
    masterFailed |= object->setAWDL_MASTER_CHANNEL(&master) != kIOReturnBadArgument;
    masterFailed |= object->getAWDL_MASTER_CHANNEL(&master) != kIOReturnSuccess ||
        master.version != 1 || master.master_channel != 6;
    if (masterFailed) ++out->method_failures;
    ++out->methods_checked;
    if (object->getAWDL_ELECTION_RSSI_THRESHOLDS(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    apple80211_awdl_election_rssi_thresholds thresholds;
    memset(&thresholds, 0xa5, sizeof(thresholds));
    auto untouchedThresholds = thresholds;
    ++out->methods_checked;
    if (object->getAWDL_ELECTION_RSSI_THRESHOLDS(&thresholds) != kIOReturnNotReady ||
        memcmp(&thresholds, &untouchedThresholds, sizeof(thresholds))) ++out->method_failures;
    ++out->methods_checked;
    if (object->setAWDL_ELECTION_RSSI_THRESHOLDS(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    ++out->methods_checked;
    thresholds = {0, 0x12345680, 0x7f, 0xffffffff, 0x100};
    bool rssiFailed = object->setAWDL_ELECTION_RSSI_THRESHOLDS(&thresholds) != kIOReturnSuccess;
    memset(&thresholds, 0xa5, sizeof(thresholds));
    rssiFailed |= object->getAWDL_ELECTION_RSSI_THRESHOLDS(&thresholds) != kIOReturnSuccess ||
        thresholds.version != 1 || thresholds.unk1 != 0xffffff80 ||
        thresholds.unk2 != 0x7f || thresholds.unk3 != 0xffffffff || thresholds.unk4 != 0;
    auto preservedThresholds = thresholds;
    thresholds = {2, 0, 0, 0, 0};
    rssiFailed |= object->setAWDL_ELECTION_RSSI_THRESHOLDS(&thresholds) != kIOReturnBadArgument;
    rssiFailed |= object->getAWDL_ELECTION_RSSI_THRESHOLDS(&thresholds) != kIOReturnSuccess ||
        memcmp(&thresholds, &preservedThresholds, sizeof(thresholds));
    thresholds = {1, 0, 0, 0, 0xffffffbf};
    rssiFailed |= object->setAWDL_ELECTION_RSSI_THRESHOLDS(&thresholds) != kIOReturnSuccess;
    rssiFailed |= object->getAWDL_ELECTION_RSSI_THRESHOLDS(&thresholds) != kIOReturnSuccess ||
        thresholds.unk1 || thresholds.unk2 || thresholds.unk3 || thresholds.unk4 != 0xffffffbf;
    if (rssiFailed) ++out->method_failures;
    ++out->methods_checked;
    if (object->getAWDL_PRESENCE_MODE(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    apple80211_awdl_presence_mode presence = {0xaaaaaaaa, 0xaaaaaaaa};
    ++out->methods_checked;
    if (object->getAWDL_PRESENCE_MODE(&presence) != kIOReturnNotReady ||
        presence.version != 0xaaaaaaaa || presence.mode != 0xaaaaaaaa) ++out->method_failures;
    ++out->methods_checked;
    if (object->setAWDL_PRESENCE_MODE(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    ++out->methods_checked;
    bool presenceFailed = false;
    const uint32_t presenceModes[] = {0U, 3U, 64U, 4U};
    for (uint32_t mode : presenceModes) {
        presence = {0, mode};
        presenceFailed |= object->setAWDL_PRESENCE_MODE(&presence) != kIOReturnSuccess;
        presence = {0xaaaaaaaa, 0xaaaaaaaa};
        presenceFailed |= object->getAWDL_PRESENCE_MODE(&presence) != kIOReturnSuccess ||
            presence.version != 1 || presence.mode != mode;
    }
    presence = {1, 65};
    presenceFailed |= object->setAWDL_PRESENCE_MODE(&presence) != kIOReturnBadArgument;
    presence = {2, 1};
    presenceFailed |= object->setAWDL_PRESENCE_MODE(&presence) != kIOReturnBadArgument;
    presenceFailed |= object->getAWDL_PRESENCE_MODE(&presence) != kIOReturnSuccess || presence.mode != 4;
    if (presenceFailed) ++out->method_failures;
    ++out->methods_checked;
    if (object->getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    apple80211_awdl_extension_state_machine_parameter extensions;
    memset(&extensions, 0xa5, sizeof(extensions));
    auto untouchedExtensions = extensions;
    ++out->methods_checked;
    if (object->getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(&extensions) != kIOReturnNotReady ||
        memcmp(&extensions, &untouchedExtensions, sizeof(extensions))) ++out->method_failures;
    ++out->methods_checked;
    if (object->setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    ++out->methods_checked;
    extensions = {0, 0x123456ff, 0x80, 0x17f, 0x100};
    bool extFailed = object->setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(&extensions) != kIOReturnSuccess;
    memset(&extensions, 0xa5, sizeof(extensions));
    extFailed |= object->getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(&extensions) != kIOReturnSuccess ||
        extensions.version != 1 || extensions.unk1 != 255 || extensions.unk2 != 128 ||
        extensions.unk3 != 127 || extensions.unk4;
    auto savedExtensions = extensions;
    extensions = {2, 0, 0, 0, 0};
    extFailed |= object->setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(&extensions) != kIOReturnBadArgument;
    extFailed |= object->getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(&extensions) != kIOReturnSuccess ||
        memcmp(&extensions, &savedExtensions, sizeof(extensions));
    extensions = {1, 0, 0, 0, 0};
    extFailed |= object->setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(&extensions) != kIOReturnSuccess;
    if (extFailed) ++out->method_failures;
    ++out->methods_checked;
    if (object->getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    apple80211_awdl_sync_channel_sequence channels;
    memset(&channels, 0xa5, sizeof(channels));
    auto untouchedChannels = channels;
    ++out->methods_checked;
    if (object->getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(&channels) != kIOReturnNotReady ||
        memcmp(&channels, &untouchedChannels, sizeof(channels))) ++out->method_failures;
    ++out->methods_checked;
    if (object->setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    ++out->methods_checked;
    channels = {};
    channels.length = 16;
    channels.step_count = 3;
    channels.fill_channel = 0xff;
    for (unsigned i = 0; i < channels.length; ++i) channels.channels[i] = {0, 6, 8};
    bool channelsFailed = object->setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(&channels) != kIOReturnSuccess;
    channels.channels[0].channel = 1; // The producer cannot change saved state.
    channelsFailed |= concrete->channelSequence.plan().channels[0] != 6;
    memset(&channels, 0xa5, sizeof(channels));
    channelsFailed |= object->getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(&channels) != kIOReturnSuccess ||
        channels.version != 1 || channels.channels[0].version != 1 || channels.channels[0].channel != 6 ||
        channels.length != 16 || channels.step_count != 3 || channels.fill_channel != 0xff;
    auto savedChannels = channels;
    channels.encoding = 1;
    channelsFailed |= object->setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(&channels) != kIOReturnUnsupported;
    channels = savedChannels;
    channels.channels[15].channel = 15; // No partial update on a bad final record.
    channelsFailed |= object->setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(&channels) != kIOReturnBadArgument;
    channels = savedChannels;
    channels.version = 2;
    channelsFailed |= object->setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(&channels) != kIOReturnBadArgument;
    channelsFailed |= object->getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(&channels) != kIOReturnSuccess ||
        memcmp(&channels, &savedChannels, sizeof(channels));
    if (channelsFailed) ++out->method_failures;
    ++out->methods_checked;
    if (object->setAWDL_SYNC_STATE(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    ++out->methods_checked;
    apple80211_awdl_sync_state role = {};
    role.state = role.operation_mode = 1;
    memcpy(role.master, concrete->selfAddress.ether_addr_octet, 6);
    bool roleFailed = object->setAWDL_SYNC_STATE(&role) != kIOReturnSuccess ||
        !concrete->stagedSyncRoleValid;
    auto savedRole = concrete->stagedSyncRole;
    role.master[0] |= 1;
    roleFailed |= !ItlAwdl::equalMac(savedRole.master, concrete->stagedSyncRole.master) ||
        object->setAWDL_SYNC_STATE(&role) != kIOReturnBadArgument;
    role = savedRole;
    role.state = 0;
    roleFailed |= object->setAWDL_SYNC_STATE(&role) != kIOReturnBadArgument;
    role = savedRole;
    role.version = 2;
    roleFailed |= object->setAWDL_SYNC_STATE(&role) != kIOReturnBadArgument;
    role = savedRole;
    role.operation_mode = 2;
    roleFailed |= object->setAWDL_SYNC_STATE(&role) != kIOReturnUnsupported;
    role = savedRole;
    role.additional_role_flags = 1;
    roleFailed |= object->setAWDL_SYNC_STATE(&role) != kIOReturnUnsupported ||
        memcmp(&savedRole, &concrete->stagedSyncRole, sizeof(savedRole));
    for (uint32_t state = 1; state <= 3; ++state) {
        role = savedRole;
        role.state = state;
        roleFailed |= object->setAWDL_SYNC_STATE(&role) != kIOReturnSuccess ||
            concrete->stagedSyncRole.state != state;
    }
    roleFailed |= object->setAWDL_SYNC_STATE(&savedRole) != kIOReturnSuccess;
    if (roleFailed) ++out->method_failures;
    ++out->methods_checked;
    if (object->setAWDL_SYNC_FRAME_TEMPLATE(nullptr) != kIOReturnBadArgument) ++out->method_failures;
    uint8_t payload[] = {21, 2, 0, 0x10, 1};
    apple80211_awdl_sync_frame_template request = {0, sizeof(payload), payload};
    ++out->methods_checked;
    const auto result = object->setAWDL_SYNC_FRAME_TEMPLATE(&request);
    payload[4] = 2; // A snapshot must not reference the producer's stack.
    const uint8_t expected[] = {21, 2, 0, 0x10, 1};
    bool failed = result || concrete->syncTemplate.length() != sizeof(expected) ||
        memcmp(concrete->syncTemplate.bytes(), expected, sizeof(expected));
    const auto revision = concrete->syncTemplate.revision();
    request.payload_len = 4; // Truncated TLV must preserve the prior template.
    failed |= object->setAWDL_SYNC_FRAME_TEMPLATE(&request) != kIOReturnBadArgument;
    failed |= concrete->syncTemplate.revision() != revision ||
        concrete->syncTemplate.length() != sizeof(expected) ||
        memcmp(concrete->syncTemplate.bytes(), expected, sizeof(expected));
    // Serialize that owned snapshot through the concrete gate-checked entry.
    // The synthetic clock/frame stays in CPU memory; this does not call TX.
    ItlAwdl::SyncActionContext context = {{2, 1, 2, 3, 4, 5}, {2, 6, 7, 8, 9, 10},
                                         0, 0x123, 100032, 100000, true};
    memcpy(context.source, concrete->selfAddress.ether_addr_octet, 6);
    ItlAwdl::SyncActionParameters parameters = {};
    parameters.nextChannel = parameters.masterChannel = 6;
    parameters.downCounterTu = 32;
    parameters.awPeriodTu = parameters.extensionTu = parameters.commonTu = 16;
    parameters.actionPeriodTu = 110;
    parameters.presence = parameters.plan.step_aw = 4;
    memcpy(parameters.master, context.source, 6);
    for (auto &channel : parameters.plan.channels) channel = 6;
    uint8_t frame[106] = {};
    frame[105] = 0xa5;
    const auto length = concrete->copySyncAction(context, parameters, frame, 105);
    ItlAwdl::SyncAnnouncement decoded = {};
    failed |= length != 105 || frame[105] != 0xa5 ||
        memcmp(frame + 16, ItlAwdl::DefaultAwdlBssid, 6) ||
        memcmp(frame + 100, expected, sizeof(expected)) ||
        ItlAwdl::parseSchedule(frame, length, &decoded) != ItlAwdl::ScheduleResult::Valid ||
        decoded.tx_delay_us != 32 || decoded.aw_period_tu != 16 || decoded.presence != 4;
    context.clockValid = false;
    frame[0] = 0x5a;
    failed |= concrete->copySyncAction(context, parameters, frame, sizeof(frame)) != 0 || frame[0] != 0x5a;
    context.clockValid = true;
    // Callers cannot substitute timings independent of the native snapshot.
    for (unsigned field = 0; field < 4; ++field) {
        auto mismatch = parameters;
        if (field == 0) ++mismatch.awPeriodTu;
        if (field == 1) ++mismatch.commonTu;
        if (field == 2) ++mismatch.extensionTu;
        if (field == 3) ++mismatch.actionPeriodTu;
        failed |= concrete->copySyncAction(context, mismatch, frame, sizeof(frame)) != 0 || frame[0] != 0x5a;
    }
    concrete->syncParameters.clear();
    failed |= concrete->copySyncAction(context, parameters, frame, sizeof(frame)) != 0 || frame[0] != 0x5a;
    timing = savedTiming;
    failed |= object->setAWDL_SYNC_PARAMS(&timing) != kIOReturnSuccess;
    parameters.masterChannel = 149; // A caller cannot override the staged channel.
    failed |= concrete->copySyncAction(context, parameters, frame, sizeof(frame)) != 0 || frame[0] != 0x5a;
    parameters.masterChannel = 6;
    parameters.master[5] ^= 1;
    failed |= concrete->copySyncAction(context, parameters, frame, sizeof(frame)) != 0 || frame[0] != 0x5a;
    parameters.master[5] ^= 1;
    context.source[5] ^= 1;
    failed |= concrete->copySyncAction(context, parameters, frame, sizeof(frame)) != 0 || frame[0] != 0x5a;
    context.source[5] ^= 1;
    parameters.presence = parameters.plan.step_aw = 2;
    failed |= concrete->copySyncAction(context, parameters, frame, sizeof(frame)) != 0 || frame[0] != 0x5a;
    parameters.presence = parameters.plan.step_aw = 4;
    parameters.minExtension = 1;
    failed |= concrete->copySyncAction(context, parameters, frame, sizeof(frame)) != 0 || frame[0] != 0x5a;
    parameters.minExtension = 0;
    parameters.plan.channels[15] = 1;
    failed |= concrete->copySyncAction(context, parameters, frame, sizeof(frame)) != 0 || frame[0] != 0x5a;
    parameters.plan.channels[15] = 6;
    concrete->channelSequence.clear();
    failed |= concrete->copySyncAction(context, parameters, frame, sizeof(frame)) != 0 || frame[0] != 0x5a;
    failed |= object->setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(&savedChannels) != kIOReturnSuccess;
    role = savedRole;
    role.operation_mode = 0;
    failed |= object->setAWDL_SYNC_STATE(&role) != kIOReturnSuccess ||
        concrete->copySyncAction(context, parameters, frame, sizeof(frame)) != 0 || frame[0] != 0x5a;
    concrete->syncTemplate.clear();
    concrete->syncParameters.clear();
    concrete->networkConfiguration.clear();
    concrete->electionId = 0;
    concrete->electionIdValid = false;
    concrete->electionMetric = 0;
    concrete->electionMetricValid = false;
    concrete->minimumRateMbps = 0;
    concrete->minimumRateValid = false;
    concrete->removeProperty("AX201StagedElectionMetric");
    concrete->removeProperty("AX201StagedMinRateMbps");
    concrete->masterChannel = 0;
    concrete->masterChannelValid = false;
    concrete->removeProperty("AX201StagedMasterChannel");
    bzero(concrete->electionRssiThresholds, sizeof(concrete->electionRssiThresholds));
    concrete->electionRssiThresholdsValid = false;
    concrete->stagedSyncRole = {};
    concrete->stagedSyncRoleValid = false;
    concrete->removeProperty("AX201StagedSyncRole");
    concrete->presenceMode = 0;
    concrete->presenceModeValid = false;
    bzero(concrete->extensionCounts, sizeof(concrete->extensionCounts));
    concrete->extensionCountsValid = false;
    concrete->removeProperty("AX201StagedPresenceMode");
    concrete->removeProperty("AX201StagedExtensionCounts");
    concrete->channelSequence.clear();
    concrete->removeProperty("AX201StagedChannelSequence");
    concrete->removeProperty("AX201StagedChannelStep");
    context.clockValid = true;
    failed |= concrete->copySyncAction(context, parameters, frame, sizeof(frame)) != 0;
    if (failed) ++out->method_failures;
    ether_addr address = {{2, 0, 0, 0, 0, 1}};
    ++out->methods_checked;
    const auto &trace = concrete->configurationResult;
    if (object->setMacAddress(address) != kIOReturnUnsupported ||
        trace.attempted || trace.trace_count != ITL_AWDL_CONFIGURE_TRACE_COUNT ||
        !trace.trace_dropped ||
        trace.trace_count + trace.trace_dropped != concrete->backendCalls - callsBefore ||
        trace.trace[0].vptr_offset != 0xfa0 || trace.trace[0].result != kIOReturnBadArgument ||
        trace.trace[1].vptr_offset != 0xfa0 || trace.trace[1].result != kIOReturnUnsupported)
        ++out->method_failures;
    concrete->backendTrace = nullptr;
    bzero(&concrete->configurationResult, sizeof(concrete->configurationResult));
}

IOReturn ItlAwdlNativeInterface::probeGated(AirportItlwm *parent, ItlIwx *radio,
    const ItlAwdlNativeInitRequest *request, ItlAwdlNativeInitResponse *out) {
    bzero(out, sizeof(*out));
    out->version = ITL_AWDL_ABI_VERSION;
    // The native layouts/lifecycle below were audited against this exact build.
    if (strcmp(osversion, "24H32")) return kIOReturnUnsupported;
    if (parent->isInactive()) return kIOReturnNotReady;
    ItlAwdlStatusResponse status = {};
    IOReturn result = radio->awdlCommand(parent, ItlAwdlStatus, nullptr, &status);
    if (result) return result;
    if (status.session != ItlAwdlIdle || parent->getAwdlInterface()) return kIOReturnBusy;
    if (status.device_id != 0x7af0) return kIOReturnUnsupported;
    out->live_before = metaClass->getInstanceCount();
    if (out->live_before) return kIOReturnBusy;
    ether_addr address;
    memcpy(address.ether_addr_octet, status.local_mac, sizeof(address.ether_addr_octet));
    address.ether_addr_octet[0] = (address.ether_addr_octet[0] | 2) & ~1;
    address.ether_addr_octet[5] ^= 0x80; // In-memory address; never programmed/transmitted.
    for (uint32_t i = 0; i < request->iterations; ++i) {
        auto object = new ItlAwdlNativeInterface;
        if (!object) { result = kIOReturnNoMemory; break; }
        ++out->attempted;
        bool initialized = object->initializeForProbe(parent, &address, request->stop_after, out);
        if (initialized && request->stop_after == 0) checkBackendContract(object, out);
        if (initialized && request->stop_after == 0) {
            IO80211AWDLProtocol *native = object;
            const auto returnedAddress = native->getSelfMacAddr();
            ++out->methods_checked;
            if (memcmp(&returnedAddress, &address, sizeof(address))) ++out->method_failures;
        }
        object->disposeInitializedResources();
        if (parent->isOpen(object)) ++out->provider_open_after;
        object->release();
        ++out->released;
        if (!initialized) { result = kIOReturnError; break; }
        if (out->provider_open_after || out->method_failures) {
            result = kIOReturnError;
            break;
        }
    }
    out->live_after = metaClass->getInstanceCount();
    if (parent->getAwdlInterface()) result = kIOReturnError;
    out->last_error = static_cast<uint32_t>(result);
    // Return the report even when the native call failed; the CLI must check
    // last_error and counts. ABI/authorization errors remain ordinary IOReturns.
    return kIOReturnSuccess;
}

IOReturn ItlAwdlNativeInterface::probeInitialization(AirportItlwm *parent, ItlIwx *radio,
    const ItlAwdlNativeInitRequest *request, ItlAwdlNativeInitResponse *out) {
    if (!parent || !radio || !out || !request || request->version != ITL_AWDL_ABI_VERSION ||
        request->reserved || !request->iterations || request->iterations > 8 ||
        request->stop_after > 2) return kIOReturnBadArgument;
    auto gate = radio->getMainCommandGate();
    if (!gate) return kIOReturnNotReady;
    NativeProbeLease lease;
    if (!lease.owned) return kIOReturnBusy;
    bzero(out, sizeof(*out));
    out->version = ITL_AWDL_ABI_VERSION;
    auto single = *request;
    single.iterations = 1;
    ItlAwdlNativeInitResponse step = {};
    struct Call { AirportItlwm *parent; ItlIwx *radio;
        const ItlAwdlNativeInitRequest *request; ItlAwdlNativeInitResponse *out; } call = {
            parent, radio, &single, &step};
    for (uint32_t i = 0; i < request->iterations; ++i) {
        IOReturn result = gate->runAction([](OSObject *, void *argument, void *, void *, void *) -> IOReturn {
            auto call = static_cast<Call *>(argument);
            return probeGated(call->parent, call->radio, call->request, call->out);
        }, &call);
        if (result) return result;
        // Even init-only power-interest deregistration can defer a release.
        // Open the gate before waiting, just as the native-start probe does.
        unsigned waited = 0;
        while (metaClass->getInstanceCount() && waited < 250) {
            IOSleep(1);
            ++waited;
        }
        step.live_after = metaClass->getInstanceCount();
        if (step.live_after) step.last_error = kIOReturnTimeout;
#define SUM(field) out->field += step.field
        SUM(attempted); SUM(base_initialized); SUM(virtual_initialized); SUM(released);
        SUM(methods_checked); SUM(method_failures); SUM(provider_open_after); SUM(provider_opened);
#undef SUM
        if (!i) out->live_before = step.live_before;
        out->live_after = step.live_after;
        out->role = step.role;
        out->last_error = step.last_error;
        if (out->last_error) break;
    }
    return kIOReturnSuccess;
}

void ItlAwdlNativeInterface::describeManaged(AirportItlwm *parent,
    ItlAwdlNativeSessionResponse *out) {
    bzero(out, sizeof(*out));
    out->version = ITL_AWDL_ABI_VERSION;
    out->boot_reserved = nativePeerAttempted();
    out->state = out->boot_reserved ? ItlAwdlNativeRetired : ItlAwdlNativeUnused;
    out->interface_count = metaClass->getInstanceCount();
    auto peerClass = nativePeerClass();
    out->peer_count = peerClass ? peerClass->getInstanceCount() : 0;
    out->controller_pointer = parent->getAwdlInterface() != nullptr;
    auto object = parent->nativeAwdlInterface;
    if (!object) return;
    out->state = ItlAwdlNativeRetained;
    out->role = static_cast<uint32_t>(object->getInterfaceRole());
    out->provider_open = parent->isOpen(object);
    out->bsd_interfaces = object->getBSDInterface() != nullptr;
    out->backend_calls = object->backendCalls;
    out->registry_id = object->getRegistryEntryID();
}

void ItlAwdlNativeInterface::releaseManagedGated(AirportItlwm *parent) {
    auto object = parent->nativeAwdlInterface;
    if (!object) return;
    auto radio = OSDynamicCast(ItlIwx, parent->fHalService);
    if (radio && radio->awdlNativeRadioOwner == object) {
        radio->awdlCommand(object, ItlAwdlEnd, nullptr, nullptr);
        radio->awdlNativeRadioOwner = nullptr;
        radio->awdlNativeTxQualification = false;
        radio->awdlNativeP2pQualification = false;
    }
    parent->nativeAwdlInterface = nullptr;
    object->stop(parent);
    object->disposeInitializedResources();
    object->detach(parent);
    object->release();
    if (radio) radio->awdlNativeInterfaceActive = false;
}

void ItlAwdlNativeInterface::releaseManaged(AirportItlwm *parent) {
    if (!parent || !parent->nativeAwdlInterface) return;
    auto radio = OSDynamicCast(ItlIwx, parent->fHalService);
    auto gate = radio ? radio->getMainCommandGate() : nullptr;
    if (gate) {
        gate->runAction([](OSObject *, void *argument, void *, void *, void *) -> IOReturn {
            releaseManagedGated(static_cast<AirportItlwm *>(argument));
            return kIOReturnSuccess;
        }, parent);
    } else {
        // Provider teardown fallback only. Normal stop calls this before the
        // radio gate/work queue are removed; user clients cannot use this path.
        releaseManagedGated(parent);
    }
}

IOReturn ItlAwdlNativeInterface::managedSessionGated(AirportItlwm *parent, ItlIwx *radio,
    uint32_t operation, ItlAwdlNativeSessionResponse *out) {
    if (strcmp(osversion, "24H32")) return kIOReturnUnsupported;
    if (parent->isInactive()) return kIOReturnNotReady;
    ItlAwdlStatusResponse status = {};
    auto result = radio->awdlCommand(parent, ItlAwdlStatus, nullptr, &status);
    if (result) return result;
    if (status.device_id != 0x7af0) return kIOReturnUnsupported;
    // Once published, child BSD/Nexus services own live references. Manual
    // probe teardown must not bypass normal IOService termination ordering.
    if (operation == ItlAwdlNativeRetire && parent->nativeAwdlInterface &&
        parent->nativeAwdlInterface->bsdPublishRequested) return kIOReturnNotPermitted;
    if (operation == ItlAwdlNativeRetire) releaseManagedGated(parent);
    if (operation == ItlAwdlNativeCreate && !parent->nativeAwdlInterface) {
        if (nativePeerAttempted()) return kIOReturnNotPermitted;
        auto peerClass = nativePeerClass();
        if (!peerClass) return kIOReturnUnsupported;
        if (status.session != ItlAwdlIdle || parent->getAwdlInterface() ||
            metaClass->getInstanceCount() || peerClass->getInstanceCount()) return kIOReturnBusy;
        radio->awdlNativeInterfaceActive = true;
        auto object = new ItlAwdlNativeInterface;
        if (!object) {
            radio->awdlNativeInterfaceActive = false;
            return kIOReturnNoMemory;
        }
        ether_addr address;
        memcpy(address.ether_addr_octet, status.local_mac, sizeof(address.ether_addr_octet));
        address.ether_addr_octet[0] = (address.ether_addr_octet[0] | 2) & ~1;
        address.ether_addr_octet[5] ^= 0x80;
        ItlAwdlNativeInitResponse initialized = {};
        bool ready = object->initializeForProbe(parent, &address, 2, &initialized);
        bool attached = ready && object->attach(parent);
        if (attached) {
            object->startAllowed = true;
            ready = object->start(parent);
            auto state = object->nativeStartState();
            ready = ready && state && state->peerManager &&
                state->peerManager->getMetaClass() == peerClass &&
                peerClass->getInstanceCount() == 1 && parent->getAwdlInterface() == object &&
                !object->getBSDInterface();
        }
        if (!ready || !attached) {
            object->stop(parent);
            object->disposeInitializedResources();
            if (attached) object->detach(parent);
            object->release();
            radio->awdlNativeInterfaceActive = false;
            return kIOReturnError;
        }
        // The controller owns the allocation reference. Client close is not
        // native teardown. No registerService, BSD registration or link enable.
        parent->nativeAwdlInterface = object;
    }
    describeManaged(parent, out);
    return kIOReturnSuccess;
}

IOReturn ItlAwdlNativeInterface::managedSession(AirportItlwm *parent, ItlIwx *radio,
    const ItlAwdlNativeSessionRequest *request, ItlAwdlNativeSessionResponse *out) {
    if (!parent || !radio || !request || !out || request->version != ITL_AWDL_ABI_VERSION ||
        request->operation > ItlAwdlNativeRetire || request->reserved[0] || request->reserved[1])
        return kIOReturnBadArgument;
    auto gate = radio->getMainCommandGate();
    if (!gate) return kIOReturnNotReady;
    NativeProbeLease lease;
    if (!lease.owned) return kIOReturnBusy;
    struct Call { AirportItlwm *parent; ItlIwx *radio; uint32_t operation;
        ItlAwdlNativeSessionResponse *out; } call = {parent, radio, request->operation, out};
    auto result = gate->runAction([](OSObject *, void *argument, void *, void *, void *) -> IOReturn {
        auto call = static_cast<Call *>(argument);
        return managedSessionGated(call->parent, call->radio, call->operation, call->out);
    }, &call);
    if (result) return result;
    if (request->operation == ItlAwdlNativeRetire) {
        auto peerClass = nativePeerClass();
        unsigned waited = 0;
        while ((metaClass->getInstanceCount() || (peerClass && peerClass->getInstanceCount())) && waited < 250) {
            IOSleep(1);
            ++waited;
        }
        out->interface_count = metaClass->getInstanceCount();
        out->peer_count = peerClass ? peerClass->getInstanceCount() : 0;
        if (out->interface_count || out->peer_count) out->last_error = kIOReturnTimeout;
    }
    return kIOReturnSuccess;
}

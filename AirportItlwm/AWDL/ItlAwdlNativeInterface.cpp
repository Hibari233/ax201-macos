// SPDX-License-Identifier: GPL-2.0-or-later
#include "ItlAwdlNativeInterface.hpp"
#include "ItlAwdlNativePacket.hpp"
#include "ItlAwdlNativeQueues.hpp"
#include "AirportItlwmV2.hpp"
#include <Airport/macOS15/IO80211ManagementBuffer.h>
#include <AWDL/AwdlNativeFrame.h>
#include <Airport/CCPipe.h>
#include <libkern/version.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/OSAtomic.h>

// Exported non-virtual member in 24H32. The bridge uses only the retained
// OSObject pointer; no guessed peer-manager class layout or private writes.
extern "C" IOReturn nativeAwdlConfigure(OSObject *)
    __asm__("__ZN22IO80211AWDLPeerManager9configureEv");

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
    if (loop && loop->inGate() && configurationTracing) {
        auto &trace = configurationResult;
        if (trace.trace_count < ITL_AWDL_CONFIGURE_TRACE_COUNT)
            trace.trace[trace.trace_count++] = {0xfa0U + 8U * index, static_cast<uint32_t>(result)};
        else ++trace.trace_dropped;
    }
    return result;
}

IOReturn ItlAwdlNativeInterface::getAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_getAWDL_SYNC_ENABLED, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    static_assert(sizeof(*data) == 12, "24H32 sync-enabled request");
    bzero(data, sizeof(*data));
    data->version = 1;
    // Radio synchronization cannot be enabled yet. Report its actual state,
    // independently of administrative IFF_UP or a staged frame template.
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
    if (isInterfaceEnabled()) return finish(kIOReturnBusy);
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
    if (isInterfaceEnabled()) return finish(kIOReturnBusy);
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

IOReturn ItlAwdlNativeInterface::setAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel *data) {
    const auto finish = [&](IOReturn r) { return backendResult(Backend_setAWDL_MASTER_CHANNEL, r); };
    if (!data) return finish(kIOReturnBadArgument);
    if (strcmp(osversion, "24H32")) return finish(kIOReturnUnsupported);
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (!loop || !loop->inGate() || resourcesDisposed || isInactive()) return finish(kIOReturnNotReady);
    if (data->version > 1) return finish(kIOReturnBadArgument);
    if (isInterfaceEnabled()) return finish(kIOReturnBusy);
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
    if (isInterfaceEnabled()) return finish(kIOReturnBusy);
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
    if (isInterfaceEnabled()) return finish(kIOReturnBusy);
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
    if (isInterfaceEnabled()) return finish(kIOReturnBusy);
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
    if (isInterfaceEnabled()) return finish(kIOReturnBusy);
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
            object->configurationTracing = true;
            saved.native_result = static_cast<uint32_t>(nativeAwdlConfigure(state->peerManager));
            object->configurationTracing = false;
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

size_t ItlAwdlNativeInterface::copySyncAction(const ItlAwdl::SyncActionContext &context,
    const ItlAwdl::SyncActionParameters &parameters, uint8_t *output, size_t capacity) {
    auto loop = controller ? controller->getWorkLoop() : nullptr;
    if (strcmp(osversion, "24H32") || !loop || !loop->inGate() ||
        resourcesDisposed || isInactive() || !syncTemplate.length() ||
        !electionIdValid || !masterChannelValid || parameters.masterChannel != masterChannel ||
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
        isInactive() || !bsdPublishRequested || !getBSDInterface() ||
        !startAttempted || !isInterfaceEnabled()) return kIOReturnNotReady;
    // Publishing or administratively raising awdl0 must not admit frames
    // before an implemented backend has actually started synchronization.
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
    virtualInitAttempted = false;
    resourcesDisposed = false;
    bpfDisposed = startAllowed = startAttempted = failPeerCreation = false;
    backendCalls = peerCreationCalls = partialCleanups = 0;
    configurationTracing = false;
    bzero(&configurationResult, sizeof(configurationResult));
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
    concrete->configurationTracing = true;
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
    concrete->networkConfiguration.clear();
    concrete->electionId = 0;
    concrete->electionIdValid = false;
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
    concrete->configurationTracing = false;
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
    parent->nativeAwdlInterface = nullptr;
    object->stop(parent);
    object->disposeInitializedResources();
    object->detach(parent);
    object->release();
    auto radio = OSDynamicCast(ItlIwx, parent->fHalService);
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

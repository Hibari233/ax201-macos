// SPDX-License-Identifier: GPL-2.0-or-later
// Bounded single-peer AWDL radio diagnostics. Not a native AirDrop driver.
#include "ItlIwx.hpp"
#include <AWDL/AwdlFrame.h>
#include <AWDL/AwdlData.h>
#include <AWDL/AwdlEcho.h>
#include <AWDL/AwdlRegulatory.h>
#include <AWDL/AwdlRxClock.h>
#include <AWDL/AwdlSyncAction.h>
#include <AWDL/AwdlTxCompletion.h>
#include <AWDL/AwdlAck.h>
#include <kern/clock.h>

static uint64_t awdlNowUs() {
    uint64_t absolute = 0, nanos = 0;
    clock_get_uptime(&absolute);
    absolutetime_to_nanoseconds(absolute, &nanos);
    return nanos / 1000;
}

IOReturn ItlIwx::awdlCommand(OSObject *owner, uint32_t selector, const void *input, void *output) {
    if (!awdl.ready) return kIOReturnNotReady;
    AwdlCall call = {owner, selector, input, output};
    return getMainCommandGate()->runAction(awdlCommandGated, this, &call);
}
IOReturn ItlIwx::awdlCommandGated(OSObject *, void *p, void *arg, void *, void *) {
    auto self = static_cast<ItlIwx *>(p);
    auto call = static_cast<AwdlCall *>(arg);
    auto &s = self->awdl;
    auto ic = &self->com.sc_ic;
    if (!s.ready) return kIOReturnNotReady;
    switch (call->selector) {
        case ItlAwdlStatus: {
            if (!call->output) return kIOReturnBadArgument;
            auto out = static_cast<ItlAwdlStatusResponse *>(call->output);
            bzero(out, sizeof(*out));
            out->version = ITL_AWDL_ABI_VERSION;
            out->session = s.session; out->opmode = ic->ic_opmode; out->net_state = ic->ic_state;
            out->channel = s.channel; out->if_flags = ic->ic_ac.ac_if.if_flags;
            out->last_error = s.lastError; out->stop_reason = s.stopReason;
            out->rx_total = s.rxTotal; out->rx_awdl = s.rxAwdl; out->rx_peer = s.rxPeer;
            out->rx_dropped = s.rxDropped; out->rx_invalid = s.rxInvalid;
            out->device_id = self->pci.pa_tag->configRead16(kIOPCIConfigDeviceID);
            out->driver_flags = self->com.sc_flags; out->queued = s.count;
            out->capabilities = 16777215; // Bit 23: bounded P2P TX series observation. No native AirDrop.
            memcpy(out->local_mac, ic->ic_myaddr, sizeof(out->local_mac));
            return kIOReturnSuccess;
        }
        case ItlAwdlBegin:
            return self->awdlBegin(call->owner, static_cast<const ItlAwdlBeginRequest *>(call->input));
        case ItlAwdlBeginLinked:
            return self->awdlBegin(call->owner, static_cast<const ItlAwdlBeginRequest *>(call->input), true);
        case ItlAwdlBeginData:
            return self->awdlBegin(call->owner, static_cast<const ItlAwdlBeginRequest *>(call->input), false, true);
        case ItlAwdlDataStatus: {
            if (!call->output) return kIOReturnBadArgument;
            memcpy(call->output, &s.dataStatus, sizeof(s.dataStatus));
            static_cast<ItlAwdlDataStatusResponse *>(call->output)->version = ITL_AWDL_ABI_VERSION;
            return kIOReturnSuccess;
        }
        case ItlAwdlReadData: {
            if (!call->output) return kIOReturnBadArgument;
            bzero(call->output, sizeof(ItlAwdlDataResponse));
            if (!s.owner || s.owner != call->owner) return kIOReturnNotPermitted;
            if (!s.dataStatus.queued) return kIOReturnNotReady;
            memcpy(call->output, &s.dataRing[s.dataHead], sizeof(*s.dataRing));
            bzero(&s.dataRing[s.dataHead], sizeof(*s.dataRing));
            s.dataHead = (s.dataHead + 1) % ITL_AWDL_RING_COUNT;
            --s.dataStatus.queued;
            return kIOReturnSuccess;
        }
        case ItlAwdlLinkedStatus: {
            if (!call->output) return kIOReturnBadArgument;
            auto out = static_cast<ItlAwdlLinkedStatusResponse *>(call->output);
            bzero(out, sizeof(*out));
            out->version = ITL_AWDL_ABI_VERSION;
            out->linked_mode = s.linked;
            out->firmware_generation = self->com.sc_generation;
            out->start_generation = s.linkedGeneration;
            out->channel = s.channel;
            out->filter_enabled = s.linkedFilterEnabled;
            out->restore_error = s.linkedRestoreError;
            out->link_matches = self->awdlLinkMatches();
            return kIOReturnSuccess;
        }
        case ItlAwdlProbe:
            return self->awdlProbe(call->owner);
        case ItlAwdlDataProbe:
            return self->awdlDataProbe(call->owner, static_cast<const ItlAwdlDataProbeRequest *>(call->input));
        case ItlAwdlClock:
            if (!call->output) return kIOReturnBadArgument;
            if (!s.owner || s.owner != call->owner) return kIOReturnNotPermitted;
            if (s.commandBusy) return kIOReturnBusy;
            return self->awdlReadClock(static_cast<ItlAwdlClockResponse *>(call->output));
        case ItlAwdlRunTest:
            return self->awdlRun(call->owner, static_cast<const ItlAwdlRunRequest *>(call->input));
        case ItlAwdlPrepareCountry:
            return self->awdlRefreshCountry(call->owner, true);
        case ItlAwdlRunStatus:
            if (!call->output) return kIOReturnBadArgument;
            memcpy(call->output, &s.run, sizeof(s.run));
            static_cast<ItlAwdlRunStatusResponse *>(call->output)->version = ITL_AWDL_ABI_VERSION;
            return kIOReturnSuccess;
        case ItlAwdlTxStatus:
            if (!call->output) return kIOReturnBadArgument;
            memcpy(call->output, &s.tx, sizeof(s.tx));
            static_cast<ItlAwdlTxStatusResponse *>(call->output)->version = ITL_AWDL_ABI_VERSION;
            return kIOReturnSuccess;
        case ItlAwdlRegStatus: {
            if (!call->output) return kIOReturnBadArgument;
            auto out = static_cast<ItlAwdlRegStatusResponse *>(call->output);
            bzero(out, sizeof(*out));
            out->version = ITL_AWDL_ABI_VERSION;
            out->channel = s.channel;
            if (s.channel <= IEEE80211_CHAN_MAX) out->channel_flags = ic->ic_channels[s.channel].ic_flags;
            out->query_count = s.mccQueries; out->last_query_error = s.mccError;
            memcpy(out->firmware_country, s.mccCountry, 2);
            return kIOReturnSuccess;
        }
        case ItlAwdlEnd:
            if (s.owner && s.owner != call->owner) return kIOReturnNotPermitted;
            self->awdlStop(ItlAwdlClosed);
            s.owner = nullptr;
            s.count = s.head = 0;
            s.rejected = false;
            if (s.ring) bzero(s.ring, sizeof(*s.ring) * (ITL_AWDL_RING_COUNT + 1));
            if (s.dataRing) bzero(s.dataRing, sizeof(*s.dataRing) * ITL_AWDL_RING_COUNT);
            s.dataHead = s.dataStatus.queued = s.dataStatus.enabled = 0;
            return kIOReturnSuccess;
        case ItlAwdlReadRejected:
            if (!call->output) return kIOReturnBadArgument;
            bzero(call->output, sizeof(ItlAwdlFrameResponse));
            if (!s.owner || s.owner != call->owner) return kIOReturnNotPermitted;
            if (!s.rejected) return kIOReturnNotReady;
            // Separate, bounded diagnostic sample. Never feed rejected frames
            // to the protocol or network stack. Cleared on close/new session.
            memcpy(call->output, &s.ring[ITL_AWDL_RING_COUNT].frame, sizeof(ItlAwdlFrameResponse));
            return kIOReturnSuccess;
        case ItlAwdlRead:
        case ItlAwdlReadTimed: {
            if (!call->output) return kIOReturnBadArgument;
            const size_t size = call->selector == ItlAwdlReadTimed ? sizeof(ItlAwdlTimedFrameResponse) : sizeof(ItlAwdlFrameResponse);
            bzero(call->output, size);
            if (!s.owner || s.owner != call->owner) return kIOReturnNotPermitted;
            if (!s.count) return kIOReturnNotReady;
            memcpy(call->output, &s.ring[s.head], size);
            bzero(&s.ring[s.head], sizeof(*s.ring));
            s.head = (s.head + 1) % ITL_AWDL_RING_COUNT;
            --s.count;
            return kIOReturnSuccess;
        }
        default: return kIOReturnUnsupported;
    }
}

IOReturn ItlIwx::awdlBegin(OSObject *owner, const ItlAwdlBeginRequest *r, bool linked, bool data) {
    auto ic = &com.sc_ic;
    auto ifp = &ic->ic_ac.ac_if;
    if (!r || !owner || r->version != ITL_AWDL_ABI_VERSION || r->reserved ||
        r->padding[0] || r->padding[1] || r->dwell_ms < 100 || r->dwell_ms > ITL_AWDL_MAX_DWELL_MS ||
        !ItlAwdl::unicastMac(r->peer)) return kIOReturnBadArgument;
    // A retained native object excludes all userspace radio diagnostics. Only
    // its internal, receive-only qualification lease may own the monitor.
    // It cannot use linked/data mode, TX probes or the diagnostic scheduler.
    if ((awdlNativeInterfaceActive && owner != awdlNativeRadioOwner) ||
        (awdlNativeRadioOwner && (owner != awdlNativeRadioOwner || linked || data)))
        return kIOReturnBusy;
    uint32_t channel = r->channel;
    if (linked) {
        if (channel) return kIOReturnBadArgument;
        const uint32_t required = IWX_FLAG_MAC_ACTIVE | IWX_FLAG_STA_ACTIVE | IWX_FLAG_BINDING_ACTIVE;
        if (ic->ic_opmode != IEEE80211_M_STA || ic->ic_state != IEEE80211_S_RUN ||
            (ifp->if_flags & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING) ||
            (com.sc_flags & required) != required || !ic->ic_bss || !ic->ic_bss->ni_associd ||
            !ic->ic_bss->ni_dtimperiod || !ic->ic_bss->ni_chan ||
            ic->ic_bss->ni_chan == IEEE80211_CHAN_ANYC) return kIOReturnNotReady;
        channel = ieee80211_chan2ieee(ic, ic->ic_bss->ni_chan);
        if (!channel || channel > IEEE80211_CHAN_MAX) return kIOReturnNotReady;
    } else if (channel != 6 && channel != 44 && channel != 149) return kIOReturnUnsupported;
    // Use the firmware's regulatory channel map; no forced/disabled channels.
    if (!isset(ic->ic_chan_active, channel) || !ic->ic_channels[channel].ic_freq)
        return kIOReturnNotPermitted;
    if (awdl.commandBusy || awdl.session != ItlAwdlIdle || (awdl.owner && awdl.owner != owner) ||
        (!linked && ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) || ic->ic_state != IEEE80211_S_INIT)) ||
        ic->ic_opmode != IEEE80211_M_STA || !ic->ic_bss ||
        (com.sc_flags & (IWX_FLAG_SCANNING | IWX_FLAG_BGSCAN | IWX_FLAG_SHUTDOWN | IWX_FLAG_RFKILL | IWX_FLAG_HW_ERR)))
        return kIOReturnBusy;
    if (!awdl.ring) awdl.ring = static_cast<ItlAwdlTimedFrameResponse *>(IOMalloc(sizeof(*awdl.ring) * (ITL_AWDL_RING_COUNT + 1)));
    if (!awdl.ring) return kIOReturnNoMemory;
    if (data && !awdl.dataRing)
        awdl.dataRing = static_cast<ItlAwdlDataResponse *>(IOMalloc(sizeof(*awdl.dataRing) * ITL_AWDL_RING_COUNT));
    if (data && !awdl.dataRing) return kIOReturnNoMemory;
    if (!awdl.timer) {
        awdl.timer = IOTimerEventSource::timerEventSource(this, awdlTimeout);
        if (!awdl.timer) return kIOReturnNoMemory;
        if (getMainWorkLoop()->addEventSource(awdl.timer) != kIOReturnSuccess) {
            OSSafeReleaseNULL(awdl.timer); return kIOReturnNoResources;
        }
    }
    bzero(awdl.ring, sizeof(*awdl.ring) * (ITL_AWDL_RING_COUNT + 1));
    if (awdl.dataRing) bzero(awdl.dataRing, sizeof(*awdl.dataRing) * ITL_AWDL_RING_COUNT);
    bzero(&awdl.dataStatus, sizeof(awdl.dataStatus));
    awdl.dataStatus.enabled = data;
    awdl.dataHead = 0;
    awdl.rejected = false;
    awdl.head = awdl.count = 0;
    awdl.rxTotal = awdl.rxAwdl = awdl.rxPeer = awdl.rxDropped = awdl.rxInvalid = 0;
    awdlNativePeerProfile = {};
    awdlNativePeerProfileSamples = 0;
    awdlObservedReservation = {};
    awdlNativeP2pSession = {};
    awdlNativeP2pCommandVersion = awdlNativeP2pNotifVersion = 0;
    bzero(&awdl.tx, sizeof(awdl.tx));
    awdl.lastPeerRxUs = awdl.lastProbeUs = 0;
    awdl.lastPeerGp2 = 0;
    awdl.schedule = ItlAwdl::ScheduleTracker();
    awdl.clock = ItlAwdl::ClockCorrelation();
    bzero(&awdl.run, sizeof(awdl.run));
    awdl.pendingWindow = awdl.hasLastTarget = false;
    ++awdl.epoch;
    awdl.mccQueries = awdl.mccError = 0;
    awdl.lastMccQueryUs = 0;
    // Firmware often announces the country during normal Wi-Fi startup,
    // before the bounded monitor session. Like Intel init_mcc's replay of
    // the last regulatory hint, preserve this firmware-originated input
    // over a radio restart; our experiment additionally expires it in 60 s.
    if (!ItlAwdl::countryHintFresh(awdlNowUs(), awdl.countryHintUs)) awdl.countryHintUs = 0;
    awdl.appliedCountryHint = awdl.appliedCountrySource = 0;
    awdl.mccNeedsRefresh = bool(awdl.countryHintUs);
    bzero(awdl.mccCountry, sizeof(awdl.mccCountry));
    awdl.lastError = awdl.stopReason = 0;
    awdl.linked = linked;
    awdl.linkedFilterEnabled = false;
    awdl.linkedRestoreError = 0;
    awdl.linkedGeneration = linked ? com.sc_generation : -1;
    bzero(awdl.linkedBssid, sizeof(awdl.linkedBssid));
    awdl.savedChannel = ic->ic_ibss_chan;
    awdl.savedMode = ic->ic_opmode;
    awdl.savedAutoJoin = ic->ic_flags & IEEE80211_F_AUTO_JOIN;
    awdl.owner = owner;
    memcpy(awdl.peer, r->peer, 6);
    awdl.channel = channel;
    awdl.session = ItlAwdlStarting;
    if (!linked) {
        ic->ic_opmode = IEEE80211_M_MONITOR;
        ic->ic_ibss_chan = &ic->ic_channels[channel];
        ic->ic_flags &= ~IEEE80211_F_AUTO_JOIN;
    }
    awdl.deadlineUs = awdlNowUs() + uint64_t(r->dwell_ms) * 1000;
    auto timerResult = awdl.timer->setTimeoutMS(r->dwell_ms);
    if (timerResult) {
        awdlStop(ItlAwdlRadioError, timerResult);
        return timerResult;
    }
    if (linked) {
        auto node = ieee80211_ref_node(ic->ic_bss);
        auto in = reinterpret_cast<iwx_node *>(node);
        memcpy(awdl.linkedBssid, node->ni_bssid, sizeof(awdl.linkedBssid));
        awdl.linkedId = in->in_id; awdl.linkedColor = in->in_color;
        // Retain STA role, association, keys, channel and power policy. Only
        // management/control reception is broadened; data promiscuity, FCS
        // append and decrypt-disable flags are deliberately not enabled.
        awdl.commandBusy = true;
        awdl.linkedFilterEnabled = true; // A timed-out command may have applied.
        int error = iwx_mac_ctxt_cmd(&com, in, IWX_FW_CTXT_ACTION_MODIFY, 1);
        ieee80211_release_node(ic, node);
        awdl.commandBusy = false;
        if (awdl.session == ItlAwdlStopping) {
            awdlFinishLinkedStop();
            return kIOReturnAborted;
        }
        if (error || !awdlLinkMatches()) {
            awdlStop(ItlAwdlRadioError, error ? error : ENETDOWN);
            return kIOReturnIOError;
        }
        awdl.session = ItlAwdlListening;
        return kIOReturnSuccess;
    }
    int error = iwx_init(ifp);
    if (error) {
        awdlStop(ItlAwdlRadioError, error);
        return kIOReturnIOError;
    }
    if (!awdlActive()) return kIOReturnAborted;
    // iwx_newstate_task reports Listening only after firmware RUN succeeds.
    return kIOReturnSuccess;
}

void ItlIwx::awdlStop(uint32_t reason, uint32_t error) {
    if (!awdlActive()) return;
    __atomic_store_n(&awdlNativeTraceArmed,uint32_t(0),__ATOMIC_RELEASE);
    awdl.session = ItlAwdlStopping;
    // This exclusive lease ends through iwx_stop/stop_device below, which
    // removes the entire firmware context, including its session protection.
    awdlNativeP2pSession.stop();
    ++awdl.epoch;
    awdl.run.active = 0;
    if (awdl.pendingWindow) ++awdl.run.cancelled;
    awdl.pendingWindow = false;
    if (awdl.runTimer) awdl.runTimer->cancelTimeout();
    awdl.stopReason = reason; awdl.lastError = error;
    if (awdl.timer) awdl.timer->cancelTimeout();
    if (awdl.linked) {
        // A synchronous firmware command releases the gate while sleeping.
        // Its caller completes restoration if a timer/close interrupts it.
        if (!awdl.commandBusy) awdlFinishLinkedStop();
        return;
    }
    iwx_stop(&com.sc_ic.ic_ac.ac_if);
    awdl.tx.aborted += awdl.tx.pending;
    awdl.tx.pending = 0;
    com.sc_ic.ic_opmode = awdl.savedMode;
    com.sc_ic.ic_ibss_chan = awdl.savedChannel;
    com.sc_ic.ic_flags = (com.sc_ic.ic_flags & ~IEEE80211_F_AUTO_JOIN) | awdl.savedAutoJoin;
    awdl.session = ItlAwdlIdle;
}

bool ItlIwx::awdlLinkMatches(bool requireChannel) const {
    const auto ic = &com.sc_ic;
    const auto ni = ic->ic_bss;
    const uint32_t required = IWX_FLAG_MAC_ACTIVE | IWX_FLAG_STA_ACTIVE | IWX_FLAG_BINDING_ACTIVE;
    if (awdl.linkedGeneration < 0 || com.sc_generation != awdl.linkedGeneration ||
        ic->ic_opmode != IEEE80211_M_STA || ic->ic_state != IEEE80211_S_RUN ||
        !(ic->ic_if.if_flags & IFF_RUNNING) || (com.sc_flags & required) != required ||
        (com.sc_flags & (IWX_FLAG_SHUTDOWN | IWX_FLAG_HW_ERR | IWX_FLAG_RFKILL)) ||
        !ni || !ni->ni_associd || !ni->ni_chan || ni->ni_chan == IEEE80211_CHAN_ANYC ||
        !ItlAwdl::equalMac(ni->ni_bssid, awdl.linkedBssid)) return false;
    const auto in = reinterpret_cast<const iwx_node *>(ni);
    return (!requireChannel || ni->ni_chan == &ic->ic_channels[awdl.channel]) &&
        in->in_id == awdl.linkedId && in->in_color == awdl.linkedColor;
}

void ItlIwx::awdlFinishLinkedStop() {
    if (!awdl.linked || awdl.session != ItlAwdlStopping || awdl.commandBusy) return;
    // Normal power/association teardown will remove this MAC. Never issue
    // restoration commands against a reset firmware or a replacement BSS.
    // A channel change can preserve the same firmware MAC and association.
    // Stop observing the old channel, but still restore that MAC's filter.
    if (awdl.linkedFilterEnabled && awdlLinkMatches(false) &&
        awdl.stopReason != ItlAwdlPowerChange && awdl.stopReason != ItlAwdlDetached &&
        awdl.stopReason != ItlAwdlLinkChanged) {
        auto ic = &com.sc_ic;
        auto node = ieee80211_ref_node(ic->ic_bss);
        int generation = com.sc_generation;
        awdl.commandBusy = true;
        // Stopping excludes the linked filter in iwx_mac_ctxt_cmd.
        int error = iwx_mac_ctxt_cmd(&com, reinterpret_cast<iwx_node *>(node), IWX_FW_CTXT_ACTION_MODIFY, 1);
        ieee80211_release_node(ic, node);
        awdl.commandBusy = false;
        if (error && generation == com.sc_generation) {
            awdl.linkedRestoreError = error;
            awdl.lastError = error;
            // Normal HAL recovery owns any necessary restart. Do not leave
            // an uncertain filter active or suppress normal STA recovery.
            task_add(systq, &com.init_task);
        }
    }
    awdl.linkedFilterEnabled = false;
    awdl.linked = false;
    awdl.session = ItlAwdlIdle;
}

IOReturn ItlIwx::awdlTransmitGuard(OSObject *owner, bool data, bool scheduled, bool native) {
    if (native) {
        if (!awdlNativeTxQualification || !awdlNativeRadioOwner || owner != awdlNativeRadioOwner ||
            data || scheduled) return kIOReturnNotPermitted;
        if (awdlNativeP2pQualification && !awdlNativeP2pSession.ready(com.sc_generation))
            return kIOReturnNotReady;
    } else if (awdlNativeRadioOwner) return kIOReturnUnsupported;
    auto ic = &com.sc_ic;
    if (!awdl.owner || awdl.owner != owner) return kIOReturnNotPermitted;
    if (awdl.linked || bool(awdl.dataStatus.enabled) != data) return kIOReturnUnsupported;
    if (!scheduled && (awdl.commandBusy || awdl.run.active)) return kIOReturnBusy;
    if (scheduled && awdl.mccNeedsRefresh) return kIOReturnNotReady;
    if (awdl.session != ItlAwdlListening || ic->ic_opmode != IEEE80211_M_MONITOR ||
        ic->ic_state != IEEE80211_S_RUN || !ic->ic_bss ||
        !(ic->ic_ac.ac_if.if_flags & IFF_RUNNING)) return kIOReturnNotReady;
    auto channel = &ic->ic_channels[awdl.channel];
    uint64_t now = awdlNowUs();
    if (!native && !scheduled && (channel->ic_flags & IEEE80211_CHAN_PASSIVE) && awdl.mccQueries < 2 &&
        (!awdl.lastMccQueryUs || now - awdl.lastMccQueryUs >= 1000000) &&
        (isset(com.sc_ucode_api, IWX_UCODE_TLV_API_WIFI_MCC_UPDATE) ||
         isset(com.sc_enabled_capa, IWX_UCODE_TLV_CAPA_LAR_MULTI_MCC))) {
        // GET_CURRENT queries the firmware's own regulatory decision. No
        // supplied/forced country and no clearing of passive/DFS flags.
        awdl.lastMccQueryUs = now;
        ++awdl.mccQueries;
        int generation = com.sc_generation;
        const uint32_t epoch = awdl.epoch;
        awdl.commandBusy = true;
        awdl.mccError = iwx_send_update_mcc_cmd(&com, "ZZ");
        awdl.commandBusy = false;
        // The synchronous firmware call can release the gate while waiting.
        if (generation != com.sc_generation || epoch != awdl.epoch || awdl.session != ItlAwdlListening ||
            awdl.owner != owner) return kIOReturnAborted;
        if (awdl.mccError) return kIOReturnIOError;
        now = awdlNowUs();
    }
    if (!isset(ic->ic_chan_active, awdl.channel) || !channel->ic_freq ||
        (channel->ic_flags & (IEEE80211_CHAN_PASSIVE | IEEE80211_CHAN_DFS)))
        return kIOReturnNotPermitted;
    // At most eight fixed diagnostic frames per session, one outstanding,
    // only while the selected peer was heard on this channel within 500 ms.
    if (!awdl.lastPeerRxUs || now - awdl.lastPeerRxUs > 500000)
        return kIOReturnNotReady;
    if (awdl.tx.submitted >= (native ? 4U : 8U)) return kIOReturnNoSpace;
    if (awdl.tx.pending || (awdl.lastProbeUs && now - awdl.lastProbeUs < 100000))
        return kIOReturnBusy;
    return kIOReturnSuccess;
}

IOReturn ItlIwx::awdlProbe(OSObject *owner, bool scheduled) {
    auto result = awdlTransmitGuard(owner, false, scheduled);
    if (result) return result;
    uint8_t bytes[ItlAwdl::ProbeLength];
    if (!ItlAwdl::buildProbe(bytes, sizeof(bytes), com.sc_ic.ic_myaddr, awdl.peer,
                            uint16_t(awdl.tx.submitted), uint32_t(awdlNowUs()))) return kIOReturnBadArgument;
    return awdlSubmitDiagnostic(bytes, sizeof(bytes));
}

IOReturn ItlIwx::awdlDataProbe(OSObject *owner, const ItlAwdlDataProbeRequest *request) {
    if (!request || request->version != ITL_AWDL_ABI_VERSION || request->reserved ||
        !request->token || !ItlAwdl::echoLength(request->length)) return kIOReturnBadArgument;
    auto result = awdlTransmitGuard(owner, true, false);
    if (result) return result;
    uint8_t bytes[40 + ItlAwdl::DataMtu];
    auto source = com.sc_ic.ic_myaddr;
    const uint16_t sequence = uint16_t(awdl.tx.submitted);
    if (!ItlAwdl::buildEchoIpv6(bytes + 40, ItlAwdl::DataMtu, source, awdl.peer,
                               request->length, request->token, sequence)) return kIOReturnBadArgument;
    // encodeData's payload source is exactly its destination, not a partially
    // overlapping buffer. This keeps the kernel stack bounded to one MPDU.
    auto length = ItlAwdl::encodeData(bytes, sizeof(bytes), source, awdl.peer,
                                     sequence, sequence, bytes + 40, request->length);
    if (!length) return kIOReturnBadArgument;
    return awdlSubmitDiagnostic(bytes, length);
}

IOReturn ItlIwx::awdlSubmitDiagnostic(const uint8_t *bytes, size_t length) {
    auto ic = &com.sc_ic;
    mbuf_t packet = nullptr;
    unsigned chunks = 1;
    int error = mbuf_allocpacket(MBUF_DONTWAIT, length, &chunks, &packet);
    if (error || !packet) { awdl.tx.last_host_error = error ? error : ENOMEM; return kIOReturnNoMemory; }
    error = mbuf_copyback(packet, 0, length, bytes, MBUF_DONTWAIT);
    if (error) { mbuf_freem(packet); awdl.tx.last_host_error = error; return kIOReturnNoMemory; }
    mbuf_setlen(packet, length);
    mbuf_pkthdr_setlen(packet, length);
    auto node = ieee80211_ref_node(ic->ic_bss);
    // iwx_tx consumes the mbuf on success AND failure; the node reference
    // transfers only on success and is then released by TX completion/stop.
    error = iwx_tx(&com, packet, node, EDCA_AC_BE);
    if (error) {
        ieee80211_release_node(ic, node);
        awdl.tx.last_host_error = error;
        return kIOReturnIOError;
    }
    ++awdl.tx.submitted;
    ++awdl.tx.pending;
    awdl.lastProbeUs = awdlNowUs();
    return kIOReturnSuccess;
}

IOReturn ItlIwx::awdlSubmitNativeSync(OSObject *owner, const uint8_t *bytes, size_t length, uint32_t gp2,
    bool unicast) {
    auto result = awdlTransmitGuard(owner, false, false, true);
    if (result) return result;
    ItlAwdl::ActionInfo action = {};
    ItlAwdl::SyncAnnouncement sync = {};
    if (!bytes || length > ItlAwdl::SyncActionMaxLength ||
        ItlAwdl::parseAction(bytes,length,&action) != ItlAwdl::ParseResult::Valid ||
        ItlAwdl::parseSchedule(bytes,length,&sync) != ItlAwdl::ScheduleResult::Valid ||
        bytes[30] != (unicast ? 3 : 0) || !ItlAwdl::equalMac(bytes+10,awdlNativeAddress) ||
        !ItlAwdl::equalMac(bytes+16,ItlAwdl::DefaultAwdlBssid) || action.phy_tx != gp2)
        return kIOReturnBadArgument;
    if (!ItlAwdl::syncQualificationDestination(bytes+4,awdl.peer,unicast)) return kIOReturnBadArgument;
    ItlAwdlClockResponse clock = {};
    result = awdlReadClock(&clock);
    const auto age = ItlAwdl::clockDelta(clock.gp2_us,gp2);
    if (result || (clock.flags&7)!=7 || age<0 || age>2000) return kIOReturnNotReady;
    // Wait before giving a packet to firmware when a decoded reservation is
    // still active. The caller will encode a fresh timestamp on its next try.
    // This never disables or overrides the NIC's own carrier sensing.
    if (awdlObservedReservation.remaining(clock.gp2_us,clock.host_end_us))
        return kIOReturnNotReady;
    ItlAwdl::PeerTxWindow peerWindow = {};
    const int64_t localRemaining = int64_t(sync.down_counter_tu)*ItlAwdl::TuUs -
        sync.tx_delay_us - age;
    if (localRemaining <= 12000 || ItlAwdl::qualifyPeerWindow(awdl.schedule,
        clock.gp2_us, clock.host_end_us, uint8_t(awdl.channel), true, &peerWindow) !=
        ItlAwdl::PeerWindowResult::Ready) return kIOReturnNotReady;
    // There are no sleeping firmware queries between this freshness check and
    // submission. Completion samples a conservative delay bound, never an
    // invented on-air timestamp. The monitor uses the native source MAC/rate.
    awdlNativeTxGp2 = gp2;
    const auto index = awdl.tx.submitted;
    if (index >= nitems(awdlNativeTrace)) return kIOReturnNoSpace;
    auto &trace = awdlNativeTrace[index];
    bzero(&trace,sizeof(trace));
    trace.frameGp2 = gp2; trace.freshGp2 = clock.gp2_us;
    trace.packetLength = uint32_t(length);
    trace.unicast = unicast;
    trace.freshHostUs = clock.host_end_us;
    trace.peerWindowElapsedUs = peerWindow.elapsedUs;
    trace.peerWindowRemainingUs = peerWindow.remainingUs;
    trace.peerWindowCounter = peerWindow.counter;
    trace.peerWindowChannel = peerWindow.channel;
    trace.localWindowRemainingUs = uint32_t(localRemaining);
    awdlNativeTraceIndex = index; awdlNativeTraceCount = index + 1;
    __atomic_store_n(&awdlNativeFilterAbsolute, uint64_t(0), __ATOMIC_RELAXED);
    __atomic_store_n(&awdlNativeTraceArmed, uint32_t(1), __ATOMIC_RELEASE);
    result = awdlSubmitDiagnostic(bytes,length);
    trace.returnHostUs = awdlNowUs();
    trace.hostError = result;
    return result;
}

void ItlIwx::awdlNativeTxDoorbell(uint32_t qid, uint32_t index, uint32_t nextSequence,
    uint32_t offloadAssist, uint32_t headerLength) {
    if (!awdlNativeTxQualification || !awdlNativeRadioOwner ||
        awdlNativeTraceIndex >= awdlNativeTraceCount || awdlNativeTraceIndex >= nitems(awdlNativeTrace)) return;
    auto &trace = awdlNativeTrace[awdlNativeTraceIndex];
    trace.qid = qid; trace.index = index;
    trace.nextSequence = nextSequence&0xfffU;
    trace.commandOffloadAssist = offloadAssist;
    trace.commandHeaderLength = headerLength;
    // Host clock immediately before the existing MMIO write, not an on-air
    // timestamp. Do not add another peripheral read/lock to the hot TX path.
    trace.doorbellHostUs = awdlNowUs();
}

uint64_t ItlIwx::awdlObserveNativeTxDma() {
    const auto now = awdlNowUs();
#ifdef AIRPORT_VTD
    static_assert(offsetof(iwx_rx_packet,data)==8 && sizeof(iwx_tx_resp)==44,
        "read-only completion probe transport layout");
    static_assert(offsetof(iwx_tx_resp,wireless_media_time)==8 &&
        offsetof(iwx_tx_resp,byte_cnt)==30 && offsetof(iwx_tx_resp,tx_queue)==36 &&
        offsetof(iwx_tx_resp,status)==40, "TX response v6 fields");
    if (!awdlNativeRadioOwner || !awdlNativeTxQualification || !awdl.tx.pending ||
        awdlNativeTraceIndex>=awdlNativeTraceCount || awdlNativeTraceIndex>=nitems(awdlNativeTrace) ||
        !getMainWorkLoop()->inGate() || !com.rxq.stat) return now;
    auto &trace=awdlNativeTrace[awdlNativeTraceIndex];
    if (!trace.doorbellHostUs || trace.dmaReplyHostUs) return now;
    // CSR reads do not require grabbing NIC access and this status register
    // has no interrupt-acknowledgement side effect. Merely sample readiness;
    // do not force the device awake or change its power policy.
    const auto mac = IWX_READ(&com, IWX_CSR_GP_CNTRL);
    ++trace.macReadSamples;
    if (mac == 0xffffffffU || (mac & 0xfffffff0U) == 0xa5a5a5a0U)
        ++trace.macInvalidSamples;
    else {
        if (mac & IWX_CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY) ++trace.macClockReadySamples;
        if (mac & IWX_CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP) ++trace.macSleepingSamples;
    }
    trace.dmaProbeSupported=true;
    ++trace.dmaProbeSamples;
    // The published close index transfers these buffers to the host. Normal
    // RX cannot reclaim them concurrently because both paths use this gate.
    // Only inspect completed DMA memory: no NIC access, ring advancement,
    // command completion, copying into live mbufs or interrupt acknowledgement.
    OSSynchronizeIO();
    const uint16_t hw=le16toh(*static_cast<volatile uint16_t *>(com.rxq.stat))&
        (IWX_RX_MQ_RING_COUNT-1);
    const uint32_t pending=(hw+IWX_RX_MQ_RING_COUNT-com.rxq.cur)&(IWX_RX_MQ_RING_COUNT-1);
    if (pending>trace.dmaPendingMax) trace.dmaPendingMax=pending;
    if (pending>16) ++trace.dmaScanTruncated;
    for (uint32_t i=0;i<pending && i<16;++i) {
        const auto index=(com.rxq.cur+i)&(IWX_RX_MQ_RING_COUNT-1);
        auto map=com.rxq.data[index].map;
        if (!map || !map->dma.arena || !map->dma.vaddr || map->dma.size<IWX_RBUF_SIZE) continue;
        ItlAwdl::TxCompletionView reply={};
        if (!ItlAwdl::findTxCompletion(static_cast<const uint8_t *>(map->dma.vaddr),IWX_RBUF_SIZE,
                uint16_t(trace.qid),uint16_t(trace.nextSequence),uint16_t(trace.packetLength),&reply)) continue;
        trace.dmaReplyHostUs=awdlNowUs();
        trace.dmaStatus=reply.status; trace.dmaMediaUs=reply.mediaUs;
        break;
    }
#endif
    return now;
}

void ItlIwx::awdlPublishNativeTxTiming(IORegistryEntry *owner) {
    if (!owner) return;
    auto records = OSArray::withCapacity(nitems(awdlNativeTrace));
    if (!records) return;
    for (uint32_t i=0;i<awdlNativeTraceCount && i<nitems(awdlNativeTrace);++i) {
        const auto &t = awdlNativeTrace[i];
        auto entry = OSDictionary::withCapacity(20);
        if (!entry) break;
        auto put = [&](const char *key, uint64_t value) {
            auto number = OSNumber::withNumber(value,64);
            if (number) { entry->setObject(key,number); number->release(); }
        };
        put("frame",i); put("completed",t.completed); put("clock_valid",t.clockValid);
        put("unicast",t.unicast);
        put("mac_status_samples",t.macReadSamples);
        put("mac_clock_ready_samples",t.macClockReadySamples);
        put("mac_going_to_sleep_samples",t.macSleepingSamples);
        put("mac_invalid_samples",t.macInvalidSamples);
        put("pending_clock_reads",t.pendingClockReads);
        put("pending_clock_max_read_us",t.pendingClockMaxReadUs);
        if (t.firstPendingClockHostUs>=t.doorbellHostUs && t.doorbellHostUs) {
            put("doorbell_to_first_pending_clock_host_us",t.firstPendingClockHostUs-t.doorbellHostUs);
            put("doorbell_to_last_pending_clock_host_us",t.lastPendingClockHostUs-t.doorbellHostUs);
        }
        put("rx_before_ack_delivery_frames",t.beforeAckRx.frames);
        put("rx_before_ack_delivery_bytes",t.beforeAckRx.bytes);
        put("rx_before_ack_delivery_management",t.beforeAckRx.management);
        put("rx_before_ack_delivery_control",t.beforeAckRx.control);
        put("rx_before_ack_delivery_data",t.beforeAckRx.data);
        put("rx_before_ack_delivery_max_nav_us",t.beforeAckRx.maxNavUs);
        if (t.beforeAckRx.frames) {
            put("rx_before_ack_delivery_first_from_frame_gp2_us",t.beforeAckRx.firstOffsetUs);
            put("rx_before_ack_delivery_last_from_frame_gp2_us",t.beforeAckRx.lastOffsetUs);
        }
        put("ack_samples",t.ackCount);
        if (t.ackCount) {
            put("first_ack_from_frame_gp2_us",uint32_t(ItlAwdl::clockDelta(t.firstAckGp2,t.frameGp2)));
            put("last_ack_from_frame_gp2_us",uint32_t(ItlAwdl::clockDelta(t.lastAckGp2,t.frameGp2)));
            if (t.firstAckHostUs>=t.doorbellHostUs && t.doorbellHostUs)
                put("first_ack_delivered_after_doorbell_host_us",t.firstAckHostUs-t.doorbellHostUs);
            const auto afterAck = ItlAwdl::clockDelta(t.completeGp2,t.firstAckGp2);
            if (t.completed && t.clockValid && afterAck>=0)
                put("first_ack_to_completion_gp2_us",uint32_t(afterAck));
        }
        put("qid",t.qid); put("index",t.index);
        put("command_offload_assist",t.commandOffloadAssist);
        put("command_header_length",t.commandHeaderLength);
        put("host_error",t.hostError); put("firmware_status",t.firmwareStatus);
        put("firmware_media_us",t.mediaUs); put("bluetooth_kills",t.btKill);
        put("rts_failures",t.rtsFailures); put("frame_failures",t.frameFailures);
        put("peer_window_elapsed_us",t.peerWindowElapsedUs);
        put("peer_window_remaining_us",t.peerWindowRemainingUs);
        put("peer_window_counter",t.peerWindowCounter);
        put("peer_window_channel",t.peerWindowChannel);
        put("local_window_remaining_us",t.localWindowRemainingUs);
        put("dma_probe_supported",t.dmaProbeSupported); put("dma_probe_samples",t.dmaProbeSamples);
        put("dma_pending_max",t.dmaPendingMax); put("dma_scan_truncated",t.dmaScanTruncated);
        put("dma_reply_seen",t.dmaReplyHostUs!=0);
        if (t.dmaReplyHostUs>=t.doorbellHostUs && t.doorbellHostUs) {
            put("doorbell_to_dma_reply_host_us",t.dmaReplyHostUs-t.doorbellHostUs);
            put("dma_reply_status",t.dmaStatus); put("dma_reply_media_us",t.dmaMediaUs);
        }
        const auto age = ItlAwdl::clockDelta(t.freshGp2,t.frameGp2);
        if (age>=0 && age<=2000) put("frame_age_at_fresh_read_us",uint64_t(age));
        if (t.doorbellHostUs>=t.freshHostUs && t.freshHostUs)
            put("fresh_to_doorbell_host_us",t.doorbellHostUs-t.freshHostUs);
        if (t.returnHostUs>=t.doorbellHostUs && t.doorbellHostUs)
            put("doorbell_to_return_host_us",t.returnHostUs-t.doorbellHostUs);
        if (t.completed && t.clockValid) {
            const auto elapsed = ItlAwdl::clockDelta(t.completeGp2,t.freshGp2);
            if (elapsed>=0) put("fresh_to_complete_gp2_us",uint64_t(elapsed));
            if (t.completeHostUs>=t.freshHostUs && t.freshHostUs)
                put("fresh_to_complete_host_us",t.completeHostUs-t.freshHostUs);
            if (t.completeHostUs>=t.doorbellHostUs && t.doorbellHostUs)
                put("doorbell_to_complete_host_us",t.completeHostUs-t.doorbellHostUs);
            if (t.completeHostUs>=t.batchHostUs && t.batchHostUs)
                put("batch_to_complete_host_us",t.completeHostUs-t.batchHostUs);
            uint64_t filterNs = 0;
            absolutetime_to_nanoseconds(t.filterAbsolute,&filterNs);
            const auto filterUs = filterNs/1000;
            if (filterUs && filterUs>=t.doorbellHostUs && t.doorbellHostUs && t.batchHostUs>=filterUs) {
                put("doorbell_to_batch_filter_host_us",filterUs-t.doorbellHostUs);
                put("batch_filter_to_handler_host_us",t.batchHostUs-filterUs);
            }
        }
        records->setObject(entry); entry->release();
    }
    // Only aggregate timing/counters, no packet bytes, identities or pointers.
    // Published after the radio is stopped, outside the measured interval.
    owner->setProperty("AX201NativeTxTiming",records);
    records->release();
}

void ItlIwx::awdlPublishNativePeerProfile(IORegistryEntry *owner) {
    if (!owner || !awdlNativePeerProfileSamples) return;
    const auto &p = awdlNativePeerProfile;
    auto entry = OSDictionary::withCapacity(25);
    if (!entry) return;
    auto put = [&](const char *key, uint32_t value) {
        auto number = OSNumber::withNumber(value,32);
        if (number) { entry->setObject(key,number); number->release(); }
    };
    put("samples",awdlNativePeerProfileSamples);
    put("last_version",p.version); put("last_subtype",p.subtype);
    put("next_channel",p.nextChannel); put("master_channel",p.masterChannel);
    put("guard",p.guard); put("presence",p.presence); put("flags",p.flags);
    put("period_tu",p.periodTu); put("action_period_tu",p.actionPeriodTu);
    put("extension_tu",p.extensionTu); put("common_tu",p.commonTu);
    put("remaining_tu",p.remainingTu); put("down_counter_tu",p.downCounterTu);
    put("counter",p.counter); put("ap_alignment",p.alignment);
    put("min_extension",p.extensions[0]); put("multicast_extension",p.extensions[1]);
    put("unicast_extension",p.extensions[2]); put("action_extension",p.extensions[3]);
    put("separate_channel_plan",p.separatePlan);
    put("plan_encoding",p.plan.encoding); put("plan_step_aw",p.plan.step_aw);
    auto plan = OSArray::withCapacity(ItlAwdl::ScheduleLength);
    if (plan) {
        for (unsigned i = 0; i < ItlAwdl::ScheduleLength; ++i) {
            // Low byte is primary channel, high byte is operating class.
            auto value = OSNumber::withNumber(uint32_t(p.plan.channels[i]) |
                uint32_t(p.plan.operating_classes[i]) << 8,32);
            if (value) { plan->setObject(value); value->release(); }
        }
        entry->setObject("channel_opclass",plan); plan->release();
    }
    owner->setProperty("AX201NativePeerProfile",entry); entry->release();
}

int ItlIwx::awdlStartNativeP2pSession() {
    static_assert(ItlAwdl::P2pSessionProtection::MacId == 1 &&
        ItlAwdl::P2pSessionProtection::Discovery == IWX_SESSION_PROTECT_CONF_P2P_DEVICE_DISCOV &&
        ItlAwdl::P2pSessionProtection::ManagementTx == IWX_SESSION_PROTECT_CONF_P2P_GO_NEGOTIATION,
        "P2P notification identity matches the firmware command");
    auto in = reinterpret_cast<iwx_node *>(com.sc_ic.ic_bss);
    if (!awdlNativeRadioOwner || !awdlNativeP2pQualification ||
        !in || in->in_id != ItlAwdl::P2pSessionProtection::MacId || in->in_color ||
        com.sc_ic.ic_opmode != IEEE80211_M_MONITOR || !awdlActive()) return EINVAL;
    awdlNativeP2pCommandVersion = iwx_lookup_cmd_ver(&com,IWX_MAC_CONF_GROUP,IWX_SESSION_PROTECTION_CMD);
    awdlNativeP2pNotifVersion = iwx_lookup_notif_ver(&com,IWX_MAC_CONF_GROUP,IWX_SESSION_PROTECTION_NOTIF);
    if (!isset(com.sc_enabled_capa,IWX_UCODE_TLV_CAPA_SESSION_PROT_CMD) ||
        awdlNativeP2pCommandVersion != 1 || awdlNativeP2pNotifVersion != 2) return ENOTSUP;
    // Firmware can autonomously ACK on a P2P MAC even though this trial has
    // no host TX. Require its current active, non-passive, non-DFS channel.
    const auto ic = &com.sc_ic;
    if (!awdl.channel || awdl.channel > IEEE80211_CHAN_MAX ||
        !isset(ic->ic_chan_active,awdl.channel) || !ic->ic_channels[awdl.channel].ic_freq ||
        (ic->ic_channels[awdl.channel].ic_flags & (IEEE80211_CHAN_PASSIVE | IEEE80211_CHAN_DFS)))
        return EPERM;
    const auto now=awdlNowUs();
    // Intel maps a management-TX ROC to GO_NEGOTIATION protection, while a
    // normal discovery ROC uses DEVICE_DISCOV. Keep the no-host-TX selector
    // on its qualified configuration; do not accept caller-chosen profiles.
    const auto config=awdlNativeTxQualification ? ItlAwdl::P2pSessionProtection::ManagementTx :
        ItlAwdl::P2pSessionProtection::Discovery;
    if (now>=awdl.deadlineUs || !awdlNativeP2pSession.arm(com.sc_generation,awdl.deadlineUs-now,config))
        return ETIMEDOUT;
    static_assert(sizeof(iwx_session_prot_cmd)==24 && sizeof(iwx_session_prot_notif)==16,
        "P2P session-protection command v1 / notification v2");
    iwx_session_prot_cmd command = {};
    command.id_and_color=htole32(IWX_FW_CMD_ID_AND_COLOR(in->in_id,in->in_color));
    command.action=htole32(IWX_FW_CTXT_ACTION_ADD);
    command.conf_id=htole32(awdlNativeP2pSession.configuration);
    command.duration_tu=htole32(awdlNativeP2pSession.durationTu);
    const auto generation=com.sc_generation;
    const auto error=iwx_send_cmd_pdu(&com,iwx_cmd_id(IWX_SESSION_PROTECTION_CMD,IWX_MAC_CONF_GROUP,0),
        0,sizeof(command),&command);
    if (error) awdlNativeP2pSession.fail();
    if (generation!=com.sc_generation || !awdlActive()) return ECANCELED;
    return error;
}

void ItlIwx::awdlPublishNativeP2pSession(IORegistryEntry *owner) {
    if (!owner || !awdlNativeP2pQualification) return;
    auto d=OSDictionary::withCapacity(12);
    if (!d) return;
    auto put=[&](const char *key,uint32_t value) {
        auto n=OSNumber::withNumber(value,32);
        if (n) { d->setObject(key,n); n->release(); }
    };
    const auto &s=awdlNativeP2pSession;
    put("configuration",s.configuration);
    put("command_version",awdlNativeP2pCommandVersion); put("notification_version",awdlNativeP2pNotifVersion);
    put("duration_tu",s.durationTu); put("start_notifications",s.starts); put("end_notifications",s.ends);
    put("ignored_notifications",s.ignored); put("malformed_notifications",s.malformed);
    put("failed",s.failed); put("active_after_stop",s.active); put("requested_after_stop",s.requested);
    put("host_tx_submitted",awdl.tx.submitted); put("generation",s.generation);
    owner->setProperty("AX201NativeP2pRadio",d); d->release();
}

IOReturn ItlIwx::awdlReadClock(ItlAwdlClockResponse *out) {
    bzero(out, sizeof(*out));
    out->version = ITL_AWDL_ABI_VERSION;
    if (awdl.linked) return kIOReturnUnsupported;
    if (awdl.session != ItlAwdlListening || com.sc_ic.ic_opmode != IEEE80211_M_MONITOR ||
        com.sc_ic.ic_state != IEEE80211_S_RUN) return kIOReturnNotReady;
    // Intel Linux v6.12 cfg/ax210.c IWL_DEVICE_AX210.gp2_reg_addr and
    // mvm/utils.c iwl_mvm_get_systime(). This AX201 radio uses a So MAC
    // (iwl_cfg_so_a0_hr_a0), not an older Qu/22000 MAC. The marketing radio
    // name is insufficient to choose the peripheral register address.
    // NOT CSR_UCODE_DRV_GP2 (0x060). Do not add the UMAC offset twice.
    if (pci.pa_tag->configRead16(kIOPCIConfigDeviceID) != 0x7af0 ||
        com.sc_device_family != IWX_DEVICE_FAMILY_AX210) return kIOReturnUnsupported;
    out->register_address = 0x00d02c68;
    out->host_begin_us = awdlNowUs();
    if (!iwx_nic_lock(&com)) return kIOReturnIOError;
    out->gp2_us = iwx_read_prph(&com, out->register_address);
    iwx_nic_unlock(&com);
    out->host_end_us = awdlNowUs();
    if (awdlNativeRadioOwner && awdlNativeTxQualification && awdl.tx.pending &&
        awdlNativeTraceIndex<awdlNativeTraceCount && awdlNativeTraceIndex<nitems(awdlNativeTrace)) {
        auto &trace=awdlNativeTrace[awdlNativeTraceIndex];
        if (trace.doorbellHostUs && out->host_begin_us>=trace.doorbellHostUs) {
            if (!trace.pendingClockReads) trace.firstPendingClockHostUs=out->host_begin_us;
            trace.lastPendingClockHostUs=out->host_begin_us;
            ++trace.pendingClockReads;
            const auto duration=out->host_end_us-out->host_begin_us;
            if (duration>trace.pendingClockMaxReadUs)
                trace.pendingClockMaxReadUs=uint32_t(duration>UINT32_MAX ? UINT32_MAX : duration);
        }
    }
    out->rx_gp2_us = awdl.lastPeerGp2;
    out->rx_host_us = awdl.lastPeerRxUs;
    out->phy_command_version = iwx_lookup_cmd_ver(&com, IWX_LONG_GROUP, IWX_PHY_CONTEXT_CMD);
    // Monitor RUN already disables both device and MAC power saving.
    out->flags = awdl.clock.observe(out->gp2_us, out->host_begin_us, out->host_end_us,
        bool(awdl.lastPeerRxUs), awdl.lastPeerGp2, awdl.lastPeerRxUs);
    return kIOReturnSuccess;
}

IOReturn ItlIwx::awdlRun(OSObject *owner, const ItlAwdlRunRequest *request) {
    if (awdlNativeRadioOwner) return kIOReturnUnsupported;
    if (!request || request->version != ITL_AWDL_ABI_VERSION || request->reserved[0] ||
        request->reserved[1] || request->mode < ItlAwdlTestWake || request->mode > ItlAwdlTestHop)
        return kIOReturnBadArgument;
    if (!awdl.owner || awdl.owner != owner) return kIOReturnNotPermitted;
    if (awdl.linked || awdl.dataStatus.enabled) return kIOReturnUnsupported;
    if (awdl.session != ItlAwdlListening) return kIOReturnNotReady;
    if (awdl.run.mode || awdl.commandBusy || awdl.tx.pending || awdl.tx.submitted) return kIOReturnBusy;
    if (request->mode == ItlAwdlTestProbe &&
        (awdl.mccNeedsRefresh || (com.sc_ic.ic_channels[awdl.channel].ic_flags & (IEEE80211_CHAN_PASSIVE | IEEE80211_CHAN_DFS)))) {
        // Wait for a fresh firmware notification rather than supplying an
        // arbitrary host country or clearing passive/DFS channel flags.
        auto refreshed = awdlRefreshCountry(owner);
        if (refreshed) return refreshed;
        if (com.sc_ic.ic_channels[awdl.channel].ic_flags & (IEEE80211_CHAN_PASSIVE | IEEE80211_CHAN_DFS))
            return kIOReturnNotPermitted;
    }
    if (!awdl.runTimer) {
        awdl.runTimer = IOTimerEventSource::timerEventSource(this, awdlRunTimeout);
        if (!awdl.runTimer) return kIOReturnNoMemory;
        if (getMainWorkLoop()->addEventSource(awdl.runTimer) != kIOReturnSuccess) {
            OSSafeReleaseNULL(awdl.runTimer); return kIOReturnNoResources;
        }
    }
    awdl.run.mode = request->mode; awdl.run.active = 1;
    auto result = awdl.runTimer->setTimeoutMS(2);
    if (result) { awdl.run.active = 0; awdl.run.last_error = result; }
    return result;
}

void ItlIwx::awdlCountryHint(uint16_t country, uint8_t source) {
    if (auto controller = getController()) {
        controller->setProperty("ItlAwdlFirmwareCountryHint", country, 32);
        controller->setProperty("ItlAwdlFirmwareCountrySource", source, 32);
    }
    if (!awdl.ready) return;
    awdl.mccNeedsRefresh = country != awdl.appliedCountryHint || source != awdl.appliedCountrySource ||
                          !ItlAwdl::countryHintAllowed(country, source);
    if (awdl.run.active && awdl.run.mode == ItlAwdlTestProbe && awdl.mccNeedsRefresh) {
        // A changed regulatory hint invalidates an armed TX run. A later
        // session must obtain the new profile; no stale-map transmissions.
        awdl.run.active = 0; awdl.run.last_error = kIOReturnNotReady;
        if (awdl.pendingWindow) ++awdl.run.cancelled;
        awdl.pendingWindow = false;
        if (awdl.runTimer) awdl.runTimer->cancelTimeout();
    }
    if (!ItlAwdl::countryHintAllowed(country, source)) {
        awdl.countryHintUs = 0; return;
    }
    awdl.countryHint = country; awdl.countrySource = source;
    awdl.countryHintUs = awdlNowUs();
    awdl.countryHintGeneration = com.sc_generation;
}

IOReturn ItlIwx::awdlRefreshCountry(OSObject *owner, bool station) {
    static_assert(IWX_MCC_SOURCE_WIFI == 5, "Intel firmware Wi-Fi MCC source");
    if (!owner || (!station && owner != awdl.owner)) return kIOReturnNotPermitted;
    auto stationReady = [&]() {
        return awdl.ready && awdl.session == ItlAwdlIdle && !awdl.owner &&
            com.sc_ic.ic_opmode == IEEE80211_M_STA && com.sc_ic.ic_state <= IEEE80211_S_SCAN &&
            (com.sc_ic.ic_ac.ac_if.if_flags & IFF_RUNNING) &&
            !(com.sc_flags & (IWX_FLAG_SHUTDOWN | IWX_FLAG_RFKILL));
    };
    if (awdl.commandBusy) return kIOReturnBusy;
    if (station ? !stationReady() :
        awdl.session != ItlAwdlListening || com.sc_ic.ic_opmode != IEEE80211_M_MONITOR) return kIOReturnNotReady;
    // The standalone comparison must consume a notification from this exact
    // firmware boot, without switching mode, joining a network or scanning.
    if (station && awdl.countryHintGeneration != com.sc_generation) return kIOReturnNotReady;
    uint64_t now = awdlNowUs();
    if (!ItlAwdl::countryHintFresh(now, awdl.countryHintUs) ||
        !ItlAwdl::countryHintAllowed(awdl.countryHint, awdl.countrySource)) return kIOReturnNotReady;
    if (awdl.mccQueries >= 2) return kIOReturnNotPermitted;
    if (awdl.lastMccQueryUs && now - awdl.lastMccQueryUs < 1000000) return kIOReturnNotReady;
    if (!isset(com.sc_ucode_api, IWX_UCODE_TLV_API_WIFI_MCC_UPDATE) &&
        !isset(com.sc_enabled_capa, IWX_UCODE_TLV_CAPA_LAR_MULTI_MCC)) return kIOReturnUnsupported;
    // Mirror Intel mvm/nvm.c's notification handshake using only the exact
    // country and source recently received in this driver instance. It may
    // have arrived before the radio restart (Intel init_mcc also replays it).
    iwx_mcc_update_cmd request = {};
    request.mcc = htole16(awdl.countryHint);
    request.source_id = awdl.countrySource;
    iwx_host_cmd command = {};
    command.id = IWX_MCC_UPDATE_CMD; command.flags = IWX_CMD_WANT_RESP;
    command.data[0] = &request; command.len[0] = sizeof(request);
    command.resp_pkt_len = IWX_CMD_RESP_MAX;
    const int generation = com.sc_generation;
    const uint32_t epoch = awdl.epoch;
    ++awdl.mccQueries; awdl.lastMccQueryUs = now;
    awdl.commandBusy = true;
    int error = iwx_send_cmd(&com, &command);
    bool current = generation == com.sc_generation && epoch == awdl.epoch &&
                   (station ? stationReady() : awdl.session == ItlAwdlListening && awdl.owner == owner);
    if (!error) {
        auto packet = command.resp_pkt;
        if (!current || !packet || (packet->hdr.group_id & IWX_CMD_FAILED_MSK)) error = EIO;
        else {
            size_t length = iwx_rx_packet_payload_len(packet);
            auto response = reinterpret_cast<iwx_mcc_update_resp *>(packet->data);
            if (auto controller = getController()) {
                controller->setProperty("ItlAwdlMccResponseLength", uint64_t(length), 32);
                controller->setProperty("ItlAwdlMccResponseVersion",
                    iwx_lookup_notif_ver(&com, IWX_LONG_GROUP, IWX_MCC_UPDATE_CMD), 32);
                if (length >= sizeof(*response)) {
                    controller->setProperty("ItlAwdlMccResponseStatus", le32toh(response->status), 32);
                    controller->setProperty("ItlAwdlMccResponseChannels", le32toh(response->n_channels), 32);
                    controller->setProperty("ItlAwdlMccResponseCountry", le16toh(response->mcc), 32);
                    controller->setProperty("ItlAwdlMccResponseSource", response->source_id, 32);
                }
            }
            if (length < sizeof(*response)) error = EINVAL;
            else {
                uint32_t count = le32toh(response->n_channels);
                uint32_t status = le32toh(response->status);
                if (!count || count > IWX_NUM_CHANNELS ||
                    length != sizeof(*response) + count * sizeof(response->channels[0])) error = EINVAL;
                else {
                    uint16_t accepted = le16toh(response->mcc);
                    awdl.mccCountry[0] = char(accepted >> 8); awdl.mccCountry[1] = char(accepted);
                    if (auto controller = getController())
                        controller->setProperty("ItlAwdlMccRawChannels", response->channels,
                            count * sizeof(response->channels[0]));
                    if (status > IWX_MCC_RESP_SAME_CHAN_PROFILE) {
                        // Intel commit 82715ac71e6b: a restart can reject a
                        // cached country and return a restricted world map.
                        // Propagate that restriction, while still denying TX.
                        if (status == IWX_MCC_RESP_ILLEGAL) {
                            iwx_init_channel_map(&com, nullptr, response->channels, count);
                            awdl.mccNeedsRefresh = true;
                        }
                        error = EPERM;
                    }
                }
                if (!error) {
                    // The firmware may return a different country/profile.
                    // Apply its returned flags verbatim through the normal mapper.
                    iwx_init_channel_map(&com, nullptr, response->channels, count);
                    if (auto controller = getController())
                        controller->setProperty("ItlAwdlMccChannel149Flags", com.sc_ic.ic_channels[149].ic_flags, 32);
                    awdl.appliedCountryHint = le16toh(request.mcc);
                    awdl.appliedCountrySource = request.source_id;
                    awdl.mccNeedsRefresh = !awdl.countryHintUs || awdl.countryHint != awdl.appliedCountryHint ||
                                          awdl.countrySource != awdl.appliedCountrySource;
                }
            }
        }
        iwx_free_resp(&com, &command);
    }
    awdl.commandBusy = false;
    if (!current) return kIOReturnAborted;
    awdl.mccError = error;
    return error == EPERM ? kIOReturnNotPermitted : error ? kIOReturnIOError :
        awdl.mccNeedsRefresh ? kIOReturnNotReady : kIOReturnSuccess;
}

IOReturn ItlIwx::awdlRetune(uint32_t channel) {
    auto ic = &com.sc_ic;
    if (channel != 6 && channel != 44 && channel != 149) return kIOReturnUnsupported;
    if (!isset(ic->ic_chan_active, channel) || !ic->ic_channels[channel].ic_freq)
        return kIOReturnNotPermitted;
    if (awdl.tx.pending || !ic->ic_bss || !(com.sc_flags & IWX_FLAG_BINDING_ACTIVE))
        return kIOReturnBusy;
    auto in = reinterpret_cast<iwx_node *>(ieee80211_ref_node(ic->ic_bss));
    const int generation = com.sc_generation;
    const uint32_t epoch = awdl.epoch;
    // Firmware command waits release the gate. Keep the node alive and never
    // resume an old transaction after stop/power change/client death.
    auto current = [&]() {
        return generation == com.sc_generation && epoch == awdl.epoch &&
               awdl.session == ItlAwdlListening;
    };
    int error = iwx_binding_cmd(&com, in, IWX_FW_CTXT_ACTION_REMOVE);
    if (!error && current()) {
        com.sc_flags &= ~IWX_FLAG_BINDING_ACTIVE;
        error = iwx_phy_ctxt_update(&com, in->in_phyctxt, &ic->ic_channels[channel], 1, 1, 0);
    }
    if (!error && current()) {
        ic->ic_ibss_chan = &ic->ic_channels[channel];
        in->in_ni.ni_chan = &ic->ic_channels[channel];
        error = iwx_binding_cmd(&com, in, IWX_FW_CTXT_ACTION_ADD);
        if (!error && current()) com.sc_flags |= IWX_FLAG_BINDING_ACTIVE;
    }
    if (!error && current()) error = iwx_mac_ctxt_cmd(&com, in, IWX_FW_CTXT_ACTION_MODIFY, 1);
    if (!error && current() && !isset(com.sc_enabled_capa, IWX_UCODE_TLV_CAPA_DYNAMIC_QUOTA))
        error = iwx_update_quotas(&com, in, 1);
    bool valid = current();
    ieee80211_release_node(ic, &in->in_ni);
    if (!valid) return kIOReturnAborted;
    if (error) return kIOReturnIOError;
    awdl.channel = channel;
    awdl.lastPeerRxUs = 0; // Never use old-channel reception to authorize TX.
    return kIOReturnSuccess;
}

void ItlIwx::awdlRunTimeout(OSObject *owner, IOTimerEventSource *) {
    static_cast<ItlIwx *>(owner)->awdlRunTick();
}
void ItlIwx::awdlRunTick() {
    auto &s = awdl;
    if (!s.run.active || s.session != ItlAwdlListening || s.commandBusy) return;
    if (awdlNowUs() >= s.deadlineUs) { awdlStop(ItlAwdlExpired); return; }
    const uint32_t limit = s.run.mode == ItlAwdlTestProbe ? 8 : ITL_AWDL_MAX_EVENTS;
    if (s.run.event_count >= limit) { s.run.active = 0; return; }
    auto later = [&](uint32_t delay) {
        if (!s.run.active || s.session != ItlAwdlListening) return;
        auto result = s.runTimer->setTimeoutUS(ItlAwdl::timerDelayUs(delay));
        if (result) { s.run.active = 0; s.run.last_error = result; }
    };
    ItlAwdlClockResponse clock = {};
    auto result = awdlReadClock(&clock);
    ++s.run.clock_reads;
    if (result) { s.run.last_error = result; awdlStop(ItlAwdlRadioError, result); return; }
    if (clock.flags != 7) { later(10000); return; }
    ++s.run.clock_valid;
    const uint32_t now = clock.gp2_us;
    const uint64_t host = clock.host_end_us;
    ItlAwdl::ScheduleSlot slot = {};
    uint32_t target = 0, operation = 0;
    if (s.run.mode == ItlAwdlTestFollow) {
        if (!s.schedule.project(now, now, host, &slot)) { ++s.run.stale_ticks; later(10000); return; }
        // Only the three discovery primary channels tested by this backend.
        bool supported = slot.channel == 6 || slot.channel == 44 || slot.channel == 149;
        if (!supported || slot.channel == s.channel) {
            later(uint32_t(ItlAwdl::clockDelta(slot.end_gp2_us + 2 * ItlAwdl::TuUs, now))); return;
        }
        target = slot.start_gp2_us + 2 * ItlAwdl::TuUs;
        if (ItlAwdl::clockDelta(target, now) > 0) { later(uint32_t(target - now)); return; }
        operation = 3;
    } else {
        if (!s.pendingWindow) {
            if (s.run.mode == ItlAwdlTestHop) {
                s.targetGp2 = now + 250000;
                s.targetSlot = {0, s.targetGp2 + 250000, 0, uint8_t(s.channel == 6 ? 149 : 6), 0};
            } else {
                uint32_t earliest = s.hasLastTarget ? s.lastTargetGp2 + 110000 : now;
                if (!s.schedule.nextWindow(now, host, uint8_t(s.channel), earliest, &s.targetSlot, &s.targetGp2)) {
                    ++s.run.stale_ticks; later(10000); return;
                }
            }
            s.pendingWindow = true;
        }
        target = s.targetGp2; slot = s.targetSlot;
        if (ItlAwdl::clockDelta(target, now) > 0) { later(uint32_t(target - now)); return; }
        s.pendingWindow = false;
        operation = s.run.mode == ItlAwdlTestHop ? 3 : s.run.mode == ItlAwdlTestProbe ? 2 : 1;
    }
    ItlAwdlRunEvent event = {};
    event.target_gp2_us = target; event.wake_gp2_us = event.finish_gp2_us = now;
    event.lateness_us = int32_t(ItlAwdl::clockDelta(now, target));
    event.channel = slot.channel; event.counter = slot.counter; event.operation = operation;
    bool missed = s.run.mode == ItlAwdlTestFollow ?
        ItlAwdl::clockDelta(slot.end_gp2_us, now) < 16 * ItlAwdl::TuUs : event.lateness_us > 2 * ItlAwdl::TuUs;
    if (missed) { event.result = kIOReturnTimeout; ++s.run.missed; }
    else if (operation != 3) {
        ItlAwdl::ScheduleSlot current = {};
        if (!s.schedule.project(now, now, host, &current) || current.channel != s.channel || current.counter != slot.counter) {
            event.result = kIOReturnNotReady; ++s.run.cancelled;
        }
    }
    const uint32_t epoch = s.epoch;
    if (!event.result) {
        s.commandBusy = true;
        if (operation == 3) event.result = awdlRetune(slot.channel);
        else if (operation == 2) event.result = awdlProbe(s.owner, true);
        s.commandBusy = false;
        if (epoch != s.epoch || s.session != ItlAwdlListening) return;
        ItlAwdlClockResponse finish = {};
        auto read = awdlReadClock(&finish);
        if (!read && (finish.flags & 1)) {
            event.finish_gp2_us = finish.gp2_us;
            int64_t duration = ItlAwdl::clockDelta(finish.gp2_us, now);
            if (duration >= 0 && duration <= 1000000) event.duration_us = uint32_t(duration);
            else if (!event.result) event.result = kIOReturnIOError;
        } else if (!event.result) event.result = read ? read : kIOReturnIOError;
        if (!event.result && operation == 3) ++s.run.retunes;
        if (!event.result && operation == 2) ++s.run.probes;
    }
    s.run.events[s.run.event_count++] = event;
    s.hasLastTarget = true; s.lastTargetGp2 = event.wake_gp2_us;
    if (operation == 3 && event.result && !missed) {
        s.run.last_error = event.result; awdlStop(ItlAwdlRadioError, event.result); return;
    }
    if (s.run.event_count >= limit) { s.run.active = 0; return; }
    if (s.run.mode == ItlAwdlTestFollow) {
        int64_t wait = ItlAwdl::clockDelta(slot.end_gp2_us + 2 * ItlAwdl::TuUs, event.finish_gp2_us);
        later(wait > 0 ? uint32_t(wait) : 200);
    } else later(10000);
}
void ItlIwx::awdlTimeout(OSObject *owner, IOTimerEventSource *) {
    static_cast<ItlIwx *>(owner)->awdlStop(ItlAwdlExpired);
}
void ItlIwx::awdlDetach() {
    if (!awdl.ready) return;
    getMainCommandGate()->runAction([](OSObject *, void *p, void *, void *, void *) -> IOReturn {
        auto self = static_cast<ItlIwx *>(p);
        self->awdlStop(ItlAwdlDetached);
        self->awdl.ready = false;
        self->awdl.owner = nullptr;
        self->awdlNativeRadioOwner = nullptr;
        self->awdlNativeTxQualification = false;
        if (self->awdl.timer) {
            self->awdl.timer->cancelTimeout();
            self->getMainWorkLoop()->removeEventSource(self->awdl.timer);
            OSSafeReleaseNULL(self->awdl.timer);
        }
        if (self->awdl.runTimer) {
            self->awdl.runTimer->cancelTimeout();
            self->getMainWorkLoop()->removeEventSource(self->awdl.runTimer);
            OSSafeReleaseNULL(self->awdl.runTimer);
        }
        if (self->awdl.ring) {
            bzero(self->awdl.ring, sizeof(*self->awdl.ring) * (ITL_AWDL_RING_COUNT + 1));
            IOFree(self->awdl.ring, sizeof(*self->awdl.ring) * (ITL_AWDL_RING_COUNT + 1));
            self->awdl.ring = nullptr;
        }
        if (self->awdl.dataRing) {
            bzero(self->awdl.dataRing, sizeof(*self->awdl.dataRing) * ITL_AWDL_RING_COUNT);
            IOFree(self->awdl.dataRing, sizeof(*self->awdl.dataRing) * ITL_AWDL_RING_COUNT);
            self->awdl.dataRing = nullptr;
        }
        self->awdl.dataHead = self->awdl.dataStatus.queued = self->awdl.dataStatus.enabled = 0;
        return kIOReturnSuccess;
    }, this);
}

void ItlIwx::awdlObserve(mbuf_t m, uint32_t channel, int32_t rssi, uint32_t gp2,
    uint64_t tsf, bool deaggregated, bool tsfOverloaded) {
    ++awdl.rxTotal;
    size_t length = mbuf_pkthdr_len(m);
    // RX MPDUs are contiguous here. Reject unexpectedly fragmented buffers.
    if (mbuf_len(m) != length) return;
    auto bytes = static_cast<const uint8_t *>(mbuf_data(m));
    if (awdlNativeRadioOwner && awdlNativeTxQualification && !deaggregated && channel == awdl.channel)
        awdlObservedReservation.observe(bytes,length,gp2,awdlNowUs(),awdlNativeAddress);
    if (awdlNativeRadioOwner && awdlNativeTxQualification && !deaggregated &&
        channel == awdl.channel && awdlNativeTraceIndex < awdlNativeTraceCount &&
        awdlNativeTraceIndex < nitems(awdlNativeTrace)) {
        auto &trace = awdlNativeTrace[awdlNativeTraceIndex];
        uint32_t delay = 0;
        // The firmware may deliver the ACK MPDU after its TX notification in
        // the same RX batch. Keep observing this lease after pending becomes
        // zero; the hardware-clock bound rejects older/later transmissions.
        if (trace.unicast && trace.doorbellHostUs && ItlAwdl::matchOwnedAck(bytes,length,
            awdlNativeAddress,trace.frameGp2,gp2,&delay)) {
            if (!trace.ackCount) { trace.firstAckGp2 = gp2; trace.firstAckHostUs = awdlNowUs(); }
            trace.lastAckGp2 = gp2;
            ++trace.ackCount;
        }
    }
    if (awdlNativeRadioOwner && awdlNativeTxQualification && !deaggregated &&
        channel == awdl.channel && awdlNativeTraceIndex < awdlNativeTraceCount &&
        awdlNativeTraceIndex < nitems(awdlNativeTrace)) {
        auto &trace = awdlNativeTrace[awdlNativeTraceIndex];
        if (trace.doorbellHostUs && !trace.ackCount)
            trace.beforeAckRx.observe(bytes,length,trace.frameGp2,gp2);
    }
    if (length < 24) return;
    if (awdl.dataStatus.enabled && channel == awdl.channel && ItlAwdl::equalMac(bytes + 10, awdl.peer))
        awdlObserveData(bytes, length, channel, rssi, gp2, deaggregated);
    if (length < 29) return;
    if (bytes[0] != 0xd0 || bytes[24] != 127 || bytes[25] != 0 ||
        bytes[26] != 0x17 || bytes[27] != 0xf2 || bytes[28] != 8) return;
    ++awdl.rxAwdl;
    if (channel != awdl.channel || !ItlAwdl::equalMac(bytes + 10, awdl.peer)) return;
    ++awdl.rxPeer;
    if (length > ITL_AWDL_MAX_FRAME || ItlAwdl::parseAction(bytes, length, nullptr) != ItlAwdl::ParseResult::Valid) {
        ++awdl.rxInvalid;
        if (length <= ITL_AWDL_MAX_FRAME && !awdl.rejected) {
            auto &sample = awdl.ring[ITL_AWDL_RING_COUNT].frame;
            bzero(&sample, sizeof(sample));
            sample.version = ITL_AWDL_ABI_VERSION;
            sample.length = uint32_t(length); sample.channel = channel; sample.rssi_dbm = rssi;
            memcpy(sample.frame, bytes, length);
            awdl.rejected = true;
        }
        return;
    }
    awdl.lastPeerRxUs = awdlNowUs();
    awdl.lastPeerGp2 = gp2;
    if (awdlNativeRadioOwner && ItlAwdl::syncWireProfile(bytes,length,&awdlNativePeerProfile))
        ++awdlNativePeerProfileSamples;
    ItlAwdl::SyncAnnouncement announcement = {};
    ItlAwdl::ScheduleSample sample = {};
    if (ItlAwdl::parseSchedule(bytes, length, &announcement) == ItlAwdl::ScheduleResult::Valid &&
        awdl.schedule.observe(announcement, gp2, awdl.lastPeerRxUs, &sample) &&
        (sample.reset || sample.outlier) && awdl.run.mode != ItlAwdlTestHop && awdl.pendingWindow) {
        awdl.pendingWindow = false; ++awdl.run.cancelled;
    }
    if (awdl.count == ITL_AWDL_RING_COUNT) { ++awdl.rxDropped; return; }
    auto &record = awdl.ring[(awdl.head + awdl.count) % ITL_AWDL_RING_COUNT];
    bzero(&record, sizeof(record));
    const auto clocks = ItlAwdl::receiveClockSample(gp2, tsf, tsfOverloaded);
    record.clock_flags = clocks.flags;
    record.gp2_us = clocks.gp2Us;
    record.tsf_us = clocks.tsfUs;
    auto &frame = record.frame;
    frame.version = ITL_AWDL_ABI_VERSION; frame.length = uint32_t(length);
    frame.channel = channel; frame.rssi_dbm = rssi;
    frame.timestamp_us = awdl.lastPeerRxUs;
    memcpy(frame.frame, bytes, length);
    ++awdl.count;
}

void ItlIwx::awdlObserveData(const uint8_t *bytes, size_t length, uint32_t channel,
                            int32_t rssi, uint32_t gp2, bool deaggregated) {
    ItlAwdl::DataBatch batch = {};
    auto result = ItlAwdl::decodeData(bytes, length, deaggregated, &batch);
    auto &status = awdl.dataStatus;
    if (result == ItlAwdl::DataResult::NotAwdl) return;
    if (result == ItlAwdl::DataResult::Unsupported) { ++status.unsupported; return; }
    if (result != ItlAwdl::DataResult::Valid) { ++status.malformed; return; }
    // Reserve space for the whole aggregate; never expose partial delivery.
    if (batch.count > ITL_AWDL_RING_COUNT - status.queued) {
        status.dropped += uint32_t(batch.count); return;
    }
    const uint64_t now = awdlNowUs();
    static_assert(ItlAwdl::DataMtu == ITL_AWDL_DATA_MTU, "RX MTU");
    for (size_t i = 0; i < batch.count; ++i) {
        const auto &packet = batch.packets[i];
        auto &record = awdl.dataRing[(awdl.dataHead + status.queued) % ITL_AWDL_RING_COUNT];
        bzero(&record, sizeof(record));
        record.version = ITL_AWDL_ABI_VERSION;
        record.length = uint32_t(packet.length); record.channel = channel; record.rssi_dbm = rssi;
        record.timestamp_us = now; record.gp2_us = gp2; record.sequence = packet.sequence;
        memcpy(record.source, packet.source, 6); memcpy(record.destination, packet.destination, 6);
        memcpy(record.ipv6, packet.ipv6, packet.length);
        ++status.queued; ++status.accepted;
    }
}

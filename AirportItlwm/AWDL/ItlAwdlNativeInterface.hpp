// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Airport/Apple80211.h>
#include <Airport/macOS15/IO80211AWDLProtocol.h>
#include <AWDL/ItlAwdlABI.h>
#include <AWDL/AwdlNativeMetadata.h>
#include <AWDL/AwdlActionTemplate.h>
#include <AWDL/AwdlSyncAction.h>
#include <AWDL/AwdlNativeChannelSequence.h>
#include <AWDL/AwdlLocalTimeline.h>
#include <AWDL/AwdlNativeSyncParameters.h>
#include <AWDL/AwdlNativeRuntime.h>
#include <IOKit/IOTimerEventSource.h>

class AirportItlwm;
class ItlIwx;
class ItlAwdlNativePacketPool;
class ItlAwdlNativeTxQueue;
class ItlAwdlNativeRxQueue;
class IOSkywalkPacket;
class IOSkywalkTxSubmissionQueue;
class IOSkywalkRxCompletionQueue;

// Retained native integration, bounded management synchronization and separate
// BSD publication. The IPv6/data path is unfinished; no AirDrop capability claim.
class ItlAwdlNativeInterface : public IO80211AWDLProtocol {
    OSDeclareDefaultStructors(ItlAwdlNativeInterface)
public:
    static IOReturn probeInitialization(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeInitRequest *, ItlAwdlNativeInitResponse *);
    static IOReturn probeStart(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeStartRequest *, ItlAwdlNativeStartResponse *);
    static IOReturn managedSession(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeSessionRequest *, ItlAwdlNativeSessionResponse *);
    static void releaseManaged(AirportItlwm *);
    static IOReturn probePackets(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativePacketRequest *, ItlAwdlNativePacketResponse *);
    static IOReturn probeQueues(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeQueueRequest *, ItlAwdlNativeQueueResponse *);
    static IOReturn managedTransport(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeTransportRequest *, ItlAwdlNativeTransportResponse *);
    static IOReturn managedBSD(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeBSDRequest *, ItlAwdlNativeBSDResponse *);
    static IOReturn probeActivationPreflight(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeConfigureRequest *, ItlAwdlNativeConfigureResponse *);
    static IOReturn probeConfiguration(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeConfigureRequest *, ItlAwdlNativeConfigureResponse *);
    static IOReturn probeTemplate(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeConfigureRequest *, ItlAwdlNativeTemplateResponse *);
    static IOReturn probeTiming(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeConfigureRequest *, ItlAwdlNativeTimingResponse *);
    static IOReturn probeRadio(AirportItlwm *, ItlIwx *,
        const ItlAwdlBeginRequest *, ItlAwdlNativeRadioResponse *, ItlAwdlNativeSyncTxResponse * = nullptr,
        bool unicast = false, bool p2p = false, bool seriesObservation = false);
    static IOReturn runSynchronization(AirportItlwm *, ItlIwx *,
        const ItlAwdlBeginRequest *, ItlAwdlNativeRuntimeResponse *);
    // HAL invalidation is non-sleeping; the retained transaction cleans up
    // the native manager after the radio has stopped or controller teardown.
    void synchronizationRadioStopped();
    void synchronizationReceive(const uint8_t *, size_t, uint32_t, int32_t, uint32_t);
    void free() override;
    bool start(IOService *) override;
    void stop(IOService *) override;
    void *createPeerManager() override;
    IO80211Controller *getController() override { return controller; }
    ether_addr getSelfMacAddr() override { return selfAddress; }
    const char *getBSDNamePrefix() override { return "awdl"; }
    IOReturn enable(unsigned int) override;
    SInt32 setInterfaceEnable(bool) override;
    IOReturn prepareBSDInterface(ifnet_t, unsigned int) override;
    // Management RX under the work-loop gate, after the live synchronization
    // engine qualifies the shared GP2 clock and native activation succeeds.
    IOReturn receiveAction(const uint8_t *, size_t, const ItlAwdl::NativeActionRxContext &);
    // Snapshot the staged template plus caller-supplied radio sync context.
    // Requires the work-loop gate, but does not transmit or imply RF readiness.
    size_t copySyncAction(const ItlAwdl::SyncActionContext &, const ItlAwdl::SyncActionParameters &,
                          uint8_t *, size_t);
    IOReturn setMacAddress(ether_addr &) override { return kIOReturnUnsupported; }
#define ITL_NATIVE_REQUEST(name, type) \
    IOReturn name(type *data) override { \
        return backendResult(Backend_##name, data ? kIOReturnUnsupported : kIOReturnBadArgument); \
    }
#define ITL_NATIVE_IMPLEMENTED_REQUEST(name, type) IOReturn name(type *data) override;
#include "ItlAwdlNativeRequests.def"
#undef ITL_NATIVE_REQUEST
#undef ITL_NATIVE_IMPLEMENTED_REQUEST
private:
    enum RuntimePhase { RuntimeIdle, RuntimeStarting, RuntimeReady, RuntimeRunning, RuntimeStopping, RuntimeDone };
    RuntimePhase runtimePhase;
    ItlIwx *runtimeRadio;
    IOTimerEventSource *runtimeTimer;
    ItlAwdl::LocalTimeline runtimeTimeline;
    ItlAwdlNativeRuntimeResponse runtimeResult;
    bool runtimeSyncEnabled, runtimeNativeEnabled, runtimeEnableInvoked;
    bool runtimeHadWindow, runtimeClockQualified, preflightRejectEnable;
    uint16_t runtimeWindow;
    int runtimeGeneration;
    uint64_t runtimeNextTxUs;
    bool runtimeRadioReady() const;
    void runtimeTick();
    void finishSynchronization();
    static void runtimeTimeout(OSObject *, IOTimerEventSource *);
    enum BackendIndex {
#define ITL_NATIVE_REQUEST(name, type) Backend_##name,
#define ITL_NATIVE_IMPLEMENTED_REQUEST(name, type) Backend_##name,
#include "ItlAwdlNativeRequests.def"
#undef ITL_NATIVE_REQUEST
#undef ITL_NATIVE_IMPLEMENTED_REQUEST
        BackendCount
    };
    static_assert(BackendCount == 63, "24H32 backend trace indices");
    IOReturn backendResult(BackendIndex, IOReturn);
    ItlAwdlNativeConfigureResponse *backendTrace;
    ItlAwdlNativeConfigureResponse configurationResult;
    ItlAwdlNativeConfigureResponse activationPreflightResult;
    ItlAwdlNativeTemplateResponse templateResult;
    ItlAwdlNativeTimingResponse timingResult;
    ItlAwdl::NativeSyncParameters syncParameters;
    ItlAwdl::ActionTemplate syncTemplate;
    ItlAwdl::NativeBssidConfiguration networkConfiguration;
    uint16_t electionId;
    bool electionIdValid;
    uint32_t electionMetric;
    bool electionMetricValid;
    uint8_t minimumRateMbps;
    bool minimumRateValid;
    uint16_t masterChannel;
    bool masterChannelValid;
    int32_t electionRssiThresholds[4];
    bool electionRssiThresholdsValid;
    apple80211_awdl_sync_state stagedSyncRole;
    bool stagedSyncRoleValid;
    uint32_t presenceMode;
    bool presenceModeValid;
    uint8_t extensionCounts[4]; // minimum, multicast, unicast, action
    bool extensionCountsValid;
    ItlAwdl::NativeChannelSequence channelSequence;
    IO80211Controller *controller;
    bool virtualInitAttempted;
    bool resourcesDisposed;
    bool bpfDisposed;
    bool startAllowed;
    bool startAttempted;
    bool failPeerCreation;
    uint32_t backendCalls;
    uint32_t peerCreationCalls;
    uint32_t partialCleanups;
    ether_addr selfAddress;
    ItlAwdlNativePacketPool *transportPool;
    ItlAwdlNativeTxQueue *transportTx;
    ItlAwdlNativeRxQueue *transportRx;
    IOWorkLoop *transportLoop;
    bool transportTxAdded, transportRxAdded, transportRegistrationAttempted, transportRegistered;
    uint32_t transportPrepares, transportRegisters, transportDeregisters, transportCallbacks;
    uint32_t transportCleanupErrors, transportLastStage;
    bool bsdPublishRequested;
    uint32_t bsdPublishCalls, bsdPrepareCalls, bsdPrepareError;
    uint32_t bsdEnableCalls, bsdEnableDenied;
    void describeBSD(ItlAwdlNativeBSDResponse *);
    void publishTemplateProfile(uint32_t, uint32_t);
    size_t encodeQualificationFrame(uint32_t, uint8_t *, size_t,
        const ItlAwdl::LocalSyncBoundary * = nullptr, uint16_t sequence = 0,
        const uint8_t *unicastPeer = nullptr);
    IOReturn prepareTransport(IOWorkLoop *, uint32_t);
    IOReturn clearTransport(bool systemStop = false);
    void describeTransport(ItlAwdlNativeTransportResponse *);
    static unsigned int transportDequeue(OSObject *, IOSkywalkTxSubmissionQueue *,
        IOSkywalkPacket *const *, unsigned int, void *);
    static unsigned int transportEnqueue(OSObject *, IOSkywalkRxCompletionQueue *,
        IOSkywalkPacket **, unsigned int, void *);
    bool initializeForProbe(IO80211Controller *, ether_addr *, uint32_t,
        ItlAwdlNativeInitResponse *);
    void disposeInitializedResources();
    static void checkBackendContract(ItlAwdlNativeInterface *, ItlAwdlNativeInitResponse *);
    static IOReturn probeGated(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeInitRequest *, ItlAwdlNativeInitResponse *);
    static IOReturn probeStartGated(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeStartRequest *, ItlAwdlNativeStartResponse *);
    static IOReturn managedSessionGated(AirportItlwm *, ItlIwx *, uint32_t,
        ItlAwdlNativeSessionResponse *);
    static void releaseManagedGated(AirportItlwm *);
    static void describeManaged(AirportItlwm *, ItlAwdlNativeSessionResponse *);
};

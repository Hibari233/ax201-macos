// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Airport/Apple80211.h>
#include <Airport/macOS15/IO80211AWDLProtocol.h>
#include <AWDL/ItlAwdlABI.h>
#include <AWDL/AwdlNativeMetadata.h>
#include <AWDL/AwdlActionTemplate.h>
#include <AWDL/AwdlSyncAction.h>

class AirportItlwm;
class ItlIwx;
class ItlAwdlNativePacketPool;
class ItlAwdlNativeTxQueue;
class ItlAwdlNativeRxQueue;
class IOSkywalkPacket;
class IOSkywalkTxSubmissionQueue;
class IOSkywalkRxCompletionQueue;

// Retained native integration and explicit BSD publication. The unfinished
// radio backend rejects enable requests; no AirDrop capability claim.
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
    static IOReturn probeConfiguration(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeConfigureRequest *, ItlAwdlNativeConfigureResponse *);
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
    // Future radio RX entry, under this interface's work-loop gate. Receive
    // context must already use the synchronization backend's local RX clock.
    // No RF callback is connected until that clock/backend is qualified.
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
    bool configurationTracing;
    ItlAwdlNativeConfigureResponse configurationResult;
    ItlAwdl::ActionTemplate syncTemplate;
    ItlAwdl::NativeBssidConfiguration networkConfiguration;
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

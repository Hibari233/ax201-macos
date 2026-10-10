// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Airport/Apple80211.h>
#include <Airport/macOS15/IO80211AWDLProtocol.h>
#include <AWDL/ItlAwdlABI.h>

class AirportItlwm;
class ItlIwx;

// Bounded native integration probes under the radio gate. No registration,
// firmware command, channel change, link enable or AirDrop capability claim.
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
    void free() override;
    bool start(IOService *) override;
    void stop(IOService *) override;
    void *createPeerManager() override;
    IO80211Controller *getController() override { return controller; }
    ether_addr getSelfMacAddr() override { return selfAddress; }
    IOReturn setMacAddress(ether_addr &) override { return kIOReturnUnsupported; }
#define ITL_NATIVE_REQUEST(name, type) \
    IOReturn name(type *data) override { \
        ++backendCalls; \
        return data ? kIOReturnUnsupported : kIOReturnBadArgument; \
    }
#include "ItlAwdlNativeRequests.def"
#undef ITL_NATIVE_REQUEST
private:
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
    bool initializeForProbe(IO80211Controller *, ether_addr *, uint32_t,
        ItlAwdlNativeInitResponse *);
    void disposeInitializedResources();
    static void checkUnsupportedContract(IO80211AWDLProtocol *, ItlAwdlNativeInitResponse *);
    static IOReturn probeGated(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeInitRequest *, ItlAwdlNativeInitResponse *);
    static IOReturn probeStartGated(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeStartRequest *, ItlAwdlNativeStartResponse *);
    static IOReturn managedSessionGated(AirportItlwm *, ItlIwx *, uint32_t,
        ItlAwdlNativeSessionResponse *);
    static void releaseManagedGated(AirportItlwm *);
    static void describeManaged(AirportItlwm *, ItlAwdlNativeSessionResponse *);
};

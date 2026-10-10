// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Airport/Apple80211.h>
#include <Airport/macOS15/IO80211AWDLProtocol.h>
#include <AWDL/ItlAwdlABI.h>

class AirportItlwm;
class ItlIwx;

// First native integration step: a real role-6 object, initialized and disposed
// synchronously under the radio gate. No start(), registration, firmware command,
// peer manager, channel change, or capability claim is made by this backend.
class ItlAwdlNativeInterface : public IO80211AWDLProtocol {
    OSDeclareDefaultStructors(ItlAwdlNativeInterface)
public:
    static IOReturn probeInitialization(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeInitRequest *, ItlAwdlNativeInitResponse *);
    void free() override;
    bool start(IOService *) override { return false; }
    IO80211Controller *getController() override { return controller; }
    ether_addr getSelfMacAddr() override { return selfAddress; }
    IOReturn setMacAddress(ether_addr &) override { return kIOReturnUnsupported; }
#define ITL_NATIVE_REQUEST(name, type) \
    IOReturn name(type *data) override { \
        return data ? kIOReturnUnsupported : kIOReturnBadArgument; \
    }
#include "ItlAwdlNativeRequests.def"
#undef ITL_NATIVE_REQUEST
private:
    IO80211Controller *controller;
    bool virtualInitAttempted;
    bool resourcesDisposed;
    ether_addr selfAddress;
    bool initializeForProbe(IO80211Controller *, ether_addr *, uint32_t,
        ItlAwdlNativeInitResponse *);
    void disposeInitializedResources();
    static void checkUnsupportedContract(IO80211AWDLProtocol *, ItlAwdlNativeInitResponse *);
    static IOReturn probeGated(AirportItlwm *, ItlIwx *,
        const ItlAwdlNativeInitRequest *, ItlAwdlNativeInitResponse *);
};

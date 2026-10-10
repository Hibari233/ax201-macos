// SPDX-License-Identifier: GPL-2.0-or-later
#include "ItlAwdlNativeInterface.hpp"
#include "AirportItlwmV2.hpp"
#include <libkern/version.h>

OSDefineMetaClassAndStructors(ItlAwdlNativeInterface, IO80211AWDLProtocol)

bool ItlAwdlNativeInterface::initializeForProbe(IO80211Controller *parent,
    ether_addr *address, uint32_t stopAfter, ItlAwdlNativeInitResponse *out) {
    virtualInitAttempted = false;
    resourcesDisposed = false;
    controller = parent;
    controller->retain();
    if (!IO80211SkywalkInterface::init()) return false;
    ++out->base_initialized;
    if (stopAfter == 1) return true;
    virtualInitAttempted = true;
    if (!IO80211VirtualInterface::init(controller, address, 6, "awdl")) return false;
    ++out->virtual_initialized;
    out->role = static_cast<uint32_t>(getInterfaceRole());
    return out->role == 6;
}

void ItlAwdlNativeInterface::disposeInitializedResources() {
    if (resourcesDisposed) return;
    resourcesDisposed = true;
    // VirtualInterface::init registers power interest and opens the controller.
    // The native free() only frees its arrays/expansion. stop() is inappropriate
    // here: Skywalk::stop assumes start() has created a peer manager.
    if (controller && virtualInitAttempted) {
        if (controller->isOpen(this)) controller->close(this);
        controller->deRegisterInterestedDriver(this);
    }
    if (hasNativeVirtualStorage()) freeBpfResources();
}

void ItlAwdlNativeInterface::free() {
    disposeInitializedResources();
    OSSafeReleaseNULL(controller);
    // Audited 24H32: Skywalk::free dereferences its expansion before testing
    // for null. The lower bases tolerate absent/partially initialized storage.
    if (hasNativeSkywalkStorage()) IO80211AWDLProtocol::free();
    else IOSkywalkEthernetInterface::free();
}

void ItlAwdlNativeInterface::checkUnsupportedContract(IO80211AWDLProtocol *object,
    ItlAwdlNativeInitResponse *out) {
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
#include "ItlAwdlNativeRequests.def"
#undef ITL_NATIVE_REQUEST
    ether_addr address = {{2, 0, 0, 0, 0, 1}};
    ++out->methods_checked;
    if (object->setMacAddress(address) != kIOReturnUnsupported) ++out->method_failures;
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
        if (initialized && request->stop_after == 0) checkUnsupportedContract(object, out);
        object->disposeInitializedResources();
        if (parent->isOpen(object)) ++out->provider_open_after;
        object->release();
        ++out->released;
        if (!initialized) { result = kIOReturnError; break; }
        if (metaClass->getInstanceCount() || out->provider_open_after || out->method_failures) {
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
    struct Call { AirportItlwm *parent; ItlIwx *radio;
        const ItlAwdlNativeInitRequest *request; ItlAwdlNativeInitResponse *out; } call = {
            parent, radio, request, out};
    return gate->runAction([](OSObject *, void *argument, void *, void *, void *) -> IOReturn {
        auto call = static_cast<Call *>(argument);
        return probeGated(call->parent, call->radio, call->request, call->out);
    }, &call);
}

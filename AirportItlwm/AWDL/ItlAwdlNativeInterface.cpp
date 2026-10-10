// SPDX-License-Identifier: GPL-2.0-or-later
#include "ItlAwdlNativeInterface.hpp"
#include "ItlAwdlNativePacket.hpp"
#include "ItlAwdlNativeQueues.hpp"
#include "AirportItlwmV2.hpp"
#include <Airport/CCPipe.h>
#include <libkern/version.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/OSAtomic.h>

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
    virtualInitAttempted = false;
    resourcesDisposed = false;
    bpfDisposed = startAllowed = startAttempted = failPeerCreation = false;
    backendCalls = peerCreationCalls = partialCleanups = 0;
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

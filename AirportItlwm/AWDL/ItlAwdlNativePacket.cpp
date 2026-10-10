// SPDX-License-Identifier: GPL-2.0-or-later
#include "ItlAwdlNativePacket.hpp"
#include <libkern/libkern.h>

OSDefineMetaClassAndStructors(ItlAwdlNativePacket, IO80211NetworkPacket)
OSDefineMetaClassAndStructors(ItlAwdlNativePacketPool, IOSkywalkPacketBufferPool)

void ItlAwdlNativePacket::acquireWithPacketHandle(unsigned long long handle, unsigned int flags) {
    enqueueTime = timestamp = 0;
    txStatus = static_cast<IO80211NetworkTXStatus>(7); // Native default, no successful TX claim.
    interfaceID = -1;
    ptmMode = false;
    IOSkywalkPacket::acquireWithPacketHandle(handle, flags);
}
void *ItlAwdlNativePacket::getVirtualAddress() {
    return getPacketBufferCount() == 1 ? getDataVirtualAddress() : nullptr;
}

bool ItlAwdlNativePacketPool::initializePool(const char *name, OSObject *owner, unsigned int failure) {
    failureMode = failure;
    packetFactories = segmentFactories = injectedFailures = 0;
    PoolOptions options = {64, 64, ItlAwdlNativePacket::BufferSize, 1, 16384, 0, nullptr};
    return initWithName(name, owner, 1, &options);
}
ItlAwdlNativePacketPool *ItlAwdlNativePacketPool::withOwner(OSObject *owner) {
    if (!owner) return nullptr;
    auto pool = new ItlAwdlNativePacketPool;
    if (pool && !pool->initializePool("ax201.awdl.packets", owner, 0)) {
        pool->release();
        return nullptr;
    }
    return pool;
}
IOReturn ItlAwdlNativePacketPool::newPacket(IOSkywalkPacketDescriptor *descriptor, IOSkywalkPacket **out) {
    if (!out) return kIOReturnBadArgument;
    *out = nullptr;
    ++packetFactories;
    if ((failureMode == 1 && packetFactories == 1) || (failureMode == 2 && packetFactories == 4)) {
        ++injectedFailures;
        return kIOReturnNoMemory;
    }
    auto packet = new ItlAwdlNativePacket;
    if (!packet) return kIOReturnNoMemory;
    if (!packet->initWithPool(this, descriptor, 0)) {
        packet->release();
        return kIOReturnNoMemory;
    }
    *out = packet;
    return kIOReturnSuccess;
}
IOReturn ItlAwdlNativePacketPool::newMemorySegment(IOSkywalkMemorySegmentDescriptor *descriptor,
    IOSkywalkMemorySegment **out) {
    if (!out) return kIOReturnBadArgument;
    *out = nullptr;
    ++segmentFactories;
    if (failureMode == 3 && segmentFactories == 1) {
        ++injectedFailures;
        return kIOReturnNoMemory;
    }
    return IOSkywalkPacketBufferPool::newMemorySegment(descriptor, out);
}

namespace {
uint8_t pattern(unsigned int round, unsigned int slot, unsigned int byte) {
    return static_cast<uint8_t>((byte * 31U) ^ (slot * 17U) ^ (round * 73U));
}
}

IOReturn ItlAwdlNativePacketPool::checkBuffers(ItlAwdlNativePacketResponse *out) {
    const uint32_t lengths[] = {64, 1280, 1514};
    const uint16_t offsets[] = {0, 32, 128};
    for (unsigned int round = 0; round < 3; ++round) {
        IOSkywalkPacket *packets[8] = {};
        IOReturn result = kIOReturnSuccess;
        for (unsigned int i = 0; i < 8 && !result; ++i) {
            result = allocatePacket(&packets[i], 1); // Native nonblocking allocation.
            if (result) break;
            ++out->allocations;
            auto packet = OSDynamicCast(ItlAwdlNativePacket, packets[i]);
            if (!packet || packet->getPktEnqueueTime() || packet->getIngressEgressTimestamp() ||
                packet->isPTMMode() || packet->getInterfaceID() != -1 ||
                static_cast<unsigned int>(packet->getFirmwareTxStatus()) != 7) {
                result = kIOReturnError; break;
            }
            ++out->metadata_checks;
            result = packet->prepare(nullptr, 0, 0); // Audited null-queue, CPU-only mapping.
            if (result) break;
            ++out->prepared;
            if (packet->getPacketBufferCount() != 1 || !packet->getVirtualAddress() ||
                packet->getBufferSize() < offsets[round] + lengths[round]) {
                result = kIOReturnError; break;
            }
            result = packet->setDataOffsetAndLength(offsets[round], lengths[round]);
            if (result) break;
            auto data = static_cast<uint8_t *>(packet->getVirtualAddress()) + offsets[round];
            for (unsigned int j = 0; j < lengths[round]; ++j) data[j] = pattern(round, i, j);
            packet->setPktEnqueueTime(0x1234567800000000ULL + i);
            packet->setIngressEgressTimestamp(0xabcde00000000000ULL + round);
            packet->setPTMMode(true);
            packet->setInterfaceID(6);
            packet->setFirmwareTxStatus(static_cast<IO80211NetworkTXStatus>(1));
            if (packet->getPktEnqueueTime() != 0x1234567800000000ULL + i ||
                packet->getIngressEgressTimestamp() != 0xabcde00000000000ULL + round ||
                !packet->isPTMMode() || packet->getInterfaceID() != 6 ||
                static_cast<unsigned int>(packet->getFirmwareTxStatus()) != 1) {
                result = kIOReturnError; break;
            }
            ++out->metadata_checks;
            if (packet->setDataOffsetAndLength(1536, 1024) != kIOReturnBadArgument) {
                result = kIOReturnError; break;
            }
            ++out->bounds_denied;
        }
        // Check after filling all eight simultaneous leases to detect aliasing.
        for (unsigned int i = 0; i < 8 && !result; ++i) {
            auto packet = packets[i];
            if (!packet || packet->getDataLength() != lengths[round] ||
                packet->getDataOffset() != offsets[round]) { result = kIOReturnError; break; }
            auto data = static_cast<const uint8_t *>(packet->getDataVirtualAddress()) + offsets[round];
            for (unsigned int j = 0; j < lengths[round]; ++j) {
                if (data[j] != pattern(round, i, j)) { result = kIOReturnError; break; }
                ++out->bytes_checked;
            }
            if (!result) ++out->pattern_checks;
        }
        for (auto packet : packets) {
            if (!packet) continue;
            // Native deallocation completes prepared packets, disposes their
            // buflet leases, and returns the kernel packet to this pool.
            deallocatePacket(packet);
            ++out->returned;
        }
        if (result) return result;
    }
    return kIOReturnSuccess;
}

IOReturn ItlAwdlNativePacketPool::probeCycle(OSObject *owner, unsigned int failure,
    ItlAwdlNativePacketResponse *out) {
    auto pool = new ItlAwdlNativePacketPool;
    if (!pool) return kIOReturnNoMemory;
    ++out->attempted;
    bool initialized = pool->initializePool("ax201.awdl.packet-probe", owner, failure);
    IOReturn result = kIOReturnSuccess;
    if (initialized) {
        ++out->initialized;
        result = failure ? kIOReturnError : pool->checkBuffers(out);
    } else if (failure && pool->injectedFailures == 1) {
        ++out->expected_failures;
    } else result = kIOReturnNoMemory;
    out->packet_factories += pool->packetFactories;
    out->segment_factories += pool->segmentFactories;
    out->injected_failures += pool->injectedFailures;
    pool->release(); // Preserve the native release override, including partial init.
    return result;
}

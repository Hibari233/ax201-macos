// SPDX-License-Identifier: GPL-2.0-or-later
#include "ItlAwdlNativeQueues.hpp"
#include "ItlAwdlNativePacket.hpp"
#include <IOKit/IOWorkLoop.h>

OSDefineMetaClassAndStructors(ItlAwdlNativeTxQueue, IOSkywalkTxSubmissionQueue)
OSDefineMetaClassAndStructors(ItlAwdlNativeRxQueue, IOSkywalkRxCompletionQueue)

#define ITL_QUEUE_LIFECYCLE(derived, native) \
IOReturn derived::initialize(void *context) { \
    ++initializeCalls; \
    auto result = native::initialize(context); \
    initializeError = static_cast<uint32_t>(result); \
    initialized = result == kIOReturnSuccess; \
    return result; \
} \
IOReturn derived::finalize() { \
    ++finalizeCalls; \
    auto result = native::finalize(); \
    if (!result) initialized = false; \
    return result; \
}
ITL_QUEUE_LIFECYCLE(ItlAwdlNativeTxQueue, IOSkywalkTxSubmissionQueue)
ITL_QUEUE_LIFECYCLE(ItlAwdlNativeRxQueue, IOSkywalkRxCompletionQueue)
#undef ITL_QUEUE_LIFECYCLE

ItlAwdlNativeTxQueue *ItlAwdlNativeTxQueue::withPool(IOSkywalkPacketBufferPool *pool,
    OSObject *owner, Dequeue callback, void *argument) {
    if (!pool || !owner || !callback) return nullptr;
    auto queue = new ItlAwdlNativeTxQueue;
    if (queue) queue->initializeCalls = queue->finalizeCalls = queue->initializeError = queue->initialized = 0;
    if (queue && !queue->initWithPool(pool, ~0U, 32, 0, owner, nullptr, callback, argument, 0)) {
        queue->release();
        return nullptr;
    }
    return queue;
}
ItlAwdlNativeRxQueue *ItlAwdlNativeRxQueue::withPool(IOSkywalkPacketBufferPool *pool,
    OSObject *owner, Enqueue callback, void *argument) {
    if (!pool || !owner || !callback) return nullptr;
    auto queue = new ItlAwdlNativeRxQueue;
    if (queue) queue->initializeCalls = queue->finalizeCalls = queue->initializeError = queue->initialized = 0;
    if (queue && !queue->initWithPool(pool, 32, 1, owner, callback, argument, 0)) {
        queue->release();
        return nullptr;
    }
    return queue;
}

namespace {
IOReturn expectResult(IOReturn actual, IOReturn expected) {
    // Preserve unexpected native errors in the report instead of losing them
    // behind a boolean comparison. Unexpected success is still a failure.
    return actual == expected ? kIOReturnSuccess : actual ? actual : kIOReturnError;
}
// A probe never enables the queues. These callbacks consume/produce zero
// packets, and any invocation fails the diagnostic instead of claiming TX.
unsigned int txCallback(OSObject *, IOSkywalkTxSubmissionQueue *,
    IOSkywalkPacket *const *, unsigned int, void *argument) {
    ++static_cast<ItlAwdlNativeQueueResponse *>(argument)->callback_calls;
    return 0;
}
unsigned int rxCallback(OSObject *, IOSkywalkRxCompletionQueue *,
    IOSkywalkPacket **, unsigned int, void *argument) {
    ++static_cast<ItlAwdlNativeQueueResponse *>(argument)->callback_calls;
    return 0;
}
bool inspect(IOSkywalkPacketQueue *queue, IOSkywalkPacketBufferPool *pool,
    unsigned int direction, unsigned int id, unsigned int type, ItlAwdlNativeQueueResponse *out) {
    IOSkywalkPacketQueue::QueueInfo info = {};
    if (queue->performCommand(0x10000, &info, sizeof(info)) || info.type != type ||
        info.capacity != 32 || info.pool != pool || queue->getPacketBufferPool() != pool ||
        queue->isEnabled() || queue->getDirection() != direction || queue->getQueueId() != id ||
        queue->getInterface() || queue->getPacketCount() || queue->getFreeSpace() != 31 ||
        queue->getPacketBufferCount()) return false;
    ++out->metadata_checks;
    return true;
}
// A rejected enqueue retains caller ownership. Check all eight simultaneous
// leases after the calls, then return them ourselves, including on errors.
IOReturn rejectPackets(ItlAwdlNativePacketPool *pool, ItlAwdlNativeRxQueue *rx,
    ItlAwdlNativeQueueResponse *out) {
    IOSkywalkPacket *packets[8] = {};
    IOReturn result = kIOReturnSuccess;
    for (unsigned int i = 0; i < 8 && !result; ++i) {
        result = pool->allocatePacket(&packets[i], 1);
        if (result) break;
        ++out->allocations;
        auto packet = packets[i];
        const unsigned int length = i & 1 ? 1514 : 64;
        result = packet->prepare(nullptr, 0, 0);
        if (!result) result = packet->setDataOffsetAndLength(32, length);
        if (result) break;
        auto data = static_cast<uint8_t *>(packet->getDataVirtualAddress());
        if (!data) { result = kIOReturnError; break; }
        for (unsigned int j = 0; j < length; ++j) data[32 + j] = static_cast<uint8_t>(i * 17U ^ j);
        const auto enqueued = rx->enqueuePackets(&packets[i], 1, 0);
        // An unexpected successful enqueue transfers ownership. Do not touch
        // that packet again; finalization must dispose any queue-owned leases.
        if (!enqueued) packets[i] = nullptr;
        result = expectResult(enqueued, kIOReturnOffline);
        if (result) break;
        ++out->rejections;
    }
    for (unsigned int i = 0; i < 8 && !result; ++i) {
        const unsigned int length = i & 1 ? 1514 : 64;
        auto packet = packets[i];
        if (!packet || packet->getDataOffset() != 32 || packet->getDataLength() != length) {
            result = kIOReturnError; break;
        }
        auto data = static_cast<const uint8_t *>(packet->getDataVirtualAddress());
        if (!data) { result = kIOReturnError; break; }
        for (unsigned int j = 0; j < length; ++j) {
            if (data[32 + j] != static_cast<uint8_t>(i * 17U ^ j)) {
                result = kIOReturnError; break;
            }
            ++out->bytes_checked;
        }
    }
    for (auto packet : packets) if (packet) {
        pool->deallocatePacket(packet);
        ++out->returned;
    }
    return result;
}
}

IOReturn ItlAwdlProbeNativeQueues(OSObject *owner, IOWorkLoop *loop, unsigned int stopAfter,
    ItlAwdlNativeQueueResponse *out) {
    if (!owner || !loop || !out || stopAfter > 7) return kIOReturnBadArgument;
    ++out->attempted;
    auto pool = ItlAwdlNativePacketPool::withOwner(owner);
    ItlAwdlNativeTxQueue *tx = nullptr;
    ItlAwdlNativeRxQueue *rx = nullptr;
    bool txAdded = false, rxAdded = false, txInitialized = false, rxInitialized = false;
    IOReturn result = kIOReturnNoMemory;
    do {
        if (!pool) break;
        ++out->pools_created;
        if (stopAfter == 1) { result = kIOReturnSuccess; break; }
        tx = ItlAwdlNativeTxQueue::withPool(pool, owner, txCallback, out);
        if (!tx) break;
        ++out->tx_created;
        if (stopAfter == 2) { result = kIOReturnSuccess; break; }
        rx = ItlAwdlNativeRxQueue::withPool(pool, owner, rxCallback, out);
        if (!rx) break;
        ++out->rx_created;
        if (stopAfter == 3) { result = kIOReturnSuccess; break; }
        result = kIOReturnError;
        if (!inspect(tx, pool, 1, 0, 1, out) || !inspect(rx, pool, 2, 1, 4, out)) break;
        // No work loop means no requests may be scheduled.
        result = expectResult(tx->requestDequeue(nullptr, 0), kIOReturnNotReady);
        if (!result) result = expectResult(rx->requestEnqueue(nullptr, 0), kIOReturnNotReady);
        if (result) break;
        out->rejections += 2;
        result = loop->addEventSource(tx);
        if (result) break;
        txAdded = true; ++out->source_adds;
        if (stopAfter == 4) break;
        result = loop->addEventSource(rx);
        if (result) break;
        rxAdded = true; ++out->source_adds;
        if (stopAfter == 5) break;
        for (unsigned int round = 0; round < 2; ++round) {
            result = tx->initialize(nullptr); // No channel/ring, never enable.
            if (result) break;
            txInitialized = true; ++out->initialized;
            if (stopAfter == 6) break;
            result = rx->initialize(nullptr);
            if (result) break;
            rxInitialized = true; ++out->initialized;
            if (stopAfter == 7) break;
            if (!inspect(tx, pool, 1, 0, 1, out) || !inspect(rx, pool, 2, 1, 4, out)) {
                result = kIOReturnError; break;
            }
            result = expectResult(tx->requestDequeue(nullptr, 0), kIOReturnNotReady);
            if (!result) result = expectResult(rx->requestEnqueue(nullptr, 0), kIOReturnOffline);
            if (result) break;
            out->rejections += 2;
            result = rejectPackets(pool, rx, out);
            if (result) break;
            result = rx->finalize();
            if (result) break;
            rxInitialized = false; ++out->finalized;
            result = tx->finalize();
            if (result) break;
            txInitialized = false; ++out->finalized;
            if (!inspect(tx, pool, 1, 0, 1, out) || !inspect(rx, pool, 2, 1, 4, out)) {
                result = kIOReturnError; break;
            }
        }
    } while (false);
    // finalize checks the queues out of the pool's retaining OSSet. Releasing
    // queue references alone would leave a pool/queue reference cycle.
    if (rx) rx->disable();
    if (tx) tx->disable();
    if (rxInitialized) {
        if (rx->finalize()) ++out->cleanup_errors;
        else ++out->finalized;
    }
    if (txInitialized) {
        if (tx->finalize()) ++out->cleanup_errors;
        else ++out->finalized;
    }
    if (rxAdded) { if (loop->removeEventSource(rx)) ++out->cleanup_errors; else ++out->source_removes; }
    if (txAdded) { if (loop->removeEventSource(tx)) ++out->cleanup_errors; else ++out->source_removes; }
    if (rx) { rx->release(); ++out->queues_released; }
    if (tx) { tx->release(); ++out->queues_released; }
    if (pool) pool->release();
    if (out->cleanup_errors || out->callback_calls) result = kIOReturnError;
    if (!result) {
        if (stopAfter) ++out->expected_stops;
        else ++out->completed;
    }
    return result;
}

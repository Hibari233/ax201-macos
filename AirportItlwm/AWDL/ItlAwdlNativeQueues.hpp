// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Airport/macOS15/IOSkywalkTxSubmissionQueue.h>
#include <Airport/macOS15/IOSkywalkRxCompletionQueue.h>
#include <AWDL/ItlAwdlABI.h>
class IOWorkLoop;

// Distinct metaclasses let diagnostics count only the queues they own.
// Lifecycle wrappers preserve native results, including initialization errors
// the native Nexus callback otherwise discards. No custom packet processing.
class ItlAwdlNativeTxQueue : public IOSkywalkTxSubmissionQueue {
    OSDeclareDefaultStructors(ItlAwdlNativeTxQueue)
public:
    static ItlAwdlNativeTxQueue *withPool(IOSkywalkPacketBufferPool *, OSObject *, Dequeue, void *);
    IOReturn initialize(void *) override;
    IOReturn finalize() override;
    uint32_t initializeCalls, finalizeCalls, initializeError, initialized;
};
class ItlAwdlNativeRxQueue : public IOSkywalkRxCompletionQueue {
    OSDeclareDefaultStructors(ItlAwdlNativeRxQueue)
public:
    static ItlAwdlNativeRxQueue *withPool(IOSkywalkPacketBufferPool *, OSObject *, Enqueue, void *);
    IOReturn initialize(void *) override;
    IOReturn finalize() override;
    uint32_t initializeCalls, finalizeCalls, initializeError, initialized;
};
static_assert(sizeof(ItlAwdlNativeTxQueue) == 0x178, "TX queue adds four diagnostic words");
static_assert(sizeof(ItlAwdlNativeRxQueue) == 0x148, "RX queue adds four diagnostic words");

IOReturn ItlAwdlProbeNativeQueues(OSObject *, IOWorkLoop *, unsigned int, ItlAwdlNativeQueueResponse *);

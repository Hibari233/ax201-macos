// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Airport/macOS15/IOSkywalkTxSubmissionQueue.h>
#include <Airport/macOS15/IOSkywalkRxCompletionQueue.h>
#include <AWDL/ItlAwdlABI.h>
class IOWorkLoop;

// Distinct metaclasses let diagnostics count only the queues they own.
// Construction keeps the native implementation and adds no virtual overrides.
class ItlAwdlNativeTxQueue : public IOSkywalkTxSubmissionQueue {
    OSDeclareDefaultStructors(ItlAwdlNativeTxQueue)
public:
    static ItlAwdlNativeTxQueue *withPool(IOSkywalkPacketBufferPool *, OSObject *, Dequeue, void *);
};
class ItlAwdlNativeRxQueue : public IOSkywalkRxCompletionQueue {
    OSDeclareDefaultStructors(ItlAwdlNativeRxQueue)
public:
    static ItlAwdlNativeRxQueue *withPool(IOSkywalkPacketBufferPool *, OSObject *, Enqueue, void *);
};
static_assert(sizeof(ItlAwdlNativeTxQueue) == 0x168, "TX queue adds no native fields");
static_assert(sizeof(ItlAwdlNativeRxQueue) == 0x138, "RX queue adds no native fields");

IOReturn ItlAwdlProbeNativeQueues(OSObject *, IOWorkLoop *, unsigned int, ItlAwdlNativeQueueResponse *);

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IOSkywalkPacketQueue.h"

class IOSkywalkTxSubmissionQueue : public IOSkywalkPacketQueue {
    OSDeclareDefaultStructors(IOSkywalkTxSubmissionQueue)
public:
    using Available = unsigned int (*)(OSObject *, IOSkywalkTxSubmissionQueue *, unsigned int *);
    using Dequeue = unsigned int (*)(OSObject *, IOSkywalkTxSubmissionQueue *,
        IOSkywalkPacket *const *, unsigned int, void *);
    void free() override;
    bool checkForWork() override;
    void enable() override;
    void disable() override;
    IOReturn initialize(void *) override;
    IOReturn finalize() override;
private:
    void _native_01c8(unsigned int) __asm__("__ZN26IOSkywalkTxSubmissionQueue11synchronizeEj") override;
    void _native_01d0(unsigned long long *, unsigned int *, unsigned int *)
        __asm__("__ZN26IOSkywalkTxSubmissionQueue16packetSubmissionEPyPjS1_") override;
    void _native_01d8(bool) __asm__("__ZN26IOSkywalkTxSubmissionQueue16packetSubmissionEb") override;
    void _native_01e0(unsigned int) __asm__("__ZN26IOSkywalkTxSubmissionQueue16sendNotificationEj") override;
public:
    unsigned int getPacketCount() const override;
    IOReturn performCommand(unsigned int, void *, unsigned long) override;
private:
    void _native_0220(IOSkywalkPacket *, IOSkywalkPacketQueue *, unsigned int)
        __asm__("__ZN26IOSkywalkTxSubmissionQueue16packetCompletionEP15IOSkywalkPacketP20IOSkywalkPacketQueuej") override;
    void _native_0238(unsigned int, unsigned int, bool)
        __asm__("__ZN26IOSkywalkTxSubmissionQueue16packetCompletionEjjb") override;
public:
    virtual IOReturn requestDequeue(void *, unsigned int);
private:
    virtual void _native_02a0() __asm__("__ZN26IOSkywalkTxSubmissionQueue12purgePacketsEv");
protected:
    virtual bool initWithPool(IOSkywalkPacketBufferPool *, unsigned int serviceClass,
        unsigned int capacity, unsigned int queueId, OSObject *, Available, Dequeue, void *, unsigned int flags);
private:
#define ITL_TX_RESERVED(n) virtual void _RESERVEDIOSkywalkTxSubmissionQueue##n();
    ITL_TX_RESERVED(0) ITL_TX_RESERVED(1) ITL_TX_RESERVED(2)
    ITL_TX_RESERVED(3) ITL_TX_RESERVED(4) ITL_TX_RESERVED(5)
    ITL_TX_RESERVED(6) ITL_TX_RESERVED(7) ITL_TX_RESERVED(8)
    ITL_TX_RESERVED(9) ITL_TX_RESERVED(10)
#undef ITL_TX_RESERVED
    uint8_t nativeStorage[0x168 - sizeof(IOSkywalkPacketQueue)];
};
static_assert(sizeof(IOSkywalkTxSubmissionQueue) == 0x168, "24H32 TX submission extent");

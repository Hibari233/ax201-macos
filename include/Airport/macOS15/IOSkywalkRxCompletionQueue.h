// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IOSkywalkPacketQueue.h"

class IOSkywalkRxCompletionQueue : public IOSkywalkPacketQueue {
    OSDeclareDefaultStructors(IOSkywalkRxCompletionQueue)
public:
    using Enqueue = unsigned int (*)(OSObject *, IOSkywalkRxCompletionQueue *,
        IOSkywalkPacket **, unsigned int, void *);
    void free() override;
    bool checkForWork() override;
    void enable() override;
    void disable() override;
    IOReturn initialize(void *) override;
    IOReturn finalize() override;
private:
    void _native_01c8(unsigned int) __asm__("__ZN26IOSkywalkRxCompletionQueue11synchronizeEj") override;
    void _native_01e0(unsigned int) __asm__("__ZN26IOSkywalkRxCompletionQueue16sendNotificationEj") override;
public:
    unsigned int getPacketCount() const override;
private:
    void _native_0210(IOSkywalkPacketPoller *)
        __asm__("__ZN26IOSkywalkRxCompletionQueue15setPacketPollerEP21IOSkywalkPacketPoller") override;
public:
    IOReturn performCommand(unsigned int, void *, unsigned long) override;
    virtual IOReturn requestEnqueue(void *, unsigned int);
    virtual IOReturn enqueuePackets(IOSkywalkPacket *const *, unsigned int, unsigned int);
private:
    virtual void _native_02a8(IOSkywalkPacket *, unsigned int, unsigned int)
        __asm__("__ZN26IOSkywalkRxCompletionQueue14enqueuePacketsEP15IOSkywalkPacketjj");
protected:
    virtual bool initWithPool(IOSkywalkPacketBufferPool *, unsigned int capacity, unsigned int queueId,
        OSObject *, Enqueue, void *, unsigned int flags);
private:
#define ITL_RX_RESERVED(n) virtual void _RESERVEDIOSkywalkRxCompletionQueue##n();
    ITL_RX_RESERVED(0) ITL_RX_RESERVED(1) ITL_RX_RESERVED(2)
    ITL_RX_RESERVED(3) ITL_RX_RESERVED(4) ITL_RX_RESERVED(5)
    ITL_RX_RESERVED(6) ITL_RX_RESERVED(7) ITL_RX_RESERVED(8)
    ITL_RX_RESERVED(9) ITL_RX_RESERVED(10)
#undef ITL_RX_RESERVED
    uint8_t nativeStorage[0x138 - sizeof(IOSkywalkPacketQueue)];
};
static_assert(sizeof(IOSkywalkRxCompletionQueue) == 0x138, "24H32 RX completion extent");

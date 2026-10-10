// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <IOKit/IOEventSource.h>
#include <IOKit/IOReturn.h>
class IOSkywalkInterface;
class IOSkywalkPacket;
class IOSkywalkPacketBuffer;
class IOSkywalkPacketBufferPool;
class IOSkywalkPacketPoller;

// 24H32 x86_64. Unused, unaudited entry points retain their native slots but
// are private aliases, not callable APIs. There is no driver-owned layout here.
class IOSkywalkPacketQueue : public IOEventSource {
    OSDeclareAbstractStructors(IOSkywalkPacketQueue)
public:
    struct QueueInfo { uint32_t type, capacity; IOSkywalkPacketBufferPool *pool; };
    void free() override;
    void disable() override;
    virtual IOReturn initialize(void *);
    virtual IOReturn finalize();
private:
    virtual void _native_01c8(unsigned int) __asm__("__ZN20IOSkywalkPacketQueue11synchronizeEj");
    virtual void _native_01d0(unsigned long long *, unsigned int *, unsigned int *)
        __asm__("__ZN20IOSkywalkPacketQueue16packetSubmissionEPyPjS1_");
    virtual void _native_01d8(bool) __asm__("__ZN20IOSkywalkPacketQueue16packetSubmissionEb");
    virtual void _native_01e0(unsigned int) __asm__("__ZN20IOSkywalkPacketQueue16sendNotificationEj");
public:
    virtual IOSkywalkPacketBufferPool *getPacketBufferPool() const;
private:
    virtual void _native_01f0(IOSkywalkPacketBufferPool *)
        __asm__("__ZN20IOSkywalkPacketQueue19setPacketBufferPoolEP25IOSkywalkPacketBufferPool");
public:
    virtual unsigned int getPacketCount() const = 0;
    virtual unsigned int getPacketBufferCount() const;
    virtual unsigned int getFreeSpace() const;
private:
    virtual void _native_0210(IOSkywalkPacketPoller *)
        __asm__("__ZN20IOSkywalkPacketQueue15setPacketPollerEP21IOSkywalkPacketPoller");
public:
    virtual IOReturn performCommand(unsigned int, void *, unsigned long);
private:
    virtual void _native_0220(IOSkywalkPacket *, IOSkywalkPacketQueue *, unsigned int)
        __asm__("__ZN20IOSkywalkPacketQueue16packetCompletionEP15IOSkywalkPacketPS_j");
    virtual void _native_0228(IOSkywalkPacketBuffer *, IOSkywalkPacketQueue *, unsigned int)
        __asm__("__ZN20IOSkywalkPacketQueue16bufferCompletionEP21IOSkywalkPacketBufferPS_j");
protected:
    virtual bool initWithPool(IOSkywalkPacketBufferPool *, unsigned int, unsigned int,
        unsigned int, unsigned int, OSObject *, IOEventSource::Action, void *);
private:
    virtual void _native_0238(unsigned int, unsigned int, bool)
        __asm__("__ZN20IOSkywalkPacketQueue16packetCompletionEjjb");
#define ITL_QUEUE_RESERVED(n) virtual void _RESERVEDIOSkywalkPacketQueue##n();
    ITL_QUEUE_RESERVED(0) ITL_QUEUE_RESERVED(1) ITL_QUEUE_RESERVED(2)
    ITL_QUEUE_RESERVED(3) ITL_QUEUE_RESERVED(4) ITL_QUEUE_RESERVED(5)
    ITL_QUEUE_RESERVED(6) ITL_QUEUE_RESERVED(7) ITL_QUEUE_RESERVED(8)
    ITL_QUEUE_RESERVED(9) ITL_QUEUE_RESERVED(10)
#undef ITL_QUEUE_RESERVED
    uint8_t nativeStorage[0xd8 - sizeof(IOEventSource)];
public:
    unsigned int getDirection() const;
    unsigned int getQueueId() const;
    IOSkywalkInterface *getInterface() const;
};
static_assert(sizeof(IOSkywalkPacketQueue) == 0xd8, "24H32 packet queue extent");
static_assert(sizeof(IOSkywalkPacketQueue::QueueInfo) == 16, "24H32 command 0x10000 output");

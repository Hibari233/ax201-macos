// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <IOKit/IOCommand.h>
#include <IOKit/IOReturn.h>
class IOSkywalkPacketBufferPool;
class IOSkywalkPacketDescriptor;
class IOSkywalkPacketBuffer;
class IOSkywalkPacketQueue;
class IOMemoryDescriptor;

class IOSkywalkPacket : public IOCommand {
    OSDeclareDefaultStructors(IOSkywalkPacket)
public:
    void free() override;
    virtual bool initWithPool(IOSkywalkPacketBufferPool *, IOSkywalkPacketDescriptor *, unsigned int);
    virtual unsigned int getPacketBuffers(IOSkywalkPacketBuffer **, unsigned int) const;
    virtual unsigned int getPacketBufferCount() const;
    virtual IOMemoryDescriptor *getMemoryDescriptor() const;
    virtual IOReturn setDataLength(unsigned int);
    virtual unsigned int getDataLength() const;
    virtual IOReturn setDataOffset(unsigned short);
    virtual unsigned short getDataOffset() const;
    virtual IOReturn setDataOffsetAndLength(unsigned short, unsigned int);
    virtual IOReturn setDataOff(long long);
    virtual long long getDataOff() const;
    virtual IOReturn setDataOffAndLen(long long, unsigned long);
    virtual void *getDataVirtualAddress() const;
private:
    // Preserve the slot without exposing an unaudited device-address API.
    virtual void _native_0180() const __asm__("__ZNK15IOSkywalkPacket23getDataIOVirtualAddressEv");
public:
    virtual IOReturn prepareWithQueue(IOSkywalkPacketQueue *, unsigned int, unsigned int);
    virtual IOReturn prepare(IOSkywalkPacketQueue *, unsigned long long, unsigned int);
    virtual IOReturn completeWithQueue(IOSkywalkPacketQueue *, unsigned int, unsigned int);
    virtual IOReturn complete(IOSkywalkPacketQueue *, unsigned int);
    virtual unsigned int getPacketType() const;
    virtual void setWakeFlag();
    virtual unsigned int getTraceID() const;
    virtual void setTraceID(unsigned int);
    virtual void traceEvent(unsigned int);
private:
    virtual void _native_01d0(IOSkywalkPacketQueue *) const __asm__("__ZNK15IOSkywalkPacket16generateTraceTagEP20IOSkywalkPacketQueue");
public:
    virtual void acquireWithPacketHandle(unsigned long long, unsigned int);
    virtual void disposePacket();
private:
    uint8_t nativeStorage[0x70 - sizeof(IOCommand)];
};
static_assert(sizeof(IOSkywalkPacket) == 0x70, "24H32 packet extent");

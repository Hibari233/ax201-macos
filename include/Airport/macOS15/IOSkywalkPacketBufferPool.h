// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <libkern/c++/OSObject.h>
#include <IOKit/IOReturn.h>

class IOSkywalkMemorySegment;
class IOSkywalkMemorySegmentDescriptor;
class IOSkywalkPacket;
class IOSkywalkPacketBuffer;
class IOSkywalkPacketDescriptor;
class IOSkywalkPacketBufferDescriptor;

// Exact 24H32 x86_64 declarations. Allocation/factory methods return IOReturn;
// release() has native pool teardown semantics and must not inherit OSObject's.
class IOSkywalkPacketBufferPool : public OSObject {
    OSDeclareDefaultStructors(IOSkywalkPacketBufferPool)
public:
    struct PoolOptions {
        uint32_t packetCount, bufferCount, bufferSize, maxBuffersPerPacket;
        uint32_t memorySegmentSize, poolFlags;
        const void *memorySegmentOptions; // Optional native DMA options; keep null.
    };
    void release() const override;
    void free() override;
    virtual bool initWithName(const char *, void *, unsigned int, const PoolOptions *);
    virtual bool initWithName(const char *, OSObject *, unsigned int, const PoolOptions *);
    virtual IOReturn allocatePacket(IOSkywalkPacket **, unsigned int);
    virtual IOReturn allocatePacket(unsigned int, IOSkywalkPacket **, unsigned int);
    virtual IOReturn allocatePackets(unsigned int, unsigned int *, IOSkywalkPacket **, unsigned int);
    virtual void deallocatePacket(IOSkywalkPacket *);
    virtual void deallocatePackets(IOSkywalkPacket **, unsigned int);
    virtual void deallocatePacketList(IOSkywalkPacket *);
    virtual void deallocatePacketChain(unsigned long long);
    virtual IOReturn allocatePacketBuffer(IOSkywalkPacketBuffer **, unsigned int);
    virtual IOReturn allocatePacketBuffers(unsigned int *, IOSkywalkPacketBuffer **, unsigned int);
    virtual void deallocatePacketBuffer(IOSkywalkPacketBuffer *);
    virtual void deallocatePacketBuffers(IOSkywalkPacketBuffer **, unsigned int);
    virtual IOReturn newPacket(IOSkywalkPacketDescriptor *, IOSkywalkPacket **);
    virtual IOReturn newPacketBuffer(IOSkywalkPacketBufferDescriptor *, IOSkywalkPacketBuffer **);
    virtual IOReturn newMemorySegment(IOSkywalkMemorySegmentDescriptor *, IOSkywalkMemorySegment **);
    static IOSkywalkPacketBufferPool *withName(const char *, OSObject *, unsigned int, const PoolOptions *);
private:
    uint8_t nativeStorage[0xb8 - sizeof(OSObject)];
};
static_assert(sizeof(IOSkywalkPacketBufferPool) == 0xb8, "24H32 pool extent");
static_assert(sizeof(IOSkywalkPacketBufferPool::PoolOptions) == 0x20, "24H32 pool input extent");

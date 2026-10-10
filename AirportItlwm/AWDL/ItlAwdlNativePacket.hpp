// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Airport/macOS15/IO80211NetworkPacket.h>
#include <Airport/macOS15/IOSkywalkPacketBufferPool.h>
#include <AWDL/ItlAwdlABI.h>

// Fixed single-buffer prototype. No queue, interface or firmware owns these
// packets yet. Reuse clears all driver metadata before a new packet lease.
class ItlAwdlNativePacket : public IO80211NetworkPacket {
    OSDeclareDefaultStructors(ItlAwdlNativePacket)
public:
    enum { BufferSize = 2048 };
    void acquireWithPacketHandle(unsigned long long, unsigned int) override;
    void *getVirtualAddress() override;
    unsigned int getBufferSize() override { return BufferSize; }
    void setPktEnqueueTime(unsigned long long value) override { enqueueTime = value; }
    unsigned long long getPktEnqueueTime() const override { return enqueueTime; }
    void setFirmwareTxStatus(IO80211NetworkTXStatus value) override { txStatus = value; }
    IO80211NetworkTXStatus getFirmwareTxStatus() override { return txStatus; }
    void setPTMMode(bool value) override { ptmMode = value; }
    bool isPTMMode() const override { return ptmMode; }
    void setIngressEgressTimestamp(unsigned long long value) override { timestamp = value; }
    unsigned long long getIngressEgressTimestamp() const override { return timestamp; }
    void setInterfaceID(int value) override { interfaceID = value; }
    int getInterfaceID() override { return interfaceID; }
private:
    unsigned long long enqueueTime, timestamp;
    IO80211NetworkTXStatus txStatus;
    int interfaceID;
    bool ptmMode;
};

class ItlAwdlNativePacketPool : public IOSkywalkPacketBufferPool {
    OSDeclareDefaultStructors(ItlAwdlNativePacketPool)
public:
    static ItlAwdlNativePacketPool *withOwner(OSObject *);
    static IOReturn probeCycle(OSObject *, unsigned int, ItlAwdlNativePacketResponse *);
    IOReturn newPacket(IOSkywalkPacketDescriptor *, IOSkywalkPacket **) override;
    IOReturn newMemorySegment(IOSkywalkMemorySegmentDescriptor *, IOSkywalkMemorySegment **) override;
private:
    uint32_t failureMode, packetFactories, segmentFactories, injectedFailures;
    bool initializePool(const char *, OSObject *, unsigned int);
    IOReturn checkBuffers(ItlAwdlNativePacketResponse *);
};

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IOSkywalkNetworkPacket.h"
enum IO80211NetworkTXStatus : unsigned int;

// Some native setters are panic stubs. A concrete driver packet must override
// the metadata accessors before this class is used by a native Wi-Fi interface.
class IO80211NetworkPacket : public IOSkywalkNetworkPacket {
    OSDeclareAbstractStructors(IO80211NetworkPacket)
public:
    IOReturn prepareWithQueue(IOSkywalkPacketQueue *, unsigned int, unsigned int) override;
    virtual IOReturn prepareWithQueue(IOSkywalkPacketQueue *, unsigned int);
    virtual void *getVirtualAddress();
    virtual unsigned int getBufferSize();
    virtual void setPktEnqueueTime(unsigned long long);
    virtual unsigned long long getPktEnqueueTime() const;
    virtual void setFirmwareTxStatus(IO80211NetworkTXStatus);
    virtual IO80211NetworkTXStatus getFirmwareTxStatus();
    virtual void setPTMMode(bool);
    virtual bool isPTMMode() const;
    virtual void setIngressEgressTimestamp(unsigned long long);
    virtual unsigned long long getIngressEgressTimestamp() const;
    virtual void setInterfaceID(int);
    virtual int getInterfaceID();
};
static_assert(sizeof(IO80211NetworkPacket) == 0x78, "24H32 Wi-Fi packet extent");

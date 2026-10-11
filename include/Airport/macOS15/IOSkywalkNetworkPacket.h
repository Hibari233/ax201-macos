// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "IOSkywalkPacket.h"
class IOSkywalkNetworkPacket : public IOSkywalkPacket {
    OSDeclareDefaultStructors(IOSkywalkNetworkPacket)
public:
    unsigned int getPacketType() const override;
    void setWakeFlag() override;
    unsigned int getTraceID() const override;
    void setTraceID(unsigned int) override;
    void traceEvent(unsigned int) override;
private:
    uint8_t nativeStorage[8];
};
static_assert(sizeof(IOSkywalkNetworkPacket) == 0x78, "24H32 network packet extent");

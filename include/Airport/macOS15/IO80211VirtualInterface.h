// SPDX-License-Identifier: GPL-2.0-or-later
// Reconstructed against macOS 15.8.1 (24H32), x86_64 IO80211Family.
// Unknown signatures are private, non-callable slot placeholders using
// the exact native symbol. Never call these aliases or infer their types.
#ifndef ITL_SEQUOIA_IO80211VIRTUALINTERFACE_H
#define ITL_SEQUOIA_IO80211VIRTUALINTERFACE_H
#include "IO80211SkywalkInterface.h"

class IO80211Buffer;
class IO80211BufferPool;

class IO80211VirtualInterface : public IO80211SkywalkInterface {
    OSDeclareAbstractStructors(IO80211VirtualInterface)
public:
    // vptr + 0x90: IO80211VirtualInterface::free()
    virtual void free() override;
    // vptr + 0x428: IO80211VirtualInterface::configureReport(IOReportChannelList*, unsigned int, void*, void*)
    virtual IOReturn configureReport(IOReportChannelList*, unsigned int, void*, void*) override;
    // vptr + 0x430: IO80211VirtualInterface::updateReport(IOReportChannelList*, unsigned int, void*, void*)
    virtual IOReturn updateReport(IOReportChannelList*, unsigned int, void*, void*) override;
    // vptr + 0x5c0: IO80211VirtualInterface::start(IOService*)
    virtual bool start(IOService*) override;
    // vptr + 0x5c8: IO80211VirtualInterface::stop(IOService*)
    virtual void stop(IOService*) override;
    // vptr + 0x600: IO80211VirtualInterface::terminate(unsigned int)
    virtual bool terminate(IOOptionBits = 0) override;
    // vptr + 0x670: IO80211VirtualInterface::attach(IOService*)
    virtual bool attach(IOService*) override;
    // vptr + 0x678: IO80211VirtualInterface::detach(IOService*)
    virtual void detach(IOService*) override;
    // vptr + 0x770: IO80211VirtualInterface::newUserClient(task*, void*, unsigned int, OSDictionary*, IOUserClient**)
    virtual IOReturn newUserClient(task*, void*, unsigned int, OSDictionary*, IOUserClient**) override;
    // vptr + 0x780: IO80211VirtualInterface::stringFromReturn(int)
    virtual const char* stringFromReturn(int) override;
    // vptr + 0x850: IO80211VirtualInterface::enable(unsigned int)
    virtual IOReturn enable(unsigned int) override;
    // vptr + 0x8e8: IO80211VirtualInterface::prepareBSDInterface(__ifnet*, unsigned int)
    virtual IOReturn prepareBSDInterface(struct __ifnet*, unsigned int) override;
    // vptr + 0x910: IO80211VirtualInterface::processBSDCommand(__ifnet*, unsigned int, void*)
    virtual IOReturn processBSDCommand(struct __ifnet*, unsigned int, void*) override;
    // vptr + 0x928: IO80211VirtualInterface::setInterfaceEnable(bool)
    virtual SInt32 setInterfaceEnable(bool) override;
    // vptr + 0x9b0: IO80211VirtualInterface::getFeatureFlags()
    virtual UInt32 getFeatureFlags() override;
    // vptr + 0x9d8: IO80211VirtualInterface::bpfTap(unsigned int, unsigned int)
    virtual bool bpfTap(unsigned int, unsigned int) override;
    // vptr + 0xa80: IO80211VirtualInterface::setMulticastAddresses(ether_addr const*, unsigned int)
    virtual bool setMulticastAddresses(ether_addr const*, unsigned int) override;
    // vptr + 0xb00: IO80211VirtualInterface::createPeerManager()
    virtual void * createPeerManager() override;
    // vptr + 0xb10: IO80211VirtualInterface::postMessage(unsigned int, void*, unsigned long, bool)
    virtual void postMessage(unsigned int, void*, unsigned long, bool) override;
    // vptr + 0xb30: IO80211VirtualInterface::logTxPacket(IO80211NetworkPacket*, PacketSkywalkScratch*, apple80211_wme_ac, bool)
    virtual void logTxPacket(IO80211NetworkPacket*, PacketSkywalkScratch*, apple80211_wme_ac, bool) override;
    // vptr + 0xb38: IO80211VirtualInterface::logTxCompletionPacket(IO80211NetworkPacket*, PacketSkywalkScratch*, unsigned char*, apple80211_wme_ac, int, unsigned int, bool, bool)
    virtual void logTxCompletionPacket(IO80211NetworkPacket*, PacketSkywalkScratch*, unsigned char*, apple80211_wme_ac, int, unsigned int, bool, bool) override;
    // vptr + 0xb48: IO80211VirtualInterface::inputPacket(IO80211NetworkPacket*, packet_info_tag*, ether_header*, bool*, bool)
    virtual IOReturn inputPacket(IO80211NetworkPacket*, packet_info_tag*, ether_header*, bool*, bool) override;
    // vptr + 0xb58: IO80211VirtualInterface::logSkywalkTxReqPacket(IO80211NetworkPacket*, PacketSkywalkScratch*, unsigned char*, apple80211_wme_ac, bool)
    virtual void logSkywalkTxReqPacket(IO80211NetworkPacket*, PacketSkywalkScratch*, unsigned char*, apple80211_wme_ac, bool) override;
    // vptr + 0xb60: IO80211VirtualInterface::pendingPackets(unsigned char)
    virtual SInt64 pendingPackets(unsigned char) override;
    // vptr + 0xb68: IO80211VirtualInterface::packetSpace(unsigned char)
    virtual SInt64 packetSpace(unsigned char) override;
    // vptr + 0xb88: IO80211VirtualInterface::linkState()
    virtual IO80211LinkState linkState() override;
    // vptr + 0xb90: IO80211VirtualInterface::setScanningState(unsigned int, bool, apple80211_scan_data*, int)
    virtual void setScanningState(unsigned int, bool, apple80211_scan_data*, int) override;
    // vptr + 0xbb0: IO80211VirtualInterface::updateLinkParameters(apple80211_interface_availability*)
    virtual void updateLinkParameters(apple80211_interface_availability*) override;
    // vptr + 0xbb8: IO80211VirtualInterface::updateInterfaceCoexRiskPct(unsigned long long)
    virtual void updateInterfaceCoexRiskPct(unsigned long long) override;
    // vptr + 0xbe8: IO80211VirtualInterface::setInterfaceNF(apple80211_channel, long long)
    virtual void setInterfaceNF(apple80211_channel, long long) override;
    // vptr + 0xc00: IO80211VirtualInterface::setDebugFlags(unsigned long long, unsigned int)
    virtual void setDebugFlags(unsigned long long, unsigned int) override;
    // vptr + 0xc08: IO80211VirtualInterface::debugFlags()
    virtual SInt64 debugFlags() override;
    // vptr + 0xc10: IO80211VirtualInterface::setInterfaceChipCounters(apple80211_stat_report*, apple80211_chip_counters_tx*, apple80211_chip_error_counters_tx*, apple80211_chip_counters_rx*)
    virtual void setInterfaceChipCounters(apple80211_stat_report*, apple80211_chip_counters_tx*, apple80211_chip_error_counters_tx*, apple80211_chip_counters_rx*) override;
    // vptr + 0xc18: IO80211VirtualInterface::setInterfaceMIBdot11(apple80211_stat_report*, apple80211_ManagementInformationBasedot11_counters*)
    virtual void setInterfaceMIBdot11(apple80211_stat_report*, apple80211_ManagementInformationBasedot11_counters*) override;
    // vptr + 0xc20: IO80211VirtualInterface::setFrameStats(apple80211_stat_report*, apple80211_frame_counters*)
    virtual void setFrameStats(apple80211_stat_report*, apple80211_frame_counters*) override;
    // vptr + 0xc28: IO80211VirtualInterface::setInfraSpecificFrameStats(apple80211_stat_report*, apple80211_infra_specific_stats*)
    virtual void setInfraSpecificFrameStats(apple80211_stat_report*, apple80211_infra_specific_stats*) override;
    // vptr + 0xc30: IO80211VirtualInterface::getWmeTxCounters(unsigned long long*)
    virtual SInt64 getWmeTxCounters(unsigned long long*) override;
    // vptr + 0xc40: IO80211VirtualInterface::setWoWEnabled(bool)
    virtual void setWoWEnabled(bool) override;
    // vptr + 0xc48: IO80211VirtualInterface::wowEnabled()
    virtual bool wowEnabled() override;
    // vptr + 0xc50: IO80211VirtualInterface::printDataPath(userPrintCtx*)
    virtual void printDataPath(userPrintCtx*) override;
    // vptr + 0xc60: IO80211VirtualInterface::findOrCreateFlowQueue(IO80211FlowQueueHash)
    private: virtual void _abi_0c68() override __asm__("__ZN23IO80211VirtualInterface21findOrCreateFlowQueueE20IO80211FlowQueueHash"); public:
    // vptr + 0xc68: IO80211VirtualInterface::findOrCreateFlowQueueWithCache(IO80211FlowQueueHash, bool*)
    private: virtual void _abi_0c70() override __asm__("__ZN23IO80211VirtualInterface30findOrCreateFlowQueueWithCacheE20IO80211FlowQueueHashPb"); public:
    // vptr + 0xc70: IO80211VirtualInterface::findExistingFlowQueue(IO80211FlowQueueHash)
    private: virtual void _abi_0c78() override __asm__("__ZN23IO80211VirtualInterface21findExistingFlowQueueE20IO80211FlowQueueHash"); public:
    // vptr + 0xc78: IO80211VirtualInterface::removePacketQueue(IO80211FlowQueueHash const*)
    private: virtual void _abi_0c80() override __asm__("__ZN23IO80211VirtualInterface17removePacketQueueEPK20IO80211FlowQueueHash"); public:
    // vptr + 0xc80: IO80211VirtualInterface::flushPacketQueues()
    virtual void flushPacketQueues() override;
    // vptr + 0xc88: IO80211VirtualInterface::cachePeer(ether_addr*, unsigned int*)
    virtual void cachePeer(ether_addr*, unsigned int*) override;
    // vptr + 0xc90: IO80211VirtualInterface::shouldLog(unsigned long long)
    virtual bool shouldLog(unsigned long long) override;
    // vptr + 0xc98: IO80211VirtualInterface::vlogDebug(unsigned long long, char const*, __va_list_tag*)
    virtual void vlogDebug(unsigned long long, char const*, va_list) override;
    // vptr + 0xca0: IO80211VirtualInterface::vlogDebugBPF(unsigned long long, char const*, __va_list_tag*)
    virtual void vlogDebugBPF(unsigned long long, char const*, va_list) override;
    // vptr + 0xcb8: IO80211VirtualInterface::getP2PSkywalkPeerMgr()
    virtual void * getP2PSkywalkPeerMgr() override;
    // vptr + 0xcc8: IO80211VirtualInterface::findPeer(ether_addr&)
    private: virtual void _abi_0cd0() override __asm__("__ZN23IO80211VirtualInterface8findPeerER10ether_addr"); public:
    // vptr + 0xdb8: IO80211VirtualInterface::getEventPipeSize()
    virtual int getEventPipeSize() override;
    // vptr + 0xdc0: IO80211VirtualInterface::createEventPipe(IO80211APIUserClient*)
    virtual UInt64 createEventPipe(IO80211APIUserClient*) override;
    // vptr + 0xe60: IO80211VirtualInterface::init(IO80211Controller*, ether_addr*, unsigned int, char const*)
    virtual bool init(IO80211Controller*, ether_addr*, unsigned int, const char*);
    // vptr + 0xe68: IO80211VirtualInterface::setLinkState(IO80211LinkState, unsigned int)
    private: virtual void _native_0e68() __asm__("__ZN23IO80211VirtualInterface12setLinkStateE16IO80211LinkStatej"); public:
    // vptr + 0xe70: IO80211VirtualInterface::forwardPacket(IO80211NetworkPacket*)
    private: virtual void _native_0e70() __asm__("__ZN23IO80211VirtualInterface13forwardPacketEP20IO80211NetworkPacket"); public:
    // vptr + 0xe78: IO80211VirtualInterface::handleIoctl(__ifnet*, unsigned long, void*)
    private: virtual void _native_0e78() __asm__("__ZN23IO80211VirtualInterface11handleIoctlEP7__ifnetmPv"); public:
    // vptr + 0xe80: IO80211VirtualInterface::addPeerToCache(unsigned char*)
    private: virtual void _native_0e80() __asm__("__ZN23IO80211VirtualInterface14addPeerToCacheEPh"); public:
    // vptr + 0xe88: IO80211VirtualInterface::deletePeerFromCache(unsigned char*)
    private: virtual void _native_0e88() __asm__("__ZN23IO80211VirtualInterface19deletePeerFromCacheEPh"); public:
    // vptr + 0xe90: IO80211VirtualInterface::handleDebugCmd(apple80211_debug_command*)
    private: virtual void _native_0e90() __asm__("__ZN23IO80211VirtualInterface14handleDebugCmdEP24apple80211_debug_command"); public:
    // vptr + 0xe98: IO80211VirtualInterface::getPeerSlotDataStats(ether_addr*, bool, peerSlotDataStats*)
    private: virtual void _native_0e98() __asm__("__ZN23IO80211VirtualInterface20getPeerSlotDataStatsEP10ether_addrbP17peerSlotDataStats"); public:
    // vptr + 0xea0: IO80211VirtualInterface::getPeerDataStats(ether_addr*, bool, packetCounters_t*)
    private: virtual void _native_0ea0() __asm__("__ZN23IO80211VirtualInterface16getPeerDataStatsEP10ether_addrbP16packetCounters_t"); public:
    // vptr + 0xea8: IO80211VirtualInterface::clearPeerDataStats(ether_addr*, bool)
    private: virtual void _native_0ea8() __asm__("__ZN23IO80211VirtualInterface18clearPeerDataStatsEP10ether_addrb"); public:
    // vptr + 0xeb0: IO80211VirtualInterface::setAwdlCurrentChannelSequenceIndex(unsigned int)
    private: virtual void _native_0eb0() __asm__("__ZN23IO80211VirtualInterface34setAwdlCurrentChannelSequenceIndexEj"); public:
    // vptr + 0xeb8: IO80211VirtualInterface::isOutputFlowControlled()
    private: virtual void _native_0eb8() __asm__("__ZN23IO80211VirtualInterface22isOutputFlowControlledEv"); public:
    // vptr + 0xec0: IO80211VirtualInterface::setOutputFlowControlled()
    private: virtual void _native_0ec0() __asm__("__ZN23IO80211VirtualInterface23setOutputFlowControlledEv"); public:
    // vptr + 0xec8: IO80211VirtualInterface::clearOutputFlowControlled()
    private: virtual void _native_0ec8() __asm__("__ZN23IO80211VirtualInterface25clearOutputFlowControlledEv"); public:
    // vptr + 0xed0: IO80211VirtualInterface::configureAQMOutput()
    private: virtual void _native_0ed0() __asm__("__ZN23IO80211VirtualInterface18configureAQMOutputEv"); public:
    // vptr + 0xed8: IO80211VirtualInterface::attachToBpf()
    virtual bool attachToBpf();
    // vptr + 0xee0: IO80211VirtualInterface::configureIfnet()
    virtual void configureIfnet();
    // vptr + 0xee8: IO80211VirtualInterface::reset()
    private: virtual void _native_0ee8() __asm__("__ZN23IO80211VirtualInterface5resetEv"); public:
    // vptr + 0xef0: IO80211VirtualInterface::dupAndTransmitMcastPacket(IO80211NetworkPacket*, unsigned char*, unsigned char*)
    private: virtual void _native_0ef0() __asm__("__ZN23IO80211VirtualInterface25dupAndTransmitMcastPacketEP20IO80211NetworkPacketPhS2_"); public:
    // vptr + 0xef8: IO80211VirtualInterface::freeMulticastPacket(IO80211NetworkPacket*)
    private: virtual void _native_0ef8() __asm__("__ZN23IO80211VirtualInterface19freeMulticastPacketEP20IO80211NetworkPacket"); public:
    // vptr + 0xf00: IO80211VirtualInterface::triggerAWDLMulticastTx(unsigned int, unsigned char)
    private: virtual void _native_0f00() __asm__("__ZN23IO80211VirtualInterface22triggerAWDLMulticastTxEjh"); public:
    // vptr + 0xf08: IO80211VirtualInterface::p2pMulticastTx(IO80211NetworkPacket**, unsigned int)
    private: virtual void _native_0f08() __asm__("__ZN23IO80211VirtualInterface14p2pMulticastTxEPP20IO80211NetworkPacketj"); public:
    // vptr + 0xf10: IO80211VirtualInterface::sendPendingPacketsToStack(IO80211NetworkPacket**, unsigned int)
    private: virtual void _native_0f10() __asm__("__ZN23IO80211VirtualInterface25sendPendingPacketsToStackEPP20IO80211NetworkPacketj"); public:
    // vptr + 0xf18: IO80211VirtualInterface::freePendingPackets(IO80211NetworkPacket**, unsigned int)
    private: virtual void _native_0f18() __asm__("__ZN23IO80211VirtualInterface18freePendingPacketsEPP20IO80211NetworkPacketj"); public:
    // vptr + 0xf20: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface0()
    private: virtual void _RESERVEDIO80211VirtualInterface0(); public:
    // vptr + 0xf28: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface1()
    private: virtual void _RESERVEDIO80211VirtualInterface1(); public:
    // vptr + 0xf30: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface2()
    private: virtual void _RESERVEDIO80211VirtualInterface2(); public:
    // vptr + 0xf38: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface3()
    private: virtual void _RESERVEDIO80211VirtualInterface3(); public:
    // vptr + 0xf40: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface4()
    private: virtual void _RESERVEDIO80211VirtualInterface4(); public:
    // vptr + 0xf48: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface5()
    private: virtual void _RESERVEDIO80211VirtualInterface5(); public:
    // vptr + 0xf50: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface6()
    private: virtual void _RESERVEDIO80211VirtualInterface6(); public:
    // vptr + 0xf58: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface7()
    private: virtual void _RESERVEDIO80211VirtualInterface7(); public:
    // vptr + 0xf60: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface8()
    private: virtual void _RESERVEDIO80211VirtualInterface8(); public:
    // vptr + 0xf68: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface9()
    private: virtual void _RESERVEDIO80211VirtualInterface9(); public:
    // vptr + 0xf70: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface10()
    private: virtual void _RESERVEDIO80211VirtualInterface10(); public:
    // vptr + 0xf78: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface11()
    private: virtual void _RESERVEDIO80211VirtualInterface11(); public:
    // vptr + 0xf80: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface12()
    private: virtual void _RESERVEDIO80211VirtualInterface12(); public:
    // vptr + 0xf88: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface13()
    private: virtual void _RESERVEDIO80211VirtualInterface13(); public:
    // vptr + 0xf90: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface14()
    private: virtual void _RESERVEDIO80211VirtualInterface14(); public:
    // vptr + 0xf98: IO80211VirtualInterface::_RESERVEDIO80211VirtualInterface15()
    private: virtual void _RESERVEDIO80211VirtualInterface15(); public:
public:
    // Nonvirtual native helper, independently inspected in 24H32. It frees
    // the BPF list/lock acquired by init(); free() alone does not free them.
    void freeBpfResources();
    // Borrowed controller pool, not a new pool and not a Skywalk data pool.
    IO80211BufferPool *getMgmtFramePool();
    // For type 12, native peer input consumes or queues the lease. All paths
    // return it natively; the caller must neither release nor return it again.
    void bpfTapInput(IO80211Buffer *, unsigned int, void *, unsigned long);
protected:
    bool hasNativeVirtualStorage() const {
        void *storage = nullptr;
        memcpy(&storage, _virtualLayout, sizeof(storage));
        return storage != nullptr;
    }
private:
    uint8_t _virtualLayout[8]; // Expansion pointer at 0x118.
};
static_assert(sizeof(IO80211VirtualInterface) == 0x120, "Sequoia VirtualInterface size");
#endif

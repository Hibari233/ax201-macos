// SPDX-License-Identifier: GPL-2.0-or-later
// Compile-only fixture. Never linked into or installed as a driver.
#include <Airport/Apple80211.h>
#include <Airport/macOS15/IO80211AWDLProtocol.h>
#include <Airport/macOS15/IO80211NetworkPacket.h>
#include <Airport/macOS15/IOSkywalkTxSubmissionQueue.h>
#include <Airport/macOS15/IOSkywalkRxCompletionQueue.h>
#include <Airport/macOS15/IO80211ManagementBuffer.h>
#include <AWDL/AwdlNativeEvents.h>
#include <AWDL/AwdlSyncAction.h>

// Compile the portable encoder using kernel flags as well as the host tests.
size_t sequoiaSyncActionCompileFixture(const ItlAwdl::SyncActionContext &context,
    const ItlAwdl::SyncActionParameters &parameters, const ItlAwdl::ActionTemplate &extra,
    const ItlAwdl::NativeBssidConfiguration &network,
    uint8_t *output, size_t capacity) {
    return ItlAwdl::buildSyncAction(context, parameters, extra, network, output, capacity);
}

// Force compiler vtables for every packet base, including the pool release
// override. Compare these object-file relocations to the target's native ABI.
#define ABI_FIXTURE(name, base) \
    class name : public base { OSDeclareDefaultStructors(name) }; \
    OSDefineMetaClassAndStructors(name, base)
ABI_FIXTURE(SequoiaPacketPoolAbiTest, IOSkywalkPacketBufferPool)
ABI_FIXTURE(SequoiaPacketAbiTest, IOSkywalkPacket)
ABI_FIXTURE(SequoiaNetworkPacketAbiTest, IOSkywalkNetworkPacket)
ABI_FIXTURE(SequoiaWifiPacketAbiTest, IO80211NetworkPacket)
ABI_FIXTURE(SequoiaTxQueueAbiTest, IOSkywalkTxSubmissionQueue)
ABI_FIXTURE(SequoiaRxQueueAbiTest, IOSkywalkRxCompletionQueue)
ABI_FIXTURE(SequoiaManagementPoolAbiTest, IO80211BufferPool)
ABI_FIXTURE(SequoiaManagementBufferAbiTest, IO80211Buffer)
#undef ABI_FIXTURE
static_assert(sizeof(SequoiaPacketPoolAbiTest) == 0xb8, "Pool fixture extent");
static_assert(sizeof(SequoiaPacketAbiTest) == 0x70, "Packet fixture extent");
static_assert(sizeof(SequoiaNetworkPacketAbiTest) == 0x78, "Network packet fixture extent");
static_assert(sizeof(SequoiaWifiPacketAbiTest) == 0x78, "Wi-Fi packet fixture extent");
static_assert(sizeof(SequoiaTxQueueAbiTest) == 0x168, "TX queue fixture extent");
static_assert(sizeof(SequoiaRxQueueAbiTest) == 0x138, "RX queue fixture extent");
static_assert(sizeof(SequoiaManagementPoolAbiTest) == 0x10, "Management pool fixture extent");
static_assert(sizeof(SequoiaManagementBufferAbiTest) == 0x18, "Management buffer fixture extent");
static_assert(__is_same(decltype(static_cast<IOSkywalkPacketQueue *>(nullptr)->initialize(nullptr)),
                        IOReturn), "Queue initialization returns IOReturn");
static_assert(__is_same(decltype(static_cast<IOSkywalkRxCompletionQueue *>(nullptr)->enqueuePackets(nullptr, 0, 0)),
                        IOReturn), "RX enqueue returns IOReturn, not an accepted count");
static_assert(__is_same(decltype(static_cast<IOSkywalkPacketBufferPool *>(nullptr)->allocatePacket(nullptr, 0)),
                        IOReturn), "Pool allocation returns IOReturn");
static_assert(__is_same(decltype(static_cast<IOSkywalkPacket *>(nullptr)->prepare(nullptr, 0, 0)),
                        IOReturn), "Packet preparation returns IOReturn");

// 24H32 ioctl length checks and native caller/backend accesses were audited
// independently. These would catch the old 20-byte/8-byte legacy declarations.
static_assert(sizeof(apple80211_awdl_sync_params) == 0x18, "Sequoia sync params length");
static_assert(sizeof(apple80211_awdl_bssid) == 16, "Sequoia BSSID pair length");
static_assert(__offsetof(apple80211_awdl_bssid, bssid) == 4, "Sequoia AWDL BSSID offset");
static_assert(__offsetof(apple80211_awdl_bssid, unk_mac) == 10, "Sequoia companion BSSID offset");
static_assert(__offsetof(apple80211_awdl_sync_params, unknown_14) == 0x14, "Sequoia sync params tail");
static_assert(sizeof(apple80211_awdl_sync_state) == 0x18, "Sequoia sync state length");
static_assert(__offsetof(apple80211_awdl_sync_state, opaque_08) == 8, "Sequoia sync state tail");
static_assert(sizeof(apple80211_awdl_sync_enabled) == 12, "Sequoia sync enable extent");
static_assert(__offsetof(apple80211_awdl_sync_enabled, enabled) == 8, "Sequoia sync enabled field");
static_assert(sizeof(apple80211_awdl_sync_frame_template) == 16, "Sequoia frame template extent");
static_assert(__offsetof(apple80211_awdl_sync_frame_template, payload) == 8, "Sequoia template pointer");
static_assert(__is_same(decltype(static_cast<IO80211SkywalkInterface *>(nullptr)->getSelfMacAddr()),
                        ether_addr), "Sequoia self MAC returns by value");
static_assert(__is_same(decltype(static_cast<IOSkywalkEthernetInterface *>(nullptr)->registerEthernetInterface(
                        nullptr, nullptr, 0, nullptr, nullptr, 0)), IOReturn),
                        "Sequoia registration returns zero-success IOReturn, not bool");

static_assert(__is_same(decltype(static_cast<IOSkywalkNetworkInterface *>(nullptr)->deferBSDAttach(true)),
                        IOReturn), "Sequoia deferred BSD attach returns IOReturn");
static_assert(__is_same(decltype(static_cast<IOSkywalkEthernetInterface *>(nullptr)->deregisterEthernetInterface(0)),
                        IOReturn), "Sequoia deregistration returns IOReturn");

static_assert(__is_same(decltype(static_cast<IO80211VirtualInterface *>(nullptr)->attachToBpf()),
                        bool), "Sequoia BPF attach returns bool");
static_assert(__is_same(decltype(static_cast<IO80211VirtualInterface *>(nullptr)->configureIfnet()),
                        void), "Sequoia ifnet configuration return is not a readiness result");
static_assert(__is_same(decltype(static_cast<IO80211SkywalkInterface *>(nullptr)->reportDataPathEvents(
                        0, nullptr, 0, false)), bool), "24H32 event dispatch returns gate-failure bool");
static_assert(APPLE80211_M_AWDL_AVAILABILITY_WINDOW_START == ItlAwdl::NativeAvailabilityWindowStartEvent,
              "24H32 AW start event is 41, not extension-start 42");
static_assert(APPLE80211_M_AWDL_AVAILABILITY_WINDOW_EXTENSION_START == ItlAwdl::NativeAvailabilityWindowExtensionStartEvent,
              "24H32 AW extension event");
static_assert(APPLE80211_M_AWDL_AVAILABILITY_WINDOW_EXTENSIONS_END == ItlAwdl::NativeAvailabilityWindowEndEvent,
              "24H32 AW end event");
static_assert(APPLE80211_M_AWDL_SYNC_STATE_CHANGED == ItlAwdl::NativeSyncStateChangedEvent,
              "24H32 sync state event");

class SequoiaAwdlAbiTest : public IO80211AWDLProtocol {
    OSDeclareDefaultStructors(SequoiaAwdlAbiTest)
public:
    IOReturn setMacAddress(ether_addr&) override { return kIOReturnUnsupported; }
    IOReturn getSSID(apple80211_ssid_data*) override { return kIOReturnUnsupported; }
    IOReturn getCHANNEL(apple80211_channel_data*) override { return kIOReturnUnsupported; }
    IOReturn getBSSID(apple80211_bssid_data*) override { return kIOReturnUnsupported; }
    IOReturn getSTATE(apple80211_state_data*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_SYNC_PARAMS(apple80211_awdl_sync_params*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_ELECTION_ID(apple80211_awdl_election_id*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_ELECTION_METRIC(apple80211_awdl_election_metric*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence*) override { return kIOReturnUnsupported; }
    IOReturn getPEER_CACHE_MAXIMUM_SIZE(apple80211_peer_cache_maximum_size*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_SYNC_STATE(apple80211_awdl_sync_state*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_ELECTION_PARAMS(apple80211_awdl_election_params*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_DEVICE_CAPABILITIES(apple80211_awdl_device_capabilities*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_SECONDARY_MASTER_CHANNEL(apple80211_awdl_secondary_master_channel*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_MIN_RATE(apple80211_awdl_min_rate*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_ENCRYPTION_TYPE(apple80211_encrypt*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_DEV_STATS(apple80211_awdl_dev_stats*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_CCA(apple80211_awdl_cca*) override { return kIOReturnUnsupported; }
    IOReturn getAWDL_AF_SECONDARY_PAYLOAD_CAPABILITIES(apple80211_awdl_cap*) override { return kIOReturnUnsupported; }
    IOReturn getLLW_PARAMS(apple80211_llw_params*) override { return kIOReturnUnsupported; }
    IOReturn setSSID(apple80211_ssid_data*) override { return kIOReturnUnsupported; }
    IOReturn setCHANNEL(apple80211_channel_data*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_SYNC_PARAMS(apple80211_awdl_sync_params*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_SYNC_ENABLED(apple80211_awdl_sync_enabled*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_EXTENSION_STATE_MACHINE_PARAMETERS(apple80211_awdl_extension_state_machine_parameter*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_ELECTION_ID(apple80211_awdl_election_id*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_BSSID(apple80211_awdl_bssid*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_ELECTION_METRIC(apple80211_awdl_election_metric*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE(apple80211_awdl_sync_channel_sequence*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_MASTER_CHANNEL(apple80211_awdl_master_channel*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_SYNC_STATE(apple80211_awdl_sync_state*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_ELECTION_RSSI_THRESHOLDS(apple80211_awdl_election_rssi_thresholds*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_PRESENCE_MODE(apple80211_awdl_presence_mode*) override { return kIOReturnUnsupported; }
    IOReturn setPEER_CACHE_CONTROL(apple80211_peer_cache_control*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_OOB_REQUEST(apple80211_awdl_oob_request*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_SYNC_FRAME_TEMPLATE(apple80211_awdl_sync_frame_template*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_ELECTION_PARAMS(apple80211_awdl_election_params*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_PEER_TRAFFIC_REGISTRATION(apple80211_awdl_peer_traffic_registration*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_SECONDARY_MASTER_CHANNEL(apple80211_awdl_secondary_master_channel*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_AF_TX_MODE(apple80211_awdl_af_tx_mode*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_MIN_RATE(apple80211_awdl_min_rate*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_ENCRYPTION_KEYS(apple80211_key*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_ENCRYPTION_TYPE(apple80211_encrypt*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_OOB_AUTO_REQUEST(apple80211_awdl_oob_request*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_TXCAL_PERIOD(apple80211_awdl_txcal_period*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_AWDL_ADVERTISERS(awdl_peer_advert_add*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_DFSP_CONFIG(apple80211_awdl_dfsp_config*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_DFSP_UCSA_CONFIG(apple80211_awdl_dfsp_ucsa_config*) override { return kIOReturnUnsupported; }
    IOReturn setCOMPANION_SKYWALK_IF_CREATE(apple80211_companion_skywalk_if_create*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_OOB_SEC_PAYLOAD_REQUEST(apple80211_awdl_af_sec_payload_request*) override { return kIOReturnUnsupported; }
    IOReturn setLLW_PARAMS(apple80211_llw_params*) override { return kIOReturnUnsupported; }
    IOReturn setTX_RATE(apple80211_tx_rate*) override { return kIOReturnUnsupported; }
    IOReturn setP2P_COEX_PROTECTION(apple80211_p2p_coex_protection*) override { return kIOReturnUnsupported; }
    IOReturn setPSF_DWELL(apple80211_awdl_psf_dwell_params*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_CONFIG(apple80211_awdl_config_request*) override { return kIOReturnUnsupported; }
    IOReturn setAWDL_RTG_PEER_STATS_CONFIG(apple80211_rtg_peer_stats_cfg*) override { return kIOReturnUnsupported; }
};
OSDefineMetaClassAndStructors(SequoiaAwdlAbiTest, IO80211AWDLProtocol)
static_assert(sizeof(SequoiaAwdlAbiTest) == 0x120, "Test fixture must add no fields");

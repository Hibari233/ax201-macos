// SPDX-License-Identifier: GPL-2.0-or-later
// Experimental, privileged AX201 AWDL radio diagnostics. Not an AirDrop ABI.
#ifndef ITL_AWDL_ABI_H
#define ITL_AWDL_ABI_H
#include <stdint.h>

enum {
    ITL_AWDL_USER_CLIENT = 0x4157444c,
    ITL_AWDL_ABI_VERSION = 1,
    ITL_AWDL_MAX_FRAME = 2048,
    ITL_AWDL_RING_COUNT = 16,
    ITL_AWDL_MAX_DWELL_MS = 30000,
    ITL_AWDL_MAX_EVENTS = 32,
    ITL_AWDL_DATA_MTU = 1500,
};
enum ItlAwdlMethod { ItlAwdlStatus = 0, ItlAwdlBegin, ItlAwdlEnd, ItlAwdlRead, ItlAwdlReadRejected, ItlAwdlProbe, ItlAwdlTxStatus, ItlAwdlRegStatus, ItlAwdlReadTimed, ItlAwdlClock, ItlAwdlRunTest, ItlAwdlRunStatus, ItlAwdlPrepareCountry, ItlAwdlBeginLinked, ItlAwdlLinkedStatus, ItlAwdlBeginData, ItlAwdlReadData, ItlAwdlDataStatus, ItlAwdlDataProbe, ItlAwdlNativeInit, ItlAwdlNativeStart, ItlAwdlNativeSession, ItlAwdlNativePackets, ItlAwdlNativeQueues, ItlAwdlNativeTransport, ItlAwdlMethodCount };
enum ItlAwdlSession { ItlAwdlIdle = 0, ItlAwdlStarting, ItlAwdlListening, ItlAwdlStopping };
enum ItlAwdlStopReason { ItlAwdlNotStopped = 0, ItlAwdlClosed, ItlAwdlExpired, ItlAwdlRadioError, ItlAwdlPowerChange, ItlAwdlDetached, ItlAwdlWifiRequest, ItlAwdlLinkChanged };

struct ItlAwdlBeginRequest {
    uint32_t version;
    uint32_t channel;
    uint32_t dwell_ms;
    uint32_t reserved;
    uint8_t peer[6]; // Required unicast AWDL MAC of the user's test peer.
    uint8_t padding[2];
};
struct ItlAwdlStatusResponse {
    uint32_t version, session, opmode, net_state;
    uint32_t channel, if_flags, last_error, stop_reason;
    uint64_t rx_total, rx_awdl, rx_peer, rx_dropped, rx_invalid;
    uint32_t device_id, driver_flags, queued, capabilities;
    uint8_t local_mac[6];
    uint8_t reserved[10];
};
struct ItlAwdlFrameResponse {
    uint32_t version, length, channel;
    int32_t rssi_dbm;
    uint64_t timestamp_us;
    uint8_t frame[ITL_AWDL_MAX_FRAME]; // 802.11 management action frame, no radiotap/FCS.
};
struct ItlAwdlTxStatusResponse {
    uint32_t version, submitted, completed, succeeded;
    uint32_t failed, last_firmware_status, last_host_error, pending, aborted;
};
struct ItlAwdlTimedFrameResponse {
    ItlAwdlFrameResponse frame; // Existing selector 3 still returns this prefix.
    uint32_t clock_flags; // bit 0: firmware RX-on-air timestamps present.
    uint32_t gp2_us; // Local device clock, wraps at 2^32 us; resets on radio init.
    uint64_t tsf_us; // Firmware RX TSF, kept separate from GP2 and host uptime.
};
struct ItlAwdlRegStatusResponse {
    uint32_t version, channel, channel_flags, query_count, last_query_error;
    char firmware_country[4];
};
// Receive-only diagnostic on the existing STA channel. No off-channel TX.
// BeginLinked uses BeginRequest with channel == 0 (current associated channel).
struct ItlAwdlLinkedStatusResponse {
    uint32_t version, linked_mode, firmware_generation, start_generation;
    uint32_t channel, filter_enabled, restore_error, link_matches;
};
// BeginData uses BeginRequest: exclusive monitor, one owned peer, <=30 s.
// RX by default; legacy management probes and timed radio tests disallowed.
// DataProbe explicitly submits a fixed diagnostic echo, not arbitrary bytes.
struct ItlAwdlDataResponse {
    uint32_t version, length, channel;
    int32_t rssi_dbm;
    uint64_t timestamp_us;
    uint32_t gp2_us;
    uint16_t sequence, reserved;
    uint8_t source[6], destination[6];
    uint8_t ipv6[ITL_AWDL_DATA_MTU];
};
struct ItlAwdlDataStatusResponse {
    uint32_t version, enabled, accepted, malformed;
    uint32_t unsupported, dropped, queued, reserved;
};
struct ItlAwdlDataProbeRequest {
    uint32_t version, length, token, reserved; // IPv6 length 64..1500; nonzero run token.
};
// No radio operations or published BSD interface. Each object is disposed
// before returning. stop_after: 0 full init, 1 after base init, 2 after VIF init.
// The two early exits exercise cleanup, not allocation failure inside Apple code.
struct ItlAwdlNativeInitRequest {
    uint32_t version, iterations, stop_after, reserved;
};
struct ItlAwdlNativeInitResponse {
    uint32_t version, attempted, base_initialized, virtual_initialized;
    uint32_t released, live_before, live_after, role;
    uint32_t methods_checked, method_failures, provider_open_after, last_error;
    uint32_t provider_opened;
};
// Native start/stop only: no BSD/queue registration, link enable or radio TX.
// fail_peer_creation == 1 injects failure at our createPeerManager override.
// Real creation permits one cycle, once per boot; subsequent calls return
// kIOReturnNotPermitted. Injected failures permit 1..4 cycles without a manager.
struct ItlAwdlNativeStartRequest {
    uint32_t version, iterations, fail_peer_creation, reserved;
};
struct ItlAwdlNativeStartResponse {
    uint32_t version, attempted, initialized, attached;
    uint32_t start_succeeded, expected_start_failures, peer_managers_created, peer_managers_released;
    uint32_t stopped, partial_cleanups, released, live_before;
    uint32_t live_after, provider_open_after, controller_pointer_after, last_error;
    uint32_t backend_calls, bsd_interfaces, cleanup_errors, peer_creation_calls;
    uint32_t peer_live_before, peer_live_after;
};
// Retain a disabled, unregistered native object across client connections.
// Create is idempotent while retained; retire prevents recreation this boot.
enum ItlAwdlNativeSessionOperation { ItlAwdlNativeInspect, ItlAwdlNativeCreate, ItlAwdlNativeRetire };
enum ItlAwdlNativeSessionState { ItlAwdlNativeUnused, ItlAwdlNativeRetained, ItlAwdlNativeRetired };
struct ItlAwdlNativeSessionRequest { uint32_t version, operation, reserved[2]; };
struct ItlAwdlNativeSessionResponse {
    uint32_t version, state, role, interface_count, peer_count, provider_open;
    uint32_t controller_pointer, bsd_interfaces, backend_calls, boot_reserved;
    uint32_t last_error, reserved;
    uint64_t registry_id;
};
// Isolated CPU-only native packet pools. No queues, interface or radio TX.
// failure: 0 normal; 1 first packet; 2 fourth packet; 3 first memory segment.
struct ItlAwdlNativePacketRequest { uint32_t version, iterations, failure, reserved; };
struct ItlAwdlNativePacketResponse {
    uint32_t version, attempted, initialized, expected_failures;
    uint32_t packet_factories, segment_factories, injected_failures, allocations;
    uint32_t prepared, returned, pattern_checks, bytes_checked;
    uint32_t metadata_checks, bounds_denied, pools_before, pools_after;
    uint32_t packets_before, packets_after, last_error, reserved;
};
// Disabled native queues, without interface registration, channel/ring or RF.
// stop_after deliberately exits after a setup stage; it is not an Apple
// allocation-failure injection. 1 pool, 2 TX, 3 RX, 4 TX work source,
// 5 RX work source, 6 TX initialize, 7 RX initialize; 0 runs two reuse cycles.
struct ItlAwdlNativeQueueRequest { uint32_t version, iterations, stop_after, reserved; };
struct ItlAwdlNativeQueueResponse {
    uint32_t version, attempted, completed, expected_stops;
    uint32_t pools_created, tx_created, rx_created, source_adds;
    uint32_t source_removes, initialized, finalized, queues_released;
    uint32_t metadata_checks, rejections, allocations, returned;
    uint32_t bytes_checked, callback_calls, pools_before, pools_after;
    uint32_t packets_before, packets_after, tx_before, tx_after;
    uint32_t rx_before, rx_after, cleanup_errors, last_error;
};
// Register disabled queues with the retained native object, without publishing
// a BSD client, enabling a ring, or transmitting. The session must exist first.
// Prepare is idempotent when registered. stop_after 1..6 is an early cleanup
// checkpoint (pool, TX, RX, TX source, RX source, registration), not fault injection.
enum ItlAwdlNativeTransportOperation { ItlAwdlTransportInspect, ItlAwdlTransportPrepare, ItlAwdlTransportClear };
struct ItlAwdlNativeTransportRequest { uint32_t version, operation, stop_after, reserved; };
struct ItlAwdlNativeTransportResponse {
    uint32_t version, registered, last_stage, prepare_calls;
    uint32_t registrations, deregistrations, callback_calls, cleanup_errors;
    uint32_t interface_count, peer_count, pool_count, packet_count;
    uint32_t tx_count, rx_count, tx_interface, rx_interface;
    uint32_t tx_enabled, rx_enabled, bsd_interfaces, registration_copies;
    uint32_t last_error, reserved;
    uint64_t registry_id;
};
enum ItlAwdlTestMode { ItlAwdlTestNone = 0, ItlAwdlTestWake, ItlAwdlTestProbe, ItlAwdlTestFollow, ItlAwdlTestHop };
struct ItlAwdlClockResponse {
    uint32_t version, flags, register_address, gp2_us;
    uint64_t host_begin_us, host_end_us;
    uint32_t rx_gp2_us, phy_command_version;
    uint64_t rx_host_us;
};
struct ItlAwdlRunRequest { uint32_t version, mode, reserved[2]; };
struct ItlAwdlRunEvent {
    uint32_t target_gp2_us, wake_gp2_us, finish_gp2_us, result;
    int32_t lateness_us;
    uint32_t duration_us;
    uint16_t channel, counter;
    uint32_t operation; // 1 timer wake, 2 fixed probe submission, 3 passive retune.
};
struct ItlAwdlRunStatusResponse {
    uint32_t version, mode, active, event_count;
    uint32_t clock_reads, clock_valid, stale_ticks, last_error;
    uint32_t retunes, probes, missed, cancelled;
    ItlAwdlRunEvent events[ITL_AWDL_MAX_EVENTS];
};
#ifdef __cplusplus
static_assert(sizeof(ItlAwdlBeginRequest) == 24, "AWDL request ABI");
static_assert(sizeof(ItlAwdlDataProbeRequest) == 16, "AWDL fixed echo request ABI");
static_assert(sizeof(ItlAwdlNativeInitRequest) == 16, "native init request ABI");
static_assert(sizeof(ItlAwdlNativeInitResponse) == 52, "native init response ABI");
static_assert(ItlAwdlDataProbe == 18 && ItlAwdlNativeInit == 19, "append-only native init ABI");
static_assert(ItlAwdlNativeStart == 20, "append-only native start ABI");
static_assert(sizeof(ItlAwdlNativeStartRequest) == 16, "native start request ABI");
static_assert(sizeof(ItlAwdlNativeStartResponse) == 88, "native start response ABI");
static_assert(ItlAwdlNativeSession == 21, "append-only native retained session ABI");
static_assert(sizeof(ItlAwdlNativeSessionRequest) == 16, "native session request ABI");
static_assert(sizeof(ItlAwdlNativeSessionResponse) == 56, "native session response ABI");
static_assert(ItlAwdlNativePackets == 22, "append-only native packet ABI");
static_assert(sizeof(ItlAwdlNativePacketRequest) == 16, "native packet request ABI");
static_assert(sizeof(ItlAwdlNativePacketResponse) == 80, "native packet response ABI");
static_assert(ItlAwdlNativeTransport == 24, "append-only native transport ABI");
static_assert(sizeof(ItlAwdlNativeTransportRequest) == 16, "native transport request ABI");
static_assert(sizeof(ItlAwdlNativeTransportResponse) == 96, "native transport response ABI");
static_assert(ItlAwdlNativeQueues == 23, "append-only native queue ABI");
static_assert(sizeof(ItlAwdlNativeQueueRequest) == 16, "native queue request ABI");
static_assert(sizeof(ItlAwdlNativeQueueResponse) == 112, "native queue response ABI");
static_assert(sizeof(ItlAwdlStatusResponse) == 104, "AWDL status ABI");
static_assert(sizeof(ItlAwdlFrameResponse) == 2072, "AWDL frame ABI");
static_assert(sizeof(ItlAwdlTxStatusResponse) == 36, "AWDL TX status ABI");
static_assert(sizeof(ItlAwdlRegStatusResponse) == 24, "AWDL regulatory status ABI");
static_assert(sizeof(ItlAwdlLinkedStatusResponse) == 32, "AWDL linked status ABI");
static_assert(sizeof(ItlAwdlDataResponse) == 1544, "AWDL IPv6 RX ABI");
static_assert(sizeof(ItlAwdlDataStatusResponse) == 32, "AWDL IPv6 status ABI");
static_assert(ItlAwdlBeginData == 15 && ItlAwdlReadData == 16 && ItlAwdlDataStatus == 17, "append-only ABI");
static_assert(sizeof(ItlAwdlTimedFrameResponse) == 2088, "AWDL timed RX ABI");
static_assert(sizeof(ItlAwdlClockResponse) == 48, "AWDL live clock ABI");
static_assert(sizeof(ItlAwdlRunRequest) == 16, "AWDL run request ABI");
static_assert(sizeof(ItlAwdlRunEvent) == 32, "AWDL run event ABI");
static_assert(sizeof(ItlAwdlRunStatusResponse) == 1072, "AWDL run status ABI");
#endif
#endif

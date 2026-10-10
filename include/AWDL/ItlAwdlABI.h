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
enum ItlAwdlMethod { ItlAwdlStatus = 0, ItlAwdlBegin, ItlAwdlEnd, ItlAwdlRead, ItlAwdlReadRejected, ItlAwdlProbe, ItlAwdlTxStatus, ItlAwdlRegStatus, ItlAwdlReadTimed, ItlAwdlClock, ItlAwdlRunTest, ItlAwdlRunStatus, ItlAwdlPrepareCountry, ItlAwdlBeginLinked, ItlAwdlLinkedStatus, ItlAwdlBeginData, ItlAwdlReadData, ItlAwdlDataStatus, ItlAwdlDataProbe, ItlAwdlNativeInit, ItlAwdlMethodCount };
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
static_assert(sizeof(ItlAwdlNativeInitResponse) == 48, "native init response ABI");
static_assert(ItlAwdlDataProbe == 18 && ItlAwdlNativeInit == 19, "append-only native init ABI");
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

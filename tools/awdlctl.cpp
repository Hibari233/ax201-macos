// SPDX-License-Identifier: GPL-2.0-or-later
#include <AWDL/ItlAwdlABI.h>
#include <AWDL/AwdlFrame.h>
#include <AWDL/AwdlSchedule.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach_error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <chrono>

static volatile sig_atomic_t interrupted = 0;
static void on_signal(int) { interrupted = 1; }
static void error(const char *op, kern_return_t r) { fprintf(stderr, "%s: 0x%08x (%s)\n", op, r, mach_error_string(r)); }
static kern_return_t status(io_connect_t c, ItlAwdlStatusResponse *s) {
    size_t n = sizeof(*s);
    auto r = IOConnectCallStructMethod(c, ItlAwdlStatus, nullptr, 0, s, &n);
    if (!r && (n != sizeof(*s) || s->version != ITL_AWDL_ABI_VERSION)) return kIOReturnBadMessageID;
    if (!r) printf("{\"session\":%u,\"opmode\":%u,\"state\":%u,\"channel\":%u,\"if_flags\":%u,\"last_error\":%u,\"stop_reason\":%u,\"rx_total\":%llu,\"rx_awdl\":%llu,\"rx_peer\":%llu,\"dropped\":%llu,\"invalid\":%llu,\"device_id\":%u,\"driver_flags\":%u,\"queued\":%u,\"capabilities\":%u}\n",
        s->session, s->opmode, s->net_state, s->channel, s->if_flags, s->last_error, s->stop_reason,
        (unsigned long long)s->rx_total, (unsigned long long)s->rx_awdl, (unsigned long long)s->rx_peer,
        (unsigned long long)s->rx_dropped, (unsigned long long)s->rx_invalid, s->device_id, s->driver_flags, s->queued, s->capabilities);
    return r;
}
static bool number(const char *text, unsigned long *out) {
    char *end = nullptr; *out = strtoul(text, &end, 10);
    return *text && end && !*end;
}
static kern_return_t tx_status(io_connect_t c, ItlAwdlTxStatusResponse *out) {
    size_t size = sizeof(*out);
    auto r = IOConnectCallStructMethod(c, ItlAwdlTxStatus, nullptr, 0, out, &size);
    if (!r && (size != sizeof(*out) || out->version != ITL_AWDL_ABI_VERSION)) return kIOReturnBadArgument;
    if (!r) printf("{\"tx_status\":true,\"submitted\":%u,\"completed\":%u,\"succeeded\":%u,\"failed\":%u,\"firmware_status\":%u,\"host_error\":%u,\"pending\":%u,\"aborted\":%u}\n",
        out->submitted, out->completed, out->succeeded, out->failed, out->last_firmware_status, out->last_host_error, out->pending, out->aborted);
    return r;
}
static kern_return_t reg_status(io_connect_t c) {
    ItlAwdlRegStatusResponse out = {}; size_t size = sizeof(out);
    auto r = IOConnectCallStructMethod(c, ItlAwdlRegStatus, nullptr, 0, &out, &size);
    if (!r && (size != sizeof(out) || out.version != ITL_AWDL_ABI_VERSION)) return kIOReturnBadArgument;
    if (!r) printf("{\"regulatory_status\":true,\"channel\":%u,\"channel_flags\":%u,\"query_count\":%u,\"query_error\":%u,\"country_hex\":\"%02x%02x\"}\n",
        out.channel, out.channel_flags, out.query_count, out.last_query_error, (unsigned char)out.firmware_country[0], (unsigned char)out.firmware_country[1]);
    return r;
}
static kern_return_t linked_status(io_connect_t c, ItlAwdlLinkedStatusResponse *out) {
    size_t size = sizeof(*out);
    auto r = IOConnectCallStructMethod(c, ItlAwdlLinkedStatus, nullptr, 0, out, &size);
    if (!r && (size != sizeof(*out) || out->version != ITL_AWDL_ABI_VERSION)) return kIOReturnBadArgument;
    if (!r) printf("{\"linked_status\":true,\"linked_mode\":%u,\"firmware_generation\":%u,\"start_generation\":%u,\"channel\":%u,\"filter_enabled\":%u,\"restore_error\":%u,\"link_matches\":%u}\n",
        out->linked_mode, out->firmware_generation, out->start_generation, out->channel,
        out->filter_enabled, out->restore_error, out->link_matches);
    return r;
}
static kern_return_t clock_status(io_connect_t c, uint32_t *flags) {
    ItlAwdlClockResponse out = {}; size_t size = sizeof(out);
    auto r = IOConnectCallStructMethod(c, ItlAwdlClock, nullptr, 0, &out, &size);
    if (!r && (size != sizeof(out) || out.version != ITL_AWDL_ABI_VERSION)) return kIOReturnBadArgument;
    if (!r) {
        *flags = out.flags;
        printf("{\"clock\":true,\"flags\":%u,\"register\":%u,\"gp2_us\":%u,\"host_begin_us\":%llu,\"host_end_us\":%llu,\"rx_gp2_us\":%u,\"rx_host_us\":%llu,\"phy_command_version\":%u}\n",
            out.flags, out.register_address, out.gp2_us, (unsigned long long)out.host_begin_us,
            (unsigned long long)out.host_end_us, out.rx_gp2_us, (unsigned long long)out.rx_host_us, out.phy_command_version);
    }
    return r;
}
static kern_return_t run_status(io_connect_t c, ItlAwdlRunStatusResponse *out) {
    size_t size = sizeof(*out);
    auto r = IOConnectCallStructMethod(c, ItlAwdlRunStatus, nullptr, 0, out, &size);
    if (!r && (size != sizeof(*out) || out->version != ITL_AWDL_ABI_VERSION || out->event_count > ITL_AWDL_MAX_EVENTS))
        return kIOReturnBadArgument;
    if (!r) {
        printf("{\"run_status\":true,\"mode\":%u,\"active\":%u,\"event_count\":%u,\"clock_reads\":%u,\"clock_valid\":%u,\"stale_ticks\":%u,\"last_error\":%u,\"retunes\":%u,\"probes\":%u,\"missed\":%u,\"cancelled\":%u}\n",
            out->mode, out->active, out->event_count, out->clock_reads, out->clock_valid,
            out->stale_ticks, out->last_error, out->retunes, out->probes, out->missed, out->cancelled);
        for (unsigned i = 0; i < out->event_count; ++i) {
            const auto &e = out->events[i];
            printf("{\"run_event\":true,\"index\":%u,\"operation\":%u,\"target_gp2_us\":%u,\"wake_gp2_us\":%u,\"finish_gp2_us\":%u,\"result\":%u,\"lateness_us\":%d,\"duration_us\":%u,\"channel\":%u,\"counter\":%u}\n",
                i, e.operation, e.target_gp2_us, e.wake_gp2_us, e.finish_gp2_us, e.result,
                e.lateness_us, e.duration_us, e.channel, e.counter);
        }
    }
    return r;
}
static int selftest(io_connect_t c) {
    unsigned failures = 0, passed = 0;
    auto expect = [&](const char *name, kern_return_t got, kern_return_t want) {
        if (got != want) { fprintf(stderr, "FAIL %s: got 0x%x wanted 0x%x\n", name, got, want); ++failures; }
        else ++passed;
    };
    ItlAwdlStatusResponse s = {}; size_t size = sizeof(s);
    expect("invalid selector", IOConnectCallStructMethod(c, ItlAwdlMethodCount, nullptr, 0, nullptr, nullptr), kIOReturnUnsupported);
    expect("large selector", IOConnectCallStructMethod(c, 0xffffffff, nullptr, 0, nullptr, nullptr), kIOReturnUnsupported);
    expect("country request with input", IOConnectCallStructMethod(c, ItlAwdlPrepareCountry, &s, sizeof(s), nullptr, nullptr), kIOReturnBadArgument);
    expect("country request with output", IOConnectCallStructMethod(c, ItlAwdlPrepareCountry, nullptr, 0, &s, &size), kIOReturnBadArgument);
    expect("country request while radio off", IOConnectCallStructMethod(c, ItlAwdlPrepareCountry, nullptr, 0, nullptr, nullptr), kIOReturnNotReady);
    size = sizeof(s) - 1;
    expect("short status", IOConnectCallStructMethod(c, ItlAwdlStatus, nullptr, 0, &s, &size), kIOReturnBadArgument);
    ItlAwdlBeginRequest r = {};
    expect("short request", IOConnectCallStructMethod(c, ItlAwdlBegin, &r, sizeof(r) - 1, nullptr, nullptr), kIOReturnBadArgument);
    expect("wrong version", IOConnectCallStructMethod(c, ItlAwdlBegin, &r, sizeof(r), nullptr, nullptr), kIOReturnBadArgument);
    r.version = 1; r.dwell_ms = 100; r.channel = 6;
    expect("zero peer", IOConnectCallStructMethod(c, ItlAwdlBegin, &r, sizeof(r), nullptr, nullptr), kIOReturnBadArgument);
    r.peer[0] = 1;
    expect("multicast peer", IOConnectCallStructMethod(c, ItlAwdlBegin, &r, sizeof(r), nullptr, nullptr), kIOReturnBadArgument);
    r.peer[0] = 2; r.dwell_ms = 30001;
    expect("excess dwell", IOConnectCallStructMethod(c, ItlAwdlBegin, &r, sizeof(r), nullptr, nullptr), kIOReturnBadArgument);
    r.dwell_ms = 100; r.channel = 1;
    expect("unsupported channel", IOConnectCallStructMethod(c, ItlAwdlBegin, &r, sizeof(r), nullptr, nullptr), kIOReturnUnsupported);
    r.channel = 6; r.reserved = 1;
    expect("reserved field", IOConnectCallStructMethod(c, ItlAwdlBegin, &r, sizeof(r), nullptr, nullptr), kIOReturnBadArgument);
    uint64_t scalar = 0;
    expect("unexpected scalar", IOConnectCallMethod(c, ItlAwdlEnd, &scalar, 1, nullptr, 0, nullptr, nullptr, nullptr, nullptr), kIOReturnBadArgument);
    ItlAwdlFrameResponse sample = {}; size = sizeof(sample) - 1;
    expect("short rejected sample", IOConnectCallStructMethod(c, ItlAwdlReadRejected, nullptr, 0, &sample, &size), kIOReturnBadArgument);
    expect("probe without session", IOConnectCallStructMethod(c, ItlAwdlProbe, nullptr, 0, nullptr, nullptr), kIOReturnNotPermitted);
    expect("probe with extra input", IOConnectCallStructMethod(c, ItlAwdlProbe, &r, sizeof(r), nullptr, nullptr), kIOReturnBadArgument);
    ItlAwdlTimedFrameResponse timed = {}; size = sizeof(timed) - 1;
    expect("short timed read", IOConnectCallStructMethod(c, ItlAwdlReadTimed, nullptr, 0, &timed, &size), kIOReturnBadArgument);
    size = sizeof(timed);
    expect("timed read without owner", IOConnectCallStructMethod(c, ItlAwdlReadTimed, nullptr, 0, &timed, &size), kIOReturnNotPermitted);
    ItlAwdlClockResponse clock = {}; size = sizeof(clock) - 1;
    expect("short clock", IOConnectCallStructMethod(c, ItlAwdlClock, nullptr, 0, &clock, &size), kIOReturnBadArgument);
    size = sizeof(clock);
    expect("clock without owner", IOConnectCallStructMethod(c, ItlAwdlClock, nullptr, 0, &clock, &size), kIOReturnNotPermitted);
    ItlAwdlRunRequest run = {1, 1, {0, 0}};
    expect("short run", IOConnectCallStructMethod(c, ItlAwdlRunTest, &run, sizeof(run) - 1, nullptr, nullptr), kIOReturnBadArgument);
    expect("run without owner", IOConnectCallStructMethod(c, ItlAwdlRunTest, &run, sizeof(run), nullptr, nullptr), kIOReturnNotPermitted);
    run.mode = 0;
    expect("invalid run mode", IOConnectCallStructMethod(c, ItlAwdlRunTest, &run, sizeof(run), nullptr, nullptr), kIOReturnBadArgument);
    run.mode = 1; run.reserved[1] = 1;
    expect("run reserved field", IOConnectCallStructMethod(c, ItlAwdlRunTest, &run, sizeof(run), nullptr, nullptr), kIOReturnBadArgument);
    ItlAwdlRunStatusResponse run_output = {}; size = sizeof(run_output) - 1;
    expect("short run status", IOConnectCallStructMethod(c, ItlAwdlRunStatus, nullptr, 0, &run_output, &size), kIOReturnBadArgument);
    r = {}; r.version = 1; r.dwell_ms = 100; r.peer[0] = 2;
    expect("short linked request", IOConnectCallStructMethod(c, ItlAwdlBeginLinked, &r, sizeof(r) - 1, nullptr, nullptr), kIOReturnBadArgument);
    r.version = 0;
    expect("linked wrong version", IOConnectCallStructMethod(c, ItlAwdlBeginLinked, &r, sizeof(r), nullptr, nullptr), kIOReturnBadArgument);
    r.version = 1; r.reserved = 1;
    expect("linked reserved", IOConnectCallStructMethod(c, ItlAwdlBeginLinked, &r, sizeof(r), nullptr, nullptr), kIOReturnBadArgument);
    r.reserved = 0; r.peer[0] = 1;
    expect("linked multicast peer", IOConnectCallStructMethod(c, ItlAwdlBeginLinked, &r, sizeof(r), nullptr, nullptr), kIOReturnBadArgument);
    r.peer[0] = 2; r.dwell_ms = 30001;
    expect("linked excessive dwell", IOConnectCallStructMethod(c, ItlAwdlBeginLinked, &r, sizeof(r), nullptr, nullptr), kIOReturnBadArgument);
    r.dwell_ms = 100; r.channel = 6;
    expect("linked forced channel", IOConnectCallStructMethod(c, ItlAwdlBeginLinked, &r, sizeof(r), nullptr, nullptr), kIOReturnBadArgument);
    ItlAwdlLinkedStatusResponse link = {}; size = sizeof(link) - 1;
    expect("short linked status", IOConnectCallStructMethod(c, ItlAwdlLinkedStatus, nullptr, 0, &link, &size), kIOReturnBadArgument);
    printf("{\"abi_tests_passed\":%u,\"failed\":%u}\n", passed, failures);
    return failures ? 1 : 0;
}
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Usage: awdlctl status|tx-status|reg-status|linked-status|prepare-country|run-status|selftest|listen|listen-timed|clock|wake-test|timed-probe|hop-test|hop-die|follow-test|probe|expire|die|linked-rx|linked-die|linked-close|linked-guards CHANNEL SECONDS PEER_MAC\nLinked commands require CHANNEL=0 (associated channel), and are receive-only.\n"); return 2; }
    auto service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AirportItlwm"));
    if (!service) { fprintf(stderr, "AirportItlwm service not found\n"); return 2; }
    io_connect_t c = IO_OBJECT_NULL;
    auto r = IOServiceOpen(service, mach_task_self(), ITL_AWDL_USER_CLIENT, &c);
    IOObjectRelease(service);
    if (r) { error("open AWDL diagnostic client", r); return 3; }
    ItlAwdlStatusResponse s = {};
    if (!strcmp(argv[1], "status")) { r = status(c, &s); IOServiceClose(c); if (r) error("status", r); return r ? 1 : 0; }
    if (!strcmp(argv[1], "selftest")) { int result = selftest(c); IOServiceClose(c); return result; }
    if (!strcmp(argv[1], "tx-status")) { ItlAwdlTxStatusResponse t = {}; r = tx_status(c, &t); IOServiceClose(c); if (r) error("TX status", r); return r ? 1 : 0; }
    if (!strcmp(argv[1], "reg-status")) { r = reg_status(c); IOServiceClose(c); if (r) error("regulatory status", r); return r ? 1 : 0; }
    if (!strcmp(argv[1], "linked-status")) { ItlAwdlLinkedStatusResponse link = {}; r = linked_status(c, &link); IOServiceClose(c); if (r) error("linked status", r); return r ? 1 : 0; }
    if (!strcmp(argv[1], "prepare-country")) {
        r = IOConnectCallStructMethod(c, ItlAwdlPrepareCountry, nullptr, 0, nullptr, nullptr);
        reg_status(c); IOServiceClose(c); if (r) error("firmware country confirmation", r); return r ? 1 : 0;
    }
    if (!strcmp(argv[1], "run-status")) { ItlAwdlRunStatusResponse run = {}; r = run_status(c, &run); IOServiceClose(c); if (r) error("run status", r); return r ? 1 : 0; }
    bool expire = !strcmp(argv[1], "expire"), die = !strcmp(argv[1], "die"), probe = !strcmp(argv[1], "probe");
    uint32_t test_mode = !strcmp(argv[1], "wake-test") ? ItlAwdlTestWake : !strcmp(argv[1], "timed-probe") ? ItlAwdlTestProbe :
        !strcmp(argv[1], "follow-test") ? ItlAwdlTestFollow : (!strcmp(argv[1], "hop-test") || !strcmp(argv[1], "hop-die")) ? ItlAwdlTestHop : ItlAwdlTestNone;
    bool run_die = !strcmp(argv[1], "hop-die");
    bool clock_test = !strcmp(argv[1], "clock");
    bool linked_die = !strcmp(argv[1], "linked-die"), linked_close = !strcmp(argv[1], "linked-close");
    bool linked_guards = !strcmp(argv[1], "linked-guards");
    bool linked = !strcmp(argv[1], "linked-rx") || linked_die || linked_close || linked_guards;
    bool timed = !strcmp(argv[1], "listen-timed") || test_mode || clock_test || linked;
    if (argc != 5 || (!expire && !die && !probe && !timed && strcmp(argv[1], "listen"))) { IOServiceClose(c); return 2; }
    unsigned long channel, seconds; unsigned mac[6]; char extra;
    if (!number(argv[2], &channel) || !number(argv[3], &seconds) || seconds < 1 || seconds > 30 ||
        (linked ? channel != 0 : (channel != 6 && channel != 44 && channel != 149)) ||
        sscanf(argv[4], "%2x:%2x:%2x:%2x:%2x:%2x%c", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5], &extra) != 6) {
        IOServiceClose(c); return 2;
    }
    ItlAwdlBeginRequest begin = {}; begin.version = 1; begin.channel = uint32_t(channel); begin.dwell_ms = uint32_t(seconds * 1000);
    for (unsigned i = 0; i < 6; ++i) begin.peer[i] = uint8_t(mac[i]);
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);
    r = IOConnectCallStructMethod(c, linked ? ItlAwdlBeginLinked : ItlAwdlBegin, &begin, sizeof(begin), nullptr, nullptr);
    if (r) { error("begin passive AWDL", r); status(c, &s); IOServiceClose(c); return 1; }
    if (die) { printf("Session started; exiting without IOServiceClose to verify kernel client-death cleanup\n"); fflush(stdout); _exit(0); }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds + 2);
    auto die_at = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    auto next_probe = std::chrono::steady_clock::now();
    auto next_clock = next_probe;
    bool run_started = false, loop_error = false;
    if (linked) {
        ItlAwdlLinkedStatusResponse link = {};
        auto result = linked_status(c, &link);
        if (result || !link.linked_mode || !link.filter_enabled || !link.link_matches ||
            link.firmware_generation != link.start_generation) loop_error = true;
    }
    if (linked_guards && !loop_error) {
        unsigned passed = 0, failed = 0;
        auto expect = [&](const char *name, kern_return_t got, kern_return_t want) {
            if (got != want) { error(name, got); ++failed; } else ++passed;
        };
        expect("linked probe must be disabled", IOConnectCallStructMethod(c, ItlAwdlProbe, nullptr, 0, nullptr, nullptr), kIOReturnUnsupported);
        ItlAwdlRunRequest run = {1, ItlAwdlTestHop, {0, 0}};
        expect("linked retune must be disabled", IOConnectCallStructMethod(c, ItlAwdlRunTest, &run, sizeof(run), nullptr, nullptr), kIOReturnUnsupported);
        ItlAwdlClockResponse clock = {}; size_t size = sizeof(clock);
        expect("linked clock unsupported", IOConnectCallStructMethod(c, ItlAwdlClock, nullptr, 0, &clock, &size), kIOReturnUnsupported);
        expect("linked duplicate session", IOConnectCallStructMethod(c, ItlAwdlBeginLinked, &begin, sizeof(begin), nullptr, nullptr), kIOReturnBusy);
        auto service2 = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AirportItlwm"));
        io_connect_t other = IO_OBJECT_NULL;
        auto opened = service2 ? IOServiceOpen(service2, mach_task_self(), ITL_AWDL_USER_CLIENT, &other) : kIOReturnNotFound;
        if (service2) IOObjectRelease(service2);
        expect("second client open", opened, kIOReturnSuccess);
        if (!opened) {
            expect("other client cannot end", IOConnectCallStructMethod(other, ItlAwdlEnd, nullptr, 0, nullptr, nullptr), kIOReturnNotPermitted);
            ItlAwdlTimedFrameResponse frame = {}; size = sizeof(frame);
            expect("other client cannot read", IOConnectCallStructMethod(other, ItlAwdlReadTimed, nullptr, 0, &frame, &size), kIOReturnNotPermitted);
            IOServiceClose(other);
        }
        printf("{\"linked_guards_passed\":%u,\"failed\":%u}\n", passed, failed);
        if (failed) loop_error = true;
    }
    unsigned clock_samples = 0, qualified_clocks = 0;
    unsigned probes = 0, permission_denials = 0; bool probe_denied = false;
    ItlAwdl::ScheduleTracker tracker;
    do {
        if (loop_error) break;
        if ((linked_die || linked_close) && std::chrono::steady_clock::now() >= die_at) {
            if (linked_close) break;
            puts("Exiting with active linked RX session; kernel must restore STA filter without stopping Wi-Fi.");
            fflush(stdout); _exit(0);
        }
        if (run_die && run_started && std::chrono::steady_clock::now() >= die_at) {
            puts("Exiting with active radio timer; kernel client-death cleanup must stop it.");
            fflush(stdout); _exit(0);
        }
        if (test_mode && !run_started) {
            ItlAwdlRunRequest request = {1, test_mode, {0, 0}};
            auto result = IOConnectCallStructMethod(c, ItlAwdlRunTest, &request, sizeof(request), nullptr, nullptr);
            if (!result) run_started = true;
            else if (result != kIOReturnNotReady) { error("run test", result); loop_error = true; break; }
        }
        if (clock_test && std::chrono::steady_clock::now() >= next_clock) {
            uint32_t flags = 0; auto result = clock_status(c, &flags);
            if (!result) { ++clock_samples; if (flags == 7) ++qualified_clocks; }
            else if (result != kIOReturnNotReady && result != kIOReturnBusy) { error("clock", result); loop_error = true; break; }
            next_clock = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        }
        if (probe && !probe_denied && probes < 8 && std::chrono::steady_clock::now() >= next_probe) {
            auto sent = IOConnectCallStructMethod(c, ItlAwdlProbe, nullptr, 0, nullptr, nullptr);
            if (!sent) ++probes;
            else if (sent == kIOReturnNotPermitted) {
                if (!permission_denials++) error("radio probe: current channel prohibits initiation", sent);
            }
            else if (sent != kIOReturnNotReady && sent != kIOReturnBusy) { error("radio probe", sent); probe_denied = true; }
            next_probe = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        }
        if (!expire) {
            ItlAwdlTimedFrameResponse record = {};
            auto &frame = record.frame;
            size_t expected = timed ? sizeof(record) : sizeof(frame), n = expected;
            auto read = IOConnectCallStructMethod(c, timed ? ItlAwdlReadTimed : ItlAwdlRead, nullptr, 0, &record, &n);
            if (!read) {
                if (frame.version != 1 || frame.length > ITL_AWDL_MAX_FRAME || n != expected) { error("frame ABI", kIOReturnBadArgument); loop_error = true; break; }
                ItlAwdl::ActionInfo info = {};
                auto parsed = ItlAwdl::parseAction(frame.frame, frame.length, &info);
                if (parsed != ItlAwdl::ParseResult::Valid) { error("frame parse", kIOReturnBadArgument); loop_error = true; break; }
                printf("{\"frame\":true,\"channel\":%u,\"rssi\":%d,\"ts_us\":%llu,\"subtype\":%u,\"tlvs\":%u,\"hex\":\"", frame.channel, frame.rssi_dbm, (unsigned long long)frame.timestamp_us, info.subtype, info.tlv_count);
                for (unsigned i = 0; i < frame.length; ++i) printf("%02x", frame.frame[i]);
                printf("\"");
                if (timed) {
                    printf(",\"clock_flags\":%u,\"gp2_us\":%u,\"tsf_us\":%llu,\"tsf_valid\":%s", record.clock_flags, record.gp2_us, (unsigned long long)record.tsf_us,
                           (record.clock_flags & 2) ? "true" : "false");
                    ItlAwdl::SyncAnnouncement sync = {}; ItlAwdl::ScheduleSample estimate = {};
                    auto parsed_sync = ItlAwdl::parseSchedule(frame.frame, frame.length, &sync);
                    printf(",\"schedule_parse\":%u", unsigned(parsed_sync));
                    if (parsed_sync == ItlAwdl::ScheduleResult::Valid && (record.clock_flags & 1) &&
                        tracker.observe(sync, record.gp2_us, frame.timestamp_us, &estimate)) {
                        printf(",\"boundary_gp2_us\":%u,\"boundary_counter\":%u,\"phase_error_us\":%lld,\"coherent_samples\":%u,\"schedule_reset\":%s,\"schedule_outlier\":%s",
                            estimate.next_boundary_gp2_us, estimate.next_counter, (long long)estimate.error_us,
                            estimate.coherent_samples, estimate.reset ? "true" : "false", estimate.outlier ? "true" : "false");
                    }
                }
                puts("}");
            } else if (read != kIOReturnNotReady) { error("read", read); loop_error = true; break; }
        }
        usleep(10000);
    } while (!interrupted && std::chrono::steady_clock::now() < deadline);
    r = status(c, &s);
    ItlAwdlTxStatusResponse tx = {};
    kern_return_t tx_result = 0;
    bool probing = probe || test_mode == ItlAwdlTestProbe;
    if (probing) { tx_result = tx_status(c, &tx); auto reg_result = reg_status(c); if (reg_result) { error("regulatory status", reg_result); loop_error = true; } }
    ItlAwdlRunStatusResponse run = {};
    if (test_mode) {
        auto result = run_status(c, &run);
        if (result) { error("run status", result); loop_error = true; }
        if (!run.event_count || run.last_error || run.active) loop_error = true;
        unsigned successful = 0;
        for (unsigned i = 0; i < run.event_count && i < ITL_AWDL_MAX_EVENTS; ++i)
            if (!run.events[i].result) ++successful;
        if (!successful || ((test_mode == ItlAwdlTestHop || test_mode == ItlAwdlTestFollow) && !run.retunes)) loop_error = true;
    }
    if (clock_test) {
        printf("{\"clock_summary\":true,\"samples\":%u,\"qualified\":%u}\n", clock_samples, qualified_clocks);
        if (!qualified_clocks) loop_error = true;
    }
    ItlAwdlFrameResponse rejected = {}; size_t rejected_size = sizeof(rejected);
    auto sample_result = IOConnectCallStructMethod(c, ItlAwdlReadRejected, nullptr, 0, &rejected, &rejected_size);
    if (!sample_result && rejected_size == sizeof(rejected) && rejected.version == ITL_AWDL_ABI_VERSION && rejected.length <= ITL_AWDL_MAX_FRAME) {
        printf("{\"rejected\":true,\"length\":%u,\"channel\":%u,\"parse_result\":%u,\"hex\":\"", rejected.length, rejected.channel,
            unsigned(ItlAwdl::parseAction(rejected.frame, rejected.length, nullptr)));
        for (unsigned i = 0; i < rejected.length; ++i) printf("%02x", rejected.frame[i]);
        puts("\"}");
    } else if (sample_result != kIOReturnNotReady) {
        error("rejected sample", sample_result ? sample_result : kIOReturnBadArgument);
        loop_error = true;
    }
    auto ended = IOConnectCallStructMethod(c, ItlAwdlEnd, nullptr, 0, nullptr, nullptr);
    if (ended) error("end", ended);
    if (linked) {
        ItlAwdlLinkedStatusResponse link = {};
        auto result = linked_status(c, &link);
        if (result || link.linked_mode || link.filter_enabled || link.restore_error || !link.link_matches ||
            link.firmware_generation != link.start_generation) loop_error = true;
        r = status(c, &s);
        if (s.opmode != 1 || s.net_state != 4) loop_error = true;
    }
    IOServiceClose(c);
    if (r) error("status", r);
    return loop_error || r || ended || s.last_error || s.session != ItlAwdlIdle ||
        (probing && (tx_result || !tx.submitted || !tx.succeeded || tx.pending || tx.aborted || probe_denied)) ? 1 : 0;
}

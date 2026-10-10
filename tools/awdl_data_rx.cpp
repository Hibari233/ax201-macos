// SPDX-License-Identifier: GPL-2.0-or-later
// Privileged, bounded reception from an explicitly selected owned test peer.
// Logs only counters/packet metadata; no addresses or IPv6 payload on stdout.
#include <IOKit/IOKitLib.h>
#include <AWDL/ItlAwdlABI.h>
#include <AWDL/AwdlData.h>
#include <AWDL/AwdlEcho.h>
#include <chrono>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static volatile sig_atomic_t interrupted = 0;
static void onSignal(int) { interrupted = 1; }
static kern_return_t openClient(io_connect_t *c) {
    auto service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AirportItlwm"));
    if (!service) return kIOReturnNotFound;
    auto r = IOServiceOpen(service, mach_task_self(), ITL_AWDL_USER_CLIENT, c);
    IOObjectRelease(service); return r;
}
template<class T> static kern_return_t readValue(io_connect_t c, unsigned selector, T &out) {
    memset(&out, 0, sizeof(out)); size_t n = sizeof(out);
    auto r = IOConnectCallStructMethod(c, selector, nullptr, 0, &out, &n);
    if (!r && (n != sizeof(out) || out.version != ITL_AWDL_ABI_VERSION)) return kIOReturnBadArgument;
    return r;
}
static kern_return_t showStatus(io_connect_t c, ItlAwdlStatusResponse *saved = nullptr) {
    ItlAwdlStatusResponse s = {}; ItlAwdlDataStatusResponse d = {};
    auto r = readValue(c, ItlAwdlStatus, s);
    if (!r) r = readValue(c, ItlAwdlDataStatus, d);
    if (r) return r;
    if (saved) *saved = s;
    printf("{\"session\":%u,\"stop_reason\":%u,\"last_error\":%u,\"channel\":%u,\"opmode\":%u,\"if_flags\":%u,"
           "\"management_peer\":%llu,\"rx_total\":%llu,\"data_mode\":%u,\"accepted\":%u,\"malformed\":%u,"
           "\"unsupported\":%u,\"dropped\":%u,\"queued\":%u}\n",
           s.session, s.stop_reason, s.last_error, s.channel, s.opmode, s.if_flags,
           (unsigned long long)s.rx_peer, (unsigned long long)s.rx_total,
           d.enabled, d.accepted, d.malformed, d.unsupported, d.dropped, d.queued);
    return kIOReturnSuccess;
}
struct Checks {
    unsigned passed = 0, failed = 0;
    void expect(const char *name, kern_return_t got, kern_return_t want) {
        if (got == want) ++passed;
        else { ++failed; fprintf(stderr, "%s: got 0x%x expected 0x%x\n", name, got, want); }
    }
    int report() { printf("{\"checks_passed\":%u,\"checks_failed\":%u}\n", passed, failed); return failed ? 1 : 0; }
};
static int selftest(io_connect_t c) {
    Checks t;
    ItlAwdlBeginRequest b = {1,6,100,0,{2,0,0,0,0,1},{0,0}};
    auto begin = [&]() { return IOConnectCallStructMethod(c, ItlAwdlBeginData, &b, sizeof(b), nullptr, nullptr); };
    t.expect("short begin", IOConnectCallStructMethod(c, ItlAwdlBeginData, &b, sizeof(b)-1, nullptr, nullptr), kIOReturnBadArgument);
    b.version = 0; t.expect("version", begin(), kIOReturnBadArgument); b.version = 1;
    b.reserved = 1; t.expect("reserved", begin(), kIOReturnBadArgument); b.reserved = 0;
    b.padding[1] = 1; t.expect("padding", begin(), kIOReturnBadArgument); b.padding[1] = 0;
    b.peer[0] = 1; t.expect("group peer", begin(), kIOReturnBadArgument); b.peer[0] = 2;
    b.dwell_ms = 30001; t.expect("dwell", begin(), kIOReturnBadArgument); b.dwell_ms = 100;
    b.channel = 0; t.expect("channel", begin(), kIOReturnUnsupported);
    ItlAwdlDataResponse packet = {}; size_t n = sizeof(packet)-1;
    t.expect("short read", IOConnectCallStructMethod(c, ItlAwdlReadData, nullptr, 0, &packet, &n), kIOReturnBadArgument);
    t.expect("read without owner", readValue(c, ItlAwdlReadData, packet), kIOReturnNotPermitted);
    ItlAwdlDataStatusResponse s = {}; n = sizeof(s)-1;
    t.expect("short status", IOConnectCallStructMethod(c, ItlAwdlDataStatus, nullptr, 0, &s, &n), kIOReturnBadArgument);
    ItlAwdlDataProbeRequest probe = {1,64,1,0};
    auto tx = [&]() { return IOConnectCallStructMethod(c, ItlAwdlDataProbe, &probe, sizeof(probe), nullptr, nullptr); };
    t.expect("short data probe", IOConnectCallStructMethod(c, ItlAwdlDataProbe, &probe, sizeof(probe)-1, nullptr, nullptr), kIOReturnBadArgument);
    probe.version = 0; t.expect("probe version", tx(), kIOReturnBadArgument); probe.version = 1;
    probe.reserved = 1; t.expect("probe reserved", tx(), kIOReturnBadArgument); probe.reserved = 0;
    probe.token = 0; t.expect("probe token", tx(), kIOReturnBadArgument); probe.token = 1;
    probe.length = 63; t.expect("probe too small", tx(), kIOReturnBadArgument);
    probe.length = 1501; t.expect("probe too large", tx(), kIOReturnBadArgument); probe.length = 64;
    t.expect("probe without owner", tx(), kIOReturnNotPermitted);
    t.expect("unknown selector", IOConnectCallStructMethod(c, ItlAwdlMethodCount, nullptr, 0, nullptr, nullptr), kIOReturnUnsupported);
    return t.report();
}
static bool number(const char *s, unsigned long *out) {
    char *end = nullptr; *out = strtoul(s, &end, 10);
    return s[0] && end && !*end;
}
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: awdl_data_rx status|selftest|listen|expire|close|die CHANNEL SECONDS OWNED_PEER_MAC\n"
                        "       awdl_data_rx probe CHANNEL SECONDS OWNED_PEER_MAC IPV6_LENGTH\n"
                        "Wi-Fi must be off for capture; restore it after this program. Ethernet is recommended.\n");
        return 2;
    }
    bool status = !strcmp(argv[1], "status"), test = !strcmp(argv[1], "selftest");
    bool listen = !strcmp(argv[1], "listen"), expire = !strcmp(argv[1], "expire"), die = !strcmp(argv[1], "die");
    bool closeEarly = !strcmp(argv[1], "close");
    bool probe = !strcmp(argv[1], "probe");
    unsigned long probeLength = 0;
    ItlAwdlBeginRequest b = {}; b.version = 1;
    if ((!status && !test && !listen && !expire && !die && !closeEarly && !probe) || ((status || test) && argc != 2)) return 2;
    if (listen || expire || die || closeEarly || probe) {
        unsigned long channel = 0, seconds = 0; unsigned mac[6]; char extra;
        if (argc != (probe ? 6 : 5) || !number(argv[2], &channel) || !number(argv[3], &seconds) ||
            (channel != 6 && channel != 44 && channel != 149) || seconds < 1 || seconds > 30 ||
            sscanf(argv[4], "%2x:%2x:%2x:%2x:%2x:%2x%c", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5], &extra) != 6) return 2;
        for (unsigned i = 0; i < 6; ++i) b.peer[i] = uint8_t(mac[i]);
        if (!ItlAwdl::unicastMac(b.peer)) return 2;
        b.channel = uint32_t(channel); b.dwell_ms = uint32_t(seconds * 1000);
        if (probe && (!number(argv[5], &probeLength) || !ItlAwdl::echoLength(probeLength))) return 2;
    }
    io_connect_t c = IO_OBJECT_NULL; auto r = openClient(&c);
    if (r) { fprintf(stderr, "Open: 0x%x (root required)\n", r); return 1; }
    if (status || test) {
        int result = test ? selftest(c) : (showStatus(c) ? 1 : 0);
        IOServiceClose(c); return result;
    }
    signal(SIGINT, onSignal); signal(SIGTERM, onSignal);
    ItlAwdlStatusResponse initial = {};
    r = readValue(c, ItlAwdlStatus, initial);
    if (r) { fprintf(stderr, "Initial status: 0x%x\n", r); IOServiceClose(c); return 1; }
    if (probe && !(initial.capabilities & 256)) { fprintf(stderr, "Driver has no fixed data probe support\n"); IOServiceClose(c); return 1; }
    r = IOConnectCallStructMethod(c, ItlAwdlBeginData, &b, sizeof(b), nullptr, nullptr);
    if (r) { fprintf(stderr, "Begin data RX: 0x%x\n", r); IOServiceClose(c); return 1; }
    if (die) { puts("{\"client_death_test\":true}"); fflush(stdout); _exit(0); }
    Checks checks;
    checks.expect("data mode prohibits probe TX", IOConnectCallStructMethod(c, ItlAwdlProbe, nullptr, 0, nullptr, nullptr), kIOReturnUnsupported);
    ItlAwdlRunRequest run = {1,ItlAwdlTestHop,{0,0}};
    checks.expect("data mode prohibits radio tests", IOConnectCallStructMethod(c, ItlAwdlRunTest, &run, sizeof(run), nullptr, nullptr), kIOReturnUnsupported);
    io_connect_t other = IO_OBJECT_NULL;
    auto opened = openClient(&other); checks.expect("second client opens", opened, kIOReturnSuccess);
    if (!opened) {
        ItlAwdlDataResponse packet = {};
        checks.expect("foreign read", readValue(other, ItlAwdlReadData, packet), kIOReturnNotPermitted);
        checks.expect("foreign end", IOConnectCallStructMethod(other, ItlAwdlEnd, nullptr, 0, nullptr, nullptr), kIOReturnNotPermitted);
        checks.expect("foreign begin", IOConnectCallStructMethod(other, ItlAwdlBeginData, &b, sizeof(b), nullptr, nullptr), kIOReturnBusy);
        ItlAwdlDataProbeRequest req = {1,64,1,0};
        checks.expect("foreign data TX", IOConnectCallStructMethod(other, ItlAwdlDataProbe, &req, sizeof(req), nullptr, nullptr), kIOReturnNotPermitted);
        IOServiceClose(other);
    }
    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now() + std::chrono::milliseconds(closeEarly ? b.dwell_ms / 2 : b.dwell_ms + (expire ? 1000 : 0));
    auto nextProbe = Clock::now();
    ItlAwdlDataProbeRequest request = {1, uint32_t(probeLength), arc4random() | 1U, 0};
    unsigned sent = 0, echoes = 0, echoed = 0, denied = 0, notReady = 0, busy = 0;
    unsigned received = 0, checksummed = 0, checksumFailed = 0, checksumUnavailable = 0;
    while (!interrupted && Clock::now() < deadline && !checks.failed) {
        if (probe && sent < 8 && Clock::now() >= nextProbe) {
            nextProbe = Clock::now() + std::chrono::milliseconds(100);
            auto result = IOConnectCallStructMethod(c, ItlAwdlDataProbe, &request, sizeof(request), nullptr, nullptr);
            if (!result) ++sent;
            else if (result == kIOReturnNotPermitted) ++denied;
            else if (result == kIOReturnNotReady) ++notReady;
            else if (result == kIOReturnBusy) ++busy;
            else checks.expect("fixed data probe", result, kIOReturnSuccess);
        }
        ItlAwdlDataResponse p = {};
        r = readValue(c, ItlAwdlReadData, p);
        if (!r) {
            if (p.reserved || p.length > ITL_AWDL_DATA_MTU || !ItlAwdl::validIpv6(p.ipv6, p.length) ||
                !ItlAwdl::equalMac(p.source, b.peer) || p.channel != b.channel) {
                checks.expect("RX contract", kIOReturnBadArgument, kIOReturnSuccess); break;
            }
            ++received;
            auto checksum = ItlAwdl::dataChecksum(p.ipv6, p.length);
            if (checksum == ItlAwdl::DataChecksum::Valid) ++checksummed;
            else if (checksum == ItlAwdl::DataChecksum::Invalid) ++checksumFailed;
            else ++checksumUnavailable;
            if (probe && p.length >= 48) {
                unsigned sequence = ItlAwdl::be16(p.ipv6 + 46);
                if (sequence < sent && sequence < 8 && !(echoed & (1U << sequence)) &&
                    ItlAwdl::equalMac(p.destination, initial.local_mac) &&
                    ItlAwdl::matchesEchoReply(p.ipv6, p.length, initial.local_mac, b.peer,
                                             probeLength, request.token, uint16_t(sequence))) {
                    echoed |= 1U << sequence; ++echoes;
                }
            }
            printf("{\"ipv6_rx\":true,\"length\":%u,\"sequence\":%u,\"channel\":%u,\"next_header\":%u,\"rssi\":%d}\n",
                   p.length, p.sequence, p.channel, p.ipv6[6], p.rssi_dbm);
        } else if (r != kIOReturnNotReady) { checks.expect("read", r, kIOReturnNotReady); break; }
        ItlAwdlFrameResponse action = {};
        // Drain management records; their private contents are not printed.
        for (unsigned i = 0; i < ITL_AWDL_RING_COUNT; ++i)
            if (readValue(c, ItlAwdlRead, action)) break;
        if (r == kIOReturnNotReady) usleep(10000);
    }
    ItlAwdlStatusResponse before = {};
    checks.expect("final status", showStatus(c, &before), kIOReturnSuccess);
    if (expire && !interrupted) {
        checks.expect("timeout is idle", before.session, ItlAwdlIdle);
        checks.expect("timeout reason", before.stop_reason, ItlAwdlExpired);
    }
    if (closeEarly && !interrupted) checks.expect("explicit close before timeout", before.session, ItlAwdlListening);
    checks.expect("end", IOConnectCallStructMethod(c, ItlAwdlEnd, nullptr, 0, nullptr, nullptr), kIOReturnSuccess);
    ItlAwdlDataStatusResponse after = {};
    checks.expect("closed status", readValue(c, ItlAwdlDataStatus, after), kIOReturnSuccess);
    checks.expect("closed queue wiped", after.queued | after.enabled, 0);
    ItlAwdlDataResponse packet = {};
    checks.expect("closed read denied", readValue(c, ItlAwdlReadData, packet), kIOReturnNotPermitted);
    ItlAwdlStatusResponse restored = {};
    checks.expect("restored status", readValue(c, ItlAwdlStatus, restored), kIOReturnSuccess);
    checks.expect("restored idle", restored.session, ItlAwdlIdle);
    checks.expect("restored opmode", restored.opmode, initial.opmode);
    checks.expect("restored net state", restored.net_state, initial.net_state);
    checks.expect("restored interface power", restored.if_flags & 0x41, initial.if_flags & 0x41);
    if (closeEarly && !interrupted) checks.expect("explicit close reason", restored.stop_reason, ItlAwdlClosed);
    showStatus(c);
    if (probe) {
        ItlAwdlTxStatusResponse tx = {};
        checks.expect("data TX completion status", readValue(c, ItlAwdlTxStatus, tx), kIOReturnSuccess);
        checks.expect("submitted count", tx.submitted, sent);
        checks.expect("pending cleared", tx.pending, 0);
        checks.expect("completion accounting", tx.completed + tx.aborted, tx.submitted);
        checks.expect("firmware accounting", tx.succeeded + tx.failed, tx.completed);
        printf("{\"fixed_data_probe\":true,\"length\":%lu,\"submitted\":%u,\"firmware_success\":%u,"
               "\"firmware_failed\":%u,\"aborted\":%u,\"last_firmware_status\":%u,\"echo_replies\":%u,"
               "\"regulatory_denied\":%u,\"peer_not_ready\":%u,\"busy\":%u}\n",
               probeLength, sent, tx.succeeded, tx.failed, tx.aborted, tx.last_firmware_status, echoes, denied, notReady, busy);
    }
    IOServiceClose(c);
    checks.expect("transport checksums", checksumFailed, 0);
    printf("{\"received\":%u,\"checksums_valid\":%u,\"checksums_failed\":%u,\"checksums_unavailable\":%u,\"native_airdrop\":false}\n",
           received, checksummed, checksumFailed, checksumUnavailable);
    return checks.report();
}

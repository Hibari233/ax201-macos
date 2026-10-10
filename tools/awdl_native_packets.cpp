// SPDX-License-Identifier: GPL-2.0-or-later
// Root-only, CPU-only native packet-pool diagnostics. No radio or interface.
#include <IOKit/IOKitLib.h>
#include <AWDL/ItlAwdlABI.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static kern_return_t invoke(io_connect_t client, const ItlAwdlNativePacketRequest &request,
    ItlAwdlNativePacketResponse &out, size_t input = sizeof(ItlAwdlNativePacketRequest),
    size_t output = sizeof(ItlAwdlNativePacketResponse)) {
    memset(&out, 0, sizeof(out));
    auto result = IOConnectCallStructMethod(client, ItlAwdlNativePackets, &request, input, &out, &output);
    if (!result && (output != sizeof(out) || out.version != ITL_AWDL_ABI_VERSION)) return kIOReturnBadArgument;
    return result;
}
static int selftest(io_connect_t client) {
    unsigned int passed = 0, failed = 0;
    auto check = [&](const char *name, kern_return_t result) {
        if (result == kIOReturnBadArgument) ++passed;
        else { ++failed; fprintf(stderr, "%s: 0x%x\n", name, result); }
    };
    ItlAwdlNativePacketRequest request = {1, 1, 0, 0};
    ItlAwdlNativePacketResponse out = {};
    check("short input", invoke(client, request, out, sizeof(request)-1));
    check("long input", IOConnectCallStructMethod(client, ItlAwdlNativePackets, &out, sizeof(out), nullptr, nullptr));
    check("short output", invoke(client, request, out, sizeof(request), sizeof(out)-1));
    request.version = 0; check("version", invoke(client, request, out)); request.version = 1;
    request.iterations = 0; check("zero iterations", invoke(client, request, out));
    request.iterations = 5; check("iterations limit", invoke(client, request, out)); request.iterations = 1;
    request.failure = 4; check("failure selector", invoke(client, request, out)); request.failure = 0;
    request.reserved = 1; check("reserved", invoke(client, request, out));
    printf("{\"invalid_packet_checks_passed\":%u,\"failed\":%u}\n", passed, failed);
    return failed ? 1 : 0;
}
int main(int argc, char **argv) {
    const bool check = argc == 2 && !strcmp(argv[1], "selftest");
    const bool run = argc == 4 && !strcmp(argv[1], "run") && strlen(argv[2]) == 1 &&
        argv[2][0] >= '1' && argv[2][0] <= '4' && strlen(argv[3]) == 1 &&
        argv[3][0] >= '0' && argv[3][0] <= '3';
    if (!check && !run) {
        fprintf(stderr, "Usage: awdl_native_packets selftest | run ITERATIONS(1..4) FAILURE(0..3)\n");
        return 2;
    }
    if (geteuid()) { fprintf(stderr, "Requires root.\n"); return 2; }
    auto service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AirportItlwm"));
    if (!service) { fprintf(stderr, "Driver not found.\n"); return 1; }
    io_connect_t client = IO_OBJECT_NULL;
    auto result = IOServiceOpen(service, mach_task_self(), ITL_AWDL_USER_CLIENT, &client);
    IOObjectRelease(service);
    if (result) { fprintf(stderr, "Open: 0x%x\n", result); return 1; }
    if (check) { auto code = selftest(client); IOServiceClose(client); return code; }
    const uint32_t n = argv[2][0] - '0', failure = argv[3][0] - '0';
    ItlAwdlNativePacketRequest request = {1, n, failure, 0};
    ItlAwdlNativePacketResponse out = {};
    result = invoke(client, request, out);
    IOServiceClose(client);
    if (result) { fprintf(stderr, "Native packets: 0x%x\n", result); return 1; }
    printf("{");
#define FIELD(name) printf("\"" #name "\":%u,", out.name)
    FIELD(version); FIELD(attempted); FIELD(initialized); FIELD(expected_failures);
    FIELD(packet_factories); FIELD(segment_factories); FIELD(injected_failures);
    FIELD(allocations); FIELD(prepared); FIELD(returned); FIELD(pattern_checks); FIELD(bytes_checked);
    FIELD(metadata_checks); FIELD(bounds_denied); FIELD(pools_before); FIELD(pools_after);
    FIELD(packets_before); FIELD(packets_after); FIELD(last_error);
#undef FIELD
    printf("\"native_airdrop\":false}\n");
    const uint32_t normal = failure ? 0 : n;
    const bool okay = !out.last_error && out.attempted == n && out.initialized == normal &&
        out.expected_failures == n - normal && out.injected_failures == n - normal &&
        !out.pools_before && !out.pools_after && !out.packets_before && !out.packets_after &&
        out.allocations == normal * 24 && out.prepared == normal * 24 && out.returned == normal * 24 &&
        out.pattern_checks == normal * 24 && out.bytes_checked == normal * 22864 &&
        out.metadata_checks == normal * 48 && out.bounds_denied == normal * 24;
    return okay ? 0 : 1;
}

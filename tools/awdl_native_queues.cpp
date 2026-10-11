// SPDX-License-Identifier: GPL-2.0-or-later
// Root-only, CPU-only native queue diagnostics. Queues stay disabled; no radio or interface.
#include <IOKit/IOKitLib.h>
#include <AWDL/ItlAwdlABI.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static kern_return_t invoke(io_connect_t client, const ItlAwdlNativeQueueRequest &request,
    ItlAwdlNativeQueueResponse &out, size_t input = sizeof(ItlAwdlNativeQueueRequest),
    size_t output = sizeof(ItlAwdlNativeQueueResponse)) {
    memset(&out, 0, sizeof(out));
    auto result = IOConnectCallStructMethod(client, ItlAwdlNativeQueues, &request, input, &out, &output);
    if (!result && (output != sizeof(out) || out.version != ITL_AWDL_ABI_VERSION)) return kIOReturnBadArgument;
    return result;
}
static int selftest(io_connect_t client) {
    unsigned int passed = 0, failed = 0;
    auto check = [&](const char *name, kern_return_t result) {
        if (result == kIOReturnBadArgument) ++passed;
        else { ++failed; fprintf(stderr, "%s: 0x%x\n", name, result); }
    };
    ItlAwdlNativeQueueRequest request = {1, 1, 0, 0};
    ItlAwdlNativeQueueResponse out = {};
    check("short input", invoke(client, request, out, sizeof(request)-1));
    check("long input", IOConnectCallStructMethod(client, ItlAwdlNativeQueues, &out, sizeof(out), nullptr, nullptr));
    check("short output", invoke(client, request, out, sizeof(request), sizeof(out)-1));
    request.version = 0; check("version", invoke(client, request, out)); request.version = 1;
    request.iterations = 0; check("zero iterations", invoke(client, request, out));
    request.iterations = 3; check("iterations limit", invoke(client, request, out)); request.iterations = 1;
    request.stop_after = 8; check("stop selector", invoke(client, request, out)); request.stop_after = 0;
    request.reserved = 1; check("reserved", invoke(client, request, out));
    printf("{\"invalid_queue_checks_passed\":%u,\"failed\":%u}\n", passed, failed);
    return failed ? 1 : 0;
}
int main(int argc, char **argv) {
    const bool check = argc == 2 && !strcmp(argv[1], "selftest");
    const bool run = argc == 4 && !strcmp(argv[1], "run") && strlen(argv[2]) == 1 &&
        argv[2][0] >= '1' && argv[2][0] <= '2' && strlen(argv[3]) == 1 &&
        argv[3][0] >= '0' && argv[3][0] <= '7';
    if (!check && !run) {
        fprintf(stderr, "Usage: awdl_native_queues selftest | run ITERATIONS(1..2) STOP_AFTER(0..7)\n");
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
    const uint32_t n = argv[2][0] - '0', stage = argv[3][0] - '0';
    ItlAwdlNativeQueueRequest request = {1, n, stage, 0};
    ItlAwdlNativeQueueResponse out = {};
    result = invoke(client, request, out);
    IOServiceClose(client);
    if (result) { fprintf(stderr, "Native queues: 0x%x\n", result); return 1; }
    printf("{");
#define FIELD(name) printf("\"" #name "\":%u,", out.name)
    FIELD(version); FIELD(attempted); FIELD(completed); FIELD(expected_stops);
    FIELD(pools_created); FIELD(tx_created); FIELD(rx_created); FIELD(source_adds);
    FIELD(source_removes); FIELD(initialized); FIELD(finalized); FIELD(queues_released);
    FIELD(metadata_checks); FIELD(rejections); FIELD(allocations); FIELD(returned);
    FIELD(bytes_checked); FIELD(callback_calls); FIELD(pools_before); FIELD(pools_after);
    FIELD(packets_before); FIELD(packets_after); FIELD(tx_before); FIELD(tx_after);
    FIELD(rx_before); FIELD(rx_after); FIELD(cleanup_errors); FIELD(last_error);
#undef FIELD
    printf("\"native_airdrop\":false}\n");
    const uint32_t normal = stage ? 0 : n;
    const uint32_t tx = !stage || stage >= 2 ? n : 0;
    const uint32_t rx = !stage || stage >= 3 ? n : 0;
    const uint32_t sources = !stage || stage >= 5 ? 2*n : stage == 4 ? n : 0;
    const uint32_t initialized = !stage ? 4*n : stage == 6 ? n : stage == 7 ? 2*n : 0;
    const uint32_t metadata = !stage ? 10*n : stage >= 4 ? 2*n : 0;
    const uint32_t rejections = !stage ? 22*n : stage >= 4 ? 2*n : 0;
    const bool okay = !out.last_error && !out.cleanup_errors && !out.callback_calls &&
        out.attempted == n && out.completed == normal && out.expected_stops == n - normal &&
        out.pools_created == n && out.tx_created == tx && out.rx_created == rx &&
        out.source_adds == sources && out.source_removes == sources &&
        out.initialized == initialized && out.finalized == initialized && out.queues_released == tx + rx &&
        out.metadata_checks == metadata && out.rejections == rejections &&
        out.allocations == normal * 16 && out.returned == normal * 16 && out.bytes_checked == normal * 12624 &&
        !out.pools_before && !out.pools_after && !out.packets_before && !out.packets_after &&
        !out.tx_before && !out.tx_after && !out.rx_before && !out.rx_after;
    return okay ? 0 : 1;
}

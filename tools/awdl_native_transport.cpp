// SPDX-License-Identifier: GPL-2.0-or-later
// Privileged retained registration diagnostic. No BSD publication or RF.
#include <IOKit/IOKitLib.h>
#include <AWDL/ItlAwdlABI.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static kern_return_t invoke(io_connect_t client, const ItlAwdlNativeTransportRequest &request,
    ItlAwdlNativeTransportResponse &out, size_t input = sizeof(request), size_t output = sizeof(out)) {
    memset(&out, 0, sizeof(out));
    auto result = IOConnectCallStructMethod(client, ItlAwdlNativeTransport, &request, input, &out, &output);
    if (!result && (output != sizeof(out) || out.version != ITL_AWDL_ABI_VERSION)) return kIOReturnBadArgument;
    return result;
}
static int selftest(io_connect_t client) {
    unsigned passed = 0, failed = 0;
    auto check = [&](const char *name, kern_return_t r) {
        if (r == kIOReturnBadArgument) ++passed;
        else { ++failed; fprintf(stderr, "%s: 0x%x\n", name, r); }
    };
    ItlAwdlNativeTransportRequest request = {1, 0, 0, 0};
    ItlAwdlNativeTransportResponse out = {};
    check("short input", invoke(client, request, out, sizeof(request)-1));
    check("long input", IOConnectCallStructMethod(client, ItlAwdlNativeTransport, &out, sizeof(out), nullptr, nullptr));
    check("short output", invoke(client, request, out, sizeof(request), sizeof(out)-1));
    request.version = 0; check("version", invoke(client, request, out)); request.version = 1;
    request.operation = 3; check("operation", invoke(client, request, out)); request.operation = 1;
    request.stop_after = 7; check("stop selector", invoke(client, request, out));
    request.stop_after = 1; request.operation = 0; check("inspect with stop", invoke(client, request, out));
    request.operation = 2; check("clear with stop", invoke(client, request, out));
    request.stop_after = 0; request.reserved = 1; check("reserved", invoke(client, request, out));
    printf("{\"invalid_transport_checks_passed\":%u,\"failed\":%u}\n", passed, failed);
    return failed ? 1 : 0;
}
int main(int argc, char **argv) {
    const bool check = argc == 2 && !strcmp(argv[1], "selftest");
    const bool inspect = argc == 2 && !strcmp(argv[1], "status");
    const bool clear = argc == 2 && !strcmp(argv[1], "clear");
    const bool prepare = argc == 3 && !strcmp(argv[1], "prepare") && strlen(argv[2]) == 1 &&
        argv[2][0] >= '0' && argv[2][0] <= '6';
    if (!check && !inspect && !clear && !prepare) {
        fprintf(stderr, "Usage: awdl_native_transport selftest | status | clear | prepare STOP_AFTER(0..6)\n"); return 2;
    }
    if (geteuid()) { fprintf(stderr, "Requires root.\n"); return 2; }
    auto service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AirportItlwm"));
    if (!service) { fprintf(stderr, "Driver not found.\n"); return 1; }
    io_connect_t client = IO_OBJECT_NULL;
    auto result = IOServiceOpen(service, mach_task_self(), ITL_AWDL_USER_CLIENT, &client);
    IOObjectRelease(service);
    if (result) { fprintf(stderr, "Open: 0x%x\n", result); return 1; }
    if (check) { auto code = selftest(client); IOServiceClose(client); return code; }
    const uint32_t operation = prepare ? ItlAwdlTransportPrepare : clear ? ItlAwdlTransportClear : ItlAwdlTransportInspect;
    const uint32_t stage = prepare ? argv[2][0] - '0' : 0;
    ItlAwdlNativeTransportRequest request = {1, operation, stage, 0};
    ItlAwdlNativeTransportResponse out = {};
    result = invoke(client, request, out);
    IOServiceClose(client);
    if (result) { fprintf(stderr, "Native transport: 0x%x\n", result); return 1; }
    printf("{");
#define FIELD(name) printf("\"" #name "\":%u,", out.name)
    FIELD(version); FIELD(registered); FIELD(last_stage); FIELD(prepare_calls);
    FIELD(registrations); FIELD(deregistrations); FIELD(callback_calls); FIELD(cleanup_errors);
    FIELD(interface_count); FIELD(peer_count); FIELD(pool_count); FIELD(packet_count);
    FIELD(tx_count); FIELD(rx_count); FIELD(tx_interface); FIELD(rx_interface);
    FIELD(tx_enabled); FIELD(rx_enabled); FIELD(bsd_interfaces); FIELD(registration_copies); FIELD(last_error);
#undef FIELD
    printf("\"registry_id\":%llu,\"native_airdrop\":false}\n", (unsigned long long)out.registry_id);
    bool okay = !out.last_error && !out.cleanup_errors && !out.callback_calls &&
        !out.tx_enabled && !out.rx_enabled && !out.bsd_interfaces &&
        out.interface_count == out.peer_count && out.interface_count <= 1;
    if (out.registered) okay = okay && out.interface_count == 1 && out.registry_id &&
        out.pool_count == 1 && out.packet_count && out.tx_count == 1 && out.rx_count == 1 &&
        out.tx_interface && out.rx_interface && out.registration_copies == 2;
    else okay = okay && !out.pool_count && !out.packet_count && !out.tx_count && !out.rx_count &&
        !out.tx_interface && !out.rx_interface && !out.registration_copies;
    if (prepare) okay = okay && out.last_stage == (stage ? stage : 6) && out.registered == !stage;
    if (clear) okay = okay && !out.registered;
    return okay ? 0 : 1;
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Privileged explicit BSD publication. Radio enable remains unready.
#include <IOKit/IOKitLib.h>
#include <AWDL/ItlAwdlABI.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static kern_return_t invoke(io_connect_t client, const ItlAwdlNativeBSDRequest &request,
    ItlAwdlNativeBSDResponse &out, size_t input = sizeof(request), size_t output = sizeof(out)) {
    memset(&out, 0, sizeof(out));
    auto r = IOConnectCallStructMethod(client, ItlAwdlNativeBSD, &request, input, &out, &output);
    if (!r && (output != sizeof(out) || out.version != ITL_AWDL_ABI_VERSION)) return kIOReturnBadArgument;
    return r;
}
static int selftest(io_connect_t client) {
    unsigned passed = 0, failed = 0;
    auto check = [&](const char *name, kern_return_t r) {
        if (r == kIOReturnBadArgument) ++passed;
        else { ++failed; fprintf(stderr, "%s: 0x%x\n", name, r); }
    };
    ItlAwdlNativeBSDRequest request = {1, 0, {0, 0}};
    ItlAwdlNativeBSDResponse out = {};
    check("short input", invoke(client, request, out, sizeof(request)-1));
    check("long input", IOConnectCallStructMethod(client, ItlAwdlNativeBSD, &out, sizeof(out), nullptr, nullptr));
    check("short output", invoke(client, request, out, sizeof(request), sizeof(out)-1));
    request.version = 0; check("version", invoke(client, request, out)); request.version = 1;
    request.operation = 2; check("operation", invoke(client, request, out)); request.operation = 0;
    request.reserved[0] = 1; check("reserved0", invoke(client, request, out)); request.reserved[0] = 0;
    request.reserved[1] = 1; check("reserved1", invoke(client, request, out));
    printf("{\"invalid_bsd_checks_passed\":%u,\"failed\":%u}\n", passed, failed);
    return failed ? 1 : 0;
}
int main(int argc, char **argv) {
    const bool check = argc == 2 && !strcmp(argv[1], "selftest");
    const bool status = argc == 2 && !strcmp(argv[1], "status");
    const bool publish = argc == 2 && !strcmp(argv[1], "publish");
    if (!check && !status && !publish) {
        fprintf(stderr, "Usage: awdl_native_bsd selftest | status | publish\n"); return 2;
    }
    if (geteuid()) { fprintf(stderr, "Requires root.\n"); return 2; }
    auto service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AirportItlwm"));
    if (!service) { fprintf(stderr, "Driver not found.\n"); return 1; }
    io_connect_t client = IO_OBJECT_NULL;
    auto r = IOServiceOpen(service, mach_task_self(), ITL_AWDL_USER_CLIENT, &client);
    IOObjectRelease(service);
    if (r) { fprintf(stderr, "Open: 0x%x\n", r); return 1; }
    if (check) { auto result = selftest(client); IOServiceClose(client); return result; }
    ItlAwdlNativeBSDRequest request = {1, publish ? ItlAwdlBSDPublish : ItlAwdlBSDInspect, {0, 0}};
    ItlAwdlNativeBSDResponse out = {};
    r = invoke(client, request, out);
    IOServiceClose(client);
    if (r) { fprintf(stderr, "Native BSD: 0x%x\n", r); return 1; }
    printf("{");
#define FIELD(name) printf("\"" #name "\":%u,", out.name)
    FIELD(version); FIELD(publication_requested); FIELD(publish_calls); FIELD(prepare_calls);
    FIELD(prepare_error); FIELD(enable_calls); FIELD(enable_denied); FIELD(bsd_present);
    FIELD(bsd_clients); FIELD(tx_initialize_calls); FIELD(rx_initialize_calls); FIELD(tx_initialized);
    FIELD(rx_initialized); FIELD(tx_initialize_error); FIELD(rx_initialize_error); FIELD(tx_kernel_queue);
    FIELD(rx_kernel_queue); FIELD(tx_enabled); FIELD(rx_enabled); FIELD(callback_calls); FIELD(last_error);
#undef FIELD
    // The native BSD name is a fixed prefix and decimal unit, not user input.
    out.bsd_name[sizeof(out.bsd_name)-1] = 0;
    printf("\"registry_id\":%llu,\"bsd_name\":\"%s\",\"native_airdrop\":false}\n",
        (unsigned long long)out.registry_id, out.bsd_name);
    return out.last_error || out.prepare_error || out.tx_initialize_error || out.rx_initialize_error ||
        out.tx_enabled || out.rx_enabled || out.callback_calls ||
        (publish && (!out.publication_requested || out.publish_calls != 1)) ? 1 : 0;
}

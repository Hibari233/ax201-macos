// SPDX-License-Identifier: GPL-2.0-or-later
// Root-only native ABI/lifecycle checks. No radio or BSD interface activation.
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <AWDL/ItlAwdlABI.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static kern_return_t invoke(io_connect_t client, const ItlAwdlNativeInitRequest &request,
    ItlAwdlNativeInitResponse &out, size_t inputSize = sizeof(ItlAwdlNativeInitRequest),
    size_t outputSize = sizeof(ItlAwdlNativeInitResponse)) {
    memset(&out, 0, sizeof(out));
    auto result = IOConnectCallStructMethod(client, ItlAwdlNativeInit,
        &request, inputSize, &out, &outputSize);
    if (!result && (outputSize != sizeof(out) || out.version != ITL_AWDL_ABI_VERSION))
        return kIOReturnBadArgument;
    return result;
}

static int invalidRequests(io_connect_t client) {
    unsigned passed = 0, failed = 0;
    auto check = [&](const char *name, kern_return_t got) {
        if (got == kIOReturnBadArgument) ++passed;
        else { ++failed; fprintf(stderr, "%s: 0x%x\n", name, got); }
    };
    ItlAwdlNativeInitRequest request = {ITL_AWDL_ABI_VERSION, 1, 0, 0};
    ItlAwdlNativeInitResponse out = {};
    check("short input", invoke(client, request, out, sizeof(request)-1));
    check("long input", IOConnectCallStructMethod(client, ItlAwdlNativeInit,
        &out, sizeof(out), nullptr, nullptr));
    check("short output", invoke(client, request, out, sizeof(request), sizeof(out)-1));
    request.version = 0; check("version", invoke(client, request, out)); request.version = 1;
    request.iterations = 0; check("zero iterations", invoke(client, request, out));
    request.iterations = 9; check("too many iterations", invoke(client, request, out)); request.iterations = 1;
    request.stop_after = 3; check("unknown phase", invoke(client, request, out)); request.stop_after = 0;
    request.reserved = 1; check("reserved", invoke(client, request, out));
    printf("{\"invalid_request_checks_passed\":%u,\"failed\":%u}\n", passed, failed);
    return failed ? 1 : 0;
}

static kern_return_t startInvoke(io_connect_t client, const ItlAwdlNativeStartRequest &request,
    ItlAwdlNativeStartResponse &out, size_t in = sizeof(ItlAwdlNativeStartRequest),
    size_t size = sizeof(ItlAwdlNativeStartResponse)) {
    memset(&out, 0, sizeof(out));
    auto r = IOConnectCallStructMethod(client, ItlAwdlNativeStart, &request, in, &out, &size);
    if (!r && (size != sizeof(out) || out.version != ITL_AWDL_ABI_VERSION)) return kIOReturnBadArgument;
    return r;
}

static int startInvalidRequests(io_connect_t client) {
    unsigned passed = 0, failed = 0;
    auto check = [&](const char *name, kern_return_t got) {
        if (got == kIOReturnBadArgument) ++passed;
        else { ++failed; fprintf(stderr, "%s: 0x%x\n", name, got); }
    };
    ItlAwdlNativeStartRequest request = {ITL_AWDL_ABI_VERSION, 1, 0, 0};
    ItlAwdlNativeStartResponse out = {};
    check("short input", startInvoke(client, request, out, sizeof(request)-1));
    check("long input", IOConnectCallStructMethod(client, ItlAwdlNativeStart,
        &out, sizeof(out), nullptr, nullptr));
    check("short output", startInvoke(client, request, out, sizeof(request), sizeof(out)-1));
    request.version = 0; check("version", startInvoke(client, request, out)); request.version = 1;
    request.iterations = 0; check("zero iterations", startInvoke(client, request, out));
    request.iterations = 5; check("too many iterations", startInvoke(client, request, out)); request.iterations = 1;
    request.iterations = 2; check("repeated real creation", startInvoke(client, request, out)); request.iterations = 1;
    request.fail_peer_creation = 2; check("unknown failure", startInvoke(client, request, out)); request.fail_peer_creation = 0;
    request.reserved = 1; check("reserved", startInvoke(client, request, out));
    printf("{\"invalid_start_checks_passed\":%u,\"failed\":%u}\n", passed, failed);
    return failed ? 1 : 0;
}

static int startRun(io_connect_t client, uint32_t iterations, uint32_t fail) {
    ItlAwdlNativeStartRequest request = {ITL_AWDL_ABI_VERSION, iterations, fail, 0};
    ItlAwdlNativeStartResponse out = {};
    auto result = startInvoke(client, request, out);
    if (result) {
        fprintf(stderr, "Native start: 0x%x\n", result);
        if (result == kIOReturnNotPermitted)
            fprintf(stderr, "Native peer creation already attempted this boot; reboot before another attempt.\n");
        return 1;
    }
    printf("{");
#define FIELD(name) printf("\"" #name "\":%u,", out.name)
    FIELD(version); FIELD(attempted); FIELD(initialized); FIELD(attached);
    FIELD(start_succeeded); FIELD(expected_start_failures); FIELD(peer_managers_created);
    FIELD(peer_managers_released); FIELD(stopped); FIELD(partial_cleanups); FIELD(released);
    FIELD(live_before); FIELD(live_after); FIELD(provider_open_after); FIELD(controller_pointer_after);
    FIELD(last_error); FIELD(backend_calls); FIELD(bsd_interfaces); FIELD(cleanup_errors);
    FIELD(peer_creation_calls); FIELD(peer_live_before); FIELD(peer_live_after);
#undef FIELD
    printf("\"native_airdrop\":false}\n");
    const uint32_t n = iterations, started = fail ? 0 : n;
    const bool okay = !out.last_error && out.attempted == n && out.initialized == n && out.attached == n &&
        out.start_succeeded == started && out.expected_start_failures == (fail ? n : 0) &&
        out.peer_managers_created == started && out.peer_managers_released == started &&
        out.stopped == n && out.partial_cleanups == (fail ? n : 0) && out.released == n &&
        !out.live_before && !out.live_after && !out.provider_open_after && !out.controller_pointer_after &&
        !out.bsd_interfaces && !out.cleanup_errors && out.peer_creation_calls == n &&
        !out.peer_live_before && !out.peer_live_after;
    return okay ? 0 : 1;
}

static bool number(const char *text, uint32_t *value) {
    if (!*text) return false;
    uint32_t n = 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9' || n > 1000) return false;
        n = n * 10 + (*p - '0');
    }
    *value = n;
    return true;
}

// Call only after the one successful real-start trial. Reopen the user client
// to verify that client lifetime does not reset the boot-scoped restriction.
static int startDenied(io_connect_t client) {
    auto root = IORegistryGetRootEntry(kIOMainPortDefault);
    auto attempted = root ? IORegistryEntryCreateCFProperty(root,
        CFSTR("AX201NativePeerManagerAttempted"), kCFAllocatorDefault, 0) : nullptr;
    if (root) IOObjectRelease(root);
    const bool reserved = attempted && CFEqual(attempted, kCFBooleanTrue);
    if (attempted) CFRelease(attempted);
    if (!reserved) {
        fprintf(stderr, "Run the single real-start trial first; boot latch is absent.\n");
        return 2;
    }
    ItlAwdlNativeStartRequest request = {ITL_AWDL_ABI_VERSION, 1, 0, 0};
    ItlAwdlNativeStartResponse out = {};
    auto result = startInvoke(client, request, out);
    printf("{\"second_native_creation_denied\":%s,\"result\":%u}\n",
        result == kIOReturnNotPermitted ? "true" : "false", result);
    return result == kIOReturnNotPermitted ? 0 : 1;
}

static kern_return_t sessionInvoke(io_connect_t client, const ItlAwdlNativeSessionRequest &request,
    ItlAwdlNativeSessionResponse &out, size_t in = sizeof(ItlAwdlNativeSessionRequest),
    size_t size = sizeof(ItlAwdlNativeSessionResponse)) {
    memset(&out, 0, sizeof(out));
    auto r = IOConnectCallStructMethod(client, ItlAwdlNativeSession, &request, in, &out, &size);
    if (!r && (size != sizeof(out) || out.version != ITL_AWDL_ABI_VERSION)) return kIOReturnBadArgument;
    return r;
}

static int sessionInvalidRequests(io_connect_t client) {
    unsigned passed = 0, failed = 0;
    auto check = [&](const char *name, kern_return_t got) {
        if (got == kIOReturnBadArgument) ++passed;
        else { ++failed; fprintf(stderr, "%s: 0x%x\n", name, got); }
    };
    ItlAwdlNativeSessionRequest r = {ITL_AWDL_ABI_VERSION, ItlAwdlNativeInspect, {0, 0}};
    ItlAwdlNativeSessionResponse out = {};
    check("short input", sessionInvoke(client, r, out, sizeof(r)-1));
    check("long input", IOConnectCallStructMethod(client, ItlAwdlNativeSession,
        &out, sizeof(out), nullptr, nullptr));
    check("short output", sessionInvoke(client, r, out, sizeof(r), sizeof(out)-1));
    r.version = 0; check("version", sessionInvoke(client, r, out)); r.version = 1;
    r.operation = 3; check("operation", sessionInvoke(client, r, out)); r.operation = 0;
    r.reserved[0] = 1; check("reserved0", sessionInvoke(client, r, out)); r.reserved[0] = 0;
    r.reserved[1] = 1; check("reserved1", sessionInvoke(client, r, out));
    printf("{\"invalid_session_checks_passed\":%u,\"failed\":%u}\n", passed, failed);
    return failed ? 1 : 0;
}

static int sessionRun(io_connect_t client, uint32_t operation) {
    ItlAwdlNativeSessionRequest request = {ITL_AWDL_ABI_VERSION, operation, {0, 0}};
    ItlAwdlNativeSessionResponse out = {};
    auto result = sessionInvoke(client, request, out);
    if (result) { fprintf(stderr, "Native session: 0x%x\n", result); return 1; }
    printf("{");
#define FIELD(name) printf("\"" #name "\":%u,", out.name)
    FIELD(version); FIELD(state); FIELD(role); FIELD(interface_count); FIELD(peer_count);
    FIELD(provider_open); FIELD(controller_pointer); FIELD(bsd_interfaces); FIELD(backend_calls);
    FIELD(boot_reserved); FIELD(last_error);
#undef FIELD
    printf("\"registry_id\":%llu,\"native_airdrop\":false}\n", (unsigned long long)out.registry_id);
    if (out.last_error || out.bsd_interfaces) return 1;
    if (operation == ItlAwdlNativeCreate)
        return out.state == ItlAwdlNativeRetained && out.role == 6 &&
            out.interface_count == 1 && out.peer_count == 1 && out.provider_open &&
            out.controller_pointer && out.boot_reserved && out.registry_id ? 0 : 1;
    if (operation == ItlAwdlNativeRetire)
        return out.state != ItlAwdlNativeRetained && !out.interface_count && !out.peer_count &&
            !out.provider_open && !out.controller_pointer && !out.registry_id ? 0 : 1;
    return 0;
}

static kern_return_t configureInvoke(io_connect_t client, const ItlAwdlNativeConfigureRequest &r,
    ItlAwdlNativeConfigureResponse &out, size_t in = sizeof(ItlAwdlNativeConfigureRequest),
    size_t size = sizeof(ItlAwdlNativeConfigureResponse)) {
    memset(&out, 0, sizeof(out));
    auto result = IOConnectCallStructMethod(client, ItlAwdlNativeConfigure, &r, in, &out, &size);
    if (!result && (size != sizeof(out) || out.version != ITL_AWDL_ABI_VERSION ||
        out.trace_count > ITL_AWDL_CONFIGURE_TRACE_COUNT)) return kIOReturnBadArgument;
    return result;
}

static int configureInvalidRequests(io_connect_t client) {
    unsigned passed = 0, failed = 0;
    auto check = [&](const char *name, kern_return_t got) {
        if (got == kIOReturnBadArgument) ++passed;
        else { ++failed; fprintf(stderr, "%s: 0x%x\n", name, got); }
    };
    ItlAwdlNativeConfigureRequest r = {ITL_AWDL_ABI_VERSION, ItlAwdlConfigureInspect, {0, 0}};
    ItlAwdlNativeConfigureResponse out = {};
    check("short input", configureInvoke(client, r, out, sizeof(r)-1));
    check("long input", IOConnectCallStructMethod(client, ItlAwdlNativeConfigure,
        &out, sizeof(out), nullptr, nullptr));
    check("short output", configureInvoke(client, r, out, sizeof(r), sizeof(out)-1));
    r.version = 0; check("version", configureInvoke(client, r, out)); r.version = 1;
    r.operation = 2; check("operation", configureInvoke(client, r, out)); r.operation = 0;
    r.reserved[0] = 1; check("reserved0", configureInvoke(client, r, out)); r.reserved[0] = 0;
    r.reserved[1] = 1; check("reserved1", configureInvoke(client, r, out));
    printf("{\"invalid_configure_checks_passed\":%u,\"failed\":%u}\n", passed, failed);
    return failed ? 1 : 0;
}

static bool printNativeCall(const ItlAwdlNativeConfigureResponse &out) {
    static const char *names[] = {
#define ITL_NATIVE_REQUEST(name, type) #name,
#define ITL_NATIVE_IMPLEMENTED_REQUEST(name, type) #name,
#include "../AirportItlwm/AWDL/ItlAwdlNativeRequests.def"
#undef ITL_NATIVE_REQUEST
#undef ITL_NATIVE_IMPLEMENTED_REQUEST
    };
    static_assert(sizeof(names) / sizeof(names[0]) == 63, "24H32 backend trace names");
    printf("{");
#define FIELD(name) printf("\"" #name "\":%u,", out.name)
    FIELD(version); FIELD(present); FIELD(attempted); FIELD(native_result);
    FIELD(backend_before); FIELD(backend_after); FIELD(trace_count); FIELD(trace_dropped);
    FIELD(interface_enabled); FIELD(tx_enabled); FIELD(rx_enabled); FIELD(bsd_present);
#undef FIELD
    printf("\"registry_id\":%llu,\"trace\":[", (unsigned long long)out.registry_id);
    bool valid = !out.interface_enabled && !out.tx_enabled && !out.rx_enabled && !out.bsd_present;
    for (unsigned i = 0; i < out.trace_count; ++i) {
        const auto &entry = out.trace[i];
        const bool known = entry.vptr_offset >= 0xfa0 && entry.vptr_offset <= 0x1190 &&
            (entry.vptr_offset & 7) == 0;
        valid &= known;
        printf("%s{\"vptr_offset\":%u,\"method\":\"%s\",\"result\":%u}", i ? "," : "",
            entry.vptr_offset, known ? names[(entry.vptr_offset - 0xfa0) / 8] : "unknown", entry.result);
    }
    printf("],\"native_configuration_verified\":false,\"native_airdrop\":false}");
    return valid;
}

static int configureRun(io_connect_t client, uint32_t operation) {
    ItlAwdlNativeConfigureRequest r = {ITL_AWDL_ABI_VERSION, operation, {0, 0}};
    ItlAwdlNativeConfigureResponse out = {};
    auto result = configureInvoke(client, r, out);
    if (result) { fprintf(stderr, "Native configure request: 0x%x\n", result); return 1; }
    const bool valid = printNativeCall(out);
    printf("\n");
    // Native errors are diagnostic data. A zero result with no backend calls
    // can be a no-op; neither outcome asserts successful configuration.
    return valid && (operation != ItlAwdlConfigureRun || (out.present && out.attempted)) ? 0 : 1;
}

static kern_return_t templateInvoke(io_connect_t client, const ItlAwdlNativeConfigureRequest &r,
    ItlAwdlNativeTemplateResponse &out, size_t in = sizeof(ItlAwdlNativeConfigureRequest),
    size_t size = sizeof(ItlAwdlNativeTemplateResponse)) {
    memset(&out, 0, sizeof(out));
    auto result = IOConnectCallStructMethod(client, ItlAwdlNativeTemplate, &r, in, &out, &size);
    if (!result && (size != sizeof(out) || out.call.version != ITL_AWDL_ABI_VERSION ||
        out.call.trace_count > ITL_AWDL_CONFIGURE_TRACE_COUNT || out.template_length > 512 ||
        out.tlv_count > 512 / 3 || out.encoded_length > 628)) return kIOReturnBadArgument;
    return result;
}

static int templateInvalidRequests(io_connect_t client) {
    unsigned passed = 0, failed = 0;
    auto check = [&](const char *name, kern_return_t got) {
        if (got == kIOReturnBadArgument) ++passed;
        else { ++failed; fprintf(stderr, "%s: 0x%x\n", name, got); }
    };
    ItlAwdlNativeConfigureRequest r = {ITL_AWDL_ABI_VERSION, ItlAwdlConfigureInspect, {0, 0}};
    ItlAwdlNativeTemplateResponse out = {};
    check("short input", templateInvoke(client, r, out, sizeof(r)-1));
    check("long input", IOConnectCallStructMethod(client, ItlAwdlNativeTemplate,
        &out, sizeof(out), nullptr, nullptr));
    check("short output", templateInvoke(client, r, out, sizeof(r), sizeof(out)-1));
    r.version = 0; check("version", templateInvoke(client, r, out)); r.version = 1;
    r.operation = 2; check("operation", templateInvoke(client, r, out)); r.operation = 0;
    r.reserved[0] = 1; check("reserved0", templateInvoke(client, r, out)); r.reserved[0] = 0;
    r.reserved[1] = 1; check("reserved1", templateInvoke(client, r, out));
    printf("{\"invalid_template_checks_passed\":%u,\"failed\":%u}\n", passed, failed);
    return failed ? 1 : 0;
}

static int templateRun(io_connect_t client, uint32_t operation) {
    ItlAwdlNativeConfigureRequest r = {ITL_AWDL_ABI_VERSION, operation, {0, 0}};
    ItlAwdlNativeTemplateResponse out = {};
    auto result = templateInvoke(client, r, out);
    if (result) { fprintf(stderr, "Native template request: 0x%x\n", result); return 1; }
    printf("{\"call\":");
    const bool valid = printNativeCall(out.call);
    printf(",\"template_length\":%u,\"template_revision\":%u,\"tlv_count\":%u,"
           "\"encoded_length\":%u,\"tlv_kinds\":[", out.template_length, out.template_revision,
           out.tlv_count, out.encoded_length);
    bool comma = false;
    for (unsigned i = 0; i < 256; ++i) {
        if (!(out.tlv_kinds[i / 32] & (uint32_t(1) << (i % 32)))) continue;
        printf("%s%u", comma ? "," : "", i); comma = true;
    }
    printf("],\"synthetic_clock\":true,\"rf_transmitted\":false,\"native_airdrop\":false}\n");
    // A native producer error remains diagnostic data. The harness must
    // independently require a successful backend call and nonzero encoding.
    return valid && (operation != ItlAwdlConfigureRun || (out.call.present && out.call.attempted)) ? 0 : 1;
}

int main(int argc, char **argv) {
    const bool selftest = argc == 2 && !strcmp(argv[1], "selftest");
    const bool denied = argc == 2 && !strcmp(argv[1], "denied");
    const bool run = argc == 4 && !strcmp(argv[1], "run");
    const bool startSelftest = argc == 2 && !strcmp(argv[1], "start-selftest");
    const bool start = argc == 4 && !strcmp(argv[1], "start");
    const bool startDeniedTest = argc == 2 && !strcmp(argv[1], "start-denied");
    const bool sessionSelftest = argc == 2 && !strcmp(argv[1], "session-selftest");
    const bool session = argc == 3 && !strcmp(argv[1], "session");
    const bool configureSelftest = argc == 2 && !strcmp(argv[1], "configure-selftest");
    const bool configure = argc == 3 && !strcmp(argv[1], "configure");
    const bool templateSelftest = argc == 2 && !strcmp(argv[1], "template-selftest");
    const bool templateRequest = argc == 3 && !strcmp(argv[1], "template");
    uint32_t configureOperation = ItlAwdlConfigureInspect;
    if (configure || templateRequest) {
        if (!strcmp(argv[2], "run")) configureOperation = ItlAwdlConfigureRun;
        else if (strcmp(argv[2], "status")) configureOperation = 2;
    }
    uint32_t operation = ItlAwdlNativeInspect;
    if (session) {
        if (!strcmp(argv[2], "create")) operation = ItlAwdlNativeCreate;
        else if (!strcmp(argv[2], "retire")) operation = ItlAwdlNativeRetire;
        else if (strcmp(argv[2], "status")) operation = 3;
    }
    ItlAwdlNativeInitRequest request = {ITL_AWDL_ABI_VERSION, 0, 0, 0};
    if ((!selftest && !run && !denied && !startSelftest && !start && !startDeniedTest && !session && !sessionSelftest &&
         !configure && !configureSelftest && !templateRequest && !templateSelftest) ||
        ((configure || templateRequest) && configureOperation > ItlAwdlConfigureRun) ||
        (session && operation > ItlAwdlNativeRetire) ||
        (run && (!number(argv[2], &request.iterations) || request.iterations < 1 || request.iterations > 8 ||
                 !number(argv[3], &request.stop_after) || request.stop_after > 2)) ||
        (start && (!number(argv[2], &request.iterations) || request.iterations < 1 || request.iterations > 4 ||
                   !number(argv[3], &request.stop_after) || request.stop_after > 1))) {
        fprintf(stderr, "Usage: awdl_native_init selftest | denied | run ITERATIONS(1..8) STOP_AFTER(0..2)\n"
                        "       awdl_native_init start-selftest | start-denied | start ITERATIONS(1..4) FAIL_PEER(0..1)\n"
                        "       awdl_native_init session-selftest | session status|create|retire\n"
                        "       awdl_native_init configure-selftest | configure status|run\n"
                        "       awdl_native_init template-selftest | template status|run\n"
                        "Real peer creation is limited to once per boot; session create reuses a retained object.\n");
        return 2;
    }
    if ((denied && geteuid() == 0) || (!denied && geteuid() != 0)) {
        fprintf(stderr, "Use denied as a non-root user; all other commands require root.\n"); return 2;
    }
    auto service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AirportItlwm"));
    if (!service) { fprintf(stderr, "Driver not found\n"); return 1; }
    io_connect_t client = IO_OBJECT_NULL;
    auto result = IOServiceOpen(service, mach_task_self(), ITL_AWDL_USER_CLIENT, &client);
    IOObjectRelease(service);
    if (denied) {
        if (!result) IOServiceClose(client);
        printf("{\"non_root_denied\":%s}\n", result ? "true" : "false");
        return result ? 0 : 1;
    }
    if (result) { fprintf(stderr, "Open: 0x%x\n", result); return 1; }
    if (selftest) { int code = invalidRequests(client); IOServiceClose(client); return code; }
    if (startSelftest) { int code = startInvalidRequests(client); IOServiceClose(client); return code; }
    if (startDeniedTest) { int code = startDenied(client); IOServiceClose(client); return code; }
    if (sessionSelftest) { int code = sessionInvalidRequests(client); IOServiceClose(client); return code; }
    if (configureSelftest) { int code = configureInvalidRequests(client); IOServiceClose(client); return code; }
    if (configure) { int code = configureRun(client, configureOperation); IOServiceClose(client); return code; }
    if (templateSelftest) { int code = templateInvalidRequests(client); IOServiceClose(client); return code; }
    if (templateRequest) { int code = templateRun(client, configureOperation); IOServiceClose(client); return code; }
    if (session) { int code = sessionRun(client, operation); IOServiceClose(client); return code; }
    if (start) { int code = startRun(client, request.iterations, request.stop_after); IOServiceClose(client); return code; }
    ItlAwdlNativeInitResponse out = {};
    result = invoke(client, request, out);
    IOServiceClose(client);
    if (result) { fprintf(stderr, "Native init: 0x%x\n", result); return 1; }
    printf("{\"attempted\":%u,\"base_initialized\":%u,\"virtual_initialized\":%u,\"released\":%u,"
           "\"live_before\":%u,\"live_after\":%u,\"role\":%u,\"methods_checked\":%u,"
           "\"method_failures\":%u,\"provider_open_after\":%u,\"last_error\":%u,\"provider_opened\":%u,\"native_airdrop\":false}\n",
           out.attempted, out.base_initialized, out.virtual_initialized, out.released,
           out.live_before, out.live_after, out.role, out.methods_checked,
           out.method_failures, out.provider_open_after, out.last_error, out.provider_opened);
    const uint32_t n = request.iterations;
    const bool okay = !out.last_error && out.attempted == n && out.released == n && out.base_initialized == n &&
        out.virtual_initialized == (request.stop_after == 1 ? 0 : n) &&
        out.role == (request.stop_after == 1 ? 0U : 6U) &&
        out.methods_checked == (request.stop_after == 0 ? 128*n : 0) &&
        out.provider_opened == (request.stop_after == 1 ? 0 : n) &&
        !out.live_before && !out.live_after && !out.method_failures && !out.provider_open_after;
    return okay ? 0 : 1;
}

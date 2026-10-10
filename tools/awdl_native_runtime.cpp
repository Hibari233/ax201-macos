// SPDX-License-Identifier: GPL-2.0-or-later
// Explicit root-only, bounded native synchronization. Never starts at boot.
#include <IOKit/IOKitLib.h>
#include <AWDL/ItlAwdlABI.h>
#include <AWDL/AwdlFrame.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static kern_return_t invoke(io_connect_t client, const ItlAwdlBeginRequest &request,
    ItlAwdlNativeRuntimeResponse &out, size_t in = sizeof(ItlAwdlBeginRequest),
    size_t size = sizeof(ItlAwdlNativeRuntimeResponse)) {
    memset(&out, 0, sizeof(out));
    auto r = IOConnectCallStructMethod(client, ItlAwdlNativeRuntime, &request, in, &out, &size);
    if (!r && (size != sizeof(out) || out.activation.version != ITL_AWDL_ABI_VERSION))
        return kIOReturnBadArgument;
    return r;
}
int main(int argc, char **argv) {
    const bool test = argc == 2 && !strcmp(argv[1], "selftest");
    ItlAwdlBeginRequest request = {ITL_AWDL_ABI_VERSION, 0, 1000, 0, {2,1,2,3,4,5}, {0,0}};
    if (geteuid() || (!test && argc != 3)) {
        fprintf(stderr, "Root only: awdl_native_runtime selftest | OWNED_PEER_MAC DWELL_MS(1000..5000)\n"); return 2;
    }
    if (!test) {
        unsigned m[6]; char tail;
        if (sscanf(argv[1], "%2x:%2x:%2x:%2x:%2x:%2x%c", &m[0],&m[1],&m[2],&m[3],&m[4],&m[5],&tail) != 6)
            return 2;
        for (unsigned i=0; i<6; ++i) request.peer[i] = uint8_t(m[i]);
        char *end = nullptr; auto dwell = strtoul(argv[2], &end, 10);
        if (!*argv[2] || *end || dwell < 1000 || dwell > 5000 || !ItlAwdl::unicastMac(request.peer)) return 2;
        request.dwell_ms = uint32_t(dwell);
    }
    auto service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AirportItlwm"));
    if (!service) return 1;
    io_connect_t client = IO_OBJECT_NULL;
    auto error = IOServiceOpen(service, mach_task_self(), ITL_AWDL_USER_CLIENT, &client);
    IOObjectRelease(service);
    if (error) { fprintf(stderr,"Open: 0x%x\n",error); return 1; }
    ItlAwdlNativeRuntimeResponse out = {};
    if (test) {
        unsigned passed=0,failed=0;
        auto check=[&](kern_return_t r) { if(r==kIOReturnBadArgument) ++passed; else ++failed; };
        check(invoke(client,request,out,sizeof(request)-1));
        check(invoke(client,request,out,sizeof(request),sizeof(out)-1));
        request.version=0; check(invoke(client,request,out)); request.version=1;
        request.channel=6; check(invoke(client,request,out)); request.channel=0;
        request.dwell_ms=999; check(invoke(client,request,out));
        request.dwell_ms=5001; check(invoke(client,request,out)); request.dwell_ms=1000;
        request.reserved=1; check(invoke(client,request,out)); request.reserved=0;
        request.padding[1]=1; check(invoke(client,request,out)); request.padding[1]=0;
        request.peer[0]=3; check(invoke(client,request,out));
        printf("{\"invalid_runtime_checks_passed\":%u,\"failed\":%u}\n",passed,failed);
        IOServiceClose(client); return failed ? 1 : 0;
    }
    error=invoke(client,request,out);
    IOServiceClose(client);
    if(error) { fprintf(stderr,"Runtime: 0x%x\n",error); return 1; }
    printf("{");
#define FIELD(name) printf("\"" #name "\":%u,", out.name)
    FIELD(attempted); FIELD(ready); FIELD(stopped); FIELD(last_error);
    FIELD(timer_ticks); FIELD(clock_valid); FIELD(aw_events); FIELD(aw_event_errors);
    FIELD(rx_handed_off); FIELD(rx_rejected); FIELD(rx_last_error); FIELD(role_updates);
    FIELD(submitted); FIELD(completed); FIELD(succeeded); FIELD(failed); FIELD(pending); FIELD(aborted);
    FIELD(timing_samples); FIELD(timing_invalid); FIELD(max_completion_delay_us); FIELD(stop_reason);
    FIELD(sync_enabled_after); FIELD(native_disable_calls); FIELD(tx_limit); FIELD(channel);
#undef FIELD
    const auto &a=out.activation;
    printf("\"activation\":{\"attempted\":%u,\"native_result\":%u,\"trace_dropped\":%u,"
           "\"registry_id\":%llu,\"interface_enabled\":%u,\"bsd_present\":%u,"
           "\"tx_enabled\":%u,\"rx_enabled\":%u,\"trace\":[",
           a.attempted,a.native_result,a.trace_dropped,(unsigned long long)a.registry_id,
           a.interface_enabled,a.bsd_present,a.tx_enabled,a.rx_enabled);
    for(unsigned i=0;i<a.trace_count && i<ITL_AWDL_CONFIGURE_TRACE_COUNT;++i)
        printf("%s{\"slot\":%u,\"result\":%u}",i ? "," : "",a.trace[i].vptr_offset,a.trace[i].result);
    printf("]},\"native_airdrop\":false}\n");
    return out.last_error || !out.ready || !out.stopped || out.sync_enabled_after ? 1 : 0;
}

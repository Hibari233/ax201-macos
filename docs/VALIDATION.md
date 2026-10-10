# Validation evidence

This record separates the earlier development prototype from artifacts built by
this repository. It is not a certification for all AX201 systems.

## Imported prototype observations

Recorded before the public fork, on AX201 / ROG STRIX B660-I GAMING WIFI /
macOS Sequoia 15.8.1 (24H32). Source imported on 2026-10-10.
The revision 9 build 2 prototype binary SHA-256 was:

`9bdb68efe691f7eded2baa529c7b576d1a24600d105281409287434ac45cd6af`

- Native Wi-Fi scanning and a WPA2-Personal 2.4 GHz connection worked.
- Revision 9 build 1: timeout, client exit, explicit close and Wi-Fi scan
  cancellation restored the linked diagnostic lifecycle; 76/76 router pings
  succeeded during the bounded test set.
- Revision 9 build 2: 33 diagnostic user-client ABI checks and 7 active-session
  boundary checks passed; 24/24 router pings succeeded, timeout restoration
  completed, and the firmware generation did not change.
- An additional channel-change restoration fix was compiled. An actual AP
  channel change was **not** tested.
- Earlier exclusive-monitor experiments parsed owned-peer AWDL PSF/MIF frames
  on channels 6 and 149 after fixing FCS handling. Those results do not prove
  simultaneous associated Wi-Fi + AWDL, peer data traffic or native AirDrop.
- The native Sequoia declarations were checked as a compile-only ABI experiment;
  no native AWDL interface was instantiated or exercised.

Raw captures, local kernel collections, host identities, deployment scripts,
passwords and EFI backups are not part of this repository. No diagnostic session
is automatically started by building or running the offline tests.

## Reproducible checks in this repository

Initial local check on 2026-10-10:

- Commit `06511578efd36c38af2b39fca3014d225401f3d1`: both Sequoia variants compiled
  with Apple Clang 17 and passed ad-hoc signature verification. The Wi-Fi variant
  compiled 74 sources; the AWDL variant compiled 76. Symbol inspection confirmed
  the experimental user client is absent from Wi-Fi and present in AWDL.
- The compile-only native ABI fixture and x86_64 diagnostic client built.
- Both 200,000-case mutation suites and all 5 Python decoder tests passed under
  local UBSan. The local ASan runtime did not finish starting; that run was
  stopped and is **not a pass**. Linux CI runs the combined ASan/UBSan suite.
- Neither newly built kext was installed as part of repository preparation.

`python3 scripts/test.py` exercises synthetic frame parsing, corrupted/truncated
inputs, channel-sequence decoding, timestamp wrapping, schedule projection and
regulatory checks under ASan/UBSan. GitHub Actions also compiles both Sequoia
variants, the native fixture and the diagnostic client.

Passing these checks demonstrates only those tested properties. Build manifests
set `runtime_validated: false`; a newly compiled binary inherits no hardware
pass result merely because the source is similar to the earlier prototype.

## Revision 10 bounded receive validation (2026-10-10)

The same development AX201, PCI ID `0x7af0`, Sequoia 15.8.1 (24H32), loaded
revision 10 build 1 from source commit
`a1f6b81a0769b227566f0bbfc7a9fb25889e2331`:

- Binary SHA-256: `27ae295f5322fc302f745eadf69e90d3401a8711502c1dec744786385c498c48`.
- Loaded UUID: `5D01C2C8-8E5A-3F59-A045-54BCE05FA7CB`; matched the locally built
  binary. Staged signature and guarded EFI file hashes were verified.
- [CI for this source](https://github.com/Hibari233/ax201-macos/actions/runs/38026710869)
  passed Linux ASan/UBSan and both Sequoia builds. The data suite also passed
  native Intel macOS ASan/UBSan and local UBSan.

An owned Mac generated AWDL traffic. Tests used exclusive receive-only monitor
on channel 149 with ordinary Wi-Fi temporarily powered off; management remained
on Ethernet. These are received **records**, not necessarily unique packets:

| Bounded test | Observation |
| --- | --- |
| 20-second timeout | 194 IPv6 records: 186 ICMPv6 and 8 UDP; 298 peer management records |
| Explicit close before a 30-second deadline | 220 IPv6 fragments: 110 of 1280 bytes and 110 of 276 bytes; 18 active/cleanup checks passed |
| Test process exits without closing the client | Driver returned to STA/idle, cleared data mode and queue |
| 1280-byte ICMPv6, explicit close | 108 records, all 108 transport checksums valid; 19 active/cleanup checks passed |
| 1500-byte unfragmented stimulus | Zero accepted data records despite peer management RX; unresolved, **not a pass** |

All receive sessions reported zero malformed/unsupported records and zero queue
overflow. This does not count frames the firmware never delivered or frames
excluded by the peer/channel/protocol filters. The first two captures predate
the checksum client; no checksum claim is made for those captures. IPv6 fragment
reassembly was not tested. Offline fixtures cover the 1500-byte decoder bound,
but that does not qualify real 1500-byte reception.

The prior 33 ABI checks and 11 new data ABI checks passed before each session.
Timeout, explicit close and client death restored STA mode; Wi-Fi rejoined its
WPA2 2.4 GHz network after the power restore. Final router connectivity over
the Wi-Fi interface was 10/10 pings. Display/GPU configuration was retained;
whole-system sleep was not tested.

This validates a diagnostic receive path only. It does not validate data TX,
peer election, concurrent STA + AWDL operation, native interface integration or
AirDrop file transfer. Later source builds do not inherit this exact binary's
hardware observations automatically.

## Revision 11 bounded transmit validation (2026-10-10)

The same AX201 / Sequoia 24H32 machine booted revision 11 build 1 from
`0313b9ec857dd71787e07d7be2b876144ca651bd`:

- Kext binary SHA-256: `a4b4828b853136007465867eb4101014f607d466b99514ac60a9b6416e183721`
- Loaded UUID: `AD0F730C-68F9-3C28-BFC4-302DE2870451`
- Revision 10 was backed up before the guarded EFI switch; EFI configuration,
  graphics and power settings were retained.
- Local UBSan, Intel macOS ASan+UBSan, and all three
  [CI jobs for this commit](https://github.com/Hibari233/ax201-macos/actions/runs/38028557020)
  passed. Offline echo tests include an independently calculated odd-length
  checksum fixture, 64/65/1280/1499/1500-byte cases, reply identity/integrity,
  truncation, bounds and exact in-place encapsulation.

Each TX test selected the same owned Mac, used exclusive monitor mode and
automatically restored Wi-Fi power afterwards. These are **diagnostic frames**;
the sender has no negotiated AWDL peer session or native network interface.

| Test | Submitted | Firmware success / failure | Valid echo replies |
| --- | ---: | ---: | ---: |
| Channel 6, 64 bytes, periodic host trigger | 8 | 3 / 5 | 0 |
| Channel 6, 64 bytes, trigger after peer RX | 8 | 3 / 5 | 0 |
| Channel 6, 1280 bytes, trigger after peer RX | 8 | 1 / 7 | 0 |
| Channel 6, 1500 bytes, trigger after peer RX | 8 | 2 / 6 | 0 |
| Channel 149, restricted by firmware policy | 0 | 0 / 0; 74 calls denied | 0 |

The RX-trigger variant changes the host client, not the installed kext. The
final failure status in the channel-6 runs was `0x83` (long retry limit).
Triggering on peer reception did not establish bidirectional IPv6. Firmware
success is **not** evidence of a received/accepted IPv6 packet, working peer
negotiation, a native AirDrop service, or reliable MTU-1500 delivery.

Every run passed 33 existing ABI checks, 18 data ABI checks and 23 active
TX/lifecycle/accounting checks. All pending TX counts were zero after cleanup.
A subsequent channel-149 RX regression received **156 packets of 1280 bytes**;
all transport checksums passed, with zero malformed/unsupported/dropped records
and 20 active RX/lifecycle checks passed. Counts may include radio duplicates.
Final WPA2 reconnection and 10/10 router pings were verified on the Wi-Fi route.
Display health and its 240 Hz mode remained intact. True system sleep was not
tested; a six-hour awake assertion was used for development.

The later [native backend audit](SEQUOIA-AWDL-BACKEND.md) corrected two request
extents and compiled successfully. That audit is not runtime native-interface
validation and is not part of this exact installed binary's evidence.

## Revision 12 native initialization validation (2026-10-10)

The same AX201 / Sequoia 24H32 machine booted the native initialization probe
from `1ec3158db9a80d8de74ec1dff80e5567c814d72f`:

- Binary SHA-256: `67a241e3a911930a2abb7f6c7e50dc93a0ab9d76c9326bdd2cb7972d70b15609`
- Loaded UUID: `7664C1EC-ADC3-33EB-B5C4-B4DB56BDCC3D`
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38031186391)
  passed Linux ASan/UBSan, both Sequoia builds, the native ABI fixture and all
  three diagnostic clients. The staged kext passed strict signature verification;
  revision 11 remains available for rollback.

Each stop-after phase ran once, then eight times in one gated request:

| Phase | Objects created / released | Controller opens / closes | Contract checks |
| --- | ---: | ---: | ---: |
| Base initialization only | 9 / 9 | 0 / 0 | 0 |
| Role-6 virtual initialization | 9 / 9 | 9 / 9 | 0 |
| Virtual initialization and dispatch contract | 9 / 9 | 9 / 9 | 1,152 |

All requests reported zero live probe instances before/after, no remaining
controller open, no method failures and no initialization error. Eight malformed
request checks passed. A non-root client was denied after successful root access
to the same loaded driver. The contract checks exercise null/non-null dispatch
through the 63 unsupported backend methods, the unsupported MAC setter, and the
corrected by-value self-address getter. These bounded observations do not prove
all native allocations are leak-free, and phase exits do not inject allocation
failures into Apple's code.

Ordinary WPA2 remained associated throughout; the post-test Wi-Fi route passed
10/10 router pings. GPU binary hashes/settings, display-power error counters and
the 240 Hz display mode passed the retained health checks. A temporary six-hour
awake assertion was active; true system sleep was not tested.

Only initialization and disposal are qualified here. `start()` remains disabled:
there is no started peer manager, native BSD interface, new radio operation or
AirDrop file-transfer result.

## Revision 13 native start probe: incomplete validation (2026-10-10)

The same machine loaded `79804b320be7a41332cd82c19613ccef8e0e237b`:

- Binary SHA-256: `404e163580e614995b3e1e17a930896e178afdd2cf14387a2de686c54f325265`
- Loaded UUID: `F32024F3-757A-3532-AAD9-4C794C11ABBD`
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38033111513)
  passed all three jobs. The compiled class still matched 560 native vtable
  slots and 63 backend methods.
- Sixteen malformed initialization/start requests passed; non-root access was
  denied after successful root access to this loaded driver.

One deliberately failed peer creation and one successful native start/stop
passed, including the wait for deferred native release. The successful cycle
created and released one `IO80211AWDLPeerManager`. Both paths ended with zero
interface/peer-manager instances, no controller open or AWDL pointer, no
remaining audited log/MAC resources and no BSD interface. The backend request
count was zero, so this is not a qualification of any firmware request.

Four consecutive deliberately failed peer creations then passed. **During the
following four-cycle normal-start request, the management connection was lost**;
neither completion nor post-test health was available. IPv4 and IPv6 SSH both
failed afterwards. After the user reported a frozen display and restarted, a
panic report confirmed a page fault at `0xb8` in peer-manager logging from an
asynchronous native power callback. No system sleep preceded the panic.
Repeated normal start/stop is therefore **not qualified**, and the
planned concurrent-client and final Wi-Fi checks did not complete. Revision 12
is retained for rollback. These probes are never started automatically at boot.

An earlier build (`ec266b1`) returned before native interface release settled;
the successful single-start trial's following userspace count was zero at
11 ms. Revision 13's outside-gate wait addresses that observation, but it does
not address the native shared-log allocation lifetime issue subsequently found
in the [backend audit](SEQUOIA-AWDL-BACKEND.md#recreating-the-native-manager-panics-on-24h32).
The next source revision restricts real creation to one attempt per boot; its
hardware validation must be reported separately. Native packet queues,
BSD registration and AirDrop transfers remain unimplemented.

## Revision 13c containment observations (2026-10-10)

The same machine loaded source `cb67c9df35bf8963cfb6a87d2544c8ca8d12f213`,
binary `ab579ce06ec17ee3d34c73f370547e812573511149f97aad0782aabe44aa2e03`,
UUID `1519EBC5-8005-31D5-954A-D353A6A0B1DE`.
[Its CI](https://github.com/Hibari233/ax201-macos/actions/runs/38034689592)
passed all three jobs; the actual compiled native class matched all 560 audited
slots and 63 backend methods.

Seventeen malformed requests passed. An eight-cycle init-only test stopped at
cycle five with a still-counted interface and no method/open error; all 640
dispatch checks had passed. The following userspace count was zero. This is an
incomplete initialization regression result, not an eight-cycle pass. The next
source revision applies the outside-gate cleanup wait to init-only requests as
well as started objects.

After confirming zero instances, four injected failures, one real start/stop,
and four more injected failures passed. Three separate reopened-client attempts
to recreate the real manager were rejected with `kIOReturnNotPermitted`,
including an attempt after the later injected failures. Final instance counts
were zero. The host stayed online with no new panic report; Wi-Fi-route router
checks passed 10/10 and retained display/GPU checks passed at 240 Hz. The initial
combined harness stopped at the init result, so its planned concurrent-denial
test did not run. These observations qualify containment only, not repeated
native creation or AirDrop.

## Revision 14 retained native object validation (2026-10-10)

The same AX201 / Sequoia 24H32 host loaded
`7736e7e82d3b4fe07a0f7fa91d4a29961825ac10`:

- Binary SHA-256: `c78c0e6277fc9ee98e7a29940a5d3d520bc52e4c6efb232c501d36e46333b53b`
- Loaded UUID: `1D4F3DFF-FBFA-302F-899C-710BF2A803A7`; matched the built image
  and guarded EFI installation. Revision 13c is backed up.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38035401355)
  passed all three jobs. The fixture and actual compiled native class each
  matched 560 slots and 63 backend methods.

Twenty-four malformed-request checks passed; a non-root client was denied.
The revised init-only cleanup wait passed eight cycles in each of the three
phases, plus eight complete cycles after native retirement: **32 cycles and
2,048 dispatch checks**, with settled instance counts and no remaining opens
or method errors. Four injected peer-creation failures passed before the
retained object, and four more passed after retirement.

One real role-6 object and one Apple AWDL peer manager were retained across
five additional create requests from separately opened/closed clients. The
registry ID stayed identical throughout the bounded observation of at least
50 seconds, with exactly one interface and peer manager, one controller open,
and the expected native controller pointer. Competing init/start probes were
denied while it existed. The native registry entry reported current/device
power state 1. There was no BSD interface and no native backend request; this
does not qualify radio operation or real power transitions.

Explicit retirement returned both instance counts, the controller open and
AWDL pointer to zero. A second retirement was harmless. Recreating through
either the retained API or the older real-start probe was denied; injected
failures and init-only tests did not reset that restriction. No new panic
report appeared. The host stayed connected, all 10 Wi-Fi-route pings during
the retained period succeeded, and final GPU/display health checks passed
with the existing 240 Hz configuration.

This validates bounded ownership and containment on this image. It does not
validate sustained idle, controller unload, simultaneous client races, native
packet queues, concurrent STA/AWDL RF operation, discovery or AirDrop transfer.
The diagnostic object was retired at the end; nothing starts it at boot.

## Required hardware matrix before a binary release

| Test | Current qualification |
| --- | --- |
| Fresh public-fork binary boot and loaded-hash verification | Tested image identities through revision 18 verified; release matrix remains incomplete |
| WPA2 2.4 GHz connect/reconnect and throughput | Bounded reconnection and 12/12 Wi-Fi pings around revision 18 publication; sustained throughput pending |
| WPA2 5 GHz, supported channel groups | Pending |
| WPA2/WPA3 transition with optional PMF | Pending |
| WPA3-only with mandatory PMF, wrong password, rekey | Unsupported / pending implementation |
| Peer AWDL RX while STA stays associated on the same channel | Pending |
| Exclusive AWDL IPv6 RX | Bounded 1280-byte reception/checksums verified; unfragmented 1500-byte case unresolved |
| Fixed AWDL data TX | Bounded firmware completions observed; zero echo replies, not bidirectional IPv6 |
| Native role-6 object initialization/disposal | Revision 14 passed 32 cycles and 2,048 dispatch checks with deferred cleanup accounted for |
| Native role-6 start/stop | One retained object reused across clients, then retired; recreation denied after the confirmed 24H32 logging panic |
| Native packet/queue ownership | Packet memory and unpublished cleanup qualified; revision 18 attached BSD and initialized real netif queues; enabled delivery and live BSD teardown pending |
| Concurrent channel scheduling with normal data traffic | Pending implementation |
| Native discovery and bidirectional AirDrop file hashes | Pending implementation |
| Wi-Fi power cycling, repeated sleep/wake and roaming | Incomplete; whole-system sleep needs a separately stable platform |
| Sustained transfer, long idle and firmware error recovery | Pending |

Report hardware results with the source commit, binary hash, OS build, adapter
PCI ID, AP security/channel configuration, exact steps and failures as well as
successes. Keep identifiable network information out of public reports.

## Native packet-pool qualification — 2026-10-10

Source `5c0c21b46ccf2649f96e6f5732742d9f8dea64d7`, AX201 / Sequoia 15.8.1
24H32:

- Binary SHA-256: `12bb4004cfa8dea313a23ef4783a24d611bb0074533c6fa48b369512b5163b9c`
- Loaded UUID: `9BF4CFBC-2A81-3731-AF0E-8E925E445462`; both loaded UUID and
  external EFI binary hash were verified after reboot.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38037038430)
  passed Linux sanitizer tests and both Sequoia variants (AWDL: 78 sources).
- Compile fixtures matched 235 packet/pool virtual slots across four native
  classes. Actual driver packet/pool classes matched 119 slots including 15
  overrides. The native AWDL interface still matched its 560 checked slots and
  63 backend methods. These checks establish layout, not networking behavior.

The root-only CPU memory test passed eight malformed-request checks, nine
normal pool cycles and 15 injected-failure cycles (five each at the first
packet, fourth packet and first memory-segment factory). The normal cycles
completed 216 packet allocations, preparations, pattern checks and returns;
205,776 bytes were compared across simultaneous packet leases. All 432 metadata
checks and 216 bounds rejections passed. Every settled pool/packet instance
count returned to zero, including after partial initialization failures.
Non-root user-client access was denied after a working root positive control.

The kernel expanded the requested 64-packet pool to 5,824 packet objects and
728 segment objects on this target. The requested count is therefore **not**
an actual pool size limit. The test reports these factory counts and does not
assume that the kernel keeps the requested counts. Its payload work remains
bounded to eight simultaneous leases and three lengths per normal cycle.

All 15 interface-bound WPA2 router pings passed around the memory tests. No new
kernel panic report appeared. Final health checks confirmed the existing
1920×1080 at 240 Hz configuration, display/GPU hashes and power settings, zero
GPU I/O failures and zero flip-latch timeouts. This is configuration and
counter evidence, not a new visual desktop confirmation.

No native peer manager, queue, BSD interface or radio transmission was created
by this test, and the native peer-manager boot latch remains unused. PTM fields
were storage round-trip tests only. Queue ownership, native AWDL transport,
peer discovery and AirDrop file transfers remain unqualified.

## Disabled native queue qualification — 2026-10-10

Corrected source `c3c98f743371d8a771ac6f3efabe50b70cf07679` (revision 16b),
AX201 / Sequoia 15.8.1 24H32:

- Binary SHA-256: `6764edeab3a8541dd0799d9b229621ca8a9a02de9a04415581b0b9c2dc53aba7`
- Loaded UUID: `A3AB2078-8F6A-3C27-975C-9A605206A2F7`; loaded identity and
  external EFI hash matched after reboot. Verified revision 15 rollback retained.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38039032921)
  passed Linux ASan/UBSan and both Sequoia builds (AWDL: 79 sources), including
  the native ABI fixture and five diagnostic clients.
- The two queue fixtures and actual queue subclasses each matched 189 native
  slots. The unchanged packet/pool and AWDL interface layouts also passed their
  119-slot and 560-slot/63-method comparisons in the initial revision 16 build.

The first revision 16 image (`06c038d`) passed all seven early-exit setup stages
but stopped the normal test before allocating packets: the diagnostic expected
`kIOReturnNotOpen`, whereas the audited disabled RX path returns
`kIOReturnOffline` (`0xe00002d7`). Cleanup completed with all owned counts zero,
Wi-Fi remained reachable, and no new panic report appeared. The correction
uses the native status and preserves unexpected error codes in the report.
That initial failed run is not included in the following passing totals.

Revision 16b passed eight malformed-request checks and **16 queue setup
attempts**: five normal attempts, each reusing its pair through two
initialize/finalize cycles, and 11 early exits across all seven setup stages.
The totals were 16 pools, 15 TX queues, 14 RX queues, 25 balanced event-source
adds/removals, 29 balanced initialization/finalization calls, and 29 released
queue allocation references. All owned packet, pool and queue instance counts
settled to zero after each attempt.

The normal attempts allocated and returned 80 packet leases and compared
63,120 bytes after the disabled RX queue rejected them. All 66 queue metadata
checks and 126 request/enqueue rejection checks passed. No queue callback ran,
and no cleanup error was reported. A separate regression check of selector 22
passed its 24 packet payload/metadata/bounds checks. A non-root user-client open
was denied after a successful root control.

All 15 interface-bound WPA2 router pings passed around the queue tests. No new
panic report appeared. Health checks passed with the existing 1920×1080 at
240 Hz configuration, matching GPU/display hashes, zero GPU I/O failures and
zero flip-latch timeouts. This records configuration/counters, not a new visual
desktop confirmation. The native manager boot latch remained unused, and
native interface/peer counts stayed zero.

These were **disabled, unregistered queues with no channel/ring**. Successful
rejection means the caller retained packet ownership; no packet was delivered
to BSD, a radio, or a peer. This does not qualify enabled queue callbacks,
registration error recovery, sustained operation, channel coexistence, native
discovery or AirDrop transfers.

## Retained native queue registration — 2026-10-10

Source `bfb7ee5293b2b09673ed9ee9e8586a63cc3f040b` (revision 17) was built
from a clean checkout and loaded on the same AX201 / Sequoia 15.8.1 (24H32):

- Binary SHA-256: `7402a4041c4e3ce87e58d8120168d9cb9166e185dac8bfa8c00bef4bc92f76af`
- Loaded UUID: `E43F8D11-792B-3074-847D-8A0DF86C6436`
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38040153513)
  passed all three jobs: Linux ASan/UBSan, Sequoia Wi-Fi, and Sequoia AWDL
  (79 sources), including the ABI fixture and six diagnostic clients.
- Local UBSan passed. The actual native interface matched 560 virtual slots
  with its new BSD-prefix override accounted for; actual queue subclasses
  matched 189 slots. The compile-only interface fixture also matched 560 slots.

One native interface/peer manager was retained throughout six setup-stage
exits and three complete prepare/clear cycles. An additional final preparation
was retired with the interface. This made **10 preparations and five successful
native registrations**. Four registrations were explicitly deregistered; the
last was cleaned by the interface's stop path. Every cleanup left owned pools,
packets and queues at zero. Final retirement also left the native interface and
peer-manager counts at zero.

While retained, each registration had one pool, one TX queue, one RX queue,
5,824 native packet objects, two matching registration copies, and both queue
associations pointing to the same interface. Three repeated prepare calls and
three repeated clear calls were idempotent. Independent client connections,
including repeated session-create calls, kept the same interface identity.
The retained state also survived a two-second gap in each normal cycle.
No queue callback ran and no cleanup error was reported.

Nine malformed requests were rejected. Prepare without a session, and prepare
after retirement, returned `kIOReturnNotReady`. Attempting to recreate the
native peer after retirement returned `kIOReturnNotPermitted`. A non-root
user-client open was denied. After retirement, the earlier disabled-queue
regression passed one attempt: 16 packet leases returned with all 12,624
payload bytes preserved, showing its diagnostic exclusion had been released.

WPA2 connectivity checks passed **21/21 interface-bound router pings** during
the test. No new panic report appeared. Health checks passed at 1920×1080,
240 Hz with unchanged GPU/display hashes and zero GPU I/O failures or flip-latch
timeouts. These are configuration/counter checks, not a new visual confirmation.
Temporary sleep prevention remained active. The immediately previous revision
16b driver is preserved in the verified rollback directory.

This qualification covers **unpublished, disabled registration**. No BSD
interface, real ring, queue callback, radio packet or native AirDrop session
was activated. Setup exits are not native allocator-failure injections. Native
low-memory paths, reporting-service lock ordering, live BSD shutdown, enabled
transport, peer scheduling/discovery and bidirectional file transfers remain
unqualified. The boot latch is consumed and the test object is retired; a
new native peer requires reboot, while normal queue reuse did not.

## Native BSD attachment — 2026-10-10

Source `dc61c3abf899e298c3c5d43d2e10e20c71d8d275` (revision 18), AX201 /
Sequoia 15.8.1 (24H32):

- Binary SHA-256: `81ca9827e832ff907e37d370a39172e5d4e0e0b2653efbdb589437cb46f423f9`
- Loaded UUID: `C7A00E11-25BF-3FCA-ADDD-A3C299F62A6C`; loaded identity and
  external EFI hash matched after reboot. The revision 17 rollback was verified.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38041420277)
  passed Linux ASan/UBSan and both Sequoia variants (AWDL: 79 sources), the
  native fixture and seven diagnostic clients. Local UBSan passed. The actual
  interface matched 560 native virtual slots/63 backend methods, and the two
  queue subclasses matched 189 slots with lifecycle wrappers accounted for.

One retained interface was explicitly published. Native matching attached one
`IOSkywalkNetworkBSDClient` and prepared a real `awdl0` ifnet. Both TX and RX
queues received native kernel queue contexts and initialized once, with zero
reported preparation/initialization errors. Three subsequent publication
requests kept the same interface identity and did not publish or prepare again.
Nothing publishes the diagnostic interface automatically at boot.

The first harness stopped on an incorrect expectation that `ifconfig awdl0 up`
would return an error. It returned zero even though the driver recorded one
rejected enable and both queues remained disabled. Local inspection confirmed
that the native BSD client clears `IFF_RUNNING` on this failure, then returns
the provider's flags-command result; administrative `IFF_UP` is independent.
This was a harness expectation failure, not a passing operational-enable test.

A continuation on the **same retained object**, without changing the driver,
tested three more down/up cycles. All four enable attempts reached the
`kIOReturnNotReady` rejection; `IFF_RUNNING` stayed clear, queues stayed disabled,
and no queue callback ran. Native queue initialization counts stayed one each.
The final administrative state was down. Manual transport clear and session
retirement were rejected while BSD remained attached, preserving its resources.
Live BSD unpublication, queue finalization and provider removal were not tested.

Seven malformed requests were rejected, and publication without a session or
without registration returned `kIOReturnNotReady`. Before publication, the older
disabled-queue regression passed 16 packet leases and 12,624 byte comparisons,
with all owned objects returning to zero. Ordinary WPA2 router pings passed
**12/12** around publication and the continued checks. No new panic report
appeared. Display/GPU health checks passed at the existing 1920×1080, 240 Hz,
with unchanged hashes and zero GPU I/O failures/flip-latch timeouts; these are
configuration/counter checks, not a new visual confirmation.

The native configuration selected MTU 1484 while the prototype registration
requested 1428; transmit limits/offload behavior require qualification before
delivery. Native logs also recorded an existing Ethernet BPF attachment and an
event-monitor entitlement denial. These are retained as unresolved integration
observations, not suppressed or counted as working BPF/event delivery.

This establishes BSD attachment and real queue initialization only. There was
**no enabled radio, packet delivery, peer discovery or AirDrop transfer**. The
interface/peer/pool remain retained for native ownership; their live teardown,
firmware scheduling, action/data transport and full transfer matrix are still
unfinished.

## Native RX context and owned sync templates — 2026-10-10

Source `4338b2f0113652dea8039bc2982998138fe198ae` (revision 20) includes the
native management-buffer boundary, typed RX metadata, TSF-union validity and
owned supplementary synchronization templates:

- Binary SHA-256: `70fc244f7f96180c35ced504b0120a016474feccccabe481d1e94ed116e7a26c`
- Loaded UUID: `CA20EF3C-4FF3-3A80-830B-5D5F38B2861B`; external EFI installation
  and the revision 18 rollback hashes were verified before an orderly reboot.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38044151426)
  passed all three jobs. Local UBSan passed. The actual interface matched 560
  native slots/63 backend methods; management-buffer fixtures matched 71 slots.

Eight malformed initialization requests were rejected. One base-only instance
and three full instances were initialized and released. The full instances
passed **384 backend contract checks** with zero failures and zero remaining
objects/provider opens. The checks cover the disabled sync getter, owned
template bytes after producer mutation, malformed-update preservation, and
unchanged rejection by the remaining unsupported methods. No native peer
manager was created in these tests.

An initial passive-listen request correctly returned busy while ordinary Wi-Fi
was associated. With Wi-Fi temporarily powered off, a five-second channel-6
capture received **39 frames from the selected owned peer**. All 39 carried
valid GP2 and TSF flags and parsed valid schedules. The longest coherent run
was 25 samples, with no phase outliers; hardware-clock phase error had median
absolute 180 us and p95 absolute 840 us. These are passive observations, not
native synchronization or scheduled transmission. The overloaded-TSF branch
was tested offline; no live frame in this capture exercised it.

The restoration harness re-enabled Wi-Fi in its `finally` block, then stopped
on its first six-second ping timeout. A later independent interface-bound
check passed 3/3. Together with the before/after contract checks, WPA2 passed
9/9 completed router pings. The initial timeout is not counted as a pass.
GPU/display health checks passed at the existing 1920×1080, 240 Hz, with
unchanged hashes and zero GPU I/O failures or flip-latch timeouts. This records
configuration/counters, not a new visual desktop confirmation.

An offline replay of 677 earlier owned-peer frames passed the typed metadata
encoder and frame boundary. Neither that replay nor this live capture invoked
native management-buffer handoff. Native RX clock mapping, sync-event delivery,
enabled radio scheduling, peer discovery and bidirectional AirDrop transfer
remain unfinished. Revision 19 was built and CI-tested but was not separately
installed; its changes are included in revision 20.

## Native configuration failure traced — 2026-10-10

Source `f187f37115433e8a30ddd6b00531bfa77c5d9437` (revision 23):

- Binary SHA-256: `61d5b8b52ef891e6d6f0aa90fb29b1259cf9853bc2cb32b6fe2cef3566002689`
- Loaded UUID: `0214F11B-A519-3643-B1CB-832CB7ACE023`, verified after reboot.
  Revision 20 was backed up before the external EFI transaction.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38046330026)
  passed all three jobs. Local UBSan passed; 1,289 native imports resolved and
  the concrete interface matched 560 virtual slots / 63 backend methods.

The init-only regression passed 384 contract checks with zero failures or
remaining instances. It now also exercises the owned template's bounded action
encoder, missing-clock rejection and the trace's order/capacity accounting.
Revision 21/22 event and encoding changes are included here; those revisions
were not separately installed. The event payload still has not been dispatched
to the native timeline.

Seven malformed configuration requests were rejected. Inspection without an
object reported absence, and a run without a session returned not ready. One
retained, unpublished native object was then created. Native `configure()` made
exactly one backend call: `setAWDL_BSSID`, at slot `0x10a8`, returned
`kIOReturnUnsupported` (`0xe00002c7`). Native configure returned the same error.
**This is a reproduced initialization failure, not successful configuration.**

Two subsequent runs from reopened clients returned the same saved trace and
registry identity. The live backend-call count stayed at one; there was still
one peer manager, no BSD interface, and no enabled interface or queues. The
object remains retained for this boot; it was not torn down or recreated.

WPA2 interface-bound router pings passed 12/12 around the checks. Display/power
health assertions passed at the existing 1920×1080, 240 Hz, with unchanged
GPU/EFI settings and zero recorded GPU I/O failures or flip-latch timeouts.
After reboot the earlier mDNS name did not resolve; the trusted IPv4 SSH
address worked. This was not a machine hang or evidence of AWDL discovery.

There was no RF synchronization, native RX handoff, discovery or file transfer.
Subsequent network-identifier staging code still requires its own hardware
qualification; the revision 23 failure must not be reported as that fix passing.

## Native network identifier accepted — 2026-10-10

Source `eef1c2723f7ac3e4753bddad50d67abc7397848e` (revision 24):

- Binary SHA-256: `64476676726aa8c9b4389c61d639d3174b1c30c91640e08aa61bd199f0160827`
- Loaded UUID: `42671D1C-7E42-30E2-AD65-4CB2A4E1820E`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38046919651)
  passed all three jobs. Local UBSan and the 560-slot / 63-method concrete
  interface comparison passed. The external EFI transaction retained a
  verified revision 23 backup and preserved the existing config.

The 384 init-only checks passed with zero failures or remaining instances,
including version/address validation, owned BSSID bytes and their use in the
encoded synchronization frame. All seven malformed configuration checks and
the missing-session rejection passed again.

The single native configuration call advanced through `setAWDL_BSSID` with a
zero return. It next queried `getPEER_CACHE_MAXIMUM_SIZE`, which returned
unsupported; native code continued using its fallback. The following
`setAWDL_ELECTION_ID` returned unsupported and was the fatal result returned by
configure. The three-call trace was preserved across two reopened-client run
requests without further backend calls or another manager creation.

The interface remained unpublished and disabled, with no enabled queues.
WPA2 interface-bound router pings passed 12/12 and post-test GPU/display/power
health assertions passed. Native configuration is still incomplete. BSSID
acceptance and CPU frame encoding establish neither firmware filter setup nor
any RF transmission, peer discovery or AirDrop transfer. Later election-ID
implementation is not covered by this revision's hardware result.

## Native election identifier accepted — 2026-10-10

Source `d1df2dab40197928bb745f45374fed0794aae528` (revision 25):

- Binary SHA-256: `38b3fec4b2f706a0b711f339051c1a7e13e7d812ef8a34d6b1a1670e29df51f7`
- Loaded UUID: `5F2F7D05-CD4F-36EE-A404-8FC563AD538C`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38047565050)
  passed all three jobs. The concrete 560-slot / 63-method comparison passed.
  Installation retained a verified revision 24 backup and unchanged EFI config.

The 384 init-only checks passed with zero failures or remaining instances.
These include native election-ID normalization to 16 bits, invalid-version
preservation and unchanged-output rejection of unset readback. Seven malformed
configuration requests and the missing-session rejection passed.

One native configuration call accepted the BSSID and election ID. The cache
size query and election-metric setter returned unsupported; native code
continued past both. The following master-channel setter returned unsupported
and ended configuration. Two repeated requests returned the same five-entry
trace and retained registry identity without new backend calls. The single
peer manager remains retained, with no BSD publication or enabled queues.

WPA2 interface-bound router pings passed 12/12. The existing GPU/display/power
health assertions passed after the checks. Election-ID acceptance is disabled
configuration only: no master was elected, no RF synchronization or native
packet handoff occurred, and no discovery or file transfer was tested.
Subsequent master-channel changes require separate hardware qualification.

## Native master channel accepted — 2026-10-10

Source `0e6879aec2751fb5ba7505e0006030fb89e17b37` (revision 26):

- Binary SHA-256: `1cf70fc49a0d60aa9824d6a5be9b4cdd5aba66d873e04f80defddc0e1f527ef4`
- Loaded UUID: `688D1E93-A7B5-3F9C-9B2B-0C29C06BBE9E`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38048569436)
  passed all three jobs; the compiled interface matched 560 slots / 63 methods.
  The EFI transaction verified its revision 25 rollback and unchanged config.

All 384 init-only contracts passed with no failures or remaining instances,
including channel-zero no-op, selected-channel readback, malformed updates and
sync-encoder rejection of a mismatched master channel. Eight malformed init
requests, seven malformed configure requests and the missing-session rejection
passed.

The single retained native configuration selected **channel 6**, confirmed by
the staged registry property. Its six-call trace shows successful BSSID,
election-ID and master-channel setters. Native configuration then stopped at
`setAWDL_ELECTION_RSSI_THRESHOLDS`, which returned unsupported. Cache-size and
election-metric unsupported results remained nonfatal on this path. Two repeated
runs returned the saved trace and identity without further backend calls.

The native interface and queues stayed disabled and unpublished. WPA2 router
pings passed 12/12 and before/after GPU/display/power assertions passed. No
radio retune, RF synchronization, native packet handoff, discovery or transfer
occurred. The subsequent 20-byte RSSI request correction has not been tested
by this revision's result.

## Sequoia election-RSSI request accepted — 2026-10-10

Source `1f412800da96354f65a131ec51ac94493dda55f3` (revision 27):

- Binary SHA-256: `cdc24484508c84d011ebb2e1b29f8f335273297f3b0c3b240a8b95074d2026ea`
- Loaded UUID: `C5100FF8-8FA5-3A86-B39A-FB6CC623AEF2`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38048905362)
  passed all three jobs, including both Sequoia variants. The concrete interface
  matched 560 native slots / 63 backend methods. Installation preserved a
  verified revision 26 rollback and unchanged EFI configuration.

All 384 init-only contracts passed with no failures or remaining instances.
The RSSI checks exercise the corrected 20-byte request, signed-byte endpoints,
upper-bit normalization, unset readback and invalid-update preservation. The
eight malformed init checks, seven malformed configure checks and missing-session
rejection also passed.

The single native configuration accepted election-RSSI thresholds and advanced
to eleven traced backend calls. Minimum rate, synchronization channel sequence,
presence mode and extension parameters returned unsupported; native configure
continued past them. The synchronization-state setter was the next fatal
unsupported call. Repeated requests returned the saved trace without another
backend call or peer-manager creation.

The interface remained unpublished and disabled. WPA2 router pings passed 12/12,
and before/after GPU/display/power assertions passed. No RF synchronization,
native packet delivery, discovery or transfer was tested. Later role-descriptor
staging is not covered by this hardware result.

## Native configure returns zero with staged role and timing — 2026-10-10

Source `74155e3d21d201a6879082f7a54cda43dccf2c2b` (revision 29) includes revision
28's role descriptor and the dependent presence/extension configuration.
Revision 28 passed build/CI checks but was not separately installed.

- Binary SHA-256: `419c4f55e4f299263b5a06e2311c56230c8049a50a58be106c190c961bdfd2db`
- Loaded UUID: `8783A866-BDA6-3190-91A8-A9C402CC68D5`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38049564475)
  passed all three jobs. The concrete interface matched 560 slots / 63 methods.
  Installation verified its revision 27 rollback and unchanged EFI config.

All 384 init-only contracts passed with zero failures or remaining instances.
The role/timing checks include invalid-update preservation, owned addresses,
presence boundaries, extension normalization and unchanged-output rejection
when frame parameters disagree with the staged configuration. Eight malformed
init requests, seven malformed configure requests and missing-session rejection
also passed.

The single native `configure()` call returned **zero** after eleven backend
calls. Staged readback showed master channel 6, role 1, presence 4 and four
extension counters of 3. BSSID, election ID, master channel, RSSI thresholds,
presence, extension parameters and synchronization role were accepted. Cache
size, election metric, minimum rate and channel sequence still returned
unsupported; native configure continued past them. Consequently the diagnostic
continues to report `native_configuration_verified: false`.

Two repeated requests returned the saved trace and retained identity without
additional backend calls. Four subsequent snapshots over 20 seconds retained
one manager, no BSD interface, disabled queues and zero queue callbacks; backend
calls stayed at eleven. WPA2 router pings passed 12/12 and before/after
GPU/display/power assertions passed.

This is disabled configuration and lifecycle evidence only. Native configure's
zero result does not establish radio readiness: no RF synchronization, native
packet delivery, peer discovery or AirDrop transfer occurred. The separate
channel-sequence layout correction is not covered by this installed binary.

## Native basic channel plan accepted — 2026-10-10

Source `58f19bcf772e87ee52c51a449a95a390da37cdcf` (revision 31) includes revision
30's 400-byte channel layout correction and the bounded encoding-0 backend.

- Binary SHA-256: `2a57ff6d8237a5d77ec99733c249095e10206836e99136e0b550fca73244623c`
- Loaded UUID: `2F88D7E8-98D3-32D2-8C04-DA24A0E4B310`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38050684852)
  passed all three jobs, including nine C++ suites under Linux ASan/UBSan and
  both Sequoia builds. Local UBSan and the 560-slot concrete-interface comparison
  also passed. The installation preserved a verified revision 29 rollback.

All 384 initialization contracts passed, including sequence readback, producer
ownership, invalid-update preservation and encoder rejection for a different
or absent plan. Malformed requests and the missing-session rejection passed.
The one retained native configuration accepted the channel sequence and returned
zero after eleven calls. Readback showed 16 slots, with channel 6 in slots 0
and 8 and unavailable slots elsewhere, at a four-AW step. Cache size, election
metric and minimum rate remained unsupported. Repeated configure requests
returned the saved trace and identity without further backend calls.

Four snapshots over 20 seconds retained one unpublished manager with eleven
backend calls and no BSD interface. WPA2 router pings passed 12/12; GPU, display
and power checks passed. This qualifies disabled configuration only: no native
RF synchronization, peer discovery or transfer occurred. The later election
metric/minimum-rate implementation is not covered by this installed result.

## Election metric and minimum rate accepted — 2026-10-10

Source `f9c37d98338cce85987b19056c2fded8ad27270c` (revision 32):

- Binary SHA-256: `688e373628a070c31c8aa52e469d5743b427a2c18a285459804a32113bccf8b6`
- Loaded UUID: `2C202A9B-7214-3969-95C3-F3B3A84B1738`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38051090108)
  passed all three jobs. The native ABI fixture and concrete 560-slot interface
  comparison passed locally. Installation verified a revision 31 rollback.

All 384 initialization contracts passed, including the corrected 12-byte metric
and eight-byte rate structures, full-width metric preservation, supported OFDM
rates, malformed requests, unset reads and rejected-update preservation.
The single native configuration returned zero after eleven calls: all ten
configuration setters succeeded, while the cache-size getter remained
unsupported. Staged values were election metric 65 and minimum rate 12 Mbps.
Two repeated requests returned the saved trace without new backend calls.

Four subsequent samples over 20 seconds retained one unpublished manager with
eleven backend calls. WPA2 router pings passed 12/12, and before/after GPU,
display and power checks passed. Radio synchronization, native packet delivery,
discovery and AirDrop transfers remain untested. In particular, this result
does not cover the later native template-producer diagnostic.

## Native template production and complete encoding — 2026-10-10

Source `c21947f1a95d68138c2bf2986486098d21764bc5` (revision 33):

- Binary SHA-256: `e014038863abed719566a1c01d0fbb36454959f418dfd7199ef322c62d612c7f`
- Loaded UUID: `690B18FC-2A10-3A20-BD3F-FE4654724713`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38051616867)
  passed all three jobs. The concrete native interface matched 560 slots / 63
  backend methods. Installation verified the revision 32 rollback.

All 384 initialization contracts passed. Malformed template requests and calls
before a session or successful configuration were rejected without consuming
the native-template attempt. One retained manager accepted all ten native
configuration setters, with the cache-size getter still unsupported.

The single native `updateSyncFrameTemplate(true)` call returned zero and called
the template setter exactly once. It produced **110 bytes / four TLVs**, with
types 6, 12, 21 and 24. The owned template combined with the staged configuration
encoded into a **210-byte MPDU**, which the independent frame/schedule parsers
accepted. The encoding check used a synthetic clock, never sent to the radio.

Two repeated requests returned identical saved template results with no new
backend calls; the original configuration trace also remained unchanged. Four
samples over 20 seconds retained one manager and twelve total backend calls.
The native interface and queues stayed unpublished and disabled. WPA2 router
pings passed 18/18; before/after GPU, display and power checks passed.

This verifies real native template production and CPU-only encoding. No RF
synchronization, native RX delivery, peer discovery or AirDrop transfer occurred.

## Retained-manager radio ownership and hardware clock — 2026-10-10

Source `b333bb726afb387449e454eefee4eff0444317a7` (revision 34):

- Binary SHA-256: `9507e40ace37e22c434a2ee111413fc7f41be82ea136d4d693ef4c7c036e1ed8`
- Loaded UUID: `B8CB3CAC-979D-33E4-8ECA-9DCC3FA26EE6`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38052744576)
  passed all three jobs. Local UBSan, the native ABI fixture and concrete
  560-slot comparison passed. Installation verified a revision 33 rollback.

All 384 initialization contracts and the native configure/template regression
checks passed. Twelve malformed radio requests were rejected. A radio request
while ordinary STA Wi-Fi was connected returned busy, without disrupting its
router connectivity.

After explicitly turning off STA, two 3000 ms receive-only monitor sessions
ran on the staged master channel 6, retaining the same native interface and
peer manager. Both expired automatically, restored INIT/STA mode and the exact
original interface flags, and left no radio owner. Native backend calls stayed
at twelve; peer-manager creation stayed at one.

The two sessions sampled the hardware clock 134/133 times, with 132/131 coherent
rate samples. All 263 coherent samples encoded and parsed the native template
as a 210-byte MPDU. Each session received **20 management frames from the owned
test peer**. Each filled its 16-frame diagnostic ring and dropped four further
ring records; malformed-frame counters remained zero. Clock/RX-domain checks
passed on 133/130 samples. No raw captures were published.

Ordinary WPA2 Wi-Fi was restored after the sessions. Router pings passed 27/27
across initialization, configuration, template, refusal and restoration checks;
GPU/display/power assertions passed. Controller termination and process-death
cancellation were not hardware tested by this run.

This demonstrates retained-manager radio ownership, clock continuity and
receive-only observation. The encoded specimens use constructed AW timing;
they were never transmitted. Native interface/queues stayed disabled and
unpublished, and received frames were not handed to the native protocol. Native
peer discovery and AirDrop transfer remain unimplemented.

## Native-template synchronization TX timing failure — 2026-10-10

Source `f359cfe430da573dbe47ab26122f4c458426b403` (revision 35):

- Binary SHA-256: `71f9ac564cc422987aa4071a6e6c86159a7041731c0d9b50120b0e26f0f71da0`
- Loaded UUID: `4E18865D-0AFF-360A-916C-DC2F5CC8B43D`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38054286061)
  passed all three jobs. Local UBSan, the native ABI fixture and concrete
  560-slot comparison passed. A revision 34 rollback was verified.

The 384 initialization contracts, native configuration and template checks
passed. Two receive-only sessions, now using the advancing local timeline,
encoded 131/132 coherent clock samples and received 24/20 owned-peer management
records. Both restored the radio and retained the same disabled native manager.

The first bounded synchronization TX trial submitted one 210-byte broadcast
PSF at the staged 12 Mbit/s rate. Firmware returned success (`0x01`), but the
GP2 interval from frame timestamp to completion processing was **25,354 us**.
This exceeded the existing 5,000 us qualification limit. The diagnostic stopped
further transmissions, recorded one completion and zero pending/aborted frames,
then restored the radio. It **failed** TX timing qualification. The completion
interval includes preparation, firmware and host notification delays; it does
not locate the delay or measure the actual on-air timestamp. Peer reception
was not observed.

Wi-Fi restoration was verified before evaluating the failure. WPA2 router pings
passed 36/36 across the regression and TX harnesses; post-test GPU/display/power
checks passed. Manager creation remained one and backend calls remained twelve.
Native interface and queues stayed disabled and unpublished, with no native RX
handoff, verified discovery or AirDrop transfer. The timeout will not be relaxed
merely to turn this failure into a pass; the next step is timing attribution.

## TX preparation versus notification latency — 2026-10-10

Source `29dcee1130c6ee466dee19d7f029dd48c63a7edf` (revision 36):

- Binary SHA-256: `801b90868f866aa4ca7dee31fa90cd71b79f20107915621f840d1b228c1dd314`
- Loaded UUID: `44BDC266-C985-3465-B49D-A0FEFA396CF2`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38055537031)
  passed all three jobs; local UBSan and both 560-slot native ABI comparisons
  passed. A revision 35 rollback was verified.

The 384 initialization contracts, native configure/template and two passive RF
regressions passed. The synchronization TX trial again stopped after its first
successful firmware completion: **25,037 us** from frame GP2 to completion.
No additional frames were submitted, and no pending or aborted TX remained.

The new instrumentation recorded:

| Interval/counter | Observation |
| --- | ---: |
| Frame age at final pre-submit GP2 read | 40 us |
| Final GP2 read to TX doorbell, host clock | 27 us |
| Doorbell to notification-batch interrupt filter | 24,867 us |
| Batch filter to gated RX handler | 56 us |
| RX batch entry to completion measurement | 48 us |
| Final pre-submit read to completion, GP2 / host | 24,997 / 24,998 us |
| Firmware-reported wireless media duration | 168 us |
| Firmware RTS/frame retries and Bluetooth kills | 0 / 0 / 0 |

The two clock domains agree. Preparation and post-interrupt host processing do
not account for most of the measured delay. This does **not** distinguish late
RF transmission, late firmware response generation, DMA publication or late
interrupt notification. The reported media duration is not an absolute on-air
timestamp. No peer reception was observed.

Wi-Fi was restored before the harness reported the qualification failure;
router pings passed 36/36 and retained health assertions passed. The same native
manager remained disabled and unpublished. The next diagnostic inspects only
completed DMA buffers between clock polls to look for an earlier matching
response, without consuming it or changing the 5 ms completion limit.

## TX response visibility before interrupt delivery — 2026-10-10

Source `bfd3d1557e9840cd6b5e9e9e3b4b71685ca70308` (revision 37):

- Binary SHA-256: `b9f68d65b028e34f6f6d4cbc7cc93e2c85a21a1cee0bc82e0b37845c5612c587`
- Loaded UUID: `AB285F3F-953E-397A-8738-6A9E26271482`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38056837467)
  passed all three jobs; local UBSan and both native ABI checks passed. The
  installed candidate retained a verified revision 36 rollback.

All 384 initialization contracts, configuration/template checks and two passive
RF sessions passed. The synchronization TX trial submitted one 210-byte frame
and received one successful firmware completion. Its **30,280 us** GP2 interval
again failed the unchanged 5,000 us qualification limit. No second frame was
submitted and no pending or aborted TX remained.

The passive observer sampled published, closed DMA buffers 23 times. It first
found the matching response **29,511 us after the TX doorbell**, reporting the
same success status and 168 us media duration as the normal completion path.
The interrupt filter ran at 30,154 us; gated processing reached the completion
measurement 85 us later. Final pre-submit GP2 read to doorbell took 8 us.
Observer support was confirmed and no bounded scan was truncated.

This run did not find an early completion sitting in a closed DMA buffer for
most of the delay. It still cannot distinguish late wireless transmission,
firmware response generation or DMA publication. Neither a firmware success
code nor the media duration proves peer reception or actual RF timing.

Wi-Fi was restored before the harness reported the timing failure. WPA2 router
pings passed 36/36, and retained GPU/display/power assertions passed. The same
single native manager remained unpublished and disabled, with no native RX
handoff, discovery or verified AirDrop transfer. Independent peer observation
is the next investigation; the timing guard remains unchanged.

A separate, bounded trial retained that same manager and stopped after one
completion at 37,198 us. An owned-address-filtered capture on a nearby native
Mac's `awdl0` produced no records in either advertised private/raw format.
There was no positive control establishing management-frame visibility, so
this capture cannot show whether the peer received the transmission. That
trial restored Wi-Fi (9/9 router pings) and passed the retained health checks.
No further identical RF trial is justified by those empty captures.

## Listener MAC parameter correction — 2026-10-10

Source `1a1832616409d3ba566e1e47fd0d62ff27883080` (revision 38):

- Binary SHA-256: `36ac90efb2b4269492a59decd91bc2524db52fc26c2c299334bdc387c9276b33`
- Loaded UUID: `E73A10BB-505A-3845-BB2B-BD35276DCEC3`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38058462471)
  passed all three jobs. Local UBSan and both native ABI comparisons passed;
  installation retained a verified revision 37 rollback.

All 384 initialization contracts, native configuration/template and two passive
RF regressions passed. The listener MAC now supplied basic rates and all four
standard OFDM EDCA entries during the native TX lease, instead of leaving them
zero. The next TX trial still failed at **32,095 us** after one successful
firmware completion. The matching DMA response was first observed 31,313 us
after the doorbell; its interrupt filter ran at 31,931 us. Firmware media
duration remained 168 us, with zero retry and Bluetooth-kill counters.

This correction alone did not resolve the observed latency. The trial stopped
without pending or aborted TX, restored WPA2 (36/36 router pings across the
harnesses), and passed retained health assertions. An early boot health check
ran before the first display-composition success; its read-only recheck passed
once that activity started. The post-test health check passed as well.

No native interface, queue, discovery or AirDrop transfer was enabled. The next
controlled change follows Intel's non-STA Smart FIFO policy rather than using
the associated-station policy for this listener. This remains a hypothesis to
test, not a confirmed explanation of actual RF timing.

## Non-STA Smart FIFO policy — 2026-10-10

Source `b1e290663b3e9a27d7e9eab2e2e33e9deed7455f` (revision 39):

- Binary SHA-256: `350c295f4950ea21894707b5cf42065f9baad578adec881d9535af2b94a0295e`
- Loaded UUID: `ECBEDD49-D932-36C5-9EEB-85D2AED02267`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38059045931)
  passed all three jobs. Local UBSan and both native ABI comparisons passed;
  installation retained a verified revision 38 rollback.

All 384 initialization contracts, native configuration/template and two passive
RF regressions passed. Under the corrected non-STA Smart FIFO policy, the next
TX trial still stopped after one successful firmware completion at **27,174 us**.
The matching DMA response was observed 26,795 us after the doorbell, the
interrupt filter at 27,081 us, and completion processing 54 us after that filter.
Firmware reported 168 us media duration, no retries and no Bluetooth kills.

This result remains within the range of earlier failures; it does not establish
a latency improvement. No extra frame was submitted and no pending or aborted
TX remained. WPA2 restoration passed (36/36 router pings), and before/after
health assertions passed. The same native manager remained disabled and
unpublished. No native RX delivery, peer discovery or AirDrop transfer occurred.
Further identical TX trials are deferred while native timing-parameter and
timeline integration is audited. The 5 ms guard has not been relaxed.

## Native synchronization-parameter round trip — 2026-10-10

Source `ecc251b58ba0b6fd1344d856fcc5b18093f169ea` (revision 40):

- Binary SHA-256: `5c29c1495d82bb3cc50dd2c8d2af6c686cc363c51ff9e01a205cac373d405ac1`
- Loaded UUID: `A8613A80-8C33-3AA8-82D2-F792CCFE1FFA`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38060537350)
  passed all three jobs. Twelve local C++ UBSan suites and five Python checks
  passed; both native ABI comparisons checked 560 slots/63 backend methods
  with no mismatch. Installation retained a verified revision 39 rollback.

All 384 initialization contracts passed. After the unchanged eleven-call
configuration, the native timing diagnostic traced exactly three calls: the
first getter returned `Unsupported` and the native fallback supplied
`[0,16,16,16,110,0]`; the setter and second getter succeeded, returning
`[1,16,16,16,110,0]`. These six words are version, AW length, AW period,
extension, frame period and the opaque tail. This verifies both the fallback
and the owned backend readback, rather than relying on the native wrapper's
zero return. Malformed requests and premature template requests were rejected;
repeated timing runs returned the identical saved result with no extra calls.

Template generation then made the fifteenth backend call, producing the same
110-byte/four-TLV template and 210-byte CPU encoding. Four stability samples
retained one unpublished, disabled manager. Two three-second passive RF runs
produced 132 and 133 coherent clock/encoding samples and heard 16 and 18
owned-peer actions. No invalid frame was counted; diagnostic-ring drops were
0 and 2. No native RX handoff or radio transmission occurred.

WPA2 restoration passed (33/33 router pings), as did before/after GPU, display
and power assertions. This initial baseline did not repeat TX: the staged
values equal the previous constants and do not address the failed completion
timing. Native AirDrop discovery and file transfers remain unavailable.

A subsequent trial added an independent, read-only native peer-database query
on the owned receiving Mac. Across 214 samples over approximately 45 seconds,
the receiver's own address was present but the selected transmitter address
was not found; all queries succeeded. Only aggregate results and owned-address
match offsets were retained. Database absence does not establish that no frame
reached the air, and the query does not measure RF transmission time.

That trial submitted one frame and received one successful firmware completion
at **36,093 us**, failing the unchanged 5 ms bound. The matching closed DMA
response was first seen at 35,478 us after the doorbell. No frame remained
pending or aborted. The same disabled manager survived, WPA2 passed another
9/9 router pings, and GPU/display/power assertions passed. The next diagnostic
will qualify the receiving peer's advertised channel window as well as the
local window before permitting another bounded transmission.

## Peer channel-window qualification — 2026-10-10

Source `7399ac5f3b9681f34c2d8b95d3d5b7289513378b` (revision 41):

- Binary SHA-256: `638c45f355872ea22c93c309ce98638254ac047c01813e792642f7d61876ba78`.
- Loaded UUID: `CC7906FD-2CF0-3A74-A7B2-344A816EAF34`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38062777019)
  passed all three jobs. Twelve local C++ UBSan suites and five Python checks
  passed, including peer-window boundaries, stale clocks, schedule confidence,
  channel mismatch and wraparound. Both native ABI comparisons passed all
  560 slots/63 backend methods. Revision 40 is retained as the verified rollback.

The loaded build passed 384 initialization contracts, native configuration and
timing roundtrip, template generation and four stability samples. Two passive
three-second RF sessions heard 21 and 16 owned-peer actions and produced 131
and 132 coherent clock/encoding samples. The diagnostic ring dropped 5 and 0
records respectively, with no invalid frame; schedule tracking precedes that
ring and continues when the diagnostic record buffer is full.

| Peer-window outcome | First session | Second session |
| --- | ---: | ---: |
| Total sampled clocks | 133 | 134 |
| Clock not yet qualified | 12 | 7 |
| Schedule unavailable | 10 | 34 |
| Advertised channel differs | 105 | 85 |
| Too close to a window edge | 1 | 3 |
| Peer window ready on channel 6 | 5 | 5 |
| Both local and peer windows overlap | **0** | **0** |

The peer did advertise usable channel-6 windows during these sessions, but none
overlapped the independently started local qualification timeline. No TX trial
was run. This explains a missing availability condition in earlier trials;
it does not by itself explain the firmware completion delay or prove an RF
drop. The next step is controlled phase alignment using the owned peer's
coherent RX hardware clock, with timing/profile checks and the existing TX
limits intact. One unpublished manager remained, WPA2 passed 33/33 router
pings, and before/after GPU/display/power assertions passed. Native AirDrop
remains unavailable.

## Peer-derived qualification epoch — 2026-10-10

Source `15c433184dd260efef83b715fba5860e7dae8780` (revision 42):

- Binary SHA-256: `b02875b848b98b049bb674ef706eeb1229335bd618e40f837f02c7734ef2131b`.
- Loaded UUID: `542709CA-3C80-3229-B923-DCCDE47DC105`.
- [Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38063497386)
  passed all three jobs. Twelve local C++ UBSan suites and five Python checks
  passed; both 560-slot/63-method native ABI comparisons passed. Tests include
  same-duration/different-profile rejection, stale RX, master changes and
  counter/clock wrap. The verified rollback is revision 41.

All 384 initialization contracts and the configuration/timing/template baseline
passed. The two passive sessions produced 117 and 106 aligned samples and
**6 and 4 shared channel-6 windows**, compared with zero shared windows in both
revision 41 sessions. These are RX-clock-derived predictions, not peer receipts.
They heard 25 and 21 owned actions, with 9 and 5 diagnostic-ring drops and no
invalid frames. The receive-only runs restored their radio state.

The subsequent broadcast trial submitted one frame during a shared window.
At the final pre-submit clock read, both local and peer windows had 43,950 us
remaining, with 21,586 us elapsed in the peer window. Host preparation took
4 us from the final read to the doorbell. Nevertheless, completion took
**42,125 us from the encoded frame's GP2 timestamp**, again exceeding 5 ms.
The first matching closed DMA reply appeared 41,045 us after the doorbell;
the interrupt filter ran at 42,005 us. Firmware reported success, 168 us media
duration, no retransmissions and no Bluetooth kills. This fixes window phase
alignment but does not qualify TX timestamp accuracy.

A read-only receiving-Mac database observer completed 211 samples over about
45 seconds without errors. Its own address was present in every sample; the
transmitter address remained absent. This does not prove no RF reception.

The original SSH PTY disconnected after the driver returned its stopped-radio
result, leaving the harness's final restoration report incomplete. A fresh SSH
connection verified the same boot and manager, Wi-Fi already on, zero pending
or aborted TX, and successful WPA2/GPU/display/power checks. The original
harness was no longer running. Total verified WPA2 pings were 42/42, including
the separate recovery check. Future RF harnesses should run detached with saved
progress so a control-session loss cannot interrupt post-test verification.
Native AirDrop remains unavailable; no identical broadcast retry was made.

## Explicit management header length — 2026-10-10

Source `054fe20c5597e62c9a22ceefe626fe74caa443df` (revision 43) supplies
the MAC header length in the bounded native TX command, as the Intel gen3
reference path does even without checksum offload. Binary SHA-256:
`a086dc04a976a7af578a296ab9716017164cac817ca479c81c7df20b36caa377`;
loaded UUID `5E361D7C-D5A2-3D31-91C8-13C7323066FB`.
[Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38064881870)
passed all three jobs. Twelve local C++ UBSan suites, five Python checks and
both 560-slot/63-method ABI comparisons passed. The rollback is revision 42.

All 384 initialization contracts and configuration/timing/template checks
passed. Passive sessions heard 22 and 17 owned actions and found 7 and 4 shared
channel-6 windows. Diagnostic-ring drops were 6 and 1, with no invalid frames.
One broadcast was then submitted: command metadata confirmed a 24-byte header
and `offload_assist == 0x0c00`. Completion still took **40,795 us**, exceeding
the unchanged 5 ms guard. The matching DMA reply appeared 40,315 us after the
doorbell; the interrupt filter ran at 40,697 us. Firmware reported success,
168 us media duration and no retransmissions or Bluetooth kills. Both windows
had only 23,949 us remaining at the last clock read. This corrects a command
format discrepancy but does not solve or locate the delayed completion.

The receiving Mac's read-only observer found no transmitter entry in 211
samples over 44.9 seconds; its own address was present throughout. This is not
proof of absent RF reception. The detached harness completed restoration and
recorded its failed timing assertion afterward. WPA2 passed 42/42 router pings;
GPU/display/power checks passed, with the same retained manager. No interface,
queue or synchronization enablement was attempted. Native AirDrop is unavailable.

## Owned-peer unicast MIF — 2026-10-11

Source `5640275c4ede14bbb9ea9de0f9d714b0f03fc3fc` (revision 44), binary
`c048ad80edcd8dff8802f9c91095c6981666f2ecec0226460a1ecc7c8278a093`, loaded
UUID `98DF7DC3-673E-3086-A5FE-CB3EE6E0BCB3`.
[Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38065515587)
passed all three jobs. Twelve local C++ UBSan suites, five Python checks and
both 560-slot/63-method ABI comparisons passed. Rollback is revision 43.

The first boot health query ran before display enumeration was ready and
stopped before any native initialization. A later read found one 240 Hz display
and no GPU errors. The next health query passed, followed by 384 initialization
contracts and configuration/timing/template checks. Each passive session heard
17 owned actions, had one diagnostic-ring drop and no invalid frames, and found
four shared channel-6 windows.

Both old and new malformed-request checks passed; unicast refused an active
infrastructure connection. The subsequent unicast MIF submitted and completed
one 210-byte frame with firmware status **1 (success)**, zero retries and no
Bluetooth kills. Media duration was 228 us, compared with 168 us in the prior
broadcast; this is consistent with an acknowledged unicast, not proof of native
AWDL acceptance. Completion still took **33,766 us**, failing the unchanged 5 ms
guard. The matching DMA reply appeared at 32,936 us after the doorbell. At the
final clock check the peer window had 48,773 us remaining and 16,763 us elapsed.
No actual TX or ACK on-air timestamp was measured by this revision.

The receiving Mac's observer found no transmitter entry in 211 samples over
44.9 seconds, with its own address present throughout. The detached harness
restored Wi-Fi and preserved the same manager before recording the failed timing
assertion. WPA2 passed 42/42 router pings and GPU/display/power checks passed.
Native AirDrop is unavailable; firmware unicast success alone is not completion.

## Addressed ACK timing observation — 2026-10-11

Source `fc7735bc70a728116a6c66cd9df5fc6885703e3f` (revision 45), binary
`c3ebda8bd44163cb161526b185184be7c068575b7816a70b172fafaa6ffec319`, loaded
UUID `048AA462-FDDA-37B2-88A5-8D063BC65425`.
[Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38066207075)
passed all three jobs. Twelve local C++ UBSan suites, five Python checks and
both 560-slot/63-method ABI comparisons passed. Rollback is revision 44.

The change only observes CRC-valid monitor ACKs addressed to the native sender
during the bounded unicast lease; transmission and interrupt ownership are
unchanged. ACKs have no transmitter address or sequence number, so their timing
is correlated with the single outstanding transmission rather than treated as
independent proof of peer identity. Unit checks cover frame bounds, receiver
isolation, stale/future timestamps and GP2 wraparound.

All 384 initialization contracts and configuration/timing/template checks
passed. Two passive sessions heard 17 and 19 owned actions and each found four
shared channel-6 windows. Ring drops were 1 and 3; invalid frames were zero.
The unicast trial submitted and completed one frame with firmware status 1,
zero retries/BT kills and 228 us media duration. One matching ACK had hardware
GP2 **22,869 us after the encoded frame timestamp**; completion was at
**24,090 us**, another 1,221 us later. The final clock read to the doorbell took
6 us. For this sample, most of the delay preceded the observed ACK rather than
being solely a host notification delay. It still fails the unchanged 5 ms
qualification. Firmware scheduling versus medium contention remains unresolved.

The receiving Mac's observer found no transmitter entry in 212 samples over
45 seconds, with its own address present throughout. Its discoverability
preference was read as Everyone without changing it. The detached harness
restored WPA2 (42/42 pings), retained the same manager, and passed GPU/display/
power checks before recording the failed timing assertion. Boot continuity was
also checked using the boot-session UUID because wall-clock adjustment changes
the reported boot time. No native interface, queues or RX handoff were enabled.
Native AirDrop remains unavailable.

## Pending radio readiness and wire profile — 2026-10-11

Source `8c62e2c84297b1b5319b63a7333ef6522d9d8042` (revision 46), binary
`8f43282e319cfc5d2b0a055ffcf8b9030d7685d466ad3981a4cb7d6551dda8c5`, loaded
UUID `7A903631-15DB-3E67-996F-BB5E42F84C6A`.
[Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38067611066)
passed all three jobs. Twelve local C++ UBSan suites, five Python checks,
both 560-slot/63-method ABI comparisons, 384 runtime initialization contracts
and the configuration/timing/template baseline passed. Rollback is revision 45.

The bounded unicast again completed successfully in firmware, without retries,
but took **24,292 us** from the encoded timestamp. Its addressed ACK was at
**23,055 us**, followed by completion 1,237 us later. All **18** pending MAC
status samples had clock-ready set, with no going-to-sleep or invalid samples.
This sample does not support the hypothesis that the device remained asleep
while waiting. Before matching ACK delivery, the observer counted five decoded
frames (one management, four control; 695 bytes), with receive offsets from
12,636 to 20,083 us and a maximum Duration reservation of **8,000 us**.
Decoded frames are not a CCA busy-time measurement, and this does not establish
the complete cause of the delay. The 5 ms qualification still failed.

The owned peer's current profile used flags `0x1800`, alignment zero, 16-TU
period/common/extension, presence four, and a separate channel/opclass plan.
Its advertised plan had channel 149/opclass 128 at slots 0, 2 and 10,
channel 6/opclass 81 at slot 8, and unavailable slots elsewhere. This is a
passive observation, not permission to transmit on channel 149.

The detached harness restored WPA2 (42/42 pings), preserved the retained
manager, and passed GPU/display/power checks before reporting failed timing.
The receiving Mac still had no transmitter database entry. No operational
native interface, RX handoff or file transfer was enabled.

## Observed reservation deferral — 2026-10-11

Source `cb245ac5726ecb44bf383955991bdf273d7b6590` (revision 47), binary
`20b28020f5e973f479d27ac4454c32f189255475b62cce6aed0c3268e451fe12`, loaded
UUID `459EE747-E596-3C15-A769-A6A55CB79CB5`.
[Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38068343655)
passed all three jobs. Twelve local C++ UBSan suites, five Python checks,
both 560-slot/63-method ABI comparisons, 384 runtime initialization contracts
and the configuration/timing/template baseline passed. Rollback is revision 46.

The bounded unicast deferred one otherwise eligible sample because a decoded
reservation still had **4,855 us** remaining. A later attempt completed in
firmware without retries, but its addressed ACK arrived **22,486 us** after the
encoded timestamp and completion at **23,658 us**. All 20 MAC-status samples
were awake. Two decoded frames before ACK delivery (640 bytes) had a maximum
Duration value of 276 us. The software deferral was exercised, but it did not
resolve the 5 ms timing failure. It does not replace firmware carrier sensing
or establish how long the RF medium was busy.

The receiving Mac had no transmitter entry in 211 samples. The detached
harness restored WPA2 (42/42 pings), retained the same manager, and passed
GPU/display/power checks. An SSH session expired during the trial; a fresh
connection succeeded and the boot-session UUID was unchanged. No native
interface, queues or RX handoff were enabled. Native AirDrop remains unavailable.

## Separate listener MAC context — 2026-10-11

Source `5cdf66ec464ebacb2e501e7c71935c6e9efc029d` (revision 48), binary
`1d45e5adc199ace6e219676d5a04fee6118e50bce180163e3d92db8ee4653e92`, loaded
UUID `CD0B21F3-2A97-3369-A976-070A6968BD85`.
[Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38069197240)
passed all three jobs. Local UBSan/Python checks, both native ABI comparisons,
384 runtime initialization contracts and the full baseline passed. Rollback is
revision 47.

The native TX lease selected MAC1, following the non-MLO allocation rule in
[Intel's upstream allocator](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/intel/iwlwifi/mvm/mac-ctxt.c#L220).
All 1,166 in-lease identity/active-flag samples matched, and stop restored MAC0.
The two passive sessions remained on MAC0 and restored normally. This checks
host configuration and successful command flow, not a firmware context dump.

The unicast still completed in **24,178 us**, with its addressed ACK at
**22,962 us**. Firmware reported success, 228 us media duration and no retries
or Bluetooth kills. All 18 pending MAC-status samples were awake. Three decoded
management frames before ACK delivery had zero Duration values. No software
NAV deferral was needed. Changing the MAC identity did not resolve the timing
failure; no second frame was sent.

WPA2 passed 42/42 pings, the retained manager and boot-session UUID were
unchanged, and GPU/display/power checks passed. The receiving Mac still had no
transmitter entry in 211 observations. Native AirDrop remains unavailable.

## Pending GP2 cadence comparison — 2026-10-11

Source `c453fe169e6fda90f934caef36de5142c2e63e10` (revision 49), binary
`59a7698647762937dcea64041a5faafc6a02f63012095b4fca64e0dc2cd61d76`, loaded
UUID `8BBAB0C8-876E-3323-B608-C54208015569`.
[Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38069914896)
passed all three jobs. Local checks, both native ABI comparisons, 384 runtime
contracts and the full baseline passed. Rollback is revision 48.

The pending-frame GP2 interval changed from 20 to 2 ms. Eleven reads occurred;
the first started 2,805 us after the doorbell, the last at 29,346 us, and the
longest read took 56 us. The addressed ACK nevertheless arrived **30,042 us**
after the encoded timestamp and completion at **31,267 us**. The experiment
did not support the hypothesis that transmission was simply waiting for the
next 20 ms GP2 access. A single comparison cannot attribute the larger delay
to the polling change. All 23 MAC-status samples were awake; firmware reported
success with no retries and 228 us media duration.

MAC1/active-flag checks passed throughout, stop restored MAC0, WPA2 passed
42/42 pings and GPU/display/power checks passed. The receiving Mac found no
transmitter entry in 211 observations. No native RX handoff or AirDrop transfer
was enabled. The next change restores 20 ms sampling and investigates a
separate firmware P2P-device radio lifecycle before enabling any TX there.

## P2P-device receive lifecycle — 2026-10-11

Source `48cb83d11ef22ecad79e2a776244b6c63088e28b` (revision 50), binary
`5c438440a7be8af77822395a41942b3fabbf16a1051c08e62e2f42a3a359e24b`, loaded
UUID `C2D15912-1529-3407-8E4C-BAB3462C3DFB`.
[Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38071269832)
passed all three jobs. Thirteen local C++ suites with UBSan, five Python tests,
both 560-slot/63-method ABI comparisons, 384 runtime contracts and the full
baseline passed. Rollback is revision 49.

Selector 32 rejected all 12 malformed requests and refused the active STA.
Two separate 3-second P2P-device leases used MAC1, firmware command version 1
and notification version 2. Each received one successful START notification
and **19 owned-peer AWDL frames**, with zero invalid frames and three ring
overflow drops. The requested protection durations were 2744/2747 TU after
radio startup. The second lease observed a normal END notification; the first
ended through the independent lease timer and full firmware teardown. Neither
left a requested/active protection or reported a failed/malformed notification.
No host TX was submitted; autonomous firmware ACKs were not measured.

MAC identity and active-context checks passed for 135/134 samples, and both
leases restored MAC0. The retained native manager and disabled/unpublished
interface were unchanged. WPA2 passed 42/42 pings across the baseline, listener
regression and P2P tests; GPU/display/power checks passed in the same boot.
This establishes a bounded P2P receive lifecycle on this firmware, not native
peer discovery or AirDrop. P2P transmission timing remains untested; native
SYNC, RX handoff and file transfer remain disabled.

## P2P-device unicast timing — 2026-10-11

Source `7e24228d6c6bde6e1c4ef92a311fa832c0463459` (revision 51), binary
`c1f3b855ffeec342870cd819a31d90dc45a1dbfbe864b8e3d9ef63bf364aa8e6`, loaded
UUID `F4824E5F-1322-37EB-A0FF-C520313743FD`.
[Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38071790203)
passed all three jobs. Local 13-suite UBSan/five-Python checks, both ABI
comparisons, the 384-contract baseline, listener regression and two P2P RX
leases passed. The new selector rejected 12 malformed requests and an active
STA; the old unicast/broadcast selector rejection checks also passed.

The P2P unicast trial received a successful session START and submitted one
frame. Firmware reported success, 12 Mbps OFDM and 396 us media duration,
with zero RTS/frame failures or Bluetooth kills. Completion was **28,900 us**
after the encoded clock value (28,828 us after the doorbell), so the unchanged
5 ms guard stopped the trial. The first matching RX batch reached the interrupt
filter at 28,744 us; all 20 sampled MAC states were awake. There was no decoded
ACK observation in this P2P trial, so it does not provide the listener trials'
independent ACK timestamp. Four management frames were delivered before stop;
their largest decoded NAV was zero, which does not measure channel energy.

P2P session state and MAC identity restored, the native manager was retained,
WPA2 passed 51/51 pings and GPU/display/power checks passed. One SSH control
connection timed out, but a fresh connection confirmed the same boot and
completed detached harness. The receiving Mac reported no transmitter entry
in 211 read-only observations. P2P-device selection alone has not resolved
the timing failure or established native discovery. Rollback is revision 50;
native synchronization, RX handoff and AirDrop transfers remain disabled.

## Management-TX protection comparison — 2026-10-11

Source `74b5790b76430ff152f0053722549aacc1e7d09f` (revision 52), binary
`b636798c7e72b1769b48357e9913b1f9474ef4d46c19d329405c3817d0b33886`, loaded
UUID `1379AFB7-9454-371E-ABCC-88638932F7D8`.
[Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38072566847)
passed all three jobs. Local 13-suite UBSan/five-Python tests, both native ABI
comparisons, the 384-contract baseline and listener/P2P receive regressions
passed. Both discovery-profile receive leases captured 20 owned-peer AWDL
frames, with zero invalid frames and four ring overflow drops each.

The unicast selector requested configuration 3, matching Intel's management-TX
ROC mapping, and received the corresponding successful START. It submitted one
frame, which firmware completed successfully at **22,663 us** after the encoded
clock value. Doorbell-to-completion was 22,594 us; the first matching RX batch
reached the interrupt filter at 22,548 us. Firmware reported 12 Mbps OFDM,
228 us media duration, no retries and no Bluetooth kills. All 17 sampled MAC
states were awake, but no independent ACK timestamp was observed. The unchanged
5 ms guard stopped the test. This one comparison does not establish that the
smaller delay than revision 51 was caused by the profile change.

The same boot and retained manager survived; P2P state and MAC identity restored,
WPA2 passed 51/51 pings and GPU/display/power checks passed. The receiving Mac
found no transmitter entry in 211 observations. Rollback is revision 51.
Native AirDrop remains unavailable. All recent timing trials stopped after
the first over-limit frame following radio startup, so they do not yet
distinguish first-frame initialization cost from persistent per-frame delay.

## Bounded P2P series observation — 2026-10-11

Source `0b724e74d729a4526ca46eee43cf14f1e33fc36d` (revision 53), binary
`3cc098d42239fb4635d6bc1de6324ab9f3f68a928a1ca05c4070251dd4ad165c`, loaded
UUID `9D0CB499-EA83-31B6-A0EE-D9238AE7B55E`.
[Exact-source CI](https://github.com/Hibari233/ax201-macos/actions/runs/38073384205)
passed all three jobs. Local 13-suite UBSan/five-Python checks, both ABI
comparisons, the 384-contract baseline and listener/P2P RX regressions passed.
Rollback is revision 52. Selector 34 permits at most four frames with a 100 ms
observation ceiling; the original 5 ms qualification and all older selectors
remain unchanged. An over-limit observation still returns a failing result.

Four unicast frames completed successfully, without retries, at 12 Mbps OFDM.
Their encoded-clock-to-completion delays were **37,978 / 801 / 1,466 / 1,523 us**.
Each had 228 us firmware media time. This supports an initial-transmission
cost rather than a persistent 20–40 ms delay for every frame. It does not
establish the cause of that startup cost, or measure RF timestamps directly.
The receiving Mac still found no transmitter entry in 211 observations,
although its own entry was present in every sample.

The series did **not** pass: the radio stopped with `EIO` before normal lease
expiry, after all four completions. No pending frame was aborted. P2P state
and MAC identity restored; WPA2 passed 51/51 pings and GPU/display/power checks
passed in the same boot with the same native manager. The buffered kernel
log was no longer available when collected, so the reset trigger is unknown.

A separately archived repeat in the same boot stopped earlier on the 100 ms
pending-frame timeout, after one submission and no completion. That frame
had 24,231 us left in the selected peer window; all 84 sampled MAC states were
awake. This is consistent with, but does not prove, startup delay exhausting
the peer's receive window. The immediate kernel snapshot had no fatal error
or device-timeout message. WPA2 passed all nine repeat-harness pings, the
manager was retained and health checks passed. The original end-of-lease
reset was not reproduced. Native discovery, RX handoff and AirDrop file
transfer remain unavailable; reset-cause retention is needed before further
radio-lifecycle changes can be evaluated reliably.

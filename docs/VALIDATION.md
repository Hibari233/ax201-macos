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

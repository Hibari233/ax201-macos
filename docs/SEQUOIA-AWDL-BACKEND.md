# Sequoia AWDL backend audit

Scope: x86_64 macOS 15.8.1, build 24H32. Observations below come from local
read-only inspection of this build's IO80211Family and AppleBCMWLANCoreMac.
No Apple binaries or disassembly are distributed here. Addresses identify the
observed unslid symbols; they are not runtime patch targets. None of these
observations establish a functioning native interface.

## Lifecycle

`IO80211VirtualInterface::createPeerManager()` at `0xffffff80022a4926`
selects `IO80211AWDLPeerManager` for role **6**. Role 7 selects the generic peer
manager. `IO80211Controller::createVirtualInterface()` at
`0xffffff8002227b64` only implements role 7. The current Sequoia driver does
not override that factory; legacy non-WCL virtual-interface code is not a
working Sequoia backend. Reusing legacy role values without checking this
build would select the wrong native object.

`IO80211SkywalkInterface::start()` sets and retains the controller/work queue,
then calls `createPeerManager()`. The role-6 branch creates Apple's AWDL peer
manager and sets the controller's AWDL interface pointer. This is separate from
queue registration and from implementing the hardware backend. The controller's
nonvirtual `getAwdlInterface()` / `setAwdlInterface()` accessors were independently
checked; they do not add vtable slots.

Partial initialization needs different cleanup from a started interface:

- `VirtualInterface::init()` registers power interest, opens the controller,
  and allocates BPF resources. Its `free()` alone does not balance those steps.
- `SkywalkInterface::stop()` assumes a peer manager exists. It must not be
  used as cleanup for an object whose `start()` was never called.
- `SkywalkInterface::free()` dereferences its expansion pointer before its
  null check. A subclass must use the lower base's cleanup if initialization
  failed before that storage was allocated. The lower bases were checked for
  absent storage and unregistered queues.
- `VirtualInterface::freeBpfResources()` frees its BPF lock without clearing
  the lock pointer. Call it once, not again through an additional `stop()`.

## Bounded native initialization probe

The experimental `awdl` build now includes `ItlAwdlNativeInterface`, a concrete
subclass of `IO80211AWDLProtocol`. Root-only selector 19 initializes and disposes
1–8 instances under the same gate as radio diagnostics. It refuses another OS
build (only 24H32 is audited), another PCI device, an active radio diagnostic,
an existing AWDL interface, or a leaked prior probe object. Nothing calls it at
boot. Capability bit 512 means this probe exists, not that AirDrop works.

```
sudo build/tools/awdl_native_init selftest
sudo build/tools/awdl_native_init run 1 1  # base init, cleanup
sudo build/tools/awdl_native_init run 1 2  # virtual init, cleanup
sudo build/tools/awdl_native_init run 1 0  # full init + backend contract checks
```

Stop-after phases exercise the wrapper's early exits; they do not simulate an
allocation failure inside Apple's code. Successful results require zero live
objects after release, no remaining controller open, and no method failures.
The complete phase checks virtual dispatch through all 63 native backend
methods, plus `setMacAddress` and the self-address getter. The 43 unimplemented
methods receive null and non-null opaque pointers and must not inspect request
bytes. Twenty implemented methods instead receive fully sized audited structures:
the sync-enabled getter reports disabled, the template setter stages an owned
copy, the BSSID setter owns the default pair, and election-ID set/get normalize
the native 16-bit value. Election-metric set/get retain the resolved 32-bit word,
and minimum-rate set/get retain a supported legacy OFDM rate. Master-channel set/get stage a channel from the radio's
active map; election-RSSI set/get normalize four signed bytes. The sync-state
setter owns a disabled role descriptor; presence and extension getters/setters
retain the associated timing parameters. Channel-sequence set/get own and
validate the qualified basic plan. Invalid updates preserve staged state. Full initialization must open
the controller successfully. These checks never start synchronization or RF.

The legacy declaration of `getSelfMacAddr()` incorrectly returned a pointer.
In 24H32 the getter returns six address bytes in the return register and native
`createPeerManager()` copies those bytes into an `ether_addr` value. The Sequoia
declaration now returns by value; the probe implements an in-memory address so
the getter also works before `start()` creates the native MAC address agent.

Initialization also releases one object per gate acquisition and waits outside
the gate for the instance count to settle, for at most 250 ms per object.
Deregistering power interest can defer a release even when `start()` was never
called; a tight loop under one gate can otherwise mistake this for a leak.

The initialization selector never calls `start()`: no peer manager, BSD `awdl0`,
packet queues, RF changes, or native AirDrop capability are registered. The compiled
class's 560 vtable entries (including the 63 backend slots) were compared with
24H32; no slot/symbol mismatch was found. Runtime evidence is recorded separately
in `VALIDATION.md` when available.

## Bounded native start probe

Root-only selector 20 additionally attaches an initialized role-6 object,
calls the native `start()` chain, inspects its peer-manager ownership, then
stops, detaches and releases it. Capability bit 1024 identifies this probe.
It uses the same exact-build/device checks and radio gate, accepts 1–4 injected
failure cycles but only **one real creation per boot**, and refuses existing
native AWDL objects. Registry matching cannot start the
backend; only this synchronous probe sets its private start permission.

```
sudo build/tools/awdl_native_init start-selftest
sudo build/tools/awdl_native_init start 1 1  # deliberately fail peer creation
sudo build/tools/awdl_native_init start 1 0  # native start/stop
sudo build/tools/awdl_native_init start-denied # reopen client; second creation must be denied
```

The injected failure returns null from this subclass's peer-manager factory,
after native start has retained its controller/work queue and created logging
resources. It does not modify Apple's code or allocator. Before a peer manager
exists, cleanup stops/releases the independently audited log pipe and stream,
stops power management, and calls the lower network-interface stop. The native
free path subsequently releases the controller/work queue. Once a peer manager
exists, the native virtual-interface stop handles it and the one BPF cleanup.
The controller's non-retaining AWDL pointer is cleared before the object is
released. The audited start ownership fields are restricted to 24H32, and the
compiled `CCPipe::stopPipe()` call was checked against native vptr offset `0x870`.

The first 24H32 run exposed deferred object release: both the injected-failure
and successful-start paths returned with one interface instance still counted,
but the following userspace count was zero (11 ms after the successful call).
The probe now performs one cycle per gate acquisition and waits outside the
gate, at most 250 one-millisecond sleeps, for native cleanup to settle.
An atomic lease excludes other lifecycle probes during that wait. A timeout
fails the request and prevents another cycle; ordinary Wi-Fi callbacks remain
able to run. No deferred release is counted as settled merely because stop
returned.

### Recreating the native manager panics on 24H32

Revision 13's subsequent real-start request triggered a confirmed page fault
at address `0xb8`. Its stack was the asynchronous `setPowerState` callback,
virtual-interface logging, `IO80211PeerManager::vlogDebug`, CoreCapture, and
`vsnprintf`. The machine had not slept. Waiting for object counts to reach zero
does not fix this failure.

Local inspection found that `PeerManager::initWithInterface` allocates shared
log buffers only when an atomic allocation counter's old value is zero.
`PeerManager::free` frees and clears those buffers but does not reset that
counter. A later manager therefore skips allocation. Its text-log path adds
the log-header length `0xb8` to the null buffer before formatting it as a
string, matching the panic. This observation is specific to the inspected
24H32 binary; it is not a claim about every macOS version.

The containment is a boot-scoped registry latch named
`AX201NativePeerManagerAttempted`, consumed immediately before native creation,
even if native creation fails. A subsequent real-creation request returns
`kIOReturnNotPermitted` before initialization. Multi-cycle real creation is a
bad argument. Closing the client or unloading this kext does not clear the
registry latch. Injected failures never enter native creation and do not consume
it. Apple's code and private global state are not modified.

This limits a diagnostic that proved unsafe; it does not qualify repeated
native teardown. A future operational backend must retain one native manager
through multiple discovery/transfer sessions. Recreating it after teardown
remains unavailable for this build.

## Retained native object diagnostic

Root-only selector 21 implements that lifetime separately from the one-shot
probe. It creates one disabled, unregistered native interface on explicit
request and retains it in the controller. Closing a diagnostic client leaves
the object alive. Repeated `create` requests return the same object and registry
ID; they do not repeat native initialization. Capability bit 2048 describes this
diagnostic, not networking support.

```
sudo build/tools/awdl_native_init session-selftest
sudo build/tools/awdl_native_init session status
sudo build/tools/awdl_native_init session create
# Reopen clients and inspect/reuse this same object.
sudo build/tools/awdl_native_init session create
sudo build/tools/awdl_native_init session retire
```

`retire` is terminal for this boot and uses the audited stop/disposal path. It
waits outside the gate for final release. Controller stop also disposes its child
before removing the radio/work queue. The retained path and the earlier real
start probe share the same boot latch, so testing one consumes the other's
creation opportunity. Failed injected factories and init-only tests do not.

The shared radio gate excludes userspace monitor diagnostics while this object
exists; normal STA Wi-Fi remains the intended active radio path. The separate
bounded radio qualifications below own internal leases. Neither
path implements concurrent STA/AWDL scheduling. No BSD interface, packet queues, service
registration, radio activation or native AirDrop capability is enabled. Runtime
qualification must verify stable identity across clients, disposal, denial of
recreation, and ordinary Wi-Fi health on the exact binary.

Reports include settled native peer-manager instance counts, balanced opens, disposal,
backend call counts and unexpected BSD interfaces. A passing lifecycle probe
does not establish a working radio or networking interface. There is still no
queue/BSD registration, link enable or packet transport from this path.

## Bounded radio qualification with the retained manager

Selector 28 / capability bit 131072 accepts a `BeginRequest` with channel zero,
an owned test peer's unicast address, and a 1000–5000 ms dwell. It requires the
exact audited build/device, a retained unpublished manager, successful native
configuration/template checks, and STA already powered off in INIT. It refuses
to disconnect an active STA connection itself. The staged master channel must
be a supported diagnostic channel in the firmware's active map.

```
sudo build/tools/awdl_native_init radio-selftest
# Explicitly power off STA before this bounded receive-only check.
sudo build/tools/awdl_native_init radio OWNED_PEER_MAC 3000
```

The native object acquires an internal identity-based lease under the existing
radio gate and reuses the monitor startup/stop machinery. This is a narrow
exception to native/diagnostic exclusion: userspace cannot supply the lease
identity, and linked mode, diagnostic TX and scheduled probes remain forbidden.
Native lifecycle requests cannot race it. The object, controller, radio and gate
are retained across sleeps; controller termination still cancels and restores
the radio before disposing native resources. Every sample rechecks ownership,
object activity and firmware generation. The independent radio timeout also
stops listening, including when the invoking process dies. Cleanup clears the
lease and captured diagnostic bytes.

The synchronous call samples AX201's qualified GP2 register outside firmware
sleep, checks consecutive rates against host time, and feeds coherent clock
samples to the owned-template encoder and independent parsers. The specimen's
AW timing is constructed, not an observed synchronization boundary. Only
aggregate clock/RX counters and cleanup state leave the kernel; no raw frame or
address is returned. No frame is transmitted, native RX is not invoked, and the
peer manager/interface/queues remain disabled. This qualifies radio ownership
and timing groundwork, not RF synchronization or an operational AirDrop link.
Hardware results are recorded separately in `VALIDATION.md`.

### Bounded native-template PSF transmission

Selector 29 / capability bit 262144 is an explicit TX qualification, separate
from the receive-only selector 28. It uses the same input and readiness checks,
plus an elected-self master role and a staged supported OFDM minimum rate.

```
sudo build/tools/awdl_native_init sync-tx-selftest
# STA must already be powered off; the test peer must be the user's device.
sudo build/tools/awdl_native_init sync-tx OWNED_PEER_MAC 3000
```

A local timeline starts from a coherent GP2 sample. It advances AW counters and
channel-plan slots, including counter/clock wrap. Its TU-aligned target timestamp
and separate fractional-TU PHY offset reconstruct the exact EAW boundary; it
does not round away the remaining fraction. The qualification uses 16-TU AWs
and the staged presence/plan. These are explicit test-timeline parameters, not
proof that the unimplemented native synchronization-parameter backend works.

At most four broadcast PSFs are built from the owned native template. Only one
can be pending, at most one is submitted per selected-channel EAW, and at least
12 ms must remain in that window. Transmission also requires a recent frame
from the specified owned peer, an active non-passive/non-DFS firmware channel,
and a hardware-clock snapshot no older than 2 ms. The internal submission helper
revalidates the frame, source address, standard BSSID and clock. It accepts no
userspace payload. The temporary monitor MAC uses the native interface's address
and staged minimum OFDM rate; the ordinary STA address is not modified.

Completion samples GP2 again to bound total elapsed time conservatively. This
includes firmware queueing and completion delivery; it is **not** a measured
on-air timestamp. The trial stops further submissions after a firmware failure,
invalid timestamp, or an observed completion bound over 5 ms. The response
reports submission/completion counts, raw final rate/status and timing bounds,
without claiming RF observation by the peer. Stop/timeout restores the radio
and clears the private TX lease.

Intel's [TX API](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/intel/iwlwifi/fw/api/tx.h)
describes media duration rather than an absolute on-air timestamp in the used
response. The gen3 command's eight trailing bytes are reserved in that source;
this implementation leaves them zero. Its [completion handler](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/intel/iwlwifi/mvm/tx.c)
accepts success and direct-done statuses; the trial handles both without changing
ordinary STA completion behavior.

The retained native peer manager, BSD interface and packet queues remain
disabled. This trial does not perform native RX handoff, native AW events,
master election, service discovery, IPv6 transport or AirDrop. Build/CI results
and hardware qualification must be recorded separately.

## Native configuration trace

Root-only selector 26 and capability bit 65536 invoke the exported, non-virtual
24H32 `IO80211AWDLPeerManager::configure()` once on the retained native object.
The interface must be unpublished and disabled, without registered transport
queues. The call runs under the existing radio gate from a userspace caller,
not on the native work-queue thread. It does not change the native manager's
private power/configuration fields or call operational enable.

```
sudo build/tools/awdl_native_init configure-selftest
sudo build/tools/awdl_native_init session create
sudo build/tools/awdl_native_init configure run
sudo build/tools/awdl_native_init configure status
```

The result records up to 32 backend calls in order, their exact virtual-table
offsets and return codes, with a dropped-entry count. It exposes no kernel
pointers or payloads. Reopening the client and repeating `run` returns the saved
result; it cannot repeat native configuration. The boot-scoped peer-manager
creation restriction remains in force. Init-only contract checks exercise trace
ordering and capacity, then clear that test trace without calling configure.

Native configure can return zero without doing work when its own power state
is zero or configuration was already attempted elsewhere. Zero is consequently
not proof of successful configuration. Revision 23 confirmed that its first
fatal backend request was `setAWDL_BSSID`, returning unsupported. The trace
identifies the next missing operation as configuration support is added.
Normal Wi-Fi health and interface/queue-disabled state must be checked after
the call. This diagnostic is not native AirDrop activation.

## Disabled network-identifier configuration

The 24H32 `setAWDL_BSSID` request is 16 bytes: a version word and two six-byte
identifiers at offsets 4 and 10. Native default initialization and its configure
producer independently establish the standard AWDL `00:25:00:ff:94:73` and
companion `00:25:00:ff:94:74` pair. Version 0 from the native producer and version
1 are accepted under the work-loop gate while the interface is disabled.

The backend now validates and owns this pair. Invalid input or an unsupported
custom pair leaves the old configuration intact. The synchronization encoder
requires the owned configuration and reads its AWDL identifier, instead of
emitting a hard-coded identifier regardless of whether configuration succeeded.
Init-only contract checks cover null/version/address rejection, producer
mutation, preserved state and the encoded frame identifier.

Acceptance stages the disabled configuration; it does not create an Intel
firmware interface, program a receive filter, or enable the companion protocol.
The future operational-enable transaction must do that work before reporting
readiness. The old isolated data diagnostic uses the same standard AWDL
identifier and is not connected to the native data path. Custom identifiers,
operational synchronization and AirDrop remain unsupported.

The native election-ID request has an eight-byte extent, with a 32-bit request
word at offset 4. The inspected 24H32 radio setter stores the low 16 bits and
its getter zero-extends those bits. The disabled backend now follows that
behavior, accepting request versions 0/1 after network-identifier configuration.
Zero, `0xffff`, and inputs with upper bits set are handled explicitly. Invalid
versions preserve the last value; querying an unset value returns not ready
without altering the caller's output. This stages an election parameter; it
does not execute election, select a master or start a radio clock. Live updates
are rejected while the interface is enabled until the radio scheduler can
apply them transactionally. Init-only checks exercise the concrete native
virtual calls and verify normalized reads and failure-state preservation.

Master-channel requests are eight bytes with a 32-bit channel at offset 4.
The disabled setter accepts versions 0/1 only after network configuration.
As in the inspected 24H32 backend, channel zero is a successful no-op: it does
not clear a previously selected channel. Nonzero selections must fit a known
channel encoding and exist in this radio's active channel map. The getter
returns version 1 and the staged channel, or not ready without changing output
when no channel has been selected. The diagnostic registry property
`AX201StagedMasterChannel` records a selection without exposing device identity.

Synchronization-frame encoding now requires staged election and channel state;
its caller cannot override the selected master channel. Init-only contracts
exercise zero, unset readback, valid selection, malformed updates and channel
mismatch rejection. Staging does not retune the radio. Operational enable and
each future transmit schedule must independently revalidate firmware regulatory
flags and timing before any frame is submitted.

Election-RSSI requests in 24H32 are **20 bytes**, including a fourth word at
byte 16 that was absent from the inherited declaration. This extent is checked
by both native ioctl wrappers; native configuration writes the fourth word.
The Sequoia-only declaration and compile fixture now cover it explicitly.
The radio setter takes each word's low byte, and the getter sign-extends it.
The disabled backend follows that normalization for all four values and owns
a copy. The peer manager resolves its sentinel before calling the backend;
the backend does not invent another sentinel or clamp the signal values.

Version 0/1 requests require staged network identifiers and a disabled
interface. Unset readback leaves the output unchanged, invalid versions leave
all four old values unchanged, and runtime contracts exercise signed endpoints,
upper-bit truncation, zero and producer-independent readback. No firmware
selection algorithm is enabled by accepting these parameters.

The 24H32 synchronization-state setter takes a 24-byte role descriptor: role
at offset 4, six-byte master address at 8, operation mode at 16 and additional
role flags at 20. The native radio maps roles 1/2/3 to elected master, follower
and non-election master. Its synchronization-enable request is a separate
operation. The disabled implementation now owns a normalized descriptor for
these roles with ordinary mode 0/1 and zero additional flags. Unsupported
modes/flags or malformed version/address/role leave the prior descriptor intact.
The getter remains unsupported; staged role state is not an active radio report.

The native configuration path requires earlier network, election, channel and
RSSI, presence and extension state before staging a role. The synchronization encoder binds its master
address to this descriptor and its source to the interface's own address;
mode zero prevents encoding. Init-only checks cover producer mutation, invalid
updates, each accepted role, and mismatched master/source rejection with an
unchanged output canary. No role is elected and no RF activity starts here.

Presence mode is an eight-byte request whose value at offset 4 is accepted
from 0 through 64, matching the inspected radio setter. The disabled backend
owns the complete value. Its readback returns version 1; malformed versions or
values above 64 preserve the previous selection. Wire encoding supports only
the previously qualified geometry (powers of two through 16) and rejects other
staged values rather than silently changing their meaning.

Extension parameters occupy 20 bytes. The native request order is multicast,
unicast, action, minimum at offsets 4, 8, 12, 16 respectively. Native firmware
submission retains the low byte of each word, and readback zero-extends each
byte. The backend owns these normalized values and binds all four counters and
presence to the synchronization encoder. Unset reads preserve output; invalid
updates preserve state. Runtime contracts cover normalization, boundary values
and encoder rejection when a caller disagrees with the owned timing. Registry
properties expose only these staged numeric parameters. These settings still
require application by the future radio scheduler before operational enable.

The Sequoia election-metric request is 12 bytes, not the legacy eight: the
native get/set wrappers require 12, and the peer manager reads an override byte
at offset 8 before forwarding the resolved metric at offset 4 to the backend.
The disabled backend owns the complete 32-bit metric without reinterpreting
the override policy; readback clears the override and padding. Zero and the
full unsigned range are preserved. This does not program firmware elections.

The minimum-rate request has an eight-byte ioctl extent, with its rate byte at
offset 4. The native default is 12, and the radio setter doubles it into firmware
500-kbit/s units. The disabled backend accepts the AX201's legacy OFDM rates
6, 9, 12, 18, 24, 36, 48 and 54 Mbps; unsupported rates preserve the previous
selection. Applying that rate to native TX still requires a radio enable path.
Canonical readback clears padding. These two layouts are corrected only for
the Sequoia WCL target. Runtime contracts check full-range metric ownership,
all supported rates, rejected updates, unset reads and canonical output.

The native channel-sequence request also retains its 400-byte extent, but its
inherited internal layout was incorrect for Sequoia. In 24H32 it contains
32 complete 12-byte `apple80211_channel` records beginning at offset 12, with
channel numbers at 16 + 12 * index. Native producer, getter and radio builder
agree on this layout; the four trailing bytes at offset 396 remain opaque.
The conditional declaration and compile fixture now match these boundaries.

The native encoding selector accepts 0/1; it is not the wire TLV encoding.
Native value 1 selects a separate firmware encoding and bandwidth policy and
remains unsupported. The sequence set/get backends now own the audited native
encoding-0 subset: 16 slots, a power-of-two step of 1..16 AWs, no duplicate or
fill substitution, and unbonded 2.4/5 GHz records. Zero channels mean unavailable
slots. Nonzero channels must have consistent band flags and belong to the
radio's active channel map; this is still not permission to transmit.

Versions 0/1 are accepted. Canonical readback uses version 1, zero unused records
and padding, and retains no producer pointers. Unknown tail bytes and alternate
command flags remain unsupported. Rejected updates preserve the entire previous
request and plan. The synchronization encoder requires exact agreement with
the stored plan, so a caller cannot advertise a different schedule. Portable
tests cover every short request extent, bad final records, ownership, reset,
unused-record handling and independent native-record-to-wire positions.
Hardware contracts also check virtual set/get dispatch and frame rejection for
a mismatched or absent plan. Firmware hopping and synchronization enable remain
unimplemented. Revision 31 qualified this disabled basic-plan path; see
`VALIDATION.md` for its exact source and runtime scope.

## Native packet prototype

Selector 22 and capability bit 4096 add an isolated, root-only packet-pool
diagnostic for AX201 / 24H32. It does not register a queue or network interface,
create a peer manager, or transmit packets. It is excluded while the retained
native interface or a radio diagnostic is active.

The Sequoia declarations now preserve the native pool's `release()` override,
its `0xb8` object extent, and zero-success `IOReturn` allocation/factory results.
The native packet hierarchy is `IOCommand` → `IOSkywalkPacket` (`0x70`) →
`IOSkywalkNetworkPacket` (`0x78`) → `IO80211NetworkPacket` (`0x78`), not an
`IOService` with padding. The fixture checks all four classes. Unused,
unaudited methods remain inaccessible rather than exposing guessed APIs.

The concrete driver packet implements the Wi-Fi metadata accessors, including
setters whose native base implementation panics. It resets timestamps, status,
interface ID and PTM metadata on every new packet lease. Storing PTM metadata
does not implement or advertise timestamp synchronization. The prototype uses
one 2048-byte buffer per packet and no device DMA options.

```
sudo build/tools/awdl_native_packets selftest
sudo build/tools/awdl_native_packets run 1 0 # normal CPU-only buffer lifecycle
sudo build/tools/awdl_native_packets run 1 1 # fail first packet factory
sudo build/tools/awdl_native_packets run 1 2 # fail fourth packet factory
sudo build/tools/awdl_native_packets run 1 3 # fail first memory-segment factory
```

Each request permits at most four cycles. A normal cycle tests eight concurrent
packet leases across three payload sizes (64, 1280, 1514), nonzero offsets,
metadata round trips, bounds rejection, and completion/deallocation. Contents
are checked after all eight buffers have been filled to detect aliasing.
Failure injection tests partial pool disposal. Settled driver pool/packet
instance counts must return to zero before another cycle begins. This is a
memory/ownership test, not a networking throughput or AirDrop test.

## Disabled native queue prototype

Selector 23 and capability bit 8192 add bounded queue ownership diagnostics.
They use `IOSkywalkTxSubmissionQueue` and `IOSkywalkRxCompletionQueue` through
audited 24H32 declarations. The common base is `IOEventSource`, with native
extents `0xd8`, `0x168` and `0x138`. Initialization, finalization and array RX
enqueue return `IOReturn`; zero means success. Unused, unaudited entry points
remain private aliases. Separate driver metaclasses count only driver-owned
objects. Revision 18 additionally wraps initialization/finalization for status
reporting, as described in the BSD publication section below.

```
sudo build/tools/awdl_native_queues selftest
sudo build/tools/awdl_native_queues run 1 1 # pool only, early cleanup
sudo build/tools/awdl_native_queues run 1 7 # both queues initialized, cleanup
sudo build/tools/awdl_native_queues run 1 0 # two disabled queue reuse cycles
```

Each request permits at most two cycles and excludes the other native/radio
diagnostics. It uses the existing radio work loop and gate, checks IOBSD
availability, creates a fixed pool and two 32-entry queues, and keeps them
disabled throughout. Early-exit stages 1–7 test cleanup after each setup step;
they do not simulate allocator failure inside the native implementation.

Queue initialization checks the queue into a retaining pool collection. The
diagnostic must finalize initialized queues before removing work sources and
releasing its queue/pool references, or it could leave a reference cycle.
Finalization is followed by reinitialization on the same pair in a normal run.
After all releases, owned packet, pool, TX and RX instance counts must settle
to zero within the bounded wait outside the gate. Callbacks return zero and
any invocation fails this disabled-queue diagnostic.

A normal cycle also attempts to enqueue eight simultaneous CPU packet leases
into the disabled RX queue, twice. Every call must return `kIOReturnOffline`
without taking ownership. The driver checks retained payload contents before
returning them. It verifies queue direction, identifier, empty capacity and
the native queue-info command. The queue base's buffer-count accessor returns
zero; it is not the per-packet single-buffer count. No queue is enabled, no
channel/ring or BSD interface is attached, and nothing reaches firmware.
Runtime qualification is recorded separately; passing these checks is not
evidence of packet delivery, peer discovery or AirDrop transfer.

## Retained queue registration prototype

Selector 24 and capability bit 16384 register the disabled TX/RX queue pair
with the existing retained AWDL object. A session must already exist; this
operation never starts another peer manager. The new `awdl_native_transport`
client supports `status`, `prepare STOP_AFTER`, `clear` and `selftest`.
`prepare 0` retains one pool, two work sources, and the native logical-link /
queue-set registration across client closes. Repeating it is idempotent.
Stages 1–6 deliberately clean up after pool creation, TX creation, RX creation,
TX source addition, RX source addition, and registration respectively.

The interface supplies an `awdl` BSD prefix and checks both native registration
copies and each queue's interface association. The registration remains
**unpublished**: BSD attachment is deferred, no service is published, no native
ring is initialized, and neither queue is enabled. Metadata keeps native defaults
for flags not yet qualified for BSD use. It is not a usable `awdl0` or AirDrop.
The callbacks live with the retained object and return zero; any invocation
is a diagnostic failure, not a successful radio operation.

Registration checks queues into the pool and retains the pool for both directions.
Cleanup calls the audited `deregisterEthernetInterface(0)` first, which checks
queues out and clears queue-set associations before releasing the native copies.
It then removes work sources and releases owned queues/pool. This ordering is
also used before retiring the retained interface. Clearing refuses an attached
BSD interface; live BSD shutdown and its reporting/lock ordering require further
implementation. Native `deferBSDAttach(bool)` returns `IOReturn`, corrected here
without changing its virtual slot. Runtime results belong in `VALIDATION.md`.

## Native BSD publication prototype

Selector 25 and capability bit 32768 explicitly publish the retained interface
to `IOSkywalkNetworkBSDClient`. `awdl_native_bsd` provides `selftest`, `status`
and `publish`. A retained session and successful queue registration must exist
first. Publication releases the deferred-BSD property outside the wireless gate
so native matching can acquire that gate. It is asynchronous and idempotent;
status distinguishes the request from a prepared ifnet and attached BSD client.
Nothing publishes this interface automatically at boot.

The native BSD client creates its Nexus/netif queue contexts. Both queue
subclasses now wrap native initialize/finalize, preserving return values and
recording initialization errors: the inspected native queue callback otherwise
ignores the initialize result. The wrappers add four diagnostic words after
each unchanged base layout. Status reports initialized state separately from
kernel-queue presence and enabled state.

The radio backend is still incomplete. The interface overrides `enable` to
preserve `IOSkywalkNetworkInterface`'s error and rejects true
`setInterfaceEnable` requests with `kIOReturnNotReady`. It does not enable TX/RX
or claim successful radio operation. Its BSD preparation preserves a failed
lower-base result before configuring ifnet/BPF; BPF/native reporting internals
are not claimed to have exhaustive error handling.

After publication, the earlier manual `clear` and `session retire` controls are
rejected. Those probe paths must not free queue/interface resources owned by a
live BSD/Nexus client. The object is retained until system-managed termination
or reboot; live unpublication is not yet a supported control. This is a staged
integration boundary, not native AirDrop support or sleep/unload qualification.
Runtime evidence and failures are recorded separately in `VALIDATION.md`.

On 24H32, a successful `ifconfig awdl0 up` does **not** prove operational
enable. The native BSD client's flags ioctl first tries `adjustInterfaceEnable`.
If enable fails, it clears its enabled state and `IFF_RUNNING`, then invokes
the provider's BSD command handler and returns that handler's status. The
administrative `IFF_UP` flag can remain set with a zero ioctl return. The
runtime check therefore verifies rejected enable calls, clear `IFF_RUNNING`,
disabled queues and zero callbacks; it does not treat command exit status as
radio readiness. No native error-return or flag state is patched.

## Management-frame input boundary

The native management-frame path uses `IO80211BufferPool` / `IO80211Buffer`,
separate from Skywalk data packets. Their 24H32 native base extents are `0x10`
and `0x18`; the pool derives directly from `OSObject`. Pool get/return are
virtual slots `0x118` / `0x120`. The base implementations panic, so only a
concrete pool returned by the interface may be called, using virtual dispatch.
The buffer's native `setLength` stores a 32-bit length without checking capacity.

`VirtualInterface::getMgmtFramePool()` returns the controller's borrowed pool.
The buffer overload of `bpfTapInput(..., 12, metadata, 72)` forwards the lease to
the native peer manager. Native input either returns it or queues it, copying
the 72-byte metadata in the queued path. The caller must not return or release
the buffer after handoff. The management pool must outlive the peer manager's
queued frames. Both helpers are nonvirtual and add no interface slots.

The kernel-only `receiveAction` adapter checks the work-loop gate, native
interface readiness and backend sync-enabled state before borrowing a buffer.
It validates the wire-frame boundary and metadata source-MAC prefix, checks
native capacity, and copies the action body without its 24-byte 802.11 header.
Pre-handoff failures return the lease; after handoff only the temporary pool
retain is released. A zero return would mean handoff, not successful parsing,
discovery or delivery. The current backend still rejects enable.

This entry is **not connected to firmware RX**. It now accepts a typed receive
context and encodes a zero-initialized, aligned 72-byte native header. The
audited native producer supplies a source MAC at 0, a 12-byte channel at
`0x0c`, signed RSSI at `0x2c`, and a 32-bit local on-air clock at `0x30`, in
x86_64 host endian. The channel encoding currently supports only 20 MHz
2.4/5 GHz reception. It describes reception, not regulatory permission to
use a channel or transmit. The rate word at `0x28` remains zero because its
unit conversion has not been qualified. `0x40` is a chain-RSSI validity word
and `0x44` contains four signed RSSI bytes, **not a timestamp high word**;
both remain zero. All other unimplemented fields remain zero as well.

The caller must supply a valid local on-air microsecond clock in the same
domain as the future sync backend. Host delivery time, the remote frame's
PHY timestamp and Intel's per-MAC TSF must not be substituted for one another.
The live GP2/sync-state mapping and the native rate representation are still
unqualified, so successful encoding does not authorize native receive input.
Separately, timed radio diagnostics now distinguish valid GP2 (flag bit 0)
from valid TSF (bit 1). A TSF union marked `IWX_RX_MPDU_PHY_TSF_OVERLOAD` is
PHY data: its exported TSF value is zero and bit 1 is clear. Older captures
without bit 1 do not establish TSF validity; GP2 schedule analysis is unchanged.

The adapter rejects duplicate/short synchronization TLVs and zero AW period
or presence before allocation. Native sync parsing was also observed to
reject zero presence before entering its peer-timeline calculation. This is
not a complete audit of every native TLV handler. Native sync activation,
full TLV semantics and runtime buffer ownership still need qualification.
Offline checks cover native header bytes, malformed/truncated boundaries,
source mismatch, fragments, size limits, channel/RSSI/clock rejection and TSF
union validity. The two management-buffer compile fixtures matched 71 native
virtual slots; this establishes declarations, not live receive behavior.

## Synchronization action output

`copySyncAction` now combines the owned native supplementary template with a
caller-supplied synchronization context under the controller work-loop gate.
The portable encoder produces a complete PSF or MIF MPDU without FCS, with a
bounded synchronization TLV, a 16-entry channel sequence and the saved native
TLVs. It supports simple and operating-class channel encodings; legacy bonded
encoding remains unsupported. Maximum output is 628 bytes. The kernel entry
refuses absent templates, inactive/disposed objects, a wrong OS build or a
missing gate. It does not submit the resulting bytes to firmware.

The wire format was checked against
[Wireshark's AWDL dissector](https://github.com/wireshark/wireshark/blob/ad85ea3cb5af4e9665f1b3e3cda5bee94b71ab73/epan/dissectors/packet-awdl.c)
and [Open Wireless Link's protocol declarations](https://github.com/seemoo-lab/owl/blob/da255a70f221784c836d943dd3f243bc798f223b/src/frame.h).
Bounds, clock and frame-subtype validation, output-capacity rejection, sequence and
32-bit clock wraps, unavailable channel slots, and template ownership are
covered offline. A supplementary sync TLV or conflicting/duplicate channel
sequence is rejected. Failed encoding leaves output bytes untouched, and the
caller receives zero length. The initialization probe additionally checks the
concrete template-to-frame entry using synthetic addresses and time in CPU
memory; its new runtime coverage remains pending until that binary is loaded.

Two synthetic PCAP specimens were independently decoded by Wireshark: a PSF
with simple channels and a MIF with operating classes and a clock wrap. Both
decoded the intended sync counters, periods, timestamps and channel encodings
without expert-format errors. These were generated files, not radio captures.

This codec preserves explicitly supplied timing and channel fields. The
backend must still provide coherent radio-clock state, validate regulatory and
channel-width support, select real availability windows and account for
firmware submission delay. Native election/configuration, actual synchronization
transmission, AW event delivery and peer acceptance remain unimplemented.

## Request extents

### Availability-window event representation

The 24H32 producer, event-name table and peer-manager dispatcher identify
**41** as `APPLE80211_M_AWDL_AVAILABILITY_WINDOW_START`. Event **42** means
window **extension** start; 43 means extensions end and 46 means sync-state
change. The inherited start definition of 42 is corrected only for the Sequoia
WCL target. No event is currently emitted by this experimental backend, so this
is a prerequisite fix, not an explanation for an observed RF failure.

The AW-start encoder uses the audited 36-byte event representation:

| Offset | Representation |
| --- | --- |
| 0 | 16-bit AW counter zero-extended to a host-endian 32-bit word |
| 4 | Host-endian native role: elected master 1, follower 2, non-election master 3 |
| 8, 14 | Six-byte immediate-master and top-master MAC addresses |
| 20 | Local native anchor timestamp, **big endian** |
| 24 | 12-byte host-endian native channel (version, primary channel, flags) |

This timestamp differs from the host-endian action-RX metadata timestamp.
The native peer-timeline calculation subtracts 3500 us from the stored AW
anchor. The codec preserves its input value exactly; it does not guess a
GP2-to-anchor adjustment or substitute timer-delivery time for a hardware
boundary. Invalid role, address, channel or absent clock yields a cleared
output. A valid zero clock and counter wrap are accepted. Support is limited
to the same 20 MHz channel representation as the RX encoder.

`IO80211SkywalkInterface::reportDataPathEvents` returns a **boolean gate-failure
flag**, not `IOReturn`: its native wrapper converts the gate result with
`test eax; setne al`. The gate callback invokes the peer manager, may forward
the event to userspace, and returns zero independently of whether the peer
manager forwarded it. False therefore indicates completed dispatch, not an
acknowledged RF event or peer discovery. The Sequoia declaration and compile
fixture now preserve this return type without changing its vtable slot.

The encoder is offline-tested but is not connected to native event dispatch.
Native synchronization must be configured and the actual radio boundary/clock
mapping qualified before emitting these events. A successful serialization
alone cannot make the current disabled backend ready.

### Backend request storage

Two backend requests now have concrete host behavior. `getAWDL_SYNC_ENABLED`
clears its audited 12-byte output, sets version 1 and reports disabled. Neither
`IFF_UP` nor a saved template makes synchronization enabled. The setter for
sync enable still returns unsupported, and interface enable still fails.

`setAWDL_SYNC_FRAME_TEMPLATE` accepts the audited 16-byte descriptor under the
controller work-loop gate. Version 0 is used by the native producer; version 1
is also accepted. Its payload must be a nonempty, bounds-checked TLV sequence
of at most 512 bytes. The driver owns a complete snapshot before returning;
failed updates preserve the previous bytes/revision. Stop/free clears it.
This is host staging only: it does not program firmware or transmit. The
native producer's 512-byte stack buffer contains supplementary TLVs and omits
the action header and radio-generated synchronization fields. The future RF
backend must construct those separately. On the inspected 24H32 build, selector
151 has a handler only in the internal set table; both external set tables
contain null entries. The internal wrapper checks the interface state and casts
to the AWDL protocol before calling the backend with the native kernel producer's
descriptor. No new user-client method accepts this pointer-bearing structure.
Revision 20 qualified the disabled getter, owned template snapshot, rejection
and cleanup contracts during bounded initialization on 24H32. It did not call
the native peer manager's template producer or enable synchronization; see
`VALIDATION.md` for the exact runtime scope.

The appended root-only `template` diagnostic calls the native
`updateSyncFrameTemplate(bool)` once after successful disabled configuration.
The 24H32 function returns `IOReturn`; its only driver ioctl is the template
setter. Its builders require the P2P supervisor and real-time manager that
successful native `initWithInterface` creates. The probe reuses that retained
manager, stays under the controller gate, refuses the native work-queue thread,
and rejects missing configuration, BSD publication, registered transport or an
enabled interface. It neither enables synchronization nor calls a TX routine.

```sh
sudo build/tools/awdl_native_init template-selftest
sudo build/tools/awdl_native_init template status
# After the retained session's successful configure run:
sudo build/tools/awdl_native_init template run
```

The producer's TLVs are copied by the existing owned-template setter. A
separate saved trace records its backend call without changing the prior
configuration trace. Repeating `run` returns the first result, including a
failure, without another native invocation or peer creation. The response
contains length, revision, TLV count and kinds, never raw template bytes or
addresses. A CPU-only integration specimen combines this actual template with
the staged plan, role and timing counts, then decodes it to check consistency.
Its clock and period are synthetic; `encoded_length` proves neither hardware
timing nor RF transmission. Firmware enable, native RX and AirDrop remain
unqualified by this diagnostic. It is not an arbitrary template-input API.

The queue-array overload of `IOSkywalkEthernetInterface::registerEthernetInterface`
at `0xffffff8002988078` returns a full `IOReturn`, preserving the copy/registration
status in the 32-bit return register. The old `bool` declaration reverses the
success interpretation and can discard error bits. The Sequoia declaration and
compile fixture now require `IOReturn`. This nonvirtual declaration correction
does not register a queue or change the native vtable.

`initRegistrationInfo` still returns `bool` and expects version 1 with extent
`0x130`. Native network registration copies its `0x108` base portion, whereas
Ethernet registration copies `0x130`. The existing legacy Wi-Fi shim uses its
own larger storage; shrinking its declaration without correcting allocation,
copy and free together would introduce an overflow. Native queue integration
must use verified registration ownership, not placeholder queue pointers or
an isolated size change.

| Request | Observed requirement | Evidence in 24H32 |
| --- | --- | --- |
| `apple80211_awdl_sync_params` | Exactly 24 bytes; word at offset `0x14` | IO80211Family `getAWDL_SYNC_PARAMS` at `0xffffff800220e290` requires `req_len == 0x18`; peer-manager fallback clears 24 bytes; Broadcom getter at `0xffffff80016aba68` and setter at `0xffffff80016ae6b2` access the final word |
| `apple80211_awdl_sync_state` | Exactly 24 bytes; six-byte field at offset 8 | IO80211Family `getAWDL_SYNC_STATE` at `0xffffff800220e836` requires `req_len == 0x18`; peer-manager getter at `0xffffff80021b8166` and Broadcom getter at `0xffffff80016abdd8` both write a word at 4 and six bytes at 8 |
| `apple80211_awdl_sync_enabled` | Backend enable word at offset 8 | Broadcom getter at `0xffffff80016ab942` writes a 32-bit value there; this agrees with the existing 12-byte declaration, but does not independently prove the complete ioctl schema |
| `apple80211_awdl_sync_frame_template` | Length at 4, pointer at 8 | Broadcom setter at `0xffffff80016ae344` checks both before submitting the template; this agrees with the existing 16-byte declaration |

The legacy sync-parameter/state declarations had extents of 20 and 8 bytes.
They are expanded **only for the Sequoia WCL build**, with unknown fields kept
opaque. The compile-only native fixture checks these sizes and offsets. No
backend may infer the semantics of the opaque bytes from their existence.

## Software fallback is selective

The peer-manager getters for sync parameters, sync state and election ID compare a backend
result against `kIOReturnUnsupported` (`0xe00002c7`) and then use software-held
state. This is a verified fallback for those requests, not permission to
return success for unimplemented firmware operations, and not proof that all
63 backend methods may be unsupported. Some Broadcom methods return BSD error
values; changing return conventions globally would lose these specific native
fallbacks.

Next: implement action/data ownership, firmware MAC/PHY contexts and peer
scheduling, and qualify live BSD teardown and native RX metadata before
enabling radio transport. Default capability
bits must continue to describe the implemented Wi-Fi driver, not an unverified
AirDrop service.

### Synchronization TX timing attribution

Revision 35's first native-template TX completion exceeded the qualification
limit. The next instrumentation keeps that limit and the four-frame bound.
After a TX trial, `AX201NativeTxTiming` on the retained native interface holds
up to four records containing preparation-to-doorbell, doorbell-to-return,
doorbell-to-completion and notification-batch timing. Existing GP2 samples are
compared with monotonic host-clock intervals. A doorbell stamp is taken just
before the existing MMIO write, without another peripheral lock/read.

An atomic flag arms a lock-free timestamp in the existing interrupt filter only
while a qualification transmission is outstanding. The gated RX handler saves
that batch's filter timestamp before processing its notifications. These are
**batch** timestamps, not proof of which interrupt caused a TX response; absent
or out-of-order interval fields must not be treated as zero latency. Firmware
media duration, retry counts and Bluetooth blocking counts are reported
separately. No interval is represented as an actual on-air TX timestamp.

Records are published after radio stop, contain no payloads or peer identities,
and are cleared at the next accepted native TX trial. No user-client ABI,
native interface enablement, firmware settings or timing threshold changes.

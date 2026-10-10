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
sudo build/tools/awdl_native_init run 1 0  # full init + unsupported-method checks
```

Stop-after phases exercise the wrapper's early exits; they do not simulate an
allocation failure inside Apple's code. Successful results require zero live
objects after release, no remaining controller open, and no method failures.
The complete phase also checks virtual dispatch through all 63 native backend
methods using null and non-null opaque pointers, plus `setMacAddress` and the
self-address getter. Full initialization must open the controller successfully. The
unimplemented methods return explicit errors and do not inspect request bytes.

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

The shared radio gate excludes exclusive monitor diagnostics while this object
exists; normal STA Wi-Fi remains the intended active radio path. That exclusion
is not concurrent STA/AWDL scheduling. No BSD interface, packet queues, service
registration, radio activation or native AirDrop capability is enabled. Runtime
qualification must verify stable identity across clients, disposal, denial of
recreation, and ordinary Wi-Fi health on the exact binary.

Reports include settled native peer-manager instance counts, balanced opens, disposal,
backend call counts and unexpected BSD interfaces. A passing lifecycle probe
does not establish a working radio or networking interface. There is still no
queue/BSD registration, link enable or packet transport from this path.

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
remain private aliases. Separate driver metaclasses add no virtual overrides
and allow counting only the objects owned by the diagnostic.

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

## Request extents

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

The peer-manager getters for sync parameters and sync state compare a backend
result against `kIOReturnUnsupported` (`0xe00002c7`) and then use software-held
state. This is a verified fallback for those two requests, not permission to
return success for unimplemented firmware operations, and not proof that all
63 backend methods may be unsupported. Some Broadcom methods return BSD error
values; changing return conventions globally would lose these specific native
fallbacks.

Next: validate started-interface teardown, remaining required request sizes,
action/data output ownership, firmware MAC/PHY contexts, and native RX queue
metadata before instantiating and registering an interface. Default capability
bits must continue to describe the implemented Wi-Fi driver, not an unverified
AirDrop service.

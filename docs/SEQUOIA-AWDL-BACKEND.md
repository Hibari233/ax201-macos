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

Reports include settled native peer-manager instance counts, balanced opens, disposal,
backend call counts and unexpected BSD interfaces. A passing lifecycle probe
does not establish a working radio or networking interface. There is still no
queue/BSD registration, link enable or packet transport from this path.

## Request extents

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

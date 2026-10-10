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

This stage deliberately rejects `start()`: no peer manager, BSD `awdl0`, packet
queues, RF changes, or native AirDrop capability are registered. The compiled
class's 560 vtable entries (including the 63 backend slots) were compared with
24H32; no slot/symbol mismatch was found. Runtime evidence is recorded separately
in `VALIDATION.md` when available.

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

Next: integrate started-interface teardown, remaining required request sizes,
action/data output ownership, firmware MAC/PHY contexts, and native RX queue
metadata before instantiating and registering an interface. Default capability
bits must continue to describe the implemented Wi-Fi driver, not an unverified
AirDrop service.

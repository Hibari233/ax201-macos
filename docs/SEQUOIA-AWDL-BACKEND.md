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

Next: audit initialization and teardown, remaining required request sizes,
action/data output ownership, firmware MAC/PHY contexts, and native RX queue
metadata before instantiating and registering an interface. Default capability
bits must continue to describe the implemented Wi-Fi driver, not an unverified
AirDrop service.

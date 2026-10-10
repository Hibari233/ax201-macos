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

## Required hardware matrix before a binary release

| Test | Current qualification |
| --- | --- |
| Fresh public-fork binary boot and loaded-hash verification | Pending |
| WPA2 2.4 GHz connect/reconnect and throughput | Prototype bounded connectivity only |
| WPA2 5 GHz, supported channel groups | Pending |
| WPA2/WPA3 transition with optional PMF | Pending |
| WPA3-only with mandatory PMF, wrong password, rekey | Unsupported / pending implementation |
| Peer AWDL RX while STA stays associated on the same channel | Pending |
| Concurrent channel scheduling with normal data traffic | Pending implementation |
| Native discovery and bidirectional AirDrop file hashes | Pending implementation |
| Wi-Fi power cycling, repeated sleep/wake and roaming | Incomplete; whole-system sleep needs a separately stable platform |
| Sustained transfer, long idle and firmware error recovery | Pending |

Report hardware results with the source commit, binary hash, OS build, adapter
PCI ID, AP security/channel configuration, exact steps and failures as well as
successes. Keep identifiable network information out of public reports.

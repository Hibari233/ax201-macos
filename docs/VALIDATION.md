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

## Required hardware matrix before a binary release

| Test | Current qualification |
| --- | --- |
| Fresh public-fork binary boot and loaded-hash verification | Revisions 10 and 11 identities verified above; release matrix remains incomplete |
| WPA2 2.4 GHz connect/reconnect and throughput | Bounded reconnection and 10/10 final Wi-Fi pings; sustained throughput pending |
| WPA2 5 GHz, supported channel groups | Pending |
| WPA2/WPA3 transition with optional PMF | Pending |
| WPA3-only with mandatory PMF, wrong password, rekey | Unsupported / pending implementation |
| Peer AWDL RX while STA stays associated on the same channel | Pending |
| Exclusive AWDL IPv6 RX | Bounded 1280-byte reception/checksums verified; unfragmented 1500-byte case unresolved |
| Fixed AWDL data TX | Bounded firmware completions observed; zero echo replies, not bidirectional IPv6 |
| Concurrent channel scheduling with normal data traffic | Pending implementation |
| Native discovery and bidirectional AirDrop file hashes | Pending implementation |
| Wi-Fi power cycling, repeated sleep/wake and roaming | Incomplete; whole-system sleep needs a separately stable platform |
| Sustained transfer, long idle and firmware error recovery | Pending |

Report hardware results with the source commit, binary hash, OS build, adapter
PCI ID, AP security/channel configuration, exact steps and failures as well as
successes. Keep identifiable network information out of public reports.

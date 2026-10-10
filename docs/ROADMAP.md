# Roadmap and acceptance criteria

The first release scope is AX201 on Sequoia. There is no promised date for full
AirDrop or universal adapter support. Each step needs its own evidence.

| Stage | Work | Exit criterion |
| --- | --- | --- |
| 0. Maintainable fork | Preserve upstream history; pin SDK; separate build variants; automate offline tests | Clean checkout builds both variants; tests and CI pass; provenance is recorded |
| 1. Wi-Fi baseline | 2.4/5 GHz joins, reconnects, network switching, roaming, throughput and radio power lifecycle | Repeatable matrix with explicit OS/AP/adapter details and no firmware restart or regression |
| 2. WPA3 | Trace Apple WCL authentication requests; SAE exchange; PMF/IGTK handling; key installation and rekey | WPA3-only AP with required PMF connects; negative authentication and reconnect/rekey tests pass; WPA2 stays working |
| 3. AWDL radio coexistence | Confirm same-channel peer RX; implement independent MAC/PHY context lifecycle and permitted scheduling | Ordinary STA traffic stays working while AWDL receives/transmits, including scan/cancel/restart recovery |
| 4. AWDL protocol and data | Election, advertisements, synchronization, peer state, encapsulation and IPv6 | Bidirectional peer IPv6 with verified data integrity and bounded recovery from peer/channel changes |
| 5. Native AirDrop | Bind verified native ABI/backend and queues; integrate with system services and discovery | Actual send and receive of small/large files with matching checksums across repeated sessions |
| 6. Release qualification | Long runs, power transitions, crash recovery, multiple machines; document support limits | A tagged candidate passes the published matrix and has a documented rollback path |

## Immediate engineering work

1. Establish an offline WPA3 capability/authentication test boundary around the
   WCL join and net80211 RSN selection paths. Distinguish PSK, SAE and mandatory
   PMF explicitly; do not claim support by changing advertised capability bits.
2. Determine which SAE state is owned by the Apple stack and which must be
   implemented in the driver. Reuse reviewed protocol implementations when their
   interface and license fit; do not invent cryptographic primitives.
3. Resume same-channel linked AWDL reception when a controlled WPA2 5 GHz network
   and an owned peer can share a permitted channel. Until then, retain the
   unvalidated status rather than assuming it works.

WPA3 and AWDL are separate tracks. Completing one does not complete the other.
The AX201 Bluetooth function and macOS discovery/identity services also need
end-to-end validation for AirDrop; Wi-Fi protocol work alone is insufficient.

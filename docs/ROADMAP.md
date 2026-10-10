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

AirDrop is the current priority. Stage numbers describe dependencies, not a
requirement to complete WPA3 before AWDL.

1. Extend the verified 1280-byte AWDL receive path into bidirectional data.
   Revision 11 adds explicit, bounded fixed echo transmission and reply checks;
   hardware results must distinguish submission, completion and peer replies.
   Unfragmented 1500-byte reception remains unqualified.
2. Establish bidirectional peer IPv6: validated peer state, advertisements,
   synchronization, permitted firmware TX/ACK handling, then independent
   radio contexts and STA coexistence. Passive reception alone is insufficient.
3. Bind the verified Sequoia native backend and packet queues, then demonstrate
   native discovery and actual bidirectional file transfers with matching hashes.
   Revision 14 verifies one retained native manager across diagnostic clients;
   the operational design must reuse it because recreation after native teardown
   panics on the inspected 24H32 image. Queue ownership and registration remain
   the next integration work, not a demonstrated AirDrop capability.
4. Return to WPA3: trace Apple WCL SAE/PMF ownership, implement the missing
   authentication/key lifecycle, and qualify it separately from AirDrop.

WPA3 and AWDL are separate tracks. Completing one does not complete the other.
The AX201 Bluetooth function and macOS discovery/identity services also need
end-to-end validation for AirDrop; Wi-Fi protocol work alone is insufficient.

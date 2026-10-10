# AWDL IPv6 data development

Revision 10 build 1 added an experimental data receive boundary. Revision 11
adds explicit fixed ICMPv6 echo transmission to an owned peer. Neither provides
native AirDrop: `awdl0`, peer election, service advertisement, native packet
delivery and normal bidirectional scheduling remain unimplemented.

`include/AWDL/AwdlData.h` decodes unencrypted, unfragmented AWDL data MPDUs with
the AWDL BSSID and Apple LLC/SNAP encapsulation. It supports short and bounded
long headers, QoS/HT headers, raw A-MSDU and an explicitly flagged firmware
deaggregated MSDU. IPv6 version, exact payload length and the 1500-byte MTU are
checked. Aggregates are validated in full before any packet is queued; at most
16 packets are accepted per MPDU. Decoder views borrow the input buffer.

The short-form encapsulator is used by the fixed echo diagnostic. Wire-format
references are the [Wireshark AWDL dissector](https://gitlab.com/wireshark/wireshark/-/blob/master/epan/dissectors/packet-awdl.c)
and [Open Wireless Link](https://github.com/seemoo-lab/owl). No OWL GPLv3 source
is incorporated in this implementation.

Diagnostic selectors 15–17 are append-only extensions to ABI 1: BeginData,
ReadData and DataStatus. BeginData has the same channel, owned-peer and 30-second
limits as the earlier exclusive monitor session, and requires ordinary Wi-Fi to
be off. It does not broaden the linked-STA receive filter. Legacy management
probe TX and radio test commands are rejected in this mode. The owner alone can read packets or
close the session. Packet memory is wiped on read, close, next session and detach.
Timeout stops the radio and retains unread records for the owner until close.

Selector 18 (`DataProbe`) accepts a 16-byte request: ABI version, IPv6 length
(64–1500), nonzero run token and zero reserved word. It is an explicit operation;
ordinary RX commands never transmit it. The driver constructs a fixed
[ICMPv6 Echo Request](https://www.rfc-editor.org/rfc/rfc4443.html#section-4.1),
derives link-local addresses from its own MAC and the selected peer's AWDL MAC,
and permits at most eight submissions, one outstanding, at least 100 ms apart.
The peer must have been heard on the selected channel within 500 ms. Firmware
regulatory, passive and DFS restrictions remain enforced. Users cannot supply
arbitrary packets or destination addresses. These derived IPv6 addresses must
be checked against the owned peer before a test; no address discovery is implied.

Monitor unicast data uses an explicit 6 Mbit/s OFDM command rate. It must not
inherit infrastructure rate control: the monitor station has no associated
link-quality table. Host submission, firmware completion and validated echo
reply are separate results. The client validates reply addresses, ICMP type,
run token, sequence, every payload byte and transport checksum, then removes
duplicate replies. Firmware success alone does not establish peer IPv6 or AirDrop.
The current client triggers after reception from the selected peer, rather than
an unrelated periodic host tick. This reduces one source of host delay but is
not negotiated AWDL scheduling; initial hardware tests still received no replies.

Build clients with `python3 scripts/build_awdlctl.py`. On a dedicated test
machine with a verified rollback path, the new client accepts:

```text
sudo awdl_data_rx selftest
sudo awdl_data_rx status
sudo awdl_data_rx listen CHANNEL SECONDS OWNED_PEER_MAC
sudo awdl_data_rx expire CHANNEL SECONDS OWNED_PEER_MAC
sudo awdl_data_rx close CHANNEL SECONDS OWNED_PEER_MAC
sudo awdl_data_rx die CHANNEL SECONDS OWNED_PEER_MAC
sudo awdl_data_rx probe CHANNEL SECONDS OWNED_PEER_MAC IPV6_LENGTH
```

Use only your own peer. The caller must restore ordinary Wi-Fi power after the
test; use an independent Ethernet management connection. `expire` checks kernel
timeout restoration, `close` ends halfway through the dwell to test explicit
close, and `die` checks client-death cleanup. The client prints
packet lengths, sequence numbers and aggregate counters, not addresses/payloads.
Zero packets is an observation, not evidence of a working data path.
The client also verifies checksums on direct UDP/ICMPv6 packets. It reports
extension-header traffic, including fragments, as checksum-unavailable; it does
not reassemble it or claim transport integrity for those packets.

On the initial development machine, 1280-byte IPv6 reception was verified with
checksums. A 1500-byte stimulus using normal macOS minimum-MTU behavior arrived
as 1280/276-byte IPv6 fragments. A separate unfragmented 1500-byte stimulus
produced zero accepted data records; the cause is undetermined and this size
remains unqualified. See [validation](VALIDATION.md) for counts and binary identity.

Native delivery still needs peer lifecycle, replay/duplicate handling,
bidirectional radio scheduling and ACKs, validated native interface/queue
integration and end-to-end discovery/file-integrity tests. Do not turn diagnostic
receive capability into an advertised AirDrop capability bit.

# AWDL IPv6 receive development

Revision 10 build 1 adds an experimental data receive boundary. It is not native
AirDrop support: no `awdl0`, peer election, service advertisement, data TX/ACK or
system packet delivery is implemented by this change.

`include/AWDL/AwdlData.h` decodes unencrypted, unfragmented AWDL data MPDUs with
the AWDL BSSID and Apple LLC/SNAP encapsulation. It supports short and bounded
long headers, QoS/HT headers, raw A-MSDU and an explicitly flagged firmware
deaggregated MSDU. IPv6 version, exact payload length and the 1500-byte MTU are
checked. Aggregates are validated in full before any packet is queued; at most
16 packets are accepted per MPDU. Decoder views borrow the input buffer.

The short-form encapsulator is offline-tested only; it is not wired to firmware
transmission. Wire-format references are the [Wireshark AWDL dissector](https://gitlab.com/wireshark/wireshark/-/blob/master/epan/dissectors/packet-awdl.c)
and [Open Wireless Link](https://github.com/seemoo-lab/owl). No OWL GPLv3 source
is incorporated in this implementation.

Diagnostic selectors 15–17 are append-only extensions to ABI 1: BeginData,
ReadData and DataStatus. BeginData has the same channel, owned-peer and 30-second
limits as the earlier exclusive monitor session, and requires ordinary Wi-Fi to
be off. It does not broaden the linked-STA receive filter. Probe TX and radio
test commands are rejected in this mode. The owner alone can read packets or
close the session. Packet memory is wiped on read, close, next session and detach.
Timeout stops the radio and retains unread records for the owner until close.

Build clients with `python3 scripts/build_awdlctl.py`. On a dedicated test
machine with a verified rollback path, the new client accepts:

```text
sudo awdl_data_rx selftest
sudo awdl_data_rx status
sudo awdl_data_rx listen CHANNEL SECONDS OWNED_PEER_MAC
sudo awdl_data_rx expire CHANNEL SECONDS OWNED_PEER_MAC
sudo awdl_data_rx die CHANNEL SECONDS OWNED_PEER_MAC
```

Use only your own peer. The caller must restore ordinary Wi-Fi power after the
test; use an independent Ethernet management connection. `expire` checks kernel
timeout restoration, and `die` checks client-death cleanup. The client prints
packet lengths, sequence numbers and aggregate counters, not addresses/payloads.
Zero packets is an observation, not evidence of a working data path.

Native delivery still needs peer lifecycle, replay/duplicate handling,
bidirectional radio scheduling and ACKs, validated native interface/queue
integration and end-to-end discovery/file-integrity tests. Do not turn diagnostic
receive capability into an advertised AirDrop capability bit.

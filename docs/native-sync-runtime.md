# Bounded native synchronization engine (24H32 / AX201)

Selector 37 connects the retained native manager to the real P2P discovery
context. A kernel timer waits for the current firmware generation's START
notification and GP2/RX correlation before invoking native `enableAWDL`.
The implemented `setAWDL_SYNC_ENABLED` accepts enable only inside that prepared
transaction; it does not change Apple's private enabled flags. Selector 36
remains a negative, no-radio activation preflight.

The timer maintains a fixed-channel local timeline, emits native availability
window events, and submits driver-built synchronization frames. Matching owned
peer RX passes through the controller's management buffer pool to the native
manager. This path precedes optional BSD/BPF delivery in 24H32; an IPv6 interface
is not required for management input. BSD data queues remain disabled.

The first implementation is explicitly limited to a root-requested 1–5 second
lease, one selected owned peer and a fixed legal channel. It uses a separate
48-frame cap, one outstanding transmission and the staged 110-TU frame period.
The original four-frame diagnostics and their 5-ms timing qualification remain
unchanged. The first four detailed TX traces and all aggregate completion
latencies are retained, including a slow cold first frame. A 100-ms outstanding
TX abort is a runtime liveness bound, not a new timing qualification threshold.

An AW event's reported counter denotes its EAW start. The event timestamp is
that GP2 boundary plus 3500 microseconds: the audited native peer calculation
subtracts 3500 before adding the counter delta times the AW period. Offline
tests independently reconstruct both native and wire-format next boundaries,
including delayed callbacks, firmware-clock wrap and counter wrap. Host timer
delivery time is never supplied as the on-air receive clock.

Radio expiry/reset immediately blocks native RX and sync readback. The retained
transaction disables the native manager, removes its timer and ends the radio
lease before releasing ownership. Controller stop uses the same cleanup. No
manager is retired or recreated. First-use activation remains once per boot
until repeat-session teardown is verified. Unsupported active configuration
changes return errors; they are not staged as if applied to the live radio.

Run only after the existing retained-session, configuration, timing and template
steps, with station Wi-Fi already off:

```sh
sudo ./awdl_native_runtime selftest
sudo ./awdl_native_runtime OWNED_PEER_MAC 5000
```

Native enable, event delivery or RX handoff is not proof of discovered peers or
AirDrop. Hardware results must report those milestones separately, followed by
bidirectional checked file transfers before declaring AirDrop usable.

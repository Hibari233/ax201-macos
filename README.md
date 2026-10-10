# ax201-macos

An AX201-focused development fork of [laobamac/itlwm](https://github.com/laobamac/itlwm),
derived from [OpenIntelWireless/itlwm](https://github.com/OpenIntelWireless/itlwm).

[简体中文](README.zh-CN.md) · [Build](docs/BUILDING.md) · [Roadmap](docs/ROADMAP.md)
· [Validation](docs/VALIDATION.md) · [Contributing](CONTRIBUTING.md)

The aim is reliable native macOS Wi-Fi on Intel AX201, including WPA3 and native
AirDrop. AirDrop is the current development priority.
**This is an experimental project. WPA3-only and AirDrop do not work yet.**
Creating an interface or seeing an AWDL frame is not an AirDrop implementation.

## Scope and current status

Development hardware: Intel AX201 on ROG STRIX B660-I GAMING WIFI, macOS Sequoia
15.8.1 (24H32), x86_64. The inherited Sequoia target is for 15.2+ / Darwin
24.2.0–24.99.99; this fork has not hardware-validated every version in that range.
An Apple Silicon Mac can cross-compile the kext but cannot load this x86_64 driver.

| Capability | Evidence and limitation |
| --- | --- |
| Native Wi-Fi menu and scanning | Observed on the development AX201 |
| WPA2-Personal, 2.4 GHz | Connected and passed bounded connectivity checks on the development machine |
| 5 GHz | Networks observed in scans; no complete connection/stability matrix for this fork |
| WPA2/WPA3 transition, optional PMF | Inherited upstream support; not independently validated here |
| WPA3-SAE-only / mandatory PMF | Unsupported; authentication and key-management work remains |
| Passive AWDL frame parsing and timing | Offline tests and earlier exclusive-monitor observations; diagnostic only |
| Passive AWDL IPv6 reception | Revision 10 received owned-peer traffic on channel 149; 108 packets of 1280 bytes passed transport checksums; diagnostic only |
| Fixed AWDL data transmission | Revision 11 obtained bounded firmware completions for 64/1280/1500-byte probes; zero valid echo replies, no bidirectional IPv6 yet |
| AWDL diagnostics while connected to Wi-Fi | Lifecycle/restoration tested; reception from a peer on the same channel still unvalidated |
| Native AWDL object lifecycle | Revision 14 retained one native object across clients and retired it; unsafe recreation is blocked |
| Native packet and queue integration | Revision 18 attached one native BSD client and initialized real netif queues; backend enable remained rejected and no queue callback ran |
| Native `awdl0` | Explicit diagnostic publication tested; disabled radio, no peer discovery or delivery |
| Bidirectional peer IPv6, AirDrop transfer | Not implemented |
| Sleep/wake, roaming, sustained throughput | Not yet qualified for release |
| Tahoe and other Intel adapters | Inherited source retained; not part of this fork's initial validation claim |

See [validation](docs/VALIDATION.md) for the exact boundary between prior hardware
observations, offline tests, compilation and work still needed.

## Build and test

Install Python 3 and Apple Command Line Tools or Xcode on macOS, then:

```sh
python3 scripts/bootstrap_sdk.py
python3 scripts/test.py
python3 scripts/build_sequoia.py --variant wifi
```

The default `wifi` build leaves the experimental AWDL user client compiled out.
Developers can explicitly build it with `--variant awdl`. Both produce a bundle
named `AirportItlwm.kext` under separate `build/wifi/` or `build/awdl/` directories.
They are alternatives and must not be loaded together.

These commands build locally; they do not install a driver or modify an EFI.
Read [BUILDING.md](docs/BUILDING.md) before hardware testing.

CI runs offline tests and compiles both variants. It does not install drivers,
test hardware, publish binary releases, or certify native AirDrop.

## Provenance and license

Upstream history and source notices are retained. The starting commit and SDK
commit are pinned in [dependencies.lock.json](dependencies.lock.json).
See [PROVENANCE.md](docs/PROVENANCE.md) and the preserved
[upstream README](docs/UPSTREAM.md) for credits and the upstream feature scope.

The driver is distributed under the GNU GPL; see [LICENSE](LICENSE) and each
file's notice. New experimental files use `GPL-2.0-or-later`. Intel firmware
retains its separate [redistribution license](iwlwifi-firmware-license).
This is an independent community project, not an Intel or Apple product.

# Building and testing

## Requirements

- macOS host with Apple Command Line Tools or Xcode (`xcrun --find clang++`).
- Python 3.9 or later, Git, and the pinned MacKernelSDK from `dependencies.lock.json`.
- An x86_64 macOS Sequoia installation and AX201 for hardware testing. Building
  on arm64 is supported; loading the kext on arm64 is not.

```sh
python3 scripts/bootstrap_sdk.py
python3 scripts/test.py
python3 scripts/build_sequoia.py --variant wifi --jobs 4
python3 scripts/build_sequoia.py --variant awdl --jobs 4
python3 scripts/build_sequoia.py --native-abi-test --out build/native-abi
python3 scripts/build_awdlctl.py
```

`test.py` also runs on Linux with Clang and its sanitizer runtime. It checks
synthetic frame boundaries, mutated inputs, clock wrapping, schedule prediction,
regulatory decisions and offline decoding using ASan/UBSan. It does not contact a
device. `--no-sanitizers` is available for hosts lacking those runtimes, but is
not equivalent to the sanitizer run.
If ASan itself cannot initialize on a host, `--sanitizers undefined` can run
UBSan alone. Report that distinction; a hung or skipped ASan run is not a pass.
Each test executable has a 60-second timeout, configurable with
`--timeout-seconds` for slower test hosts.

## Outputs

- `build/wifi/AirportItlwm.kext`: inherited Wi-Fi path, diagnostic interface disabled.
- `build/awdl/AirportItlwm.kext`: Wi-Fi plus the experimental diagnostic interface.
- `build/<variant>/build-manifest.json`: source commit, dirty state, SDK, compiler,
  variant and binary SHA-256. A dirty build is not an exact committed source build.
- `build/native-abi/obj/tests/awdl/native_abi.o`: compile-only native interface fixture.
- `build/tools/awdlctl`: x86_64 command-line client; compiling it does not run it.

The build reads source membership and module version from the Sequoia Xcode
target. Experimental sources are added only for the AWDL variant. Firmware input
is regenerated in sorted order. The SDK revision and its clean working tree are
checked. The local recipe is reproducible; identical bytes across different
compiler/Python/zlib versions are not promised.

The kext keeps the upstream bundle ID `com.zxystd.AirportItlwm` for compatibility.
`AX201SourceRevision` and `AX201BuildVariant` in its Info.plist identify this fork's
build. Signing is ad-hoc, not notarization or Apple approval. SDK updates need
separate review of headers, imports and hardware behavior.

The inherited Xcode project does not select the new experimental sources. Use
the script above for a consistent fork build. The native ABI fixture is not linked
into the kext. `tests/awdl/native_abi_test.py` is an advanced offline comparison
tool for locally held Sequoia 24H32 kernel data and method metadata; it is not part
of CI and its slot comparison does not validate runtime argument layouts.

## Hardware tests

Keep a known-good bootable EFI and a working recovery connection before replacing
a test kext. Load only one of `itlwm`/`AirportItlwm` and only one build variant.
The Sequoia bundle must be restricted to Darwin 24.2.0–24.99.99 in OpenCore.
This project intentionally has no unattended installation or reboot script.

Start with ordinary WPA2 connectivity and compare against the prior build. Record
the loaded binary hash separately: a successful build is not evidence that the
machine booted that binary. See the [validation matrix](VALIDATION.md).

The AWDL client requires administrator access and the experimental kext. Its
exclusive-monitor operations can interrupt ordinary Wi-Fi. The `linked-*` path
accepts channel `0` (the current associated channel), has a maximum 30-second
session, does not retune, and yields to normal Wi-Fi operations. Diagnostic logs
can contain peer addresses and raw frame bytes; keep them in ignored
`diagnostics/` and review/redact before sharing.

No test may bypass the driver's or firmware's regulatory channel restrictions.
Native AirDrop must eventually be tested with real bidirectional file transfers
and matching hashes, not just UI discovery.

# Contributing

Start with the [capability matrix](README.md) and [roadmap](docs/ROADMAP.md).
Keep Wi-Fi, WPA3 and AWDL changes reviewable separately. Preserve upstream notices
and explain the origin/license of imported code.

Run `python3 scripts/test.py` for protocol changes. Build both `wifi` and `awdl`
variants for shared-driver changes. Add tests for malformed frames, cancellations
and restoration failures where relevant, rather than checking only a happy path.
Offline tests must use synthetic fixtures without real device/network identities.

Hardware reports should distinguish a successful build, a verified loaded binary,
a protocol observation and an actual end-to-end function. Document negative
results. Do not describe WPA3 or AirDrop as working until their acceptance tests
pass. A native ABI declaration compiling is not proof of safe runtime arguments.

Use ignored `diagnostics/` for local captures and reports. Share only the minimum
redacted evidence needed to reproduce an issue. Do not commit credentials, EFI
backups, kernel collections, packet captures or machine-specific install helpers.

Binary releases are manual and require the hardware matrix, matching source
commit, dependency revisions, licenses, checksums, limitations and rollback
instructions. CI compilation is not release qualification.

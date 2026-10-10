# Provenance and credits

This repository preserves the history of laobamac/itlwm at
`9bc4b4d6e5f5f96d0687d42385b77f81768b9e83`, itself derived from
OpenIntelWireless/itlwm and OpenBSD network drivers. The unmodified upstream README
is kept in [UPSTREAM.md](UPSTREAM.md), including its contributors and acknowledgements.

The imported development work adds optional AWDL diagnostics, parsing, timing,
regulatory guards, restoration handling, compile-only Sequoia ABI declarations,
offline tests and diagnostic tools. These additions do not supply a native
AirDrop implementation. Changes to existing driver files carry modification
notices; the original authors' license and copyright notices remain intact.

The first public build infrastructure adds pinned SDK setup, distinct Wi-Fi and
AWDL builds, actual Git revision metadata, sorted firmware generation, portable
offline tests and CI with no automatic release publication.

## Sources and dependencies

- [laobamac/itlwm](https://github.com/laobamac/itlwm): Sequoia/Tahoe native Wi-Fi integration.
- [OpenIntelWireless/itlwm](https://github.com/OpenIntelWireless/itlwm): original macOS Intel driver project.
- [OpenBSD](https://github.com/openbsd/src): net80211 and Intel driver foundations.
- [acidanthera/MacKernelSDK](https://github.com/acidanthera/MacKernelSDK): pinned kernel build headers/library; fetched separately, not vendored here.
- [Intel Linux wireless firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/): firmware already carried by the upstream tree.
- [Wireshark AWDL dissector](https://github.com/wireshark/wireshark/blob/ad85ea3cb5af4e9665f1b3e3cda5bee94b71ab73/epan/dissectors/packet-awdl.c)
  and [Open Wireless Link protocol declarations](https://github.com/seemoo-lab/owl/blob/da255a70f221784c836d943dd3f243bc798f223b/src/frame.h):
  public wire-format references for the independently written bounded codecs.

## License boundaries

The repository retains the GNU GPL v2 license text. File-level notices determine
the applicable terms; for example the iwx implementation permits GPL v2 or later
and retains embedded upstream notices. New diagnostic files and build scripts
are marked `GPL-2.0-or-later`. Existing BSD/ISC-style notices are preserved.

Intel firmware is covered by `iwlwifi-firmware-license`, not relicensed as project
source. Keep that notice when redistributing it. No firmware bytes are patched by
this fork; the inherited build compresses the original blobs for embedding.

Apple kernel collections and binary analysis inputs remain local and are not
distributed with the project. Private ABI declarations are provisional and do
not amount to an Apple-supported driver interface.

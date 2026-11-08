# OnAir OGN plugin

A macOS plugin for [OnAir](https://github.com/adswill/OnAir) that receives classic
OGNTP v1 / OGN1 on **M1 (868.2 MHz)** and **M2 (868.4 MHz)**, checks and corrects
LDPC, reverses OGN whitening, and decodes standard packet parameters.

**Experimental 0.1.0.** The actual plugin library is tested through OnAir's
public SDK replay interface. Full installation and live reception in the OnAir
GUI remain to be verified. This is a community plugin, independent of OnAir
and Open Glider Network.

## Capabilities

- Combined M1/M2 reception or individual channels; 100 kchip/s 2-GFSK,
  Manchester IEEE, OGN sync and 26-byte codewords.
- OGN (208,160) LDPC validation, conservative one/two-bit correction and
  experimental higher-error correction.
- Eight-cycle zero-key TEA payload dewhitening after LDPC validation.
- Position, device status, information and wind reports, including OGN's
  variable-resolution scaling and unavailable-value handling.
- Local result tables, no network access and no automatic file output.

See [protocol and limits](docs/PROTOCOL.md), [architecture](docs/ARCHITECTURE.md)
and [test coverage](docs/TESTING.md). This does not decode unrelated FLARM or
ADS-L protocols, or decrypt private encrypted formats.

## Requirements

- macOS 15 or later; Apple silicon or Intel.
- OnAir 3.5.0 or later with plugin ABI 1. SDK replay validation used the 3.6.0
  interface. Compatibility with future ABI changes is not assumed.
- To build: Apple Command Line Tools, CMake 3.20+ and Python 3.

Apple silicon has been runtime-tested. The Intel slice has been compiled and
inspected but has not been run on an Intel Mac.

## Install and receive

Download an available `.onairplugin` from [Releases](https://github.com/DME-3/onair-ogn-plugin/releases),
or build it using the commands below. Creating the source repository does not
itself publish a binary release. Packages are ad-hoc signed and not notarized.

1. In OnAir, choose **View → Plugins → Install from file…**, then select the
   `.onairplugin` package.
2. Enable **OGN M-band Decoder**.
3. Under **PLUGINS**, choose **OGN M1 + M2**, **OGN M1** or **OGN M2**.
4. For a recording, import its actual IQ format, sample rate and centre.
   Set the plugin's **Recording centre MHz** to the original recording centre.
   Leave it blank for live IQ whose metadata reflects the actual tuned centre.
5. Start reception. Check the input centre, sample rate and channel offsets
   reported by the plugin. Results appear only after integrity checks pass.

The receiver accepts 400 kS/s to 20 MS/s when the selected channels fit within
the input bandwidth; 1.25 MS/s and 2 MS/s have explicit tests. OnAir converts
supported file formats to float IQ before delivering them to the plugin.
Only receiver 1 is supported by the host plugin interface.

Default sync tolerance is zero and the LDPC correction limit is two bits.
Increasing these controls can increase false candidates or miscorrections.
OGN parity is error detection, not sender authentication.

Unavailable values display as `--`. Stream time is elapsed input time; the
transmitted GNSS second does not supply a complete UTC timestamp. Tables retain
at most 2,000 rows each. The optional full-field table exposes additional
semantic values. Optional local automation events are disabled by default.

## Build, test and package

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
python3 tools/package.py --build build
```

The output is `dist/ogn-mband-0.1.0-macos-universal.onairplugin`, accompanied
by `dist/SHA256SUMS`. Each package includes complete corresponding source,
license notices and public documentation. Build folders and packages are not
committed to Git.

Tests use the published OGN reference vector and generated IQ. No private
recording is needed. The bundled receiver includes the acquisition correction
submitted in [OnAir PR #461](https://github.com/adswill/OnAir/pull/461), so a
separate OnAir rebuild is not required for that correction.

## Collaboration

[Report a bug or suggest a feature](https://github.com/DME-3/onair-ogn-plugin/issues/new/choose).
Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request. GitHub
Actions builds the universal plugin, runs synthetic SDK tests and UBSan checks,
and validates the public file tree. Use a branch for changes and include the
relevant test results in the pull request.

For work in Codex, add this repository folder as a local project.
[AGENTS.md](AGENTS.md) supplies project guidance for future chats.

## Privacy and licensing

Keep recordings and decoded real-world data outside this repository. Bug
reports should use synthetic reproductions or sanitized descriptions; do not
attach captured positions, device identifiers, registrations or private logs.
The plugin requests no permissions, makes no network calls, and does not
automatically write decoded data. Enabling automation events passes decoded
values to the user's configured OnAir rules.

GPL-3.0-or-later; the public OnAir SDK header is CC0-1.0. See [LICENSE](LICENSE)
and [NOTICE.txt](NOTICE.txt) for bundled code and protocol provenance.

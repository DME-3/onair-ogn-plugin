OGN M-band Decoder for OnAir, version 0.1.0

This is a macOS plugin for classic OGNTP v1 / OGN1 on M1 (868.2 MHz)
and M2 (868.4 MHz). It runs on OnAir's public plugin ABI version 1. The
package contains Apple silicon and Intel code, targeting macOS 15 or later.

Install and use

1. Open OnAir, then View > Plugins > Install from file..., and select the
   ogn-mband-0.1.0-macos-universal.onairplugin package.
2. Enable OGN M-band Decoder in the plugin manager.
3. Select OGN M1 + M2 under PLUGINS, or choose the individual M1/M2 mode.
4. For file replay, import the original IQ format, rate and centre correctly.
   In the plugin settings, enter the ORIGINAL recording centre in MHz in
   "Recording centre MHz". This explicitly handles recordings whose centre
   differs from the channel selected in the main receiver. Leave it blank
   for live reception using the tuned centre.
5. Start playback. Reception shows the input centre, actual sample rate and
   the relative channel offsets. Position and device-report tables appear
   only after OGN LDPC and header parity checks pass.

The combined mode needs both channels inside the recording's bandwidth.
The plugin accepts IQ rates from 400 kS/s to 20 MS/s, subject to channel
coverage; 1.25 MS/s and 2 MS/s are explicitly tested. OnAir supplies float IQ
to the plugin after reading the file, so the source recording need not be
converted. Only receiver 1 is supported by OnAir's plugin system.

Decoded information

- Position: address/type, packet second, coordinates, GNSS altitude above
  geoid (MSL), standard-pressure altitude when available, ground speed,
  track, climb, turn, DOP, GPS fix quality/dimensions, aircraft type, relay,
  stealth and emergency flags.
- Status: battery, temperature, humidity, pressure, altitude/fix/satellites,
  GPS SNR, RF power/noise, receive rate, firmware/hardware and available
  pilot-sensor fields.
- Info: checked 7-bit information fields, including registration and model.
- Wind: measurement location, altitude, wind speed/direction and climb.
- Other/custom/encrypted reports: validated header/type only. They are not
  interpreted as standard positions and encrypted data is not decrypted.

Unavailable values are shown as --. Invalid/no-fix reports do not create a
displayed coordinate. Stream s is elapsed input-stream time, not an inferred
UTC timestamp. GNSS second is only the transmitted second within a minute.
The optional "Show every decoded field" table exposes the full semantic
fields, including extra integrity information. Tables retain at most 2000
rows each, as imposed by OnAir's host. No automatic file export is enabled.

Reception and integrity

The bundled receive chain uses 100 kchip/s, +/-50 kHz 2-GFSK, BT 0.5,
Manchester IEEE, OGN sync and 26-byte frames. It includes the acquisition
fix submitted in OnAir PR #461, so that PR need not be installed in OnAir.

The earlier raw Mode Builder test generator does not create authentic OGN
parity/payload encoding. Such a framing-only test file is expected to be
rejected here. This project's tests generate properly encoded OGN packets.

The plugin checks OGN's 48 LDPC equations on the still-whitened codeword,
optionally corrects bit errors, verifies header even parity, and then applies
OGN's eight-cycle zero-key TEA inverse to the two payload blocks. Field
decoding uses explicit little-endian words and the OGN1 variable-resolution
rules. DOP includes its 1.0 offset. Heading uses the reference rounding.

The default correction limit is 2, with ambiguous one/two-error syndromes
rejected. Limits above 2 enable experimental hard-input min-sum correction.
The receiver currently supplies hard bits, not soft reliabilities. Beyond
the uniquely identifiable error patterns, an LDPC-valid codeword can still
be a miscorrection; passing parity is not authentication. The table reports
the number of changed bits. Zero disables correction and accepts only
already-valid codewords. Sync error tolerance defaults to zero separately.

Privacy

The manifest requests no permissions. The plugin makes no network calls,
opens no recording files itself and automatically writes no decoded data.
Decoded results stay in OnAir's local, bounded result tables. Optional local
automation events are off by default. Enabling them hands decoded fields
to any OnAir automation rules the user has configured.

Building and tests

Requirements: Apple Command Line Tools and CMake. This plugin does not need
a complete OnAir application build or Rust. From this source directory:

  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'
  cmake --build build -j 4
  ctest --test-dir build --output-on-failure
  python3 tools/package.py --build build

The protocol/IQ tests use a public reference codeword and synthetic signals,
not a private recording. The replay test host dynamically loads the actual
plugin through the SDK, without linking in its decoder. It supports ci16
and cf32 files, and writes decoded JSON only when explicitly given --output.
See its usage by running build/ogn_replay with no arguments.

The installable package includes source.tar.gz with complete corresponding
source. It is an ad-hoc-signed build, not an Apple-notarized build.
Read NOTICE.txt and LICENSE for provenance and licensing.

Project source and collaboration:
https://github.com/DME-3/onair-ogn-plugin
See README.md and docs/ in the embedded source for contribution, testing
and release guidance.

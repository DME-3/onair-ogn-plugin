# Architecture

`src/plugin.cpp` implements `onair_plugin_entry` and exposes OnAir ABI 1 mode
and setting callbacks. The host supplies float IQ; the plugin neither opens
recordings nor uses private OnAir application symbols.

`src/receiver.cpp` creates one bundled Mode Builder receiver per selected
channel, mixes the input to the channel offset, and collects decoded sync/frame
candidates. The original recording centre can override host tuning metadata.
Both channels are processed from the same input stream.

`src/ldpc.cpp` validates the 48 parity equations of the 208-bit codeword. It
uses exact syndrome matching for conservative one/two-bit correction, rejecting
ambiguous matches. Larger configured limits use bounded hard-input normalized
min-sum recovery. The parity encoder solves the parity submatrix over GF(2)
and is used to generate synthetic tests.

`src/protocol.cpp` checks header parity, skips custom/encrypted payload
interpretation, reverses the two TEA blocks, then dispatches standard report
types. `src/ogn.hpp` defines the small internal packet/field interface.
Unavailable fields are absent rather than fabricated as zero.

The plugin formats data into bounded host tables. Nullable numeric columns
use text because OnAir coerces cells according to the column schema; this
preserves `--` for missing values. Optional automation events are off by default.

`vendor/onair/` is a GPL-3.0-or-later subset pinned in NOTICE.txt, including a
documented acquisition correction. `sdk/onair_plugin.h` is the separate CC0
public ABI. No full OnAir source checkout is needed to build the plugin.

`tests/replay_host.cpp` dynamically loads the actual shared library and mimics
host column coercion. It does not link decoder code. Public protocol and
synthetic IQ checks live in `tests/test_ogn.cpp`. The packager includes the
complete corresponding source, using an allowlist to exclude build products
and private data.

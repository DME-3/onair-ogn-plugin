# Testing

Use the build commands in README.md, then `ctest --test-dir build
--output-on-failure`. Tests need no private files or network access.

## Coverage

The protocol executable checks the public OGN wiki codeword, independent TEA
plaintext words, systematic parity encoding, all single-bit locations,
deterministic two-bit cases, ambiguous-syndrome rejection, signed coordinates,
variable-resolution boundaries, missing values, header checks and report types.

Synthetic receiver tests cover normal/inverted polarity, rate and frequency
offset variations, different chunk boundaries, and encoded frames. The SDK
replay tests dynamically load the built plugin and cover both channels, M1,
M2 with centre override, and missing nullable fields. The test host reproduces
OnAir's column-type coercion.

CTest currently registers eight cases. Local results for public tests are
recorded in TEST-REPORT.txt. Private capture results are intentionally absent
from this repository and are never needed for CI.

## UndefinedBehaviorSanitizer

```sh
cmake -S . -B build-ubsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DOGN_SANITIZE=ON -DOGN_SANITIZERS=undefined
cmake --build build-ubsan --parallel 4
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-ubsan --output-on-failure
```

AddressSanitizer is available through `-DOGN_SANITIZERS=address,undefined`.
Do not claim a successful sanitizer run unless it completes. CI uses UBSan.

## Package checks

`python3 tools/package.py --build build` creates a universal package, verifies
its ad-hoc signature and embeds complete corresponding source. Inspect its
file list and SHA256SUMS before distributing it. Release instructions are in
RELEASING.md.

Run `python3 tools/check_public_tree.py` on tracked working files, or add
`--staged` to check the index about to be committed. It rejects unexpected
paths/extensions, likely local home paths and common credential formats. It
does not establish that arbitrary logs or human-written reports are sanitized;
review those manually. Never use production captures as shared test fixtures.

## What these tests do not prove

Headless ABI replay is separate from installation and live reception in the
OnAir GUI. Compilation of both architectures is separate from Intel runtime
testing. Record each of those outcomes explicitly when testing a release.

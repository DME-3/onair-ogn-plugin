# Working on this plugin

This repository builds an independent macOS mode plugin for OnAir's public C ABI.
Keep changes scoped to this folder. Do not initialize Git in a parent folder.

## Privacy

- Never add recordings, decoded real-world packets, identifiers, coordinates,
  local paths, private validation reports or credentials to commits, issues,
  screenshots, CI logs or releases.
- Use the public OGN wiki vector and synthetic IQ for shared tests. Private
  recordings and reports must remain outside this repository; do not copy them
  into a build folder or CI artifact either.
- Before committing, review `git diff --cached` and run
  `python3 tools/check_public_tree.py --staged`. The scanner is a guard against
  common mistakes and does not replace reviewing the actual files.

## Build and validation

See README.md for the universal macOS build and docs/TESTING.md for sanitizer
checks. Run the eight CTest cases for decoder, receiver or ABI changes. Tests
must dynamically load the actual plugin as well as exercise protocol logic.
For documentation-only edits, check commands and links; do not add artificial
tests that repeat implementation details.

Keep ABI version 1 compatibility and use only sdk/onair_plugin.h. Preserve
missing values as unavailable, not zero. LDPC must run on the whitened codeword,
then header parity, then payload dewhitening and field parsing. See
docs/PROTOCOL.md before changing bit layouts or units.

## Contributions and packaging

- Follow CONTRIBUTING.md. Explain changed behavior and relevant test evidence.
- New code uses GPL-3.0-or-later SPDX notices. Retain third-party copyright
  notices and update NOTICE.txt for vendor changes. The SDK header is CC0.
- Do not modify vendored OnAir code without recording the upstream revision
  and explaining the local difference in NOTICE.txt.
- Build products stay ignored. tools/package.py must include complete
  corresponding source and notices without personal filesystem metadata.
- Publishing a release, inviting collaborators or changing repository access
  requires an instruction covering that action. Normal local edits and tests
  need no additional confirmation.
- Describe headless SDK replay and full OnAir GUI verification separately.
  Do not claim Intel runtime, notarization or successful tests without evidence.

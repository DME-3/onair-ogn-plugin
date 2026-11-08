# Contributing

Issues and pull requests are welcome. For a substantial feature or protocol
change, open an issue describing the expected behavior first.

## Development

1. Fork the repository, clone your fork and create a focused branch.
2. Build and run the CTest suite as shown in README.md.
3. Add a meaningful synthetic regression when behavior changes. Use the public
   wiki vector to anchor protocol tests independently of our encoder.
4. For low-level decoder changes, also run UBSan (docs/TESTING.md).
5. Review the full diff and run `python3 tools/check_public_tree.py --staged`
   after staging your changes.
6. Open a pull request explaining the problem, resulting behavior, validation
   and any remaining limitations. Link the relevant issue.

Do not include recordings, decoded real-world packets, device identities,
positions, registrations, credentials or personal filesystem paths. Describe
signal format, rate, relative offset and symptoms without identifying a
transmitter. Share generated reproductions wherever possible. Keep private
test results outside the repository and never upload them as CI artifacts.

## Code and review

Use C++20 and the existing style of each file. Keep parsing explicit about
bit positions, signedness, endianness, units and unavailable sentinels. Preserve
the OnAir ABI boundary. Check integrity before interpreting a payload. Do not
add network access, automatic capture output or telemetry implicitly.

Keep PRs focused. Do not reformat vendored code or mix unrelated fixes into a
feature. If changing vendored files, document the upstream revision and local
patch in NOTICE.txt. New original code uses GPL-3.0-or-later; contributions are
provided under that license. Preserve the separate CC0 SDK notice. Disclose
material AI assistance in the PR and review generated code as your own.

The macOS build and test and macOS UBSan checks should pass before merge.
Maintainers review changes through pull requests; the repository does not
grant write access merely by accepting a contribution.

## Releases

See docs/RELEASING.md. Release artifacts must be built from the tagged source
and include complete corresponding source and licensing. Do not describe an
ad-hoc signature as Apple notarization or claim untested Intel runtime support.

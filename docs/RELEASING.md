# Preparing a release

1. Review outstanding issues and known limitations. Update the version in
   CMakeLists.txt, manifest.json, README files, NOTICE.txt, TEST-REPORT.txt and
   CHANGELOG.md consistently.
2. Build the universal Release plugin from a clean checkout; run all CTest and
   UBSan checks. Perform an OnAir GUI installation/reception check when possible.
   Report untested platforms honestly.
3. Review the exact tracked files, licensing notices and public tree check.
   Build using `python3 tools/package.py --build build`.
4. Inspect the package and embedded source. Ensure no recording, private report,
   absolute home path, credential, decoded identifier or location from a real
   capture is included. Verify `codesign --verify --all-architectures --strict`
   on the staged library and inspect both slices using `lipo -info`.
5. Commit and tag the reviewed source as `vVERSION`. Publish the GitHub release
   with the matching `.onairplugin` and SHA256SUMS, plus a concise changelog,
   installation instructions and any validation limitations.

The package embeds complete corresponding source for the distributed GPL
binary. Keep the release tied to that source revision. CI artifacts are
temporary build outputs and are not automatically published releases.
The normal CI workflow has read-only repository permissions.

Ad-hoc signing is local code signing, not developer identity verification
or Apple notarization. Do not remove operating-system protections as a default
installation instruction. A future notarized release needs a separate signing
and notarization process managed by the maintainer.

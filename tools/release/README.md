# CTR-AP release preparation

`assemble-release.py` combines the exact AP artifacts from the native `Build
clients` workflow with a packed `ctr.apworld` and a generated player template.
It accepts a native checkout, the downloaded artifacts of that run (every
target's archive and debug sidecar, each CI artifact in its own folder, as `gh
run download` leaves them), the apworld, template, pair version, and output
directory explicitly:

```sh
python3 tools/release/assemble-release.py \
  --native-source /path/to/native-checkout \
  --artifacts /path/to/native-artifacts \
  --apworld /path/to/ctr.apworld \
  --template /path/to/Crash.Team.Racing.yaml \
  --version 0.2.0-rc-prep1 \
  --output-dir /path/to/release
```

The command refuses a source/version mismatch, a mismatched `BUILD.json`,
missing or tampered sidecars, unsafe or unexpected archive members, an
apworld/template identity mismatch, and an existing output asset. It repeats
the platform/debuglink checks from `tools/ci/package-client.py` and emits the
nineteen names required by `RELEASING.md`: for each target in
`release_policy.TARGETS` (`windows-x86`, `linux-x86_64`, `linux-arm64`,
`macos-arm64`) the client archive and its debug sidecar, each with a
`.sha256`, plus the apworld, its `.sha256` and the template. With
`--solo-runtimes DIR` it puts `DIR/<target>/` (`tools/solo/build-runtime.py
--platform <target>`) into each client archive as `solo-runtime/`. The macOS
archive holds the release folder inside `CTR Decrypted.app`
(`Contents/Resources/game/`, `docs/MACOS.md`): the assembler keeps the CI app's
launcher, `Info.plist` and icon, puts the release folder in and seals the app
with Apple's `codesign`, so it runs on macOS (`ci-assemble.sh`), with GNU
binutils on `PATH` for the Windows and Linux checks. The archives contain the stripped
client, apworld, versions file, setup/license/notices, extraction helper,
platform support helpers, and the `assets/` tree that `package-client.py`
placed in the build archive. Debug sidecars are standalone assets and
extracted retail game assets are never read or included.

With `--authoring`, the assembler also repackages the separate box authoring
archives (`ctr-decrypted-box-authoring-<target>-<commit>`) as
`ctr-decrypted-vX.Y.Z-box-authoring-<target>` downloads with their
`.sha256` sidecars. They are optional release assets: not one of the nineteen, not
in the manifest, and the verifier accepts a release with or without them but
checks each archive against its sidecar when present.

After the standard assets pass their completeness check, the assembler
writes `manifest.json` from the final archive bytes. It covers exactly the four
client archives and refuses to overwrite an existing manifest. The shared
policy in `release_policy.py` owns the standard asset list, the signed manifest
contract, strict duplicate-rejecting JSON loading and the checksum-sidecar
checks, and both the assembler and `tools/verify-release.py` import it.

`tools/verify-release.py` verifies a release against the signed manifest.
Default mode fails closed: it requires the exact published file set (nineteen
standard assets plus `manifest.json` and `manifest.json.minisig`), a
non-placeholder committed public key and minisign, verifies the signature over
the exact manifest bytes before parsing, binds the manifest version to a
required `--version`, checks the four archive hashes and validates every
standard `.sha256` sidecar:

```sh
python3 tools/verify-release.py /path/to/downloaded-release --version vX.Y.Z
```

`--pre-sign` is a distinct mode for a complete twenty-file pre-sign assembly
(the nineteen standard assets plus `manifest.json`). It omits signature
verification and states unmistakably that the result is not a signed release:

```sh
python3 tools/verify-release.py /path/to/assembly --version vX.Y.Z --pre-sign
```

`.github/workflows/release-prepare.yml` is a manual, read-only preparation
workflow, on a macOS runner. It requires exact successful native and companion run IDs plus their
full source commits, downloads both hosted build outputs with `GH_TOKEN`,
generates the template from Archipelago 0.6.8 and the downloaded apworld,
assembles the nineteen standard assets and the unsigned manifest, verifies that
assembly with `--pre-sign`, and uploads the twenty files as one workflow
artifact. It deliberately stops before signing, tagging or creating a GitHub
release. The owner signs the exact downloaded `manifest.json` outside CI, then
a clean download of the published release must pass default-mode verification
with the committed public key.

Release rehearsal can test paired changes before merging. On a native PR,
an apworld branch with the same head branch name is preferred when present;
it must have a successful manual or push CTR world run at its current head
with an unexpired `ctr.apworld` artifact. A missing companion branch uses the
normal base branch. An inaccessible branch or stale companion run fails the
rehearsal. Manual rehearsal also accepts an explicit `apworld_branch` input;
that branch must exist and have a successful current-head run and artifact.

Build archive members may be nested. Anything directly under the archive root
must be one of the known package-client outputs, and the only permitted
subtree is `assets/`, which is copied into both bundles with its relative
paths intact. The build-only `BUILD-NOTICE.txt` is dropped and never ships.
The two platform archives must agree on the asset tree byte for byte. In the
tarball the Linux client and `support-bundle.sh` keep mode 755 and every other
regular file is 644.

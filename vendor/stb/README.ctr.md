# Pinned JPEG decoder

Unmodified `stb_image.h` 2.30 (public domain or MIT, see LICENSE).
Upstream: https://github.com/nothings/stb
Commit: `f58f558c120e9b32c217290b80bad1a0729fbb2c`.

Used only by the Mods screen's track pictures (`platform/native_track_picture.c`),
in every build with the package library. It is built for JPEG only
(`STBI_ONLY_JPEG`, no stdio, no HDR), private to that file. SDL carries its own
copy, changed to SDL's types, which does not build outside SDL.

`cmake/CustomPackageDeps.cmake` refuses to configure a package build when a
file here differs from `SHA256SUMS`. Updating the decoder needs its own review,
not a silent refresh inside an unrelated change.

SHA-256 pins for the unmodified files:

| File | SHA-256 |
|---|---|
| LICENSE | bebfe904b14301657e4e5d655c811d51fd31b97c455b9cc2d8600d6bac6cff63 |
| stb_image.h | 594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3 |

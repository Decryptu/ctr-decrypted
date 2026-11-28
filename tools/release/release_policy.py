#!/usr/bin/env python3
"""Shared release asset policy for the assembler and the verifier.

Single source of truth for the standard asset set, the signed client manifest
contract, version validation, and the strict readers both tools need. Keep
assembler and verifier importing these definitions rather than maintaining
parallel asset lists.
"""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path


# The build targets a release carries a client archive for, in the updater's
# order (ap/ap_update_core.h, Target). Windows ships the 32-bit x86 build, Linux
# the 64-bit builds.
TARGETS = ("windows-x86", "linux-x86_64", "linux-arm64")


def target_os(target: str) -> str:
    """"windows" or "linux": the archive format and executable name."""
    return "windows" if target.startswith("windows-") else "linux"


def archive_suffix(target: str) -> str:
    return ".zip" if target_os(target) == "windows" else ".tar.gz"


def client_asset_template(target: str) -> str:
    return "ctr-decrypted-{version}-" + target + archive_suffix(target)


def debug_asset_name(target: str) -> str:
    """The player client's debug sidecar as a release asset. The Linux ones
    carry their target, since both are named ctr_native_ap.debug by the build
    (and inside each executable's .gnu_debuglink)."""
    return "ctr_native_ap.exe.debug" if target_os(target) == "windows" else f"ctr_native_ap-{target}.debug"


STANDARD_ASSET_NAMES = (
    *(name for target in TARGETS for name in (
        client_asset_template(target),
        client_asset_template(target) + ".sha256",
        debug_asset_name(target),
        debug_asset_name(target) + ".sha256",
    )),
    "ctr.apworld",
    "ctr.apworld.sha256",
    "Crash.Team.Racing.yaml",
)

# The separate box authoring download (a CTR_AP_AUTHORING client that never
# connects to a room), one archive per target. Optional: a release may carry it
# or not, it is not one of the standard assets and the signed manifest never
# covers it. When an archive is present its .sha256 sidecar must be present and
# correct.
AUTHORING_ASSET_NAMES = {
    target: "ctr-decrypted-{version}-box-authoring-" + target + archive_suffix(target) for target in TARGETS
}

SIGNED_ASSET_NAMES = tuple(client_asset_template(target) for target in TARGETS)

MANIFEST_NAME = "manifest.json"
SIGNATURE_NAME = "manifest.json.minisig"
SIGNATURE_TEMPORARY_NAME = "manifest.json.minisig.tmp"
PUBLIC_KEY_NAME = "ctr-release-minisign.pub"
PLACEHOLDER_PREFIX = "PLACEHOLDER:"

VERSION = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$")
TAGGED_VERSION = re.compile(r"^v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")


class PolicyError(ValueError):
    """A release input failed a trust or policy gate."""


def fail(message: str) -> None:
    raise PolicyError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def standard_asset_names(version: str) -> list[str]:
    """The standard release assets, with the tag-style version filled in."""
    tag = version if version.startswith("v") else f"v{version}"
    return [name.format(version=tag) for name in STANDARD_ASSET_NAMES]


def client_asset_name(version: str, target: str) -> str:
    tag = version if version.startswith("v") else f"v{version}"
    return client_asset_template(target).format(version=tag)


def authoring_asset_name(version: str, target: str) -> str:
    tag = version if version.startswith("v") else f"v{version}"
    return AUTHORING_ASSET_NAMES[target].format(version=tag)


def authoring_asset_names(version: str) -> list[str]:
    """The optional box authoring archives and their checksum sidecars."""
    names = []
    for target in TARGETS:
        archive = authoring_asset_name(version, target)
        names += [archive, archive + ".sha256"]
    return names


def verify_authoring_sidecars(release: Path, version: str) -> None:
    """An optional authoring archive, if present, needs its own valid sidecar."""
    for target in TARGETS:
        archive = authoring_asset_name(version, target)
        sidecar = archive + ".sha256"
        has_archive = (release / archive).exists()
        has_sidecar = (release / sidecar).exists()
        if has_archive != has_sidecar:
            fail(f"box authoring download needs both {archive} and {sidecar}")
        if not has_archive:
            continue
        require_regular_file(release / archive, "box authoring archive")
        require_regular_file(release / sidecar, "box authoring checksum sidecar")
        lines = (release / sidecar).read_text(encoding="utf-8").splitlines()
        fields = lines[0].split() if len(lines) == 1 else []
        if len(fields) != 2 or fields[1] != archive or not SHA256.fullmatch(fields[0]):
            fail(f"malformed checksum sidecar: {sidecar}")
        actual = sha256(release / archive)
        if fields[0] != actual:
            fail(f"checksum mismatch for {archive}: sidecar has {fields[0]}, actual is {actual}")


def signed_asset_names(version: str) -> list[str]:
    """The exact client archives the signed manifest covers, one per target."""
    tag = version if version.startswith("v") else f"v{version}"
    return sorted(name.format(version=tag) for name in SIGNED_ASSET_NAMES)


def prepared_release_files(version: str) -> set[str]:
    """Permitted files in a pre-sign assembly: the standard assets plus manifest."""
    return {*standard_asset_names(version), MANIFEST_NAME}


def published_release_files(version: str) -> set[str]:
    """Permitted files in a published signed release: the above plus one signature."""
    return {*prepared_release_files(version), SIGNATURE_NAME}


def require_regular_file(path: Path, label: str) -> None:
    """Reject a missing path, a symlink, or any non-regular filesystem entry."""
    if path.is_symlink():
        fail(f"{label} is a symlink, not a regular file: {path}")
    if not path.is_file():
        fail(f"missing or non-regular {label}: {path}")


def _reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict:
    result: dict = {}
    for key, value in pairs:
        if key in result:
            fail(f"duplicate JSON key: {key!r}")
        result[key] = value
    return result


def _reject_nonstandard_constant(token: str):
    fail(f"non-standard JSON constant: {token}")


def loads_strict(text: str) -> object:
    """Parse JSON, rejecting duplicate keys and non-standard constants."""
    try:
        return json.loads(
            text,
            object_pairs_hook=_reject_duplicate_keys,
            parse_constant=_reject_nonstandard_constant,
        )
    except json.JSONDecodeError as error:
        fail(f"invalid JSON: {error}")


def load_manifest_bytes(data: bytes) -> tuple[str, dict[str, str]]:
    """Parse manifest bytes with the strict reader (the signature covers these bytes)."""
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        fail(f"manifest is not UTF-8: {error}")
    manifest = loads_strict(text)
    if not isinstance(manifest, dict):
        fail("manifest must be a JSON object")
    if set(manifest) != {"version", "artifacts"}:
        fail("manifest must contain exactly 'version' and 'artifacts'")
    version = manifest["version"]
    artifacts = manifest["artifacts"]
    if not isinstance(version, str) or not TAGGED_VERSION.fullmatch(version):
        fail(f"invalid manifest version: {version!r}")
    if not isinstance(artifacts, dict):
        fail("manifest artifacts must be an object")
    expected_names = set(signed_asset_names(version))
    actual_names = set(artifacts)
    missing = sorted(expected_names - actual_names)
    extra = sorted(actual_names - expected_names)
    if missing or extra:
        details = []
        if missing:
            details.append(f"missing artifact entries: {', '.join(missing)}")
        if extra:
            details.append(f"extra artifact entries: {', '.join(extra)}")
        fail("; ".join(details))
    for name, digest in artifacts.items():
        if Path(name).name != name:
            fail(f"artifact name is not a plain filename: {name!r}")
        if not isinstance(digest, str) or not SHA256.fullmatch(digest):
            fail(f"invalid SHA-256 for {name}: {digest!r}")
    return version, artifacts


def verify_standard_sidecars(release: Path, version: str) -> None:
    """Validate every existing standard ``.sha256`` sidecar against its asset."""
    for name in standard_asset_names(version):
        if not name.endswith(".sha256"):
            continue
        sidecar = release / name
        require_regular_file(sidecar, "standard checksum sidecar")
        lines = sidecar.read_text(encoding="utf-8").splitlines()
        if len(lines) != 1:
            fail(f"checksum sidecar must contain one line: {name}")
        fields = lines[0].split()
        target_name = name[: -len(".sha256")]
        if (
            len(fields) != 2
            or fields[1] != target_name
            or not SHA256.fullmatch(fields[0])
        ):
            fail(f"malformed checksum sidecar: {name}")
        target = release / target_name
        require_regular_file(target, "standard asset")
        actual = sha256(target)
        if fields[0] != actual:
            fail(
                f"checksum mismatch for {target_name}: "
                f"sidecar has {fields[0]}, actual is {actual}"
            )

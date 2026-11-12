"""Install Penta's English PAL race voices from a raw European CTR BIN."""

import argparse
from contextlib import closing
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "racers"))
from ctrdisc import Disc, RAW_SECTOR


def extract(disc_path, assets):
    with closing(Disc(disc_path)) as disc:
        if "SCES_021.05" not in disc.files or disc_path.stat().st_size % RAW_SECTOR:
            raise ValueError("Expected a raw MODE2/2352 European CTR disc (SCES-02105)")

        manifest = disc.file("XA/ENG.XNF")
        if struct.unpack_from("<4sII", manifest) != (b"XINF", 102, 3):
            raise ValueError("Unexpected English XA manifest format")
        file_count, track_count = struct.unpack_from("<II", manifest, 12)
        game_count = struct.unpack_from("<I", manifest, 0x34)[0]
        game_first = struct.unpack_from("<I", manifest, 0x40)[0]
        entries = 0x44 + 4 * file_count
        if game_count < 224 or game_first + game_count > track_count or entries + 4 * track_count > len(manifest):
            raise ValueError("English XA manifest is missing Penta's track range")

        # The PAL executable's Penta voice table uses English game tracks 208–223.
        # Read each shared XA source only as far as the last Penta clip needs it.
        tracks = [struct.unpack_from("<BBh", manifest, entries + 4 * (game_first + track))
                  for track in range(208, 224)]
        sources = {}
        for file_number in sorted({file_number for _, file_number, _ in tracks}):
            name = f"S{file_number:02d}.XA"
            lba, _ = disc.files[f"XA/ENG/GAME/{name}"]
            count = max(count for _, source, count in tracks if source == file_number)
            if count <= 0:
                raise ValueError(f"Invalid sector count for {name}")
            disc.f.seek(lba * RAW_SECTOR)
            raw = disc.f.read(count * RAW_SECTOR)
            if len(raw) != count * RAW_SECTOR:
                raise ValueError(f"Truncated XA source {name}")
            sources[file_number] = raw

        for channel, file_number, count in tracks:
            raw = sources[file_number]
            if count <= 0 or not any(
                raw[offset] == 1 and raw[offset + 1] == channel
                and raw[offset + 2] & 0x04 and not raw[offset + 3] & 0x30
                for offset in range(16, count * RAW_SECTOR, RAW_SECTOR)
            ):
                raise ValueError(f"No playable XA sectors for S{file_number:02d}, channel {channel}")

    destination = assets / "XA" / "PENTA-PAL"
    destination.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=destination) as staging:
        staging = Path(staging)
        for file_number, raw in sources.items():
            (staging / f"S{file_number:02d}.XA").write_bytes(raw)
        (staging / "ENG.XNF").write_bytes(manifest)
        for file_number in sources:
            name = f"S{file_number:02d}.XA"
            (staging / name).replace(destination / name)
        (staging / "ENG.XNF").replace(destination / "ENG.XNF")
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pal_bin", type=Path)
    parser.add_argument("assets", type=Path, help="assets directory beside the client executable")
    args = parser.parse_args()
    try:
        destination = extract(args.pal_bin, args.assets)
    except (OSError, KeyError, ValueError, struct.error) as exc:
        parser.exit(1, f"Penta voice extraction failed: {exc}\n")
    print(f"Installed 16 PAL Penta race clips in {destination}")


if __name__ == "__main__":
    main()

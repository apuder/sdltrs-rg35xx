#!/usr/bin/env python3
"""Build an LDOS 5.3.1 data hard disk using only Python's standard library."""

import argparse
import csv
import gzip
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
TPK_ROOT = ROOT / "tpk"
DISKIMAGE_ROOT = ROOT / "diskimages"
TEMPLATE_EMPTY_HDV = DISKIMAGE_ROOT / "empty.hdv.gz"
TEMPLATE_BOOT_DSK = DISKIMAGE_ROOT / "ldos531-model3-boot.dsk.gz"
SECTOR = 256
SECTORS_PER_CYLINDER = 256
CYLINDERS = 202
GRANULE = 32 * SECTOR
DIRECTORY = SECTOR + SECTOR * SECTORS_PER_CYLINDER
IMAGE_SIZE = SECTOR + CYLINDERS * SECTORS_PER_CYLINDER * SECTOR
MAX_MAPPING_BYTES = 4095  # B000h..BFFFh holds this payload plus its final NUL.

# LDOS has a flat 8.3 namespace. Keep the two pairs of games distinguishable.
ALIASES = {
    "defense_command/DEFENSE.CMD": "DEFCOM.CMD",
    "missile_defense/DEFENSE.CMD": "MISSDEF.CMD",
    "space_invaders_model1/SPACEINV.CMD": "SPACEIN1.CMD",
    "space_invaders_model3/SPACEINV.CMD": "SPACEIN3.CMD",
}

# Control tokens are independent of a frontend's SDL/Linux button numbering.
CONTROL_TOKENS = {name: name for name in
                  ("A", "B", "X", "Y", "L1", "L2", "R1", "R2", "START", "SELECT")}
for prefix, label in (("D", "D-pad"), ("LS", "Left stick"), ("RS", "Right stick")):
    for direction in ("Up", "Down", "Left", "Right"):
        token = f"{prefix}_{direction[0]}"
        CONTROL_TOKENS[f"{label} {direction}"] = token
        CONTROL_TOKENS[token] = token
KEY_TOKENS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@") | {
    "ENTER", "CLEAR", "BREAK", "SPACE", "UP", "DOWN", "LEFT", "RIGHT",
    "COMMA", "PERIOD", "SEMICOLON", "SHIFT",
}


def encode_controls(actions):
    """Encode manual action-map cells as CONTROL:KEY[,CONTROL:KEY...]."""
    bindings = []
    used = set()
    for keys, control in actions.items():
        control = control.strip()
        if not control or control == "x":
            continue
        key_parts = keys.split("+")
        if any(key not in KEY_TOKENS for key in key_parts) or len(set(key_parts)) != len(key_parts):
            raise ValueError(f"Unknown or repeated physical TRS-80 key: {keys}")
        try:
            parts = [CONTROL_TOKENS[c.strip()] for c in control.split("+")]
        except KeyError as error:
            raise ValueError(f"Unknown RG35XXPRO control for {keys}: {control}") from error
        trigger = frozenset(parts)
        if len(trigger) != len(parts) or trigger in used:
            raise ValueError(f"Repeated RG35XXPRO control/chord: {control}; use one key-chord column")
        used.add(trigger)
        bindings.append(f"{'+'.join(parts)}:{keys}")
    return ",".join(bindings)


def load_control_mappings(path=None):
    path = path if path is not None else HERE / "action-map.csv"
    with path.open(newline="", encoding="ascii") as f:
        reader = csv.reader(f)
        header = next(reader, [])
        if not header or header[0] != "game" or len(set(header)) != len(header):
            raise ValueError(f"{path}: invalid action-map header")
        result = {}
        for row in reader:
            if len(row) != len(header) or not row[0] or row[0] in result:
                raise ValueError(f"{path}:{reader.line_num}: invalid or duplicate game row")
            try:
                result[row[0]] = encode_controls(dict(zip(header[1:], row[1:])))
            except ValueError as error:
                raise ValueError(f"{path}:{reader.line_num}: {row[0]}: {error}") from error
    return result


def source_files():
    """Find game sources in the tpk checkout and require sibling metadata."""
    if not TPK_ROOT.is_dir():
        raise ValueError(f"Missing game source directory: {TPK_ROOT}")
    paths = {
        path.relative_to(ROOT)
        for path in TPK_ROOT.rglob("*")
        if (path.is_file()
            and path.suffix.lower() in {".cmd", ".bas"}
            and (path.parent / "info.properties").is_file())
    }
    return sorted(paths)


def dos_name(path, used):
    alias = ALIASES.get("/".join(path.parts[-2:]))
    original = Path(alias) if alias else path
    stem = re.sub("[^A-Z0-9]", "", original.stem.upper())
    if not stem or not stem[0].isalpha():
        stem = "G" + stem
    ext = original.suffix[1:].upper()
    candidate = stem[:8]
    sequence = 1
    while f"{candidate}.{ext}" in used:
        sequence += 1
        suffix = str(sequence)
        if len(suffix) >= 8:
            raise ValueError("Too many filename collisions")
        candidate = stem[:8 - len(suffix)] + suffix
    name = f"{candidate}.{ext}"
    used.add(name)
    return name


def name_hash(name):
    value = 0
    for char in name:
        value ^= char
        value = ((value << 1) | (value >> 7)) & 255
    return value or 1


def game_title(path):
    properties = ROOT / path.parent / "info.properties"
    for line in properties.read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if separator and key.strip() == "app.name" and value.strip():
            return value.strip()
    raise ValueError(f"{properties}: missing app.name")


def game_list(games):
    width = max(len("Game"), *(len(title) for title, _, _ in games))
    lines = [f"{'Game':<{width}}  LDOS filename\tRG35XXPRO controls", "-" * (width + 15)]
    for title, name, mapping in sorted(games, key=lambda game: game[0].casefold()):
        prefix = f"{title:<{width}}  {name}"
        if any(c in title for c in "\t\r\n") or len(prefix) > 127:
            raise ValueError(f"Title/filename exceeds launcher format: {title!r}")
        if len(mapping.encode("ascii")) > MAX_MAPPING_BYTES:
            raise ValueError(f"Control mapping exceeds {MAX_MAPPING_BYTES} bytes: {title!r}")
        lines.append(f"{prefix}\t{mapping}")
    # Native TRS-80 text uses carriage returns, without line feeds.
    return ("\r".join(lines) + "\r").encode("ascii")


def assemble_launcher(assembler):
    """Assemble with Pasmo, then encode the binary as TRS-80 /CMD records."""
    with tempfile.TemporaryDirectory(prefix="tpk-launch-") as directory:
        binary = Path(directory) / "LAUNCH.BIN"
        try:
            subprocess.run([assembler, "--bin", str(HERE / "LAUNCH.ASM"), str(binary)],
                           check=True)
        except FileNotFoundError as error:
            raise SystemExit("Pasmo is required: install pasmo or use --assembler /path/to/pasmo") from error
        code = binary.read_bytes()
    origin = 0x5200
    if not code or origin + len(code) > 0x7000:
        raise ValueError("Launcher code overlaps its game table at 7000h")
    result = bytearray()
    for offset in range(0, len(code), 250):
        block = code[offset:offset + 250]
        result.extend(bytes([1, len(block) + 2]))
        result.extend((origin + offset).to_bytes(2, "little"))
        result.extend(block)
    result.extend(bytes([2, 2]) + origin.to_bytes(2, "little"))
    return bytes(result)


def entry_offset(dec):
    # Directory entry code: bits 0..4 select sector, bits 5..7 select slot.
    return DIRECTORY + (2 + (dec & 31)) * SECTOR + (dec >> 5) * 32


def empty_image():
    image = bytearray(gzip.decompress(TEMPLATE_EMPTY_HDV.read_bytes()))
    if (len(image) != 131328 or image[:3] != bytes.fromhex("56cb10")
            or image[28:32] != bytes([202, 0, 8, 1])
            or image[DIRECTORY:DIRECTORY + 2] != b"\x01\xff"):
        raise ValueError("Unexpected LDOS template or geometry")
    image.extend(bytes(IMAGE_SIZE - len(image)))
    return image


def add_file(image, name, data):
    # This writer intentionally supports only one extent per file (256 KiB).
    # Refuse unsupported sizes before writing instead of silently truncating.
    granules = (len(data) + GRANULE - 1) // GRANULE
    if granules > 32:
        raise ValueError(f"{name}: exceeds the single-extent limit of 256 KiB")
    hit = DIRECTORY + SECTOR
    dec = next((d for d in range(256) if image[hit + d] == 0), None)
    if dec is None:
        raise ValueError("LDOS directory is full (254 user entries)")

    # Cylinder 0 holds BOOT/SYS; cylinder 1 is reserved for the directory.
    run = []
    if granules:
        for g in range(16, CYLINDERS * 8):
            allocated = image[DIRECTORY + g // 8] & (1 << (g % 8))
            run = [] if allocated else run + [g]
            if len(run) == granules:
                break
        if len(run) != granules:
            raise ValueError("Hard disk is full")
        start = SECTOR + run[0] * GRANULE
        image[start:start + len(data)] = data
        for g in run:
            image[DIRECTORY + g // 8] |= 1 << (g % 8)

    stem, ext = name.split(".")
    encoded = (stem.ljust(8) + ext.ljust(3)).encode("ascii")
    entry = bytearray(32)
    entry[0] = 0x10  # Active, unrestricted file.
    entry[1:3] = bytes.fromhex("4108")  # Fixed date: 1 January 2000.
    entry[3] = len(data) % SECTOR  # EOF byte offset; zero means a full sector.
    entry[5:16] = encoded
    entry[16:18] = bytes.fromhex("9642")  # LDOS default password hash.
    entry[19] = 20  # Extended year, relative to 1980.
    entry[20:22] = ((len(data) + SECTOR - 1) // SECTOR).to_bytes(2, "little")
    entry[22:32] = b"\xff" * 10
    if run:
        entry[22] = run[0] // 8
        entry[23] = ((run[0] % 8) << 5) | (granules - 1)
    offset = entry_offset(dec)
    image[offset:offset + 32] = entry
    image[hit + dec] = name_hash(encoded)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "out/hard-disk")
    parser.add_argument("--assembler", default="pasmo", help="Pasmo executable (default: pasmo)")
    args = parser.parse_args()
    controls = load_control_mappings()
    paths = source_files()
    if not paths:
        raise ValueError("No .CMD or .BAS source files found")
    image = empty_image()
    launcher = assemble_launcher(args.assembler)
    used = {"LAUNCH.CMD"}
    rows = []
    games = []
    for path in paths:
        data = (ROOT / path).read_bytes()
        name = dos_name(path, used)
        add_file(image, name, data)
        title = game_title(path)
        if title not in controls:
            raise ValueError(f"Missing action-map.csv row for {title!r}")
        games.append((title, name.replace(".", "/"), controls[title]))
        rows.append({"dos_name": name.replace(".", "/"), "source": path.as_posix(),
                     "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})

    catalog = game_list(games)
    add_file(image, "LIST.TXT", catalog)
    rows.append({"dos_name": "LIST/TXT", "source": "LIST.TXT",
                 "bytes": len(catalog), "sha256": hashlib.sha256(catalog).hexdigest()})
    add_file(image, "LAUNCH.CMD", launcher)
    rows.append({"dos_name": "LAUNCH/CMD", "source": "LAUNCH.CMD",
                 "bytes": len(launcher), "sha256": hashlib.sha256(launcher).hexdigest()})
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "LIST.TXT").write_bytes(catalog)
    (args.output_dir / "LAUNCH.CMD").write_bytes(launcher)
    (args.output_dir / "games.hdv").write_bytes(image)
    (args.output_dir / "empty.hdv").write_bytes(empty_image())
    (args.output_dir / "ldos531-model3-boot.dsk").write_bytes(
        gzip.decompress(TEMPLATE_BOOT_DSK.read_bytes()))
    with (args.output_dir / "manifest.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["dos_name", "source", "bytes", "sha256"])
        writer.writeheader()
        writer.writerows(rows)
    print(f"Copied {len(rows)} files ({sum(r['bytes'] for r in rows):,} bytes)")
    print(f"Hard disk: {args.output_dir / 'games.hdv'}")
    print(f"Empty disk, configured boot floppy and manifest: {args.output_dir}")


if __name__ == "__main__":
    main()

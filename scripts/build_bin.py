"""
ESPVerse bin builder for PlatformIO.

Use as a PlatformIO extra script:

    extra_scripts = post:scripts/build_bin.py

It creates a merged browser-flashable .bin in firmware/ and writes
firmware/firmware.json with the flash offset, chip, and size.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from typing import Iterable


try:
    Import("env")  # type: ignore[name-defined]
except NameError:
    env = None


FIRMWARE_DIR = Path("firmware")
FIRMWARE_INDEX = FIRMWARE_DIR / "firmware.json"


def option(name: str, default: str = "") -> str:
    if env is None:
        return default

    return str(env.GetProjectOption(f"custom_espverse_{name.lower()}", default) or default).strip()


def env_name_from_source(source: Iterable[object]) -> str:
    first = str(list(source)[0])
    parts = Path(first).parts
    if ".pio" in parts:
        pio_index = parts.index(".pio")
        if len(parts) > pio_index + 2:
            return parts[pio_index + 2]
    return option("board_slug") or "firmware"


def display_name_for_env(environment: str) -> str:
    if env is not None:
        value = env.GetProjectOption(f"custom_espverse_name_{environment}", "")
        if value:
            return str(value)
    return environment.replace("_", " ").replace("-", " ").title()


def parse_flash_segments(upload_command: str) -> tuple[str, list[tuple[int, Path]]]:
    chip_match = re.search(r"--chip\s+(\S+)", upload_command)
    chip = chip_match.group(1).upper() if chip_match else option("chip_family", "ESP32").upper()
    chip = (
        chip.replace("ESP32S", "ESP32-S")
        .replace("ESP32C", "ESP32-C")
        .replace("ESP32H", "ESP32-H")
        .replace("ESP32P", "ESP32-P")
    )

    segments: list[tuple[int, Path]] = []
    for offset, path in re.findall(r"(0x[0-9a-fA-F]+)\s+([^\s]+\.bin)", upload_command):
        bin_path = Path(path.strip('"'))
        if bin_path.exists():
            segments.append((int(offset, 16), bin_path))

    return chip, segments


def merge_bins(segments: list[tuple[int, Path]], output_base: Path) -> tuple[Path, int, int]:
    if not segments:
        raise RuntimeError("No ESP32 binary segments were found in the PlatformIO upload command.")

    loaded: list[tuple[int, bytes, Path]] = []
    for offset, path in segments:
        loaded.append((offset, path.read_bytes(), path))

    start_offset = min(offset for offset, _, _ in loaded)
    end_offset = max(offset + len(data) for offset, data, _ in loaded)
    merged = bytearray([0xFF]) * (end_offset - start_offset)

    for offset, data, path in loaded:
        rel = offset - start_offset
        merged[rel : rel + len(data)] = data
        print(f"ESPVerse: segment {path} @ 0x{offset:X}, {len(data)} bytes")

    output_path = output_base.with_name(f"{output_base.stem}_0x{start_offset:X}.bin")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(merged)
    return output_path, start_offset, len(merged)


def update_firmware_index(environment: str, firmware: dict[str, object]) -> None:
    FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
    data: dict[str, object] = {}
    if FIRMWARE_INDEX.exists():
        data = json.loads(FIRMWARE_INDEX.read_text(encoding="utf-8"))
    data[environment] = firmware
    FIRMWARE_INDEX.write_text(json.dumps(data, indent=4) + "\n", encoding="utf-8")


def after_build(source, target, env) -> None:  # noqa: ANN001
    if env is None:
        raise RuntimeError("This script is intended to run as a PlatformIO extra script.")

    environment = env_name_from_source(source)
    board_slug = option("board_slug", environment)
    version = option("version", "dev")

    upload_command = env.subst("$UPLOADCMD") + f" {str(source[0])}"
    chip, segments = parse_flash_segments(upload_command)
    output_base = FIRMWARE_DIR / f"{environment}_{version}.bin"
    firmware_path, flash_offset, size = merge_bins(segments, output_base)

    update_firmware_index(environment, {
        "id": environment,
        "name": display_name_for_env(environment),
        "file": firmware_path.name,
        "address": f"0x{flash_offset:X}",
        "size": size,
        "chip": chip,
        "board_slug": board_slug,
    })

    print(f"ESPVerse: wrote {firmware_path} ({size} bytes), flash offset 0x{flash_offset:X}")


if env is not None:
    env.AddPostAction("buildprog", after_build)
else:
    print("Run this file through PlatformIO extra_scripts.", file=sys.stderr)

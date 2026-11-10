"""Extract the roguelike icons referenced by battle-config.js.

Arknights uses a byte-swapped LZ4 variant for compression flags 4 and 5.
UnityPy's upstream build does not decode it, so this script applies the
compatible transform at runtime instead of modifying the installed package.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
from pathlib import Path

import UnityPy
from UnityPy import Environment
from UnityPy.enums.BundleFile import CompressionFlags
from UnityPy.files.BundleFile import BundleFile
from UnityPy.helpers import CompressionHelper


ROOT = Path(__file__).resolve().parents[3]
TABLE_PATH = (
    ROOT / "_assets" / "ArknightsGameResource" / "gamedata" / "excel"
    / "roguelike_topic_table.json"
)
GAME_AB = Path(r"E:\Games\ArkTest\Arknights\Arknights_Data\StreamingAssets\AB\Windows")
OUTPUT_ROOT = Path(__file__).resolve().parents[1] / "ui" / "assets" / "game" / "roguelike"


def read_long_length(data: bytearray, position: int) -> tuple[int, int]:
    total = 0
    while True:
        value = data[position]
        position += 1
        total += value
        if value != 255:
            return total, position


def decompress_ak_lz4(data: bytes, uncompressed_size: int) -> bytes:
    source = bytearray(data)
    input_position = 0
    output_position = 0
    while True:
        token = source[input_position]
        literal_length = token & 0x0F
        match_length = (token >> 4) & 0x0F
        source[input_position] = ((literal_length << 4) | match_length) & 0xFF
        input_position += 1
        if literal_length == 15:
            extra, input_position = read_long_length(source, input_position)
            literal_length += extra
        output_position += literal_length
        input_position += literal_length
        if uncompressed_size - output_position < 12:
            break
        offset = source[input_position + 1] | (source[input_position] << 8)
        source[input_position] = offset & 0xFF
        source[input_position + 1] = (offset >> 8) & 0xFF
        input_position += 2
        if match_length == 15:
            extra, input_position = read_long_length(source, input_position)
            match_length += extra
        match_length += 4
        output_position += match_length
    return CompressionHelper.decompress_lz4(bytes(source), uncompressed_size)


def install_arknights_decompressor() -> None:
    original = BundleFile.decompress_data

    def decompress_data(self, compressed_data, uncompressed_size, flags, index=0):
        compression = int(flags) & 0x3F
        if compression not in (4, 5):
            return original(self, compressed_data, uncompressed_size, flags, index)
        if self.decryptor is not None and flags & 0x100:
            compressed_data = self.decryptor.decrypt_block(compressed_data, index)
        return decompress_ak_lz4(compressed_data, uncompressed_size)

    BundleFile.decompress_data = decompress_data
    if 5 not in {member.value for member in CompressionFlags}:
        CompressionFlags._value2member_map_[5] = CompressionFlags(5)


def icon_ids_for_topic(table: dict, topic_id: str) -> set[str]:
    detail = table["details"][topic_id]
    modules = table["modules"][topic_id]
    customize = table["customizeData"][topic_id]
    icons: set[str] = set()

    relic_ids = set((detail.get("relics") or {}).keys())
    for item_id, item in (detail.get("items") or {}).items():
        if item_id in relic_ids and item and item.get("name") and item.get("iconId"):
            icons.add(str(item["iconId"]))

    for entry in (detail.get("variationData") or {}).values():
        if not isinstance(entry, dict):
            continue
        for key in ("iconId", "iconBigId", "toastIconId"):
            if entry.get(key):
                icons.add(str(entry[key]))

    scrap = (modules.get("scrap") or {}).get("passiveScrapData") or {}
    for entry in scrap.values():
        if isinstance(entry, dict) and entry.get("scrapId"):
            icons.add(str(entry["scrapId"]))

    for module in modules.values():
        if not isinstance(module, dict):
            continue
        for container in module.values():
            if not isinstance(container, dict):
                continue
            for entry in container.values():
                if not isinstance(entry, dict):
                    continue
                for key in ("iconId", "iconBigId", "picSmallId", "picBigActiveId",
                            "picBigInactiveId", "picSmallActiveId", "picSmallInactiveId",
                            "toastIconId"):
                    if entry.get(key):
                        icons.add(str(entry[key]))

    development = (
        (customize.get("commonDevelopment") or {}).get("developments")
        or customize.get("developments")
        or {}
    )
    for entry in development.values():
        if not isinstance(entry, dict):
            continue
        for key in ("buffIconId", "activeIconId", "inactiveIconId",
                    "bottomIconId", "lightId", "decoId"):
            if entry.get(key):
                icons.add(str(entry[key]))
    return icons


def bundle_paths(topic_id: str) -> list[Path]:
    number = topic_id.rsplit("_", 1)[-1]
    candidates = [
        GAME_AB / "spritepack" / f"ui_roguelike_topic_item_h1_rogue_{number}_0.ab",
        GAME_AB / "spritepack" / f"act_archive_h1_rogue_{number}_0.ab",
        GAME_AB / "refs" / f"rglktp_rogue_{number}.ab",
        GAME_AB / "refs" / f"rglktp_rogue_{number}add.ab",
        GAME_AB / "ui" / "rglktopic" / f"rogue_{number}.ab",
        GAME_AB / "ui" / "rglktopic" / f"rogue_{number}_update.ab",
        GAME_AB / "ui" / "archivedynamiccomponent"
        / f"[uc]archive_rogue_{number}.ab",
        GAME_AB / "ui" / "roguelike.ab",
    ]
    return [path for path in candidates if path.is_file()]


def safe_name(value: str) -> str:
    value = re.sub(r"[^A-Za-z0-9_.-]+", "_", value)
    return value.strip("._") or "icon"


def extract_topic(topic_id: str, wanted: set[str], clean: bool) -> int:
    output = OUTPUT_ROOT / topic_id
    if clean and output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True, exist_ok=True)
    remaining = set(wanted)
    written: set[str] = set()
    for bundle in bundle_paths(topic_id):
        print(f"[{topic_id}] {bundle.name}")
        try:
            environment = Environment()
            environment.load_file(str(bundle))
        except Exception as error:
            print(f"  skipped: {error}")
            continue
        for obj in environment.objects:
            try:
                if obj.type.name != "Sprite":
                    continue
                sprite = obj.read()
                name = str(getattr(sprite, "m_Name", "") or "")
                if not name or name not in remaining:
                    continue
                image = sprite.image
                target = output / f"{safe_name(name)}.png"
                image.save(target)
                remaining.discard(name)
                written.add(name)
            except Exception as error:
                print(f"  sprite read failed: {error}")
        if not remaining:
            break
    missing = sorted(wanted - written)
    print(f"[{topic_id}] wrote {len(written)}/{len(wanted)}; missing {len(missing)}")
    if missing:
        print("  missing sample:", ", ".join(missing[:20]))
    return len(written)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--topic", action="append", help="Topic id, e.g. rogue_6")
    parser.add_argument("--clean", action="store_true", help="Delete existing topic output")
    args = parser.parse_args()
    install_arknights_decompressor()
    table = json.loads(TABLE_PATH.read_text(encoding="utf-8"))
    topics = args.topic or list(table["details"].keys())
    total = 0
    for topic_id in topics:
        total += extract_topic(topic_id, icon_ids_for_topic(table, topic_id), args.clean)
    print(f"extracted {total} icons into {OUTPUT_ROOT}")


if __name__ == "__main__":
    main()

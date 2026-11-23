from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable


DEFAULT_ANALYSIS_DIR = Path("_analysis") / "action_graphs"
DEFAULT_EXCEL_DIR = (
    Path("_assets")
    / "ArknightsGameData"
    / "zh_CN"
    / "gamedata"
    / "excel"
)


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def iter_jsonl(path: Path) -> Iterable[dict[str, Any]]:
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            line = line.strip()
            if not line:
                continue
            try:
                yield json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_number}: {exc}") from exc


@dataclass
class DataStore:
    root: Path
    analysis_dir: Path
    excel_dir: Path
    operators: list[dict[str, Any]] = field(default_factory=list)
    operator_by_id: dict[str, dict[str, Any]] = field(default_factory=dict)
    prefabs: dict[str, dict[str, Any]] = field(default_factory=dict)
    prefab_by_lower: dict[str, str] = field(default_factory=dict)
    components: dict[str, dict[str, Any]] = field(default_factory=dict)
    buff_templates: dict[str, dict[str, Any]] = field(default_factory=dict)
    nodes: dict[str, dict[str, Any]] = field(default_factory=dict)
    action_node_types: dict[str, dict[str, Any]] = field(default_factory=dict)
    skill_table: dict[str, dict[str, Any]] = field(default_factory=dict)
    character_table: dict[str, dict[str, Any]] = field(default_factory=dict)
    summary: dict[str, Any] = field(default_factory=dict)
    unresolved_extract: dict[str, Any] = field(default_factory=dict)


def _resolve_root(root: str | os.PathLike[str]) -> Path:
    path = Path(root).expanduser().resolve()
    if not path.exists():
        raise FileNotFoundError(f"root does not exist: {path}")
    return path


def load_store(
    root: str | os.PathLike[str] = ".",
    analysis_dir: str | os.PathLike[str] | None = None,
    excel_dir: str | os.PathLike[str] | None = None,
) -> DataStore:
    root_path = _resolve_root(root)
    analysis_path = (
        Path(analysis_dir).expanduser().resolve()
        if analysis_dir
        else root_path / DEFAULT_ANALYSIS_DIR
    )
    excel_path = (
        Path(excel_dir).expanduser().resolve()
        if excel_dir
        else root_path / DEFAULT_EXCEL_DIR
    )
    if not analysis_path.exists():
        raise FileNotFoundError(f"analysis dir does not exist: {analysis_path}")

    store = DataStore(root=root_path, analysis_dir=analysis_path, excel_dir=excel_path)

    for item in iter_jsonl(analysis_path / "operators.jsonl"):
        store.operators.append(item)
        operator_id = item.get("id")
        if operator_id:
            store.operator_by_id[operator_id] = item

    for item in iter_jsonl(analysis_path / "prefabs.jsonl"):
        prefab_id = item.get("id")
        if prefab_id:
            store.prefabs[prefab_id] = item
            store.prefab_by_lower.setdefault(prefab_id.lower(), prefab_id)
        for component in item.get("components") or []:
            component_id = component.get("id")
            if component_id:
                store.components[component_id] = component

    for item in iter_jsonl(analysis_path / "buffTemplates.jsonl"):
        key = item.get("key")
        if key:
            store.buff_templates[key] = item

    for item in iter_jsonl(analysis_path / "nodes.jsonl"):
        node_id = item.get("id")
        if node_id:
            store.nodes[node_id] = item

    action_types_path = analysis_path / "actionNodeTypes.jsonl"
    if action_types_path.exists():
        for item in iter_jsonl(action_types_path):
            name = item.get("name")
            if name:
                store.action_node_types[name] = item

    summary_path = analysis_path / "summary.json"
    if summary_path.exists():
        store.summary = load_json(summary_path)
    unresolved_path = analysis_path / "unresolved.json"
    if unresolved_path.exists():
        store.unresolved_extract = load_json(unresolved_path)

    skill_table_path = excel_path / "skill_table.json"
    if skill_table_path.exists():
        store.skill_table = load_json(skill_table_path)
    character_table_path = excel_path / "character_table.json"
    if character_table_path.exists():
        store.character_table = load_json(character_table_path)

    return store

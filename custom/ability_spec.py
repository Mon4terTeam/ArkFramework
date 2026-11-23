from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PREFABS = ROOT.parent / "_analysis" / "action_graphs" / "prefabs.jsonl"


def _load_prefab(path: Path, prefab_id: str) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            item = json.loads(line)
            if item.get("id") == prefab_id:
                return item
    raise SystemExit(f"prefab not found: {prefab_id}")


def _json_value(value: Any) -> Any:
    if isinstance(value, dict):
        if "isDefined" in value and "value" in value:
            return _json_value(value["value"])
        return None
    if isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, list):
        if all(isinstance(item, str) for item in value):
            return list(value)
        return None
    return None


def _pick_component(prefab: dict[str, Any], path: str,
                    kind: str | None) -> dict[str, Any]:
    matches = [
        component
        for component in prefab.get("components", [])
        if component.get("path") == path
        and (not kind or component.get("kind") == kind)
    ]
    if not matches:
        raise SystemExit(f"component not found: path={path!r} kind={kind!r}")
    if len(matches) > 1:
        kinds = ", ".join(sorted({str(item.get("kind")) for item in matches}))
        raise SystemExit(
            f"component path is ambiguous: {path!r}; pass --kind ({kinds})"
        )
    return matches[0]


def _infer_runtime(component: dict[str, Any]) -> tuple[str, str, str]:
    fields = component.get("fields") or {}
    if "_manageProjectileByOwner" in fields:
        return (
            "Torappu.Battle.Abilities.SimpleProjectileEmitter",
            "behaviour",
            "attack",
        )
    if component.get("kind") == "PassiveBuffAbility":
        return ("Torappu.Battle.Abilities.PassiveBuffAbility", "ability", "entity")
    raise SystemExit(
        "cannot infer runtime type; pass --type and --kind explicitly"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate a custom-create-ability JSON spec from static prefab data."
    )
    parser.add_argument("--prefab", required=True, help="prefab id, e.g. skill:skchr_chen3_3")
    parser.add_argument("--path", required=True, help="component path, e.g. skchr_chen3_3/EmitProjectile")
    parser.add_argument("--kind", help="static component kind, e.g. ProjectileAbility")
    parser.add_argument("--type", dest="runtime_type", help="runtime full type name")
    parser.add_argument("--runtime-kind", choices=("ability", "behaviour"))
    parser.add_argument("--anchor", choices=("entity", "skill", "attack"))
    parser.add_argument("--prefabs", type=Path, default=DEFAULT_PREFABS)
    parser.add_argument("--out", type=Path, help="write spec here; default stdout")
    args = parser.parse_args()

    prefab = _load_prefab(args.prefabs, args.prefab)
    component = _pick_component(prefab, args.path, args.kind)
    inferred_type, inferred_kind, inferred_anchor = _infer_runtime(component)
    runtime_type = args.runtime_type or inferred_type
    runtime_kind = args.runtime_kind or inferred_kind
    anchor = args.anchor or inferred_anchor

    fields: dict[str, Any] = {}
    skipped: list[str] = []
    for key, raw in (component.get("fields") or {}).items():
        value = _json_value(raw)
        if value is None:
            skipped.append(key)
            continue
        fields[key] = value

    spec = {
        "type": runtime_type,
        "kind": runtime_kind,
        "anchor": anchor,
        "fields": fields,
        "_source": {
            "prefab": args.prefab,
            "path": args.path,
            "kind": component.get("kind"),
        },
    }
    text = json.dumps(spec, ensure_ascii=False, indent=2) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
        print(f"wrote {args.out}")
    else:
        print(text, end="")
    if skipped:
        print("skipped non-scalar fields: " + ", ".join(skipped))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

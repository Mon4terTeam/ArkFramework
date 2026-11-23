from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from .coverage import build_coverage, write_reports
from .loader import load_store
from .resolver import Resolver


def _default_root() -> Path:
    # ArkFramework/custom/parser/cli.py -> ArkTest
    return Path(__file__).resolve().parents[3]


def _dump(value: Any) -> None:
    json.dump(value, sys.stdout, ensure_ascii=False, indent=2)
    sys.stdout.write("\n")


def _find_operator(store, operator_id: str) -> dict[str, Any]:
    operator = store.operator_by_id.get(operator_id)
    if operator is None:
        raise SystemExit(f"operator not found: {operator_id}")
    return operator


def _find_skill(store, skill_id: str) -> tuple[dict[str, Any], dict[str, Any]]:
    for operator in store.operators:
        for skill in operator.get("skills") or []:
            if skill.get("skillId") == skill_id:
                return operator, skill
    raise SystemExit(f"skill not found: {skill_id}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="custom.parser",
        description=(
            "Static parser for operator skills, prefab abilities, Buff templates "
            "and ActionNode graphs."
        ),
    )
    parser.add_argument(
        "--root",
        default=str(_default_root()),
        help="ArkTest root containing _analysis/action_graphs and _assets",
    )
    parser.add_argument(
        "--analysis-dir",
        default=None,
        help="override the _analysis/action_graphs directory",
    )
    parser.add_argument(
        "--excel-dir",
        default=None,
        help="override the gamedata/excel directory",
    )
    parser.add_argument(
        "--out",
        default=None,
        help="output directory (default: <ArkFramework>/custom/out)",
    )
    parser.add_argument(
        "--operator",
        default=None,
        help="dump one operator (e.g. char_103_angel)",
    )
    parser.add_argument(
        "--skill",
        default=None,
        help="dump one skill (e.g. skchr_angel_3)",
    )
    parser.add_argument(
        "--prefab",
        default=None,
        help="dump one prefab (e.g. skill:skchr_angel_3)",
    )
    parser.add_argument(
        "--buff",
        default=None,
        help="dump one Buff template graph (e.g. c2e_cold)",
    )
    parser.add_argument(
        "--full",
        action="store_true",
        help="include serialized component fields and nested prefab graphs",
    )
    parser.add_argument(
        "--no-write",
        action="store_true",
        help="do not write coverage reports",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    except Exception:
        pass
    args = build_parser().parse_args(argv)
    store = load_store(
        root=args.root,
        analysis_dir=args.analysis_dir,
        excel_dir=args.excel_dir,
    )
    resolver = Resolver(store)

    if args.operator:
        operator = _find_operator(store, args.operator)
        _dump(resolver.resolve_operator(operator, full=args.full))
        return 0

    if args.skill:
        operator, skill = _find_skill(store, args.skill)
        report = resolver.resolve_operator(operator, full=args.full)
        for item in report.get("skills") or []:
            if item.get("skill_id") == args.skill:
                _dump(item)
                return 0
        raise SystemExit(f"skill not resolved: {args.skill}")

    if args.prefab:
        _dump(resolver._prefab_summary(args.prefab, args.full, "<cli>", frozenset()))
        return 0

    if args.buff:
        _dump(resolver.resolve_buff(args.buff, full=True))
        return 0

    operators = resolver.resolve_all_operators(full=args.full)
    report = build_coverage(store, resolver, operators)
    if not args.no_write:
        out = (
            Path(args.out)
            if args.out
            else Path(__file__).resolve().parents[1] / "out"
        )
        write_reports(out, report, resolver.unresolved)
        print(f"coverage written to {out}", file=sys.stderr)

    coverage = report["coverage"]
    print(
        json.dumps(
            {
                "counts": report["counts"],
                "coverage": coverage,
                "needsRuntimeVerification": report["needsRuntimeVerification"],
                "unresolved": {
                    "total": report["unresolved"]["total"],
                    "byKind": report["unresolved"]["byKind"],
                    "byReason": report["unresolved"]["byReason"],
                },
            },
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0


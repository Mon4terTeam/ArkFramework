from __future__ import annotations

import json
from collections import Counter
from datetime import datetime
from pathlib import Path
from typing import Any

from .loader import DataStore
from .resolver import Resolver


def _ratio(part: int, total: int) -> float:
    return round(part / total, 4) if total else 1.0


def _sorted_counter(counter: Counter[str]) -> dict[str, int]:
    return dict(sorted(counter.items()))


def build_coverage(
    store: DataStore,
    resolver: Resolver,
    operators: list[dict[str, Any]],
) -> dict[str, Any]:
    operator_count = sum(1 for item in operators if item.get("kind") == "operator")
    token_count = sum(1 for item in operators if item.get("kind") == "token")

    character_total = sum(
        1 for item in operators if item.get("character_prefab", {}).get("id")
    )
    character_resolved = sum(
        1 for item in operators if item.get("character_prefab", {}).get("exists")
    )

    skill_prefab_ids: set[str] = set()
    skill_prefab_occurrences = 0
    skill_prefab_resolved_occurrences = 0
    skills_total = 0
    operators_with_skills = 0
    operators_all_skill_prefabs = 0
    for operator in operators:
        skills = operator.get("skills") or []
        skills_total += len(skills)
        if skills:
            operators_with_skills += 1
        all_resolved = True
        for skill in skills:
            prefab_ids = skill.get("prefab_ids") or []
            skill_prefab_occurrences += len(prefab_ids)
            skill_prefab_resolved_occurrences += skill.get("prefabs_resolved", 0)
            skill_prefab_ids.update(prefab_ids)
            if len(prefab_ids) != skill.get("prefabs_resolved", 0):
                all_resolved = False
        if skills and all_resolved:
            operators_all_skill_prefabs += 1

    talent_total = sum(
        item.get("talents", {}).get("components_total", 0) for item in operators
    )
    talent_resolved = sum(
        item.get("talents", {}).get("components_resolved", 0) for item in operators
    )
    trait_total = sum(
        item.get("trait", {}).get("components_total", 0) for item in operators
    )
    trait_resolved = sum(
        item.get("trait", {}).get("components_resolved", 0) for item in operators
    )
    module_total = sum(
        item.get("modules", {}).get("prefabs_total", 0) for item in operators
    )
    module_resolved = sum(
        item.get("modules", {}).get("prefabs_resolved", 0) for item in operators
    )

    core_covered = sum(
        1
        for item in operators
        if item.get("coverage", {}).get("core_fully_covered")
    )
    all_covered = sum(
        1
        for item in operators
        if item.get("coverage", {}).get("all_fully_covered")
    )

    reached_prefabs = len(resolver.reached_prefabs)
    all_prefabs = len(store.prefabs)
    component_kinds: Counter[str] = Counter()
    for prefab_id in resolver.reached_prefabs:
        prefab = store.prefabs.get(prefab_id)
        if not prefab:
            continue
        for component in prefab.get("components") or []:
            component_kinds[str(component.get("kind") or "Unknown")] += 1

    buff_graphs = list(resolver.unique_buff_graphs.values())
    buff_graphs_with_events = [graph for graph in buff_graphs if graph.get("events")]
    buff_graphs_resolved = [
        graph
        for graph in buff_graphs_with_events
        if graph.get("exists") and not graph.get("missing")
    ]
    inline_graphs = list(resolver.unique_inline_graphs.values())
    inline_graphs_resolved = [
        graph for graph in inline_graphs if not graph.get("missing")
    ]

    reached_nodes: set[str] = set()
    missing_nodes: set[str] = set()
    for reached, missing in resolver.node_closure_cache.values():
        reached_nodes.update(reached)
        missing_nodes.update(missing)
    dynamic_prefixes = (
        "CreateBuff",
        "CreateBuffById",
        "SpawnToken",
        "TriggerAbility",
        "TriggerSpecifiedAbility",
        "SwitchMode",
        "FinishBuff",
        "AddBuff",
        "RemoveBuff",
        "CreateProjectile",
    )
    dynamic_node_types = sorted(
        {
            str(store.nodes[node_id].get("name"))
            for node_id in reached_nodes
            if node_id in store.nodes
            and any(
                str(store.nodes[node_id].get("name") or "").startswith(prefix)
                for prefix in dynamic_prefixes
            )
        }
    )

    unresolved_by_kind = Counter(item.get("kind") for item in resolver.unresolved)
    unresolved_by_reason = Counter(item.get("reason") for item in resolver.unresolved)

    report = {
        "generatedAt": datetime.now().astimezone().isoformat(timespec="seconds"),
        "source": {
            "root": str(store.root),
            "analysisDir": str(store.analysis_dir),
            "excelDir": str(store.excel_dir),
        },
        "counts": {
            "operators": operator_count,
            "tokens": token_count,
            "operatorEntries": len(operators),
            "skills": skills_total,
            "prefabsTotal": all_prefabs,
            "prefabsReached": reached_prefabs,
            "buffTemplatesTotal": len(store.buff_templates),
            "actionNodeTypesTotal": len(store.action_node_types),
            "nodesTotal": len(store.nodes),
        },
        "coverage": {
            "characterPrefabs": {
                "total": character_total,
                "resolved": character_resolved,
                "ratio": _ratio(character_resolved, character_total),
            },
            "skillPrefabs": {
                "unique": len(skill_prefab_ids),
                "occurrences": skill_prefab_occurrences,
                "resolvedOccurrences": skill_prefab_resolved_occurrences,
                "ratio": _ratio(
                    skill_prefab_resolved_occurrences, skill_prefab_occurrences
                ),
            },
            "talents": {
                "components": talent_total,
                "resolved": talent_resolved,
                "ratio": _ratio(talent_resolved, talent_total),
            },
            "traits": {
                "components": trait_total,
                "resolved": trait_resolved,
                "ratio": _ratio(trait_resolved, trait_total),
            },
            "modules": {
                "prefabs": module_total,
                "resolved": module_resolved,
                "ratio": _ratio(module_resolved, module_total),
            },
            "operators": {
                "withSkills": operators_with_skills,
                "allSkillPrefabsResolved": operators_all_skill_prefabs,
                "coreFullyCovered": core_covered,
                "allFullyCovered": all_covered,
                "coreRatio": _ratio(core_covered, len(operators)),
                "allRatio": _ratio(all_covered, len(operators)),
            },
            "graphs": {
                "buffTemplatesWithEvents": len(buff_graphs_with_events),
                "buffTemplatesResolved": len(buff_graphs_resolved),
                "inlineGraphs": len(inline_graphs),
                "inlineGraphsResolved": len(inline_graphs_resolved),
                "nodesReached": len(reached_nodes),
                "nodesMissing": len(missing_nodes),
            },
            "componentKinds": _sorted_counter(component_kinds),
        },
        "needsRuntimeVerification": {
            "staticExtractionGaps": {
                "unresolvedPrefabs": sum(
                    1 for item in resolver.unresolved if item.get("kind") == "prefab"
                ),
            },
            "staticRefs": {
                "selectors": {
                    "resolved": resolver.stats.get("selector_refs_resolved", 0),
                    "default": resolver.stats.get("selector_refs_default", 0),
                    "unresolved": resolver.stats.get("selector_refs_unresolved", 0),
                },
                "targetValidators": {
                    "resolved": resolver.stats.get(
                        "target_validator_refs_resolved", 0
                    ),
                    "default": resolver.stats.get(
                        "target_validator_refs_default", 0
                    ),
                    "unresolved": resolver.stats.get(
                        "target_validator_refs_unresolved", 0
                    ),
                },
                "components": {
                    "resolved": resolver.stats.get("component_refs_resolved", 0),
                    "unresolved": resolver.stats.get("component_refs_unresolved", 0),
                },
                "abilities": {
                    "resolved": resolver.stats.get("ability_refs_resolved", 0),
                    "unresolved": resolver.stats.get("ability_refs_unresolved", 0),
                },
                "opaqueAuxComponents": resolver.stats.get(
                    "opaque_aux_components", 0
                ),
            },
            "runtimeState": {
                "modules": {
                    "modeComponents": resolver.stats.get("mode_components", 0),
                    "modeEntries": resolver.stats.get("mode_entries", 0),
                    "extraAbilityComponents": resolver.stats.get(
                        "extra_ability_components", 0
                    ),
                    "extraAbilityEntries": resolver.stats.get(
                        "extra_ability_entries", 0
                    ),
                    "nativeAbilityFields": resolver.stats.get(
                        "native_ability_fields", 0
                    ),
                },
                "buffs": {
                    "loadFromDb": resolver.stats.get("buff_load_from_db", 0),
                },
                "dynamicNodes": {
                    "nodeTypes": len(dynamic_node_types),
                    "nodeTypesSample": dynamic_node_types[:20],
                },
            },
            "staticGraph": {
                "resolvedBuffTemplates": resolver.stats.get(
                    "buff_template_resolved", 0
                ),
                "missingBuffTemplates": resolver.stats.get(
                    "buff_template_missing", 0
                ),
                "buffGraphsWithEvents": len(buff_graphs_with_events),
                "inlineGraphs": len(inline_graphs),
                "nodesReached": len(reached_nodes),
            },
        },
        "unresolved": {
            "total": len(resolver.unresolved),
            "byKind": _sorted_counter(unresolved_by_kind),
            "byReason": _sorted_counter(unresolved_by_reason),
            "extractedReport": store.unresolved_extract,
        },
        "operators": operators,
    }
    return report


def _md_table(headers: list[str], rows: list[list[Any]]) -> str:
    lines = ["| " + " | ".join(headers) + " |"]
    lines.append("| " + " | ".join("---" for _ in headers) + " |")
    for row in rows:
        lines.append("| " + " | ".join(str(value) for value in row) + " |")
    return "\n".join(lines)


def render_markdown(report: dict[str, Any], max_unresolved: int = 40) -> str:
    coverage = report["coverage"]
    counts = report["counts"]
    lines = [
        "# Custom skill parser coverage",
        "",
        f"Generated: {report['generatedAt']}",
        "",
        "## Summary",
        "",
        _md_table(
            ["Item", "Resolved", "Total", "Ratio"],
            [
                [
                    "Character prefabs",
                    coverage["characterPrefabs"]["resolved"],
                    coverage["characterPrefabs"]["total"],
                    coverage["characterPrefabs"]["ratio"],
                ],
                [
                    "Skill prefab occurrences",
                    coverage["skillPrefabs"]["resolvedOccurrences"],
                    coverage["skillPrefabs"]["occurrences"],
                    coverage["skillPrefabs"]["ratio"],
                ],
                [
                    "Talent components",
                    coverage["talents"]["resolved"],
                    coverage["talents"]["components"],
                    coverage["talents"]["ratio"],
                ],
                [
                    "Trait components",
                    coverage["traits"]["resolved"],
                    coverage["traits"]["components"],
                    coverage["traits"]["ratio"],
                ],
                [
                    "Module prefabs",
                    coverage["modules"]["resolved"],
                    coverage["modules"]["prefabs"],
                    coverage["modules"]["ratio"],
                ],
            ],
        ),
        "",
        _md_table(
            ["Count", "Value"],
            [
                ["Operators", counts["operators"]],
                ["Tokens", counts["tokens"]],
                ["Skills", counts["skills"]],
                ["Prefabs total", counts["prefabsTotal"]],
                ["Prefabs reached", counts["prefabsReached"]],
                ["Buff templates total", counts["buffTemplatesTotal"]],
                ["ActionNode types", counts["actionNodeTypesTotal"]],
                ["Nodes total", counts["nodesTotal"]],
                ["Operators with skills", coverage["operators"]["withSkills"]],
                [
                    "Operators all skill prefabs resolved",
                    coverage["operators"]["allSkillPrefabsResolved"],
                ],
                [
                    "Operators core fully covered",
                    coverage["operators"]["coreFullyCovered"],
                ],
                [
                    "Operators all fully covered",
                    coverage["operators"]["allFullyCovered"],
                ],
                [
                    "Buff graphs with events",
                    coverage["graphs"]["buffTemplatesWithEvents"],
                ],
                [
                    "Buff graphs resolved",
                    coverage["graphs"]["buffTemplatesResolved"],
                ],
                ["Inline graphs", coverage["graphs"]["inlineGraphs"]],
                [
                    "Inline graphs resolved",
                    coverage["graphs"]["inlineGraphsResolved"],
                ],
                ["Nodes reached", coverage["graphs"]["nodesReached"]],
                ["Nodes missing", coverage["graphs"]["nodesMissing"]],
            ],
        ),
        "",
        "## Component kinds reached",
        "",
        _md_table(
            ["Kind", "Count"],
            sorted(coverage["componentKinds"].items(), key=lambda item: -item[1]),
        ),
        "",
        "## Unresolved",
        "",
        f"Total: {report['unresolved']['total']}",
        "",
        _md_table(
            ["Kind", "Count"],
            sorted(
                report["unresolved"]["byKind"].items(),
                key=lambda item: -item[1],
            ),
        ),
        "",
        "## Needs runtime verification",
        "",
        _md_table(
            ["Class", "Category", "Count"],
            [
                ["Static gap", "Unresolved prefabs", report["needsRuntimeVerification"]["staticExtractionGaps"]["unresolvedPrefabs"]],
                ["Static ref", "Selectors resolved", report["needsRuntimeVerification"]["staticRefs"]["selectors"]["resolved"]],
                ["Static ref", "Selectors unresolved", report["needsRuntimeVerification"]["staticRefs"]["selectors"]["unresolved"]],
                ["Static ref", "Target validators resolved", report["needsRuntimeVerification"]["staticRefs"]["targetValidators"]["resolved"]],
                ["Static ref", "Target validators unresolved", report["needsRuntimeVerification"]["staticRefs"]["targetValidators"]["unresolved"]],
                ["Static ref", "Component refs resolved", report["needsRuntimeVerification"]["staticRefs"]["components"]["resolved"]],
                ["Static ref", "Component refs unresolved", report["needsRuntimeVerification"]["staticRefs"]["components"]["unresolved"]],
                ["Static ref", "Ability refs resolved", report["needsRuntimeVerification"]["staticRefs"]["abilities"]["resolved"]],
                ["Static ref", "Ability refs unresolved", report["needsRuntimeVerification"]["staticRefs"]["abilities"]["unresolved"]],
                ["Static ref", "Aux components without readable fields", report["needsRuntimeVerification"]["staticRefs"]["opaqueAuxComponents"]],
                ["Runtime state", "Mode components", report["needsRuntimeVerification"]["runtimeState"]["modules"]["modeComponents"]],
                ["Runtime state", "Mode entries", report["needsRuntimeVerification"]["runtimeState"]["modules"]["modeEntries"]],
                ["Runtime state", "Extra ability components", report["needsRuntimeVerification"]["runtimeState"]["modules"]["extraAbilityComponents"]],
                ["Runtime state", "Extra ability entries", report["needsRuntimeVerification"]["runtimeState"]["modules"]["extraAbilityEntries"]],
                ["Runtime state", "Native ability field occurrences", report["needsRuntimeVerification"]["runtimeState"]["modules"]["nativeAbilityFields"]],
                ["Runtime state", "Buff descriptors from DB", report["needsRuntimeVerification"]["runtimeState"]["buffs"]["loadFromDb"]],
                ["Runtime state", "Dynamic node types", report["needsRuntimeVerification"]["runtimeState"]["dynamicNodes"]["nodeTypes"]],
                ["Static graph", "Resolved Buff templates", report["needsRuntimeVerification"]["staticGraph"]["resolvedBuffTemplates"]],
                ["Static graph", "Missing Buff templates", report["needsRuntimeVerification"]["staticGraph"]["missingBuffTemplates"]],
            ],
        ),
    ]

    unresolved = report.get("unresolvedItems") or []
    if unresolved:
        lines.extend(
            [
                "",
                "First unresolved references:",
                "",
                _md_table(
                    ["Kind", "Owner", "Ref", "Reason"],
                    [
                        [
                            item.get("kind"),
                            item.get("owner"),
                            item.get("ref"),
                            item.get("reason"),
                        ]
                        for item in unresolved[:max_unresolved]
                    ],
                ),
            ]
        )
    failed_operators = [
        operator
        for operator in report.get("operators") or []
        if not operator.get("coverage", {}).get("core_fully_covered")
    ]
    if failed_operators:
        lines.extend(
            [
                "",
                "## Operators not core-covered",
                "",
                _md_table(
                    ["Operator", "Name", "Missing skill refs", "Missing talent refs", "Missing trait refs"],
                    [
                        [
                            operator.get("id"),
                            operator.get("name"),
                            operator.get("coverage", {}).get("missing_skill_refs"),
                            operator.get("coverage", {}).get("missing_talent_refs"),
                            operator.get("coverage", {}).get("missing_trait_refs"),
                        ]
                        for operator in failed_operators
                    ],
                ),
            ]
        )
    return "\n".join(lines) + "\n"


def write_reports(
    output_dir: str | Path,
    report: dict[str, Any],
    unresolved_items: list[dict[str, Any]],
) -> None:
    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)
    report_with_items = dict(report)
    report_with_items["unresolvedItems"] = unresolved_items

    (out / "coverage.json").write_text(
        json.dumps(report_with_items, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    (out / "coverage.md").write_text(
        render_markdown(report_with_items),
        encoding="utf-8",
    )
    (out / "unresolved.json").write_text(
        json.dumps(unresolved_items, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    with (out / "operators.jsonl").open("w", encoding="utf-8") as handle:
        for operator in report.get("operators") or []:
            handle.write(json.dumps(operator, ensure_ascii=False) + "\n")
    with (out / "skills.jsonl").open("w", encoding="utf-8") as handle:
        for operator in report.get("operators") or []:
            for skill in operator.get("skills") or []:
                handle.write(
                    json.dumps(
                        {
                            "operator": operator.get("id"),
                            "operator_name": operator.get("name"),
                            **skill,
                        },
                        ensure_ascii=False,
                    )
                    + "\n"
                )

    runtime = report["needsRuntimeVerification"]
    gaps = runtime["staticExtractionGaps"]
    state = runtime["runtimeState"]
    static_graph = runtime["staticGraph"]
    static_refs = runtime["staticRefs"]
    unresolved = unresolved_items
    unresolved_lines = "\n".join(
        f"- `{item['ref']}` ({item['kind']}, owner `{item['owner']}`): {item['reason']}"
        for item in unresolved
    )
    (out / "runtime_verification.md").write_text(
        "\n".join(
            [
                "# Runtime verification checklist",
                "",
                "This report separates two different things that are often mixed:",
                "",
                "1. static expansion status - prefab-internal references and graphs;",
                "2. runtime state/execution - static structure is known, but live objects matter;",
                "",
                "## A. Static expansion status",
                "",
                f"- unresolved prefabs: {gaps['unresolvedPrefabs']}",
                f"- selectors resolved/default/unresolved: {static_refs['selectors']['resolved']}/{static_refs['selectors']['default']}/{static_refs['selectors']['unresolved']}",
                f"- target validators resolved/default/unresolved: {static_refs['targetValidators']['resolved']}/{static_refs['targetValidators']['default']}/{static_refs['targetValidators']['unresolved']}",
                f"- component references resolved/unresolved: {static_refs['components']['resolved']}/{static_refs['components']['unresolved']}",
                f"- ability references resolved/unresolved: {static_refs['abilities']['resolved']}/{static_refs['abilities']['unresolved']}",
                f"- auxiliary components with no readable fields: {static_refs['opaqueAuxComponents']}",
                "",
                "Selector, target-validator and component references are fully expanded.  Ability",
                "names are matched against captured components; the remaining names are dynamic",
                "or external abilities.  Auxiliary objects without a type tree are retained as",
                "opaque components, so their internal filters still need runtime inspection.",
                "Live target choice still needs runtime.",
                "",
                "## B. Runtime state/execution (structure known, live state needed)",
                "",
                f"- mode components: {state['modules']['modeComponents']}",
                f"- mode entries: {state['modules']['modeEntries']}",
                f"- extra ability components: {state['modules']['extraAbilityComponents']}",
                f"- extra ability entries: {state['modules']['extraAbilityEntries']}",
                f"- native ability field occurrences: {state['modules']['nativeAbilityFields']}",
                f"- Buff descriptors loaded from DB: {state['buffs']['loadFromDb']}",
                f"- dynamic node types: {state['dynamicNodes']['nodeTypes']}",
                "",
                "These are the actual runtime-sensitive parts: current mode, whether an",
                "extra ability is attached/executing, live blackboard/selector resolution,",
                "DB-loaded Buffs, and entities created by ActionNodes.",
                "",
                "## C. Static graph that was resolved",
                "",
                f"- resolved Buff templates: {static_graph['resolvedBuffTemplates']}",
                f"- missing Buff templates: {static_graph['missingBuffTemplates']}",
                f"- Buff graphs with events: {static_graph['buffGraphsWithEvents']}",
                f"- inline graphs: {static_graph['inlineGraphs']}",
                f"- nodes reached: {static_graph['nodesReached']}",
                "",
                "## D. Unresolved references",
                "",
                f"Count: {len(unresolved)}",
                "",
                unresolved_lines or "- none",
                "",
                "## E. Genuinely runtime-only checks",
                "",
                "- `loadFromDB` Buffs (205): the template lives in runtime/DB data.",
                "- Live selectors/targets and blackboard values: the rules are static, but",
                "  the selected entity depends on the current battle state.",
                "- Native Ability code: `_additionalTimes`, `_attackType`, `_damageType`,",
                "  `_projectileKey`, `_maxTriggerTime`, `_isOverloadSkill` are parameters;",
                "  the actual behavior is native code, not an ActionNode graph.",
                "- ActionNodes that create/mutate entities (`CreateBuff`, `SpawnToken`,",
                "  `TriggerAbility`, `SwitchMode`): graph is known, runtime lifetime is not.",
                "- Lua bridge: `enableBattleLua`, `[uc]lua/entry.lua`,",
                "  `Hotfixes/DefinedFix.lua`, `LuaBattleMgr`, `LuaAbilityBehaviourStub`.",
                "",
            ]
        ),
        encoding="utf-8",
    )

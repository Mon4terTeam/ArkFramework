from __future__ import annotations

from pathlib import Path

from .coverage import build_coverage
from .loader import load_store
from .resolver import Resolver


def main() -> int:
    root = Path(__file__).resolve().parents[3]
    store = load_store(root=root)
    resolver = Resolver(store)

    angel = resolver.resolve_operator(store.operator_by_id["char_103_angel"])
    skills = {skill["skill_id"]: skill for skill in angel["skills"]}
    assert set(skills) == {"skchr_angel_1", "skchr_angel_2", "skchr_angel_3"}
    angel_1 = skills["skchr_angel_1"]["prefabs"][0]
    assert "ProjectileAbility" in angel_1["component_kinds"]
    assert "BuffAbility" not in angel_1["component_kinds"]
    assert "BuffAbility" in skills["skchr_angel_2"]["prefabs"][0]["component_kinds"]

    chen = resolver.resolve_operator(store.operator_by_id["char_1050_chen3"], full=True)
    chen_s3 = next(
        skill for skill in chen["skills"] if skill["skill_id"] == "skchr_chen3_3"
    )
    prefab = chen_s3["prefabs"][0]
    assert prefab["exists"]
    assert "ProjectileAbility" in prefab["component_kinds"]
    projectile = prefab["projectiles"][0]
    assert projectile["id"] == "projectile:projectile_chr_chen3_s3"
    action = projectile["components"][0]
    assert action["kind"] == "ActionAbility"
    details = action["inline_graph"]["details"]
    assert details and details[0]["name"] == "DamageViaCurHpRatio"
    selector_components = [
        component
        for prefab_item in chen_s3["prefabs"]
        for component in prefab_item.get("components") or []
        if component.get("selector", {}).get("kind") == "resolved"
    ]
    assert selector_components
    assert selector_components[0]["selector"]["target"]["kind"] == "TargetSelector"

    cold = resolver.resolve_buff("c2e_cold")
    assert cold["exists"]
    assert "c2e_freeze[holder]" in cold["explanations"]["ON_BUFF_TRIGGER"]
    assert cold["action_counts"]["buff"] > 0

    operators = resolver.resolve_all_operators()
    report = build_coverage(store, resolver, operators)
    counts = report["counts"]
    assert counts["operators"] == 467
    assert counts["tokens"] == 73
    assert report["coverage"]["characterPrefabs"]["resolved"] == 540
    assert report["coverage"]["skillPrefabs"]["resolvedOccurrences"] == 1145
    assert report["coverage"]["graphs"]["nodesMissing"] == 0
    refs = report["needsRuntimeVerification"]["staticRefs"]
    assert refs["selectors"]["unresolved"] == 0
    assert refs["targetValidators"]["unresolved"] == 0
    assert refs["components"]["unresolved"] == 0
    assert report["unresolved"]["byKind"]["prefab"] == 3
    assert report["unresolved"]["byKind"]["ability"] == 7
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

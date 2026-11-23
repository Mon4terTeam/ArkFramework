from __future__ import annotations

import json
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Iterable

from .loader import DataStore

sys.setrecursionlimit(20000)


ABILITY_KINDS = {
    "BasicSkill",
    "BuffAbility",
    "ProjectileAbility",
    "ActionAbility",
    "ToggleBuffAbility",
    "PassiveBuffAbility",
    "WrappedAbility",
    "Other",
}

KEY_ABILITY_FIELDS = (
    "_buffs",
    "_passiveBuffs",
    "_activeBuffs",
    "_actions",
    "_runActionOnEvent",
    "_projectileKey",
    "_attackType",
    "_damageType",
    "_additionalTimes",
    "_maxTriggerTime",
    "_isOverloadSkill",
    "_extraAbilities",
    "_modes",
    "_targetSide",
    "_selectNum",
    "_maxNum",
    "_limitTargetNum",
    "_checkUnitType",
    "_abnormalFlag",
    "_buffKeys",
    "_checkHpRatioCondition",
    "_checkDistToExit",
    "_useBlackboardRangeId",
    "_targetOptions",
)

COSMETIC_NODE_PREFIXES = (
    "CreateEffect",
    "PlayAudio",
    "PlaySound",
    "PlayVoice",
    "PlayAnim",
    "ShowText",
    "Camera",
    "LogExtraBattleInfo",
)

NODE_CATEGORIES = (
    (
        "buff",
        (
            "CreateBuff",
            "FinishBuff",
            "FinishDerivedBuff",
            "TriggerBuff",
            "AddBuff",
            "RemoveBuff",
        ),
    ),
    ("check", ("Check", "If", "Filter", "Condition", "AlwaysNext")),
    ("damage", ("Damage", "Attack")),
    ("ability", ("TriggerAbility", "TriggerEnemySkill", "SwitchMode", "CreateProjectile")),
    ("value", ("Assign", "Blackboard", "Calculate", "ModifySp")),
    ("cosmetic", COSMETIC_NODE_PREFIXES),
)

EVENT_LABELS = {
    "ON_BORN": "出生",
    "ON_DEAD": "死亡",
    "ON_ATTACK": "攻击时",
    "ON_OUTPUT_DAMAGE": "造成伤害后",
    "ON_BEING_ATTACKED": "受击时",
    "ON_TAKE_DAMAGE": "受到伤害后",
    "ON_TICK": "周期触发",
    "ON_BUFF_ADDED": "获得 Buff",
    "ON_BUFF_REMOVED": "移除 Buff",
    "ON_BUFF_STACK_CHANGED": "Buff 层数变化",
    "ON_BUFF_START": "Buff 开始",
    "ON_BUFF_FINISH": "Buff 结束",
    "ON_BUFF_ENABLE": "Buff 启用",
    "ON_BUFF_LATE_ENABLE": "Buff 延迟启用",
    "ON_BUFF_TRIGGER": "Buff 触发",
    "ON_ABNORMAL_FLAG_DIRTY": "异常状态变化",
    "ON_OWNER_LOCATE": "宿主定位",
    "ON_OWNER_FINISH": "宿主结束",
    "ON_TARGET_KILLED": "击杀目标",
    "ON_SKILL_START": "技能开始",
    "ON_SKILL_END": "技能结束",
    "ON_KILL": "击杀",
    "ON_HEAL": "治疗",
    "ON_OUTPUT_HEAL": "造成治疗后",
    "ON_RECEIVE_HEAL": "受到治疗",
    "ON_MOVE": "移动",
    "ON_BLOCK": "阻挡",
    "ON_UNBLOCK": "解除阻挡",
}


def _is_buff_descriptor(value: Any) -> bool:
    return (
        isinstance(value, dict)
        and isinstance(value.get("buffKey"), str)
        and any(
            key in value
            for key in ("templateKey", "loadFromDB", "overrideType", "lifeTimeType")
        )
    )


def _walk(value: Any, path: tuple[Any, ...] = ()) -> Iterable[tuple[Any, tuple[Any, ...]]]:
    """Yield every dict/list value with its path."""
    if isinstance(value, dict):
        yield value, path
        for key, child in value.items():
            yield from _walk(child, path + (key,))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from _walk(child, path + (index,))


def _dedupe(values: Iterable[Any], key=lambda value: value) -> list[Any]:
    seen: set[Any] = set()
    result: list[Any] = []
    for value in values:
        marker = key(value)
        if marker in seen:
            continue
        seen.add(marker)
        result.append(value)
    return result


def _node_category(name: str) -> str:
    for category, prefixes in NODE_CATEGORIES:
        if any(name.startswith(prefix) for prefix in prefixes):
            return category
    return "other"


def _node_role(path: tuple[Any, ...], node_name: str) -> str:
    field = ""
    for part in reversed(path):
        if not isinstance(part, int):
            field = str(part)
            break
    if field == "_conditionNode":
        return "条件"
    if field in ("_succeedNodes", "_successNodes"):
        return "满足"
    if field in ("_failNodes", "_failedNodes"):
        return "否则"
    if field in ("_actions", "_nodes"):
        return "执行"
    return field or "执行"


def _first_value(value: Any) -> Any:
    if isinstance(value, list):
        return value[0] if value else None
    return value


class Resolver:
    """Resolve the static graph rooted at one operator or prefab.

    The resolver is deliberately read-only.  It treats missing references as
    data, not as fatal errors, so a coverage run can report every gap instead of
    stopping at the first bad prefab.
    """

    def __init__(self, store: DataStore):
        self.store = store
        self.unresolved: list[dict[str, Any]] = []
        self.reached_prefabs: set[str] = set()
        self.reached_components: set[str] = set()
        self.unique_buff_graphs: dict[str, dict[str, Any]] = {}
        self.unique_inline_graphs: dict[str, dict[str, Any]] = {}
        self.stats: Counter[str] = Counter()
        self.prefab_cache: dict[tuple[str, bool], dict[str, Any]] = {}
        self.component_cache: dict[tuple[str, bool], dict[str, Any]] = {}
        self.buff_graph_cache: dict[tuple[str, bool], dict[str, Any]] = {}
        self._buff_aliases: dict[str, str] | None = None
        self.node_closure_cache: dict[str, tuple[frozenset[str], frozenset[str]]] = {}
        self._node_visiting: set[str] = set()
        self.component_parent: dict[str, str] = {}
        for prefab_id, prefab in store.prefabs.items():
            for component in prefab.get("components") or []:
                component_id = component.get("id")
                if component_id:
                    self.component_parent[component_id] = prefab_id

    # ------------------------------------------------------------------
    # generic reporting helpers
    # ------------------------------------------------------------------
    def _add_unresolved(
        self,
        kind: str,
        owner: str,
        reference: str,
        reason: str,
    ) -> None:
        self.unresolved.append(
            {
                "kind": kind,
                "owner": owner,
                "ref": reference,
                "reason": reason,
            }
        )

    def _node_closure(self, node_id: str) -> tuple[frozenset[str], frozenset[str]]:
        cached = self.node_closure_cache.get(node_id)
        if cached is not None:
            return cached
        node = self.store.nodes.get(node_id)
        if node is None:
            result = (frozenset(), frozenset({node_id}))
            self.node_closure_cache[node_id] = result
            return result

        if node_id in self._node_visiting:
            return (frozenset({node_id}), frozenset())

        self._node_visiting.add(node_id)
        reached: set[str] = {node_id}
        missing: set[str] = set()
        # A node may reuse a child node; recursively walk all serialized fields.
        for value, _path in _walk(node.get("fields") or {}):
            if not isinstance(value, dict):
                continue
            child = value.get("$node")
            if not isinstance(child, str):
                continue
            if child in reached:
                continue
            child_reached, child_missing = self._node_closure(child)
            reached.update(child_reached)
            missing.update(child_missing)

        self._node_visiting.discard(node_id)
        result = (frozenset(reached), frozenset(missing))
        self.node_closure_cache[node_id] = result
        return result

    def _node_detail(self, node_id: str, stack: frozenset[str]) -> dict[str, Any]:
        node = self.store.nodes.get(node_id)
        if node is None:
            return {"id": node_id, "missing": True}
        if node_id in stack:
            return {"id": node_id, "cycle": True}
        next_stack = stack | {node_id}
        fields = node.get("fields") or {}
        name = str(node.get("name") or node_id)
        children: list[dict[str, Any]] = []
        for value, path in _walk(fields):
            if not isinstance(value, dict):
                continue
            child = value.get("$node")
            if not isinstance(child, str):
                continue
            children.append(
                {
                    "path": list(path),
                    "role": _node_role(path, name),
                    "node": self._node_detail(child, next_stack),
                }
            )
        return {
            "id": node_id,
            "name": name,
            "type": node.get("type"),
            "category": _node_category(name),
            "summary": self._node_summary(name, fields),
            "fields": fields,
            "children": children,
        }

    def _node_summary(self, name: str, fields: dict[str, Any]) -> str:
        if name == "CreateBuff":
            buff = fields.get("_buff") or {}
            key = buff.get("buffKey") or buff.get("templateKey") or "?"
            bits = [f"创建 {key}"]
            if buff.get("maxStackCnt", 1) not in (None, 1):
                bits.append(f"最多 {buff['maxStackCnt']} 层")
            if buff.get("triggerInterval", -1) not in (None, -1, -1.0):
                bits.append(f"每 {buff['triggerInterval']:g}s 触发")
            if fields.get("_isDerivedBuff"):
                bits.append("派生 Buff")
            return "，".join(bits)
        if name == "CreateBuffById":
            bits = [f"创建 {fields.get('_buffKey') or '?'}"]
            if fields.get("_isDerivedBuff"):
                bits.append("派生 Buff")
            return "，".join(bits)
        if name in ("FinishBuffsById", "FinishDerivedBuffById"):
            keys = (
                fields.get("_buffKeys")
                or fields.get("_buffKey")
                or fields.get("_derviedBuffKey")
            )
            if not keys:
                return "结束当前派生 Buff" if "Derived" in name else "结束当前 Buff"
            if isinstance(keys, list):
                keys = "、".join(str(key) for key in keys)
            return f"结束 {keys or '?'}"
        if name == "FinishDerivedBuff":
            return "结束当前派生 Buff"
        if name in ("CheckContainsBuff", "CheckContainsDerviedBuff"):
            keys = (
                fields.get("_buffKeys")
                or fields.get("_buffKey")
                or fields.get("_derviedBuffKey")
                or []
            )
            if not keys:
                return (
                    "检查当前派生 Buff"
                    if "Dervied" in name or "Derived" in name
                    else "检查当前 Buff"
                )
            if not isinstance(keys, list):
                keys = [keys]
            joiner = " 且 " if fields.get("isAND") else " 或 "
            return "检查是否包含 " + joiner.join(str(key) for key in keys)
        if name == "CheckAbnormalFlag":
            prefix = "检查未处于" if fields.get("_isUnset") else "检查是否处于"
            return f"{prefix} {fields.get('_abnormalFlag') or '?'}"
        if name == "IfElse":
            return "条件分支"
        if name in ("IfConditions", "CheckConditions"):
            return "组合条件"
        if name == "TriggerAbility":
            return f"触发能力 {fields.get('_abilityName') or '?'}"
        if name == "AlwaysNext":
            return "继续"
        if name == "IfNot":
            return "取反"
        if name == "TriggerBuff":
            return "触发当前 Buff"
        if name == "CheckTriggerable":
            return "检查可触发"
        if name == "CheckTriggerableBuffsByKeys":
            return "检查可触发的 Buff"
        if name in ("CheckUnitAlive", "CheckUnitDead"):
            return "检查单位存活" if name.endswith("Alive") else "检查单位死亡"
        if name == "IsBlackboardEqualWithFloat":
            return f"比较黑板 {fields.get('_key') or fields.get('_bbKey') or '?'}"
        if name == "IfTargetEqual":
            return "判断目标是否相同"
        if name == "AssignGridPositionToBlackboard":
            return "记录格子位置"
        if name == "AddCharacterSharedBlackboard":
            return "写入共享黑板"
        if name == "FinishManagedProjectiles":
            return "结束管理的投射物"
        if name == "Evade":
            return "闪避"
        if name == "TriggerEnemySkill":
            return f"触发敌方技能 {fields.get('_skillKey') or fields.get('_abilityName') or '?'}"
        if name == "SwitchMode":
            return f"切换模式 #{fields.get('_modeIndex')}"
        if name in ("CreateProjectile", "CreateProjectileById"):
            key = (
                fields.get("_projectileKey")
                or fields.get("_projectileId")
                or fields.get("_key")
                or "?"
            )
            return f"创建投射物 {key}"
        if name in ("AdvancedApplyDamage", "FixedValueDamage", "DamageViaCurHpRatio"):
            scale = fields.get("_atkScale") or fields.get("_value")
            damage_type = fields.get("_damageType") or ""
            owner = fields.get("_targetType") or ""
            bits = ["造成伤害"]
            if scale is not None:
                bits.append(f"倍率 {scale}")
            if damage_type:
                bits.append(str(damage_type))
            if owner:
                bits.append(f"目标 {owner}")
            return "，".join(bits)
        if name in ("AssignValueToBB", "BlackboardAdd", "CalculateBlackboardValueViaParams"):
            key = fields.get("_key") or fields.get("_bbKey") or fields.get("_blackboardKey")
            return f"写入黑板 {key or '?'}"
        if name in ("FilterByBlackboardValue", "FilterByBuffStackCount"):
            key = fields.get("_key") or fields.get("_bbKey") or fields.get("_buffKey")
            return f"按 {key or '?'} 过滤"
        if name == "CreateBuffUseAbilitySelector":
            buff = fields.get("_buff") or {}
            return f"按技能选择器创建 {buff.get('buffKey') or buff.get('templateKey') or '?'}"
        if name == "TriggerBuffsByKeys":
            keys = fields.get("_buffKeys") or []
            return "触发 " + "、".join(str(key) for key in keys)
        if name == "ModifySp":
            return f"修改 SP {fields.get('_value', '?')}"
        if name.startswith("PlayAudio") or name.startswith("CreateEffect"):
            return "视觉/音效"
        if name == "LogExtraBattleInfo":
            return "调试日志"
        if name == "DoNothing":
            return "无操作"
        interesting = []
        for key in ("_buffKey", "_abilityName", "_projectileKey", "_modeIndex", "_value", "_key"):
            value = fields.get(key)
            if value not in (None, "", 0, -1):
                interesting.append(f"{key}={value}")
        return "，".join(interesting) if interesting else ""

    def _explain_node(self, node: dict[str, Any], depth: int = 0) -> str:
        if not node or node.get("missing"):
            return f"缺失节点 {node.get('id') if node else '?'}"
        if node.get("cycle"):
            return f"{node.get('name')}（循环）"
        if depth > 5:
            return node.get("summary") or node.get("name") or "…"
        name = node.get("name") or ""
        summary = node.get("summary") or name
        if name == "IfElse":
            condition = ""
            succeed: list[str] = []
            fail: list[str] = []
            for child in node.get("children") or []:
                role = child.get("role") or "执行"
                text = self._explain_node(child.get("node") or {}, depth + 1)
                if role == "条件":
                    condition = text
                elif role == "满足":
                    succeed.append(text)
                elif role == "否则":
                    fail.append(text)
            out = f"如果 {condition or '?'}"
            if succeed:
                out += "，则 " + " → ".join(succeed)
            else:
                out += "，则 无操作"
            if fail:
                out += "；否则 " + " → ".join(fail)
            return out
        for child in node.get("children") or []:
            role = child.get("role") or "执行"
            text = self._explain_node(child.get("node") or {}, depth + 1)
            if role in ("执行", "条件", "满足", "否则"):
                summary += f" → {text}"
            else:
                summary += f" → [{role}] {text}"
        return summary

    def _explain_nodes(self, nodes: list[dict[str, Any]]) -> str:
        if not nodes:
            return ""
        parts = [self._explain_node(node) for node in nodes]
        return "；".join(part for part in parts if part)

    def _resolve_node_list(
        self,
        node_ids: Iterable[str],
        owner: str,
        full: bool = False,
    ) -> dict[str, Any]:
        reached: set[str] = set()
        missing: set[str] = set()
        roots: list[str] = []
        for node_id in node_ids:
            if not isinstance(node_id, str):
                continue
            roots.append(node_id)
            child_reached, child_missing = self._node_closure(node_id)
            reached.update(child_reached)
            missing.update(child_missing)
        for node_id in sorted(missing):
            self._add_unresolved(
                "action_node",
                owner,
                node_id,
                "node id not present in nodes.jsonl",
            )
        result = {
            "roots": roots,
            "reached": len(reached),
            "missing": sorted(missing),
        }
        if full:
            result["details"] = [
                self._node_detail(node_id, frozenset()) for node_id in roots
            ]
        return result

    def _buff_graph(
        self,
        template_key: str,
        owner: str,
        full: bool = False,
    ) -> dict[str, Any]:
        cache_key = (template_key, full)
        cached = self.buff_graph_cache.get(cache_key)
        if cached is not None:
            return cached
        template = self.store.buff_templates.get(template_key)
        if template is None:
            result = {
                "template_key": template_key,
                "exists": False,
                "events": 0,
                "roots": 0,
                "nodes": 0,
                "missing": [template_key],
            }
            self.buff_graph_cache[cache_key] = result
            self._add_unresolved(
                "buff_template",
                owner,
                template_key,
                "template key not present in buffTemplates.jsonl",
            )
            return result

        events = template.get("events") or {}
        root_ids: list[str] = []
        for node_ids in events.values():
            root_ids.extend(node_ids or [])
        node_info = self._resolve_node_list(
            root_ids, f"buff:{template_key}", full=full
        )
        result = {
            "template_key": template_key,
            "exists": True,
            "events": len(events),
            "roots": len(root_ids),
            "nodes": node_info["reached"],
            "missing": node_info["missing"],
            "event_names": sorted(events),
            "refs": template.get("refs") or {},
        }
        if full:
            event_graphs: dict[str, Any] = {}
            explanations: dict[str, str] = {}
            for event, node_ids in events.items():
                info = self._resolve_node_list(
                    node_ids or [], f"buff:{template_key}.{event}", full=True
                )
                event_graphs[event] = info.get("details") or []
                explanations[event] = self._explain_nodes(event_graphs[event])
            result["event_graphs"] = event_graphs
            result["event_labels"] = {
                event: EVENT_LABELS.get(event, event) for event in event_graphs
            }
            result["explanations"] = explanations
            action_counts: Counter[str] = Counter()

            def count_node(item: dict[str, Any]) -> None:
                category = str(item.get("category") or "other")
                if category != "cosmetic":
                    action_counts[category] += 1
                for child in item.get("children") or []:
                    count_node(child.get("node") or {})

            for nodes in event_graphs.values():
                for item in nodes:
                    count_node(item)
            result["action_counts"] = dict(sorted(action_counts.items()))
        self.buff_graph_cache[cache_key] = result
        return result

    def _record_buff_graph(
        self,
        template_key: str,
        owner: str,
        full: bool = False,
    ) -> dict[str, Any]:
        graph = self._buff_graph(template_key, owner, full=full)
        if template_key not in self.unique_buff_graphs or full:
            self.unique_buff_graphs[template_key] = graph
        return graph

    def _record_inline_graph(
        self,
        host_id: str,
        node_ids: Iterable[str],
        owner: str,
        full: bool = False,
    ) -> dict[str, Any]:
        roots = [node_id for node_id in node_ids if isinstance(node_id, str)]
        if not roots:
            return {"roots": [], "reached": 0, "missing": []}
        info = self._resolve_node_list(roots, owner, full=full)
        key = f"inline:{host_id}"
        if key not in self.unique_inline_graphs or full:
            self.unique_inline_graphs[key] = {
                "host": host_id,
                "source": owner,
                **info,
            }
        return info

    # ------------------------------------------------------------------
    # prefab/component decomposition
    # ------------------------------------------------------------------
    def _component_kind(self, component: dict[str, Any]) -> str:
        return str(component.get("kind") or "Unknown")

    def _is_ability(self, component: dict[str, Any]) -> bool:
        return self._component_kind(component) in ABILITY_KINDS

    def _component_link(
        self, component_id: str, owner: str, count: bool = True
    ) -> dict[str, Any]:
        component = self.store.components.get(component_id)
        if component is None:
            if count:
                self.stats["component_refs_unresolved"] += 1
            self._add_unresolved(
                "component",
                owner,
                component_id,
                "component id not present in prefabs.jsonl",
            )
            return {
                "id": component_id,
                "exists": False,
                "kind": None,
                "path": None,
            }
        if count:
            self.stats["component_refs_resolved"] += 1
        return {
            "id": component_id,
            "exists": True,
            "kind": component.get("kind"),
            "path": component.get("path"),
            "go": component.get("go"),
            "parent_prefab": self.component_parent.get(component_id),
        }

    def _collect_component_graph_refs(
        self,
        component: dict[str, Any],
    ) -> dict[str, Any]:
        component_id = str(component.get("id") or component.get("path") or "<unknown>")
        fields = component.get("fields") or {}
        buff_descriptors: list[dict[str, Any]] = []
        node_ids: list[str] = []
        projectile_keys: list[str] = []

        for value, path in _walk(fields):
            if not isinstance(value, dict):
                continue
            if _is_buff_descriptor(value):
                buff_descriptors.append(value)
            node_ref = value.get("$node")
            if isinstance(node_ref, str):
                node_ids.append(node_ref)
            key = path[-1] if path else None
            if key in (
                "_projectileKey",
                "_additionalProjectile",
                "_extraProjectileKeys",
            ):
                if isinstance(value, str) and value:
                    projectile_keys.append(value)
                elif isinstance(value, list):
                    projectile_keys.extend(
                        item for item in value if isinstance(item, str) and item
                    )

        node_ids.extend(
            node_id
            for node_id in component.get("actionNodes") or []
            if isinstance(node_id, str)
        )
        projectile_keys.extend(
            key
            for key in component.get("projectileRefs") or []
            if isinstance(key, str) and key
        )

        def descriptor_key(item: dict[str, Any]) -> tuple[Any, ...]:
            return (item.get("buffKey"), item.get("templateKey"))

        merged_descriptors: dict[tuple[Any, ...], dict[str, Any]] = {}

        def merge_descriptor(item: dict[str, Any]) -> None:
            marker = descriptor_key(item)
            current = merged_descriptors.get(marker)
            if current is None:
                merged_descriptors[marker] = dict(item)
                return
            for field, value in item.items():
                if field not in current or current[field] in (None, ""):
                    current[field] = value

        for descriptor in buff_descriptors:
            merge_descriptor(descriptor)
        for ref in component.get("buffRefs") or []:
            if not isinstance(ref, dict) or not _is_buff_descriptor(ref):
                continue
            merge_descriptor(ref)
        buff_descriptors = list(merged_descriptors.values())
        seen_templates = {
            item.get("templateKey")
            for item in buff_descriptors
            if isinstance(item.get("templateKey"), str)
        }
        for key in component.get("buffTemplates") or []:
            if not isinstance(key, str) or not key or key == "empty":
                continue
            if key in seen_templates:
                continue
            seen_templates.add(key)
            buff_descriptors.append({"buffKey": key, "templateKey": key})
        node_ids = _dedupe(node_ids)
        projectile_keys = _dedupe(projectile_keys)

        return {
            "component_id": component_id,
            "buff_descriptors": buff_descriptors,
            "node_ids": node_ids,
            "projectile_keys": projectile_keys,
        }

    def _component_summary(
        self,
        component: dict[str, Any],
        full: bool,
        owner: str,
    ) -> dict[str, Any]:
        component_id = str(component.get("id") or component.get("path") or "<unknown>")
        cache_key = (component_id, full)
        cached = self.component_cache.get(cache_key)
        if cached is not None:
            return cached

        self.reached_components.add(component_id)
        fields = component.get("fields") or {}
        if not fields and self._component_kind(component) in (
            "TargetSelector",
            "TargetValidator",
            "TargetOptions",
            "TargetTrigger",
            "Range",
        ):
            self.stats["opaque_aux_components"] += 1
        refs = self._collect_component_graph_refs(component)
        buff_graphs: list[dict[str, Any]] = []
        buff_missing: list[str] = []
        for descriptor in refs["buff_descriptors"]:
            template_key = descriptor.get("templateKey")
            if not isinstance(template_key, str) or not template_key or template_key == "empty":
                continue
            graph = self._record_buff_graph(template_key, owner, full=full)
            graph_summary = {
                "buff_key": descriptor.get("buffKey"),
                "template_key": template_key,
                "exists": graph["exists"],
                "events": graph["events"],
                "nodes": graph["nodes"],
                "missing": graph["missing"],
            }
            if full:
                graph_summary["event_graphs"] = graph.get("event_graphs") or {}
                graph_summary["event_labels"] = graph.get("event_labels") or {}
                graph_summary["explanations"] = graph.get("explanations") or {}
            buff_graphs.append(graph_summary)
            buff_missing.extend(graph["missing"])

        inline = self._record_inline_graph(
            component_id,
            refs["node_ids"],
            owner,
            full=full,
        ) if refs["node_ids"] else {"roots": [], "reached": 0, "missing": []}

        links = [
            self._component_link(link_id, owner)
            for link_id in _dedupe(component.get("componentRefs") or [])
            if isinstance(link_id, str)
        ]
        ability_links = [
            self._component_link(link_id, owner, count=False)
            for link_id in _dedupe(component.get("abilityRefTargets") or [])
            if isinstance(link_id, str)
        ]
        for ref in _dedupe(component.get("abilityRefs") or []):
            if not isinstance(ref, str):
                continue
            if any(link.get("go") == ref for link in ability_links):
                self.stats["ability_refs_resolved"] += 1
            else:
                self.stats["ability_refs_unresolved"] += 1
                self._add_unresolved(
                    "ability",
                    owner,
                    ref,
                    "ability name does not match a captured component",
                )

        selector = fields.get("_selector")
        selector_info: dict[str, Any] = {"kind": "default", "target": None}
        if isinstance(selector, dict):
            if selector.get("$component"):
                selector_info = {
                    "kind": "resolved",
                    "target": self._component_link(
                        selector["$component"], owner, count=False
                    ),
                }
                self.stats["selector_refs_resolved"] += 1
            elif selector.get("$pathId"):
                selector_info = {"kind": "unresolved", "target": selector["$pathId"]}
                self.stats["selector_refs_unresolved"] += 1
                self._add_unresolved(
                    "selector",
                    owner,
                    str(selector["$pathId"]),
                    "selector pathId was not captured as a component",
                )
            else:
                self.stats["selector_refs_default"] += 1
        elif selector is None:
            self.stats["selector_refs_missing"] += 1

        validator = fields.get("_targetValidator")
        validator_info: dict[str, Any] = {"kind": "default", "target": None}
        if isinstance(validator, dict):
            if validator.get("$component"):
                validator_info = {
                    "kind": "resolved",
                    "target": self._component_link(
                        validator["$component"], owner, count=False
                    ),
                }
                self.stats["target_validator_refs_resolved"] += 1
            elif validator.get("$pathId"):
                validator_info = {
                    "kind": "unresolved",
                    "target": validator["$pathId"],
                }
                self.stats["target_validator_refs_unresolved"] += 1
                self._add_unresolved(
                    "target_validator",
                    owner,
                    str(validator["$pathId"]),
                    "target validator pathId was not captured as a component",
                )
            else:
                self.stats["target_validator_refs_default"] += 1
        elif validator is None:
            self.stats["target_validator_refs_missing"] += 1

        result: dict[str, Any] = {
            "id": component_id,
            "kind": self._component_kind(component),
            "path": component.get("path"),
            "go": component.get("go"),
            "is_ability": self._is_ability(component),
            "field_count": len(fields),
            "key_fields": {
                key: fields[key]
                for key in KEY_ABILITY_FIELDS
                if key in fields
            },
            "buff_refs": [
                {
                    "buff_key": descriptor.get("buffKey"),
                    "template_key": descriptor.get("templateKey"),
                    "load_from_db": descriptor.get("loadFromDB"),
                    "override_effect_key": descriptor.get("overrideEffectKey"),
                }
                for descriptor in refs["buff_descriptors"]
            ],
            "buff_graphs": buff_graphs,
            "inline_graph": inline,
            "projectiles": refs["projectile_keys"],
            "links": links,
            "ability_links": ability_links,
            "selector": selector_info,
            "target_validator": validator_info,
            "missing": sorted(set(buff_missing + inline["missing"])),
        }
        modes = fields.get("_modes")
        if isinstance(modes, list) and modes:
            self.stats["mode_components"] += 1
            self.stats["mode_entries"] += len(modes)
        extra_abilities = fields.get("_extraAbilities")
        if isinstance(extra_abilities, list) and extra_abilities:
            self.stats["extra_ability_components"] += 1
            self.stats["extra_ability_entries"] += len(extra_abilities)
        for native_field in (
            "_additionalTimes",
            "_attackType",
            "_damageType",
            "_projectileKey",
            "_isOverloadSkill",
        ):
            if native_field in fields:
                self.stats["native_ability_fields"] += 1
        for descriptor in refs["buff_descriptors"]:
            if descriptor.get("overrideEffectKey"):
                self.stats["buff_override_effect_key"] += 1
            template_key = descriptor.get("templateKey")
            if descriptor.get("audioSignal"):
                self.stats["buff_audio_only"] += 1
            if descriptor.get("loadFromDB"):
                self.stats["buff_load_from_db"] += 1
            elif not template_key or template_key == "empty":
                self.stats["buff_empty_template"] += 1
            elif template_key in self.store.buff_templates:
                self.stats["buff_template_resolved"] += 1
            else:
                self.stats["buff_template_missing"] += 1
        if full:
            result["fields"] = fields
        self.component_cache[cache_key] = result
        return result

    def _prefab_summary(
        self,
        prefab_id: str,
        full: bool,
        owner: str,
        stack: frozenset[str],
    ) -> dict[str, Any]:
        cache_key = (prefab_id, full)
        cached = self.prefab_cache.get(cache_key)
        if cached is not None:
            return cached
        if prefab_id in stack:
            return {
                "id": prefab_id,
                "exists": True,
                "cycle": True,
                "components": [],
                "missing": [],
            }

        resolved_id = prefab_id
        prefab = self.store.prefabs.get(prefab_id)
        if prefab is None:
            canonical = self.store.prefab_by_lower.get(prefab_id.lower())
            if canonical:
                resolved_id = canonical
                prefab = self.store.prefabs[canonical]
        if prefab is None:
            self._add_unresolved(
                "prefab",
                owner,
                prefab_id,
                "prefab id not present in prefabs.jsonl",
            )
            result = {
                "id": prefab_id,
                "exists": False,
                "components": [],
                "missing": [prefab_id],
            }
            self.prefab_cache[cache_key] = result
            return result

        self.reached_prefabs.add(resolved_id)
        next_stack = stack | {prefab_id}
        components: list[dict[str, Any]] = []
        missing: list[str] = []
        projectile_summaries: list[dict[str, Any]] = []
        seen_projectiles: set[str] = set()
        for component in prefab.get("components") or []:
            summary = self._component_summary(component, full, prefab_id)
            components.append(summary)
            missing.extend(summary.get("missing") or [])
            for projectile_key in summary.get("projectiles") or []:
                projectile_id = (
                    projectile_key
                    if projectile_key.startswith("projectile:")
                    else f"projectile:{projectile_key}"
                )
                if projectile_id in seen_projectiles:
                    continue
                seen_projectiles.add(projectile_id)
                projectile_summaries.append(
                    self._prefab_summary(
                        projectile_id,
                        full,
                        prefab_id,
                        next_stack,
                    )
                )

        kinds = Counter(component["kind"] for component in components)
        result = {
            "id": resolved_id,
            "requested_id": prefab_id if resolved_id != prefab_id else None,
            "exists": True,
            "prefab": prefab.get("prefab"),
            "bundle": prefab.get("bundle"),
            "components": components,
            "projectiles": projectile_summaries,
            "component_kinds": dict(sorted(kinds.items())),
            "ability_components": sum(
                1 for component in components if component["is_ability"]
            ),
            "buff_graphs": sum(len(component["buff_graphs"]) for component in components),
            "inline_graphs": sum(
                1 for component in components if component["inline_graph"]["roots"]
            ),
            "missing": sorted(set(missing)),
        }
        self.prefab_cache[cache_key] = result
        return result

    # ------------------------------------------------------------------
    # operator/skill entry points
    # ------------------------------------------------------------------
    def _skill_prefab_ids(self, skill: dict[str, Any]) -> list[str]:
        prefab_ids = [
            str(item)
            for item in skill.get("prefabs") or []
            if isinstance(item, str) and item
        ]
        if prefab_ids:
            return _dedupe(prefab_ids)
        for prefab_id in skill.get("prefabIds") or []:
            if isinstance(prefab_id, str) and prefab_id:
                prefab_ids.append(f"skill:{prefab_id}")
        if prefab_ids:
            return _dedupe(prefab_ids)
        skill_id = skill.get("skillId")
        table = self.store.skill_table.get(skill_id) if skill_id else None
        levels = (table or {}).get("levels") or []
        if levels:
            prefab_id = levels[-1].get("prefabId")
            if isinstance(prefab_id, str) and prefab_id:
                return [f"skill:{prefab_id}"]
        return []

    def resolve_operator(self, operator: dict[str, Any], full: bool = False) -> dict[str, Any]:
        operator_id = str(operator.get("id") or "<unknown>")
        character_entry = self.store.character_table.get(operator_id) or {}
        prefab_id = operator.get("prefab")
        character_prefab = (
            self._prefab_summary(str(prefab_id), full, operator_id, frozenset())
            if prefab_id
            else {"id": None, "exists": False, "missing": ["<character prefab>"]}
        )

        skill_reports: list[dict[str, Any]] = []
        skill_prefab_total = 0
        skill_prefab_resolved = 0
        skill_unresolved = 0
        for skill in operator.get("skills") or []:
            prefabs = self._skill_prefab_ids(skill)
            summaries = []
            for skill_prefab_id in prefabs:
                summary = self._prefab_summary(
                    skill_prefab_id, full, operator_id, frozenset()
                )
                summaries.append(summary)
            missing = sorted(
                {
                    item
                    for summary in summaries
                    for item in (summary.get("missing") or [])
                }
            )
            resolved = all(summary.get("exists") for summary in summaries)
            skill_prefab_total += len(prefabs)
            skill_prefab_resolved += sum(
                1 for summary in summaries if summary.get("exists")
            )
            skill_unresolved += len(missing)
            skill_reports.append(
                {
                    "skill_id": skill.get("skillId"),
                    "name": skill.get("name"),
                    "index": skill.get("index"),
                    "skill_type": skill.get("skillType"),
                    "duration_type": skill.get("durationType"),
                    "prefab_ids": prefabs,
                    "prefabs_resolved": sum(
                        1 for summary in summaries if summary.get("exists")
                    ),
                    "missing": missing,
                    "prefabs": summaries if full else [
                        {
                            "id": summary.get("id"),
                            "exists": summary.get("exists"),
                            "component_kinds": summary.get("component_kinds", {}),
                            "ability_components": summary.get("ability_components", 0),
                            "buff_graphs": summary.get("buff_graphs", 0),
                            "inline_graphs": summary.get("inline_graphs", 0),
                            "missing": summary.get("missing", []),
                        }
                        for summary in summaries
                    ],
                }
            )

        talent_component_ids: list[str] = []
        for talent in operator.get("talents") or []:
            talent_component_ids.extend(
                component_id
                for component_id in talent.get("components") or []
                if isinstance(component_id, str)
            )
        trait_component_ids = [
            component_id
            for component_id in operator.get("traitComponents") or []
            if isinstance(component_id, str)
        ]
        talent_component_ids = _dedupe(talent_component_ids)
        trait_component_ids = _dedupe(trait_component_ids)

        def component_item(component_id: str) -> dict[str, Any]:
            component = self.store.components.get(component_id)
            item = {
                "id": component_id,
                "exists": component is not None,
                "parent_prefab": self.component_parent.get(component_id),
                "kind": component.get("kind") if component else None,
            }
            if component is not None and full:
                item["component"] = self._component_summary(
                    component, True, operator_id
                )
            return item

        talent_summaries = [
            component_item(component_id) for component_id in talent_component_ids
        ]
        talent_items_by_id = {
            item["id"]: item for item in talent_summaries if item.get("id")
        }
        talent_entries: list[dict[str, Any]] = []
        if full:
            for talent in operator.get("talents") or []:
                candidates = []
                for candidate in talent.get("candidates") or []:
                    component_ids = _dedupe(
                        [
                            component_id
                            for component_id in (
                                candidate.get("components")
                                or talent.get("components")
                                or []
                            )
                            if isinstance(component_id, str)
                        ]
                    )
                    candidates.append(
                        {
                            "name": candidate.get("name"),
                            "description": candidate.get("description"),
                            "blackboard": candidate.get("blackboard") or [],
                            "required_potential_rank": candidate.get(
                                "requiredPotentialRank"
                            ),
                            "unlock_condition": candidate.get("unlockCondition") or {},
                            "prefab_key": candidate.get("prefabKey"),
                            "range_id": candidate.get("rangeId"),
                            "components": [
                                talent_items_by_id[component_id]
                                for component_id in component_ids
                                if component_id in talent_items_by_id
                            ],
                        }
                    )
                talent_entries.append(
                    {
                        "index": talent.get("index"),
                        "name": next(
                            (
                                candidate.get("name")
                                for candidate in candidates
                                if candidate.get("name")
                            ),
                            None,
                        ),
                        "prefab_keys": [
                            str(key) for key in talent.get("prefabKeys") or []
                        ],
                        "hidden": (
                            not any(
                                (candidate.get("name") or "").strip()
                                or (candidate.get("description") or "").strip()
                                for candidate in candidates
                            )
                            or (
                                bool(talent.get("prefabKeys"))
                                and all(
                                    str(key) == "-1"
                                    for key in talent.get("prefabKeys") or []
                                )
                            )
                        ),
                        "candidates": candidates,
                    }
                )
        trait_summaries = []
        for component_id in trait_component_ids:
            trait_summaries.append(component_item(component_id))

        module_summaries = []
        module_total = 0
        module_resolved = 0
        for module in operator.get("modules") or []:
            candidates = [
                candidate
                for candidate in module.get("prefabCandidates") or []
                if isinstance(candidate, str)
            ]
            module_total += len(candidates)
            resolved_candidates = []
            for candidate in candidates:
                exists = candidate in self.store.prefabs
                module_resolved += 1 if exists else 0
                item = {"id": candidate, "exists": exists}
                if exists and full:
                    item["prefab"] = self._prefab_summary(
                        candidate, True, operator_id, frozenset()
                    )
                elif exists:
                    prefab = self.store.prefabs[candidate]
                    item["component_kinds"] = dict(
                        sorted(
                            Counter(
                                component.get("kind")
                                for component in prefab.get("components") or []
                            ).items()
                        )
                    )
                resolved_candidates.append(item)
            module_summaries.append(
                {
                    "name": module.get("name"),
                    "type": module.get("type"),
                    "uni_equip_id": module.get("uniEquipId"),
                    "prefabs": resolved_candidates,
                }
            )

        missing_skill_refs = sum(
            len(skill.get("missing") or []) for skill in skill_reports
        )
        missing_talent_refs = sum(
            1 for item in talent_summaries if not item["exists"]
        )
        missing_trait_refs = sum(
            1 for item in trait_summaries if not item["exists"]
        )
        missing_module_refs = module_total - module_resolved
        core_fully_covered = (
            bool(character_prefab.get("exists"))
            and missing_skill_refs == 0
            and missing_talent_refs == 0
            and missing_trait_refs == 0
        )
        all_fully_covered = core_fully_covered and missing_module_refs == 0

        return {
            "id": operator_id,
            "name": operator.get("name"),
            "kind": operator.get("kind"),
            "appellation": operator.get("appellation"),
            "profession": operator.get("profession"),
            "character_prefab": character_prefab if full else {
                "id": character_prefab.get("id"),
                "exists": character_prefab.get("exists"),
                "component_kinds": character_prefab.get("component_kinds", {}),
                "missing": character_prefab.get("missing", []),
            },
            "skills": skill_reports,
            "talents": {
                "components_total": len(talent_summaries),
                "components_resolved": sum(
                    1 for item in talent_summaries if item["exists"]
                ),
                "items": talent_summaries,
                "entries": talent_entries,
            },
            "trait": {
                "components_total": len(trait_summaries),
                "components_resolved": sum(
                    1 for item in trait_summaries if item["exists"]
                ),
                "items": trait_summaries,
                "description": character_entry.get("description"),
                "sub_profession_id": character_entry.get("subProfessionId"),
            },
            "modules": {
                "prefabs_total": module_total,
                "prefabs_resolved": module_resolved,
                "items": module_summaries,
            },
            "coverage": {
                "skill_prefabs_total": skill_prefab_total,
                "skill_prefabs_resolved": skill_prefab_resolved,
                "missing_skill_refs": missing_skill_refs,
                "missing_talent_refs": missing_talent_refs,
                "missing_trait_refs": missing_trait_refs,
                "missing_module_refs": missing_module_refs,
                "core_fully_covered": core_fully_covered,
                "all_fully_covered": all_fully_covered,
            },
        }

    def resolve_all_operators(self, full: bool = False) -> list[dict[str, Any]]:
        reports = []
        for operator in self.store.operators:
            reports.append(self.resolve_operator(operator, full=full))
        return reports

    def resolve_buff(self, template_key: str, full: bool = True) -> dict[str, Any]:
        requested = template_key
        resolved = template_key
        if resolved not in self.store.buff_templates:
            aliases = self._buff_alias_map()
            seen: set[str] = set()
            while resolved in aliases and resolved not in seen:
                seen.add(resolved)
                resolved = aliases[resolved]
                if resolved in self.store.buff_templates:
                    break
        graph = self._buff_graph(resolved, f"buff:{requested}", full=full)
        result = dict(graph)
        result["template_key"] = resolved
        if resolved != requested:
            result["requested_key"] = requested
            result["alias_of"] = resolved
        return result

    def _buff_alias_map(self) -> dict[str, str]:
        if self._buff_aliases is not None:
            return self._buff_aliases
        aliases: dict[str, str] = {}

        def visit(value: Any) -> None:
            if isinstance(value, dict):
                buff_key = value.get("buffKey")
                template_key = value.get("templateKey")
                if (
                    isinstance(buff_key, str)
                    and isinstance(template_key, str)
                    and buff_key
                    and template_key
                    and buff_key != template_key
                ):
                    aliases.setdefault(buff_key, template_key)
                for child in value.values():
                    visit(child)
            elif isinstance(value, list):
                for child in value:
                    visit(child)

        for node in self.store.nodes.values():
            if "buff" not in str(node.get("name") or "").lower():
                continue
            visit(node.get("fields"))
        self._buff_aliases = aliases
        return aliases


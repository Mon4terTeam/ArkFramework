# Custom skill module

This directory is the standalone side of the custom-skill work. It does not
import or link `BetterBattle`, and it does not start or inject into the game.

Layout:

- `parser/`: independent static parser and coverage reports.
- `runtime/`: standalone custom skill/Buff/ActionNode loader. It is compiled
  into `CustomSkills.dll` and does not link against BetterBattle.
- `ability_spec.py`: generates `custom-create-ability` JSON from static prefab
  data, without requiring the source operator to be deployed or in the squad.
- `specs/`: example Ability specs used by the runtime tests.
- `plugin/`: the `CustomSkills` plugin entry, frame callback and lifecycle.
- `web/`: local read-only viewer for parser output.

The first component is a static parser over the extracted battle data:

```text
operator -> skill -> skill prefab -> component/Ability
         -> BuffData -> Buff template -> event -> ActionNode graph
         -> projectile prefab -> ActionAbility -> ActionNode graph
```

## Run

From `ArkFramework`:

```powershell
python -m custom.parser
python -m custom.parser --operator char_1050_chen3 --full
python -m custom.parser --skill skchr_chen3_3 --full
python -m custom.parser --prefab skill:skchr_chen3_3
python -m custom.parser --buff c2e_cold
python -m custom.parser --no-write
```

Default output directory: `custom/out`.

Regenerate the extracted graph (run from `E:\Games\ArkTest`):

```powershell
.\_tools\decompress_ark_bundles.ps1
python _tools\extract_action_graphs.py --decomp-root _analysis\bundles_decomp
```

Runtime plugin:

```powershell
cmake --build build-cmake --target CustomSkills -j 8
```

The plugin reads commands from `build-cmake\dist\CustomSkills.request` and
writes `CustomSkills.status`. Command names are the same `custom-*` commands
that were previously routed through BetterBattle.

Runtime Ability/Behaviour injection and the verified cross-operator examples
are documented in `docs/CustomAbilityInjection.md`.
The experiment matrix, special-case audit and open framework work are recorded
in `docs/CustomAbilityExperiments.md`.

The module owns its own game-thread pump. If another pump owner is already
present, it submits a game-thread tick task instead of stealing that plugin's
`Update`/`FixedUpdate` hook.

Generated files:

- `coverage.json`: full coverage report and compact operator reports
- `coverage.md`: human-readable summary
- `operators.jsonl`: one compact record per operator/token
- `skills.jsonl`: one compact record per skill
- `unresolved.json`: missing prefab/component/Buff/ActionNode references

`--full` includes serialized component fields and resolved ActionNode node
fields/children. Without it, the parser emits compact counts and references so
a whole-data run stays small.

## Boundary

- Input is the extracted graph in `_analysis/action_graphs` plus gamedata tables.
- The parser reports static structure and missing references; it does not claim
  runtime equivalence.
- Runtime-only behavior (selectors, dynamic Buffs, `loadFromDB`, mode changes,
  native Ability code) is reported separately in `needsRuntimeVerification`.
- No BetterBattle command, hook or JSON file is modified by this module.

## Self-test

```powershell
python -m custom.parser.selftest
```

## Visual viewer

The viewer follows the same “left rail + center detail + right inspector” shape
as BetterBattle Web, but is completely separate and read-only:

```powershell
python -m custom.web --open
```

Open `http://127.0.0.1:8765/` if the browser does not open automatically.

The viewer has four pages:

- `浏览`: operator/token list, skill tabs, prefab shell, Ability components,
  Buff event graphs, ActionNode field trees and projectile graphs.
- `Buff 图`: search a Buff template by key and inspect its event graph directly.
- `覆盖率`: prefab/talent/trait/module coverage, unresolved refs and component
  kind distribution.
- `运行时分类`: separates static expansion status, runtime state and genuinely
  runtime-only checks.

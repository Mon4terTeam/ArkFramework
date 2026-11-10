# BetterBattle Web UI Modules

## Load order

Scripts are classic (non-module) scripts loaded in this order:

1. static data (`assets/game/*`, `data/*`)
2. kernel modules: `protocol.js`, `buff-catalog.js`, `asset-loader.js`, `session.js`
3. page modules: `features-config.js`, `features-config-actions.js`,
   `features-squad.js`, `features-battle.js`, `features-damage.js`,
   `features-enemies.js`
4. UI components and the game-view page
5. `app.js` (shared UI state, routing, event delegation, page API aliases)
6. `timeline.js` (self-contained)

`timeline.js` owns its DOM, state and command handling. Treat it as a closed
module: changes to the other pages must not require edits inside it.

## Page module contract

Every `features-*.js` file is an IIFE and publishes one namespace:

```js
window.BetterBattlePages.<name> = {
  render,        // required
  ...            // page-specific operations
};
```

Rules:

- A page may read `window.BetterBattleSession`, `BetterBattleProtocol`,
  `BetterBattleAssets` and `BuffCatalog` directly.
- A page may call an earlier page only through `window.BetterBattlePages`.
- Later pages are reached lazily, e.g.
  `window.BetterBattlePages.damage?.renderBattlePanel(uid)`.
- `app.js` aliases page APIs to its historical local names so routing and
  event handlers stay unchanged.
- Shared UI state (`state`), DOM lookup (`dom`) and formatting helpers still
  live in `app.js`; moving them into a `core/` module is the next step.

## Page APIs

- `pages.config`: topic/entry normalization, skill levels, config rendering
  and all `set*`/`toggle*` actions (published by `features-config-actions.js`).
- `pages.squad`: stage/roster/squad state, rendering, picker operations.
- `pages.battle`: battle rendering, card/summon selection, rail/detail/buff
  presentation helpers.
- `pages.damage`: damage page and battle-side damage panel.
- `pages.enemies`: enemy page rendering plus enemy profile/stat lookups.
- `window.BetterBattleGameView`: game-view page (`init/enter/leave/render`).
- `window.BetterBattleTimeline`: frame timeline (`render/onTabActivated`).

## Verification

- `node --check` every edited script.
- Load the page and confirm the host log has no `EARLY`/`Uncaught` entries.
- Exercise each nav tab, the game-view operator/enemy selection and the
  timeline refresh path after page-boundary changes.

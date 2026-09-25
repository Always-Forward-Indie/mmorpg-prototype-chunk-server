# MobAI plan docs — reconciliation with code/DB (2026-09-22)

The three root docs (`MOB_AI_IMPROVEMENT_PLAN.md` = V1,
`MOB_AI_IMPROVEMENT_PLAN_V2.md` = V2, `MOB_SYSTEM_PLAN.md`) contradict
each other and the implementation. This file pins what is actually true
(verified against `src/` + live DB on 2026-09-22); the root docs are
kept as history and must not be used as build instructions.

## Verdicts

| # | Claim | Reality |
|---|---|---|
| 1 | V2 header says "V1 реализован" | False. V1 checklist is all `[ ] не начато`, and most V1 items were never built as designed. What exists instead: `MobAIController` + `MobAIFormulas` (threat as ints with decay, no V1 `map<int,float>` table). **Update 2026-09-24:** EVADING since landed (`MobAIController.cpp:928-954`: 2s invulnerability on reaching spawn, then back to PATROLLING) + RETURNING leash regen 10%/s — V1 items §3.1/§3.2 now exist in simplified form (no V1 `mobHealthUpdate` packet / `isEvading` wire flag). |
| 2 | V1 threat decay "5%/сек" | Wrong unit. Code: `threatDecayFactor = 0.95^(dt*10)` (`MobAIFormulas.hpp:155`) ≈ −40%/s, not −5%/s. Comments saying "50%/halved" are also inaccurate. |
| 3 | V2 `chase_duration` default 30 | Wrong. DB default is **10.0** (`mob.chase_duration`), code fallback `10.0f` (`DataStructs.hpp:518`). |
| 4 | V1 migration `011_mob_ai_config` | Superseded. Real migrations are `013–029` range; `011/012` never applied as written. Per-mob AI tuning lives in `mob` table columns + `game_config`, not V1's proposed columns. |
| 5 | V1/V2 `fleeHpThreshold` / `aiArchetype` / `patrolRadius` per-mob tuning | Parsed from game-server JSON but **lost in copies** — flee never fired, every mob behaved MELEE. Fixed in chunk `MobManager` + `SpawnZoneManager` (propagate fields on template copy) + regression pin `AiFieldsSurviveTemplateCopy`. Behavior unchanged in prod (DB values are all defaults) — the feature is unblocked, content tuning is open. |
| 6 | V2 "skill-driven combat timing, social aggro, passive mobs" | Partially true: combat timing reads skill data; social aggro / passive archetypes exist in code paths but were unreachable before fix #5 (all MELEE). |
| 7 | MOB_SYSTEM depth (rank/champion interplay) | Rank/champion evolution implemented via `ChampionManager` + survival evolution tick. **Stacking resolved 2026-09-25 (add-then-multiply):** single combined multiplier — champion bonus ADDS to template rank (`rankMult = template.rankMult + (hpMult − 1)`), HP/mana scaled once by it. Normal template (1.0) unchanged; XP uses the same `rankMult`, so HP/XP stay consistent. |

## Numeric ground truth (code/DB, not docs)

- `chase_duration` default: **10.0** (DB + code).
- Threat decay: `0.95^(10·dt)` per second ≈ **×0.60/s** (−40%/s).
- Flee duration: `chaseDuration / 3.0` (`MobAIController.cpp:961`).
- Absolute reputation snapshot every 60s bounds delta loss (chunk `repSnapshotTask`, `scheduler.rep_snapshot_ms`); unrelated to AI but bounds outbox staleness.

## Fixed (this repo history)

- Flee/archetype/patrol propagation through template copies + `AiFieldsSurviveTemplateCopy` pin.
- Chunk→game reconnect: infinite capped backoff 5…60s (was `exit(1)` after 5 tries).

## Open (content tuning, not code)

- Which mobs are casters (`aiArchetype` values in DB) — game-design decision.
  Flee content done 2026-09-25 (small beasts 0.3, migration 092).
- V1 items never built (leash-regen packet, spatial index for players — the latter superseded by InterestManager subscriptions). EVADING + RETURNING regen landed 2026-09-24 (see row 1).

## Rule

New MobAI work cites this file + code, never the V1/V2 headers. If a root doc's claim is needed, verify against `src/services/MobAIController.cpp`, `MobAIFormulas.hpp`, and the `mob` table first.

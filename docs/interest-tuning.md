# Interest v2 tuning (spatial subscriptions)

Чанк рассылает mob-update'ы не всем подряд, а подписчикам ячеек сетки
(`InterestManager`). Fail-open: нетреканные/грузящиеся клиенты и выключенный
`interest.enabled` получают всё (как legacy broadcast).

## Ключи (`game_config`, живьём при старте + `reload`)

| Ключ | Default | Смысл |
|------|---------|-------|
| `interest.cell_size` | 1500.0 | Размер ячейки в world units |
| `interest.view_radius` | 1 | Радиус подписки в ячейках (1 = 3×3 вокруг якоря) |
| `interest.rehome_margin_frac` | 0.15 | Гистерезис рехоума якоря (доля ячейки, anti-jitter на границах) |
| `interest.enabled` | true | Мастер-свитч (false = legacy broadcast-all) |
| `interest.snapshots` | true | Enter-снапшоты для новых ячеек |
| `interest.snapshot_cooldown_ms` | 2000 | Не чаще одного снапшота на клиента в N мс |

Сиды: миграция `085`. Код: `InterestManager.hpp:80-85` (`kDefault*`),
`ChunkServer.cpp:63-69` (configure при старте).

## События подписки (wire)

- Клиент уходит из ячейки → `mobCellLeft` (evict; клиент дропает мобов ячейки).
- Re-subscribe/thin-delta при рехоуме; enter-snapshot с cooldown.
- 10s-саммари чанка: `Interest: clients/cells/subs/resubscribes/tickMobs/...`.

## Связь с mob tick

`mob_tick_ms` (default 50) — resolution планировщика; реальное движение
гейтится per-mob (`nextMoveTime`, chase/return-интервалы), broadcast —
per-mob rate-limit 100/200/500/1000мс по состояниям. Детали тика — в
`mob-ai-movement-protocol.md`.

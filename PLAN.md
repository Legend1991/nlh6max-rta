# Pluribus Blueprint Plan (Functional vs Scale)

Status legend: `DONE`, `IN_PROGRESS`, `PENDING`, `BLOCKED`, `PARTIAL`

## Re-Plan v2 Status (2026-03-02)

- `DONE` Blueprint format bump to `v7` with persisted scheduler state:
  - `next_discount_second`
  - `next_snapshot_second`
  - `discount_events_applied`
- `DONE` Time-exact discount scheduling path (event cadence by elapsed seconds) wired into training loop.
- `DONE` Warmup-gated controls:
  - `--warmup-seconds`
  - `--snapshot-start-seconds`
  - `--avg-start-seconds`
  - profile defaults set to `48000` seconds.
- `DONE` Pluribus profile schedule defaults tightened:
  - discount every `600s`, stop at `24000s`
  - pruning starts at `12000s`
  - snapshot cadence `12000s`.
- `DONE` Finalization filtering:
  - `--snapshot-min-seconds` option
  - finalize reports skipped snapshot reasons.
- `DONE` Run script updated to v2 production semantics:
  - train uses 600s discount cadence, prune start 12000s, warmup/snapshot start 48000s
  - finalize uses `--snapshot-min-seconds 48000`.
- `DONE` TDD coverage additions:
  - time-discount cadence/stop test
  - snapshot-min-seconds finalize filtering test.
- `DONE` Search leaf-policy/off-tree-injection parity pass.
  - round-structured leaf stop now triggers on betting-round transition (in addition to depth cap).
  - frozen observed actions now replay through exact legal injection first, with pseudo-harmonic fallback only when exact replay is unavailable.
  - scalar and vector search traversals both honor the same injected frozen-action path.
- `DONE` Additional memory-allocation parity optimizations (smaller eager allocation profile).
  - eager blueprint init capacities reduced (`nodes/hash/actions`) with deterministic growth preserved.
  - startup footprint regression test added.

## Primary Goal

Target: spend ~12,400 CPU core-hours on a 32-core machine (about 16 days) to compute a blueprint as close as practically possible to public Pluribus design constraints.

Core-hours math:
- 32 cores * 16 days * 24h/day = 12,288 core-hours
- To reach ~12,400 core-hours exactly at 32 cores, wall time is ~16.15 days.

## Sources (Ground Truth)

- https://par.nsf.gov/servlets/purl/10119653
- https://noambrown.github.io/papers/19-Science-Superhuman.pdf
- https://noambrown.github.io/papers/19-Science-Superhuman_Supp.pdf

## Track A: Functional Parity (Algorithmic Shape)

This track answers: "Do we have the same kinds of mechanisms?"

- `DONE` External-sampling CFR+ style trainer with Pluribus-like controls (`--pluribus-profile`, pruning knobs, regret floor/int mode).
- `DONE` 6-max no-limit game logic and off-tree translation support.
- `DONE` Blueprint/query/search compatibility hashing and resumable checkpoints.
- `DONE` Postflop abstraction artifacts, separate blueprint/search bucket modes.
- `DONE` Nested unsafe search scaffold with continuation model (`k=4`) and multi-thread search path.
- `DONE` Deterministic seeded parallel blueprint behavior (including 24-thread deterministic regression).

Track A result: functional parity is mostly present.

## Track B: Scale Parity (Engineering + Throughput + Capacity)

This track answers: "Can we run Pluribus-like compute budget and not collapse under scale?"

### B0. Current Gaps (Why not yet "close enough")

- `DONE` Memory/capacity:
  - dynamic infoset growth is implemented (no fixed soft-cap truncation).
  - compact node payload storage removed fixed per-node 16-action arrays.
  - regrets are int32 and serialized in compact variable-length payloads.
- `PARTIAL` Throughput:
  - 32-thread efficiency is workload-sensitive; light workloads still scale poorly.
  - remaining bottlenecks identified:
    - serial delta-prep stage between train and merge (`precreate + delta transform + partition build`).
    - hard train/merge phase barrier with no overlap.
    - static per-worker iteration split can leave tail imbalance.
- `DONE` Schedule fidelity:
  - strict wall-clock phase mode runs persisted time-phase checks with bounded parallel chunking.
- `DONE` Long-run operational tooling:
  - robust 16-day orchestration with persisted progress accounting and phase transitions is in place.

### B1. Long-Run Orchestration (16-day runability)

Status: `DONE`

Implemented:
- automated chunked run loop via `run_pluribus_16d_32c.bat`.
- persistent progress accounting in `data\run_pluribus_16d_32c.state`.
- periodic checkpoints/snapshots/logging in `data\run_pluribus_16d_32c.log` + snapshot dir.
- phase control for early discount window vs late no-discount window.
- strict resume compatibility in early phase; compatibility override only in post-boundary transition path.
- fixed end-of-train finalize path to avoid full in-memory blueprint clone (prevents `Final blueprint copy failed` on large states).

Acceptance gate:
- can run unattended in slices and resume after interruption without manual option recomposition.

### B2. Capacity Expansion Beyond Current Soft Cap

Status: `DONE`

Implemented:
- replaced fixed-size infoset storage with dynamic growing sparse structures.
- kept deterministic serialization/loading and compatibility behavior.
- validated growth beyond initial 262,144-node baseline in tests.

Acceptance gate:
- state growth continues far beyond initial fixed baseline without truncation.

### B3. Discount/Pruning Cost at Scale

Status: `DONE`

Implemented:
- lazy LCFR discounting via deferred per-node scaling stamps.
- removed global full-table discount sweeps from training schedule boundaries.
- checkpoint/snapshot paths materialize deferred scaling before persistence.

Acceptance gate:
- no full-table discount sweep on schedule boundaries.

### B4. Parallel Update Strategy Experiments (Determinism vs Throughput)

Status: `DONE`

Implemented:
- added explicit trainer/bench control: `--parallel-mode deterministic|sharded`.
- retained deterministic path (worker-local overlays + deterministic serial reduction) as default.
- implemented alternative sharded path (worker-local overlays + deterministic sharded parallel merge).
- optimized sharded merge with per-worker pre-partitioned shard index lists (avoids re-scanning full touched lists per merge shard).
- added Windows tests:
  - sharded mode deterministic repeatability.
  - deterministic-vs-sharded policy-state equivalence on fixed seeded workload.
  - shard partition stability/completeness regression.
- benchmarked both modes on this hardware:
  - `bench --iters 320 --samples-per-player 64 --chunk-iters 4000 --max-threads 24`
    - deterministic: `24t = 24.54 it/s` (`2.08x` vs 1t baseline in that run)
    - sharded: `24t = 20.84 it/s` (`1.56x` vs 1t baseline), but `16t = 34.95 it/s`
  - production-like trainer profile:
    - `train --pluribus-profile --iters 2000 --chunk-iters 50000 --samples-per-player 1`
    - deterministic: `1t=954.59 it/s`, `24t=1468.24 it/s`, `32t=1400.07 it/s`
    - sharded: `1t=929.21 it/s`, `24t=1651.69 it/s`, `32t=1628.72 it/s`
  - strict-time production-like path after chunk + merge optimizations:
    - `train --pluribus-profile --iters 20000 --chunk-iters 50000 --samples-per-player 1 --strict-time-phases`
    - `1t=352.76s`, `24t=29.30s` (`12.04x` speedup)
- selected `sharded` in `run_pluribus_16d_32c.bat` for the current 32-core production path.

Acceptance gate:
- documented choice with measured tradeoffs on this hardware.

### B6. Delta-Prep Bottleneck Reduction (Deterministic)

Status: `DONE`

Implemented:
- moved worker-local delta transform (`worker -= base`) into worker thread immediately after train traversal phase.
- moved worker-local shard partition build into worker thread for sharded mode.
- reduced main-thread handoff to serial global-node precreation only (required for thread-safe open-address table insertion).
- added worker task-failure propagation and fail-fast checks in parallel coordinator.
- preserved deterministic merge semantics; full determinism test suite remains green.
- fixed large-blueprint checkpoint/resume reliability:
  - long-run script now forces `--no-async-checkpoint` (avoids full-blueprint clone for async save at large states).
  - blueprint load now runs a payload prepass and pre-allocates nodes/hash/payload capacities to avoid repeated huge reallocs during resume.

Measured effect (this machine):
- strict-time profile (`train --pluribus-profile --iters 20000 --chunk-iters 50000 --samples-per-player 1 --strict-time-phases`):
  - `1t=252.44s`, `24t=28.56s` (`8.84x`)
- `bench --iters 240 --chunk-iters 50000 --samples-per-player 1 --parallel-mode sharded`:
  - `24t=1871.22 it/s` (`7.64x` vs 1t),
  - `32t=1566.01 it/s` (`6.39x` vs 1t).

### B5. Abstraction Fidelity Ramp

Status: `DONE`

Implemented:
- selected frozen production abstraction artifact: `data\abstraction_prod_pluribus_v1.bin`.
- production config locked to:
  - seed `20260301`
  - blueprint buckets `200/200/200` (flop/turn/river)
  - search buckets `500/500/500` (flop/turn/river)
  - `--mc-samples 32`
  - `--samples 50000`
  - `--kmeans-iters 64`
- deterministic reproducibility check passed:
  - rebuilt twin artifact `data\abstraction_prod_pluribus_v1_repro.bin`
  - `fc /b` binary comparison identical.
- 16-day runner switched to this frozen artifact path/config.

Acceptance gate:
- frozen abstraction artifact hash for full production run, with documented config.
- current frozen hash: `0xD5B800C8`.

## Hard Benchmark Gates (Scale Track)

All gates measured on this machine, release build, fixed seed.

### G1. Determinism Gate

- `DONE` Repeated `--threads 24` runs with identical config produce bit-identical blueprint files.

### G2. Multi-thread Efficiency Gate

- `DONE` For at least one production-like heavy workload, `--threads 24` exceeds `--threads 1` by >=1.2x.
- current observed:
  - `bench` heavy config:
    - deterministic: `24` vs `1` = `2.08x`
    - sharded: `24` vs `1` = `1.56x`
  - `train --pluribus-profile`:
    - deterministic: `24` vs `1` = `1.54x`
    - sharded: `24` vs `1` = `1.78x`
  - strict-time profile (`--iters 20000`, sharded): `24` vs `1` = `12.04x`
  - light workloads can still favor single-thread.

### G3. Long-run Stability Gate

- `PENDING` 24h continuous training smoke with periodic checkpoints/snapshots and zero crashes/corruption.

### G4. 16-day Orchestration Gate

- `DONE` scripted progression toward 12,288-12,400 core-hours with persisted progress and phase switching.

## Immediate Next Actions (Scale Priority)

1. `DONE` Dedicated 32-core 16-day training script (chunked + resumable + phase-aware) with frozen production abstraction.
2. `DONE` Implement lazy LCFR discounting (remove global discount sweeps).
3. `DONE` Dynamic sparse infoset growth migration.
4. `PENDING` Run 24h stability burn and publish checkpoint/snapshot integrity report.

## Definition of "As Close as Possible" (for this repo)

For this project, "close to Pluribus blueprint" is defined by:
- same broad algorithmic family and schedule behaviors.
- long-run compute budget executed reliably on available hardware.
- deterministic and resumable training pipeline.
- abstraction/search setup frozen and documented for reproducible full-budget runs.

This definition explicitly requires both Track A and Track B; Track A alone is not sufficient.

# Re-Plan: Pluribus-Closest Parity Pass (Blueprint-First, Breaking Changes Allowed, Deterministic)

## Summary
This plan re-targets the project to match public Pluribus details as closely as practical, with priority on blueprint fidelity first, then search fidelity, while preserving deterministic reproducibility.

## Execution Status (2026-03-02)

- `DONE` Phase 1: compact sparse node payload refactor is implemented.
  - `CFRNode` now stores compact header + payload offsets/pointers.
  - dynamic contiguous action payload storage is used (no fixed per-node 16-action arrays).
  - regrets are `int32_t` in memory and on disk.
  - blueprint format bumped to v6 with explicit reject of older versions.
- `DONE` Phase 2: two-stage postflop pipeline is enforced.
  - `train` now produces raw blueprint + snapshots only.
  - `finalize-blueprint` builds postflop from snapshot-average of current strategy.
  - in-train postflop averaging flags/path were removed.
- `DONE` Phase 3: wall-clock phase persistence/defaults are implemented and resumable with strict wall-clock scheduling and bounded parallel chunks in strict-time mode.
- `DONE` Phase 4:
  - deterministic scalar/vector switch rule is implemented and tested.
  - k=4 continuation remains implemented.
  - nested round-root flow now rebuilds round-root and replays current-round observed actions as frozen prefix in `search`.
  - belief/range update alignment is integrated for frozen observed action replay at round-root boundaries.
- `DONE` Phase 5:
  - deterministic weighted-distance clustering + k-means++ centroid init implemented.
  - action template shape aligned (broad preflop, coarser turn/river, search capped to 1-6 raises).
- `DONE` Phase 6: long-run script/docs now reflect train -> finalize production flow.

Public-paper deltas found in current codebase that need correction:
1. Postflop strategy handling: resolved via train+snapshot then `finalize-blueprint`.
2. Memory/storage heaviness: resolved with compact node payloads and dynamic action storage.
3. Schedule fidelity: resolved in strict-time mode via persisted wall-clock phase state with bounded chunk stepping.
4. Search parity: round-root nested replay + deterministic scalar/vector switch + belief updates integrated.
5. Abstraction parity: bucket counts + deterministic weighted clustering + action-template constraints integrated.

Ground-truth references used for this re-plan:
- https://noambrown.github.io/papers/19-Science-Superhuman.pdf
- https://noambrown.github.io/papers/19-Science-Superhuman_Supp.pdf
- https://par.nsf.gov/servlets/purl/10119653

---

## Scope And Decisions Locked
1. Priority: **Blueprint parity first**.
2. Compatibility policy: **Breaking changes allowed** (blueprint/abstraction/checkpoint version bumps are permitted).
3. Determinism policy: **Determinism required** (no nondeterministic shared-atomic update path as the default parity path).
4. 24h burn-in remains out-of-scope for this pass (kept pending as final validation gate).

---

## Current-State Gap Audit (Decision-Complete)
1. Correct and keep:
- 6-max game logic, side pots, off-tree pseudo-harmonic translation, canonical hand indexing.
- 200/200/200 blueprint and 500/500/500 search bucket capability.
- k=4 continuation leaf model.
- resumable long-run script and frozen abstraction artifact flow.

2. Must change:
- Postflop averaging pipeline.
- Node storage layout and memory representation.
- Phase scheduler semantics and persisted phase state.
- Search nested/root semantics and scalar/vector switch rule.
- Abstraction action-template and clustering-distance fidelity details.

---

## Implementation Plan

## Phase 1: Blueprint Storage/Memory Parity Refactor
1. Replace current per-node fixed arrays with compact sparse layout.
- File targets: `src/core.c`, `src/poker_state.c`.
- Replace `CFRNode` fixed `float regret[CFR_MAX_ACTIONS]` and `float strategy_sum[...]` with:
  - compact node header (`key`, `street_hint`, `action_count`, offsets),
  - dynamic contiguous action payload arrays.
- Regret representation:
  - store regret in `int32_t` in-memory and on-disk (true integer regrets).
  - keep floor clamp `-310000000`.
- Strategy representation:
  - keep preflop average accumulator.
  - remove postflop in-memory average accumulator from main training state.
- Expected result:
  - significantly lower memory per active postflop infoset.
  - no fixed-action-array waste for low-action nodes.

2. Blueprint format version bump.
- File targets: `src/core.c`, `src/poker_state.c`, `src/commands.c`.
- Bump blueprint version (new major internal layout).
- Add migration policy:
  - old versions explicitly rejected with clear error.
  - no backward load compatibility required (per locked decision).

3. Dynamic sparse behavior preservation.
- Keep open-addressed key index and dynamic growth behavior.
- Preserve deterministic insertion and serialization order.

## Phase 2: Postflop Snapshot-Current Strategy Pipeline (Paper-Faithful)
1. Change snapshot semantics.
- File targets: `src/commands.c`, `src/cfr_trainer.c`.
- At snapshot time, write postflop **current strategy** (regret-matching policy) instead of cumulative average.
- Keep preflop average strategy in memory as before.

2. Introduce explicit finalization command.
- File targets: `src/main.c`, `src/commands.c`, `src/cli_utils.c`.
- New command: `finalize-blueprint`.
- Behavior:
  - input: raw training blueprint + snapshot directory.
  - output: final playable blueprint with:
    - preflop from in-memory average,
    - postflop from snapshot-average-of-current-strategy.
- This matches public Pluribus description more closely than current postflop handling.

3. Snapshot cadence fidelity.
- Default production cadence: every `12000` seconds (200 minutes).
- Keep configurable override but default to paper-faithful value in production scripts.

## Phase 3: Schedule Fidelity Hardening (Time-Accurate)
1. Add explicit wall-clock phase scheduler state to training state.
- File targets: `src/core.c`, `src/commands.c`, `src/cfr_trainer.c`.
- Persist:
  - elapsed training seconds,
  - discount phase status,
  - pruning phase status.
- Resume restores exact phase state without recomputation ambiguity.

2. Lock paper-like defaults in `--pluribus-profile`.
- Linear CFR discount enabled only for first `24000` seconds.
- Pruning threshold `-300000000`.
- 95% skip behavior represented deterministically via cadence (`full_every=20`) rather than random drift.
- Snapshot cadence default 200 minutes.

3. Keep deterministic behavior across restarts.
- Phase transitions must occur at same wall-clock thresholds regardless of chunk boundaries.

## Phase 4: Search Parity Upgrade (After Blueprint Core)
1. Enforce nested round-root solving semantics.
- File targets: `src/search_engine.c`, `src/commands.c`.
- Solve from start of current betting round, not from arbitrary mid-round point state.
- Reconstruct/maintain round-root state and apply observed actions consistently.

2. Tighten scalar/vector switching rule.
- Replace heuristic threshold switching with rule-based switch aligned to paper intent:
  - scalar MCCFR path for large/early-round subgames,
  - vector path for smaller/later-round subgames.
- Keep deterministic branching condition.

3. Keep and validate k=4 continuation.
- Retain continuation set: blueprint, fold-biased, call-biased, raise-biased.
- Ensure leaf evaluation path uses this set consistently.

4. Belief/range update alignment.
- Strengthen Bayes update flow so observed action history and blockers are applied at round-root boundaries consistently.

## Phase 5: Abstraction Fidelity Improvements
1. Action abstraction templates.
- File targets: `src/poker_state.c`.
- Align templates/caps to paper-described shape:
  - blueprint with broad preflop, coarser turn/river,
  - search with up to 1-6 raise sizes depending on state.

2. Information abstraction clustering distance.
- File targets: `src/abstraction_config.c`.
- Keep current feature vector (`E[HS]`, `E[HS^2]`, potential, positive potential proxy), but replace/augment assignment distance and centroid initialization to better approximate paper-reported bucket quality behavior.
- Keep deterministic seed-stable build path.

3. Artifact freezing refresh.
- Build and freeze new production abstraction artifact/hash after refactor.
- Update run scripts and runbook to this new frozen artifact.

## Phase 6: Tooling And Operational Flow
1. Production script update.
- File targets: `run_pluribus_16d_32c.bat`, `README.md`, `RUNBOOK.md`.
- New production sequence:
  - train raw blueprint with snapshot-current enabled,
  - run `finalize-blueprint`,
  - optionally verify compatibility hash and determinism checks.

2. Diagnostics and observability.
- Extend status logging:
  - phase (`discount-on`/`discount-off`),
  - snapshot count used for postflop averaging,
  - memory active/allocated by component.

---

## Public API / Interface Changes
1. CLI additions.
- `finalize-blueprint` command.
- `train` additions for explicit snapshot-current mode and phase persistence options (if needed by implementation).
- `train` profile defaults updated to paper-faithful schedule constants.

2. CLI behavior changes.
- `--snapshot-postflop-avg` current behavior replaced by explicit two-stage train/finalize semantics.
- `--pluribus-profile` becomes time-phase strict by default.

3. Binary format changes.
- Blueprint format version bump required.
- Old blueprint load may be intentionally rejected (breaking by design).
- If abstraction format changes are required by clustering updates, bump abstraction version too.

---

## Test Plan (Must Be Added/Updated)
1. Storage and serialization.
- Roundtrip for new blueprint format.
- Reject old incompatible blueprint versions with clear message.
- Deterministic serialization hash stability across repeated identical runs.

2. Postflop snapshot pipeline.
- Unit test: postflop final strategy equals average of snapshot-current strategies.
- Unit test: preflop strategy in finalized blueprint equals in-memory preflop average path.
- Unit test: finalize command is deterministic with fixed input snapshots.

3. Schedule fidelity.
- Simulated-time tests for phase transitions at exact thresholds:
  - discount on before 24000s,
  - off at/after 24000s.
- Snapshot timing tests at exact 12000s cadence.
- Resume tests that preserve phase state exactly.

4. Parallel determinism and throughput sanity.
- Determinism tests for `1/24/32` threads with `sharded` mode.
- Deterministic equivalence across chunk partition changes.
- Keep existing deterministic-vs-mode invariants where applicable.

5. Search parity behavior.
- Nested round-root solve regression tests.
- Scalar/vector switch rule tests based on explicit subgame conditions.
- k=4 continuation policy output sanity tests.

6. End-to-end.
- Train short run -> finalize -> query/search/match smoke.
- Resume across interruption -> finalize -> query/search smoke.

---

## Acceptance Criteria
1. Blueprint parity acceptance.
- Postflop strategy in final artifact is snapshot-average-of-current-strategy, not in-memory cumulative average.
- Preflop remains average-strategy based.
- Phase transitions are wall-clock faithful and resume-safe.

2. Scale acceptance.
- Memory footprint on heavy benchmark reduced materially versus current baseline (target: >=30% lower active memory at similar infoset count).
- Deterministic `sharded` multi-thread path preserved.
- 16-day script supports full flow including finalization step.

3. Search acceptance.
- Nested round-root semantics and deterministic scalar/vector switching implemented and tested.

---

## Rollout Order
1. Implement Phase 1 + Phase 2 first, run tests, and regenerate artifacts.
2. Implement Phase 3 and update long-run script semantics.
3. Implement Phase 4 + Phase 5.
4. Refresh docs and runbook.
5. Re-run benchmark suite and update `PLAN.md` with measured deltas.

---

## Assumptions And Defaults
1. Backward compatibility with old artifacts is intentionally not preserved.
2. Determinism remains a hard requirement for production runs.
3. Frozen abstraction and blueprint artifacts will be regenerated after format/scheduler changes.
4. 24h stability burn remains deferred until this parity pass is complete.

# Pluribus Parity Re-Plan v2 (Post-Audit, Blueprint-First)

## Summary
Current code is solid on foundations (6-max engine, CFR+ core, snapshots/finalize flow, search scaffold, determinism), but it is still not as close to public Pluribus behavior as it can be.  
Main parity gaps are schedule semantics, warmup handling, pruning timing, search leaf policy/off-tree handling, and memory allocation strategy.

Sources used for this audit:
1. https://noambrown.github.io/papers/19-Science-Superhuman_Supp.pdf
2. https://noambrown.github.io/papers/19-Science-Superhuman.pdf
3. https://par.nsf.gov/servlets/purl/10119653

---

## Gap Audit (What Is Still Wrong or Incomplete)
1. Discount schedule is iteration-driven in practice, but paper behavior is time-driven (10-minute cadence, first 400 minutes only).
2. Pruning starts by iteration threshold now, but paper behavior starts at 200 minutes and uses threshold `-300,000,000` with ~95% skip regime.
3. Snapshot/finalize timing is not warmup-faithful: paper indicates post-warmup usage (initial 800-minute stage handling differs from current all-run behavior).
4. Preflop averaging window is not explicitly warmup-gated to paper timing.
5. Search leaf policy is still depth-based; paper policy uses round-structured leaf cut rules.
6. Observed off-tree actions are translated, not injected/re-solved from round root as closely as paper search flow.
7. Memory engineering is improved but still not parity-grade due large eager base allocation and per-node payload shape not fully optimized for postflop sparsity at scale.
8. Abstraction distance quality is still approximate versus paper-style stronger distributional comparisons.

---

## Locked Decisions
1. Priority remains blueprint parity first, then search parity.
2. Breaking artifact compatibility is allowed.
3. Deterministic mode remains required for production parity path.
4. 24h burn-in stays deferred for this pass.

---

## Implementation Plan

## Phase 1: Time-Exact Schedule Semantics
1. Introduce strict wall-clock scheduler state with persisted event clocks, not iteration approximations.
2. Add persisted fields for next discount event, next snapshot event, prune-enabled state, average-enabled state, and warmup-complete state.
3. Set paper-faithful profile defaults:
   - discount cadence every 600 seconds
   - discount active only before 24,000 seconds
   - pruning starts at 12,000 seconds
   - prune threshold `-300000000`
   - snapshot cadence 12,000 seconds
   - warmup gate 48,000 seconds for postflop snapshot/finalization usage
4. Keep iteration knobs for non-profile mode, but profile must be time-authoritative.
5. Bump blueprint format version and reject older versions with explicit error.

## Phase 2: Warmup-Faithful Strategy Pipeline
1. Gate preflop average accumulation by warmup policy (paper-faithful window).
2. Gate postflop snapshot collection by warmup policy.
3. Finalizer averages only eligible postflop current-strategy snapshots.
4. Add strict compatibility filtering in finalizer: abstraction hash + compat hash + schedule mode.
5. Emit finalize report with counts: scanned snapshots, eligible snapshots, skipped snapshots by reason.

## Phase 3: Memory/Storage Parity Upgrade
1. Replace large eager initial node/hash allocation with small initial capacities and deterministic growth.
2. Split strategy payload policy by street:
   - preflop keeps average storage
   - postflop raw training avoids unnecessary average payload maintenance
3. Keep int32 regret storage and floor clamp semantics.
4. Keep deterministic serialization order and stable hashing.
5. Add memory acceptance gate: at matched infoset count, active+allocated memory should improve materially versus current baseline.

## Phase 4: Search Parity Corrections
1. Replace pure depth-only leaf control with round-structured leaf rules aligned to public description.
2. Keep nested solving from round root and enforce deterministic replay of observed current-round actions.
3. Implement off-tree observed action injection path:
   - if observed action is missing from current abstraction branch, add branch and re-solve from round root
   - keep pseudo-harmonic translation only as fallback where injection is not applicable
4. Keep k=4 continuation set and standardize bias transform constants across code path.
5. Tighten scalar/vector switch rule to explicit deterministic criteria tied to subgame size and street.

## Phase 5: Abstraction Fidelity Upgrade
1. Keep bucket counts (`200/200/200` blueprint, `500/500/500` search) and deterministic build.
2. Replace current weighted-L2-only clustering distance with stronger distribution-aware distance approximation (EMD-style proxy).
3. Keep deterministic k-means++ init and deterministic assignment order.
4. Align action-template caps to paper shape per street/context and lock templates in profile.
5. Freeze new abstraction artifact and hash after rebuild.

## Phase 6: Throughput and MT Parity Engineering
1. Keep deterministic sharded mode as default production path.
2. Optimize merge path for reduced contention and better cache locality.
3. Add optional experimental shared-update mode behind explicit flag (not default parity path).
4. Add production-profile benchmark gate at 24 threads and 32 threads with fixed seed and fixed workload.

## Phase 7: Runbook/Operational Parity
1. Update production flow to strict sequence:
   - train raw
   - snapshot archive
   - finalize blueprint
   - deterministic verification
2. Update run scripts and docs with paper-faithful defaults and new scheduler knobs.
3. Add transcripted smoke run in runbook for train -> finalize -> query -> search.

---

## Public API / Interface Changes
1. `train` gains explicit time-schedule controls:
   - `--discount-every-seconds`
   - `--warmup-seconds`
   - `--snapshot-start-seconds`
   - `--avg-start-seconds`
2. `--pluribus-profile` becomes fully time-authoritative and sets all schedule defaults above.
3. `finalize-blueprint` gains strict snapshot eligibility checks and summary output.
4. Search command gains explicit leaf-policy switch and off-tree action handling mode.
5. Blueprint format version bump required; older blueprint versions intentionally rejected.

---

## Test Plan
1. Scheduler tests:
   - exact discount on/off boundaries at 24,000s
   - exact pruning start at 12,000s
   - exact snapshot cadence at 12,000s
   - restart/resume invariance at boundary crossings
2. Warmup pipeline tests:
   - preflop averaging starts only at configured warmup gate
   - postflop snapshots before gate are excluded from finalize
3. Finalizer tests:
   - deterministic output on identical inputs
   - compatibility mismatch rejection behavior
4. Memory tests:
   - startup allocation regression test
   - active/allocation ratio regression under synthetic growth
5. Search tests:
   - round-structured leaf stopping behavior
   - off-tree action injection and re-solve behavior
   - scalar/vector rule determinism
6. End-to-end smoke:
   - short train -> finalize -> query -> search, deterministic hash repeat.

---

## Acceptance Criteria
1. Schedule fidelity:
   - profile run behavior is time-exact, not iteration-approximate.
2. Strategy fidelity:
   - final postflop strategy comes from eligible snapshot-current average only.
   - preflop strategy follows gated average-policy window.
3. Search fidelity:
   - nested round-root + structured leaf policy + off-tree injection are active and tested.
4. Scale fidelity:
   - memory footprint improves versus current baseline at similar infoset counts.
   - deterministic 24-thread and 32-thread production-profile runs pass reproducibility checks.
5. Operational readiness:
   - docs/scripts reflect complete production flow with new format versions.

---

## Assumptions and Defaults
1. Deterministic production path remains mandatory.
2. Backward compatibility with prior blueprint formats is intentionally dropped for parity upgrades.
3. Frozen abstraction and blueprint artifacts will be regenerated.
4. 24h stability burn is still deferred and remains the first post-parity validation gate.

# Re-Plan v3 (Paper-Fidelity Delta Pass, 2026-03-02)

## Re-Plan v3 Status (2026-03-02)

- `DONE` V3-1 Preflop/postflop averaging semantics:
  - `--pluribus-profile` now keeps preflop average active from start (`avg_start_seconds=0`).
  - warmup gate remains for snapshot/finalize postflop path.
- `DONE` V3-2 Search policy semantics:
  - search action pick supports `sample-final|argmax`; profile/default is `sample-final`.
- `DONE` V3-3 Belief source parity:
  - Bayes update now uses latest available search sigma policy and falls back to blueprint policy.
- `DONE` V3-4 Leaf-rule parity pass:
  - explicit preflop chance-node stop and multiway-flop second pot-increase stop added (with depth guard).
- `DONE` V3-5 Off-tree mode separation:
  - `inject|translate` mode added and wired through search/match paths.
- `DONE` V3-6 Runtime RTA integration:
  - match harness now supports `--mode search` per-decision nested solve.
- `DONE` V3-7 Frozen-history parallel search:
  - multi-thread search path now supports frozen history too; deterministic tests added.
- `DONE` V3-8 Abstraction distance upgrade:
  - clustering distance now uses weighted-L2 + EMD-style cumulative-difference proxy.
- `DONE` V3-9 Memory follow-through:
  - node payload storage is street-split in practice: postflop nodes in raw training blueprints omit persistent `strategy_sum` payload.
  - blueprint format bumped to `v8` with per-node payload flags (`has_strategy`) and persisted storage mode flag.
  - periodic snapshots now use compact postflop-current-strategy binary format (`CFR_SNAPSHOT_MAGIC`), and finalize consumes both compact and legacy snapshot formats.
  - TDD added for missing-postflop-payload blueprint roundtrip and compact snapshot save/load accumulation.

## Why Re-Plan v3
After re-reading public Pluribus sources:
1. https://noambrown.github.io/papers/19-Science-Superhuman_Supp.pdf
2. https://noambrown.github.io/papers/19-Science-Superhuman.pdf
3. https://par.nsf.gov/servlets/purl/10119653

there are still important parity gaps that are implementable in this repo.

Key paper anchors used for this pass:
- Supp Algorithm 1/notes: postflop snapshots of current strategy every 200 minutes, preflop average strategy path, pruning threshold/floor/schedule, dynamic regret allocation, int32 regrets.
- Supp Algorithm 2/notes: nested round-root search, off-tree action handling with re-solve from round root, final-iteration action policy for play, k=4 continuation set.
- Main paper text: compute budget and hardware profile, abstraction/search framing.

## Current Gap Audit (v3)
1. `DONE` Preflop averaging semantics: profile keeps preflop averaging active from time 0.
2. `DONE` Search action selection parity: sampled-final strategy is supported and default in profile paths.
3. `DONE` Belief source parity: Bayes update uses latest search sigma when available, with blueprint fallback.
4. `DONE` Leaf policy parity: explicit round-structured stopping rules are implemented.
5. `DONE` Off-tree handling parity modes: `inject|translate` are separated and wired through search/match.
6. `DONE` Runtime RTA integration: match supports per-decision nested search mode.
7. `DONE` Frozen-history search multithreading: parallel search path supports frozen histories.
8. `DONE` Abstraction fidelity upgrade: weighted-L2 + EMD-style cumulative-difference proxy distance implemented.
9. `DONE` Memory follow-through: postflop strategy payload suppression + compact snapshot format implemented.

## Re-Plan v3 Scope
1. Priority remains blueprint/search behavior parity before any new benchmark campaign.
2. Breaking artifact compatibility remains allowed.
3. Determinism remains default production path.
4. 24h burn remains deferred until v3 functional parity items are done.

## Phase V3-1: Fix Preflop/Postflop Averaging Semantics
1. Make `--pluribus-profile` preflop averaging always active from time 0.
2. Keep postflop averaging disabled in-train and continue snapshot-current finalize path.
3. Keep postflop snapshot eligibility gate at warmup horizon for finalization path.
4. Add tests:
   - preflop avg active before 48,000s in profile
   - postflop still finalize-from-snapshots only.

## Phase V3-2: Search Policy Semantics Parity
1. Change action pick in search from argmax-final to sampled-final strategy.
2. Preserve deterministic reproducibility by seeded sampler in decision output.
3. Expose optional `--search-pick argmax|sample-final` flag for debugging, with profile default = `sample-final`.
4. Add tests:
   - deterministic sampled decision under fixed seed
   - argmax debug mode unchanged.

## Phase V3-3: Belief Update Sigma Source Parity
1. Maintain per-round search strategy state (`sigma`) in `CFRSearchContext`.
2. Use latest available search strategy for Bayes likelihood when present; fallback to blueprint otherwise.
3. Add explicit compatibility behavior for first decision in round (no prior sigma).
4. Add tests:
   - belief update source fallback logic
   - repeated same-round decisions change belief using persisted search sigma.

## Phase V3-4: Leaf Policy Rule Parity
1. Implement explicit round-structured leaf rules:
   - preflop: leaf at next chance node
   - flop with >2 players: leaf at next chance node or after second pot-increasing action (whichever first)
   - later/smaller subgames: leaf at hand end or explicit depth guard.
2. Keep `--depth` as safety cap only, not primary semantics in profile mode.
3. Add tests for each rule branch.

## Phase V3-5: Off-Tree Injection + Re-Solve Parity
1. Add search branch-injection mode:
   - if observed action is not represented in current limited action set, inject it as legal branch for this subgame and re-solve from round root.
2. Keep pseudo-harmonic mapping only as fallback mode.
3. Add CLI knob:
   - `--offtree-mode inject|translate` with profile default `inject`.
4. Add tests:
   - injected branch exists in solved policy support
   - translation fallback behavior preserved when inject is disabled.

## Phase V3-6: Runtime RTA Integration
1. Add search-enabled match/runtime path that mirrors Pluribus-style decision loop:
   - nested round-root re-solve each acting decision
   - use final-iteration sampled policy for action choice
   - persist per-round search context and beliefs.
2. Keep current blueprint-only match as baseline mode.
3. Add tests:
   - smoke hand progression under search-enabled match
   - deterministic replay with fixed seed and identical options.

## Phase V3-7: Search Throughput Parity for Frozen Histories
1. Remove `frozen_count==0` restriction for parallel search iterations.
2. Ensure frozen-history replay path is thread-safe and deterministic in merge.
3. Benchmark gate:
   - show `threads 24` gain on frozen-history workloads representative of real decisions.

## Phase V3-8: Abstraction Fidelity Upgrade (Search-First)
1. Add stronger distribution-aware distance proxy (EMD-style approximation) for clustering assignment.
2. Add optional per-flop-family centroiding path for search abstraction build.
3. Keep deterministic centroid initialization and assignment order.
4. Regenerate and freeze new abstraction artifact/hash.

## Phase V3-9: Memory Parity Follow-Through
1. Split payload storage by street usage:
   - preflop nodes retain average payload
   - postflop raw blueprint nodes may omit persistent `strategy_sum` payload when not needed.
2. Add compact snapshot format for postflop current-strategy dumps to cut I/O and memory pressure.
3. Keep deterministic serialization and compatibility hashing.

## V3 Acceptance Criteria
1. Search parity:
   - sampled final-iteration action selection in profile mode.
   - sigma-based belief updates across same-round decisions.
   - explicit leaf-rule tests pass.
2. Off-tree parity:
   - inject-and-resolve mode active by default in profile.
3. Runtime parity:
   - search-enabled match path available and deterministic.
4. Scale parity:
   - frozen-history search parallelism shows measurable gain at 24 threads.
   - postflop payload memory reduced versus current v3 baseline at matched infoset count.
5. Operational parity:
   - docs and scripts updated to new profile defaults and options.

## Immediate Next Task (recommended)
Implement V3-1 and V3-2 first, then re-run deterministic smoke:
1. `train --pluribus-profile` short run
2. `finalize-blueprint`
3. `search` sampled-final decision check
4. update README/RUNBOOK with changed defaults.

# Re-Plan v4 (Paper-Alignment Corrections, 2026-03-03)

Ground truth re-checked:
1. https://noambrown.github.io/papers/19-Science-Superhuman_Supp.pdf
2. https://noambrown.github.io/papers/19-Science-Superhuman.pdf
3. https://par.nsf.gov/servlets/purl/10119653

## Re-Plan v4 Execution Status (2026-03-03)

- `DONE` V4-1 warmup/profile defaults (`avg_start_seconds=48000`, `snapshot_start_seconds=48000`, `warmup_seconds=48000`).
- `DONE` V4-2 AddAction-like inject support in search (legal support augmentation + frozen-prefix materialization).
- `DONE` V4-3 deterministic scalar/vector first-decision rule.
- `DONE` V4-4 runtime trigger mode (`--runtime-profile pluribus`) for match.
- `DONE` V4-5 weighted-average sigma for belief updates while keeping sampled-final play policy.
- `DONE` V4-6 sampled preflop average-update mode (`--preflop-avg-mode sampled`) and compatibility hashing.
- `DONE` abstraction distance/clustering upgraded to deterministic histogram-EMD + k-medoids pipeline.

## v4 Gap Audit
1. `DONE` Profile default for preflop average warmup is paper-aligned.
   - Paper: preflop average strategy starts after initial `800` minutes.
   - Code default in `--pluribus-profile`: `avg_start_seconds=48000`.
2. `DONE` Off-tree inject path now includes AddAction-like support materialization.
   - Injected non-template raises are accepted directly in inject mode when legal by game constraints.
   - Frozen-prefix nodes are materialized with injected action support in subgame solve state.
3. `DONE` Search scalar/vector switching rule is deterministic and paper-style.
   - Paper: MCCFR variant on first decision in early rounds; vector/chance-sampled variant otherwise.
   - Code uses deterministic first-decision gate (`preflop/flop + frozen_count==0 -> scalar`, else vector).
4. `DONE` Runtime search trigger policy has paper-like mode.
   - Paper: preflop search trigger depends on large off-tree action and active-player cap.
   - Match supports `--runtime-profile pluribus` with preflop trigger proxy and postflop default search.
5. `DONE` Belief sigma source uses weighted-average sigma path.
   - Paper notes sigma update from weighted average while playing final-iteration action policy.
   - Action pick remains sampled-final from current strategy.
6. `DONE` Preflop average update procedure supports sampled action-counter mode.
   - Paper Update-Strategy uses sparse sampled action-counter updates at cadence.
   - Code supports `--preflop-avg-mode sampled` and profile defaults to sampled.
7. `DONE` Abstraction parity pass implemented.
   - Deterministic histogram profile extraction (24 bins) added for postflop states.
   - Bucket assignment now supports EMD-based medoid distance (`--cluster-algo emd-kmedoids` default).
   - Centroid build path now computes medoids + EMD quality metrics (intra/separation) and persists them in abstraction artifact `v3`.

## v4 Execution Plan

## Phase V4-1: Restore Paper-Faithful Warmup Defaults
1. Set `--pluribus-profile` default `avg_start_seconds=48000`.
2. Keep `snapshot_start_seconds=48000` default.
3. Add test: profile defaults reflect `avg_start=48000`, `snapshot_start=48000`, `warmup=48000`.
4. Update README/RUNBOOK production examples accordingly.

## Phase V4-2: True AddAction Off-Tree Injection
1. Extend subgame action representation so observed off-tree action can be explicitly added for the current public state family.
2. Ensure injected action appears in legal support during nested solve, not only as a frozen replay transition.
3. Keep `translate` mode as fallback/debug path.
4. Add tests:
   - injected off-tree action appears in search policy support after re-solve,
   - inject/translate deterministic parity checks.

## Phase V4-3: Paper-Style Scalar/Vector Switch Rule
1. Replace heuristic switch with explicit rule keyed to:
   - first decision of preflop/flop subgame -> scalar MCCFR variant,
   - otherwise -> vector/chance-sampled variant.
2. Persist needed round/decision metadata in search context.
3. Add tests covering each branch deterministically.

## Phase V4-4: Runtime Trigger Policy Parity
1. Add `--runtime-profile pluribus` mode in `match`.
2. Implement preflop trigger: only run nested search when off-tree action-size condition and active-player cap are met; otherwise use blueprint.
3. Keep existing `--mode search` as explicit override.
4. Add smoke tests for trigger decisions.

## Phase V4-5: Sigma/Average Strategy Alignment in Search
1. Maintain explicit weighted-average sigma buffer in search context for belief updates.
2. Keep action choice from final-iteration strategy (`sample-final`) for play.
3. Add tests:
   - belief updates use weighted sigma when available,
   - final action pick remains sampled-final from current strategy.

## Phase V4-6: Preflop Update-Strategy Shape Alignment
1. Introduce preflop action-counter accumulator (integer) updated on cadence.
2. Implement sampled Update-Strategy traversal path for preflop-only average artifact.
3. Convert counters to policy at query/finalization time.
4. Add regression tests vs current preflop policy stability metrics.

## v4 Acceptance Criteria
1. `DONE` Profile defaults match paper warmup semantics (`avg_start=48000`, `snapshot_start=48000`).
2. `DONE` Off-tree inject mode includes AddAction-like injected support materialization in nested solve.
3. `DONE` Scalar/vector switching follows deterministic paper-style round/decision rule.
4. `DONE` Match runtime supports paper-like trigger policy mode.
5. `DONE` Belief updates read weighted sigma path; action pick remains sampled-final.
6. `DONE` New tests pass; determinism tests (`1/24/32`, deterministic/sharded invariants) remain green.

# Re-Plan v5 (Paper Parity Closure, 2026-03-04)

Ground truth re-checked:
1. https://noambrown.github.io/papers/19-Science-Superhuman_Supp.pdf
2. https://noambrown.github.io/papers/19-Science-Superhuman.pdf
3. https://par.nsf.gov/servlets/purl/10119653

## v5 Gap Audit
1. `DONE` Preflop runtime search trigger upgraded from proxy.
   - Paper: preflop search only when opponent raise size is more than `$100` away from any blueprint raise size and active players `<= 4`.
   - Code now computes last-opponent preflop raise distance to nearest abstract raise and gates on `diff > 100` with `active_players <= 4`.
2. `DONE` Nested-search freezing policy aligned to own-action freezing.
   - Paper: solve from round root; freeze only our already chosen actions for our actual hand; do not freeze opponent action probabilities.
   - Code now uses full observed sequence for belief/replay, but only own realized actions are hard-frozen in solve traversals.
3. `DONE` Scalar/vector switch upgraded to deterministic size-aware rule.
   - Paper: variant choice depends on subgame size and game part.
   - Code now switches by round + active players + legal branching/raise count + observed-prefix depth proxy.
4. `DONE` Search action cap tightened.
   - Paper: subgame abstraction usually has no more than five raise actions.
   - Code now caps raises to five in subgame limiter while preserving injected observed raises.
5. `DONE` Abstraction-distance follow-through implemented in current deterministic pipeline.
   - Enhanced histogram profile scoring and tail/potential-aware EMD distance weighting were added to `emd-kmedoids` assignment.

## Phase V5-1: Exact Preflop Trigger Semantics
1. Replace proxy trigger with explicit off-tree-distance trigger:
   - detect last opponent preflop raise size in current round,
   - compute nearest blueprint raise size at that public state,
   - trigger preflop search only if absolute difference `> 100` and active players `<= 4`.
2. Keep postflop runtime default search unchanged.
3. Add tests:
   - no trigger when difference `<= 100`,
   - trigger when difference `> 100`,
   - disabled when active players `> 4`.

## Phase V5-2: Freeze-Policy Alignment
1. Refactor frozen-history handling in search to distinguish:
   - `our_frozen_actions` (must stay fixed for our actual hand),
   - observed opponent/public history context (used for belief/state consistency, not hard-frozen policy path).
2. Preserve round-root re-solve and AddAction-like injected support.
3. Add tests:
   - repeated same-round solve preserves our previous realized action constraints,
   - opponent strategy above current decision is not hard-frozen by replay path.

## Phase V5-3: Size-Aware Scalar/Vector Rule
1. Introduce deterministic size proxy (public-state branching and active-player/round factors) for variant selection.
2. Use rule:
   - large or early subgames -> Monte Carlo Linear CFR path,
   - smaller/later subgames -> vector path.
3. Add tests covering all branches with fixed seed determinism.

## Phase V5-4: Search Action-Set Fidelity
1. Tighten search raise-cap limiter from six to five raises per decision in profile path.
2. Keep injected observed off-tree action always included when legal.
3. Add regression tests:
   - max raise actions <= 5 in normal profile path,
   - injected off-tree raise retained even if cap pressure exists.

## Phase V5-5: Abstraction Distance Follow-Through
1. Keep `emd-kmedoids` as default deterministic pipeline.
2. Improve feature profile to closer paper-style potential-aware distributions:
   - refine histogram generation and potential proxies,
   - validate with quality metrics (`intra`, `sep`) and stability checks.
3. Regenerate frozen abstraction artifact/hash and update runbook.

## v5 Acceptance Criteria
1. `DONE` Runtime preflop search trigger matches paper-like condition (`> $100`, `<= 4 players`) in runtime profile path.
2. `DONE` Nested search freeze behavior aligns with paper intent (own realized actions frozen; opponent path observed via beliefs/replay).
3. `DONE` Scalar/vector selection uses deterministic size-aware rule (not only frozen-count heuristic).
4. `DONE` Search abstraction action cap is paper-like (`<= 5` raises typical path, plus injected support retention).
5. `DONE` Determinism and existing test suite remain green after changes (`80 tests, 0 failures`; `build.bat` green).

# Re-Plan v6 (Large Blueprint Runtime on 64GB RAM, 2026-03-04)

## Problem Statement

Expected full-budget blueprint size can approach hundreds of GB (Pluribus-scale artifacts are publicly reported around this order of magnitude).  
Current runtime path (`search` / `search-server`) loads full blueprint into RAM, which is incompatible with a 64GB host once artifact size grows very large.

## Goal

Enable practical real-time search on a 64GB machine with very large finalized blueprints by splitting training and runtime artifacts and making runtime disk-backed.

## Locked Decisions

1. Keep full training artifact unchanged in spirit (contains regrets and resume state).
2. Add separate runtime artifact optimized for serving queries (policy-only).
3. Runtime path must not require full in-memory load of policy artifact.
4. Determinism stays a hard requirement.
5. Backward compatibility with old runtime artifact versions is optional (explicit version bump allowed).

## v6 Gap Audit

1. `DONE` Runtime serving path now supports policy-only runtime artifacts via `--runtime-blueprint`.
2. `DONE` `finalize-blueprint` can now emit policy-only serving artifacts.
3. `DONE` Windows mmap/disk-backed runtime loader is implemented.
4. `DONE` Runtime path includes bounded decoded-policy cache, prefetch, and `stats` observability.
5. `PARTIAL` Large-artifact RAM/latency gates are implemented in code path and smoke-tested, but not yet validated on a true multi-hundred-GB artifact.

## Phase V6-1: Runtime Artifact Format (Policy-Only)

1. Define `CFR_RUNTIME_BLUEPRINT_VERSION` with explicit file magic/header.
2. Store only serving fields per node:
   - infoset key,
   - action count,
   - street hint,
   - canonical action count/order-compatible policy payload,
   - final policy payload (quantized).
3. Remove regrets/resume fields from runtime artifact.
4. Quantization modes:
   - default `uint16` probabilities (recommended),
   - optional `uint8` for higher compression.
5. Add explicit decode rules and checksum/hash metadata.

Status: `DONE`

## Phase V6-2: Finalize Pipeline Extension

1. Extend `finalize-blueprint` to optionally emit runtime artifact:
   - `--runtime-out <file>`
   - `--runtime-quant u16|u8`
   - `--runtime-shards <N>` (or auto strategy).
2. Preserve existing finalized blueprint output for compatibility/debug.
3. Build runtime artifact from finalized policy (after snapshot averaging), not from raw training state.

Status: `DONE`

## Phase V6-3: Disk-Backed Runtime Loader

1. Implement Windows memory-mapped loader for runtime artifact.
2. Add shard index:
   - street-based + hashed key ranges,
   - O(1)/logN lookup metadata resident in RAM.
3. Keep node payload on disk-backed pages; avoid full `malloc` of policy table.
4. Add fallback path when mmap unavailable (explicit error or reduced mode).

Status: `DONE`

## Phase V6-4: Hot Cache + Prefetch

1. Add bounded RAM cache for decoded policy blocks (LRU or clock).
2. Prefetch likely hot shards:
   - preflop,
   - common flop families,
   - recent-query locality.
3. Add cache observability:
   - hit rate,
   - resident bytes,
   - page fault/load counters.

Status: `DONE`

## Phase V6-5: Search Integration

1. Add runtime policy provider abstraction in search:
   - in-memory provider (existing path),
   - mmap runtime provider (new path).
2. Wire `search-server` to runtime artifact mode:
   - `--runtime-blueprint <file>` preferred for large serving deployments.
3. Keep current behavior when `--blueprint` is used.
4. Ensure deterministic action sampling remains unchanged with provider swap.

Status: `DONE`

## Phase V6-6: Validation + Gates

1. Correctness:
   - policy equivalence tolerance test (`finalized` vs `runtime` decode).
   - deterministic repeated query decisions with fixed seed.
2. Capacity:
   - large artifact load must keep resident RAM below configured ceiling (target: `< 48GB` on 64GB host).
3. Latency:
   - warm-query target: `1-5s` budgeted solve path in `search-server`.
   - cold-query metrics captured separately.
4. Throughput:
   - compare queries/sec between full in-memory and mmap modes on medium artifact.

Status: `PARTIAL`

## CLI/API Changes (Planned)

1. `finalize-blueprint`:
   - `--runtime-out <file>`
   - `--runtime-quant u16|u8`
   - `--runtime-shards <N>`
2. `search-server`:
   - `--runtime-blueprint <file>` (disk-backed serving path)
3. Optional diagnostics:
   - `--cache-bytes <N>`
   - `--prefetch <mode>`

Status: `DONE`

## v6 Acceptance Criteria

1. Runtime artifact can be served without full in-memory blueprint load.
2. 64GB machine can run `search-server` against very large policy artifact without OOM.
3. Warm-query latency is compatible with RTA workflow (target 1-5s in configured budgeted mode).
4. Deterministic behavior preserved vs existing seeded semantics.
5. Documentation updated with production serving workflow for large artifacts.

Status: `PARTIAL`

Implemented notes:

1. `DONE` Runtime artifact format:
   - `CFR_RUNTIME_BLUEPRINT_MAGIC` / `CFR_RUNTIME_BLUEPRINT_VERSION`
   - policy-only nodes with `key`, `action_count`, `street_hint`, quantized payload
   - `u16|u8` quantization and stored content hash
2. `DONE` Finalize integration:
   - `finalize-blueprint --runtime-out <file> --runtime-quant u16|u8 --runtime-shards <N>`
3. `DONE` Runtime loader:
   - Windows memory-mapped loader
   - street-sharded on-disk index (`street * buckets_per_street + hash(key)`)
   - no full in-memory policy table load required for serving
4. `DONE` Hot path:
   - bounded decoded-policy cache (`--cache-bytes`)
   - prefetch modes (`--prefetch none|auto|preflop`)
   - `search-server` `stats` command for cache/prefetch counters
5. `DONE` Search integration:
   - provider abstraction for search leaf policy / belief source
   - `search` and `search-server` accept `--runtime-blueprint`
   - in-memory `--blueprint` behavior preserved
6. `PARTIAL` Validation:
  - unit coverage added for runtime save/load and CLI parse
  - live smoke verified: `train -> finalize-blueprint --runtime-out -> search -> search-server --runtime-blueprint`
  - full large-artifact RAM ceiling / warm-latency acceptance still needs production-scale measurement

# Re-Plan v7 (Post-v9 Audit Against Public Pluribus Sources, 2026-03-06)

Ground truth re-checked:
1. https://noambrown.github.io/papers/19-Science-Superhuman_Supp.pdf
2. https://noambrown.github.io/papers/19-Science-Superhuman.pdf
3. https://par.nsf.gov/servlets/purl/10119653

## v7 Review Result

Current codebase is still broadly aligned with the public Pluribus shape:
- blueprint-side Linear MCCFR-style schedule with pruning and int32 regret storage
- postflop snapshot-current averaging offline
- blueprint/search bucket split (`200/200/200` and `500/500/500`)
- nested round-root search with `k=4` continuation set
- preflop runtime trigger condition and postflop default nested search behavior

However, remaining actionable gaps are now mostly engineering-scale rather than missing whole algorithm families.

## v7 Gap Audit

1. `PARTIAL` Blueprint throughput architecture is still weaker than public Pluribus engineering scale.
   - Current trainer still uses worker-local overlays with chunk barriers and a reconcile/merge phase.
   - Public papers describe the end result at a much larger throughput/capacity point; exact MT internals are not public.
   - In this repo, remaining low-CPU tails indicate memory-bound reconcile cost still dominates at large infoset counts.

2. `PARTIAL` Large-blueprint startup/load cost is improved but still not fully production-proven.
   - `v9` removed the payload prepass and now does direct linear load + one hash build.
   - This should materially cut resume/startup time, but it still needs measured validation on the next fresh production-scale artifact.

3. `PARTIAL` Search/RTA latency parity is not yet proven on production-scale artifacts.
   - Runtime artifact serving path exists.
   - Warm-query `1-5s` behavior is still not validated on a real long-run artifact with production search settings.

4. `PARTIAL` Abstraction fidelity remains the closest practical deterministic approximation, not a guaranteed replication of the exact paper pipeline.
   - Current pipeline uses deterministic histogram-EMD / k-medoids style clustering.
   - Public papers do not disclose enough detail to guarantee exact reconstruction of the original feature pipeline and clustering implementation.

5. `PARTIAL` Runtime-profile preflop trigger in the match harness still uses an estimated observed raise target path.
   - This is close to paper intent for a self-play/runtime trigger proxy, but not a perfect raw bet-size replay channel.
   - Search/search-server user-facing paths already accept explicit live state inputs directly, so this is narrower than a core blueprint/search mismatch.

6. `PENDING` Validation/docs debt after the `v9` format bump.
   - Fresh long-run smoke with new `v9` raw blueprint format
   - Runtime serving smoke on fresh `v9 -> finalize -> runtime artifact`
   - README/RUNBOOK/HANDOFF consistency sweep

## v7 Execution Plan

### V7-1: Reconcile/Merge Throughput Refactor
1. Reduce or eliminate chunk-tail reconcile bottlenecks in `src/cfr_trainer.c`.
2. Priorities:
   - avoid repeated base-node lookup work across phases
   - reduce full-worker barrier cost
   - move more reconcile work off the main thread or into shard-owned deterministic phases
3. Acceptance:
   - visibly shorter low-CPU tail per chunk
   - better `iters/sec_recent` at high infoset counts on the same machine

Status: `PARTIAL_DONE`

Implemented in this pass:
- worker delta-prep now skips repeated base hash lookup for overlay nodes that already know their base origin
  - packed overlay-base index metadata is carried in the in-memory node `used` bitfield (no extra per-node memory footprint)
- delta-prep no longer materializes/syncs base nodes in place just to compute the worker delta
  - scaled base values are reconstructed read-only from base node payload + discount stamp
- merge application path already uses raw find-node lookup where create/sync are not needed
- sparse worker reset now clears touch marks sparsely instead of full-array memset each chunk
- precreate phase still only creates genuinely new global nodes
- `touched_mark` storage is now a packed bitset instead of one byte per node
  - lowers per-blueprint bookkeeping memory and reduces bulk memset/copy traffic in trainer/search paths
- sampled opponent postflop traversal now uses read-only lookup (`create=0`) unless that street is actually accumulating strategy
  - missing read-only postflop opponent nodes no longer inflate worker overlays; they fall back to uniform regret-matching from zero regret
- trainer now marks nodes as touched only on actual mutation paths
  - traverser regret-update nodes
  - preflop-average accumulation nodes
  - read-only sampled opponent postflop nodes are no longer inserted into the touched set

Still remaining:
- chunk-tail idle period is materially reduced, but overall throughput is still memory-bound at high infoset counts
- reconcile/merge phase still has hard barriers and can remain memory-bandwidth limited at large infoset counts
- next likely gains need deeper changes in large-table traversal/cache behavior, not more small scheduling fixes

Measured acceptance evidence (user-run, same machine, 2026-03-06):
- before this pass, at `iter=180000`:
  - `elapsed_sec=449`
  - `iters/sec_recent=204.80`
  - `iters/sec_avg=399.94`
  - `infosets=419285254`
  - `mem_active_mb=25027.53`
- after this pass, at `iter=180000`:
  - `elapsed_sec=334`
  - `iters/sec_recent=375.16`
  - `iters/sec_avg=537.31`
  - `infosets=145116307`
  - `mem_active_mb=8537.59`
- observed deltas at `180k`:
  - `recent it/s`: about `+83%`
  - `avg it/s`: about `+34%`
  - wall time to `180k`: about `-26%`
  - infoset count: about `-65%`
  - active memory: about `-66%`
- interpretation:
  - acceptance criterion `visibly shorter low-CPU tail per chunk` is satisfied
  - acceptance criterion `better iters/sec_recent at high infoset counts on the same machine` is satisfied
  - biggest gain appears to come from no longer creating/touching read-only sampled opponent postflop nodes

Follow-up implemented after those measurements (`V7-1b`):
- compacted `CFRNode` layout while preserving pointer-based access semantics
  - `action_count` narrowed to `uint8_t`
  - fields reordered to reduce hot node footprint without changing behavior
- `DONE` generic flat-hash experiment implemented and then reverted
  - the flat `CFRHashEntry` generic key index was implemented and benchmarked
  - it did not recover throughput after the dense-preflop rollback
  - the hot generic table is now back on the older split sparse index layout (`key_hash_keys` + `key_hash_node_plus1`)
- validation after `V7-1b`:
  - `build.bat` passes
  - `test.bat` passes (`91 Tests, 0 Failures`)
- measured trainer result after the dense-preflop experiment:
  - `180k` baseline before these two changes:
    - `elapsed_sec=317`
    - `iters/sec_recent=342.94`
    - `iters/sec_avg=566.63`
    - `infosets=138,177,288`
    - `mem_alloc_mb=18,720`
    - `mem_active_mb=7,076.83`
  - `180k` after dense preflop store + flat hash:
    - `elapsed_sec=369`
    - `iters/sec_recent=229.66`
    - `iters/sec_avg=487.96`
    - `infosets=141,082,723`
    - `mem_alloc_mb=25,568`
    - `mem_active_mb=7,225.82`
- measured trainer result after the corrective sparse-inside-block redesign:
    - `elapsed_sec=551`
    - `iters/sec_recent=252.56`
    - `iters/sec_avg=325.82`
    - `infosets=151,222,556`
    - `mem_alloc_mb=20,768`
    - `mem_active_mb=7,739.28`
- interpretation:
    - both dense-preflop variants were functionally correct, but both regressed throughput materially
    - the eager `169`-slot design over-allocated
    - the sparse-inside-block redesign reduced allocation pressure, but added enough per-access overhead to make wall-clock throughput even worse
- user benchmark result for the trainer-side infoset cache was slightly negative on throughput
  - at `180k`: `iters/sec_recent` dropped from `375.16` to `352.91`
  - at `180k`: `iters/sec_avg` dropped from `537.31` to `515.32`
  - memory and infoset count improved modestly, but not enough to offset the added cache overhead
- response:
  - reverted the trainer-side infoset cache path
  - kept the safe node-layout compaction and the structural wins
  - reverted the flat generic hash-table refactor
  - reverted the dense-preflop specialization line entirely
  - blueprint format bumped again to `v11`
  - recorded both dense-preflop specialization and flat generic hash as failed throughput branches on this hardware
- measured trainer result after both rollbacks (current kept baseline):
  - `180k` after reverting dense-preflop specialization and flat generic hash:
    - `elapsed_sec=336`
    - `iters/sec_recent=384.56`
    - `iters/sec_avg=528.35`
    - `infosets=137,670,856`
    - `mem_alloc_mb=18,720`
    - `mem_active_mb=7,049.85`
- interpretation after rollback:
  - catastrophic throughput regressions from the dense-preflop and flat-hash branches are gone
  - current hot-path performance is back near the earlier good regime
  - memory footprint is effectively back to baseline
  - indexing-layout experimentation is closed for now; further work should target proven locality wins rather than more hash/preflop redesign churn
Next realistic throughput targets after `V7-1b`:
- `MEDIUM_HIGH` stronger hot/cold node split
  - rationale:
    - current `CFRNode` is improved but still heavier than ideal for the traversal hot path
    - longer-term target is to keep only hot metadata in the main node table and avoid storing derived/cold fields there
  - expected value:
    - lower memory bandwidth pressure on large blueprints
- `DONE` Decision: stop touching indexing layout for now
  - rationale:
    - the dense-preflop specialization branch regressed throughput in both tested variants
    - the flat generic hash-table refactor also failed to outperform the older split sparse index on this hardware
    - the rollback benchmark recovered near-baseline behavior, which is the strongest current evidence for keeping the split sparse key index
  - current kept layout:
    - generic sparse key index
    - split sparse hot table (`key_hash_keys` + `key_hash_node_plus1`)
- `MEDIUM` separate preflop and postflop storage/indexing strategies
  - rationale:
    - the two regions still have different access patterns, but dense-preflop specialization should only be revisited with a substantially different design
  - expected value:
    - cleaner specialization only if a future design proves itself on throughput instead of just on structure
- `MEDIUM` Windows thread affinity / physical-core pinning
  - rationale:
    - may reduce scheduler noise and improve run-to-run stability on 24-thread production settings
  - expected value:
    - secondary gain and better measurement stability, but not expected to beat the data-structure wins above

Not currently worth more time:
- more tiny traversal caches
- more chunk-size micro-tuning
- more small merge-scheduler tweaks
- more indexing-layout experiments of the same kind (dense preflop / flat generic hash)
- mixing production scripts with benchmark-only controls

### V7-2: Fresh v11 Long-Run Validation
1. Start a new raw blueprint with the current binary and frozen abstraction.
2. Measure:
   - startup/resume time
   - training throughput trend
   - checkpoint cadence correctness
3. Acceptance:
   - `v11` startup materially better than old `v10` behavior
   - no resume/load regressions

### V7-3: Runtime Search Validation
1. Produce a fresh runtime artifact from a `v11` run.
2. Measure `search-server` warm-query latency with representative postflop states.
3. Acceptance:
   - practical RTA path stays within configured budget on warm queries

### V7-4: Documentation Consistency Sweep
1. Update README/HANDOFF/RUNBOOK/examples/scripts to current `v11` state.
2. Remove stale `v8` references where they imply current compatibility.
3. Acceptance:
   - no misleading artifact/version guidance remains in the main operational docs

## v7 Conclusion

No new major algorithm-family mismatch was found relative to the public papers.

The remaining important gaps are:
- throughput/engineering scale
- production-scale validation
- exact abstraction-pipeline unknowables not disclosed publicly

So the correct next plan is not another wholesale algorithm redesign.  
It is a focused engineering pass on trainer throughput and production-scale validation.

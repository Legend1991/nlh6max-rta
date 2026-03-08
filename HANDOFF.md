# HANDOFF

Compact snapshot for context-compaction recovery.

## Current State

Project is in single-translation-unit MSVC `/TC` mode with production abstraction freeze and B4 parallel-mode implementation integrated.

Latest delta (V7-1b rollback/correction pass, 2026-03-07):
- blueprint format bumped to `v11`; existing old raw/final blueprint files are intentionally incompatible.
- reverted the flat generic `CFRHashEntry` key index refactor after negative throughput benchmarks.
- the hot generic infoset table is back on the older split sparse layout:
  - `key_hash_keys`
  - `key_hash_node_plus1`
- reverted the dense-preflop specialization line after negative throughput benchmarks:
  - preflop now uses the same generic sparse key/index path again
  - dense public-state block storage is no longer in the hot path
- `cfr_blueprint_load` still uses direct linear load + one index rebuild (no payload prepass).
- merge/reconcile path keeps the earlier sparse-reset and no-read-only-postflop-root creation wins.
- touched-mark storage remains bit-packed.
- `CFRNode` hot layout remains compacted (`action_count` is `uint8_t`).
- trainer-side local infoset cache experiment remains reverted.
- abstraction/runtime artifact formats are unchanged.
- post-rollback benchmark recovered near the earlier good baseline:
  - at `180k`: `elapsed_sec=336`, `iters/sec_recent=384.56`, `iters/sec_avg=528.35`
  - `infosets=137,670,856`, `mem_alloc_mb=18,720`, `mem_active_mb=7,049.85`
- current decision:
  - keep the older split sparse generic key index
  - stop spending time on dense-preflop / flat-hash style indexing experiments for now
  - next optimization target is hot/cold node locality work, not more indexing churn
- validation now passes at **91 tests, 0 failures**.

Latest delta (throughput tuning pass, 2026-03-04):
- strict-time chunk scheduler no longer degrades to tiny second-limited chunks in multithread mode.
- strict-time chunk policy now uses bounded parallel ranges (`min=threads*8`, `max=threads*256`) from requested chunk size.
- sharded merge now uses pre-partitioned per-worker shard index lists (deterministic, reduced merge scanning overhead).
- worker-local delta transform + shard partition build moved into worker thread after traversal; main-thread handoff now only precreates missing global nodes.
- fixed long-run train shutdown failure on very large blueprints by removing end-of-train full blueprint clone in `train`; final save now writes current blueprint directly.
- added search latency budgeting:
  - `search --time-ms <N>`
  - `match --search-time-ms <N>`
  - `search` now reports `solve_ms=...` in output.
- observed behavior: with large production blueprint file, one-shot CLI `search` latency is dominated by blueprint load time; solve itself can be bounded to ~1-5s via `--time-ms`.
- long-run script updated to `--status-iters 20000` (reduced status-boundary overhead).
- added persistent `search-server` command for RTA:
  - loads blueprint/abstraction once, initializes hand index once, then serves stdin query lines.
  - supports `defaults`, `set-default <opts>`, `help`, `quit`.
  - query lines accept search option overrides (`--hole ... --time-ms ...` etc).
- fixed large-run failure around high iteration counts:
  - `run_pluribus_16d_32c.bat` now uses `--no-async-checkpoint` to avoid memory blowups from async full-blueprint clone during checkpoint.
  - `cfr_blueprint_load` now does a prepass and pre-allocates large node/hash/payload capacities before ingesting nodes, improving large-file resume reliability.
- unit tests updated for strict-time chunk policy and shard partition correctness.
- validation now passes at **83 tests, 0 failures**.

Previous delta (Re-Plan v3 completion, 2026-03-03):
- blueprint format bumped to `v8` with payload-aware node serialization (`has_strategy` flag).
- raw training blueprints now suppress persistent postflop `strategy_sum` payload (street-split memory behavior).
- compact snapshot format added (`CFR_SNAPSHOT_MAGIC`), storing postflop current strategy only.
- `train` periodic snapshots use compact format; `finalize-blueprint` consumes compact and legacy snapshot files.
- regression coverage expanded; tests now pass at **72 tests, 0 failures**.

## Implemented Highlights

- Versioned abstraction artifact system:
  - `abstraction-build` command
  - `CFR_ABSTRACTION_VERSION` + hash
  - strict abstraction compatibility checks in `train/query/search/match`
- Blueprint format/storage updates:
  - `CFR_BLUEPRINT_VERSION 6`
  - compact node payload layout (`CFRNode` header + dynamic action payload storage)
  - int32 regret storage in memory and on disk
  - persisted `abstraction_hash32`
  - persisted `street_hint` per node
  - persisted runtime phase state (`elapsed_train_seconds`, `phase_flags`)
- Training/storage hardening:
  - snapshot cadence (`--snapshot-iters`, `--snapshot-seconds`, `--snapshot-dir`)
  - async checkpoint writer on Windows (`--async-checkpoint`)
- strict wall-clock phase mode (`--strict-time-phases`, `--discount-stop-seconds`, `--prune-start-seconds`)
- strict mode uses bounded parallel chunking with persisted wall-clock phase state
  - removed in-train postflop averaging flag path; two-stage train/finalize flow is canonical
- Finalization flow:
  - `finalize-blueprint` command
  - postflop built from snapshot-average of current strategy
  - preflop kept from in-memory average path
- Real-time search command:
  - `search` command with `--iters`, `--depth`, `--threads`
  - depth-limited subgame solving
  - continuation leaf model (`k=4` blueprint/fold/call/raise-bias rollouts)
  - final-vs-average policy output
  - off-tree mapping hook + frozen-action path support in engine
  - deterministic scalar/vector switch rule for late-round smaller subgames
  - subgame context reset keyed by street + history hash
  - search command now builds round-root state and replays current-round observed history as frozen actions before decision extraction
  - search belief/range updates are applied on frozen observed actions at round-root boundaries
  - `search` output includes `belief_updates=<N>`
- Match harness:
  - `match --a ... --b ... --hands ...`
  - deterministic seed support
- Belief/range engine remains integrated and tested.
- Additional no-limit edge-case regressions added.
- Parallel update mode split (B4) implemented:
  - `--parallel-mode deterministic|sharded` for `train` and `bench`
  - deterministic mode retained as baseline/default
  - sharded merge mode added for throughput-oriented long runs
  - compat hash now includes parallel mode
- Frozen production abstraction artifact (B5) integrated:
  - file: `data\abstraction_prod_pluribus_v1.bin`
  - hash: `0xD5B800C8`
  - config: seed `20260301`, bp buckets `200/200/200`, search buckets `500/500/500`, `mc=32`, `samples=50000`, `kmeans=64`
  - reproducibility twin `data\abstraction_prod_pluribus_v1_repro.bin` verified bit-identical via `fc /b`
- 16-day runner now uses the frozen artifact by default:
  - `run_pluribus_16d_32c.bat`
  - abstraction path switched from `abstraction_pluribus_16d.bin` to `abstraction_prod_pluribus_v1.bin`
  - training call uses `--parallel-mode sharded`
  - early phase resume is strict; `--resume-ignore-compat` is used only at/after the discount-off phase transition
- Long-run output/state reset for clean production start:
  - removed `data\blueprint_pluribus_16d.bin`
  - removed `data\run_pluribus_16d_32c.state`
  - removed `data\run_pluribus_16d_32c.log`
- Added helper script:
  - `freeze_abstraction_prod.bat` to rebuild + reproduce-check the frozen artifact.
- Added launch checklist:
  - `RUNBOOK.md` with exact build/freeze/clean/start/resume/monitor commands for the 16-day run.

## Key Commands

1. Build: `build.bat`
2. Tests: `test.bat`
3. Build abstraction:
   - `build\main.exe abstraction-build --out data\abstraction.bin`
4. Train (raw blueprint):
   - `build\main.exe train --abstraction data\abstraction.bin --iters 10000 --threads 32 --snapshot-seconds 12000 --snapshot-dir data\snapshots --out data\blueprint_raw.bin`
5. Finalize:
   - `build\main.exe finalize-blueprint --raw data\blueprint_raw.bin --snapshot-dir data\snapshots --abstraction data\abstraction.bin --out data\blueprint.bin`
6. Query:
   - `build\main.exe query --blueprint data\blueprint.bin --abstraction data\abstraction.bin --hole AsKd`
7. Search:
   - `build\main.exe search --blueprint data\blueprint.bin --abstraction data\abstraction.bin --hole AsKd --pot 8 --to-call 2 --iters 400 --depth 3 --threads 8`
8. Match:
   - `build\main.exe match --a data\blueprint_a.bin --b data\blueprint_b.bin --abstraction data\abstraction.bin --hands 2000`

## Validation Baseline

- Current baseline: **80 tests, 0 failures** via `test.bat`.
- Production build: passes via `build.bat`.
- Smoke training with frozen abstraction passes:
  - `build\main.exe train --iters 64 --threads 2 ... --abstraction data\abstraction_prod_pluribus_v1.bin`
  - trainer reports abstraction hash `0xD5B800C8`.
- Parallel-mode validation:
  - sharded mode deterministic repeatability test passes
  - deterministic-vs-sharded equivalence test passes on fixed seeded workload
- Parallel-mode throughput snapshots:
  - `bench` heavy config CSV/JSON: `data\bench_threads_mode_det.*`, `data\bench_threads_mode_sharded.*`
  - `train --pluribus-profile --iters 2000` on this machine:
    - deterministic: `1t=954.59 it/s`, `24t=1468.24 it/s`, `32t=1400.07 it/s`
    - sharded: `1t=929.21 it/s`, `24t=1651.69 it/s`, `32t=1628.72 it/s`
- Preflight smoke (start+resume) executed successfully, then long-run artifacts were cleaned again.

## Main Files

- `PLAN.md`
- `README.md`
- `src/core.c`
- `src/poker_state.c`
- `src/cfr_abstraction.c`
- `src/search_engine.c`
- `src/cfr_trainer.c`
- `src/commands.c`
- `tests/test_main.c`

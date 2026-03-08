# CFR+ 6-Max NLHE (Unity/Jumbo C Build)

Single-translation-unit CFR+ trainer/query tool for **6-max No Limit Hold'em** in pure C (MSVC `/TC`).

The project is split into multiple `*.c` files for organization, but compiled as one translation unit via `src/main.c` includes.

## Requirements

- Windows
- Visual Studio 2022 (Community/BuildTools/Professional/Enterprise)
- MSVC toolchain (`cl.exe`)

`build.bat` and `test.bat` automatically call `vcvars64.bat`.

## Project Layout

- `src/main.c` - include aggregator + program entrypoint (`main`)
- `src/core.c` - core types, RNG, card/eval helpers, strategy math
- `src/cfr_phe_bridge.c` - embedded HenryRLee/PokerHandEvaluator 7-card evaluator
- `src/cfr_isomorphism.c` - embedded `kdub0/hand-isomorphism` canonical hand indexing
- `src/abstraction_config.c` - versioned abstraction artifact hashing/load/save + blueprint/search bucket mapping
- `src/poker_state.c` - blueprint persistence + hand state transitions (including history-key builder + off-tree action translation hook)
- `src/cfr_abstraction.c` - infoset abstraction fields + numeric pot/call buckets + canonical history-key usage
- `src/range_engine.c` - 1326-combo range indexing, normalization, blocker filtering, Bayes-likelihood update primitives
- `src/search_engine.c` - belief-state primitives + round-root nested real-time subgame search (round-structured leaf policy with depth guard), continuation leaf model (`k=4`), and multi-thread search solve path
- `src/cfr_trainer.c` - CFR+ traversal and iteration loop
- `src/cli_utils.c` - CLI helper parsing/usage formatting
- `src/commands.c` - `train`, `query`, `search`, `match`, `bench`, `abstraction-build` command handlers
- `tests/test_main.c` - Unity tests (includes `../src/main.c` with `CFR_TEST`)
- `third_party/unity/src/*` - Unity framework sources
- `build.bat` - production build
- `test.bat` - test build + run
- `eval.bat` - automated evaluation runner (CI/gates + CSV/JSON reports)
- `eval_profile_quick.bat` - quick smoke eval preset
- `eval_profile_pluribus.bat` - one-command Pluribus-style eval preset
- `bench.bat` - evaluator benchmark (CFR wrapper vs direct PHE call)
- `freeze_abstraction_prod.bat` - build/reproduce frozen production abstraction artifact

## Build

```bat
build.bat
```

Output:

- `build/main.exe`
- `build/main.obj`

## Test

```bat
test.bat
```

Output:

- `build/test_main.exe`
- `build/test_main.obj`
- `build/unity.obj`

## Benchmark Evaluator

```bat
bench.bat
```

Optional hand count argument (default `1000000`):

```bat
bench.bat 2000000
```

Benchmark compares:

- CFR showdown wrapper path (used by the trainer)
- direct `evaluate_7cards` calls

## Training

```bat
build\main.exe train [options]
```

### Important behavior

- Default blueprint output: `data\blueprint.bin`
- `data\` folder is auto-created when output path starts with `data`
- Safe stop with `Ctrl+C` (writes final checkpoint before exit)
- Resume from checkpoint with `--resume`
- Infoset table grows dynamically (no fixed soft-cap truncation)
- Blueprint binary format is now `v11`; older blueprint files are intentionally rejected

### Default values

- `--out data\blueprint.bin`
- `--dump-iters 1000`
- `--dump-seconds 0` (disabled)
- `--status-iters 100`
- If neither `--iters` nor `--seconds` provided: `--iters 1000`

### Train options

- `--out <file>` output blueprint path
- `--abstraction <file>` abstraction config artifact (`abstraction-build` output)
- `--resume <file>` resume from blueprint
- `--resume-ignore-compat` ignore solver compatibility hash mismatch on resume
- `--iters <N>` run additional iterations
- `--seconds <N>` wall-clock training limit
- `--dump-iters <N>` periodic checkpoint every N iterations
- `--dump-seconds <N>` periodic checkpoint every N seconds
- `--snapshot-iters <N>` periodic snapshot cadence (compact postflop-current snapshots for finalize pipeline)
- `--snapshot-seconds <N>` periodic snapshot cadence in seconds (compact postflop-current snapshots)
- `--snapshot-dir <dir>` snapshot output directory (default `data\snapshots`)
- `finalize-blueprint` performs postflop snapshot-current averaging offline into final playable blueprint
  - `--snapshot-min-seconds <N>` (finalize) uses only snapshots with `elapsed_train_seconds >= N`
  - finalize accepts both compact snapshot files and legacy full-blueprint snapshot files
  - finalize can also emit a policy-only runtime artifact via `--runtime-out <file>`
- `--async-checkpoint` enable async checkpoint writer (Windows)
- `--no-async-checkpoint` disable async checkpoint writer
- `--status-iters <N>` status print every N iterations (`0` disables)
  - Status line includes: `iter`, `hands`, `infosets`, `mem_alloc_mb`, `mem_active_mb`, `iters/sec_recent`, `iters/sec_avg`
- `--threads <N>` parallel training workers (default: detected CPU cores)
- `--parallel-mode <name>` parallel update mode: `deterministic` (worker-local delta reduce) or `sharded` (parallel shard merge)
- `--chunk-iters <N>` max iterations per scheduling slice (default: 256)
- `--samples-per-player <N>` traversals per player per iteration (default: 1)
- `--strategy-interval <N>` accumulate average strategy every N iterations (default: 1)
- `--linear-discount-iters <N>` periodic linear discount interval (default: 1000, `0` disables)
- `--linear-discount-stop-iters <N>` stop discounting after iteration N (default: 0 = disabled)
- `--linear-discount-scale <X>` linear discount scale (default: 1.0)
- `--prune-start <N>` start pruning at iteration N (default: 2000)
- `--prune-full-iters <N>` force full no-prune traversal cadence after prune start (default: 0 = disabled)
- `--prune-threshold <X>` prune actions with regret <= X (default: -200)
- `--prune-prob <X>` pruning probability in `[0,1]` (default: 0.95)
- `--int-regret` enforce floor-clamped int32 regret updates (native storage is already int32)
- `--float-regret` use float regrets
- `--regret-floor <N>` minimum regret clamp value (must be <= 0)
- `--discount-every-seconds <N>` apply linear discount event every N seconds (time-scheduled mode)
- `--warmup-seconds <N>` warmup boundary for profile-gated averaging/snapshots
- `--snapshot-start-seconds <N>` enable snapshots at elapsed second N
- `--avg-start-seconds <N>` enable preflop average accumulation at elapsed second N
- `--preflop-avg` enable preflop average accumulation (default)
- `--no-preflop-avg` disable preflop average accumulation
- `--preflop-avg-mode <m>` preflop averaging mode: `full|sampled` (profile default: `sampled`)
- `--pluribus-profile` apply Pluribus-like trainer schedule defaults
- `--no-prune` disable pruning
- `--no-linear-discount` disable periodic linear discount
- `--seed <N>` RNG seed (new training only)

### Example

```bat
build\main.exe train --iters 50000 --dump-iters 5000 --status-iters 500 --out data\blueprint.bin
```

Pluribus-like schedule example:

```bat
build\main.exe train --pluribus-profile --iters 500000 --threads 32 --chunk-iters 128 --dump-iters 10000 --out data\blueprint.bin
```

## 32-Core 16-Day Blueprint Run

For long-run orchestration targeting roughly Pluribus-scale compute budget on a 32-core CPU, use:

```bat
run_pluribus_16d_32c.bat
```

Full launch/resume checklist is in `RUNBOOK.md`.

This script:

- builds if needed
- creates/uses `data\abstraction_prod_pluribus_v1.bin`
- trains in 6-hour slices with automatic resume
- tracks accumulated wall-seconds in `data\run_pluribus_16d_32c.state`
- writes progress logs to `data\run_pluribus_16d_32c.log`
- writes blueprint to `data\blueprint_pluribus_16d.bin`
- checkpoints every 1800 seconds by default to avoid large-blueprint throughput collapse from overly frequent sync saves
- uses strict wall-clock phase scheduler with persisted phase state and bounded chunk checks:
  - discount every 600s up to 24000s
  - pruning from 12000s
  - preflop average strategy starts at 48000s
  - snapshot/finalize warmup gate at 48000s
- finalizes directly to `data\blueprint_pluribus_16d_runtime.bin` for disk-backed `search-server` serving
- skips full finalized blueprint output by default to reduce RAM pressure during post-processing
- uses `--parallel-mode sharded` for higher 32-core throughput on current hardware/profile

Optional overrides:

```bat
run_pluribus_16d_32c.bat <total_seconds> <threads>
```

Examples:

```bat
run_pluribus_16d_32c.bat 1382400 32
run_pluribus_16d_32c.bat 600 8
```

Notes:

- `1382400` seconds is exactly 16 days (`12288` core-hours at 32 cores).
- To approach `12400` core-hours on 32 cores, run about `1395000` seconds (~16.15 days).

## Frozen Production Abstraction

For production runs, use the frozen abstraction artifact:

- `data\abstraction_prod_pluribus_v1.bin`
- expected abstraction hash: `0xD5B800C8`
- config: seed `20260301`, blueprint buckets `200/200/200`, search buckets `500/500/500`, `--mc-samples 32`, `--samples 50000`, `--kmeans-iters 64`

Build/reproduce it with:

```bat
freeze_abstraction_prod.bat
```

Direct command equivalent:

```bat
build\main.exe abstraction-build --out data\abstraction_prod_pluribus_v1.bin --seed 20260301 --bp-flop 200 --bp-turn 200 --bp-river 200 --search-flop 500 --search-turn 500 --search-river 500 --mc-samples 32 --samples 50000 --kmeans-iters 64
```

Multithread tuning note:

- Parallel throughput improves when each scheduling slice has enough work.
- For high core counts, prefer larger chunks and/or higher samples:
  - example: `--threads 24 --chunk-iters 4000 --samples-per-player 64`
- Very small chunks with light per-iteration work can still favor single-thread execution.
- Lazy linear discounting is enabled in training path, so discount events no longer trigger full-table sweeps.

## Thread Scaling Benchmark

```bat
build\main.exe bench [options]
```

Runs training throughput benchmarks for thread counts from the set `{1,2,4,8,16,24,32}` up to `--max-threads` (capped by detected hardware threads).

Bench options:

- `--iters <N>` iterations per thread-count run (default: `200`)
- `--max-threads <N>` max thread count to include (default: `32`)
- `--samples-per-player <N>` traversals per player per iteration (default: `1`)
- `--parallel-mode <name>` parallel update mode: `deterministic|sharded` (default: `deterministic`)
- `--chunk-iters <N>` chunk size (default: `256`)
- `--seed <N>` benchmark seed (default: `123456789`)
- `--csv <file>` write benchmark table to CSV file
- `--json <file>` write benchmark table to JSON file

Benchmark table/CSV columns include:

- `mode`, `threads`, `iters_per_sec`, `hands_per_sec`, `speedup`, `infosets`
- `mem_alloc_mb`, `mem_active_mb`
- run metadata: `iterations`, `samples_per_player`, `chunk_iters`, `seed`

Example:

```bat
build\main.exe bench --iters 400 --max-threads 32 --chunk-iters 16 --csv data\bench_threads.csv --json data\bench_threads.json
```

## Parallel Mode Tradeoff (B4)

Two parallel update paths are implemented:

- `deterministic`: worker-local traversals + single-thread deterministic reduction
- `sharded`: worker-local traversals + deterministic sharded parallel merge

Measured on this machine with frozen abstraction (`0xD5B800C8`):

- `bench --iters 320 --samples-per-player 64 --chunk-iters 4000 --max-threads 24`
  - deterministic: `24 threads = 24.54 iters/sec` (`2.08x` vs 1-thread baseline in that run)
  - sharded: `24 threads = 20.84 iters/sec` (`1.56x` vs 1-thread baseline in that run), but faster at mid-scale (`16 threads = 34.95 iters/sec`)
- production-profile style (`train --pluribus-profile --iters 2000 --chunk-iters 50000 --samples-per-player 1`)
  - deterministic: `32 threads = 1400.07 iters/sec`
  - sharded: `32 threads = 1628.72 iters/sec`

Current long-run choice in `run_pluribus_16d_32c.bat`: `sharded`.

## Build Abstraction Artifact

```bat
build\main.exe abstraction-build [options]
```

This writes a versioned abstraction artifact with strict hash checking used by `train`, `query`, `search`, and `match`.

Options:

- `--out <file>` output abstraction file (default: `data\abstraction.bin`)
- `--seed <N>` deterministic abstraction seed
- `--bp-flop/--bp-turn/--bp-river <N>` blueprint postflop bucket counts
- `--search-flop/--search-turn/--search-river <N>` real-time search postflop bucket counts
- `--mc-samples <N>` Monte Carlo samples per feature evaluation (default `12`)
- `--samples <N>` feature sample count per postflop street for clustering (default `8000`)
- `--kmeans-iters <N>` k-means iterations (default `24`)
- `--cluster-algo <name>` clustering algorithm: `legacy|emd-kmedoids` (default `emd-kmedoids`)
- `--no-centroids` disable centroid build and keep legacy hash fallback mapping

Example:

```bat
build\main.exe abstraction-build --out data\abstraction.bin --bp-flop 200 --bp-turn 200 --bp-river 200 --search-flop 500 --search-turn 500 --search-river 500
```

## Real-Time Search

```bat
build\main.exe search [options]
```

Runs a nested subgame solve from round-root with a round-structured leaf policy (and `--depth` as a guard), replaying current-round observed actions from `--history`.

- Uses search abstraction buckets (`CFR_ABS_MODE_SEARCH`).
- Uses continuation leaf model with `k=4` strategy variants (blueprint/fold-bias/call-bias/raise-bias).
- Reports both final-iteration policy (`final`) and weighted-average policy (`avg`).
- Supports multi-thread search solving via `--threads`.
- Search output reports `frozen_actions=<N>` to show how many current-round actions were replayed before the decision node.
- In v5, observed actions are replayed for belief/state consistency, while hard-freezing in traversal is restricted to the acting player's already-realized actions.
- Search output reports `belief_updates=<N>` to show Bayes/range updates applied while replaying frozen observed actions.
- Inject mode (`--offtree-mode inject`) keeps observed non-template legal raises as explicit support in the solved subgame.
- Search subgame action limiter keeps at most five raise branches (plus injected observed raise support when legal).

Important options:

- `--blueprint <file>` full finalized blueprint path
- `--runtime-blueprint <file>` disk-backed policy-only runtime artifact path
- `--abstraction <file>` abstraction artifact to match blueprint hashing
- `--hole <cards>` required
- `--iters <N>` subgame iterations
- `--time-ms <N>` solve time budget in milliseconds (`0` disables; still bounded by `--iters`)
- `--depth <N>` horizon depth
- `--threads <N>` search workers
- `--search-pick sample-final|argmax` action choice policy (default `sample-final`)
- `--offtree-mode inject|translate` observed off-tree handling mode (default `inject`)
- `--cache-bytes <N>` decoded runtime-policy cache size for `--runtime-blueprint`
- `--prefetch none|auto|preflop` runtime artifact warmup mode
- `--player-seat` must match the acting player after replaying the provided current-round history

Example:

```bat
build\main.exe search --blueprint data\blueprint.bin --abstraction data\abstraction.bin --hole AsKd --player-seat 2 --dealer-seat 5 --pot 3 --to-call 1 --iters 100000 --time-ms 2000 --depth 3 --threads 8 --seed 7
```

## Persistent Search Server (RTA Path)

For repeated real-time queries, use `search-server` so policy loading happens once. On large artifacts, prefer the disk-backed runtime artifact path instead of loading the full finalized blueprint.

```bat
build\main.exe search-server --runtime-blueprint data\blueprint_pluribus_16d_runtime.bin --abstraction data\abstraction_prod_pluribus_v1.bin --iters 100000 --time-ms 2000 --depth 3 --threads 24 --cache-bytes 134217728 --prefetch auto --seed 7
```

Then send one query per line on stdin:

```text
--hole AsKd --player-seat 2 --pot 3 --to-call 1 --time-ms 2000
--hole QhQs --board AhKd2c --pot 14 --to-call 6 --time-ms 1500
quit
```

Server commands:

- `defaults` prints current default options
- `set-default <opts>` updates defaults for subsequent queries
- `stats` prints runtime cache/prefetch counters for mmap mode
- `help` prints protocol help
- `quit` exits

This removes per-query model load latency; query time is then mostly `solve_ms`.

## Runtime Artifact Workflow

For large serving deployments on a 64GB host, emit only the runtime artifact after finalization:

```bat
build\main.exe finalize-blueprint --raw data\blueprint_pluribus_16d.bin --snapshot-dir data\snapshots_pluribus_16d --abstraction data\abstraction_prod_pluribus_v1.bin --runtime-out data\blueprint_pluribus_16d_runtime.bin --runtime-quant u16 --runtime-shards 256
```

The runtime artifact is policy-only and memory-mapped at serve time. It excludes regrets/resume state, keeps street-sharded on-disk lookup metadata, and supports a bounded decoded-policy cache for hot query locality.

If you still want a classic finalized blueprint file for inspection/debug, add `--out data\blueprint_pluribus_16d_final.bin`.

## Match Harness

```bat
build\main.exe match --a <blueprintA> --b <blueprintB> [options]
```

Self-play sanity harness for policy-vs-policy comparisons.

- `--a <file>` policy A blueprint (required)
- `--b <file>` policy B blueprint (required)
- `--abstraction <file>` abstraction artifact used by both
- `--hands <N>` number of hands (default `1000`)
- `--mode blueprint|search` decision mode (`search` enables per-decision nested re-solve)
- `--runtime-profile none|pluribus` runtime trigger policy (`pluribus` enables preflop trigger on large off-tree raise-size gap `>100` with active players `<=4`, plus postflop search)
- `--search-iters <N>` per-decision search iterations in `search` mode
- `--search-time-ms <N>` per-decision search time budget in `search` mode (`0` disables)
- `--search-depth <N>` per-decision search depth guard in `search` mode
- `--search-threads <N>` per-decision search worker threads in `search` mode
- `--search-pick sample-final|argmax` search action pick mode
- `--offtree-mode inject|translate` observed off-tree handling mode
- `--seed <N>` deterministic match seed

Example:

```bat
build\main.exe match --a data\blueprint_a.bin --b data\blueprint_b.bin --abstraction data\abstraction.bin --hands 2000 --seed 11
```

Search-enabled match example:

```bat
build\main.exe match --a data\blueprint_a.bin --b data\blueprint_b.bin --mode search --search-iters 128 --search-depth 3 --search-threads 8 --search-pick sample-final --offtree-mode inject --hands 200 --seed 11
```

Runtime-profile match example:

```bat
build\main.exe match --a data\blueprint_a.bin --b data\blueprint_b.bin --runtime-profile pluribus --search-iters 128 --search-depth 3 --search-threads 8 --search-pick sample-final --offtree-mode inject --hands 200 --seed 11
```

## Automated Evaluation Suite

Use the scripted evaluator to run repeated seeded matchups, compute 95% confidence intervals, and enforce pass/fail gates.

```bat
eval.bat [PowerShell options]
```

The wrapper:

- ensures MSVC environment
- auto-builds `build\main.exe` if missing
- runs `scripts\eval.ps1`
- writes CSV + JSON reports
- returns exit code `0` when all gates pass, `2` when any gate fails

Important options (`eval.ps1`):

- `-Target <file>` target blueprint to evaluate
- `-Baselines "<file1>;<file2>;...>"` baseline blueprints
- `-Abstraction <file>` abstraction artifact used for compatibility checks
- `-Mode blueprint|search|runtime-pluribus` evaluation mode
- `-Hands <N>` hands per seeded run
- `-Seeds <N>` number of seeded runs per baseline
- `-SeedStart <N>` first seed (increments by 1)
- `-SearchIters <N> -SearchDepth <N> -SearchThreads <N>`
- `-SearchPick sample-final|argmax`
- `-OfftreeMode inject|translate`
- `-MinBb100 <X>` pass gate threshold (default `0.0`), requires CI lower bound `> X`
- `-Csv <file> -Json <file>` report output paths
- `-Build` force `build.bat` before evaluation
- `-IgnoreAbstractionCompat` pass through `--ignore-abstraction-compat` to match runs

Example:

```bat
eval.bat -Target data\candidate.bin -Baselines "data\ref_a.bin;data\ref_b.bin" -Abstraction data\abstraction_prod_pluribus_v1.bin -Mode runtime-pluribus -Hands 50000 -Seeds 8 -SeedStart 20260304 -SearchIters 128 -SearchDepth 3 -SearchThreads 24 -MinBb100 0.0 -Csv data\eval_candidate.csv -Json data\eval_candidate.json
```

Preset one-command run:

```bat
eval_profile_pluribus.bat
```

`eval_profile_pluribus.bat` uses production-style defaults (`runtime-pluribus`, 50k hands, 8 seeds, 24 search threads) and writes:

- `data\eval_pluribus_profile.csv`
- `data\eval_pluribus_profile.json`

Default baseline refs in the preset are:

- `data\baseline_pluribus_ref_a_v9.bin`
- `data\baseline_pluribus_ref_b_v9.bin`

If those files are missing, the preset seeds them from current target once; replace them with your real reference blueprints for meaningful comparisons.
If `data\abstraction_prod_pluribus_v1.bin` is missing, the preset builds it with the frozen production abstraction settings.
If your artifact names differ, edit `TARGET` / `BASELINE_A` / `BASELINE_B` / `ABSTRACTION` at the top of the preset script.

Quick smoke preset:

```bat
eval_profile_quick.bat
```

`eval_profile_quick.bat` is a fast pipeline sanity check:

- auto-selects first existing target from:
  - `data\blueprint_pluribus_16d_final.bin`
  - `data\blueprint.bin`
- if neither exists, auto-generates `data\quick_eval_target_v9.bin` via a tiny `train --iters 2` run
- uses `mode=blueprint`, `hands=2000`, `seeds=2`
- defaults to self-baseline (`BASELINES=%TARGET%`) to validate reporting/gates workflow quickly
- writes:
  - `data\eval_quick_profile.csv`
  - `data\eval_quick_profile.json`

For real quality comparisons, set strong external baselines in `eval_profile_pluribus.bat` (or call `eval.bat` directly with multiple baselines).

Report semantics:

- run rows: per-seed `target_bb100`/`baseline_bb100`
- summary rows: `mean_target_bb100`, `std_target_bb100`, `ci95_low_target_bb100`, `ci95_high_target_bb100`, gate decision

## Query Blueprint

```bat
build\main.exe query [options]
```

Required:

- `--blueprint <file>`
- `--hole <cards>` (example: `AsKd`)

Optional:

- `--abstraction <file>`
- `--ignore-abstraction-compat`
- `--board <cards>` (example: `AhKs2d`)
- `--history "<tokens>"`
- `--street preflop|flop|turn|river`
- `--player-seat <0..5>`
- `--dealer-seat <0..5>`
- `--active <N>`
- `--pot <N>`
- `--to-call <N>`
- `--stack <N>`
- `--raises <N>`

History tokens:

- `f` fold
- `c`/`k` call/check
- `h` half-pot bet/raise action
- `p` pot bet/raise action
- `a` all-in
- `r` raise marker
- `r<bucket>` raise marker + explicit amount bucket code
- `/` or `|` street separator marker

If an exact infoset bucket is missing, query returns uniform fallback strategy for legal actions.

Board/street compatibility for canonical indexing:

- preflop: board card count must be `0`
- flop: board card count must be `3`
- turn: board card count must be `4`
- river: board card count must be `5`

### Example

```bat
build\main.exe query --blueprint data\blueprint.bin --hole AsKd --board AhKs2d --history "c h / c" --pot 20 --to-call 5 --player-seat 0 --dealer-seat 5 --active 4 --stack 180
```

## Notes

- This is an abstraction-based CFR+ blueprint solver, not a full unabstracted exact NLHE game tree.
- Showdown evaluation uses HenryRLee/PokerHandEvaluator with native card encoding (`rank * 4 + suit`) and no conversion.
- Hand strength uses raw PHE rank semantics (`1` strongest, `7462` weakest).
- Card-state abstraction uses `kdub0/hand-isomorphism` canonical indices per street (`169`, `1,286,792`, `55,190,538`, `2,428,287,420`).
- Postflop abstraction artifacts now include feature-cluster centroids (`E[HS]`, `E[HS^2]`, potential, positive potential style vector) for blueprint/search bucket assignment.
- Abstraction format `v3` adds deterministic histogram-EMD medoids (24 bins) and EMD quality metrics (`intra`, `sep`) for each street/mode.
- Runtime bucket assignment uses EMD medoid distance when available; legacy centroid path remains selectable via `--cluster-algo legacy`.
- Multi-thread blueprint mode runs parallel iteration chunks with worker-local updates and supports two merge paths: `deterministic` and `sharded`.
- Sharded mode parallelizes merge by key-shard ownership; deterministic mode uses serial reduction.
- Blueprint storage uses dynamic sparse growth (node index + open-addressed key index) with compact per-node action payload offsets, not fixed 16-action inline arrays.
- Regrets are native `int32_t` in-memory and on-disk (blueprint format v11).
- Blueprint load path is `v11` fast-load:
  - no full-file payload prepass,
  - linear node/payload deserialize,
  - one hash-table build after load.
- Preflop uses the same generic sparse key/index path as the rest of the blueprint again; the dense-preflop specialization branch was reverted after negative throughput benchmarks.
- The hot generic infoset index is also back on the older split sparse layout (`key_hash_keys` + `key_hash_node_plus1`); the flat generic hash experiment was reverted after negative throughput benchmarks.
- Raw training blueprints suppress persistent postflop `strategy_sum` payload; postflop strategy is reconstructed from snapshots in `finalize-blueprint`.
- Trainer supports MCCFR-P-style scheduling controls (`strategy_interval`, prune full-traversal cadence, early discount window stop) and optional int32 regret quantization mode.
- Linear discount uses deferred lazy application (per-node scale stamps) to avoid full-table discount sweeps.
- Trainer includes snapshot controls (`--snapshot-iters`, `--snapshot-seconds`, `--snapshot-dir`) and optional async checkpoint writes (`--async-checkpoint` on Windows).
- Postflop strategy construction is a two-stage flow: `train` saves raw blueprint + snapshots; `finalize-blueprint` builds postflop from snapshot-average of current strategies, while preflop remains in-memory average.
- Blueprint files store solver compatibility hash and abstraction compatibility hash used by `train --resume` and query/search/match compatibility checks.
- Seeded runs are deterministic for a fixed config (including deterministic per-worker seed partitioning in parallel training).
- Range/belief primitives are available for 1326-combo Bayes updates and blueprint-conditioned action likelihoods.
- Belief updates in search mode prefer latest available search sigma policy (if present) and fall back to blueprint policy otherwise.
- Real-time search command performs depth-limited subgame solving with a continuation leaf model (`k=4`) and multi-threaded solve path.
- Real-time search uses a deterministic scalar/vector switch rule keyed by round/size (late-round smaller subgames use vector traversal).
- Betting transitions include short-all-in reopen handling (short raises do not reopen raising for players who already acted at the current full-raise sequence).
- Showdown resolution supports side-pot layering from per-player contributions.
- Off-tree/observed action replay supports `inject` mode (exact legal branch replay) and `translate` mode (pseudo-harmonic mapping).
- Search off-tree raise translation supports pseudo-harmonic mapping between adjacent legal raise targets.
- Abstraction artifact binary and blueprint binary are both versioned (`CFR_ABSTRACTION_VERSION`, `CFR_BLUEPRINT_VERSION`) and hash-checked for compatibility.
- Pluribus parity work status is tracked in `PLAN.md` audit section.

## Roadmap Targets

Phase acceptance targets tracked in `PLAN.md` are operationalized as:

- Phase 1 (game/tree correctness): no-limit legal-action/regression tests must stay green.
- Phase 2 (solver runtime): `train` must support `--threads` and retain deterministic seeded behavior.
- Phase 3 (abstraction): preflop remains lossless canonical; postflop abstraction artifacts must be versioned.
- Phase 4 (storage/resume): resume mismatch checks must reject incompatible solver config without override.
- Phase 5 (real-time search): subgame search interfaces should support bounded budgets and deterministic seeding.
- Phase 6 (belief/range): 1326-combo Bayes update tests must remain stable.
- Phase 7 (benchmarking): maintain benchmark outputs for `1/2/4/8/16/32` threads using `bench` with CSV/JSON export.

Benchmark target reporting:

- Record `iters_per_sec`, `hands_per_sec`, `speedup`, `infosets`, `mem_alloc_mb`, `mem_active_mb`.
- Include run metadata: `iterations`, `samples_per_player`, `chunk_iters`, `seed`, thread count.

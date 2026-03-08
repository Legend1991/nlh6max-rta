# 16-Day Production Runbook (32 Cores)

This runbook is for the current frozen setup:

- abstraction: `data\abstraction_prod_pluribus_v1.bin`
- abstraction hash: `0xD5B800C8`
- long-run script: `run_pluribus_16d_32c.bat`
- parallel mode: `sharded`

## 1. Build + Sanity

```bat
build.bat
test.bat
```

Expected: tests pass (`78 Tests 0 Failures`).

## 2. Re-freeze Abstraction (Reproducibility Check)

```bat
freeze_abstraction_prod.bat
```

Expected:

- hash `0xD5B800C8`
- reproducibility check passes (`fc /b` identical)

## 3. Clean Start (Remove Old Long-Run State)

Run from project root:

```bat
cmd /c "if exist data\blueprint_pluribus_16d.bin del /f /q data\blueprint_pluribus_16d.bin & if exist data\run_pluribus_16d_32c.state del /f /q data\run_pluribus_16d_32c.state & if exist data\run_pluribus_16d_32c.log del /f /q data\run_pluribus_16d_32c.log"
```

## 4. Launch Production Run

For exactly 16 days (`12288` core-hours @ 32 cores):

```bat
run_pluribus_16d_32c.bat 1382400 32
```

For ~`12400` core-hours target (~16.15 days):

```bat
run_pluribus_16d_32c.bat 1395000 32
```

Notes:

- Script auto-resumes from `data\blueprint_pluribus_16d.bin`.
- It checkpoints every 1800 seconds and snapshots periodically.
- It uses strict wall-clock phase scheduling:
  - discount every 600s until 24,000 elapsed seconds
  - pruning starts at 12,000 elapsed seconds
  - preflop average accumulation starts at 48,000 elapsed seconds
  - snapshot/finalize warmup gate starts at 48,000 elapsed seconds.
- It finalizes the raw blueprint into `data\blueprint_pluribus_16d_final.bin` at completion.

## 5. Monitor Progress

Current progress state:

```bat
type data\run_pluribus_16d_32c.state
```

Live log tail (PowerShell):

```powershell
Get-Content data\run_pluribus_16d_32c.log -Wait
```

Check blueprint file growth:

```bat
dir data\blueprint_pluribus_16d.bin
```

## 6. Stop/Resume

Stop safely:

- press `Ctrl+C` in the training console.

Resume:

```bat
run_pluribus_16d_32c.bat 1395000 32
```

Use the same total-seconds target as launch.

## 7. Quick Preflight (Optional, Before Long Run)

Short smoke with start+resume behavior:

```bat
run_pluribus_16d_32c.bat 3 2
run_pluribus_16d_32c.bat 8 2
```

Then clean again using Step 3.

## 8. Post-Run Basic Checks

```bat
type data\run_pluribus_16d_32c.state
dir data\blueprint_pluribus_16d.bin
dir data\blueprint_pluribus_16d_final.bin
```

Optional policy query smoke:

```bat
build\main.exe query --blueprint data\blueprint_pluribus_16d_final.bin --abstraction data\abstraction_prod_pluribus_v1.bin --hole AsKd
```

## 9. Deterministic Smoke Transcript (2026-03-02)

Artifacts used:

- `data\smoke_det_raw.bin`
- `data\smoke_det_final.bin`
- `data\smoke_det_snap\snapshot_*.bin`

Exact command/output transcript:

```text
> build\main.exe train --abstraction data\abstraction_prod_pluribus_v1.bin --out data\smoke_det_raw.bin --iters 32 --threads 1 --parallel-mode deterministic --chunk-iters 1 --samples-per-player 1 --strategy-interval 1 --snapshot-iters 16 --snapshot-dir data\smoke_det_snap --status-iters 16 --seed 20260302
Initialized new blueprint (seed=20260302)
Trainer config: threads=1 mode=deterministic chunk=1 samples/player=1 strategy_interval=1 linear_discount=on(1000, stop_iters=0, stop_seconds=0, scale=1.00) pruning=on(start_iters=2000, start_seconds=0, full_every=0, threshold=-200.00, p=0.95) regret_mode=float(floor=-2000000000) snapshots=16/0sec(async=on) strict_time=off compat=0x6E47E31F abstraction=0xD5B800C8
iter=16 hands=96 infosets=12742 elapsed_sec=0 phase=discount-on/prune-on mem_alloc_mb=22.75 mem_active_mb=0.93 iters/sec_recent=1066.67 iters/sec_avg=1066.67
snapshot: data\smoke_det_snap\snapshot_000000000016.bin (iter=16)
iter=32 hands=192 infosets=24901 elapsed_sec=0 phase=discount-on/prune-on mem_alloc_mb=23.25 mem_active_mb=1.80 iters/sec_recent=1142.86 iters/sec_avg=1103.45
snapshot: data\smoke_det_snap\snapshot_000000000032.bin (iter=32)
Saved blueprint: data\smoke_det_raw.bin
Final iter=32 hands=192 infosets=24901

> build\main.exe finalize-blueprint --raw data\smoke_det_raw.bin --snapshot-dir data\smoke_det_snap --abstraction data\abstraction_prod_pluribus_v1.bin --runtime-out data\smoke_det_runtime.bin
Finalized blueprint save skipped (runtime-only mode)
  raw blueprint: data\smoke_det_raw.bin
  snapshots scanned: 2
  snapshots used: 2
  abstraction hash: 0xD5B800C8
  runtime artifact: data\smoke_det_runtime.bin (quant=u16 shards=256)

> build\main.exe query --blueprint data\smoke_det_final.bin --abstraction data\abstraction_prod_pluribus_v1.bin --hole AsKd --pot 8 --to-call 2 --player-seat 0 --dealer-seat 5 --active 6
Blueprint node not found. Returning uniform fallback strategy for this abstraction bucket.
Query key: 16038459168785635690
Street=0 Position=1 HandBucket=89 PotBucket=2 ToCallBucket=0 Active=6
  fold         : 0.142857
  call         : 0.142857
  raise        to=4 : 0.142857
  raise        to=5 : 0.142857
  raise        to=6 : 0.142857
  raise        to=7 : 0.142857
  all_in       to=200 : 0.142857

> build\main.exe search --blueprint data\smoke_det_final.bin --abstraction data\abstraction_prod_pluribus_v1.bin --hole AsKd --pot 8 --to-call 2 --player-seat 2 --dealer-seat 5 --active 6 --iters 64 --depth 3 --threads 1 --seed 20260302
Search result: iters=64 depth=3 threads=1 frozen_actions=0 belief_updates=0 chosen=call
  call         final=1.000000 avg=0.834956
  raise        to=2 final=0.000000 avg=0.054491
  raise        to=3 final=0.000000 avg=0.007894
  raise        to=4 final=0.000000 avg=0.000200
  raise        to=5 final=0.000000 avg=0.046055
  all_in       to=200 final=0.000000 avg=0.056404

> build\main.exe search-server --runtime-blueprint data\smoke_det_runtime.bin --abstraction data\abstraction_prod_pluribus_v1.bin --iters 64 --time-ms 500 --depth 2 --threads 1 --seed 20260302
search-server ready: runtime_blueprint=data\smoke_det_runtime.bin nodes=24901
...
OK q=1 iters=64 solve_ms=... chosen=...
runtime-stats: cache_hits=... cache_misses=... decode_loads=... cache_resident_bytes=... prefetch_loads=... prefetch_bytes=...
```

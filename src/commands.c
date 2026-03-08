#ifdef _WIN32
#include <io.h>
#endif

static uint64_t cfr_train_next_chunk_iters(const CFRTrainOptions *opt,
                                           const CFRBlueprint *bp,
                                           uint64_t start_iter,
                                           uint64_t last_dump_iter)
{
    uint64_t chunk;
    uint64_t requested_chunk;

    requested_chunk = (opt->chunk_iters > 0) ? opt->chunk_iters : 1ULL;
    chunk = requested_chunk;
    if (opt->threads <= 1 && chunk > 1ULL)
    {
        chunk = 1ULL;
    }

    if (opt->strict_time_phases)
    {
        if (opt->threads <= 1)
        {
            /* Single-thread strict mode advances in single-iteration chunks. */
            chunk = 1ULL;
        }
        else
        {
            uint64_t strict_min_chunk;
            uint64_t strict_max_chunk;

            /*
             * Keep strict-time chunks small enough for frequent wall-clock phase checks,
             * but large enough to trigger parallel execution.
             */
            strict_min_chunk = (uint64_t)opt->threads * 8ULL;
            strict_max_chunk = (uint64_t)opt->threads * 256ULL;
            if (strict_max_chunk < strict_min_chunk)
            {
                strict_max_chunk = strict_min_chunk;
            }
            /*
             * In strict mode, derive chunk from requested chunk directly instead of
             * second-based small-batch fallback to preserve parallel utilization.
             */
            chunk = requested_chunk;
            if (chunk < strict_min_chunk)
            {
                chunk = strict_min_chunk;
            }
            if (chunk > strict_max_chunk)
            {
                chunk = strict_max_chunk;
            }
        }
    }
    else if (opt->seconds_limit > 0 || opt->dump_every_seconds > 0)
    {
        if (chunk > 8ULL)
        {
            chunk = 8ULL;
        }
    }

    if (opt->iterations > 0)
    {
        uint64_t done;
        uint64_t rem;

        done = bp->iteration - start_iter;
        rem = (done < opt->iterations) ? (opt->iterations - done) : 0ULL;
        if (rem < chunk)
        {
            chunk = rem;
        }
    }

    if (opt->status_every_iters > 0)
    {
        uint64_t gap;
        gap = opt->status_every_iters - (bp->iteration % opt->status_every_iters);
        if (gap == 0)
        {
            gap = opt->status_every_iters;
        }
        if (gap < chunk)
        {
            chunk = gap;
        }
    }

    if (opt->dump_every_iters > 0)
    {
        uint64_t elapsed;
        uint64_t gap;

        elapsed = bp->iteration - last_dump_iter;
        if (elapsed >= opt->dump_every_iters)
        {
            gap = 1ULL;
        }
        else
        {
            gap = opt->dump_every_iters - elapsed;
        }
        if (gap < chunk)
        {
            chunk = gap;
        }
    }
    if (opt->snapshot_every_iters > 0ULL)
    {
        uint64_t gap;
        gap = opt->snapshot_every_iters - (bp->iteration % opt->snapshot_every_iters);
        if (gap == 0ULL)
        {
            gap = opt->snapshot_every_iters;
        }
        if (gap < chunk)
        {
            chunk = gap;
        }
    }

    if (chunk == 0)
    {
        chunk = 1ULL;
    }
    return chunk;
}

static void cfr_hash32_mix_u32(uint32_t *h, uint32_t v)
{
    int i;
    for (i = 0; i < 4; ++i)
    {
        unsigned char b;
        b = (unsigned char)((v >> (i * 8)) & 0xFFu);
        *h ^= (uint32_t)b;
        *h *= 16777619u;
    }
}

static void cfr_hash32_mix_u64(uint32_t *h, uint64_t v)
{
    cfr_hash32_mix_u32(h, (uint32_t)(v & 0xFFFFFFFFu));
    cfr_hash32_mix_u32(h, (uint32_t)(v >> 32));
}

static int cfr_parse_parallel_mode(const char *text)
{
    if (text == NULL)
    {
        return -1;
    }
    if (_stricmp(text, "deterministic") == 0 || _stricmp(text, "det") == 0)
    {
        return CFR_PARALLEL_MODE_DETERMINISTIC;
    }
    if (_stricmp(text, "sharded") == 0 || _stricmp(text, "shared") == 0)
    {
        return CFR_PARALLEL_MODE_SHARDED;
    }
    return -1;
}

static const char *cfr_parallel_mode_name(int mode)
{
    if (mode == CFR_PARALLEL_MODE_SHARDED)
    {
        return "sharded";
    }
    return "deterministic";
}

static uint32_t cfr_train_compat_hash32(const CFRTrainOptions *opt)
{
    uint32_t h;
    uint64_t bits;

    if (opt == NULL)
    {
        return 0u;
    }

    h = 2166136261u;
    cfr_hash32_mix_u32(&h, (uint32_t)CFR_MAX_PLAYERS);
    cfr_hash32_mix_u32(&h, (uint32_t)CFR_MAX_ACTIONS);
    cfr_hash32_mix_u32(&h, (uint32_t)CFR_HISTORY_WINDOW);
    cfr_hash32_mix_u32(&h, (uint32_t)CFR_BLUEPRINT_VERSION);

    cfr_hash32_mix_u64(&h, opt->strategy_interval);
    cfr_hash32_mix_u32(&h, (uint32_t)opt->enable_linear_discount);
    cfr_hash32_mix_u64(&h, opt->linear_discount_every_iters);
    cfr_hash32_mix_u64(&h, opt->linear_discount_stop_iter);
    memcpy(&bits, &opt->linear_discount_scale, sizeof(bits));
    cfr_hash32_mix_u64(&h, bits);

    cfr_hash32_mix_u32(&h, (uint32_t)opt->enable_pruning);
    cfr_hash32_mix_u64(&h, opt->prune_start_iter);
    cfr_hash32_mix_u64(&h, opt->prune_full_every_iters);
    memcpy(&bits, &opt->prune_threshold, sizeof(bits));
    cfr_hash32_mix_u64(&h, bits);
    memcpy(&bits, &opt->prune_prob, sizeof(bits));
    cfr_hash32_mix_u64(&h, bits);

    cfr_hash32_mix_u32(&h, (uint32_t)opt->samples_per_player);
    cfr_hash32_mix_u32(&h, (uint32_t)opt->parallel_mode);
    cfr_hash32_mix_u32(&h, (uint32_t)opt->use_int_regret);
    cfr_hash32_mix_u32(&h, (uint32_t)opt->regret_floor);
    cfr_hash32_mix_u64(&h, opt->snapshot_every_iters);
    cfr_hash32_mix_u32(&h, (uint32_t)opt->snapshot_every_seconds);
    cfr_hash32_mix_u32(&h, (uint32_t)opt->strict_time_phases);
    cfr_hash32_mix_u64(&h, opt->discount_stop_seconds);
    cfr_hash32_mix_u64(&h, opt->prune_start_seconds);
    cfr_hash32_mix_u64(&h, opt->discount_every_seconds);
    cfr_hash32_mix_u64(&h, opt->warmup_seconds);
    cfr_hash32_mix_u64(&h, opt->snapshot_start_seconds);
    cfr_hash32_mix_u64(&h, opt->avg_start_seconds);
    cfr_hash32_mix_u32(&h, (uint32_t)opt->enable_preflop_avg);
    cfr_hash32_mix_u32(&h, (uint32_t)opt->preflop_avg_sampled);
    cfr_hash32_mix_u32(&h, (uint32_t)opt->abstraction_hash32);
    return h;
}

static void cfr_apply_pluribus_profile(CFRTrainOptions *opt)
{
    if (opt == NULL)
    {
        return;
    }

    opt->strategy_interval = 10000ULL;
    opt->enable_linear_discount = 1;
    opt->linear_discount_every_iters = 0ULL;
    opt->linear_discount_stop_iter = 0ULL;
    opt->linear_discount_scale = 1.0;
    opt->enable_pruning = 1;
    opt->prune_start_iter = 0ULL;
    opt->prune_full_every_iters = 20ULL;
    opt->prune_threshold = -300000000.0;
    opt->prune_prob = 1.0;
    opt->use_int_regret = 1;
    opt->regret_floor = -310000000;
    opt->snapshot_every_iters = 0ULL;
    opt->snapshot_every_seconds = 12000;
    opt->snapshot_start_seconds = 48000ULL;
    opt->discount_every_seconds = 600ULL;
    opt->warmup_seconds = 48000ULL;
    opt->avg_start_seconds = 48000ULL;
    opt->strict_time_phases = 1;
    opt->discount_stop_seconds = 24000ULL;
    opt->prune_start_seconds = 12000ULL;
    opt->enable_preflop_avg = 1;
    opt->preflop_avg_sampled = 1;
#ifdef _WIN32
    opt->enable_async_checkpoint = 1;
#else
    opt->enable_async_checkpoint = 0;
#endif
}

static int cfr_load_active_abstraction(const char *path, uint32_t *out_hash32)
{
    static CFRAbstractionConfig cfg;

    if (out_hash32 == NULL)
    {
        return 0;
    }

    if (path != NULL)
    {
        if (!cfr_abstraction_load(&cfg, path))
        {
            return 0;
        }
    }
    else
    {
        cfr_abstraction_set_defaults(&cfg);
    }

    if (!cfr_abstraction_use(&cfg))
    {
        return 0;
    }

    *out_hash32 = cfr_abstraction_effective_hash32();
    return 1;
}

static int cfr_check_blueprint_abstraction_compat(const CFRBlueprint *bp,
                                                  uint32_t abstraction_hash32,
                                                  int ignore_mismatch,
                                                  const char *context_name);

typedef struct
{
#ifdef _WIN32
    HANDLE thread;
#endif
    CFRBlueprint snapshot;
    char path[512];
    int save_kind;
    int active;
    int save_ok;
} CFRAsyncCheckpointWriter;

enum
{
    CFR_ASYNC_SAVE_BLUEPRINT = 0,
    CFR_ASYNC_SAVE_SNAPSHOT_POSTFLOP = 1
};

#ifdef _WIN32
static DWORD WINAPI cfr_async_checkpoint_thread_proc(LPVOID param)
{
    CFRAsyncCheckpointWriter *w;
    w = (CFRAsyncCheckpointWriter *)param;
    if (w == NULL)
    {
        return 1;
    }
    if (w->save_kind == CFR_ASYNC_SAVE_SNAPSHOT_POSTFLOP)
    {
        w->save_ok = cfr_snapshot_save_postflop_current(&w->snapshot, w->path) ? 1 : 0;
    }
    else
    {
        w->save_ok = cfr_blueprint_save(&w->snapshot, w->path) ? 1 : 0;
    }
    return 0;
}
#endif

static void cfr_async_writer_init(CFRAsyncCheckpointWriter *w)
{
    if (w == NULL)
    {
        return;
    }
    if (w->snapshot.alloc_guard == CFR_BLUEPRINT_ALLOC_GUARD)
    {
        cfr_blueprint_release(&w->snapshot);
    }
    memset(w, 0, sizeof(*w));
    memset(&w->snapshot, 0, sizeof(w->snapshot));
    if (!cfr_blueprint_init(&w->snapshot, 1ULL))
    {
        memset(&w->snapshot, 0, sizeof(w->snapshot));
    }
    w->save_kind = CFR_ASYNC_SAVE_BLUEPRINT;
    w->active = 0;
    w->save_ok = 1;
}

static int cfr_async_writer_drain(CFRAsyncCheckpointWriter *w)
{
    if (w == NULL)
    {
        return 0;
    }
#ifdef _WIN32
    if (w->active)
    {
        WaitForSingleObject(w->thread, INFINITE);
        CloseHandle(w->thread);
        w->thread = NULL;
        w->active = 0;
    }
#endif
    return w->save_ok;
}

static int cfr_async_writer_submit(CFRAsyncCheckpointWriter *w,
                                   const CFRBlueprint *bp,
                                   const char *path,
                                   int save_kind,
                                   int enable_async)
{
    if (w == NULL || bp == NULL || path == NULL)
    {
        return 0;
    }

    if (!enable_async)
    {
        if (save_kind == CFR_ASYNC_SAVE_SNAPSHOT_POSTFLOP)
        {
            w->save_ok = cfr_snapshot_save_postflop_current(bp, path) ? 1 : 0;
        }
        else
        {
            w->save_ok = cfr_blueprint_save(bp, path) ? 1 : 0;
        }
        return w->save_ok;
    }

#ifdef _WIN32
    if (w->active)
    {
        if (!cfr_async_writer_drain(w))
        {
            return 0;
        }
    }

    if (!cfr_blueprint_copy_from(&w->snapshot, bp))
    {
        if (save_kind == CFR_ASYNC_SAVE_SNAPSHOT_POSTFLOP)
        {
            w->save_ok = cfr_snapshot_save_postflop_current(bp, path) ? 1 : 0;
        }
        else
        {
            w->save_ok = cfr_blueprint_save(bp, path) ? 1 : 0;
        }
        return w->save_ok;
    }
    strncpy(w->path, path, sizeof(w->path) - 1);
    w->path[sizeof(w->path) - 1] = '\0';
    w->save_kind = save_kind;
    w->thread = CreateThread(NULL, 0, cfr_async_checkpoint_thread_proc, w, 0, NULL);
    if (w->thread == NULL)
    {
        if (save_kind == CFR_ASYNC_SAVE_SNAPSHOT_POSTFLOP)
        {
            w->save_ok = cfr_snapshot_save_postflop_current(bp, path) ? 1 : 0;
        }
        else
        {
            w->save_ok = cfr_blueprint_save(bp, path) ? 1 : 0;
        }
        return w->save_ok;
    }
    w->active = 1;
    return 1;
#else
    if (save_kind == CFR_ASYNC_SAVE_SNAPSHOT_POSTFLOP)
    {
        w->save_ok = cfr_snapshot_save_postflop_current(bp, path) ? 1 : 0;
    }
    else
    {
        w->save_ok = cfr_blueprint_save(bp, path) ? 1 : 0;
    }
    return w->save_ok;
#endif
}

static void cfr_compose_snapshot_path(char *out_path, size_t out_len, const char *dir, uint64_t iter)
{
    if (out_path == NULL || out_len == 0)
    {
        return;
    }
    if (dir == NULL || dir[0] == '\0')
    {
        dir = "data\\snapshots";
    }
    snprintf(out_path, out_len, "%s\\snapshot_%012llu.bin", dir, (unsigned long long)iter);
    out_path[out_len - 1] = '\0';
}

static void cfr_snapshot_avg_accumulate_postflop(CFRBlueprint *dst_avg, const CFRBlueprint *src)
{
    int i;

    if (dst_avg == NULL || src == NULL)
    {
        return;
    }

    cfr_blueprint_materialize_all((CFRBlueprint *)src);

    for (i = 0; i < (int)src->used_node_count; ++i)
    {
        const CFRNode *s;
        CFRNode *d;
        int a;

        s = &src->nodes[i];
        if (!cfr_node_is_used(s) || s->street_hint == 0u || s->street_hint > 3u)
        {
            continue;
        }

        d = cfr_blueprint_get_node_ex(dst_avg, s->key, 1, s->action_count, (int)s->street_hint);
        if (d == NULL)
        {
            continue;
        }
        if (d->street_hint > 3u)
        {
            d->street_hint = s->street_hint;
        }
        if (!cfr_blueprint_ensure_strategy_payload(dst_avg, d))
        {
            continue;
        }
        {
            float current[CFR_MAX_ACTIONS];
            cfr_compute_strategy_n(s, s->action_count, current);
            for (a = 0; a < s->action_count; ++a)
            {
                d->strategy_sum[a] += current[a];
            }
        }
    }
}

static void cfr_snapshot_avg_apply_postflop(CFRBlueprint *dst, const CFRBlueprint *avg, uint64_t snapshot_count)
{
    int i;

    if (dst == NULL || avg == NULL || snapshot_count == 0ULL)
    {
        return;
    }

    cfr_blueprint_materialize_all(dst);
    cfr_blueprint_materialize_all((CFRBlueprint *)avg);

    for (i = 0; i < (int)dst->used_node_count; ++i)
    {
        CFRNode *d;
        CFRNode *a;
        int k;

        d = &dst->nodes[i];
        if (!cfr_node_is_used(d) || d->street_hint == 0u || d->street_hint > 3u)
        {
            continue;
        }
        if (!cfr_blueprint_ensure_strategy_payload(dst, d))
        {
            continue;
        }

        a = cfr_blueprint_get_node((CFRBlueprint *)avg, d->key, 0, d->action_count);
        if (a == NULL)
        {
            continue;
        }
        if (a->strategy_sum == NULL)
        {
            continue;
        }

        for (k = 0; k < d->action_count; ++k)
        {
            if (k < a->action_count)
            {
                d->strategy_sum[k] = (float)((double)a->strategy_sum[k] / (double)snapshot_count);
            }
            else
            {
                d->strategy_sum[k] = 0.0f;
            }
        }
    }
}

static uint64_t cfr_train_elapsed_seconds_total(const CFRBlueprint *bp, uint64_t elapsed_at_start, time_t start_time)
{
    time_t now;
    uint64_t run_elapsed;

    if (bp == NULL)
    {
        return elapsed_at_start;
    }
    now = time(NULL);
    if (now < start_time)
    {
        run_elapsed = 0ULL;
    }
    else
    {
        run_elapsed = (uint64_t)(now - start_time);
    }
    return elapsed_at_start + run_elapsed;
}

static int cfr_train_discount_phase_active(const CFRTrainOptions *opt, uint64_t elapsed_seconds)
{
    if (opt == NULL || !opt->enable_linear_discount)
    {
        return 0;
    }
    if (opt->strict_time_phases && opt->discount_stop_seconds > 0ULL &&
        elapsed_seconds >= opt->discount_stop_seconds)
    {
        return 0;
    }
    return 1;
}

static int cfr_train_prune_phase_active(const CFRTrainOptions *opt, uint64_t elapsed_seconds)
{
    if (opt == NULL || !opt->enable_pruning)
    {
        return 0;
    }
    if (opt->strict_time_phases && elapsed_seconds < opt->prune_start_seconds)
    {
        return 0;
    }
    return 1;
}

static int cfr_train_preflop_avg_phase_active(const CFRTrainOptions *opt, uint64_t elapsed_seconds)
{
    if (opt == NULL || !opt->enable_preflop_avg)
    {
        return 0;
    }
    if (opt->avg_start_seconds > 0ULL && elapsed_seconds < opt->avg_start_seconds)
    {
        return 0;
    }
    return 1;
}

static int cfr_train_snapshot_phase_active(const CFRTrainOptions *opt, uint64_t elapsed_seconds)
{
    if (opt == NULL)
    {
        return 0;
    }
    if (opt->snapshot_every_iters == 0ULL && opt->snapshot_every_seconds <= 0)
    {
        return 0;
    }
    if (opt->snapshot_start_seconds > 0ULL && elapsed_seconds < opt->snapshot_start_seconds)
    {
        return 0;
    }
    return 1;
}

static void cfr_train_ensure_time_schedule_state(CFRBlueprint *bp, const CFRTrainOptions *opt)
{
    if (bp == NULL || opt == NULL)
    {
        return;
    }

    if (opt->enable_linear_discount && opt->discount_every_seconds > 0ULL)
    {
        if (bp->next_discount_second == 0ULL)
        {
            bp->next_discount_second = opt->discount_every_seconds;
        }
    }

    if (opt->snapshot_every_seconds > 0)
    {
        uint64_t start_at;
        start_at = (opt->snapshot_start_seconds > 0ULL)
                       ? opt->snapshot_start_seconds
                       : (uint64_t)opt->snapshot_every_seconds;
        if (bp->next_snapshot_second == 0ULL)
        {
            bp->next_snapshot_second = start_at;
        }
        else if (bp->next_snapshot_second < start_at)
        {
            bp->next_snapshot_second = start_at;
        }
    }
}

static void cfr_train_apply_time_discount_events(CFRBlueprint *bp,
                                                 const CFRTrainOptions *opt,
                                                 uint64_t elapsed_before,
                                                 uint64_t elapsed_after)
{
    if (bp == NULL || opt == NULL)
    {
        return;
    }
    if (!opt->enable_linear_discount || opt->discount_every_seconds == 0ULL)
    {
        return;
    }

    cfr_train_ensure_time_schedule_state(bp, opt);
    while (bp->next_discount_second > 0ULL &&
           bp->next_discount_second <= elapsed_after &&
           (opt->discount_stop_seconds == 0ULL || bp->next_discount_second <= opt->discount_stop_seconds))
    {
        if (bp->next_discount_second > elapsed_before)
        {
            cfr_apply_linear_discount_event_index(bp, opt, bp->discount_events_applied + 1ULL);
            bp->discount_events_applied++;
        }

        if (UINT64_MAX - bp->next_discount_second < opt->discount_every_seconds)
        {
            bp->next_discount_second = 0ULL;
            break;
        }
        bp->next_discount_second += opt->discount_every_seconds;
    }
}

static void cfr_train_refresh_runtime_state(CFRBlueprint *bp,
                                            const CFRTrainOptions *opt,
                                            uint64_t elapsed_at_start,
                                            time_t start_time)
{
    uint32_t flags;
    uint64_t elapsed_total;

    if (bp == NULL || opt == NULL)
    {
        return;
    }

    elapsed_total = cfr_train_elapsed_seconds_total(bp, elapsed_at_start, start_time);
    flags = 0u;
    if (cfr_train_discount_phase_active(opt, elapsed_total))
    {
        flags |= 1u;
    }
    if (cfr_train_prune_phase_active(opt, elapsed_total))
    {
        flags |= 2u;
    }
    if (cfr_train_preflop_avg_phase_active(opt, elapsed_total))
    {
        flags |= 4u;
    }
    if (cfr_train_snapshot_phase_active(opt, elapsed_total))
    {
        flags |= 8u;
    }
    bp->elapsed_train_seconds = elapsed_total;
    bp->phase_flags = flags;
}

static int cfr_train_do_snapshot_if_due(CFRAsyncCheckpointWriter *writer,
                                        CFRBlueprint *bp,
                                        const CFRTrainOptions *opt,
                                        uint64_t elapsed_at_start,
                                        time_t start_time,
                                        time_t *last_snapshot_time,
                                        uint64_t *last_snapshot_iter)
{
    char snapshot_path[512];

    if (writer == NULL || bp == NULL || opt == NULL || last_snapshot_time == NULL || last_snapshot_iter == NULL)
    {
        return 0;
    }

    if (!((opt->snapshot_every_iters > 0ULL && (bp->iteration - *last_snapshot_iter) >= opt->snapshot_every_iters) ||
          (opt->snapshot_every_seconds > 0 &&
           bp->next_snapshot_second > 0ULL &&
           bp->elapsed_train_seconds >= bp->next_snapshot_second &&
           cfr_train_snapshot_phase_active(opt, bp->elapsed_train_seconds))))
    {
        return 1;
    }

    cfr_compose_snapshot_path(snapshot_path, sizeof(snapshot_path), opt->snapshot_dir, bp->iteration);
    cfr_train_refresh_runtime_state(bp, opt, elapsed_at_start, start_time);
    if (!cfr_async_writer_submit(writer,
                                 bp,
                                 snapshot_path,
                                 CFR_ASYNC_SAVE_SNAPSHOT_POSTFLOP,
                                 opt->enable_async_checkpoint))
    {
        fprintf(stderr, "snapshot failed: %s\n", snapshot_path);
        return 0;
    }
    printf("snapshot: %s (iter=%llu)\n", snapshot_path, (unsigned long long)bp->iteration);
    *last_snapshot_iter = bp->iteration;
    *last_snapshot_time = time(NULL);
    if (opt->snapshot_every_seconds > 0 && bp->next_snapshot_second > 0ULL)
    {
        while (bp->next_snapshot_second <= bp->elapsed_train_seconds)
        {
            if (UINT64_MAX - bp->next_snapshot_second < (uint64_t)opt->snapshot_every_seconds)
            {
                bp->next_snapshot_second = 0ULL;
                break;
            }
            bp->next_snapshot_second += (uint64_t)opt->snapshot_every_seconds;
        }
    }
    return 1;
}

static int cfr_train_do_checkpoint_if_due(CFRAsyncCheckpointWriter *writer,
                                          CFRBlueprint *bp,
                                          const CFRTrainOptions *opt,
                                          uint64_t elapsed_at_start,
                                          time_t start_time,
                                          time_t now,
                                          time_t *last_dump_time,
                                          uint64_t *last_dump_iter)
{
    const char *label;

    if (writer == NULL || bp == NULL || opt == NULL || last_dump_time == NULL || last_dump_iter == NULL)
    {
        return 0;
    }

    label = NULL;
    if (opt->dump_every_iters > 0 && (bp->iteration - *last_dump_iter) >= opt->dump_every_iters)
    {
        label = "iter";
    }
    else if (opt->dump_every_seconds > 0 && (int)(now - *last_dump_time) >= opt->dump_every_seconds)
    {
        label = "time";
    }
    else
    {
        return 1;
    }

    cfr_train_refresh_runtime_state(bp, opt, elapsed_at_start, start_time);
    if (!cfr_async_writer_submit(writer,
                                 bp,
                                 opt->out_path,
                                 CFR_ASYNC_SAVE_BLUEPRINT,
                                 opt->enable_async_checkpoint))
    {
        fprintf(stderr, "checkpoint(%s) failed: %s\n", label, opt->out_path);
        return 0;
    }

    printf("checkpoint(%s): %s (iter=%llu)\n",
           label,
           opt->out_path,
           (unsigned long long)bp->iteration);
    *last_dump_iter = bp->iteration;
    *last_dump_time = time(NULL);
    return 1;
}

static int cfr_cmd_train(int argc, char **argv)
{
    CFRTrainOptions opt;
    static CFRBlueprint bp;
    time_t start_time;
    time_t last_dump_time;
    time_t last_snapshot_time;
    clock_t start_clock;
    clock_t last_status_clock;
    uint64_t elapsed_at_start;
    uint64_t start_iter;
    uint64_t last_dump_iter;
    uint64_t last_status_iter;
    uint64_t last_snapshot_iter;
    uint32_t compat_hash32;
    static CFRAsyncCheckpointWriter writer;
    int had_save_error;
    int had_train_error;
    int i;

    memset(&opt, 0, sizeof(opt));
    opt.iterations = 0;
    opt.seconds_limit = 0;
    opt.dump_every_iters = 1000;
    opt.dump_every_seconds = 0;
    opt.status_every_iters = 100;
    opt.threads = cfr_detect_hw_threads();
    opt.min_threads = 1;
    opt.parallel_mode = CFR_PARALLEL_MODE_DETERMINISTIC;
    opt.chunk_iters = 256;
    opt.samples_per_player = 1;
    opt.strategy_interval = 1ULL;
    opt.enable_linear_discount = 1;
    opt.linear_discount_every_iters = 1000;
    opt.linear_discount_stop_iter = 0ULL;
    opt.linear_discount_scale = 1.0;
    opt.enable_pruning = 1;
    opt.prune_start_iter = 2000;
    opt.prune_full_every_iters = 0ULL;
    opt.prune_threshold = -200.0;
    opt.prune_prob = 0.95;
    opt.use_int_regret = 0;
    opt.regret_floor = -2000000000;
    opt.snapshot_every_iters = 0ULL;
    opt.snapshot_every_seconds = 0;
    opt.snapshot_dir = "data\\snapshots";
    opt.strict_time_phases = 0;
    opt.discount_stop_seconds = 0ULL;
    opt.prune_start_seconds = 0ULL;
    opt.discount_every_seconds = 0ULL;
    opt.warmup_seconds = 0ULL;
    opt.snapshot_start_seconds = 0ULL;
    opt.avg_start_seconds = 0ULL;
    opt.enable_preflop_avg = 1;
    opt.preflop_avg_sampled = 0;
#ifdef _WIN32
    opt.enable_async_checkpoint = 1;
#else
    opt.enable_async_checkpoint = 0;
#endif
    opt.resume_ignore_compat = 0;
    opt.abstraction_hash32 = 0u;
    opt.abstraction_path = NULL;
    opt.out_path = "data\\blueprint.bin";
    opt.resume_path = NULL;
    opt.seed = 0ULL;
    opt.seed_set = 0;

    for (i = 0; i < argc; ++i)
    {
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
        {
            opt.out_path = argv[++i];
        }
        else if (strcmp(argv[i], "--resume") == 0 && i + 1 < argc)
        {
            opt.resume_path = argv[++i];
        }
        else if (strcmp(argv[i], "--abstraction") == 0 && i + 1 < argc)
        {
            opt.abstraction_path = argv[++i];
        }
        else if (strcmp(argv[i], "--iters") == 0 && i + 1 < argc)
        {
            opt.iterations = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
        {
            opt.seconds_limit = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--dump-iters") == 0 && i + 1 < argc)
        {
            opt.dump_every_iters = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--dump-seconds") == 0 && i + 1 < argc)
        {
            opt.dump_every_seconds = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--snapshot-iters") == 0 && i + 1 < argc)
        {
            opt.snapshot_every_iters = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--snapshot-seconds") == 0 && i + 1 < argc)
        {
            opt.snapshot_every_seconds = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--snapshot-dir") == 0 && i + 1 < argc)
        {
            opt.snapshot_dir = argv[++i];
        }
        else if (strcmp(argv[i], "--status-iters") == 0 && i + 1 < argc)
        {
            opt.status_every_iters = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
        {
            opt.threads = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--min-threads") == 0 && i + 1 < argc)
        {
            opt.min_threads = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--parallel-mode") == 0 && i + 1 < argc)
        {
            opt.parallel_mode = cfr_parse_parallel_mode(argv[++i]);
            if (opt.parallel_mode < 0)
            {
                fprintf(stderr, "Invalid --parallel-mode. Use deterministic|sharded\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--chunk-iters") == 0 && i + 1 < argc)
        {
            opt.chunk_iters = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--samples-per-player") == 0 && i + 1 < argc)
        {
            opt.samples_per_player = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--strategy-interval") == 0 && i + 1 < argc)
        {
            opt.strategy_interval = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--linear-discount-iters") == 0 && i + 1 < argc)
        {
            opt.linear_discount_every_iters = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--linear-discount-stop-iters") == 0 && i + 1 < argc)
        {
            opt.linear_discount_stop_iter = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--linear-discount-scale") == 0 && i + 1 < argc)
        {
            opt.linear_discount_scale = cfr_parse_f64(argv[++i]);
        }
        else if (strcmp(argv[i], "--prune-start") == 0 && i + 1 < argc)
        {
            opt.prune_start_iter = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--prune-threshold") == 0 && i + 1 < argc)
        {
            opt.prune_threshold = cfr_parse_f64(argv[++i]);
        }
        else if (strcmp(argv[i], "--prune-full-iters") == 0 && i + 1 < argc)
        {
            opt.prune_full_every_iters = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--prune-prob") == 0 && i + 1 < argc)
        {
            opt.prune_prob = cfr_parse_f64(argv[++i]);
        }
        else if (strcmp(argv[i], "--int-regret") == 0)
        {
            opt.use_int_regret = 1;
        }
        else if (strcmp(argv[i], "--float-regret") == 0)
        {
            opt.use_int_regret = 0;
        }
        else if (strcmp(argv[i], "--regret-floor") == 0 && i + 1 < argc)
        {
            opt.regret_floor = (int32_t)cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--pluribus-profile") == 0)
        {
            cfr_apply_pluribus_profile(&opt);
        }
        else if (strcmp(argv[i], "--no-prune") == 0)
        {
            opt.enable_pruning = 0;
        }
        else if (strcmp(argv[i], "--no-linear-discount") == 0)
        {
            opt.enable_linear_discount = 0;
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            opt.seed = cfr_parse_u64(argv[++i]);
            opt.seed_set = 1;
        }
        else if (strcmp(argv[i], "--resume-ignore-compat") == 0)
        {
            opt.resume_ignore_compat = 1;
        }
        else if (strcmp(argv[i], "--snapshot-postflop-avg") == 0 ||
                 strcmp(argv[i], "--no-snapshot-postflop-avg") == 0)
        {
            fprintf(stderr,
                    "Option %s was removed. Use two-stage flow: train with snapshots, then finalize-blueprint.\n",
                    argv[i]);
            return 1;
        }
        else if (strcmp(argv[i], "--strict-time-phases") == 0)
        {
            opt.strict_time_phases = 1;
        }
        else if (strcmp(argv[i], "--no-strict-time-phases") == 0)
        {
            opt.strict_time_phases = 0;
        }
        else if (strcmp(argv[i], "--discount-stop-seconds") == 0 && i + 1 < argc)
        {
            opt.discount_stop_seconds = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--prune-start-seconds") == 0 && i + 1 < argc)
        {
            opt.prune_start_seconds = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--discount-every-seconds") == 0 && i + 1 < argc)
        {
            opt.discount_every_seconds = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--warmup-seconds") == 0 && i + 1 < argc)
        {
            opt.warmup_seconds = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--snapshot-start-seconds") == 0 && i + 1 < argc)
        {
            opt.snapshot_start_seconds = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--avg-start-seconds") == 0 && i + 1 < argc)
        {
            opt.avg_start_seconds = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--no-preflop-avg") == 0)
        {
            opt.enable_preflop_avg = 0;
            if (opt.avg_start_seconds == 0ULL)
            {
                opt.avg_start_seconds = UINT64_MAX;
            }
        }
        else if (strcmp(argv[i], "--preflop-avg") == 0)
        {
            opt.enable_preflop_avg = 1;
        }
        else if (strcmp(argv[i], "--preflop-avg-mode") == 0 && i + 1 < argc)
        {
            const char *v;
            v = argv[++i];
            if (_stricmp(v, "sampled") == 0)
            {
                opt.preflop_avg_sampled = 1;
            }
            else if (_stricmp(v, "full") == 0)
            {
                opt.preflop_avg_sampled = 0;
            }
            else
            {
                fprintf(stderr, "Invalid --preflop-avg-mode value: %s (expected full|sampled)\n", v);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--async-checkpoint") == 0)
        {
            opt.enable_async_checkpoint = 1;
        }
        else if (strcmp(argv[i], "--no-async-checkpoint") == 0)
        {
            opt.enable_async_checkpoint = 0;
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            cfr_print_usage("main.exe");
            return 0;
        }
        else
        {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return 1;
        }
    }

    if (opt.iterations == 0 && opt.seconds_limit == 0)
    {
        opt.iterations = 1000;
    }
    if (opt.threads < 1)
    {
        opt.threads = 1;
    }
    if (opt.min_threads < 1)
    {
        opt.min_threads = 1;
    }
    if (opt.min_threads > opt.threads)
    {
        opt.min_threads = opt.threads;
    }
    if (opt.parallel_mode != CFR_PARALLEL_MODE_DETERMINISTIC &&
        opt.parallel_mode != CFR_PARALLEL_MODE_SHARDED)
    {
        opt.parallel_mode = CFR_PARALLEL_MODE_DETERMINISTIC;
    }
    if (opt.chunk_iters < 1ULL)
    {
        opt.chunk_iters = 1ULL;
    }
    if (opt.samples_per_player < 1)
    {
        opt.samples_per_player = 1;
    }
    if (opt.samples_per_player > 64)
    {
        opt.samples_per_player = 64;
    }
    if (opt.strategy_interval == 0ULL)
    {
        opt.strategy_interval = 1ULL;
    }
    if (opt.linear_discount_every_iters == 0ULL && opt.discount_every_seconds == 0ULL)
    {
        opt.enable_linear_discount = 0;
    }
    if (opt.linear_discount_scale <= 0.0)
    {
        opt.linear_discount_scale = 1.0;
    }
    if (opt.prune_prob < 0.0)
    {
        opt.prune_prob = 0.0;
    }
    if (opt.prune_prob > 1.0)
    {
        opt.prune_prob = 1.0;
    }
    if (opt.prune_prob == 0.0)
    {
        opt.enable_pruning = 0;
    }
    if (!opt.enable_pruning)
    {
        opt.prune_full_every_iters = 0ULL;
    }
    if (opt.regret_floor > 0)
    {
        opt.regret_floor = 0;
    }
    if (opt.snapshot_every_seconds < 0)
    {
        opt.snapshot_every_seconds = 0;
    }
    if (opt.warmup_seconds > 0ULL)
    {
        if (opt.snapshot_start_seconds == 0ULL)
        {
            opt.snapshot_start_seconds = opt.warmup_seconds;
        }
    }
#ifndef _WIN32
    opt.enable_async_checkpoint = 0;
#endif

    if (!cfr_load_active_abstraction(opt.abstraction_path, &opt.abstraction_hash32))
    {
        if (opt.abstraction_path != NULL)
        {
            fprintf(stderr, "Failed to load abstraction file: %s\n", opt.abstraction_path);
        }
        else
        {
            fprintf(stderr, "Failed to initialize default abstraction config\n");
        }
        return 1;
    }

    compat_hash32 = cfr_train_compat_hash32(&opt);

    if (cfr_is_data_path(opt.out_path))
    {
        cfr_ensure_data_dir();
    }
    if ((opt.snapshot_every_iters > 0ULL || opt.snapshot_every_seconds > 0) &&
        opt.snapshot_dir != NULL && opt.snapshot_dir[0] != '\0')
    {
        cfr_ensure_dir(opt.snapshot_dir);
    }

    if (opt.resume_path != NULL)
    {
        if (!cfr_blueprint_load(&bp, opt.resume_path))
        {
            fprintf(stderr, "Failed to load blueprint: %s\n", opt.resume_path);
            return 1;
        }
        if (bp.abstraction_hash32 != 0u && bp.abstraction_hash32 != opt.abstraction_hash32)
        {
            if (!opt.resume_ignore_compat)
            {
                fprintf(stderr,
                        "Resume abstraction mismatch: file hash=0x%08X current hash=0x%08X (use --resume-ignore-compat to override)\n",
                        (unsigned int)bp.abstraction_hash32,
                        (unsigned int)opt.abstraction_hash32);
                return 1;
            }
            else
            {
                printf("Warning: resume abstraction mismatch ignored (file hash=0x%08X current hash=0x%08X)\n",
                       (unsigned int)bp.abstraction_hash32,
                       (unsigned int)opt.abstraction_hash32);
            }
        }
        if (bp.compat_hash32 != 0u && bp.compat_hash32 != compat_hash32)
        {
            if (!opt.resume_ignore_compat)
            {
                fprintf(stderr,
                        "Resume compatibility mismatch: file hash=0x%08X current hash=0x%08X (use --resume-ignore-compat to override)\n",
                        (unsigned int)bp.compat_hash32,
                        (unsigned int)compat_hash32);
                return 1;
            }
            else
            {
                printf("Warning: resume compatibility mismatch ignored (file hash=0x%08X current hash=0x%08X)\n",
                       (unsigned int)bp.compat_hash32,
                       (unsigned int)compat_hash32);
            }
        }
        bp.compat_hash32 = compat_hash32;
        bp.abstraction_hash32 = opt.abstraction_hash32;
        printf("Resumed blueprint from %s (iter=%llu, hands=%llu)\n",
               opt.resume_path,
               (unsigned long long)bp.iteration,
               (unsigned long long)bp.total_hands);
    }
    else
    {
        uint64_t seed;

        seed = opt.seed_set ? opt.seed : (((uint64_t)time(NULL)) ^ 0xC6F6ULL);
        if (!cfr_blueprint_init(&bp, seed))
        {
            fprintf(stderr, "Failed to initialize blueprint\n");
            return 1;
        }
        bp.compat_hash32 = compat_hash32;
        bp.abstraction_hash32 = opt.abstraction_hash32;
        printf("Initialized new blueprint (seed=%llu)\n", (unsigned long long)bp.rng_state);
    }
    /* Raw blueprint keeps postflop strategy out of persistent payload; finalized postflop comes from snapshots. */
    bp.omit_postflop_strategy_sum = 1;

    printf("Trainer config: threads=%d min_threads=%d mode=%s chunk=%llu samples/player=%d strategy_interval=%llu linear_discount=%s(%llu, stop_iters=%llu, stop_seconds=%llu, every_seconds=%llu, scale=%.2f) pruning=%s(start_iters=%llu, start_seconds=%llu, full_every=%llu, threshold=%.2f, p=%.2f) regret_mode=%s(floor=%d) snapshots=%llu/%dsec(start=%llus, async=%s) avg_start=%llus preflop_avg=%s(%s) warmup=%llus strict_time=%s compat=0x%08X abstraction=0x%08X\n",
           opt.threads,
           opt.min_threads,
           cfr_parallel_mode_name(opt.parallel_mode),
           (unsigned long long)opt.chunk_iters,
           opt.samples_per_player,
           (unsigned long long)opt.strategy_interval,
           opt.enable_linear_discount ? "on" : "off",
           (unsigned long long)opt.linear_discount_every_iters,
           (unsigned long long)opt.linear_discount_stop_iter,
           (unsigned long long)opt.discount_stop_seconds,
           (unsigned long long)opt.discount_every_seconds,
           opt.linear_discount_scale,
           opt.enable_pruning ? "on" : "off",
           (unsigned long long)opt.prune_start_iter,
           (unsigned long long)opt.prune_start_seconds,
           (unsigned long long)opt.prune_full_every_iters,
           opt.prune_threshold,
           opt.prune_prob,
           opt.use_int_regret ? "int32" : "float",
           (int)opt.regret_floor,
           (unsigned long long)opt.snapshot_every_iters,
           opt.snapshot_every_seconds,
           (unsigned long long)opt.snapshot_start_seconds,
           opt.enable_async_checkpoint ? "on" : "off",
           (unsigned long long)opt.avg_start_seconds,
           opt.enable_preflop_avg ? "on" : "off",
           opt.preflop_avg_sampled ? "sampled" : "full",
           (unsigned long long)opt.warmup_seconds,
           opt.strict_time_phases ? "on" : "off",
           (unsigned int)compat_hash32,
           (unsigned int)opt.abstraction_hash32);

    if (!cfr_hand_index_init())
    {
        fprintf(stderr, "Failed to initialize hand-isomorphism indexer\n");
        return 1;
    }

    signal(SIGINT, cfr_on_signal);

    start_time = time(NULL);
    last_dump_time = start_time;
    last_snapshot_time = start_time;
    start_clock = clock();
    last_status_clock = start_clock;
    elapsed_at_start = bp.elapsed_train_seconds;
    start_iter = bp.iteration;
    last_dump_iter = bp.iteration;
    last_status_iter = bp.iteration;
    last_snapshot_iter = bp.iteration;
    cfr_async_writer_init(&writer);
    had_save_error = 0;
    had_train_error = 0;
    cfr_train_ensure_time_schedule_state(&bp, &opt);

    while (!g_cfr_stop_requested)
    {
        time_t now;

        now = time(NULL);
        cfr_train_refresh_runtime_state(&bp, &opt, elapsed_at_start, start_time);

        if (opt.iterations > 0 && (bp.iteration - start_iter) >= opt.iterations)
        {
            break;
        }
        if (opt.seconds_limit > 0 && (int)(now - start_time) >= opt.seconds_limit)
        {
            break;
        }

        if (!cfr_train_do_snapshot_if_due(&writer,
                                          &bp,
                                          &opt,
                                          elapsed_at_start,
                                          start_time,
                                          &last_snapshot_time,
                                          &last_snapshot_iter))
        {
            had_save_error = 1;
            break;
        }

        if (!cfr_train_do_checkpoint_if_due(&writer,
                                            &bp,
                                            &opt,
                                            elapsed_at_start,
                                            start_time,
                                            now,
                                            &last_dump_time,
                                            &last_dump_iter))
        {
            had_save_error = 1;
            break;
        }

        {
            CFRTrainOptions chunk_opt;
            uint64_t chunk_iters;
            uint64_t elapsed_before_chunk;
            uint64_t run_chunk_iters;
            int run_threads;
            int chunk_ok;

            elapsed_before_chunk = bp.elapsed_train_seconds;
            chunk_opt = opt;
            chunk_opt.enable_linear_discount = cfr_train_discount_phase_active(&opt, bp.elapsed_train_seconds);
            if (opt.discount_every_seconds > 0ULL)
            {
                /* Time-scheduled discounting is applied after each chunk by elapsed-seconds events. */
                chunk_opt.enable_linear_discount = 0;
            }
            chunk_opt.enable_preflop_avg = cfr_train_preflop_avg_phase_active(&opt, bp.elapsed_train_seconds);
            if (!cfr_train_prune_phase_active(&opt, bp.elapsed_train_seconds))
            {
                chunk_opt.enable_pruning = 0;
                chunk_opt.prune_full_every_iters = 0ULL;
            }

            chunk_iters = cfr_train_next_chunk_iters(&opt, &bp, start_iter, last_dump_iter);
            run_chunk_iters = chunk_iters;
            run_threads = chunk_opt.threads;
            chunk_ok = 0;

            for (;;)
            {
                uint64_t min_chunk_for_threads;
                uint64_t max_chunk_for_threads;

                chunk_opt.threads = run_threads;
                if (cfr_run_iterations(&bp, run_chunk_iters, &chunk_opt))
                {
                    chunk_ok = 1;
                    break;
                }

                if (run_threads > 1)
                {
                    (void)cfr_train_pool_force_reclaim(run_threads);
                }

                min_chunk_for_threads = 1ULL;
                if (opt.strict_time_phases && run_threads > 1)
                {
                    min_chunk_for_threads = (uint64_t)run_threads * 8ULL;
                }
                if (run_chunk_iters > min_chunk_for_threads)
                {
                    uint64_t prev_chunk;
                    prev_chunk = run_chunk_iters;
                    run_chunk_iters /= 2ULL;
                    if (run_chunk_iters < min_chunk_for_threads)
                    {
                        run_chunk_iters = min_chunk_for_threads;
                    }
                    fprintf(stderr,
                            "Parallel chunk retry: threads=%d, chunk %llu -> %llu\n",
                            run_threads,
                            (unsigned long long)prev_chunk,
                            (unsigned long long)run_chunk_iters);
                    continue;
                }

                if (run_threads > opt.min_threads)
                {
                    int prev_threads;
                    prev_threads = run_threads;
                    run_threads /= 2;
                    if (run_threads < opt.min_threads)
                    {
                        run_threads = opt.min_threads;
                    }
                    max_chunk_for_threads = (uint64_t)run_threads * 256ULL;
                    if (max_chunk_for_threads < 1ULL)
                    {
                        max_chunk_for_threads = 1ULL;
                    }
                    if (run_chunk_iters > max_chunk_for_threads)
                    {
                        run_chunk_iters = max_chunk_for_threads;
                    }
                    fprintf(stderr,
                            "Parallel chunk retry: threads %d -> %d, chunk=%llu\n",
                            prev_threads,
                            run_threads,
                            (unsigned long long)run_chunk_iters);
                    continue;
                }

                break;
            }
            if (!chunk_ok)
            {
                had_train_error = 1;
                break;
            }
            if (run_threads != opt.threads)
            {
                fprintf(stderr,
                        "Adaptive thread backoff applied: %d -> %d for remaining training.\n",
                        opt.threads,
                        run_threads);
                opt.threads = run_threads;
            }
            cfr_train_refresh_runtime_state(&bp, &opt, elapsed_at_start, start_time);
            cfr_train_apply_time_discount_events(&bp, &opt, elapsed_before_chunk, bp.elapsed_train_seconds);
        }
        if (had_train_error)
        {
            break;
        }

        if (opt.status_every_iters > 0 && (bp.iteration % opt.status_every_iters) == 0)
        {
            int used;
            double mem_alloc_mb;
            double mem_active_mb;
            double elapsed_total;
            double elapsed_status;
            double ips_avg;
            double ips_recent;
            uint64_t total_iters_done;
            uint64_t status_iters_done;
            int dump_age_sec;

            used = cfr_count_used_nodes(&bp);
            mem_alloc_mb = (double)cfr_blueprint_allocated_bytes(&bp) / (1024.0 * 1024.0);
            mem_active_mb = (double)cfr_blueprint_active_node_bytes(&bp) / (1024.0 * 1024.0);

            now = time(NULL);
            elapsed_total = ((double)(clock() - start_clock)) / (double)CLOCKS_PER_SEC;
            elapsed_status = ((double)(clock() - last_status_clock)) / (double)CLOCKS_PER_SEC;
            total_iters_done = bp.iteration - start_iter;
            status_iters_done = bp.iteration - last_status_iter;
            dump_age_sec = (int)(now - last_dump_time);

            ips_avg = (elapsed_total > 0.0) ? ((double)total_iters_done / elapsed_total) : 0.0;
            ips_recent = (elapsed_status > 0.0) ? ((double)status_iters_done / elapsed_status) : 0.0;

            printf("iter=%llu hands=%llu infosets=%d elapsed_sec=%llu phase=%s/%s/%s/%s discount_events=%llu dump_age_sec=%d mem_alloc_mb=%.2f mem_active_mb=%.2f iters/sec_recent=%.2f iters/sec_avg=%.2f\n",
                   (unsigned long long)bp.iteration,
                   (unsigned long long)bp.total_hands,
                   used,
                   (unsigned long long)bp.elapsed_train_seconds,
                   (bp.phase_flags & 1u) ? "discount-on" : "discount-off",
                   (bp.phase_flags & 2u) ? "prune-on" : "prune-off",
                   (bp.phase_flags & 4u) ? "avg-on" : "avg-off",
                   (bp.phase_flags & 8u) ? "snapshot-on" : "snapshot-off",
                   (unsigned long long)bp.discount_events_applied,
                   dump_age_sec,
                   mem_alloc_mb,
                   mem_active_mb,
                   ips_recent,
                   ips_avg);

            last_status_clock = clock();
            last_status_iter = bp.iteration;
        }

        now = time(NULL);
        if (!cfr_train_do_snapshot_if_due(&writer,
                                          &bp,
                                          &opt,
                                          elapsed_at_start,
                                          start_time,
                                          &last_snapshot_time,
                                          &last_snapshot_iter))
        {
            had_save_error = 1;
            break;
        }

        if (!cfr_train_do_checkpoint_if_due(&writer,
                                            &bp,
                                            &opt,
                                            elapsed_at_start,
                                            start_time,
                                            now,
                                            &last_dump_time,
                                            &last_dump_iter))
        {
            had_save_error = 1;
            break;
        }
    }

    if (!cfr_async_writer_drain(&writer))
    {
        fprintf(stderr, "checkpoint worker reported a save failure\n");
        had_save_error = 1;
    }

    if (had_save_error)
    {
        return 1;
    }
    if (had_train_error)
    {
        fprintf(stderr,
                "Training aborted: parallel chunk execution failed under current resource pressure. "
                "Adjust --threads/--min-threads/--chunk-iters and resume.\n");
        return 1;
    }

    cfr_train_refresh_runtime_state(&bp, &opt, elapsed_at_start, start_time);

    if (!cfr_blueprint_save(&bp, opt.out_path))
    {
        fprintf(stderr, "Final save failed: %s\n", opt.out_path);
        return 1;
    }

    printf("Saved blueprint: %s\n", opt.out_path);
    printf("Final iter=%llu hands=%llu infosets=%d\n",
           (unsigned long long)bp.iteration,
           (unsigned long long)bp.total_hands,
           cfr_count_used_nodes(&bp));

    if (g_cfr_stop_requested)
    {
        printf("Stopped by signal; safe checkpoint written.\n");
    }

    return 0;
}

static int cfr_cmp_cstr_ptr(const void *a, const void *b)
{
    const char *const *pa;
    const char *const *pb;
    pa = (const char *const *)a;
    pb = (const char *const *)b;
    return strcmp(*pa, *pb);
}

static int cfr_collect_snapshot_files(const char *snapshot_dir, char ***out_files, int *out_count)
{
    char **files;
    int count;
    int cap;
#ifdef _WIN32
    struct _finddata_t fd;
    intptr_t h;
    char pattern[512];

    if (snapshot_dir == NULL || out_files == NULL || out_count == NULL)
    {
        return 0;
    }

    snprintf(pattern, sizeof(pattern), "%s\\snapshot_*.bin", snapshot_dir);
    pattern[sizeof(pattern) - 1] = '\0';

    files = NULL;
    count = 0;
    cap = 0;

    h = _findfirst(pattern, &fd);
    if (h == -1)
    {
        *out_files = NULL;
        *out_count = 0;
        return 1;
    }

    do
    {
        char *path;
        if (fd.attrib & _A_SUBDIR)
        {
            continue;
        }
        if (count == cap)
        {
            int new_cap;
            char **new_files;
            new_cap = (cap > 0) ? (cap * 2) : 64;
            new_files = (char **)realloc(files, (size_t)new_cap * sizeof(*new_files));
            if (new_files == NULL)
            {
                int i;
                for (i = 0; i < count; ++i)
                {
                    free(files[i]);
                }
                free(files);
                _findclose(h);
                return 0;
            }
            files = new_files;
            cap = new_cap;
        }

        path = (char *)malloc(512u);
        if (path == NULL)
        {
            int i;
            for (i = 0; i < count; ++i)
            {
                free(files[i]);
            }
            free(files);
            _findclose(h);
            return 0;
        }
        snprintf(path, 512u, "%s\\%s", snapshot_dir, fd.name);
        path[511] = '\0';
        files[count++] = path;
    } while (_findnext(h, &fd) == 0);

    _findclose(h);
    if (count > 1)
    {
        qsort(files, (size_t)count, sizeof(*files), cfr_cmp_cstr_ptr);
    }
    *out_files = files;
    *out_count = count;
    return 1;
#else
    (void)snapshot_dir;
    (void)out_files;
    (void)out_count;
    return 0;
#endif
}

static void cfr_free_snapshot_files(char **files, int count)
{
    int i;
    if (files == NULL)
    {
        return;
    }
    for (i = 0; i < count; ++i)
    {
        free(files[i]);
    }
    free(files);
}

static int cfr_cmd_finalize_blueprint(int argc, char **argv)
{
    CFRFinalizeOptions opt;
    static CFRBlueprint raw_bp;
    static CFRBlueprint avg_bp;
    static CFRBlueprint snap_bp;
    uint32_t abstraction_hash32;
    char **snapshot_files;
    int snapshot_n;
    int i;
    uint64_t used_snapshots;
    uint64_t skipped_min_seconds;
    uint64_t skipped_compat;
    uint64_t skipped_hash;

    memset(&opt, 0, sizeof(opt));
    opt.raw_path = NULL;
    opt.out_path = NULL;
    opt.runtime_out_path = NULL;
    opt.snapshot_dir = "data\\snapshots";
    opt.abstraction_path = NULL;
    opt.ignore_abstraction_compat = 0;
    opt.write_full_output = 0;
    opt.snapshot_min_seconds = 0ULL;
    opt.runtime_quant_mode = CFR_RUNTIME_QUANT_U16;
    opt.runtime_shards = 256u;

    for (i = 0; i < argc; ++i)
    {
        if (strcmp(argv[i], "--raw") == 0 && i + 1 < argc)
        {
            opt.raw_path = argv[++i];
        }
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
        {
            opt.out_path = argv[++i];
            opt.write_full_output = 1;
        }
        else if (strcmp(argv[i], "--runtime-out") == 0 && i + 1 < argc)
        {
            opt.runtime_out_path = argv[++i];
        }
        else if (strcmp(argv[i], "--snapshot-dir") == 0 && i + 1 < argc)
        {
            opt.snapshot_dir = argv[++i];
        }
        else if (strcmp(argv[i], "--abstraction") == 0 && i + 1 < argc)
        {
            opt.abstraction_path = argv[++i];
        }
        else if (strcmp(argv[i], "--ignore-abstraction-compat") == 0)
        {
            opt.ignore_abstraction_compat = 1;
        }
        else if (strcmp(argv[i], "--snapshot-min-seconds") == 0 && i + 1 < argc)
        {
            opt.snapshot_min_seconds = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--runtime-quant") == 0 && i + 1 < argc)
        {
            const char *v;
            v = argv[++i];
            if (_stricmp(v, "u8") == 0)
            {
                opt.runtime_quant_mode = CFR_RUNTIME_QUANT_U8;
            }
            else if (_stricmp(v, "u16") == 0)
            {
                opt.runtime_quant_mode = CFR_RUNTIME_QUANT_U16;
            }
            else
            {
                fprintf(stderr, "Invalid --runtime-quant value: %s (expected u16|u8)\n", v);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--runtime-shards") == 0 && i + 1 < argc)
        {
            opt.runtime_shards = (uint32_t)cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            cfr_print_usage("main.exe");
            return 0;
        }
        else
        {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return 1;
        }
    }

    if (opt.raw_path == NULL)
    {
        fprintf(stderr, "--raw is required\n");
        return 1;
    }
    if (!opt.write_full_output && opt.runtime_out_path == NULL)
    {
        opt.out_path = "data\\blueprint_final.bin";
        opt.write_full_output = 1;
    }

    if (!cfr_load_active_abstraction(opt.abstraction_path, &abstraction_hash32))
    {
        fprintf(stderr, "Failed to load abstraction config\n");
        return 1;
    }

    if (!cfr_blueprint_load(&raw_bp, opt.raw_path))
    {
        fprintf(stderr, "Failed to load raw blueprint: %s\n", opt.raw_path);
        return 1;
    }

    if (!cfr_check_blueprint_abstraction_compat(&raw_bp,
                                                abstraction_hash32,
                                                opt.ignore_abstraction_compat,
                                                "finalize-blueprint(raw)"))
    {
        return 1;
    }

    if (!cfr_blueprint_init(&avg_bp, 1ULL) ||
        !cfr_blueprint_init(&snap_bp, 1ULL))
    {
        fprintf(stderr, "Failed to initialize finalize buffers\n");
        return 1;
    }

    snapshot_files = NULL;
    snapshot_n = 0;
    if (!cfr_collect_snapshot_files(opt.snapshot_dir, &snapshot_files, &snapshot_n))
    {
        fprintf(stderr, "Failed to enumerate snapshots in: %s\n", opt.snapshot_dir);
        return 1;
    }

    used_snapshots = 0ULL;
    skipped_min_seconds = 0ULL;
    skipped_compat = 0ULL;
    skipped_hash = 0ULL;
    printf("Finalize start: raw=%s snapshot_dir=%s full_out=%s runtime_out=%s\n",
           opt.raw_path,
           opt.snapshot_dir,
           opt.write_full_output ? opt.out_path : "(skip)",
           (opt.runtime_out_path != NULL) ? opt.runtime_out_path : "(skip)");
    printf("Scanning %d snapshots...\n", snapshot_n);
    for (i = 0; i < snapshot_n; ++i)
    {
        CFRSnapshotFileHeader sh;
        int is_compact;

        is_compact = cfr_snapshot_peek_header(snapshot_files[i], &sh);
        if (is_compact)
        {
            if (!opt.ignore_abstraction_compat &&
                sh.abstraction_hash32 != 0u &&
                abstraction_hash32 != 0u &&
                sh.abstraction_hash32 != abstraction_hash32)
            {
                skipped_compat++;
                continue;
            }
            if (sh.compat_hash32 != 0u &&
                raw_bp.compat_hash32 != 0u &&
                sh.compat_hash32 != raw_bp.compat_hash32)
            {
                skipped_hash++;
                continue;
            }
            if (opt.snapshot_min_seconds > 0ULL &&
                sh.elapsed_train_seconds < opt.snapshot_min_seconds)
            {
                skipped_min_seconds++;
                continue;
            }
            if (!cfr_snapshot_load_postflop_into_avg(&avg_bp,
                                                     snapshot_files[i],
                                                     NULL,
                                                     NULL,
                                                     NULL))
            {
                fprintf(stderr, "Skipping unreadable snapshot: %s\n", snapshot_files[i]);
                continue;
            }
            used_snapshots++;
            if (((i + 1) % 100u) == 0u || i + 1 == snapshot_n)
            {
                printf("  snapshots: scanned=%d/%d used=%llu skipped_min=%llu skipped_compat=%llu skipped_hash=%llu\n",
                       i + 1,
                       snapshot_n,
                       (unsigned long long)used_snapshots,
                       (unsigned long long)skipped_min_seconds,
                       (unsigned long long)skipped_compat,
                       (unsigned long long)skipped_hash);
            }
            continue;
        }

        if (!cfr_blueprint_load(&snap_bp, snapshot_files[i]))
        {
            fprintf(stderr, "Skipping unreadable snapshot: %s\n", snapshot_files[i]);
            continue;
        }
        if (!cfr_check_blueprint_abstraction_compat(&snap_bp,
                                                    abstraction_hash32,
                                                    opt.ignore_abstraction_compat,
                                                    "finalize-blueprint(snapshot)"))
        {
            skipped_compat++;
            continue;
        }
        if (snap_bp.compat_hash32 != 0u &&
            raw_bp.compat_hash32 != 0u &&
            snap_bp.compat_hash32 != raw_bp.compat_hash32)
        {
            skipped_hash++;
            continue;
        }
        if (opt.snapshot_min_seconds > 0ULL &&
            snap_bp.elapsed_train_seconds < opt.snapshot_min_seconds)
        {
            skipped_min_seconds++;
            continue;
        }
        cfr_snapshot_avg_accumulate_postflop(&avg_bp, &snap_bp);
        used_snapshots++;
        if (((i + 1) % 100u) == 0u || i + 1 == snapshot_n)
        {
            printf("  snapshots: scanned=%d/%d used=%llu skipped_min=%llu skipped_compat=%llu skipped_hash=%llu\n",
                   i + 1,
                   snapshot_n,
                   (unsigned long long)used_snapshots,
                   (unsigned long long)skipped_min_seconds,
                   (unsigned long long)skipped_compat,
                   (unsigned long long)skipped_hash);
        }
    }

    raw_bp.omit_postflop_strategy_sum = 0;

    if (used_snapshots > 0ULL)
    {
        cfr_snapshot_avg_apply_postflop(&raw_bp, &avg_bp, used_snapshots);
    }

    if (opt.write_full_output && cfr_is_data_path(opt.out_path))
    {
        cfr_ensure_data_dir();
    }
    if (opt.write_full_output && !cfr_blueprint_save(&raw_bp, opt.out_path))
    {
        cfr_free_snapshot_files(snapshot_files, snapshot_n);
        fprintf(stderr, "Failed to save finalized blueprint: %s\n", opt.out_path);
        return 1;
    }

    if (opt.write_full_output)
    {
        printf("Finalized blueprint saved: %s\n", opt.out_path);
    }
    else
    {
        printf("Finalized blueprint save skipped (runtime-only mode)\n");
    }
    printf("  raw blueprint: %s\n", opt.raw_path);
    printf("  snapshots scanned: %d\n", snapshot_n);
    printf("  snapshots used: %llu\n", (unsigned long long)used_snapshots);
    printf("  snapshots skipped(min-seconds): %llu\n", (unsigned long long)skipped_min_seconds);
    printf("  snapshots skipped(compat): %llu\n", (unsigned long long)skipped_compat);
    printf("  snapshots skipped(compat-hash): %llu\n", (unsigned long long)skipped_hash);
    printf("  abstraction hash: 0x%08X\n", (unsigned int)abstraction_hash32);

    if (opt.runtime_out_path != NULL)
    {
        if (cfr_is_data_path(opt.runtime_out_path))
        {
            cfr_ensure_data_dir();
        }
        if (!cfr_runtime_blueprint_save(&raw_bp, opt.runtime_out_path, opt.runtime_quant_mode, opt.runtime_shards))
        {
            cfr_free_snapshot_files(snapshot_files, snapshot_n);
            fprintf(stderr, "Failed to save runtime blueprint: %s\n", opt.runtime_out_path);
            return 1;
        }
        printf("  runtime artifact: %s (quant=%s shards=%u)\n",
               opt.runtime_out_path,
               (opt.runtime_quant_mode == CFR_RUNTIME_QUANT_U8) ? "u8" : "u16",
               (unsigned int)opt.runtime_shards);
    }

    cfr_free_snapshot_files(snapshot_files, snapshot_n);
    return 0;
}

static int cfr_cmd_bench(int argc, char **argv)
{
    static CFRBlueprint bp;
    uint64_t iterations;
    int max_threads;
    int samples_per_player;
    uint64_t chunk_iters;
    uint64_t seed;
    const char *csv_path;
    const char *json_path;
    FILE *csvf;
    FILE *jsonf;
    int json_rows;
    int i;
    int hw_threads;
    int thread_values[8];
    int n_thread_values;
    double base_ips;
    const char *abstraction_path;
    uint32_t abstraction_hash32;
    int parallel_mode;

    iterations = 200ULL;
    max_threads = 32;
    samples_per_player = 1;
    chunk_iters = 256ULL;
    seed = 123456789ULL;
    csv_path = NULL;
    json_path = NULL;
    csvf = NULL;
    jsonf = NULL;
    json_rows = 0;
    abstraction_path = NULL;
    abstraction_hash32 = 0u;
    parallel_mode = CFR_PARALLEL_MODE_DETERMINISTIC;

    for (i = 0; i < argc; ++i)
    {
        if (strcmp(argv[i], "--iters") == 0 && i + 1 < argc)
        {
            iterations = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--max-threads") == 0 && i + 1 < argc)
        {
            max_threads = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--samples-per-player") == 0 && i + 1 < argc)
        {
            samples_per_player = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--chunk-iters") == 0 && i + 1 < argc)
        {
            chunk_iters = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            seed = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--parallel-mode") == 0 && i + 1 < argc)
        {
            parallel_mode = cfr_parse_parallel_mode(argv[++i]);
            if (parallel_mode < 0)
            {
                fprintf(stderr, "Invalid --parallel-mode. Use deterministic|sharded\n");
                return 1;
            }
        }
        else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc)
        {
            csv_path = argv[++i];
        }
        else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc)
        {
            json_path = argv[++i];
        }
        else if (strcmp(argv[i], "--abstraction") == 0 && i + 1 < argc)
        {
            abstraction_path = argv[++i];
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            cfr_print_usage("main.exe");
            return 0;
        }
        else
        {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return 1;
        }
    }

    if (iterations < 1ULL)
    {
        iterations = 1ULL;
    }
    if (samples_per_player < 1)
    {
        samples_per_player = 1;
    }
    if (samples_per_player > 64)
    {
        samples_per_player = 64;
    }
    if (chunk_iters < 1ULL)
    {
        chunk_iters = 1ULL;
    }
    if (max_threads < 1)
    {
        max_threads = 1;
    }
    if (csv_path != NULL && cfr_is_data_path(csv_path))
    {
        cfr_ensure_data_dir();
    }
    if (json_path != NULL && cfr_is_data_path(json_path))
    {
        cfr_ensure_data_dir();
    }

    hw_threads = cfr_detect_hw_threads();
    if (max_threads > hw_threads)
    {
        max_threads = hw_threads;
    }
    if (max_threads < 1)
    {
        max_threads = 1;
    }

    if (!cfr_hand_index_init())
    {
        fprintf(stderr, "Failed to initialize hand-isomorphism indexer\n");
        return 1;
    }
    if (!cfr_load_active_abstraction(abstraction_path, &abstraction_hash32))
    {
        if (abstraction_path != NULL)
        {
            fprintf(stderr, "Failed to load abstraction file: %s\n", abstraction_path);
        }
        else
        {
            fprintf(stderr, "Failed to initialize default abstraction config\n");
        }
        return 1;
    }
    if (csv_path != NULL)
    {
        csvf = fopen(csv_path, "w");
        if (csvf == NULL)
        {
            fprintf(stderr, "Failed to open CSV output: %s\n", csv_path);
            return 1;
        }
        fprintf(csvf, "mode,threads,iters_per_sec,hands_per_sec,speedup,infosets,mem_alloc_mb,mem_active_mb,iterations,samples_per_player,chunk_iters,seed\n");
    }
    if (json_path != NULL)
    {
        jsonf = fopen(json_path, "w");
        if (jsonf == NULL)
        {
            if (csvf != NULL)
            {
                fclose(csvf);
            }
            fprintf(stderr, "Failed to open JSON output: %s\n", json_path);
            return 1;
        }
        fprintf(jsonf,
                "{\n"
                "  \"iterations\": %llu,\n"
                "  \"samples_per_player\": %d,\n"
                "  \"chunk_iters\": %llu,\n"
                "  \"seed\": %llu,\n"
                "  \"parallel_mode\": \"%s\",\n"
                "  \"max_threads\": %d,\n"
                "  \"hardware_threads\": %d,\n"
                "  \"runs\": [\n",
                (unsigned long long)iterations,
                samples_per_player,
                (unsigned long long)chunk_iters,
                (unsigned long long)seed,
                cfr_parallel_mode_name(parallel_mode),
                max_threads,
                hw_threads);
    }

    thread_values[0] = 1;
    thread_values[1] = 2;
    thread_values[2] = 4;
    thread_values[3] = 8;
    thread_values[4] = 16;
    thread_values[5] = 24;
    thread_values[6] = 32;
    n_thread_values = 7;
    base_ips = 0.0;

    printf("Benchmark settings: mode=%s iters=%llu samples/player=%d chunk-iters=%llu seed=%llu max-threads=%d (hw=%d) abstraction=0x%08X\n",
           cfr_parallel_mode_name(parallel_mode),
           (unsigned long long)iterations,
           samples_per_player,
           (unsigned long long)chunk_iters,
           (unsigned long long)seed,
           max_threads,
           hw_threads,
           (unsigned int)abstraction_hash32);
    printf("%-7s %-12s %-12s %-9s %-10s %-12s %-12s\n",
           "threads",
           "iters/sec",
           "hands/sec",
           "speedup",
           "infosets",
           "mem_alloc_mb",
           "mem_active_mb");

    for (i = 0; i < n_thread_values; ++i)
    {
        int t;
        CFRTrainOptions opt;
        double t0;
        double t1;
        double elapsed;
        double ips;
        double hps;
        double speedup;
        int infosets;
        double mem_alloc_mb;
        double mem_active_mb;

        t = thread_values[i];
        if (t > max_threads)
        {
            continue;
        }

        if (!cfr_blueprint_init(&bp, seed))
        {
            fprintf(stderr, "Failed to initialize blueprint for thread count %d\n", t);
            return 1;
        }
        bp.omit_postflop_strategy_sum = 1;

        memset(&opt, 0, sizeof(opt));
        opt.threads = t;
        opt.parallel_mode = parallel_mode;
        opt.chunk_iters = chunk_iters;
        opt.samples_per_player = samples_per_player;
        opt.strategy_interval = 1ULL;
        opt.enable_linear_discount = 0;
        opt.linear_discount_every_iters = 0ULL;
        opt.linear_discount_stop_iter = 0ULL;
        opt.linear_discount_scale = 1.0;
        opt.enable_pruning = 0;
        opt.prune_start_iter = 0ULL;
        opt.prune_full_every_iters = 0ULL;
        opt.prune_threshold = 0.0;
        opt.prune_prob = 0.0;
        opt.use_int_regret = 0;
        opt.regret_floor = -2000000000;
        opt.abstraction_hash32 = abstraction_hash32;

        t0 = cfr_wall_seconds();
        if (!cfr_run_iterations(&bp, iterations, &opt))
        {
            fprintf(stderr,
                    "Benchmark aborted: parallel execution failed for threads=%d mode=%s\n",
                    t,
                    cfr_parallel_mode_name(parallel_mode));
            return 1;
        }
        t1 = cfr_wall_seconds();

        elapsed = t1 - t0;
        if (elapsed <= 0.0)
        {
            elapsed = 1e-9;
        }

        ips = (double)iterations / elapsed;
        hps = (double)bp.total_hands / elapsed;
        infosets = cfr_count_used_nodes(&bp);
        mem_alloc_mb = (double)cfr_blueprint_allocated_bytes(&bp) / (1024.0 * 1024.0);
        mem_active_mb = (double)cfr_blueprint_active_node_bytes(&bp) / (1024.0 * 1024.0);

        if (base_ips <= 0.0)
        {
            base_ips = ips;
        }
        speedup = ips / base_ips;

        printf("%-7d %-12.2f %-12.2f %-9.2f %-10d %-12.2f %-12.2f\n",
               t,
               ips,
               hps,
               speedup,
               infosets,
               mem_alloc_mb,
               mem_active_mb);
        if (csvf != NULL)
        {
            fprintf(csvf, "%s,%d,%.9f,%.9f,%.9f,%d,%.9f,%.9f,%llu,%d,%llu,%llu\n",
                    cfr_parallel_mode_name(parallel_mode),
                    t,
                    ips,
                    hps,
                    speedup,
                    infosets,
                    mem_alloc_mb,
                    mem_active_mb,
                    (unsigned long long)iterations,
                    samples_per_player,
                    (unsigned long long)chunk_iters,
                    (unsigned long long)seed);
        }
        if (jsonf != NULL)
        {
            if (json_rows > 0)
            {
                fprintf(jsonf, ",\n");
            }
            fprintf(jsonf,
                    "    {\"mode\": \"%s\", \"threads\": %d, \"iters_per_sec\": %.9f, \"hands_per_sec\": %.9f, \"speedup\": %.9f, \"infosets\": %d, \"mem_alloc_mb\": %.9f, \"mem_active_mb\": %.9f}",
                    cfr_parallel_mode_name(parallel_mode),
                    t,
                    ips,
                    hps,
                    speedup,
                    infosets,
                    mem_alloc_mb,
                    mem_active_mb);
            json_rows++;
        }
    }

    if (csvf != NULL)
    {
        fclose(csvf);
        printf("CSV saved: %s\n", csv_path);
    }
    if (jsonf != NULL)
    {
        fprintf(jsonf, "\n  ]\n}\n");
        fclose(jsonf);
        printf("JSON saved: %s\n", json_path);
    }

    return 0;
}

static int cfr_cmd_abstraction_build(int argc, char **argv)
{
    static CFRAbstractionConfig cfg;
    const char *out_path;
    const char *cluster_algo_name;
    int build_centroids;
    int i;

    cfr_abstraction_set_defaults(&cfg);
    out_path = "data\\abstraction.bin";
    cluster_algo_name = "emd-kmedoids";
    build_centroids = 1;

    for (i = 0; i < argc; ++i)
    {
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
        {
            out_path = argv[++i];
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            cfg.seed = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--mc-samples") == 0 && i + 1 < argc)
        {
            cfg.feature_mc_samples = (uint32_t)cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--kmeans-iters") == 0 && i + 1 < argc)
        {
            cfg.kmeans_iters = (uint32_t)cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--samples") == 0 && i + 1 < argc)
        {
            cfg.build_samples_per_street = (uint32_t)cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--cluster-algo") == 0 && i + 1 < argc)
        {
            const char *v;
            v = argv[++i];
            if (_stricmp(v, "legacy") == 0)
            {
                cfg.clustering_algo = CFR_ABS_CLUSTER_ALGO_LEGACY;
                cluster_algo_name = "legacy";
            }
            else if (_stricmp(v, "emd-kmedoids") == 0)
            {
                cfg.clustering_algo = CFR_ABS_CLUSTER_ALGO_EMD_KMEDOIDS;
                cluster_algo_name = "emd-kmedoids";
            }
            else
            {
                fprintf(stderr, "Invalid --cluster-algo value: %s (expected legacy|emd-kmedoids)\n", v);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--bp-flop") == 0 && i + 1 < argc)
        {
            cfg.street_bucket_count_blueprint[1] = (uint32_t)cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--bp-turn") == 0 && i + 1 < argc)
        {
            cfg.street_bucket_count_blueprint[2] = (uint32_t)cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--bp-river") == 0 && i + 1 < argc)
        {
            cfg.street_bucket_count_blueprint[3] = (uint32_t)cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--search-flop") == 0 && i + 1 < argc)
        {
            cfg.street_bucket_count_search[1] = (uint32_t)cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--search-turn") == 0 && i + 1 < argc)
        {
            cfg.street_bucket_count_search[2] = (uint32_t)cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--search-river") == 0 && i + 1 < argc)
        {
            cfg.street_bucket_count_search[3] = (uint32_t)cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--no-centroids") == 0)
        {
            build_centroids = 0;
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            cfr_print_usage("main.exe");
            return 0;
        }
        else
        {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return 1;
        }
    }

    cfg.magic = CFR_ABSTRACTION_MAGIC;
    cfg.version = CFR_ABSTRACTION_VERSION;
    cfg.street_bucket_count_blueprint[0] = 169u;
    cfg.street_bucket_count_search[0] = 169u;
    if (!cfr_abstraction_validate(&cfg))
    {
        fprintf(stderr, "Invalid abstraction configuration\n");
        return 1;
    }

    if (build_centroids)
    {
        printf("Building abstraction centroids (algo=%s, mc=%u, samples/street=%u, kmeans-iters=%u)...\n",
               cluster_algo_name,
               (unsigned int)cfg.feature_mc_samples,
               (unsigned int)cfg.build_samples_per_street,
               (unsigned int)cfg.kmeans_iters);
        if (!cfr_abstraction_build_centroids(&cfg))
        {
            fprintf(stderr, "Failed to build abstraction centroids\n");
            return 1;
        }
    }
    else
    {
        memset(cfg.centroid_ready, 0, sizeof(cfg.centroid_ready));
        memset(cfg.emd_ready, 0, sizeof(cfg.emd_ready));
        memset(cfg.emd_quality, 0, sizeof(cfg.emd_quality));
    }

    cfg.hash32 = cfr_abstraction_hash32(&cfg);

    if (cfr_is_data_path(out_path))
    {
        cfr_ensure_data_dir();
    }

    if (!cfr_abstraction_save(&cfg, out_path))
    {
        fprintf(stderr, "Failed to save abstraction file: %s\n", out_path);
        return 1;
    }

    printf("Saved abstraction: %s\n", out_path);
    printf("Abstraction hash: 0x%08X\n", (unsigned int)cfg.hash32);
    printf("Blueprint buckets: preflop=%u flop=%u turn=%u river=%u\n",
           (unsigned int)cfg.street_bucket_count_blueprint[0],
           (unsigned int)cfg.street_bucket_count_blueprint[1],
           (unsigned int)cfg.street_bucket_count_blueprint[2],
           (unsigned int)cfg.street_bucket_count_blueprint[3]);
    printf("Search buckets:    preflop=%u flop=%u turn=%u river=%u\n",
           (unsigned int)cfg.street_bucket_count_search[0],
           (unsigned int)cfg.street_bucket_count_search[1],
           (unsigned int)cfg.street_bucket_count_search[2],
           (unsigned int)cfg.street_bucket_count_search[3]);
    printf("Centroids:         bp(flop/turn/river)=%u/%u/%u search(flop/turn/river)=%u/%u/%u\n",
           (unsigned int)cfg.centroid_ready[CFR_ABS_MODE_BLUEPRINT][1],
           (unsigned int)cfg.centroid_ready[CFR_ABS_MODE_BLUEPRINT][2],
           (unsigned int)cfg.centroid_ready[CFR_ABS_MODE_BLUEPRINT][3],
           (unsigned int)cfg.centroid_ready[CFR_ABS_MODE_SEARCH][1],
           (unsigned int)cfg.centroid_ready[CFR_ABS_MODE_SEARCH][2],
           (unsigned int)cfg.centroid_ready[CFR_ABS_MODE_SEARCH][3]);
    printf("Clustering:        algo=%s emd_bins=%u emd_ready bp=%u/%u/%u search=%u/%u/%u\n",
           (cfg.clustering_algo == CFR_ABS_CLUSTER_ALGO_EMD_KMEDOIDS) ? "emd-kmedoids" : "legacy",
           (unsigned int)cfg.emd_bins,
           (unsigned int)cfg.emd_ready[CFR_ABS_MODE_BLUEPRINT][1],
           (unsigned int)cfg.emd_ready[CFR_ABS_MODE_BLUEPRINT][2],
           (unsigned int)cfg.emd_ready[CFR_ABS_MODE_BLUEPRINT][3],
           (unsigned int)cfg.emd_ready[CFR_ABS_MODE_SEARCH][1],
           (unsigned int)cfg.emd_ready[CFR_ABS_MODE_SEARCH][2],
           (unsigned int)cfg.emd_ready[CFR_ABS_MODE_SEARCH][3]);
    printf("EMD quality:       bp(flop/turn/river) intra=%.6f/%.6f/%.6f sep=%.6f/%.6f/%.6f\n",
           (double)cfg.emd_quality[CFR_ABS_MODE_BLUEPRINT][1][0],
           (double)cfg.emd_quality[CFR_ABS_MODE_BLUEPRINT][2][0],
           (double)cfg.emd_quality[CFR_ABS_MODE_BLUEPRINT][3][0],
           (double)cfg.emd_quality[CFR_ABS_MODE_BLUEPRINT][1][1],
           (double)cfg.emd_quality[CFR_ABS_MODE_BLUEPRINT][2][1],
           (double)cfg.emd_quality[CFR_ABS_MODE_BLUEPRINT][3][1]);
    printf("EMD quality:       search(flop/turn/river) intra=%.6f/%.6f/%.6f sep=%.6f/%.6f/%.6f\n",
           (double)cfg.emd_quality[CFR_ABS_MODE_SEARCH][1][0],
           (double)cfg.emd_quality[CFR_ABS_MODE_SEARCH][2][0],
           (double)cfg.emd_quality[CFR_ABS_MODE_SEARCH][3][0],
           (double)cfg.emd_quality[CFR_ABS_MODE_SEARCH][1][1],
           (double)cfg.emd_quality[CFR_ABS_MODE_SEARCH][2][1],
           (double)cfg.emd_quality[CFR_ABS_MODE_SEARCH][3][1]);
    return 0;
}

static int cfr_build_state_with_known_hole(CFRHandState *st,
                                           int player_seat,
                                           int dealer_seat,
                                           int active_players,
                                           int pot,
                                           int to_call,
                                           int stack,
                                           int street,
                                           int raises_this_street,
                                           const int *hero_hole,
                                           const int *board_cards,
                                           int board_n,
                                           const unsigned char *hist_codes,
                                           int hist_n,
                                           uint64_t *rng)
{
    int i;
    int used[CFR_DECK_SIZE];
    int remaining[CFR_DECK_SIZE];
    int rem_n;
    int draw_pos;

    if (st == NULL || hero_hole == NULL || board_cards == NULL || rng == NULL)
    {
        return 0;
    }

    memset(st, 0, sizeof(*st));
    memset(used, 0, sizeof(used));
    rem_n = 0;

    st->dealer = ((dealer_seat % CFR_MAX_PLAYERS) + CFR_MAX_PLAYERS) % CFR_MAX_PLAYERS;
    st->street = street;
    st->pot = pot;
    st->to_call = to_call;
    st->current_player = player_seat;
    st->board_count = board_n;
    st->num_raises_street = raises_this_street;
    st->last_full_raise = CFR_BIG_BLIND;
    st->full_raise_seq = raises_this_street;

    for (i = 0; i < CFR_MAX_PLAYERS; ++i)
    {
        st->stack[i] = 0;
        st->in_hand[i] = 0;
        st->needs_action[i] = 0;
        st->committed_street[i] = 0;
        st->contributed_total[i] = 0;
        st->acted_on_full_raise_seq[i] = -1;
    }

    for (i = 0; i < active_players && i < CFR_MAX_PLAYERS; ++i)
    {
        st->in_hand[i] = 1;
        st->stack[i] = stack;
        st->needs_action[i] = 1;
    }
    if (!st->in_hand[player_seat])
    {
        st->in_hand[player_seat] = 1;
        st->stack[player_seat] = stack;
        st->needs_action[player_seat] = 1;
    }

    if (hero_hole[0] < 0 || hero_hole[0] >= CFR_DECK_SIZE || hero_hole[1] < 0 || hero_hole[1] >= CFR_DECK_SIZE || hero_hole[0] == hero_hole[1])
    {
        return 0;
    }
    st->hole[player_seat][0] = hero_hole[0];
    st->hole[player_seat][1] = hero_hole[1];
    used[hero_hole[0]] = 1;
    used[hero_hole[1]] = 1;

    for (i = 0; i < board_n; ++i)
    {
        int c;
        c = board_cards[i];
        if (c < 0 || c >= CFR_DECK_SIZE || used[c])
        {
            return 0;
        }
        st->board[i] = c;
        used[c] = 1;
    }

    for (i = 0; i < CFR_DECK_SIZE; ++i)
    {
        if (!used[i])
        {
            remaining[rem_n++] = i;
        }
    }
    for (i = rem_n - 1; i > 0; --i)
    {
        int j;
        int t;
        j = cfr_rng_int(rng, i + 1);
        t = remaining[i];
        remaining[i] = remaining[j];
        remaining[j] = t;
    }

    draw_pos = 0;
    for (i = 0; i < CFR_MAX_PLAYERS; ++i)
    {
        if (!st->in_hand[i] || i == player_seat)
        {
            continue;
        }
        if (draw_pos + 1 >= rem_n)
        {
            return 0;
        }
        st->hole[i][0] = remaining[draw_pos++];
        st->hole[i][1] = remaining[draw_pos++];
    }

    {
        int remain_after_holes;
        int start_pos;
        int k;
        remain_after_holes = rem_n - draw_pos;
        if (remain_after_holes < 0)
        {
            remain_after_holes = 0;
        }
        if (remain_after_holes > CFR_DECK_SIZE)
        {
            remain_after_holes = CFR_DECK_SIZE;
        }
        start_pos = CFR_DECK_SIZE - remain_after_holes;
        st->deck_pos = start_pos;
        for (k = 0; k < remain_after_holes; ++k)
        {
            st->deck[start_pos + k] = remaining[draw_pos + k];
        }
        for (k = 0; k < start_pos; ++k)
        {
            st->deck[k] = 0;
        }
    }

    st->history_len = 0;
    if (hist_codes != NULL && hist_n > 0)
    {
        if (hist_n > CFR_MAX_HISTORY)
        {
            hist_n = CFR_MAX_HISTORY;
        }
        memcpy(st->history, hist_codes, (size_t)hist_n);
        st->history_len = hist_n;
    }

    return 1;
}

static int cfr_is_round_separator_code(unsigned char code)
{
    return (code == 240u || code == 241u || code == 242u || code == 243u) ? 1 : 0;
}

static void cfr_history_segment_bounds_for_street(const unsigned char *hist_codes,
                                                  int hist_n,
                                                  int street,
                                                  int *out_prefix_end,
                                                  int *out_segment_begin,
                                                  int *out_segment_end)
{
    int i;
    int cur_street;
    int seg_begin;
    int seg_end;
    int saw_separator;

    cur_street = 0;
    seg_begin = 0;
    seg_end = hist_n;
    saw_separator = 0;
    if (hist_codes == NULL || hist_n <= 0)
    {
        if (out_prefix_end != NULL) *out_prefix_end = 0;
        if (out_segment_begin != NULL) *out_segment_begin = 0;
        if (out_segment_end != NULL) *out_segment_end = 0;
        return;
    }
    if (street <= 0)
    {
        for (i = 0; i < hist_n; ++i)
        {
            if (cfr_is_round_separator_code(hist_codes[i]))
            {
                seg_end = i;
                break;
            }
        }
        if (out_prefix_end != NULL) *out_prefix_end = 0;
        if (out_segment_begin != NULL) *out_segment_begin = 0;
        if (out_segment_end != NULL) *out_segment_end = seg_end;
        return;
    }

    for (i = 0; i < hist_n; ++i)
    {
        if (!cfr_is_round_separator_code(hist_codes[i]))
        {
            continue;
        }
        saw_separator = 1;
        if (cur_street == street)
        {
            seg_end = i;
            break;
        }
        cur_street++;
        if (cur_street == street)
        {
            seg_begin = i + 1;
        }
    }

    if (cur_street < street)
    {
        if (!saw_separator && street > 0)
        {
            /* If no round separators are provided, treat history as current-street local sequence. */
            seg_begin = 0;
            seg_end = hist_n;
        }
        else
        {
            seg_begin = hist_n;
            seg_end = hist_n;
        }
    }

    if (out_prefix_end != NULL) *out_prefix_end = seg_begin;
    if (out_segment_begin != NULL) *out_segment_begin = seg_begin;
    if (out_segment_end != NULL) *out_segment_end = seg_end;
}

static void cfr_prepare_round_root_state(CFRHandState *st,
                                         int street,
                                         int pot,
                                         const unsigned char *hist_codes,
                                         int hist_prefix_n)
{
    int i;

    if (st == NULL)
    {
        return;
    }

    st->street = street;
    st->pot = cfr_max_int(0, pot);
    st->to_call = 0;
    st->num_raises_street = 0;
    st->last_full_raise = CFR_BIG_BLIND;
    st->full_raise_seq = 0;
    st->is_terminal = 0;
    st->terminal_resolved = 0;
    for (i = 0; i < CFR_MAX_PLAYERS; ++i)
    {
        st->committed_street[i] = 0;
        st->needs_action[i] = (st->in_hand[i] && st->stack[i] > 0) ? 1 : 0;
        st->acted_this_street[i] = 0;
        st->acted_on_full_raise_seq[i] = -1;
    }
    st->current_player = cfr_first_actor_for_street(st);

    st->history_len = 0;
    if (hist_codes != NULL && hist_prefix_n > 0)
    {
        if (hist_prefix_n > CFR_MAX_HISTORY)
        {
            hist_prefix_n = CFR_MAX_HISTORY;
        }
        memcpy(st->history, hist_codes, (size_t)hist_prefix_n);
        st->history_len = hist_prefix_n;
    }
}

static int cfr_select_observed_raise_target(const CFRHandState *st,
                                            int player,
                                            int bucket,
                                            int *out_action,
                                            int *out_target)
{
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int raise_idx[CFR_MAX_ACTIONS];
    int legal_count;
    int raise_n;
    int i;
    int pick;

    if (st == NULL || out_action == NULL || out_target == NULL)
    {
        return 0;
    }
    legal_count = cfr_get_legal_actions(st, player, legal_actions, legal_targets);
    if (legal_count <= 0)
    {
        return 0;
    }

    raise_n = 0;
    for (i = 0; i < legal_count; ++i)
    {
        if (cfr_is_raise_action_code(legal_actions[i]))
        {
            raise_idx[raise_n++] = i;
        }
    }
    if (raise_n <= 0)
    {
        return 0;
    }

    if (bucket < 0)
    {
        pick = raise_n / 2;
    }
    else
    {
        pick = (bucket * (raise_n - 1)) / 255;
    }
    if (pick < 0) pick = 0;
    if (pick >= raise_n) pick = raise_n - 1;
    *out_action = legal_actions[raise_idx[pick]];
    *out_target = legal_targets[raise_idx[pick]];
    return 1;
}

static int cfr_build_search_round_root_with_frozen(CFRHandState *out_root,
                                                    CFRSearchFrozenAction *out_frozen,
                                                    int *out_frozen_n,
                                                    int frozen_cap,
                                                    int player_seat,
                                                    int dealer_seat,
                                                    int active_players,
                                                    int pot,
                                                    int to_call,
                                                    int stack,
                                                    int street,
                                                    int raises_this_street,
                                                    const int *hero_hole,
                                                    const int *board_cards,
                                                    int board_n,
                                                    const unsigned char *hist_codes,
                                                    int hist_n,
                                                    int offtree_mode,
                                                    uint64_t *rng)
{
    CFRHandState base;
    CFRHandState sim;
    int prefix_n;
    int seg_begin;
    int seg_end;
    int i;
    int frozen_n;
    uint64_t local_rng;

    if (out_root == NULL || out_frozen_n == NULL || rng == NULL)
    {
        return 0;
    }
    if (frozen_cap < 0)
    {
        return 0;
    }

    memset(&base, 0, sizeof(base));
    if (!cfr_build_state_with_known_hole(&base,
                                         player_seat,
                                         dealer_seat,
                                         active_players,
                                         pot,
                                         to_call,
                                         stack,
                                         street,
                                         raises_this_street,
                                         hero_hole,
                                         board_cards,
                                         board_n,
                                         hist_codes,
                                         hist_n,
                                         rng))
    {
        return 0;
    }

    cfr_history_segment_bounds_for_street(hist_codes, hist_n, street, &prefix_n, &seg_begin, &seg_end);

    if (seg_begin >= seg_end)
    {
        /* No current-street history to replay: preserve explicit state context (including to_call). */
        *out_root = base;
        *out_frozen_n = 0;
        return 1;
    }

    *out_root = base;
    cfr_prepare_round_root_state(out_root, street, pot, hist_codes, prefix_n);

    sim = *out_root;
    frozen_n = 0;
    local_rng = (*rng) ^ 0xE7037ED1A0B428DBULL;

    for (i = seg_begin; i < seg_end; ++i)
    {
        unsigned char code;
        int p;
        int req_action;
        int req_target;
        int bucket;

        code = hist_codes[i];
        if (cfr_is_round_separator_code(code))
        {
            continue;
        }
        if (code == 250u)
        {
            if (i + 1 < seg_end)
            {
                i++;
            }
            continue;
        }
        if (code != (unsigned char)CFR_ACT_FOLD &&
            code != (unsigned char)CFR_ACT_CALL_CHECK &&
            code != (unsigned char)CFR_ACT_RAISE_TO &&
            code != (unsigned char)CFR_ACT_ALL_IN)
        {
            continue;
        }

        cfr_auto_advance_rounds_if_needed(&sim);
        if (sim.is_terminal || sim.street != street)
        {
            break;
        }
        p = sim.current_player;
        if (p < 0 || p >= CFR_MAX_PLAYERS)
        {
            return 0;
        }

        req_action = (int)code;
        req_target = sim.to_call;
        bucket = -1;
        if (req_action == CFR_ACT_RAISE_TO)
        {
            if (i + 2 < seg_end && hist_codes[i + 1] == 250u)
            {
                bucket = (int)hist_codes[i + 2];
                i += 2;
            }
            if (!cfr_select_observed_raise_target(&sim, p, bucket, &req_action, &req_target))
            {
                return 0;
            }
        }
        else if (req_action == CFR_ACT_ALL_IN)
        {
            req_target = sim.committed_street[p] + sim.stack[p];
        }
        else if (req_action == CFR_ACT_FOLD)
        {
            req_target = 0;
        }

        if (frozen_n >= frozen_cap || out_frozen == NULL)
        {
            return 0;
        }
        out_frozen[frozen_n].player = p;
        out_frozen[frozen_n].action = req_action;
        out_frozen[frozen_n].target = req_target;
        frozen_n++;

        if (!cfr_search_apply_observed_action(&sim,
                                              p,
                                              req_action,
                                              req_target,
                                              offtree_mode,
                                              &local_rng,
                                              NULL))
        {
            return 0;
        }
    }

    *rng = local_rng;
    *out_frozen_n = frozen_n;
    return 1;
}

static int cfr_extract_round_root_with_frozen_from_state(const CFRHandState *state,
                                                         CFRHandState *out_root,
                                                         CFRSearchFrozenAction *out_frozen,
                                                         int *out_frozen_n,
                                                         int frozen_cap,
                                                         int offtree_mode,
                                                         uint64_t *rng)
{
    CFRHandState root;
    CFRHandState sim;
    int prefix_n;
    int seg_begin;
    int seg_end;
    int pot_round_start;
    int i;
    int frozen_n;

    if (state == NULL || out_root == NULL || out_frozen_n == NULL || rng == NULL || frozen_cap < 0)
    {
        return 0;
    }

    root = *state;
    cfr_history_segment_bounds_for_street(state->history,
                                          state->history_len,
                                          state->street,
                                          &prefix_n,
                                          &seg_begin,
                                          &seg_end);

    if (seg_begin >= seg_end)
    {
        /* No current-street actions to replay; state already represents the decision context. */
        *out_root = *state;
        *out_frozen_n = 0;
        return 1;
    }

    pot_round_start = state->pot;
    for (i = 0; i < CFR_MAX_PLAYERS; ++i)
    {
        pot_round_start -= state->committed_street[i];
    }
    if (pot_round_start < 0)
    {
        pot_round_start = 0;
    }

    cfr_prepare_round_root_state(&root,
                                 state->street,
                                 pot_round_start,
                                 state->history,
                                 prefix_n);

    sim = root;
    frozen_n = 0;
    for (i = seg_begin; i < seg_end; ++i)
    {
        unsigned char code;
        int p;
        int req_action;
        int req_target;
        int bucket;

        code = state->history[i];
        if (cfr_is_round_separator_code(code))
        {
            continue;
        }
        if (code == 250u)
        {
            if (i + 1 < seg_end)
            {
                i++;
            }
            continue;
        }
        if (code != (unsigned char)CFR_ACT_FOLD &&
            code != (unsigned char)CFR_ACT_CALL_CHECK &&
            code != (unsigned char)CFR_ACT_RAISE_TO &&
            code != (unsigned char)CFR_ACT_ALL_IN)
        {
            continue;
        }

        cfr_auto_advance_rounds_if_needed(&sim);
        if (sim.is_terminal || sim.street != root.street)
        {
            break;
        }
        p = sim.current_player;
        if (p < 0 || p >= CFR_MAX_PLAYERS)
        {
            return 0;
        }

        req_action = (int)code;
        req_target = sim.to_call;
        bucket = -1;
        if (req_action == CFR_ACT_RAISE_TO)
        {
            if (i + 2 < seg_end && state->history[i + 1] == 250u)
            {
                bucket = (int)state->history[i + 2];
                i += 2;
            }
            if (!cfr_select_observed_raise_target(&sim, p, bucket, &req_action, &req_target))
            {
                return 0;
            }
        }
        else if (req_action == CFR_ACT_ALL_IN)
        {
            req_target = sim.committed_street[p] + sim.stack[p];
        }
        else if (req_action == CFR_ACT_FOLD)
        {
            req_target = 0;
        }

        if (frozen_n >= frozen_cap || out_frozen == NULL)
        {
            return 0;
        }
        out_frozen[frozen_n].player = p;
        out_frozen[frozen_n].action = req_action;
        out_frozen[frozen_n].target = req_target;
        frozen_n++;

        if (!cfr_search_apply_observed_action(&sim,
                                              p,
                                              req_action,
                                              req_target,
                                              offtree_mode,
                                              rng,
                                              NULL))
        {
            return 0;
        }
    }

    *out_root = root;
    *out_frozen_n = frozen_n;
    return 1;
}

static int cfr_check_blueprint_abstraction_compat(const CFRBlueprint *bp,
                                                  uint32_t abstraction_hash32,
                                                  int ignore_mismatch,
                                                  const char *label)
{
    if (bp == NULL)
    {
        return 0;
    }
    if (bp->abstraction_hash32 != 0u && bp->abstraction_hash32 != abstraction_hash32)
    {
        if (!ignore_mismatch)
        {
            fprintf(stderr,
                    "%s abstraction mismatch: file hash=0x%08X current hash=0x%08X\n",
                    label,
                    (unsigned int)bp->abstraction_hash32,
                    (unsigned int)abstraction_hash32);
            return 0;
        }
        printf("Warning: %s abstraction mismatch ignored (file hash=0x%08X current hash=0x%08X)\n",
               label,
               (unsigned int)bp->abstraction_hash32,
               (unsigned int)abstraction_hash32);
    }
    return 1;
}

static int cfr_check_policy_provider_abstraction_compat(const CFRPolicyProvider *provider,
                                                        uint32_t abstraction_hash32,
                                                        int ignore_mismatch,
                                                        const char *label)
{
    uint32_t provider_hash32;

    if (provider == NULL)
    {
        return 0;
    }
    provider_hash32 = cfr_policy_provider_abstraction_hash32(provider);
    if (provider_hash32 != 0u && provider_hash32 != abstraction_hash32)
    {
        if (!ignore_mismatch)
        {
            fprintf(stderr,
                    "%s abstraction mismatch: file hash=0x%08X current hash=0x%08X\n",
                    label,
                    (unsigned int)provider_hash32,
                    (unsigned int)abstraction_hash32);
            return 0;
        }
        printf("Warning: %s abstraction mismatch ignored (file hash=0x%08X current hash=0x%08X)\n",
               label,
               (unsigned int)provider_hash32,
               (unsigned int)abstraction_hash32);
    }
    return 1;
}

static void cfr_search_options_set_defaults(CFRSearchOptions *opt)
{
    if (opt == NULL)
    {
        return;
    }
    memset(opt, 0, sizeof(*opt));
    opt->blueprint_path = NULL;
    opt->runtime_blueprint_path = NULL;
    opt->abstraction_path = NULL;
    opt->ignore_abstraction_compat = 0;
    opt->hole_text[0] = '\0';
    opt->board_text[0] = '\0';
    opt->history_text[0] = '\0';
    opt->player_seat = 0;
    opt->dealer_seat = 5;
    opt->active_players = 6;
    opt->pot = 3;
    opt->to_call = 0;
    opt->stack = CFR_START_STACK;
    opt->street = -1;
    opt->raises_this_street = 0;
    opt->iters = 400ULL;
    opt->time_ms = 0ULL;
    opt->depth = 3;
    opt->threads = cfr_detect_hw_threads();
    opt->search_pick_mode = CFR_SEARCH_PICK_SAMPLE_FINAL;
    opt->offtree_mode = CFR_OFFTREE_MODE_INJECT;
    opt->cache_bytes = 64ULL * 1024ULL * 1024ULL;
    opt->runtime_prefetch_mode = CFR_RUNTIME_PREFETCH_AUTO;
    opt->seed = 20260227ULL;
}

/* Return: 0=error, 1=ok, 2=help requested */
static int cfr_search_options_parse(CFRSearchOptions *opt,
                                    int argc,
                                    char **argv,
                                    int allow_model_paths,
                                    int allow_help,
                                    const char *error_prefix)
{
    int i;
    const char *prefix;
    int print_errors;

    if (opt == NULL)
    {
        return 0;
    }
    prefix = (error_prefix != NULL) ? error_prefix : "search";
    print_errors = (prefix[0] != '\0');

    for (i = 0; i < argc; ++i)
    {
        if (allow_model_paths && strcmp(argv[i], "--blueprint") == 0 && i + 1 < argc)
        {
            opt->blueprint_path = argv[++i];
        }
        else if (allow_model_paths && strcmp(argv[i], "--runtime-blueprint") == 0 && i + 1 < argc)
        {
            opt->runtime_blueprint_path = argv[++i];
        }
        else if (allow_model_paths && strcmp(argv[i], "--abstraction") == 0 && i + 1 < argc)
        {
            opt->abstraction_path = argv[++i];
        }
        else if (allow_model_paths && strcmp(argv[i], "--ignore-abstraction-compat") == 0)
        {
            opt->ignore_abstraction_compat = 1;
        }
        else if (strcmp(argv[i], "--hole") == 0 && i + 1 < argc)
        {
            strncpy(opt->hole_text, argv[++i], sizeof(opt->hole_text) - 1);
            opt->hole_text[sizeof(opt->hole_text) - 1] = '\0';
        }
        else if (strcmp(argv[i], "--board") == 0 && i + 1 < argc)
        {
            strncpy(opt->board_text, argv[++i], sizeof(opt->board_text) - 1);
            opt->board_text[sizeof(opt->board_text) - 1] = '\0';
        }
        else if (strcmp(argv[i], "--history") == 0 && i + 1 < argc)
        {
            strncpy(opt->history_text, argv[++i], sizeof(opt->history_text) - 1);
            opt->history_text[sizeof(opt->history_text) - 1] = '\0';
        }
        else if (strcmp(argv[i], "--street") == 0 && i + 1 < argc)
        {
            opt->street = cfr_parse_street_text(argv[++i]);
        }
        else if (strcmp(argv[i], "--player-seat") == 0 && i + 1 < argc)
        {
            opt->player_seat = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--dealer-seat") == 0 && i + 1 < argc)
        {
            opt->dealer_seat = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--active") == 0 && i + 1 < argc)
        {
            opt->active_players = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--pot") == 0 && i + 1 < argc)
        {
            opt->pot = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--to-call") == 0 && i + 1 < argc)
        {
            opt->to_call = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--stack") == 0 && i + 1 < argc)
        {
            opt->stack = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--raises") == 0 && i + 1 < argc)
        {
            opt->raises_this_street = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--iters") == 0 && i + 1 < argc)
        {
            opt->iters = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--time-ms") == 0 && i + 1 < argc)
        {
            opt->time_ms = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc)
        {
            opt->depth = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
        {
            opt->threads = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--search-pick") == 0 && i + 1 < argc)
        {
            const char *v;
            v = argv[++i];
            if (_stricmp(v, "sample-final") == 0)
            {
                opt->search_pick_mode = CFR_SEARCH_PICK_SAMPLE_FINAL;
            }
            else if (_stricmp(v, "argmax") == 0)
            {
                opt->search_pick_mode = CFR_SEARCH_PICK_ARGMAX;
            }
            else
            {
                if (print_errors)
                {
                    fprintf(stderr,
                            "%s: invalid --search-pick value: %s (expected sample-final|argmax)\n",
                            prefix,
                            v);
                }
                return 0;
            }
        }
        else if (strcmp(argv[i], "--offtree-mode") == 0 && i + 1 < argc)
        {
            const char *v;
            v = argv[++i];
            if (_stricmp(v, "inject") == 0)
            {
                opt->offtree_mode = CFR_OFFTREE_MODE_INJECT;
            }
            else if (_stricmp(v, "translate") == 0)
            {
                opt->offtree_mode = CFR_OFFTREE_MODE_TRANSLATE;
            }
            else
            {
                if (print_errors)
                {
                    fprintf(stderr,
                            "%s: invalid --offtree-mode value: %s (expected inject|translate)\n",
                            prefix,
                            v);
                }
                return 0;
            }
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            opt->seed = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--cache-bytes") == 0 && i + 1 < argc)
        {
            opt->cache_bytes = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--prefetch") == 0 && i + 1 < argc)
        {
            const char *v;
            v = argv[++i];
            if (_stricmp(v, "none") == 0)
            {
                opt->runtime_prefetch_mode = CFR_RUNTIME_PREFETCH_NONE;
            }
            else if (_stricmp(v, "auto") == 0)
            {
                opt->runtime_prefetch_mode = CFR_RUNTIME_PREFETCH_AUTO;
            }
            else if (_stricmp(v, "preflop") == 0)
            {
                opt->runtime_prefetch_mode = CFR_RUNTIME_PREFETCH_PREFLOP;
            }
            else
            {
                if (print_errors)
                {
                    fprintf(stderr,
                            "%s: invalid --prefetch value: %s (expected none|auto|preflop)\n",
                            prefix,
                            v);
                }
                return 0;
            }
        }
        else if (allow_help && strcmp(argv[i], "--help") == 0)
        {
            return 2;
        }
        else
        {
            if (print_errors)
            {
                fprintf(stderr, "%s: unknown or incomplete option: %s\n", prefix, argv[i]);
            }
            return 0;
        }
    }

    return 1;
}

static void cfr_search_options_normalize(CFRSearchOptions *opt)
{
    if (opt == NULL)
    {
        return;
    }
    if (opt->active_players < 2) opt->active_players = 2;
    if (opt->active_players > CFR_MAX_PLAYERS) opt->active_players = CFR_MAX_PLAYERS;
    if (opt->threads < 1) opt->threads = 1;
    if (opt->depth < 1) opt->depth = 1;
    if (opt->iters == 0ULL) opt->iters = 1ULL;
}

static int cfr_search_execute_with_policy_provider(const CFRSearchOptions *opt,
                                                   CFRPolicyProvider *provider,
                                                   CFRSearchContext *ctx,
                                                   CFRSearchDecision *decision,
                                                   double *solve_ms,
                                                   int *frozen_count)
{
    CFRHandState st;
    CFRSearchFrozenAction frozen[CFR_MAX_HISTORY];
    int hole_cards[2];
    int board_cards[5];
    int hole_n;
    int board_n;
    unsigned char hist_codes[CFR_MAX_HISTORY];
    int hist_n;
    uint64_t rng;
    int local_frozen_n;
    double solve_start_sec;
    CFRSearchOptions q;
    int restore_runtime_cache;

    if (opt == NULL || provider == NULL || ctx == NULL || decision == NULL)
    {
        return 0;
    }

    q = *opt;
    cfr_search_options_normalize(&q);
    restore_runtime_cache = provider->use_runtime_cache;
    if (q.hole_text[0] == '\0')
    {
        fprintf(stderr, "Search requires --hole\n");
        return 0;
    }

    hole_n = cfr_parse_cards(q.hole_text, hole_cards, 2);
    if (hole_n != 2)
    {
        fprintf(stderr, "Invalid --hole cards\n");
        return 0;
    }
    board_n = cfr_parse_cards(q.board_text, board_cards, 5);
    if (board_n < 0 || board_n > 5)
    {
        fprintf(stderr, "Invalid --board cards\n");
        return 0;
    }
    hist_n = cfr_parse_history_text(q.history_text, hist_codes, CFR_MAX_HISTORY);
    if (hist_n < 0)
    {
        fprintf(stderr, "Invalid --history format\n");
        return 0;
    }

    if (q.street < 0)
    {
        q.street = cfr_derive_street_from_board_count(board_n);
    }
    if (q.street < 0 || q.street > 3)
    {
        fprintf(stderr, "Street must be preflop/flop/turn/river\n");
        return 0;
    }

    rng = q.seed;
    local_frozen_n = 0;
    if (!cfr_build_search_round_root_with_frozen(&st,
                                                 frozen,
                                                 &local_frozen_n,
                                                 CFR_MAX_HISTORY,
                                                 q.player_seat,
                                                 q.dealer_seat,
                                                 q.active_players,
                                                 q.pot,
                                                 q.to_call,
                                                 q.stack,
                                                 q.street,
                                                 q.raises_this_street,
                                                 hole_cards,
                                                 board_cards,
                                                 board_n,
                                                 hist_codes,
                                                 hist_n,
                                                 q.offtree_mode,
                                                 &rng))
    {
        fprintf(stderr, "Failed to build round-root search state from provided context/history\n");
        return 0;
    }

    ctx->seed = q.seed;
    solve_start_sec = cfr_wall_seconds();
    if (provider->kind == 1 && q.threads > 1)
    {
        provider->use_runtime_cache = 0;
    }
    if (!cfr_search_decide(ctx,
                           provider,
                           &st,
                           q.player_seat,
                           q.iters,
                           q.time_ms,
                           q.depth,
                           q.threads,
                           q.search_pick_mode,
                           q.offtree_mode,
                           frozen,
                           local_frozen_n,
                           decision))
    {
        provider->use_runtime_cache = restore_runtime_cache;
        fprintf(stderr, "Search solve failed (player may not be next to act after replaying current-round history)\n");
        return 0;
    }
    provider->use_runtime_cache = restore_runtime_cache;

    if (solve_ms != NULL)
    {
        *solve_ms = (cfr_wall_seconds() - solve_start_sec) * 1000.0;
    }
    if (frozen_count != NULL)
    {
        *frozen_count = local_frozen_n;
    }
    return 1;
}

static int cfr_search_load_policy_provider(const CFRSearchOptions *opt,
                                           CFRBlueprint *bp,
                                           CFRRuntimeBlueprint *runtime_bp,
                                           CFRPolicyProvider *provider,
                                           uint32_t abstraction_hash32,
                                           const char *label)
{
    if (opt == NULL || provider == NULL)
    {
        return 0;
    }

    if (opt->blueprint_path != NULL)
    {
        if (!cfr_blueprint_load(bp, opt->blueprint_path))
        {
            fprintf(stderr, "Failed to load blueprint: %s\n", opt->blueprint_path);
            return 0;
        }
        cfr_policy_provider_init_blueprint(provider, bp);
    }
    else if (opt->runtime_blueprint_path != NULL)
    {
        cfr_runtime_blueprint_close(runtime_bp);
        if (!cfr_runtime_blueprint_open(runtime_bp,
                                        opt->runtime_blueprint_path,
                                        opt->cache_bytes,
                                        opt->runtime_prefetch_mode))
        {
            fprintf(stderr, "Failed to load runtime blueprint: %s\n", opt->runtime_blueprint_path);
            return 0;
        }
        cfr_policy_provider_init_runtime(provider, runtime_bp);
    }
    else
    {
        fprintf(stderr, "--blueprint or --runtime-blueprint is required\n");
        return 0;
    }

    if (!cfr_check_policy_provider_abstraction_compat(provider,
                                                      abstraction_hash32,
                                                      opt->ignore_abstraction_compat,
                                                      label))
    {
        return 0;
    }
    return 1;
}

static void cfr_print_search_decision_result(const CFRSearchOptions *opt,
                                             const CFRSearchDecision *decision,
                                             double solve_ms,
                                             int frozen_n,
                                             const char *result_prefix)
{
    int i;
    const char *prefix;

    if (opt == NULL || decision == NULL)
    {
        return;
    }
    prefix = (result_prefix != NULL) ? result_prefix : "Search result";

    printf("%s: iters=%llu depth=%d threads=%d solve_ms=%.2f pick=%s offtree=%s frozen_actions=%d belief_updates=%d chosen=%s",
           prefix,
           (unsigned long long)decision->iterations_done,
           opt->depth,
           opt->threads,
           solve_ms,
           (opt->search_pick_mode == CFR_SEARCH_PICK_ARGMAX) ? "argmax" : "sample-final",
           (opt->offtree_mode == CFR_OFFTREE_MODE_TRANSLATE) ? "translate" : "inject",
           frozen_n,
           decision->belief_updates,
           cfr_action_name(decision->legal_actions[decision->chosen_index], opt->to_call));
    if (cfr_is_raise_action_code(decision->legal_actions[decision->chosen_index]))
    {
        printf(" to=%d", decision->legal_targets[decision->chosen_index]);
    }
    printf("\n");

    for (i = 0; i < decision->legal_count; ++i)
    {
        if (cfr_is_raise_action_code(decision->legal_actions[i]))
        {
            printf("  %-12s to=%d final=%.6f avg=%.6f\n",
                   cfr_action_name(decision->legal_actions[i], opt->to_call),
                   decision->legal_targets[i],
                   decision->final_policy[i],
                   decision->avg_policy[i]);
        }
        else
        {
            printf("  %-12s final=%.6f avg=%.6f\n",
                   cfr_action_name(decision->legal_actions[i], opt->to_call),
                   decision->final_policy[i],
                   decision->avg_policy[i]);
        }
    }
}

static int cfr_search_server_split_args(char *line, char **argv, int max_argv)
{
    int argc;
    char *p;

    if (line == NULL || argv == NULL || max_argv <= 0)
    {
        return -1;
    }

    argc = 0;
    p = line;
    while (*p != '\0')
    {
        while (*p != '\0' && isspace((unsigned char)*p))
        {
            ++p;
        }
        if (*p == '\0')
        {
            break;
        }

        if (argc >= max_argv)
        {
            return -1;
        }

        if (*p == '"')
        {
            ++p;
            argv[argc++] = p;
            while (*p != '\0' && *p != '"')
            {
                ++p;
            }
            if (*p == '\0')
            {
                return -1;
            }
            *p = '\0';
            ++p;
        }
        else
        {
            argv[argc++] = p;
            while (*p != '\0' && !isspace((unsigned char)*p))
            {
                ++p;
            }
            if (*p != '\0')
            {
                *p = '\0';
                ++p;
            }
        }
    }
    return argc;
}

static void cfr_search_server_print_defaults(const CFRSearchOptions *opt)
{
    if (opt == NULL)
    {
        return;
    }
    printf("defaults: player=%d dealer=%d active=%d pot=%d to_call=%d stack=%d street=%d raises=%d iters=%llu time_ms=%llu depth=%d threads=%d pick=%s offtree=%s cache_bytes=%llu prefetch=%s seed=%llu\n",
           opt->player_seat,
           opt->dealer_seat,
           opt->active_players,
           opt->pot,
           opt->to_call,
           opt->stack,
           opt->street,
           opt->raises_this_street,
           (unsigned long long)opt->iters,
           (unsigned long long)opt->time_ms,
           opt->depth,
           opt->threads,
           (opt->search_pick_mode == CFR_SEARCH_PICK_ARGMAX) ? "argmax" : "sample-final",
           (opt->offtree_mode == CFR_OFFTREE_MODE_TRANSLATE) ? "translate" : "inject",
           (unsigned long long)opt->cache_bytes,
           (opt->runtime_prefetch_mode == CFR_RUNTIME_PREFETCH_NONE) ? "none" :
           ((opt->runtime_prefetch_mode == CFR_RUNTIME_PREFETCH_PREFLOP) ? "preflop" : "auto"),
           (unsigned long long)opt->seed);
}

static int cfr_cmd_search(int argc, char **argv)
{
    CFRSearchOptions opt;
    static CFRBlueprint bp;
    static CFRRuntimeBlueprint runtime_bp;
    CFRPolicyProvider provider;
    static CFRSearchContext ctx;
    CFRSearchDecision decision;
    uint32_t abstraction_hash32;
    double solve_ms;
    int frozen_n;
    int parse_rc;

    cfr_search_options_set_defaults(&opt);
    parse_rc = cfr_search_options_parse(&opt, argc, argv, 1, 1, "search");
    if (parse_rc == 2)
    {
        cfr_print_usage("main.exe");
        return 0;
    }
    if (parse_rc == 0)
    {
        return 1;
    }

    if (opt.blueprint_path == NULL && opt.runtime_blueprint_path == NULL)
    {
        fprintf(stderr, "--blueprint or --runtime-blueprint is required\n");
        return 1;
    }
    if (opt.hole_text[0] == '\0')
    {
        fprintf(stderr, "--hole is required\n");
        return 1;
    }
    cfr_search_options_normalize(&opt);

    if (!cfr_load_active_abstraction(opt.abstraction_path, &abstraction_hash32))
    {
        if (opt.abstraction_path != NULL)
        {
            fprintf(stderr, "Failed to load abstraction: %s\n", opt.abstraction_path);
        }
        else
        {
            fprintf(stderr, "Failed to initialize default abstraction\n");
        }
        return 1;
    }
    if (!cfr_search_load_policy_provider(&opt, &bp, &runtime_bp, &provider, abstraction_hash32, "search"))
    {
        return 1;
    }

    if (!cfr_hand_index_init())
    {
        fprintf(stderr, "Failed to initialize hand indexer\n");
        return 1;
    }

    cfr_search_context_init(&ctx, opt.seed);
    if (!cfr_search_execute_with_policy_provider(&opt, &provider, &ctx, &decision, &solve_ms, &frozen_n))
    {
        return 1;
    }
    cfr_print_search_decision_result(&opt, &decision, solve_ms, frozen_n, "Search result");

    return 0;
}

static int cfr_cmd_search_server(int argc, char **argv)
{
    CFRSearchOptions defaults;
    static CFRBlueprint bp;
    static CFRRuntimeBlueprint runtime_bp;
    CFRPolicyProvider provider;
    static CFRSearchContext ctx;
    uint32_t abstraction_hash32;
    int parse_rc;
    char line[1024];
    int query_id;

    cfr_search_options_set_defaults(&defaults);
    parse_rc = cfr_search_options_parse(&defaults, argc, argv, 1, 1, "search-server");
    if (parse_rc == 2)
    {
        cfr_print_usage("main.exe");
        return 0;
    }
    if (parse_rc == 0)
    {
        return 1;
    }

    if (defaults.blueprint_path == NULL && defaults.runtime_blueprint_path == NULL)
    {
        fprintf(stderr, "--blueprint or --runtime-blueprint is required\n");
        return 1;
    }
    cfr_search_options_normalize(&defaults);

    if (!cfr_load_active_abstraction(defaults.abstraction_path, &abstraction_hash32))
    {
        if (defaults.abstraction_path != NULL)
        {
            fprintf(stderr, "Failed to load abstraction: %s\n", defaults.abstraction_path);
        }
        else
        {
            fprintf(stderr, "Failed to initialize default abstraction\n");
        }
        return 1;
    }
    if (!cfr_search_load_policy_provider(&defaults,
                                         &bp,
                                         &runtime_bp,
                                         &provider,
                                         abstraction_hash32,
                                         "search-server"))
    {
        return 1;
    }
    if (!cfr_hand_index_init())
    {
        fprintf(stderr, "Failed to initialize hand indexer\n");
        return 1;
    }

    cfr_search_context_init(&ctx, defaults.seed);
    printf("search-server ready: %s=%s nodes=%u\n",
           (provider.kind == 1) ? "runtime_blueprint" : "blueprint",
           (provider.kind == 1) ? defaults.runtime_blueprint_path : defaults.blueprint_path,
           (unsigned int)cfr_policy_provider_node_count(&provider));
    cfr_search_server_print_defaults(&defaults);
    printf("line protocol:\n");
    printf("  query line: --hole AsKd [other search options overrides]\n");
    printf("  commands: defaults | set-default <opts> | stats | help | quit\n");
    fflush(stdout);

    query_id = 0;
    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        char *p;
        size_t len;
        char *line_argv[128];
        int line_argc;
        CFRSearchOptions run_opt;
        CFRSearchDecision decision;
        double solve_ms;
        int frozen_n;

        len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        {
            line[--len] = '\0';
        }

        p = line;
        while (*p != '\0' && isspace((unsigned char)*p))
        {
            ++p;
        }
        if (*p == '\0')
        {
            continue;
        }
        if (_stricmp(p, "quit") == 0 || _stricmp(p, "exit") == 0)
        {
            printf("bye\n");
            fflush(stdout);
            return 0;
        }
        if (_stricmp(p, "stats") == 0)
        {
            if (provider.kind == 1)
            {
                printf("runtime-stats: cache_hits=%llu cache_misses=%llu decode_loads=%llu cache_resident_bytes=%llu prefetch_loads=%llu prefetch_bytes=%llu\n",
                       (unsigned long long)runtime_bp.cache_hits,
                       (unsigned long long)runtime_bp.cache_misses,
                       (unsigned long long)runtime_bp.decode_loads,
                       (unsigned long long)cfr_runtime_blueprint_cache_resident_bytes(&runtime_bp),
                       (unsigned long long)runtime_bp.prefetch_loads,
                       (unsigned long long)runtime_bp.prefetch_bytes);
            }
            else
            {
                printf("runtime-stats: provider=in-memory-blueprint\n");
            }
            fflush(stdout);
            continue;
        }
        if (_stricmp(p, "help") == 0)
        {
            printf("line protocol commands:\n");
            printf("  defaults\n");
            printf("  set-default --iters 20000 --time-ms 1500 --threads 8 --depth 3 --offtree-mode inject\n");
            printf("  stats\n");
            printf("  --hole AsKd --board AhKs2d --history c,r2,/,c --pot 8 --to-call 2 --time-ms 2000\n");
            printf("  quit\n");
            fflush(stdout);
            continue;
        }
        if (_stricmp(p, "defaults") == 0)
        {
            cfr_search_server_print_defaults(&defaults);
            fflush(stdout);
            continue;
        }

        line_argc = cfr_search_server_split_args(p, line_argv, (int)(sizeof(line_argv) / sizeof(line_argv[0])));
        if (line_argc <= 0)
        {
            fprintf(stderr, "ERR parse line\n");
            fflush(stderr);
            continue;
        }
        if (_stricmp(line_argv[0], "set-default") == 0)
        {
            parse_rc = cfr_search_options_parse(&defaults,
                                                line_argc - 1,
                                                line_argv + 1,
                                                0,
                                                0,
                                                "search-server set-default");
            if (parse_rc == 0)
            {
                fprintf(stderr, "ERR set-default failed\n");
                fflush(stderr);
                continue;
            }
            cfr_search_options_normalize(&defaults);
            cfr_search_server_print_defaults(&defaults);
            fflush(stdout);
            continue;
        }

        run_opt = defaults;
        if (_stricmp(line_argv[0], "search") == 0)
        {
            line_argc -= 1;
            if (line_argc > 0)
            {
                memmove(line_argv, line_argv + 1, (size_t)line_argc * sizeof(line_argv[0]));
            }
        }
        parse_rc = cfr_search_options_parse(&run_opt,
                                            line_argc,
                                            line_argv,
                                            0,
                                            0,
                                            "search-server query");
        if (parse_rc == 0)
        {
            fprintf(stderr, "ERR query parse failed\n");
            fflush(stderr);
            continue;
        }
        if (run_opt.hole_text[0] == '\0')
        {
            fprintf(stderr, "ERR --hole is required\n");
            fflush(stderr);
            continue;
        }

        cfr_search_options_normalize(&run_opt);
        if (!cfr_search_execute_with_policy_provider(&run_opt, &provider, &ctx, &decision, &solve_ms, &frozen_n))
        {
            fprintf(stderr, "ERR search solve failed\n");
            fflush(stderr);
            continue;
        }

        query_id++;
        printf("OK q=%d iters=%llu solve_ms=%.2f chosen=%s",
               query_id,
               (unsigned long long)decision.iterations_done,
               solve_ms,
               cfr_action_name(decision.legal_actions[decision.chosen_index], run_opt.to_call));
        if (cfr_is_raise_action_code(decision.legal_actions[decision.chosen_index]))
        {
            printf(" to=%d", decision.legal_targets[decision.chosen_index]);
        }
        printf("\n");
        cfr_print_search_decision_result(&run_opt, &decision, solve_ms, frozen_n, "detail");
        fflush(stdout);
    }

    return 0;
}

static int cfr_policy_pick_action(const CFRBlueprint *bp, const CFRHandState *st, int player, uint64_t *rng, int *out_action, int *out_target)
{
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int legal_count;
    CFRInfoKeyFields kf;
    uint64_t key;
    CFRNode *node;
    float strat[CFR_MAX_ACTIONS];

    if (bp == NULL || st == NULL || rng == NULL || out_action == NULL || out_target == NULL)
    {
        return 0;
    }

    legal_count = cfr_get_legal_actions(st, player, legal_actions, legal_targets);
    if (legal_count <= 0)
    {
        return 0;
    }
    if (!cfr_extract_infoset_fields(st, player, &kf))
    {
        return 0;
    }
    key = cfr_make_infoset_key(&kf);
    node = cfr_blueprint_get_node((CFRBlueprint *)bp, key, 0, legal_count);
    if (node == NULL)
    {
        int i;
        float p = 1.0f / (float)legal_count;
        for (i = 0; i < legal_count; ++i)
        {
            strat[i] = p;
        }
    }
    else
    {
        cfr_compute_average_strategy_n(node, legal_count, strat);
    }

    {
        int idx;
        idx = cfr_sample_action_index(strat, legal_count, rng);
        *out_action = legal_actions[idx];
        *out_target = legal_targets[idx];
    }
    return 1;
}

static int cfr_match_estimate_observed_raise_target(const CFRHandState *st,
                                                    int player,
                                                    int bucket,
                                                    int mapped_target,
                                                    int *out_target)
{
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int legal_count;
    int min_raise;
    int max_raise;
    int saw_raise;
    int i;
    int est;
    static const int bucket_amount[9] = {0, 2, 4, 8, 16, 32, 64, 128, 256};

    if (st == NULL || out_target == NULL)
    {
        return 0;
    }

    legal_count = cfr_get_legal_actions(st, player, legal_actions, legal_targets);
    if (legal_count <= 0)
    {
        return 0;
    }

    min_raise = 0;
    max_raise = 0;
    saw_raise = 0;
    for (i = 0; i < legal_count; ++i)
    {
        if (!cfr_is_raise_action_code(legal_actions[i]))
        {
            continue;
        }
        if (!saw_raise)
        {
            min_raise = legal_targets[i];
            max_raise = legal_targets[i];
            saw_raise = 1;
        }
        else
        {
            if (legal_targets[i] < min_raise)
            {
                min_raise = legal_targets[i];
            }
            if (legal_targets[i] > max_raise)
            {
                max_raise = legal_targets[i];
            }
        }
    }
    if (!saw_raise)
    {
        return 0;
    }

    if (bucket < 0)
    {
        *out_target = mapped_target;
        return 1;
    }
    if (bucket <= 8)
    {
        int base;
        base = cfr_max_int(st->to_call, st->committed_street[player]);
        est = base + bucket_amount[bucket];
    }
    else
    {
        est = min_raise + (int)(((int64_t)(max_raise - min_raise) * (int64_t)bucket + 127LL) / 255LL);
    }

    if (est < min_raise)
    {
        est = min_raise;
    }
    if (est > max_raise)
    {
        est = max_raise;
    }
    *out_target = est;
    return 1;
}

static int cfr_match_nearest_raise_target_diff(const CFRHandState *st,
                                               int player,
                                               int target,
                                               int *out_diff)
{
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int legal_count;
    int i;
    int saw_raise;
    int best_diff;

    if (st == NULL || out_diff == NULL)
    {
        return 0;
    }

    legal_count = cfr_get_legal_actions(st, player, legal_actions, legal_targets);
    if (legal_count <= 0)
    {
        return 0;
    }

    saw_raise = 0;
    best_diff = 0;
    for (i = 0; i < legal_count; ++i)
    {
        int d;
        if (!cfr_is_raise_action_code(legal_actions[i]))
        {
            continue;
        }
        d = legal_targets[i] - target;
        if (d < 0)
        {
            d = -d;
        }
        if (!saw_raise || d < best_diff)
        {
            best_diff = d;
            saw_raise = 1;
        }
    }
    if (!saw_raise)
    {
        return 0;
    }

    *out_diff = best_diff;
    return 1;
}

static int cfr_match_preflop_last_opponent_raise_diff(const CFRHandState *state,
                                                      int acting_player,
                                                      int offtree_mode,
                                                      uint64_t *rng,
                                                      int *out_last_diff)
{
    CFRHandState root;
    CFRHandState sim;
    int prefix_n;
    int seg_begin;
    int seg_end;
    int pot_round_start;
    int i;
    int found;
    int last_diff;

    if (state == NULL || rng == NULL || out_last_diff == NULL || state->street > 0)
    {
        return 0;
    }

    root = *state;
    cfr_history_segment_bounds_for_street(state->history,
                                          state->history_len,
                                          state->street,
                                          &prefix_n,
                                          &seg_begin,
                                          &seg_end);

    pot_round_start = state->pot;
    for (i = 0; i < CFR_MAX_PLAYERS; ++i)
    {
        pot_round_start -= state->committed_street[i];
    }
    if (pot_round_start < 0)
    {
        pot_round_start = 0;
    }

    cfr_prepare_round_root_state(&root,
                                 state->street,
                                 pot_round_start,
                                 state->history,
                                 prefix_n);

    sim = root;
    found = 0;
    last_diff = 0;
    for (i = seg_begin; i < seg_end; ++i)
    {
        unsigned char code;
        int p;
        int req_action;
        int req_target;
        int bucket;

        code = state->history[i];
        if (cfr_is_round_separator_code(code))
        {
            continue;
        }
        if (code == 250u)
        {
            if (i + 1 < seg_end)
            {
                i++;
            }
            continue;
        }
        if (code != (unsigned char)CFR_ACT_FOLD &&
            code != (unsigned char)CFR_ACT_CALL_CHECK &&
            code != (unsigned char)CFR_ACT_RAISE_TO &&
            code != (unsigned char)CFR_ACT_ALL_IN)
        {
            continue;
        }

        cfr_auto_advance_rounds_if_needed(&sim);
        if (sim.is_terminal || sim.street != root.street)
        {
            break;
        }
        p = sim.current_player;
        if (p < 0 || p >= CFR_MAX_PLAYERS)
        {
            return 0;
        }

        req_action = (int)code;
        req_target = sim.to_call;
        bucket = -1;
        if (req_action == CFR_ACT_RAISE_TO)
        {
            if (i + 2 < seg_end && state->history[i + 1] == 250u)
            {
                bucket = (int)state->history[i + 2];
                i += 2;
            }
            if (!cfr_select_observed_raise_target(&sim, p, bucket, &req_action, &req_target))
            {
                return 0;
            }
        }
        else if (req_action == CFR_ACT_ALL_IN)
        {
            req_target = sim.committed_street[p] + sim.stack[p];
        }
        else if (req_action == CFR_ACT_FOLD)
        {
            req_target = 0;
        }

        if (p != acting_player && cfr_is_raise_action_code(req_action))
        {
            int observed_target;
            int diff;
            observed_target = req_target;
            if (!cfr_match_estimate_observed_raise_target(&sim,
                                                          p,
                                                          bucket,
                                                          req_target,
                                                          &observed_target))
            {
                observed_target = req_target;
            }
            if (cfr_match_nearest_raise_target_diff(&sim, p, observed_target, &diff))
            {
                last_diff = diff;
                found = 1;
            }
        }

        if (!cfr_search_apply_observed_action(&sim,
                                              p,
                                              req_action,
                                              req_target,
                                              offtree_mode,
                                              rng,
                                              NULL))
        {
            return 0;
        }
    }

    if (!found)
    {
        return 0;
    }
    *out_last_diff = last_diff;
    return 1;
}

static int cfr_match_should_use_search_for_state(const CFRMatchOptions *opt,
                                                 const CFRHandState *st,
                                                 int acting_player,
                                                 uint64_t map_seed)
{
    int active_players;

    if (opt == NULL || st == NULL || acting_player < 0 || acting_player >= CFR_MAX_PLAYERS)
    {
        return 0;
    }
    if (opt->use_search)
    {
        return 1;
    }
    if (opt->runtime_profile != CFR_RUNTIME_PROFILE_PLURIBUS)
    {
        return 0;
    }

    active_players = cfr_count_in_hand(st);
    if (st->street <= 0)
    {
        int last_diff;
        uint64_t map_rng;
        if (active_players > 4)
        {
            return 0;
        }
        map_rng = map_seed ^ cfr_history_key_from_state(st) ^ (uint64_t)(acting_player + 1);
        if (!cfr_match_preflop_last_opponent_raise_diff(st,
                                                        acting_player,
                                                        opt->offtree_mode,
                                                        &map_rng,
                                                        &last_diff))
        {
            return 0;
        }
        return last_diff > 100;
    }

    /* Postflop: nested solve by default in runtime profile mode. */
    return 1;
}

static int cfr_cmd_match(int argc, char **argv)
{
    CFRMatchOptions opt;
    static CFRBlueprint bp_a;
    static CFRBlueprint bp_b;
    static CFRSearchContext search_ctx_a[CFR_MAX_PLAYERS];
    static CFRSearchContext search_ctx_b[CFR_MAX_PLAYERS];
    uint32_t abstraction_hash32;
    uint64_t rng;
    uint64_t h;
    double total_a;
    double total_b;
    int hands_done;
    int i;

    memset(&opt, 0, sizeof(opt));
    opt.a_path = NULL;
    opt.b_path = NULL;
    opt.abstraction_path = NULL;
    opt.ignore_abstraction_compat = 0;
    opt.hands = 1000ULL;
    opt.use_search = 0;
    opt.search_iters = 128ULL;
    opt.search_time_ms = 0ULL;
    opt.search_depth = 3;
    opt.search_threads = 1;
    opt.search_pick_mode = CFR_SEARCH_PICK_SAMPLE_FINAL;
    opt.offtree_mode = CFR_OFFTREE_MODE_INJECT;
    opt.runtime_profile = CFR_RUNTIME_PROFILE_NONE;
    opt.seed = 20260227ULL;

    for (i = 0; i < argc; ++i)
    {
        if (strcmp(argv[i], "--a") == 0 && i + 1 < argc)
        {
            opt.a_path = argv[++i];
        }
        else if (strcmp(argv[i], "--b") == 0 && i + 1 < argc)
        {
            opt.b_path = argv[++i];
        }
        else if (strcmp(argv[i], "--abstraction") == 0 && i + 1 < argc)
        {
            opt.abstraction_path = argv[++i];
        }
        else if (strcmp(argv[i], "--ignore-abstraction-compat") == 0)
        {
            opt.ignore_abstraction_compat = 1;
        }
        else if (strcmp(argv[i], "--hands") == 0 && i + 1 < argc)
        {
            opt.hands = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--use-search") == 0)
        {
            opt.use_search = 1;
        }
        else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc)
        {
            const char *v;
            v = argv[++i];
            if (_stricmp(v, "blueprint") == 0)
            {
                opt.use_search = 0;
            }
            else if (_stricmp(v, "search") == 0)
            {
                opt.use_search = 1;
            }
            else
            {
                fprintf(stderr, "Invalid --mode value: %s (expected blueprint|search)\n", v);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--search-iters") == 0 && i + 1 < argc)
        {
            opt.search_iters = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--search-time-ms") == 0 && i + 1 < argc)
        {
            opt.search_time_ms = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--search-depth") == 0 && i + 1 < argc)
        {
            opt.search_depth = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--search-threads") == 0 && i + 1 < argc)
        {
            opt.search_threads = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--search-pick") == 0 && i + 1 < argc)
        {
            const char *v;
            v = argv[++i];
            if (_stricmp(v, "sample-final") == 0)
            {
                opt.search_pick_mode = CFR_SEARCH_PICK_SAMPLE_FINAL;
            }
            else if (_stricmp(v, "argmax") == 0)
            {
                opt.search_pick_mode = CFR_SEARCH_PICK_ARGMAX;
            }
            else
            {
                fprintf(stderr, "Invalid --search-pick value: %s (expected sample-final|argmax)\n", v);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--offtree-mode") == 0 && i + 1 < argc)
        {
            const char *v;
            v = argv[++i];
            if (_stricmp(v, "inject") == 0)
            {
                opt.offtree_mode = CFR_OFFTREE_MODE_INJECT;
            }
            else if (_stricmp(v, "translate") == 0)
            {
                opt.offtree_mode = CFR_OFFTREE_MODE_TRANSLATE;
            }
            else
            {
                fprintf(stderr, "Invalid --offtree-mode value: %s (expected inject|translate)\n", v);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--runtime-profile") == 0 && i + 1 < argc)
        {
            const char *v;
            v = argv[++i];
            if (_stricmp(v, "none") == 0)
            {
                opt.runtime_profile = CFR_RUNTIME_PROFILE_NONE;
            }
            else if (_stricmp(v, "pluribus") == 0)
            {
                opt.runtime_profile = CFR_RUNTIME_PROFILE_PLURIBUS;
            }
            else
            {
                fprintf(stderr, "Invalid --runtime-profile value: %s (expected none|pluribus)\n", v);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            opt.seed = cfr_parse_u64(argv[++i]);
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            cfr_print_usage("main.exe");
            return 0;
        }
        else
        {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return 1;
        }
    }

    if (opt.a_path == NULL || opt.b_path == NULL)
    {
        fprintf(stderr, "--a and --b are required\n");
        return 1;
    }
    if (opt.hands == 0ULL)
    {
        opt.hands = 1ULL;
    }
    if (opt.search_iters == 0ULL)
    {
        opt.search_iters = 1ULL;
    }
    if (opt.search_depth < 1)
    {
        opt.search_depth = 1;
    }
    if (opt.search_threads < 1)
    {
        opt.search_threads = 1;
    }

    if (!cfr_hand_index_init())
    {
        fprintf(stderr, "Failed to initialize hand indexer\n");
        return 1;
    }
    if (!cfr_load_active_abstraction(opt.abstraction_path, &abstraction_hash32))
    {
        fprintf(stderr, "Failed to load abstraction config\n");
        return 1;
    }
    if (!cfr_blueprint_load(&bp_a, opt.a_path))
    {
        fprintf(stderr, "Failed to load blueprint A: %s\n", opt.a_path);
        return 1;
    }
    if (!cfr_blueprint_load(&bp_b, opt.b_path))
    {
        fprintf(stderr, "Failed to load blueprint B: %s\n", opt.b_path);
        return 1;
    }
    if (!cfr_check_blueprint_abstraction_compat(&bp_a, abstraction_hash32, opt.ignore_abstraction_compat, "match(A)"))
    {
        return 1;
    }
    if (!cfr_check_blueprint_abstraction_compat(&bp_b, abstraction_hash32, opt.ignore_abstraction_compat, "match(B)"))
    {
        return 1;
    }

    rng = opt.seed;
    total_a = 0.0;
    total_b = 0.0;
    hands_done = 0;

    if (opt.use_search || opt.runtime_profile == CFR_RUNTIME_PROFILE_PLURIBUS)
    {
        for (i = 0; i < CFR_MAX_PLAYERS; ++i)
        {
            cfr_search_context_init(&search_ctx_a[i], opt.seed ^ (0xA5A5A5A500000000ULL + (uint64_t)i));
            cfr_search_context_init(&search_ctx_b[i], opt.seed ^ (0x5A5A5A5A00000000ULL + (uint64_t)i));
        }
    }

    for (h = 0; h < opt.hands; ++h)
    {
        CFRHandState st;
        int dealer;
        int seat_owner[CFR_MAX_PLAYERS];
        int guard;

        dealer = (int)(h % (uint64_t)CFR_MAX_PLAYERS);
        cfr_init_hand(&st, dealer, &rng);
        for (i = 0; i < CFR_MAX_PLAYERS; ++i)
        {
            seat_owner[i] = (((int)h + i) & 1);
        }

        guard = 512;
        while (!st.is_terminal && guard-- > 0)
        {
            int p;
            int action;
            int target;
            const CFRBlueprint *policy_bp;
            CFRPolicyProvider policy_provider;
            int picked;

            cfr_auto_advance_rounds_if_needed(&st);
            if (st.is_terminal)
            {
                break;
            }
            p = st.current_player;
            if (p < 0 || p >= CFR_MAX_PLAYERS || !st.in_hand[p] || st.stack[p] <= 0)
            {
                break;
            }
            action = CFR_ACT_CALL_CHECK;
            target = st.to_call;

            policy_bp = (seat_owner[p] == 0) ? &bp_a : &bp_b;
            cfr_policy_provider_init_blueprint(&policy_provider, policy_bp);
            picked = 0;
            if (cfr_match_should_use_search_for_state(&opt, &st, p, rng))
            {
                CFRHandState root_state;
                CFRSearchFrozenAction frozen[CFR_MAX_HISTORY];
                CFRSearchDecision decision;
                int frozen_n;
                CFRSearchContext *ctx_ptr;
                uint64_t map_rng;

                map_rng = rng ^ ((uint64_t)h * 0x9E3779B97F4A7C15ULL) ^ (uint64_t)(p + 1);
                frozen_n = 0;
                if (cfr_extract_round_root_with_frozen_from_state(&st,
                                                                  &root_state,
                                                                  frozen,
                                                                  &frozen_n,
                                                                  CFR_MAX_HISTORY,
                                                                  opt.offtree_mode,
                                                                  &map_rng))
                {
                    ctx_ptr = (seat_owner[p] == 0) ? &search_ctx_a[p] : &search_ctx_b[p];
                    if (cfr_search_decide(ctx_ptr,
                                          &policy_provider,
                                          &root_state,
                                          p,
                                          opt.search_iters,
                                          opt.search_time_ms,
                                          opt.search_depth,
                                          opt.search_threads,
                                          opt.search_pick_mode,
                                          opt.offtree_mode,
                                          frozen,
                                          frozen_n,
                                          &decision))
                    {
                        action = decision.legal_actions[decision.chosen_index];
                        target = decision.legal_targets[decision.chosen_index];
                        picked = 1;
                    }
                }
                rng = map_rng;
            }

            if (!picked && !cfr_policy_pick_action(policy_bp, &st, p, &rng, &action, &target))
            {
                int legal_actions[CFR_MAX_ACTIONS];
                int legal_targets[CFR_MAX_ACTIONS];
                int n;
                n = cfr_get_legal_actions(&st, p, legal_actions, legal_targets);
                if (n <= 0)
                {
                    break;
                }
                action = legal_actions[0];
                target = legal_targets[0];
            }

            cfr_apply_action(&st, p, action, target);
        }

        cfr_resolve_terminal(&st);
        for (i = 0; i < CFR_MAX_PLAYERS; ++i)
        {
            double profit;
            profit = (double)(st.stack[i] - CFR_START_STACK);
            if (seat_owner[i] == 0)
            {
                total_a += profit;
            }
            else
            {
                total_b += profit;
            }
        }
        hands_done++;
    }

    printf("Match results: hands=%d seed=%llu mode=%s\n",
           hands_done,
           (unsigned long long)opt.seed,
           opt.use_search ? "search" : ((opt.runtime_profile == CFR_RUNTIME_PROFILE_PLURIBUS) ? "runtime-pluribus" : "blueprint"));
    if (opt.use_search || opt.runtime_profile == CFR_RUNTIME_PROFILE_PLURIBUS)
    {
        printf("  search: iters=%llu time_ms=%llu depth=%d threads=%d pick=%s offtree=%s runtime_profile=%s\n",
               (unsigned long long)opt.search_iters,
               (unsigned long long)opt.search_time_ms,
               opt.search_depth,
               opt.search_threads,
               (opt.search_pick_mode == CFR_SEARCH_PICK_ARGMAX) ? "argmax" : "sample-final",
               (opt.offtree_mode == CFR_OFFTREE_MODE_TRANSLATE) ? "translate" : "inject",
               (opt.runtime_profile == CFR_RUNTIME_PROFILE_PLURIBUS) ? "pluribus" : "none");
    }
    printf("  A ev/chips_per_hand=%.6f  ev_bb/100=%.6f\n",
           total_a / (double)hands_done,
           (total_a * 100.0) / ((double)hands_done * (double)CFR_BIG_BLIND));
    printf("  B ev/chips_per_hand=%.6f  ev_bb/100=%.6f\n",
           total_b / (double)hands_done,
           (total_b * 100.0) / ((double)hands_done * (double)CFR_BIG_BLIND));
    return 0;
}

static int cfr_cmd_query(int argc, char **argv)
{
    CFRQueryOptions q;
    static CFRBlueprint bp;
    CFRInfoKeyFields kf;
    uint64_t key;
    CFRNode *node;
    int hole_cards[2];
    int board_cards[5];
    int hole_n;
    int board_n;
    unsigned char hist_codes[CFR_MAX_HISTORY];
    int hist_n;
    CFRHandState pseudo;
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int legal_count;
    float strat[CFR_MAX_ACTIONS];
    int i;

    memset(&q, 0, sizeof(q));
    q.blueprint_path = NULL;
    q.hole_text[0] = '\0';
    q.board_text[0] = '\0';
    q.history_text[0] = '\0';
    q.player_seat = 0;
    q.dealer_seat = 5;
    q.active_players = 6;
    q.pot = 3;
    q.to_call = 0;
    q.stack = CFR_START_STACK;
    q.street = -1;
    q.raises_this_street = 0;
    q.abstraction_path = NULL;
    q.ignore_abstraction_compat = 0;

    for (i = 0; i < argc; ++i)
    {
        if (strcmp(argv[i], "--blueprint") == 0 && i + 1 < argc)
        {
            q.blueprint_path = argv[++i];
        }
        else if (strcmp(argv[i], "--abstraction") == 0 && i + 1 < argc)
        {
            q.abstraction_path = argv[++i];
        }
        else if (strcmp(argv[i], "--hole") == 0 && i + 1 < argc)
        {
            strncpy(q.hole_text, argv[++i], sizeof(q.hole_text) - 1);
            q.hole_text[sizeof(q.hole_text) - 1] = '\0';
        }
        else if (strcmp(argv[i], "--board") == 0 && i + 1 < argc)
        {
            strncpy(q.board_text, argv[++i], sizeof(q.board_text) - 1);
            q.board_text[sizeof(q.board_text) - 1] = '\0';
        }
        else if (strcmp(argv[i], "--history") == 0 && i + 1 < argc)
        {
            strncpy(q.history_text, argv[++i], sizeof(q.history_text) - 1);
            q.history_text[sizeof(q.history_text) - 1] = '\0';
        }
        else if (strcmp(argv[i], "--street") == 0 && i + 1 < argc)
        {
            q.street = cfr_parse_street_text(argv[++i]);
        }
        else if (strcmp(argv[i], "--player-seat") == 0 && i + 1 < argc)
        {
            q.player_seat = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--dealer-seat") == 0 && i + 1 < argc)
        {
            q.dealer_seat = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--active") == 0 && i + 1 < argc)
        {
            q.active_players = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--pot") == 0 && i + 1 < argc)
        {
            q.pot = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--to-call") == 0 && i + 1 < argc)
        {
            q.to_call = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--stack") == 0 && i + 1 < argc)
        {
            q.stack = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--raises") == 0 && i + 1 < argc)
        {
            q.raises_this_street = cfr_parse_i32(argv[++i]);
        }
        else if (strcmp(argv[i], "--ignore-abstraction-compat") == 0)
        {
            q.ignore_abstraction_compat = 1;
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            cfr_print_usage("main.exe");
            return 0;
        }
        else
        {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return 1;
        }
    }

    if (q.blueprint_path == NULL)
    {
        fprintf(stderr, "--blueprint is required\n");
        return 1;
    }

    if (q.hole_text[0] == '\0')
    {
        fprintf(stderr, "--hole is required\n");
        return 1;
    }

    if (q.player_seat < 0 || q.player_seat >= CFR_MAX_PLAYERS || q.dealer_seat < 0 || q.dealer_seat >= CFR_MAX_PLAYERS)
    {
        fprintf(stderr, "Seats must be in range 0..5\n");
        return 1;
    }

    if (q.active_players < 2)
    {
        q.active_players = 2;
    }
    if (q.active_players > CFR_MAX_PLAYERS)
    {
        q.active_players = CFR_MAX_PLAYERS;
    }

    hole_n = cfr_parse_cards(q.hole_text, hole_cards, 2);
    if (hole_n != 2)
    {
        fprintf(stderr, "Invalid --hole cards. Example: AsKd\n");
        return 1;
    }

    board_n = cfr_parse_cards(q.board_text, board_cards, 5);
    if (board_n < 0 || board_n > 5)
    {
        fprintf(stderr, "Invalid --board cards\n");
        return 1;
    }

    hist_n = cfr_parse_history_text(q.history_text, hist_codes, CFR_MAX_HISTORY);
    if (hist_n < 0)
    {
        fprintf(stderr, "Invalid --history format\n");
        return 1;
    }

    if (q.street < 0)
    {
        q.street = cfr_derive_street_from_board_count(board_n);
    }
    if (q.street < 0 || q.street > 3)
    {
        fprintf(stderr, "Street must be preflop/flop/turn/river (board cards must be 0/3/4/5)\n");
        return 1;
    }

    if (!cfr_blueprint_load(&bp, q.blueprint_path))
    {
        fprintf(stderr, "Failed to load blueprint: %s\n", q.blueprint_path);
        return 1;
    }

    {
        uint32_t active_abstraction_hash32;
        if (!cfr_load_active_abstraction(q.abstraction_path, &active_abstraction_hash32))
        {
            if (q.abstraction_path != NULL)
            {
                fprintf(stderr, "Failed to load abstraction file: %s\n", q.abstraction_path);
            }
            else
            {
                fprintf(stderr, "Failed to initialize default abstraction config\n");
            }
            return 1;
        }

        if (bp.abstraction_hash32 != 0u && bp.abstraction_hash32 != active_abstraction_hash32)
        {
            if (!q.ignore_abstraction_compat)
            {
                fprintf(stderr,
                        "Blueprint abstraction mismatch: file hash=0x%08X current hash=0x%08X (use --ignore-abstraction-compat to override)\n",
                        (unsigned int)bp.abstraction_hash32,
                        (unsigned int)active_abstraction_hash32);
                return 1;
            }
            printf("Warning: abstraction mismatch ignored (file hash=0x%08X current hash=0x%08X)\n",
                   (unsigned int)bp.abstraction_hash32,
                   (unsigned int)active_abstraction_hash32);
        }
    }

    memset(&kf, 0, sizeof(kf));
    kf.street = q.street;
    kf.position = (q.player_seat - q.dealer_seat + CFR_MAX_PLAYERS) % CFR_MAX_PLAYERS;
    if (!cfr_hand_index_for_state(q.street, hole_cards[0], hole_cards[1], board_cards, board_n, &kf.hand_index))
    {
        fprintf(stderr, "Invalid hole/board/street combination for canonical indexing\n");
        return 1;
    }
    kf.hand_index = cfr_abstraction_bucket_for_state(q.street,
                                                     hole_cards[0],
                                                     hole_cards[1],
                                                     board_cards,
                                                     board_n,
                                                     kf.hand_index,
                                                     CFR_ABS_MODE_BLUEPRINT);
    kf.pot_bucket = cfr_bucket_amount(q.pot);
    kf.to_call_bucket = cfr_bucket_amount(q.to_call);
    kf.active_players = q.active_players;
    kf.history_hash = cfr_history_key_from_codes(hist_codes, hist_n);

    key = cfr_make_infoset_key(&kf);
    node = cfr_blueprint_get_node(&bp, key, 0, 1);

    memset(&pseudo, 0, sizeof(pseudo));
    pseudo.pot = q.pot;
    pseudo.to_call = q.to_call;
    pseudo.num_raises_street = q.raises_this_street;
    pseudo.last_full_raise = CFR_BIG_BLIND;
    pseudo.full_raise_seq = q.raises_this_street;
    pseudo.stack[q.player_seat] = q.stack;
    pseudo.committed_street[q.player_seat] = 0;
    pseudo.in_hand[q.player_seat] = 1;

    legal_count = cfr_get_legal_actions(&pseudo, q.player_seat, legal_actions, legal_targets);
    if (legal_count <= 0)
    {
        fprintf(stderr, "No legal actions in provided query state\n");
        return 1;
    }

    if (node == NULL)
    {
        for (i = 0; i < legal_count; ++i)
        {
            strat[i] = 1.0f / (float)legal_count;
        }
        for (i = legal_count; i < CFR_MAX_ACTIONS; ++i)
        {
            strat[i] = 0.0f;
        }

        printf("Blueprint node not found. Returning uniform fallback strategy for this abstraction bucket.\n");
    }
    else
    {
        cfr_compute_average_strategy_n(node, legal_count, strat);
    }

    printf("Query key: %llu\n", (unsigned long long)key);
    printf("Street=%d Position=%d HandBucket=%llu PotBucket=%d ToCallBucket=%d Active=%d\n",
           kf.street,
           kf.position,
           (unsigned long long)kf.hand_index,
           kf.pot_bucket,
           kf.to_call_bucket,
           kf.active_players);

    for (i = 0; i < legal_count; ++i)
    {
        if (legal_actions[i] == CFR_ACT_RAISE_TO || legal_actions[i] == CFR_ACT_ALL_IN || legal_actions[i] == CFR_ACT_BET_HALF || legal_actions[i] == CFR_ACT_BET_POT)
        {
            printf("  %-12s to=%d : %.6f\n", cfr_action_name(legal_actions[i], q.to_call), legal_targets[i], strat[i]);
        }
        else
        {
            printf("  %-12s : %.6f\n", cfr_action_name(legal_actions[i], q.to_call), strat[i]);
        }
    }

    return 0;
}

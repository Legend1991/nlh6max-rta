#ifdef _WIN32
#include <windows.h>
#endif

#define CFR_TRAIN_MAX_WORKERS 64

typedef struct
{
    const CFRTrainOptions *opt;
    uint64_t iteration_index;
    double strategy_weight;
    int accumulate_strategy;
    int force_no_prune;
} CFRTrainStepConfig;

enum
{
    CFR_WORKER_TASK_TRAIN = 0,
    CFR_WORKER_TASK_MERGE = 1
};

#define CFR_WORKER_NODE_FLAG_NEW_BASE CFR_NODE_NEW_BASE_MASK

typedef struct
{
    CFRBlueprint *local_bp;
    const CFRBlueprint *base_bp;
    CFRBlueprint *merge_dst_bp;
    int merge_worker_count;
    uint64_t iter_start;
    uint64_t iter_count;
    CFRTrainOptions opt_copy;
    int task_kind;
    int worker_id;
#ifdef _WIN32
    HANDLE start_event;
    HANDLE done_event;
    HANDLE thread;
#endif
    uint32_t *merge_indices;
    uint32_t merge_indices_capacity;
    uint32_t *merge_offsets;
    uint32_t merge_offsets_capacity;
    uint32_t merge_index_count;
    int merge_shard_count;
    int task_failed;
    int should_stop;
} CFRWorkerContext;

typedef struct
{
    int ready;
    int worker_count;
    CFRWorkerContext workers[CFR_TRAIN_MAX_WORKERS];
} CFRTrainerThreadPool;

static CFRTrainerThreadPool g_cfr_train_pool;

static int cfr_worker_should_reclaim_local_bp(const CFRWorkerContext *ctx)
{
    uint32_t used;
    uint32_t cap;
    uint64_t reclaim_threshold;

    if (ctx == NULL || ctx->local_bp == NULL)
    {
        return 0;
    }

    cap = ctx->local_bp->node_capacity;
    used = ctx->local_bp->used_node_count;

    /* Avoid churn on small overlays; reclaim only when capacity drift is clearly high. */
    if (cap < (1u << 20))
    {
        return 0;
    }

    if (used < (1u << 16))
    {
        used = (1u << 16);
    }
    reclaim_threshold = (uint64_t)used * 6ULL;
    return (uint64_t)cap > reclaim_threshold;
}

static int cfr_worker_reclaim_local_bp(CFRWorkerContext *ctx)
{
    if (ctx == NULL || ctx->local_bp == NULL)
    {
        return 0;
    }
    if (!cfr_worker_should_reclaim_local_bp(ctx))
    {
        return 1;
    }

    cfr_blueprint_release(ctx->local_bp);
    memset(ctx->local_bp, 0, sizeof(*ctx->local_bp));
    if (!cfr_blueprint_init(ctx->local_bp, (uint64_t)(ctx->worker_id + 1)))
    {
        return 0;
    }
    cfr_blueprint_reset_sparse(ctx->local_bp);

    /* Partition scratch also grows with touched sets; drop and regrow lazily. */
    free(ctx->merge_indices);
    ctx->merge_indices = NULL;
    ctx->merge_indices_capacity = 0u;
    free(ctx->merge_offsets);
    ctx->merge_offsets = NULL;
    ctx->merge_offsets_capacity = 0u;
    ctx->merge_index_count = 0u;
    ctx->merge_shard_count = 0;
    return 1;
}

static int cfr_reclaim_worker_blueprints_if_needed(int workers)
{
    int w;
    int reclaimed;

    reclaimed = 0;
    if (workers < 1)
    {
        return 1;
    }
    if (workers > CFR_TRAIN_MAX_WORKERS)
    {
        workers = CFR_TRAIN_MAX_WORKERS;
    }

    for (w = 0; w < workers; ++w)
    {
        CFRWorkerContext *ctx;
        ctx = &g_cfr_train_pool.workers[w];
        if (cfr_worker_should_reclaim_local_bp(ctx))
        {
            if (!cfr_worker_reclaim_local_bp(ctx))
            {
                return 0;
            }
            reclaimed++;
        }
    }

    if (reclaimed > 0)
    {
        fprintf(stderr, "Worker local overlay reclaim: reclaimed=%d\n", reclaimed);
    }
    return 1;
}

static int cfr_train_pool_force_reclaim(int workers)
{
    return cfr_reclaim_worker_blueprints_if_needed(workers);
}

static uint64_t cfr_mix_seed(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
}

static uint64_t cfr_seed_for_worker(uint64_t base_seed, int worker_id, uint64_t iter_start)
{
    uint64_t seed;

    seed = base_seed ^ (uint64_t)(worker_id + 1);
    seed ^= (iter_start * 0xD1B54A32D192ED03ULL);
    return cfr_mix_seed(seed);
}

static void cfr_blueprint_touch_reset(CFRBlueprint *bp)
{
    uint32_t i;

    if (bp == NULL)
    {
        return;
    }
    if (bp->touched_mark != NULL && bp->touched_indices != NULL)
    {
        for (i = 0u; i < bp->touched_count; ++i)
        {
            uint32_t idx;
            idx = bp->touched_indices[i];
            if (idx < bp->node_capacity)
            {
                cfr_touched_mark_clear(bp, idx);
            }
        }
    }
    bp->touched_count = 0u;
}

static void cfr_blueprint_touch_node(CFRBlueprint *bp, const CFRNode *node)
{
    uint32_t idx;

    if (bp == NULL || node == NULL)
    {
        return;
    }

    idx = (uint32_t)(node - bp->nodes);
    if (idx >= bp->used_node_count || bp->touched_mark == NULL || bp->touched_indices == NULL)
    {
        return;
    }

    if (!cfr_touched_mark_test(bp, idx))
    {
        cfr_touched_mark_set(bp, idx);
        if (bp->touched_count < bp->touched_capacity)
        {
            bp->touched_indices[bp->touched_count++] = idx;
        }
    }
}

static int cfr_sample_action_index(const float *strategy, int n, uint64_t *rng)
{
    double r;
    double acc;
    int i;

    r = cfr_rng_unit(rng);
    acc = 0.0;

    for (i = 0; i < n; ++i)
    {
        acc += (double)strategy[i];
        if (r <= acc)
        {
            return i;
        }
    }

    return n - 1;
}

static double cfr_terminal_utility(CFRHandState *st, int traverser)
{
    cfr_resolve_terminal(st);
    return (double)(st->stack[traverser] - CFR_START_STACK);
}

static int cfr_is_full_traversal_iteration(const CFRTrainOptions *opt, uint64_t iteration_index)
{
    if (opt == NULL)
    {
        return 1;
    }
    if (!opt->enable_pruning)
    {
        return 1;
    }
    if (iteration_index < opt->prune_start_iter)
    {
        return 1;
    }
    if (opt->prune_full_every_iters == 0ULL)
    {
        return 0;
    }
    return ((iteration_index - opt->prune_start_iter) % opt->prune_full_every_iters) == 0ULL;
}

static int cfr_should_accumulate_strategy(const CFRTrainOptions *opt, uint64_t iteration_index)
{
    uint64_t interval;

    if (opt == NULL)
    {
        return 1;
    }

    interval = opt->strategy_interval;
    if (interval <= 1ULL)
    {
        return 1;
    }
    return ((iteration_index + 1ULL) % interval) == 0ULL;
}

static int cfr_should_accumulate_node_strategy_street(const CFRTrainStepConfig *cfg, int street);

static int cfr_should_accumulate_node_strategy(const CFRTrainStepConfig *cfg, const CFRNode *node)
{
    if (cfg == NULL || node == NULL)
    {
        return 0;
    }
    return cfr_should_accumulate_node_strategy_street(cfg, (int)node->street_hint);
}

static int cfr_should_accumulate_node_strategy_street(const CFRTrainStepConfig *cfg, int street)
{
    int preflop_avg_enabled;

    if (cfg == NULL)
    {
        return 0;
    }
    if (!cfg->accumulate_strategy)
    {
        return 0;
    }
    preflop_avg_enabled = 1;
    if (cfg->opt != NULL)
    {
        /* Preserve legacy call-sites that zero-init CFRTrainOptions without setting the new flag. */
        if (!cfg->opt->enable_preflop_avg &&
            (cfg->opt->avg_start_seconds > 0ULL ||
             cfg->opt->warmup_seconds > 0ULL ||
             cfg->opt->snapshot_start_seconds > 0ULL ||
             cfg->opt->discount_every_seconds > 0ULL ||
             cfg->opt->strict_time_phases))
        {
            preflop_avg_enabled = 0;
        }
    }
    if (!preflop_avg_enabled)
    {
        return 0;
    }
    /* Pluribus blueprint keeps preflop as average strategy; postflop is built from snapshot-current finalize flow. */
    return street == 0;
}

static int cfr_should_create_train_node(const CFRTrainStepConfig *cfg, int player, int traverser, int street)
{
    if (player == traverser)
    {
        return 1;
    }
    return cfr_should_accumulate_node_strategy_street(cfg, street);
}

static int cfr_use_sampled_preflop_avg(const CFRTrainStepConfig *cfg, const CFRNode *node)
{
    if (cfg == NULL || node == NULL || cfg->opt == NULL)
    {
        return 0;
    }
    if (!cfg->opt->preflop_avg_sampled)
    {
        return 0;
    }
    return node->street_hint == 0u;
}

static void cfr_accumulate_node_strategy(CFRNode *node,
                                         const float *strategy,
                                         int legal_count,
                                         const CFRTrainStepConfig *cfg,
                                         uint64_t *rng)
{
    int i;

    if (node == NULL || strategy == NULL || cfg == NULL || legal_count <= 0)
    {
        return;
    }
    if (node->strategy_sum == NULL)
    {
        return;
    }

    if (cfr_use_sampled_preflop_avg(cfg, node))
    {
        int sampled;
        sampled = cfr_sample_action_index(strategy, legal_count, rng);
        if (sampled >= 0 && sampled < legal_count)
        {
            node->strategy_sum[sampled] += (float)cfg->strategy_weight;
        }
        return;
    }

    for (i = 0; i < legal_count; ++i)
    {
        node->strategy_sum[i] += (float)((double)strategy[i] * cfg->strategy_weight);
    }
}

static int32_t cfr_quantize_regret_value(const CFRTrainOptions *opt, double value)
{
    int32_t iv;

    if (value >= 0.0)
    {
        iv = (int32_t)(value + 0.5);
    }
    else
    {
        iv = (int32_t)(value - 0.5);
    }

    if (opt != NULL && opt->use_int_regret && iv < opt->regret_floor)
    {
        iv = opt->regret_floor;
    }
    return iv;
}

static int32_t cfr_add_i32_clamped(int32_t a, int32_t b)
{
    int64_t v;

    v = (int64_t)a + (int64_t)b;
    if (v > (int64_t)INT32_MAX)
    {
        v = (int64_t)INT32_MAX;
    }
    if (v < (int64_t)INT32_MIN)
    {
        v = (int64_t)INT32_MIN;
    }
    return (int32_t)v;
}

static int cfr_should_prune_action(const CFRTrainStepConfig *cfg, float regret, uint64_t *rng)
{
    if (cfg == NULL || cfg->opt == NULL)
    {
        return 0;
    }
    if (cfg->force_no_prune)
    {
        return 0;
    }
    if (!cfg->opt->enable_pruning)
    {
        return 0;
    }
    if (cfg->iteration_index < cfg->opt->prune_start_iter)
    {
        return 0;
    }
    if ((double)regret > cfg->opt->prune_threshold)
    {
        return 0;
    }
    if (cfg->opt->prune_prob <= 0.0)
    {
        return 0;
    }
    if (cfg->opt->prune_prob >= 1.0)
    {
        return 1;
    }
    return cfr_rng_unit(rng) < cfg->opt->prune_prob;
}

static int cfr_best_action_by_regret(const CFRNode *node, int legal_count)
{
    int i;
    int best_i;
    int32_t best_r;

    best_i = 0;
    best_r = node->regret[0];
    for (i = 1; i < legal_count; ++i)
    {
        if (node->regret[i] > best_r)
        {
            best_r = node->regret[i];
            best_i = i;
        }
    }
    return best_i;
}

static int cfr_action_is_prune_exempt(const CFRHandState *st, int player, int action, int target)
{
    CFRHandState child;

    if (st == NULL)
    {
        return 1;
    }

    /* Pluribus pruning excludes actions on the final betting round. */
    if (st->street >= 3)
    {
        return 1;
    }

    child = *st;
    cfr_apply_action(&child, player, action, target);
    cfr_auto_advance_rounds_if_needed(&child);
    return child.is_terminal ? 1 : 0;
}

static double cfr_traverse(CFRBlueprint *bp, CFRHandState *st, int traverser, const CFRTrainStepConfig *cfg)
{
    int p;
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int legal_count;
    int accumulate_node_strategy;
    int create_node;
    CFRInfoKeyFields kf;
    uint64_t key;
    CFRNode *node;
    float strategy[CFR_MAX_ACTIONS];

    if (st->is_terminal)
    {
        return cfr_terminal_utility(st, traverser);
    }

    cfr_auto_advance_rounds_if_needed(st);
    if (st->is_terminal)
    {
        return cfr_terminal_utility(st, traverser);
    }

    p = st->current_player;
    if (p < 0 || p >= CFR_MAX_PLAYERS)
    {
        return cfr_terminal_utility(st, traverser);
    }

    legal_count = cfr_get_legal_actions(st, p, legal_actions, legal_targets);
    if (legal_count <= 0)
    {
        st->needs_action[p] = 0;
        st->current_player = cfr_find_next_actor_from(st, p);
        cfr_auto_advance_rounds_if_needed(st);
        return cfr_traverse(bp, st, traverser, cfg);
    }

    if (!cfr_extract_infoset_fields(st, p, &kf))
    {
        return 0.0;
    }

    key = cfr_make_infoset_key(&kf);
    accumulate_node_strategy = cfr_should_accumulate_node_strategy_street(cfg, kf.street);
    create_node = cfr_should_create_train_node(cfg, p, traverser, kf.street);

    node = cfr_blueprint_get_node_ex(bp, key, create_node, legal_count, kf.street);
    if (node != NULL)
    {
        if (node->street_hint > 3u)
        {
            node->street_hint = (unsigned char)kf.street;
        }
        cfr_compute_strategy_n(node, legal_count, strategy);
    }
    else
    {
        int i;
        float uniform;

        uniform = 1.0f / (float)legal_count;
        for (i = 0; i < legal_count; ++i)
        {
            strategy[i] = uniform;
        }
    }

    {

        if (p == traverser)
        {
        double util[CFR_MAX_ACTIONS];
        unsigned char pruned[CFR_MAX_ACTIONS];
        int i;
        int considered_count;
        double considered_mass;
        double considered_node_util;
        double fallback;
        double node_util;

        memset(util, 0, sizeof(util));
        memset(pruned, 0, sizeof(pruned));

        considered_count = 0;
        for (i = 0; i < legal_count; ++i)
        {
            if (cfr_action_is_prune_exempt(st, p, legal_actions[i], legal_targets[i]))
            {
                pruned[i] = 0u;
            }
            else
            {
                pruned[i] = (unsigned char)cfr_should_prune_action(cfg, (float)node->regret[i], &bp->rng_state);
            }
            if (!pruned[i])
            {
                ++considered_count;
            }
        }

        if (considered_count <= 0)
        {
            int keep_i;
            keep_i = cfr_best_action_by_regret(node, legal_count);
            pruned[keep_i] = 0;
            considered_count = 1;
        }

        considered_mass = 0.0;
        considered_node_util = 0.0;
        for (i = 0; i < legal_count; ++i)
        {
            if (!pruned[i])
            {
                CFRHandState child;
                child = *st;
                cfr_apply_action(&child, p, legal_actions[i], legal_targets[i]);
                util[i] = cfr_traverse(bp, &child, traverser, cfg);
                considered_mass += (double)strategy[i];
                considered_node_util += (double)strategy[i] * util[i];
            }
        }

        if (considered_mass > 1e-12)
        {
            fallback = considered_node_util / considered_mass;
        }
        else
        {
            fallback = 0.0;
        }

        node_util = considered_node_util + (1.0 - considered_mass) * fallback;
        for (i = 0; i < legal_count; ++i)
        {
            if (pruned[i])
            {
                util[i] = fallback;
            }
        }

        node = cfr_blueprint_get_node_ex(bp, key, 1, legal_count, kf.street);
        if (node == NULL)
        {
            return node_util;
        }
        cfr_blueprint_touch_node(bp, node);
        for (i = 0; i < legal_count; ++i)
        {
            float regret_delta;
            regret_delta = (float)(util[i] - node_util);
            node->regret[i] = cfr_quantize_regret_value(cfg->opt, (double)node->regret[i] + (double)regret_delta);
        }
        if (accumulate_node_strategy)
        {
            cfr_accumulate_node_strategy(node,
                                         strategy,
                                         legal_count,
                                         cfg,
                                         &bp->rng_state);
        }

        return node_util;
    }
    else
    {
        int sampled;
        CFRHandState child;

        if (accumulate_node_strategy)
        {
            cfr_blueprint_touch_node(bp, node);
            cfr_accumulate_node_strategy(node,
                                         strategy,
                                         legal_count,
                                         cfg,
                                         &bp->rng_state);
        }

        sampled = cfr_sample_action_index(strategy, legal_count, &bp->rng_state);
        child = *st;
        cfr_apply_action(&child, p, legal_actions[sampled], legal_targets[sampled]);
        return cfr_traverse(bp, &child, traverser, cfg);
    }
}
}

static void cfr_apply_linear_discount_event_index(CFRBlueprint *bp, const CFRTrainOptions *opt, uint64_t event_index);

static void cfr_apply_linear_discount_at_iteration(CFRBlueprint *bp, const CFRTrainOptions *opt, uint64_t iter)
{
    uint64_t stage_u64;

    if (bp == NULL || opt == NULL)
    {
        return;
    }
    if (!opt->enable_linear_discount || opt->linear_discount_every_iters == 0)
    {
        return;
    }
    if (opt->linear_discount_stop_iter > 0ULL && iter > opt->linear_discount_stop_iter)
    {
        return;
    }
    if (iter == 0ULL || (iter % opt->linear_discount_every_iters) != 0ULL)
    {
        return;
    }

    stage_u64 = iter / opt->linear_discount_every_iters;
    if (stage_u64 == 0ULL)
    {
        return;
    }
    cfr_apply_linear_discount_event_index(bp, opt, stage_u64);
}

static void cfr_apply_linear_discount(CFRBlueprint *bp, const CFRTrainOptions *opt)
{
    if (bp == NULL)
    {
        return;
    }
    cfr_apply_linear_discount_at_iteration(bp, opt, bp->iteration);
}

static void cfr_apply_linear_discount_event_index(CFRBlueprint *bp, const CFRTrainOptions *opt, uint64_t event_index)
{
    double stage;
    double factor;

    if (bp == NULL || opt == NULL || event_index == 0ULL)
    {
        return;
    }

    stage = (double)event_index;
    factor = stage / (stage + opt->linear_discount_scale);
    if (factor <= 0.0 || factor >= 1.0)
    {
        return;
    }

    if (bp->lazy_discount_scale <= 0.0)
    {
        bp->lazy_discount_scale = 1.0;
    }
    bp->lazy_discount_scale *= factor;

    /* Keep deferred scale numerically stable in very long runs. */
    if (bp->lazy_discount_scale < 1e-12)
    {
        cfr_blueprint_materialize_all(bp);
        bp->lazy_discount_scale = 1.0;
        if (opt->use_int_regret)
        {
            uint32_t i;
            for (i = 0u; i < bp->used_node_count; ++i)
            {
                CFRNode *n;
                int a;
                n = &bp->nodes[i];
                if (!cfr_node_is_used(n))
                {
                    continue;
                }
                for (a = 0; a < n->action_count; ++a)
                {
                    n->regret[a] = cfr_quantize_regret_value(opt, (double)n->regret[a]);
                }
            }
        }
    }
}

static void cfr_apply_linear_discount_range(CFRBlueprint *bp,
                                            const CFRTrainOptions *opt,
                                            uint64_t iter_from_exclusive,
                                            uint64_t iter_to_inclusive)
{
    uint64_t iter;

    if (bp == NULL || opt == NULL || iter_to_inclusive <= iter_from_exclusive)
    {
        return;
    }

    for (iter = iter_from_exclusive + 1ULL; iter <= iter_to_inclusive; ++iter)
    {
        cfr_apply_linear_discount_at_iteration(bp, opt, iter);
    }
}

static void cfr_run_iteration(CFRBlueprint *bp, const CFRTrainOptions *opt)
{
    CFRTrainOptions fallback_opt;
    const CFRTrainOptions *use_opt;
    CFRTrainStepConfig cfg;
    int traverser;
    int sample;
    int samples_per_player;

    if (bp == NULL)
    {
        return;
    }

    if (opt == NULL)
    {
        memset(&fallback_opt, 0, sizeof(fallback_opt));
        fallback_opt.samples_per_player = 1;
        fallback_opt.strategy_interval = 1ULL;
        fallback_opt.enable_preflop_avg = 1;
        use_opt = &fallback_opt;
    }
    else
    {
        use_opt = opt;
    }

    samples_per_player = use_opt->samples_per_player;
    if (samples_per_player < 1)
    {
        samples_per_player = 1;
    }

    cfg.opt = use_opt;
    cfg.iteration_index = bp->iteration;
    cfg.strategy_weight = (double)(bp->iteration + 1ULL);
    cfg.accumulate_strategy = cfr_should_accumulate_strategy(use_opt, bp->iteration);
    cfg.force_no_prune = cfr_is_full_traversal_iteration(use_opt, bp->iteration);

    for (traverser = 0; traverser < CFR_MAX_PLAYERS; ++traverser)
    {
        for (sample = 0; sample < samples_per_player; ++sample)
        {
            CFRHandState st;
            int dealer;

            dealer = (int)((bp->iteration + (uint64_t)traverser + (uint64_t)(sample * CFR_MAX_PLAYERS)) % (uint64_t)CFR_MAX_PLAYERS);
            cfr_init_hand(&st, dealer, &bp->rng_state);
            (void)cfr_traverse(bp, &st, traverser, &cfg);
            bp->total_hands++;
        }
    }

    bp->iteration++;
    cfr_apply_linear_discount(bp, use_opt);
}

static void cfr_merge_worker_delta(CFRBlueprint *delta, const CFRBlueprint *base, const CFRBlueprint *worker)
{
    uint32_t i;

    if (delta == NULL || base == NULL || worker == NULL)
    {
        return;
    }

    for (i = 0u; i < worker->touched_count; ++i)
    {
        uint32_t idx;
        const CFRNode *w;
        const CFRNode *b;
        CFRNode *d;
        int a;

        idx = worker->touched_indices[i];
        if (idx >= worker->used_node_count)
        {
            continue;
        }
        w = &worker->nodes[idx];
        if (!cfr_node_is_used(w))
        {
            continue;
        }
        d = cfr_blueprint_get_node_ex(delta, w->key, 1, w->action_count, (int)w->street_hint);
        if (d == NULL)
        {
            continue;
        }
        b = cfr_blueprint_get_node((CFRBlueprint *)base, w->key, 0, w->action_count);
        if (d->street_hint > 3u)
        {
            d->street_hint = w->street_hint;
        }

        for (a = 0; a < d->action_count; ++a)
        {
            int32_t base_regret;
            float base_sum;

            if (b != NULL && a < b->action_count)
            {
                base_regret = b->regret[a];
                base_sum = (b->strategy_sum != NULL) ? b->strategy_sum[a] : 0.0f;
            }
            else
            {
                base_regret = 0;
                base_sum = 0.0f;
            }
            d->regret[a] = cfr_add_i32_clamped(d->regret[a], (int32_t)(w->regret[a] - base_regret));
            if (d->strategy_sum != NULL && w->strategy_sum != NULL)
            {
                d->strategy_sum[a] += (w->strategy_sum[a] - base_sum);
            }
        }
    }
}

static void cfr_apply_delta_to_blueprint(CFRBlueprint *dst, const CFRBlueprint *delta)
{
    uint32_t i;

    if (dst == NULL || delta == NULL)
    {
        return;
    }

    for (i = 0u; i < delta->used_node_count; ++i)
    {
        const CFRNode *d;
        CFRNode *g;
        int a;

        d = &delta->nodes[i];
        if (!cfr_node_is_used(d))
        {
            continue;
        }

        g = cfr_blueprint_get_node_ex(dst, d->key, 1, d->action_count, (int)d->street_hint);
        if (g == NULL)
        {
            continue;
        }
        if (g->street_hint > 3u)
        {
            g->street_hint = d->street_hint;
        }

        for (a = 0; a < d->action_count; ++a)
        {
            g->regret[a] = cfr_add_i32_clamped(g->regret[a], d->regret[a]);
            if (g->strategy_sum != NULL && d->strategy_sum != NULL)
            {
                g->strategy_sum[a] += d->strategy_sum[a];
            }
        }
    }
}

static int cfr_worker_key_shard(uint64_t key, int shard_count)
{
    if (shard_count <= 1)
    {
        return 0;
    }
    return (int)(cfr_mix_seed(key) % (uint64_t)shard_count);
}

static int cfr_worker_ensure_u32_capacity(uint32_t **buf, uint32_t *capacity, uint32_t min_capacity)
{
    uint32_t new_cap;
    uint32_t *new_buf;

    if (buf == NULL || capacity == NULL)
    {
        return 0;
    }
    if (*capacity >= min_capacity)
    {
        return 1;
    }

    new_cap = (*capacity > 0u) ? *capacity : 64u;
    while (new_cap < min_capacity)
    {
        if (new_cap > (UINT32_MAX / 2u))
        {
            new_cap = min_capacity;
            break;
        }
        new_cap *= 2u;
    }

    new_buf = (uint32_t *)realloc(*buf, (size_t)new_cap * sizeof(uint32_t));
    if (new_buf == NULL)
    {
        return 0;
    }

    *buf = new_buf;
    *capacity = new_cap;
    return 1;
}

static int cfr_build_shard_partitions(const CFRBlueprint *worker,
                                      int shard_count,
                                      uint32_t *out_indices,
                                      uint32_t *out_offsets)
{
    uint32_t counts[CFR_TRAIN_MAX_WORKERS];
    uint32_t positions[CFR_TRAIN_MAX_WORKERS];
    uint32_t i;
    uint32_t total;
    int s;

    if (worker == NULL || out_offsets == NULL)
    {
        return 0;
    }
    if (shard_count < 1 || shard_count > CFR_TRAIN_MAX_WORKERS)
    {
        return 0;
    }

    memset(counts, 0, sizeof(counts));
    total = 0u;
    for (i = 0u; i < worker->touched_count; ++i)
    {
        uint32_t idx;
        const CFRNode *w;
        int shard;

        idx = worker->touched_indices[i];
        if (idx >= worker->used_node_count)
        {
            continue;
        }
        w = &worker->nodes[idx];
        if (!cfr_node_is_used(w))
        {
            continue;
        }
        shard = cfr_worker_key_shard(w->key, shard_count);
        if (shard < 0 || shard >= shard_count)
        {
            continue;
        }
        counts[shard]++;
        total++;
    }

    out_offsets[0] = 0u;
    for (s = 0; s < shard_count; ++s)
    {
        out_offsets[s + 1] = out_offsets[s] + counts[s];
        positions[s] = out_offsets[s];
    }
    if (total == 0u)
    {
        return 1;
    }
    if (out_indices == NULL)
    {
        return 0;
    }

    for (i = 0u; i < worker->touched_count; ++i)
    {
        uint32_t idx;
        const CFRNode *w;
        int shard;

        idx = worker->touched_indices[i];
        if (idx >= worker->used_node_count)
        {
            continue;
        }
        w = &worker->nodes[idx];
        if (!cfr_node_is_used(w))
        {
            continue;
        }
        shard = cfr_worker_key_shard(w->key, shard_count);
        if (shard < 0 || shard >= shard_count)
        {
            continue;
        }
        out_indices[positions[shard]++] = idx;
    }
    return 1;
}

static int cfr_prepare_worker_merge_partitions(CFRWorkerContext *ctx, int shard_count)
{
    uint32_t needed_indices;
    uint32_t needed_offsets;

    if (ctx == NULL || ctx->local_bp == NULL)
    {
        return 0;
    }
    if (shard_count < 1 || shard_count > CFR_TRAIN_MAX_WORKERS)
    {
        return 0;
    }

    needed_indices = ctx->local_bp->touched_count;
    needed_offsets = (uint32_t)shard_count + 1u;

    if (needed_indices > 0u)
    {
        if (!cfr_worker_ensure_u32_capacity(&ctx->merge_indices, &ctx->merge_indices_capacity, needed_indices))
        {
            return 0;
        }
    }
    if (!cfr_worker_ensure_u32_capacity(&ctx->merge_offsets, &ctx->merge_offsets_capacity, needed_offsets))
    {
        return 0;
    }
    if (!cfr_build_shard_partitions(ctx->local_bp, shard_count, ctx->merge_indices, ctx->merge_offsets))
    {
        return 0;
    }

    ctx->merge_shard_count = shard_count;
    ctx->merge_index_count = ctx->merge_offsets[shard_count];
    return 1;
}

static int cfr_precreate_worker_nodes(CFRBlueprint *bp, const CFRBlueprint *worker)
{
    uint32_t i;

    if (bp == NULL || worker == NULL)
    {
        return 0;
    }

    for (i = 0u; i < worker->touched_count; ++i)
    {
        uint32_t idx;
        const CFRNode *w;
        CFRNode *g;

        idx = worker->touched_indices[i];
        if (idx >= worker->used_node_count)
        {
            continue;
        }
        w = &worker->nodes[idx];
        if (!cfr_node_is_used(w))
        {
            continue;
        }
        if (!cfr_node_has_new_base_flag(w))
        {
            continue;
        }

        g = cfr_blueprint_get_node_ex(bp, w->key, 1, w->action_count, (int)w->street_hint);
        if (g == NULL)
        {
            return 0;
        }
        if (g->street_hint > 3u)
        {
            g->street_hint = w->street_hint;
        }
    }

    return 1;
}

static void cfr_prepare_worker_delta_against_base(CFRBlueprint *base, CFRBlueprint *worker)
{
    uint32_t i;

    if (base == NULL || worker == NULL)
    {
        return;
    }

    for (i = 0u; i < worker->touched_count; ++i)
    {
        uint32_t idx;
        CFRNode *w;
        uint32_t base_index;
        int a;
        double base_ratio;

        idx = worker->touched_indices[i];
        if (idx >= worker->used_node_count)
        {
            continue;
        }
        w = &worker->nodes[idx];
        if (!cfr_node_is_used(w))
        {
            continue;
        }

        base_index = cfr_node_overlay_base_index_plus1(w);
        if (base_index > 0u)
        {
            base_index -= 1u;
            if (base_index >= base->used_node_count || !cfr_node_is_used(&base->nodes[base_index]))
            {
                base_index = 0u;
            }
        }
        if (base_index == 0u)
        {
            CFRNode *b;
            b = cfr_blueprint_find_node_raw(base, w->key, &base_index);
            if (b == NULL)
            {
                cfr_node_set_new_base_flag(w, 1);
                base_index = UINT32_MAX;
            }
            else
            {
                cfr_node_set_overlay_meta(w, base_index + 1u, 0);
            }
        }
        else
        {
            cfr_node_set_new_base_flag(w, 0);
        }
        base_ratio = 1.0;
        if (base_index != UINT32_MAX && base_index < base->used_node_count)
        {
            float base_mark;
            double base_target;

            base_target = (base->lazy_discount_scale > 0.0) ? base->lazy_discount_scale : 1.0;
            base_mark = 1.0f;
            if (base->node_discount_scale != NULL)
            {
                base_mark = base->node_discount_scale[base_index];
            }
            if (!(base_mark > 0.0f))
            {
                base_mark = 1.0f;
            }
            base_ratio = base_target / (double)base_mark;
        }
        for (a = 0; a < w->action_count; ++a)
        {
            int32_t base_regret;
            float base_sum;

            if (base_index != UINT32_MAX &&
                base_index < base->used_node_count &&
                cfr_node_is_used(&base->nodes[base_index]) &&
                a < base->nodes[base_index].action_count)
            {
                const CFRNode *b;
                double scaled_regret;

                b = &base->nodes[base_index];
                scaled_regret = (double)b->regret[a] * base_ratio;
                if (scaled_regret >= 0.0)
                {
                    base_regret = (int32_t)(scaled_regret + 0.5);
                }
                else
                {
                    base_regret = (int32_t)(scaled_regret - 0.5);
                }
                base_sum = (b->strategy_sum != NULL) ? (float)((double)b->strategy_sum[a] * base_ratio) : 0.0f;
            }
            else
            {
                base_regret = 0;
                base_sum = 0.0f;
            }
            w->regret[a] -= base_regret;
            if (w->strategy_sum != NULL)
            {
                w->strategy_sum[a] -= base_sum;
            }
        }
    }
}

static int cfr_precreate_all_worker_nodes(CFRBlueprint *bp, int workers)
{
    int w;

    if (bp == NULL || workers < 1)
    {
        return 0;
    }

    for (w = 0; w < workers; ++w)
    {
        if (!cfr_precreate_worker_nodes(bp, g_cfr_train_pool.workers[w].local_bp))
        {
            return 0;
        }
    }

    return 1;
}

static void cfr_apply_worker_delta_indices(CFRBlueprint *dst,
                                           const CFRBlueprint *worker,
                                           const uint32_t *indices,
                                           uint32_t begin,
                                           uint32_t end)
{
    uint32_t pos;

    if (dst == NULL || worker == NULL || indices == NULL)
    {
        return;
    }

    for (pos = begin; pos < end; ++pos)
    {
        uint32_t idx;
        const CFRNode *w;
        CFRNode *g;
        int a;

        idx = indices[pos];
        if (idx >= worker->used_node_count)
        {
            continue;
        }
        w = &worker->nodes[idx];
        if (!cfr_node_is_used(w))
        {
            continue;
        }

        g = cfr_blueprint_find_node_raw(dst, w->key, NULL);
        if (g == NULL)
        {
            continue;
        }
        if (g->street_hint > 3u)
        {
            g->street_hint = w->street_hint;
        }
        for (a = 0; a < w->action_count; ++a)
        {
            g->regret[a] = cfr_add_i32_clamped(g->regret[a], w->regret[a]);
            if (g->strategy_sum != NULL && w->strategy_sum != NULL)
            {
                g->strategy_sum[a] += w->strategy_sum[a];
            }
        }
    }
}

static void cfr_apply_worker_delta_shard(CFRBlueprint *dst,
                                         const CFRBlueprint *worker,
                                         int shard_id,
                                         int shard_count)
{
    uint32_t i;

    if (dst == NULL || worker == NULL)
    {
        return;
    }

    for (i = 0u; i < worker->touched_count; ++i)
    {
        uint32_t idx;
        const CFRNode *w;
        CFRNode *g;
        int a;

        idx = worker->touched_indices[i];
        if (idx >= worker->used_node_count)
        {
            continue;
        }
        w = &worker->nodes[idx];
        if (!cfr_node_is_used(w))
        {
            continue;
        }
        if (cfr_worker_key_shard(w->key, shard_count) != shard_id)
        {
            continue;
        }

        g = cfr_blueprint_find_node_raw(dst, w->key, NULL);
        if (g == NULL)
        {
            continue;
        }
        if (g->street_hint > 3u)
        {
            g->street_hint = w->street_hint;
        }
        for (a = 0; a < w->action_count; ++a)
        {
            g->regret[a] = cfr_add_i32_clamped(g->regret[a], w->regret[a]);
            if (g->strategy_sum != NULL && w->strategy_sum != NULL)
            {
                g->strategy_sum[a] += w->strategy_sum[a];
            }
        }
    }
}

static void cfr_apply_worker_delta_shard_fast(CFRBlueprint *dst,
                                              const CFRWorkerContext *worker_ctx,
                                              int shard_id,
                                              int shard_count)
{
    const CFRBlueprint *worker;
    uint32_t begin;
    uint32_t end;

    if (worker_ctx == NULL)
    {
        return;
    }
    worker = worker_ctx->local_bp;
    if (worker == NULL)
    {
        return;
    }
    if (shard_id < 0 || shard_id >= shard_count || shard_count < 1)
    {
        return;
    }

    if (worker_ctx->merge_shard_count == shard_count &&
        worker_ctx->merge_offsets != NULL &&
        worker_ctx->merge_offsets_capacity >= (uint32_t)(shard_count + 1) &&
        worker_ctx->merge_indices != NULL)
    {
        begin = worker_ctx->merge_offsets[shard_id];
        end = worker_ctx->merge_offsets[shard_id + 1];
        if (end > begin)
        {
            cfr_apply_worker_delta_indices(dst, worker, worker_ctx->merge_indices, begin, end);
        }
        return;
    }

    cfr_apply_worker_delta_shard(dst, worker, shard_id, shard_count);
}

static void cfr_merge_delta_shard_task(const CFRWorkerContext *ctx)
{
    int w;

    if (ctx == NULL || ctx->merge_dst_bp == NULL)
    {
        return;
    }

    for (w = 0; w < ctx->merge_worker_count; ++w)
    {
        cfr_apply_worker_delta_shard_fast(ctx->merge_dst_bp,
                                          &g_cfr_train_pool.workers[w],
                                          ctx->worker_id,
                                          ctx->merge_worker_count);
    }
}

static int cfr_any_worker_task_failed(int workers)
{
    int w;

    if (workers < 1)
    {
        return 1;
    }
    if (workers > CFR_TRAIN_MAX_WORKERS)
    {
        workers = CFR_TRAIN_MAX_WORKERS;
    }

    for (w = 0; w < workers; ++w)
    {
        if (g_cfr_train_pool.workers[w].task_failed)
        {
            return 1;
        }
    }
    return 0;
}

#ifdef _WIN32
static DWORD WINAPI cfr_worker_proc(LPVOID param)
{
    CFRWorkerContext *ctx;

    ctx = (CFRWorkerContext *)param;
    if (ctx == NULL)
    {
        return 1;
    }

    for (;;)
    {
        uint64_t i;
        WaitForSingleObject(ctx->start_event, INFINITE);

        if (ctx->should_stop)
        {
            SetEvent(ctx->done_event);
            return 0;
        }

        if (ctx->local_bp == NULL)
        {
            SetEvent(ctx->done_event);
            continue;
        }

        if (ctx->task_kind == CFR_WORKER_TASK_MERGE)
        {
            cfr_merge_delta_shard_task(ctx);
        }
        else
        {
            if (ctx->base_bp == NULL)
            {
                SetEvent(ctx->done_event);
                continue;
            }
            cfr_blueprint_reset_sparse(ctx->local_bp);
            ctx->local_bp->overlay_base = ctx->base_bp;
            ctx->local_bp->omit_postflop_strategy_sum = ctx->base_bp->omit_postflop_strategy_sum;
            ctx->local_bp->compat_hash32 = ctx->base_bp->compat_hash32;
            ctx->local_bp->abstraction_hash32 = ctx->base_bp->abstraction_hash32;
            ctx->local_bp->iteration = ctx->iter_start;
            ctx->local_bp->total_hands = 0ULL;
            ctx->local_bp->rng_state = cfr_seed_for_worker(ctx->base_bp->rng_state, ctx->worker_id, ctx->iter_start);
            for (i = 0ULL; i < ctx->iter_count; ++i)
            {
                cfr_run_iteration(ctx->local_bp, &ctx->opt_copy);
            }

            cfr_prepare_worker_delta_against_base((CFRBlueprint *)ctx->base_bp, ctx->local_bp);
            if (ctx->opt_copy.parallel_mode == CFR_PARALLEL_MODE_SHARDED)
            {
                if (!cfr_prepare_worker_merge_partitions(ctx, ctx->merge_worker_count))
                {
                    ctx->task_failed = 1;
                }
            }
            else
            {
                ctx->merge_shard_count = 0;
                ctx->merge_index_count = 0u;
            }
        }

        SetEvent(ctx->done_event);
    }
}

static void cfr_train_pool_shutdown(void)
{
    int w;

    if (!g_cfr_train_pool.ready)
    {
        return;
    }

    for (w = 0; w < g_cfr_train_pool.worker_count; ++w)
    {
        CFRWorkerContext *ctx;
        ctx = &g_cfr_train_pool.workers[w];
        ctx->should_stop = 1;
        ResetEvent(ctx->done_event);
        SetEvent(ctx->start_event);
    }

    for (w = 0; w < g_cfr_train_pool.worker_count; ++w)
    {
        CFRWorkerContext *ctx;
        ctx = &g_cfr_train_pool.workers[w];
        if (ctx->thread != NULL)
        {
            WaitForSingleObject(ctx->thread, INFINITE);
            CloseHandle(ctx->thread);
        }
        if (ctx->start_event != NULL)
        {
            CloseHandle(ctx->start_event);
        }
        if (ctx->done_event != NULL)
        {
            CloseHandle(ctx->done_event);
        }
        free(ctx->merge_indices);
        free(ctx->merge_offsets);
        cfr_blueprint_release(ctx->local_bp);
        free(ctx->local_bp);
        memset(ctx, 0, sizeof(*ctx));
    }

    if (g_cfr_train_pool.ready && g_cfr_train_pool.worker_count > 0)
    {
        /* No-op; pool metadata cleared below. */
    }

    memset(&g_cfr_train_pool, 0, sizeof(g_cfr_train_pool));
}

static int cfr_train_pool_ensure(int workers)
{
    int w;

    if (workers < 1)
    {
        workers = 1;
    }
    if (workers > CFR_TRAIN_MAX_WORKERS)
    {
        workers = CFR_TRAIN_MAX_WORKERS;
    }

    if (g_cfr_train_pool.ready && g_cfr_train_pool.worker_count == workers)
    {
        return 1;
    }

    cfr_train_pool_shutdown();

    g_cfr_train_pool.worker_count = workers;
    for (w = 0; w < workers; ++w)
    {
        CFRWorkerContext *ctx;
        ctx = &g_cfr_train_pool.workers[w];
        memset(ctx, 0, sizeof(*ctx));

        ctx->worker_id = w;
        ctx->local_bp = (CFRBlueprint *)malloc(sizeof(CFRBlueprint));
        if (ctx->local_bp == NULL)
        {
            cfr_train_pool_shutdown();
            return 0;
        }
        memset(ctx->local_bp, 0, sizeof(*ctx->local_bp));
        if (!cfr_blueprint_init(ctx->local_bp, (uint64_t)(w + 1)))
        {
            cfr_train_pool_shutdown();
            return 0;
        }
        cfr_blueprint_reset_sparse(ctx->local_bp);

        ctx->start_event = CreateEvent(NULL, FALSE, FALSE, NULL);
        ctx->done_event = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (ctx->start_event == NULL || ctx->done_event == NULL)
        {
            cfr_train_pool_shutdown();
            return 0;
        }

        ctx->thread = CreateThread(NULL, 0, cfr_worker_proc, ctx, 0, NULL);
        if (ctx->thread == NULL)
        {
            cfr_train_pool_shutdown();
            return 0;
        }
    }

    g_cfr_train_pool.ready = 1;
    return 1;
}

static void cfr_run_parallel_train_phase(CFRBlueprint *bp,
                                         uint64_t start_iter,
                                         uint64_t iteration_count,
                                         const CFRTrainOptions *opt,
                                         int workers,
                                         HANDLE *done_handles)
{
    int w;
    uint64_t offset;
    uint64_t per_worker;
    uint64_t remainder;

    offset = 0ULL;
    per_worker = iteration_count / (uint64_t)workers;
    remainder = iteration_count % (uint64_t)workers;
    for (w = 0; w < workers; ++w)
    {
        CFRWorkerContext *ctx;
        uint64_t share;

        ctx = &g_cfr_train_pool.workers[w];
        share = per_worker + ((uint64_t)w < remainder ? 1ULL : 0ULL);

        ctx->task_kind = CFR_WORKER_TASK_TRAIN;
        ctx->base_bp = bp;
        ctx->iter_start = start_iter + offset;
        ctx->iter_count = share;
        ctx->opt_copy = *opt;
        ctx->opt_copy.enable_linear_discount = 0;
        ctx->merge_dst_bp = NULL;
        ctx->merge_worker_count = workers;
        ctx->task_failed = 0;
        ctx->should_stop = 0;

        done_handles[w] = ctx->done_event;
        ResetEvent(ctx->done_event);
        SetEvent(ctx->start_event);

        offset += share;
    }
}

static void cfr_run_parallel_merge_phase_sharded(CFRBlueprint *bp, int workers, HANDLE *done_handles)
{
    int w;

    if (bp == NULL || workers < 1)
    {
        return;
    }

    for (w = 0; w < workers; ++w)
    {
        CFRWorkerContext *ctx;

        ctx = &g_cfr_train_pool.workers[w];
        ctx->task_kind = CFR_WORKER_TASK_MERGE;
        ctx->base_bp = NULL;
        ctx->iter_start = 0ULL;
        ctx->iter_count = 0ULL;
        ctx->merge_dst_bp = bp;
        ctx->merge_worker_count = workers;
        ctx->should_stop = 0;

        done_handles[w] = ctx->done_event;
        ResetEvent(ctx->done_event);
        SetEvent(ctx->start_event);
    }
}

static int cfr_run_iterations_parallel_deterministic(CFRBlueprint *bp, int workers)
{
    int w;

    if (bp == NULL || workers < 1)
    {
        return 0;
    }

    for (w = 0; w < workers; ++w)
    {
        cfr_apply_worker_delta_shard(bp,
                                     g_cfr_train_pool.workers[w].local_bp,
                                     0,
                                     1);
    }
    return 1;
}

static int cfr_run_iterations_parallel_sharded(CFRBlueprint *bp, int workers, HANDLE *done_handles)
{
    if (bp == NULL || workers < 1)
    {
        return 0;
    }
    cfr_run_parallel_merge_phase_sharded(bp, workers, done_handles);
    WaitForMultipleObjects((DWORD)workers, done_handles, TRUE, INFINITE);
    return 1;
}

static int cfr_run_iterations_parallel(CFRBlueprint *bp, uint64_t iteration_count, const CFRTrainOptions *opt)
{
    HANDLE done_handles[CFR_TRAIN_MAX_WORKERS];
    int workers;
    int w;
    uint64_t samples_per_player_u64;
    uint64_t combined_rng;
    uint64_t start_iter;

    if (bp == NULL || opt == NULL)
    {
        return 0;
    }

    workers = opt->threads;
    if (workers < 2 || iteration_count < 2)
    {
        return 0;
    }

    if (!cfr_hand_index_init())
    {
        return 0;
    }
    if (workers > CFR_TRAIN_MAX_WORKERS)
    {
        workers = CFR_TRAIN_MAX_WORKERS;
    }
    if ((uint64_t)workers > iteration_count)
    {
        workers = (int)iteration_count;
    }

    if (!cfr_train_pool_ensure(workers))
    {
        return 0;
    }

    start_iter = bp->iteration;
    cfr_run_parallel_train_phase(bp, start_iter, iteration_count, opt, workers, done_handles);
    WaitForMultipleObjects((DWORD)workers, done_handles, TRUE, INFINITE);
    if (cfr_any_worker_task_failed(workers))
    {
        return 0;
    }
    if (!cfr_precreate_all_worker_nodes(bp, workers))
    {
        return 0;
    }

    if (opt->parallel_mode == CFR_PARALLEL_MODE_SHARDED)
    {
        if (!cfr_run_iterations_parallel_sharded(bp, workers, done_handles))
        {
            return 0;
        }
    }
    else
    {
        if (!cfr_run_iterations_parallel_deterministic(bp, workers))
        {
            return 0;
        }
    }

    samples_per_player_u64 = (uint64_t)cfr_max_int(1, opt->samples_per_player);
    bp->iteration = start_iter + iteration_count;
    bp->total_hands += iteration_count * (uint64_t)CFR_MAX_PLAYERS * samples_per_player_u64;

    cfr_apply_linear_discount_range(bp, opt, start_iter, bp->iteration);

    combined_rng = bp->rng_state;
    for (w = 0; w < workers; ++w)
    {
        combined_rng ^= cfr_mix_seed(g_cfr_train_pool.workers[w].local_bp->rng_state + (uint64_t)(w + 1));
    }
    bp->rng_state = combined_rng;
    if (!cfr_reclaim_worker_blueprints_if_needed(workers))
    {
        return 0;
    }

    return 1;
}
#endif

static int cfr_run_iterations(CFRBlueprint *bp, uint64_t iteration_count, const CFRTrainOptions *opt)
{
    uint64_t i;

    if (bp == NULL || iteration_count == 0)
    {
        return 1;
    }

#ifdef _WIN32
    if (opt != NULL && opt->threads > 1)
    {
        if (cfr_run_iterations_parallel(bp, iteration_count, opt))
        {
            return 1;
        }
        fprintf(stderr,
                "Parallel train chunk failed (threads=%d, mode=%d, chunk=%llu).\n",
                opt->threads,
                opt->parallel_mode,
                (unsigned long long)iteration_count);
        return 0;
    }
#endif

    for (i = 0; i < iteration_count; ++i)
    {
        cfr_run_iteration(bp, opt);
    }

    return 1;
}

typedef struct
{
    CFRRange1326 player_range[CFR_MAX_PLAYERS];
    int initialized[CFR_MAX_PLAYERS];
} CFRBeliefState;

static int32_t cfr_search_add_i32_clamped(int32_t a, int32_t b)
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

static void cfr_belief_init(CFRBeliefState *belief)
{
    int p;

    if (belief == NULL)
    {
        return;
    }
    memset(belief, 0, sizeof(*belief));
    for (p = 0; p < CFR_MAX_PLAYERS; ++p)
    {
        cfr_range_clear(&belief->player_range[p]);
        belief->initialized[p] = 0;
    }
}

static int cfr_belief_init_player_uniform(CFRBeliefState *belief,
                                          int player,
                                          const int *blocked_cards,
                                          int blocked_count)
{
    if (belief == NULL || player < 0 || player >= CFR_MAX_PLAYERS)
    {
        return 0;
    }
    cfr_range_init_uniform_blocked(&belief->player_range[player], blocked_cards, blocked_count);
    belief->initialized[player] = 1;
    return 1;
}

static int cfr_extract_infoset_fields_mode(const CFRHandState *st, int player, int mode, int root_street, CFRInfoKeyFields *out);

static int cfr_belief_fill_action_likelihood_from_policy(CFRPolicyProvider *bp,
                                                         const CFRBlueprint *search_sigma,
                                                         int root_street,
                                                         const CFRHandState *st,
                                                         int player,
                                                         int action,
                                                         int target,
                                                         const int *blocked_cards,
                                                         int blocked_count,
                                                         double *out_likelihood)
{
    unsigned char blocked[CFR_DECK_SIZE];
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int legal_count;
    int action_idx;
    int i;
    int valid_n;

    if (bp == NULL || st == NULL || out_likelihood == NULL || player < 0 || player >= CFR_MAX_PLAYERS)
    {
        return 0;
    }

    legal_count = cfr_get_legal_actions(st, player, legal_actions, legal_targets);
    if (legal_count <= 0)
    {
        memset(out_likelihood, 0, sizeof(double) * CFR_HOLDEM_COMBOS);
        return 0;
    }

    action_idx = -1;
    for (i = 0; i < legal_count; ++i)
    {
        if (legal_actions[i] == action && legal_targets[i] == target)
        {
            action_idx = i;
            break;
        }
    }
    if (action_idx < 0)
    {
        memset(out_likelihood, 0, sizeof(double) * CFR_HOLDEM_COMBOS);
        return 0;
    }

    memset(blocked, 0, sizeof(blocked));
    for (i = 0; i < st->board_count; ++i)
    {
        int c;
        c = st->board[i];
        if (c >= 0 && c < CFR_DECK_SIZE)
        {
            blocked[c] = 1u;
        }
    }
    if (blocked_cards != NULL && blocked_count > 0)
    {
        for (i = 0; i < blocked_count; ++i)
        {
            int c;
            c = blocked_cards[i];
            if (c >= 0 && c < CFR_DECK_SIZE)
            {
                blocked[c] = 1u;
            }
        }
    }

    valid_n = 0;
    for (i = 0; i < CFR_HOLDEM_COMBOS; ++i)
    {
        int c1;
        int c2;
        CFRHandState tmp;
        double p;
        int used_sigma;

        if (!cfr_range_index_to_cards(i, &c1, &c2))
        {
            out_likelihood[i] = 0.0;
            continue;
        }
        if (blocked[c1] || blocked[c2])
        {
            out_likelihood[i] = 0.0;
            continue;
        }

        tmp = *st;
        tmp.hole[player][0] = c1;
        tmp.hole[player][1] = c2;
        p = 0.0;
        used_sigma = 0;

        if (search_sigma != NULL)
        {
            CFRInfoKeyFields kf_sigma;
            uint64_t key_sigma;
            CFRNode *node_sigma;

            if (cfr_extract_infoset_fields_mode(&tmp, player, CFR_ABS_MODE_SEARCH, root_street, &kf_sigma))
            {
                key_sigma = cfr_make_infoset_key(&kf_sigma);
                node_sigma = cfr_blueprint_get_node((CFRBlueprint *)search_sigma, key_sigma, 0, legal_count);
                if (node_sigma != NULL)
                {
                    float strat_sigma[CFR_MAX_ACTIONS];
                    /* Belief updates consume weighted-average sigma when available. */
                    cfr_compute_average_strategy_n(node_sigma, legal_count, strat_sigma);
                    p = (double)strat_sigma[action_idx];
                    used_sigma = 1;
                }
            }
        }

        if (!used_sigma)
        {
            CFRInfoKeyFields kf_bp;
            uint64_t key_bp;
            float strat_bp[CFR_MAX_ACTIONS];

            if (!cfr_extract_infoset_fields(&tmp, player, &kf_bp))
            {
                out_likelihood[i] = 0.0;
                continue;
            }
            key_bp = cfr_make_infoset_key(&kf_bp);
            if (!cfr_policy_provider_get_average_policy(bp, key_bp, kf_bp.street, legal_count, strat_bp))
            {
                p = 1.0 / (double)legal_count;
            }
            else
            {
                p = (double)strat_bp[action_idx];
            }
        }

        if (p < 0.0)
        {
            p = 0.0;
        }
        out_likelihood[i] = p;
        valid_n++;
    }

    return valid_n;
}

static int cfr_belief_update_player_action(CFRBeliefState *belief,
                                           CFRPolicyProvider *bp,
                                           const CFRBlueprint *search_sigma,
                                           int root_street,
                                           const CFRHandState *st,
                                           int player,
                                           int action,
                                           int target,
                                           const int *blocked_cards,
                                           int blocked_count)
{
    static double likelihood[CFR_HOLDEM_COMBOS];
    int valid_n;

    if (belief == NULL || bp == NULL || st == NULL || player < 0 || player >= CFR_MAX_PLAYERS)
    {
        return 0;
    }
    if (!belief->initialized[player])
    {
        if (!cfr_belief_init_player_uniform(belief, player, blocked_cards, blocked_count))
        {
            return 0;
        }
    }

    valid_n = cfr_belief_fill_action_likelihood_from_policy(bp,
                                                            search_sigma,
                                                            root_street,
                                                            st,
                                                            player,
                                                            action,
                                                            target,
                                                            blocked_cards,
                                                            blocked_count,
                                                            likelihood);
    if (valid_n <= 0)
    {
        return 0;
    }

    cfr_range_apply_likelihood(&belief->player_range[player], likelihood);
    return 1;
}

static double cfr_belief_player_combo_prob(const CFRBeliefState *belief, int player, int combo_index)
{
    if (belief == NULL || player < 0 || player >= CFR_MAX_PLAYERS || combo_index < 0 || combo_index >= CFR_HOLDEM_COMBOS)
    {
        return 0.0;
    }
    return belief->player_range[player].weight[combo_index];
}

static int cfr_belief_collect_round_root_blockers(const CFRHandState *st,
                                                  int observer_player,
                                                  int *out_cards,
                                                  int max_cards)
{
    int n;
    int i;

    if (st == NULL || out_cards == NULL || max_cards <= 0)
    {
        return 0;
    }

    n = 0;
    for (i = 0; i < st->board_count && n < max_cards; ++i)
    {
        int c;
        c = st->board[i];
        if (c >= 0 && c < CFR_DECK_SIZE)
        {
            out_cards[n++] = c;
        }
    }

    if (observer_player >= 0 && observer_player < CFR_MAX_PLAYERS)
    {
        int c1;
        int c2;

        c1 = st->hole[observer_player][0];
        c2 = st->hole[observer_player][1];
        if (c1 >= 0 && c1 < CFR_DECK_SIZE && c2 >= 0 && c2 < CFR_DECK_SIZE && c1 != c2)
        {
            if (n < max_cards) out_cards[n++] = c1;
            if (n < max_cards) out_cards[n++] = c2;
        }
    }

    return n;
}

static int cfr_belief_apply_round_root_blockers(CFRBeliefState *belief,
                                                const CFRHandState *st,
                                                int observer_player)
{
    int blockers[CFR_MAX_BOARD + 2];
    int blocker_n;
    int p;
    int updated_n;

    if (belief == NULL || st == NULL)
    {
        return 0;
    }

    blocker_n = cfr_belief_collect_round_root_blockers(st, observer_player, blockers, (int)(sizeof(blockers) / sizeof(blockers[0])));
    if (blocker_n <= 0)
    {
        return 0;
    }

    updated_n = 0;
    for (p = 0; p < CFR_MAX_PLAYERS; ++p)
    {
        if (p == observer_player)
        {
            continue;
        }
        if (!belief->initialized[p])
        {
            continue;
        }
        cfr_range_apply_blockers(&belief->player_range[p], blockers, blocker_n);
        updated_n++;
    }
    return updated_n;
}

typedef struct
{
    int player;
    int action;
    int target;
} CFRSearchFrozenAction;

typedef struct
{
    int legal_count;
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    float final_policy[CFR_MAX_ACTIONS];
    float avg_policy[CFR_MAX_ACTIONS];
    int chosen_index;
    uint64_t iterations_done;
    int belief_updates;
} CFRSearchDecision;

typedef struct
{
    CFRBlueprint subgame_bp;
    int root_street;
    uint32_t root_history_hash;
    int initialized;
    uint64_t seed;
} CFRSearchContext;

static int cfr_extract_infoset_fields_mode(const CFRHandState *st, int player, int mode, int root_street, CFRInfoKeyFields *out)
{
    int to_call_player;
    uint64_t canonical_hand_index;
    uint64_t hand_bucket;

    if (st == NULL || out == NULL)
    {
        return 0;
    }

    if (!cfr_hand_index_for_state(st->street,
                                  st->hole[player][0],
                                  st->hole[player][1],
                                  st->board,
                                  st->board_count,
                                  &canonical_hand_index))
    {
        return 0;
    }

    to_call_player = st->to_call - st->committed_street[player];
    if (to_call_player < 0)
    {
        to_call_player = 0;
    }

    if (mode == CFR_ABS_MODE_SEARCH && st->street == root_street)
    {
        /* Pluribus keeps the current round lossless in real-time search. */
        hand_bucket = canonical_hand_index;
    }
    else
    {
        hand_bucket = cfr_abstraction_bucket_for_state(st->street,
                                                       st->hole[player][0],
                                                       st->hole[player][1],
                                                       st->board,
                                                       st->board_count,
                                                       canonical_hand_index,
                                                       mode);
    }
    out->street = st->street;
    out->position = (player - st->dealer + CFR_MAX_PLAYERS) % CFR_MAX_PLAYERS;
    out->hand_index = hand_bucket;
    out->pot_bucket = cfr_bucket_amount(st->pot);
    out->to_call_bucket = cfr_bucket_amount(to_call_player);
    out->active_players = cfr_count_in_hand(st);
    out->history_hash = cfr_history_key_from_state(st);
    return 1;
}

static void cfr_search_touch_reset(CFRBlueprint *bp)
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

static void cfr_search_touch_node(CFRBlueprint *bp, const CFRNode *node)
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

static int cfr_search_sample_action_index(const float *strategy, int n, uint64_t *rng)
{
    double r;
    double acc;
    int i;

    if (strategy == NULL || rng == NULL || n <= 0)
    {
        return 0;
    }

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

static int cfr_is_raise_action_code(int action)
{
    return action == CFR_ACT_RAISE_TO || action == CFR_ACT_ALL_IN || action == CFR_ACT_BET_HALF || action == CFR_ACT_BET_POT;
}

static int cfr_search_collect_injected_raise_targets(const CFRSearchFrozenAction *frozen,
                                                     int frozen_count,
                                                     int offtree_mode,
                                                     int *out_targets,
                                                     int out_cap)
{
    int i;
    int n;

    if (out_targets == NULL || out_cap <= 0)
    {
        return 0;
    }
    if (offtree_mode != CFR_OFFTREE_MODE_INJECT || frozen == NULL || frozen_count <= 0)
    {
        return 0;
    }

    n = 0;
    for (i = 0; i < frozen_count; ++i)
    {
        int target;
        int j;
        int seen;

        if (!cfr_is_raise_action_code(frozen[i].action))
        {
            continue;
        }
        target = frozen[i].target;
        if (target <= 0)
        {
            continue;
        }

        seen = 0;
        for (j = 0; j < n; ++j)
        {
            if (out_targets[j] == target)
            {
                seen = 1;
                break;
            }
        }
        if (seen)
        {
            continue;
        }
        if (n >= out_cap)
        {
            break;
        }
        out_targets[n++] = target;
    }
    return n;
}

static int cfr_search_limit_subgame_actions(int *actions, int *targets, int n)
{
    int keep[CFR_MAX_ACTIONS];
    int raise_idx[CFR_MAX_ACTIONS];
    int raise_n;
    int i;
    int k;
    int out_n;
    int out_actions[CFR_MAX_ACTIONS];
    int out_targets[CFR_MAX_ACTIONS];

    if (actions == NULL || targets == NULL || n <= 0)
    {
        return 0;
    }

    if (n > CFR_MAX_ACTIONS)
    {
        n = CFR_MAX_ACTIONS;
    }

    memset(keep, 0, sizeof(keep));
    raise_n = 0;
    for (i = 0; i < n; ++i)
    {
        if (cfr_is_raise_action_code(actions[i]))
        {
            if (raise_n < CFR_MAX_ACTIONS)
            {
                raise_idx[raise_n++] = i;
            }
        }
        else
        {
            keep[i] = 1;
        }
    }

    /* Paper-style subgame action abstraction keeps at most 5 raise branches. */
    if (raise_n <= 5)
    {
        for (i = 0; i < raise_n; ++i)
        {
            keep[raise_idx[i]] = 1;
        }
    }
    else
    {
        for (k = 0; k < 5; ++k)
        {
            int pos;
            int idx;
            pos = (int)((double)k * (double)(raise_n - 1) / 4.0 + 0.5);
            if (pos < 0)
            {
                pos = 0;
            }
            if (pos >= raise_n)
            {
                pos = raise_n - 1;
            }
            idx = raise_idx[pos];
            keep[idx] = 1;
        }
    }

    out_n = 0;
    for (i = 0; i < n; ++i)
    {
        if (keep[i] && out_n < CFR_MAX_ACTIONS)
        {
            out_actions[out_n] = actions[i];
            out_targets[out_n] = targets[i];
            out_n++;
        }
    }

    for (i = 0; i < out_n; ++i)
    {
        actions[i] = out_actions[i];
        targets[i] = out_targets[i];
    }

    return out_n;
}

static int cfr_search_ensure_injected_raise_support(const CFRHandState *st,
                                                    int *actions,
                                                    int *targets,
                                                    int n,
                                                    const int *injected_targets,
                                                    int injected_count)
{
    int p;
    int committed;
    int stack;
    int min_target;
    int max_target;
    int k;
    int i;

    if (st == NULL || actions == NULL || targets == NULL || n <= 0 ||
        injected_targets == NULL || injected_count <= 0)
    {
        return n;
    }
    if (n > CFR_MAX_ACTIONS)
    {
        n = CFR_MAX_ACTIONS;
    }

    p = st->current_player;
    if (p < 0 || p >= CFR_MAX_PLAYERS)
    {
        return n;
    }
    committed = st->committed_street[p];
    stack = st->stack[p];
    max_target = committed + stack;
    if (max_target <= st->to_call)
    {
        return n;
    }

    min_target = cfr_compute_min_raise_target(st, p);
    if (min_target > max_target)
    {
        min_target = max_target;
    }
    if (min_target <= st->to_call)
    {
        min_target = st->to_call + 1;
    }

    for (k = 0; k < injected_count; ++k)
    {
        int target;
        int exists;

        target = injected_targets[k];
        if (target < min_target || target > max_target)
        {
            continue;
        }

        exists = 0;
        for (i = 0; i < n; ++i)
        {
            if (cfr_is_raise_action_code(actions[i]) && targets[i] == target)
            {
                exists = 1;
                break;
            }
        }
        if (exists)
        {
            continue;
        }
        if (n >= CFR_MAX_ACTIONS)
        {
            break;
        }

        actions[n] = (target == max_target) ? CFR_ACT_ALL_IN : CFR_ACT_RAISE_TO;
        targets[n] = target;
        n++;
    }

    /* Keep raise actions ordered by target for deterministic action-index mapping. */
    for (i = 0; i < n; ++i)
    {
        int j;
        if (!cfr_is_raise_action_code(actions[i]))
        {
            continue;
        }
        for (j = i + 1; j < n; ++j)
        {
            if (!cfr_is_raise_action_code(actions[j]))
            {
                continue;
            }
            if (targets[j] < targets[i])
            {
                int ta;
                int tt;
                ta = actions[i];
                actions[i] = actions[j];
                actions[j] = ta;
                tt = targets[i];
                targets[i] = targets[j];
                targets[j] = tt;
            }
        }
    }

    return n;
}

static void cfr_search_bias_policy(float *policy, const int *actions, int n, int bias_mode)
{
    int i;
    float sum;
    int boosted;

    if (policy == NULL || actions == NULL || n <= 0)
    {
        return;
    }

    if (bias_mode <= 0)
    {
        return;
    }

    boosted = 0;
    for (i = 0; i < n; ++i)
    {
        if (bias_mode == 1 && actions[i] == CFR_ACT_FOLD)
        {
            policy[i] *= 5.0f;
            boosted = 1;
        }
        else if (bias_mode == 2 && actions[i] == CFR_ACT_CALL_CHECK)
        {
            policy[i] *= 5.0f;
            boosted = 1;
        }
        else if (bias_mode == 3 && cfr_is_raise_action_code(actions[i]))
        {
            policy[i] *= 5.0f;
            boosted = 1;
        }
    }

    if (!boosted)
    {
        return;
    }

    sum = 0.0f;
    for (i = 0; i < n; ++i)
    {
        if (policy[i] < 0.0f)
        {
            policy[i] = 0.0f;
        }
        sum += policy[i];
    }
    if (sum <= 1e-12f)
    {
        float p = 1.0f / (float)n;
        for (i = 0; i < n; ++i)
        {
            policy[i] = p;
        }
    }
    else
    {
        for (i = 0; i < n; ++i)
        {
            policy[i] /= sum;
        }
    }
}

static int cfr_search_pick_action_with_provider(CFRPolicyProvider *bp,
                                                CFRHandState *st,
                                                int player,
                                                int bias_mode,
                                                 uint64_t *rng,
                                                 int *out_action,
                                                 int *out_target)
{
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int legal_count;
    CFRInfoKeyFields kf;
    uint64_t key;
    float policy[CFR_MAX_ACTIONS];
    int picked;

    if (bp == NULL || st == NULL || out_action == NULL || out_target == NULL || rng == NULL)
    {
        return 0;
    }

    legal_count = cfr_get_legal_actions(st, player, legal_actions, legal_targets);
    if (legal_count <= 0)
    {
        return 0;
    }
    legal_count = cfr_search_limit_subgame_actions(legal_actions, legal_targets, legal_count);
    if (legal_count <= 0)
    {
        return 0;
    }
    if (!cfr_extract_infoset_fields_mode(st, player, CFR_ABS_MODE_BLUEPRINT, -1, &kf))
    {
        return 0;
    }
    key = cfr_make_infoset_key(&kf);
    if (!cfr_policy_provider_get_average_policy(bp, key, kf.street, legal_count, policy))
    {
        int i;
        float p = 1.0f / (float)legal_count;
        for (i = 0; i < legal_count; ++i)
        {
            policy[i] = p;
        }
    }
    cfr_search_bias_policy(policy, legal_actions, legal_count, bias_mode);
    picked = cfr_search_sample_action_index(policy, legal_count, rng);
    *out_action = legal_actions[picked];
    *out_target = legal_targets[picked];
    return 1;
}

static double cfr_search_rollout_value(CFRPolicyProvider *bp,
                                       CFRHandState *st,
                                       int traverser,
                                       int bias_mode,
                                       int depth_left,
                                       uint64_t *rng)
{
    if (st == NULL || bp == NULL || traverser < 0 || traverser >= CFR_MAX_PLAYERS)
    {
        return 0.0;
    }

    cfr_auto_advance_rounds_if_needed(st);
    if (st->is_terminal)
    {
        cfr_resolve_terminal(st);
        return (double)(st->stack[traverser] - CFR_START_STACK);
    }
    if (depth_left <= 0)
    {
        return (double)(st->stack[traverser] - CFR_START_STACK);
    }

    {
        int p;
        int action;
        int target;

        p = st->current_player;
        if (p < 0 || p >= CFR_MAX_PLAYERS)
        {
            return (double)(st->stack[traverser] - CFR_START_STACK);
        }

        if (!cfr_search_pick_action_with_provider(bp, st, p, bias_mode, rng, &action, &target))
        {
            return (double)(st->stack[traverser] - CFR_START_STACK);
        }

        cfr_apply_action(st, p, action, target);
        return cfr_search_rollout_value(bp, st, traverser, bias_mode, depth_left - 1, rng);
    }
}

static double cfr_search_continuation_leaf_value(CFRPolicyProvider *bp,
                                                 const CFRHandState *st,
                                                 int traverser,
                                                 uint64_t *rng)
{
    int k;
    double sum;

    if (bp == NULL || st == NULL || rng == NULL)
    {
        return 0.0;
    }

    sum = 0.0;
    for (k = 0; k < 4; ++k)
    {
        CFRHandState tmp;
        tmp = *st;
        sum += cfr_search_rollout_value(bp, &tmp, traverser, k, 8, rng);
    }
    return sum / 4.0;
}

static int cfr_search_pot_increase_step(const CFRHandState *before, const CFRHandState *after)
{
    if (before == NULL || after == NULL)
    {
        return 0;
    }
    return (after->pot > before->pot) ? 1 : 0;
}

static int cfr_search_should_stop_at_leaf(const CFRHandState *st,
                                          int depth,
                                          int max_depth,
                                          int root_street,
                                          int root_active_players,
                                          int pot_increase_count)
{
    if (st == NULL)
    {
        return 1;
    }
    if (depth >= max_depth)
    {
        return 1;
    }

    /* Preflop-rooted search: stop at next chance node (flop dealt). */
    if (root_street == 0)
    {
        return (st->street > 0) ? 1 : 0;
    }

    /* Multiway flop-rooted search: stop at next chance or second pot-increase action. */
    if (root_street == 1 && root_active_players > 2)
    {
        if (st->street > 1)
        {
            return 1;
        }
        if (pot_increase_count >= 2)
        {
            return 1;
        }
        return 0;
    }

    /* Later/smaller subgames continue to terminal unless depth guard triggers. */
    return 0;
}

static int cfr_search_frozen_pick(const CFRSearchFrozenAction *frozen,
                                  int frozen_count,
                                  int depth,
                                  int player,
                                  int *out_action,
                                  int *out_target)
{
    if (frozen == NULL || depth < 0 || depth >= frozen_count || out_action == NULL || out_target == NULL)
    {
        return 0;
    }
    if (frozen[depth].player != player)
    {
        return 0;
    }
    *out_action = frozen[depth].action;
    *out_target = frozen[depth].target;
    return 1;
}

static int cfr_search_map_observed_action(const CFRHandState *st,
                                          int player,
                                          int requested_action,
                                          int requested_target,
                                          int offtree_mode,
                                          uint64_t *rng,
                                          int *out_action,
                                          int *out_target,
                                          int *out_off_tree);

static int cfr_search_apply_observed_action(CFRHandState *st,
                                            int player,
                                            int requested_action,
                                            int requested_target,
                                            int offtree_mode,
                                            uint64_t *rng,
                                            int *out_off_tree);

static double cfr_search_traverse(CFRBlueprint *sub_bp,
                                  CFRPolicyProvider *blueprint,
                                  CFRHandState *st,
                                  int traverser,
                                  int depth,
                                  int max_depth,
                                  int root_street,
                                  int root_active_players,
                                  int pot_increase_count,
                                  double strategy_weight,
                                  int accumulate_strategy,
                                  uint64_t *rng,
                                  const CFRSearchFrozenAction *frozen,
                                  int frozen_count,
                                  const int *injected_targets,
                                  int injected_target_count,
                                  int offtree_mode)
{
    int p;
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int legal_count;
    CFRInfoKeyFields kf;
    uint64_t key;
    CFRNode *node;
    float strategy[CFR_MAX_ACTIONS];

    if (sub_bp == NULL || blueprint == NULL || st == NULL || rng == NULL)
    {
        return 0.0;
    }

    cfr_auto_advance_rounds_if_needed(st);
    if (st->is_terminal)
    {
        cfr_resolve_terminal(st);
        return (double)(st->stack[traverser] - CFR_START_STACK);
    }

    if (cfr_search_should_stop_at_leaf(st,
                                       depth,
                                       max_depth,
                                       root_street,
                                       root_active_players,
                                       pot_increase_count))
    {
        return cfr_search_continuation_leaf_value(blueprint, st, traverser, rng);
    }

    p = st->current_player;
    if (p < 0 || p >= CFR_MAX_PLAYERS)
    {
        return (double)(st->stack[traverser] - CFR_START_STACK);
    }

    {
        int frozen_action;
        int frozen_target;
        if (cfr_search_frozen_pick(frozen, frozen_count, depth, p, &frozen_action, &frozen_target))
        {
            CFRHandState child;
            int frozen_legal_actions[CFR_MAX_ACTIONS];
            int frozen_legal_targets[CFR_MAX_ACTIONS];
            int frozen_legal_count;

            /* AddAction-like materialization: include injected support in frozen-prefix nodes. */
            frozen_legal_count = cfr_get_legal_actions(st, p, frozen_legal_actions, frozen_legal_targets);
            if (frozen_legal_count > 0)
            {
                CFRInfoKeyFields frozen_kf;
                uint64_t frozen_key;
                CFRNode *frozen_node;

                frozen_legal_count = cfr_search_limit_subgame_actions(frozen_legal_actions,
                                                                      frozen_legal_targets,
                                                                      frozen_legal_count);
                frozen_legal_count = cfr_search_ensure_injected_raise_support(st,
                                                                               frozen_legal_actions,
                                                                               frozen_legal_targets,
                                                                               frozen_legal_count,
                                                                               injected_targets,
                                                                               injected_target_count);
                if (frozen_legal_count > 0 &&
                    cfr_extract_infoset_fields_mode(st, p, CFR_ABS_MODE_SEARCH, root_street, &frozen_kf))
                {
                    frozen_key = cfr_make_infoset_key(&frozen_kf);
                    frozen_node = cfr_blueprint_get_node(sub_bp, frozen_key, 1, frozen_legal_count);
                    if (frozen_node != NULL)
                    {
                        if (frozen_node->street_hint > 3u)
                        {
                            frozen_node->street_hint = (unsigned char)frozen_kf.street;
                        }
                        cfr_search_touch_node(sub_bp, frozen_node);
                    }
                }
            }

            child = *st;
            if (!cfr_search_apply_observed_action(&child,
                                                  p,
                                                  frozen_action,
                                                  frozen_target,
                                                  offtree_mode,
                                                  rng,
                                                  NULL))
            {
                return cfr_search_continuation_leaf_value(blueprint, st, traverser, rng);
            }
            return cfr_search_traverse(sub_bp,
                                       blueprint,
                                       &child,
                                       traverser,
                                       depth + 1,
                                       max_depth,
                                       root_street,
                                       root_active_players,
                                       pot_increase_count + cfr_search_pot_increase_step(st, &child),
                                       strategy_weight,
                                       accumulate_strategy,
                                       rng,
                                       frozen,
                                       frozen_count,
                                       injected_targets,
                                       injected_target_count,
                                       offtree_mode);
        }
    }

    legal_count = cfr_get_legal_actions(st, p, legal_actions, legal_targets);
    if (legal_count <= 0)
    {
        st->needs_action[p] = 0;
        st->current_player = cfr_find_next_actor_from(st, p);
        return cfr_search_traverse(sub_bp,
                                   blueprint,
                                   st,
                                   traverser,
                                   depth + 1,
                                   max_depth,
                                   root_street,
                                   root_active_players,
                                   pot_increase_count,
                                   strategy_weight,
                                   accumulate_strategy,
                                   rng,
                                   frozen,
                                   frozen_count,
                                   injected_targets,
                                   injected_target_count,
                                   offtree_mode);
    }

    legal_count = cfr_search_limit_subgame_actions(legal_actions, legal_targets, legal_count);
    legal_count = cfr_search_ensure_injected_raise_support(st,
                                                           legal_actions,
                                                           legal_targets,
                                                           legal_count,
                                                           injected_targets,
                                                           injected_target_count);
    if (legal_count <= 0)
    {
        return cfr_search_continuation_leaf_value(blueprint, st, traverser, rng);
    }

    if (!cfr_extract_infoset_fields_mode(st, p, CFR_ABS_MODE_SEARCH, root_street, &kf))
    {
        return cfr_search_continuation_leaf_value(blueprint, st, traverser, rng);
    }
    key = cfr_make_infoset_key(&kf);
    node = cfr_blueprint_get_node(sub_bp, key, 1, legal_count);
    if (node == NULL)
    {
        return cfr_search_continuation_leaf_value(blueprint, st, traverser, rng);
    }
    if (node->street_hint > 3u)
    {
        node->street_hint = (unsigned char)kf.street;
    }
    cfr_search_touch_node(sub_bp, node);

    cfr_compute_strategy_n(node, legal_count, strategy);

    if (p == traverser)
    {
        double util[CFR_MAX_ACTIONS];
        double node_util;
        int i;

        node_util = 0.0;
        for (i = 0; i < legal_count; ++i)
        {
            CFRHandState child;
            child = *st;
            cfr_apply_action(&child, p, legal_actions[i], legal_targets[i]);
            util[i] = cfr_search_traverse(sub_bp,
                                          blueprint,
                                          &child,
                                          traverser,
                                          depth + 1,
                                          max_depth,
                                          root_street,
                                          root_active_players,
                                          pot_increase_count + cfr_search_pot_increase_step(st, &child),
                                          strategy_weight,
                                          accumulate_strategy,
                                          rng,
                                          frozen,
                                          frozen_count,
                                          injected_targets,
                                          injected_target_count,
                                          offtree_mode);
            node_util += (double)strategy[i] * util[i];
        }

        node = cfr_blueprint_get_node(sub_bp, key, 1, legal_count);
        if (node == NULL)
        {
            return node_util;
        }
        for (i = 0; i < legal_count; ++i)
        {
            int32_t regret_delta;
            if ((util[i] - node_util) >= 0.0)
            {
                regret_delta = (int32_t)((util[i] - node_util) + 0.5);
            }
            else
            {
                regret_delta = (int32_t)((util[i] - node_util) - 0.5);
            }
            node->regret[i] = cfr_search_add_i32_clamped(node->regret[i], regret_delta);
            if (accumulate_strategy)
            {
                node->strategy_sum[i] += (float)((double)strategy[i] * strategy_weight);
            }
        }
        return node_util;
    }
    else
    {
        int sampled;
        CFRHandState child;
        int i;
        if (accumulate_strategy)
        {
            for (i = 0; i < legal_count; ++i)
            {
                node->strategy_sum[i] += (float)((double)strategy[i] * strategy_weight);
            }
        }
        sampled = cfr_search_sample_action_index(strategy, legal_count, rng);
        child = *st;
        cfr_apply_action(&child, p, legal_actions[sampled], legal_targets[sampled]);
        return cfr_search_traverse(sub_bp,
                                   blueprint,
                                   &child,
                                   traverser,
                                   depth + 1,
                                   max_depth,
                                   root_street,
                                   root_active_players,
                                   pot_increase_count + cfr_search_pot_increase_step(st, &child),
                                   strategy_weight,
                                   accumulate_strategy,
                                   rng,
                                   frozen,
                                   frozen_count,
                                   injected_targets,
                                   injected_target_count,
                                   offtree_mode);
    }
}

static void cfr_search_iteration(CFRBlueprint *sub_bp,
                                 CFRPolicyProvider *blueprint,
                                 const CFRHandState *root_state,
                                 int max_depth,
                                 int root_street,
                                 int root_active_players,
                                 uint64_t iteration_index,
                                 uint64_t *rng,
                                 const CFRSearchFrozenAction *frozen,
                                 int frozen_count,
                                 const int *injected_targets,
                                 int injected_target_count,
                                 int offtree_mode)
{
    int traverser;
    double strategy_weight;
    int accumulate_strategy;

    if (sub_bp == NULL || blueprint == NULL || root_state == NULL || rng == NULL)
    {
        return;
    }

    strategy_weight = (double)(iteration_index + 1ULL);
    accumulate_strategy = 1;

    for (traverser = 0; traverser < CFR_MAX_PLAYERS; ++traverser)
    {
        if (!root_state->in_hand[traverser])
        {
            continue;
        }
        {
            CFRHandState st;
            st = *root_state;
            (void)cfr_search_traverse(sub_bp,
                                      blueprint,
                                      &st,
                                      traverser,
                                      0,
                                      max_depth,
                                      root_street,
                                      root_active_players,
                                      0,
                                      strategy_weight,
                                      accumulate_strategy,
                                      rng,
                                      frozen,
                                      frozen_count,
                                      injected_targets,
                                      injected_target_count,
                                      offtree_mode);
        }
    }
}

static void cfr_search_fill_terminal_util(const CFRHandState *st, double out_util[CFR_MAX_PLAYERS])
{
    int p;
    for (p = 0; p < CFR_MAX_PLAYERS; ++p)
    {
        out_util[p] = (double)(st->stack[p] - CFR_START_STACK);
    }
}

static void cfr_search_fill_leaf_util_vector(CFRPolicyProvider *blueprint,
                                             const CFRHandState *st,
                                             uint64_t *rng,
                                             double out_util[CFR_MAX_PLAYERS])
{
    int p;
    for (p = 0; p < CFR_MAX_PLAYERS; ++p)
    {
        out_util[p] = cfr_search_continuation_leaf_value(blueprint, st, p, rng);
    }
}

static void cfr_search_traverse_vector(CFRBlueprint *sub_bp,
                                       CFRPolicyProvider *blueprint,
                                       CFRHandState *st,
                                       int depth,
                                       int max_depth,
                                       int root_street,
                                       int root_active_players,
                                       int pot_increase_count,
                                       double strategy_weight,
                                       int accumulate_strategy,
                                       uint64_t *rng,
                                       const CFRSearchFrozenAction *frozen,
                                       int frozen_count,
                                       const int *injected_targets,
                                       int injected_target_count,
                                       int offtree_mode,
                                       double out_util[CFR_MAX_PLAYERS])
{
    int p;
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int legal_count;
    CFRInfoKeyFields kf;
    uint64_t key;
    CFRNode *node;
    float strategy[CFR_MAX_ACTIONS];
    int i;

    if (sub_bp == NULL || blueprint == NULL || st == NULL || rng == NULL || out_util == NULL)
    {
        return;
    }

    cfr_auto_advance_rounds_if_needed(st);
    if (st->is_terminal)
    {
        cfr_resolve_terminal(st);
        cfr_search_fill_terminal_util(st, out_util);
        return;
    }

    if (cfr_search_should_stop_at_leaf(st,
                                       depth,
                                       max_depth,
                                       root_street,
                                       root_active_players,
                                       pot_increase_count))
    {
        cfr_search_fill_leaf_util_vector(blueprint, st, rng, out_util);
        return;
    }

    p = st->current_player;
    if (p < 0 || p >= CFR_MAX_PLAYERS)
    {
        cfr_search_fill_terminal_util(st, out_util);
        return;
    }

    {
        int frozen_action;
        int frozen_target;
        if (cfr_search_frozen_pick(frozen, frozen_count, depth, p, &frozen_action, &frozen_target))
        {
            CFRHandState child;
            int frozen_legal_actions[CFR_MAX_ACTIONS];
            int frozen_legal_targets[CFR_MAX_ACTIONS];
            int frozen_legal_count;

            frozen_legal_count = cfr_get_legal_actions(st, p, frozen_legal_actions, frozen_legal_targets);
            if (frozen_legal_count > 0)
            {
                CFRInfoKeyFields frozen_kf;
                uint64_t frozen_key;
                CFRNode *frozen_node;

                frozen_legal_count = cfr_search_limit_subgame_actions(frozen_legal_actions,
                                                                      frozen_legal_targets,
                                                                      frozen_legal_count);
                frozen_legal_count = cfr_search_ensure_injected_raise_support(st,
                                                                               frozen_legal_actions,
                                                                               frozen_legal_targets,
                                                                               frozen_legal_count,
                                                                               injected_targets,
                                                                               injected_target_count);
                if (frozen_legal_count > 0 &&
                    cfr_extract_infoset_fields_mode(st, p, CFR_ABS_MODE_SEARCH, root_street, &frozen_kf))
                {
                    frozen_key = cfr_make_infoset_key(&frozen_kf);
                    frozen_node = cfr_blueprint_get_node(sub_bp, frozen_key, 1, frozen_legal_count);
                    if (frozen_node != NULL)
                    {
                        if (frozen_node->street_hint > 3u)
                        {
                            frozen_node->street_hint = (unsigned char)frozen_kf.street;
                        }
                        cfr_search_touch_node(sub_bp, frozen_node);
                    }
                }
            }

            child = *st;
            if (!cfr_search_apply_observed_action(&child,
                                                  p,
                                                  frozen_action,
                                                  frozen_target,
                                                  offtree_mode,
                                                  rng,
                                                  NULL))
            {
                cfr_search_fill_leaf_util_vector(blueprint, st, rng, out_util);
                return;
            }
            cfr_search_traverse_vector(sub_bp,
                                       blueprint,
                                       &child,
                                       depth + 1,
                                       max_depth,
                                       root_street,
                                       root_active_players,
                                       pot_increase_count + cfr_search_pot_increase_step(st, &child),
                                       strategy_weight,
                                        accumulate_strategy,
                                        rng,
                                        frozen,
                                        frozen_count,
                                        injected_targets,
                                        injected_target_count,
                                        offtree_mode,
                                        out_util);
            return;
        }
    }

    legal_count = cfr_get_legal_actions(st, p, legal_actions, legal_targets);
    if (legal_count <= 0)
    {
        st->needs_action[p] = 0;
        st->current_player = cfr_find_next_actor_from(st, p);
        cfr_search_traverse_vector(sub_bp,
                                   blueprint,
                                   st,
                                   depth + 1,
                                   max_depth,
                                   root_street,
                                   root_active_players,
                                   pot_increase_count,
                                    strategy_weight,
                                    accumulate_strategy,
                                    rng,
                                    frozen,
                                    frozen_count,
                                    injected_targets,
                                    injected_target_count,
                                    offtree_mode,
                                    out_util);
        return;
    }

    legal_count = cfr_search_limit_subgame_actions(legal_actions, legal_targets, legal_count);
    legal_count = cfr_search_ensure_injected_raise_support(st,
                                                           legal_actions,
                                                           legal_targets,
                                                           legal_count,
                                                           injected_targets,
                                                           injected_target_count);
    if (legal_count <= 0)
    {
        cfr_search_fill_leaf_util_vector(blueprint, st, rng, out_util);
        return;
    }

    if (!cfr_extract_infoset_fields_mode(st, p, CFR_ABS_MODE_SEARCH, root_street, &kf))
    {
        cfr_search_fill_leaf_util_vector(blueprint, st, rng, out_util);
        return;
    }

    key = cfr_make_infoset_key(&kf);
    node = cfr_blueprint_get_node(sub_bp, key, 1, legal_count);
    if (node == NULL)
    {
        cfr_search_fill_leaf_util_vector(blueprint, st, rng, out_util);
        return;
    }

    if (node->street_hint > 3u)
    {
        node->street_hint = (unsigned char)kf.street;
    }
    cfr_search_touch_node(sub_bp, node);

    cfr_compute_strategy_n(node, legal_count, strategy);

    {
        double child_util[CFR_MAX_ACTIONS][CFR_MAX_PLAYERS];
        double node_util[CFR_MAX_PLAYERS];

        memset(child_util, 0, sizeof(child_util));
        memset(node_util, 0, sizeof(node_util));

        for (i = 0; i < legal_count; ++i)
        {
            CFRHandState child;

            child = *st;
            cfr_apply_action(&child, p, legal_actions[i], legal_targets[i]);
            cfr_search_traverse_vector(sub_bp,
                                       blueprint,
                                       &child,
                                       depth + 1,
                                       max_depth,
                                       root_street,
                                       root_active_players,
                                       pot_increase_count + cfr_search_pot_increase_step(st, &child),
                                       strategy_weight,
                                       accumulate_strategy,
                                       rng,
                                       frozen,
                                       frozen_count,
                                       injected_targets,
                                       injected_target_count,
                                       offtree_mode,
                                       child_util[i]);
            {
                int tp;
                for (tp = 0; tp < CFR_MAX_PLAYERS; ++tp)
                {
                    node_util[tp] += (double)strategy[i] * child_util[i][tp];
                }
            }
        }

        for (i = 0; i < CFR_MAX_PLAYERS; ++i)
        {
            out_util[i] = node_util[i];
        }

        node = cfr_blueprint_get_node(sub_bp, key, 1, legal_count);
        if (node == NULL)
        {
            return;
        }
        for (i = 0; i < legal_count; ++i)
        {
            int32_t regret_delta;
            if ((child_util[i][p] - node_util[p]) >= 0.0)
            {
                regret_delta = (int32_t)((child_util[i][p] - node_util[p]) + 0.5);
            }
            else
            {
                regret_delta = (int32_t)((child_util[i][p] - node_util[p]) - 0.5);
            }
            node->regret[i] = cfr_search_add_i32_clamped(node->regret[i], regret_delta);
            if (accumulate_strategy)
            {
                node->strategy_sum[i] += (float)((double)strategy[i] * strategy_weight);
            }
        }
    }
}

static void cfr_search_iteration_vector(CFRBlueprint *sub_bp,
                                        CFRPolicyProvider *blueprint,
                                        const CFRHandState *root_state,
                                        int max_depth,
                                        int root_street,
                                        int root_active_players,
                                        uint64_t iteration_index,
                                        uint64_t *rng,
                                        const CFRSearchFrozenAction *frozen,
                                        int frozen_count,
                                        const int *injected_targets,
                                        int injected_target_count,
                                        int offtree_mode)
{
    CFRHandState st;
    double util[CFR_MAX_PLAYERS];
    double strategy_weight;
    int accumulate_strategy;

    if (sub_bp == NULL || blueprint == NULL || root_state == NULL || rng == NULL)
    {
        return;
    }

    st = *root_state;
    strategy_weight = (double)(iteration_index + 1ULL) * (double)cfr_count_in_hand(root_state);
    accumulate_strategy = 1;
    memset(util, 0, sizeof(util));

    cfr_search_traverse_vector(sub_bp,
                               blueprint,
                               &st,
                               0,
                               max_depth,
                               root_street,
                               root_active_players,
                               0,
                               strategy_weight,
                               accumulate_strategy,
                               rng,
                               frozen,
                               frozen_count,
                               injected_targets,
                               injected_target_count,
                               offtree_mode,
                               util);
}

static int cfr_search_should_use_vector(const CFRHandState *root_state,
                                        int max_depth,
                                        int observed_count,
                                        int root_active_players,
                                        int root_legal_count,
                                        int root_raise_count)
{
    if (root_state == NULL)
    {
        return 0;
    }
    if (max_depth < 2)
    {
        return 0;
    }

    /*
     * Deterministic size-aware switch:
     * - large / early-round subgames stay on scalar external-sampling path
     * - smaller / later-round subgames use vector path
     */
    if (root_state->street >= 2)
    {
        return 1;
    }
    if (root_active_players >= 4)
    {
        return 0;
    }
    if (root_state->street == 0)
    {
        if (root_raise_count >= 5 || root_legal_count >= 8)
        {
            return 0;
        }
        if (observed_count <= 0)
        {
            return 0;
        }
        if (max_depth >= 4 && root_legal_count >= 6)
        {
            return 0;
        }
        return 1;
    }

    /* Flop-rooted subgames: keep scalar on larger multiway starts. */
    if (root_active_players >= 3 && (root_legal_count >= 7 || max_depth >= 4))
    {
        return 0;
    }
    if (observed_count <= 0 && root_legal_count >= 6)
    {
        return 0;
    }
    return 1;
}

static int cfr_search_filter_frozen_for_player(const CFRSearchFrozenAction *frozen,
                                               int frozen_count,
                                               int player,
                                               CFRSearchFrozenAction *out,
                                               int out_cap)
{
    int i;
    int n;

    if (frozen == NULL || frozen_count <= 0 || out == NULL || out_cap <= 0)
    {
        return 0;
    }

    n = 0;
    for (i = 0; i < frozen_count; ++i)
    {
        if (frozen[i].player != player)
        {
            continue;
        }
        if (n >= out_cap)
        {
            break;
        }
        out[n++] = frozen[i];
    }
    return n;
}

typedef struct
{
    CFRBlueprint *local_bp;
    const CFRBlueprint *base_bp;
    CFRPolicyProvider blueprint;
    CFRHandState root_state;
    int max_depth;
    int root_street;
    int root_active_players;
    int use_vector;
    const CFRSearchFrozenAction *frozen;
    int frozen_count;
    const int *injected_targets;
    int injected_target_count;
    int offtree_mode;
    uint64_t iter_start;
    uint64_t iter_count;
    int worker_id;
    uint64_t seed;
} CFRSearchWorkerContext;

static void cfr_search_merge_worker_delta(CFRBlueprint *dst, CFRBlueprint *base, const CFRBlueprint *worker)
{
    uint32_t i;

    if (dst == NULL || base == NULL || worker == NULL)
    {
        return;
    }

    for (i = 0; i < worker->touched_count; ++i)
    {
        uint32_t idx;
        const CFRNode *w;
        CFRNode *g;
        CFRNode *b;
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

        g = cfr_blueprint_get_node(dst, w->key, 1, w->action_count);
        if (g == NULL)
        {
            continue;
        }
        b = cfr_blueprint_get_node(base, w->key, 0, w->action_count);
        if (g->street_hint > 3u)
        {
            g->street_hint = w->street_hint;
        }

        for (a = 0; a < g->action_count; ++a)
        {
            int32_t base_regret;
            float base_sum;

            if (b != NULL && a < b->action_count)
            {
                base_regret = b->regret[a];
                base_sum = b->strategy_sum[a];
            }
            else
            {
                base_regret = 0;
                base_sum = 0.0f;
            }
            g->regret[a] = cfr_search_add_i32_clamped(g->regret[a], (int32_t)(w->regret[a] - base_regret));
            g->strategy_sum[a] += (w->strategy_sum[a] - base_sum);
        }
    }
}

#ifdef _WIN32
static DWORD WINAPI cfr_search_worker_proc(LPVOID param)
{
    CFRSearchWorkerContext *ctx;
    uint64_t i;
    uint64_t rng;

    ctx = (CFRSearchWorkerContext *)param;
    if (ctx == NULL || ctx->local_bp == NULL || ctx->base_bp == NULL)
    {
        return 1;
    }

    if (!cfr_blueprint_copy_from(ctx->local_bp, ctx->base_bp))
    {
        return 1;
    }
    cfr_search_touch_reset(ctx->local_bp);
    rng = ctx->seed;

    for (i = 0; i < ctx->iter_count; ++i)
    {
        if (ctx->use_vector)
        {
            cfr_search_iteration_vector(ctx->local_bp,
                                        &ctx->blueprint,
                                        &ctx->root_state,
                                        ctx->max_depth,
                                        ctx->root_street,
                                        ctx->root_active_players,
                                        ctx->iter_start + i,
                                        &rng,
                                        ctx->frozen,
                                        ctx->frozen_count,
                                        ctx->injected_targets,
                                        ctx->injected_target_count,
                                        ctx->offtree_mode);
        }
        else
        {
            cfr_search_iteration(ctx->local_bp,
                                 &ctx->blueprint,
                                 &ctx->root_state,
                                 ctx->max_depth,
                                 ctx->root_street,
                                 ctx->root_active_players,
                                 ctx->iter_start + i,
                                 &rng,
                                 ctx->frozen,
                                 ctx->frozen_count,
                                 ctx->injected_targets,
                                 ctx->injected_target_count,
                                 ctx->offtree_mode);
        }
    }

    ctx->local_bp->rng_state = rng;
    return 0;
}

static int cfr_search_run_iterations_parallel(CFRBlueprint *sub_bp,
                                              CFRPolicyProvider *blueprint,
                                              const CFRHandState *root_state,
                                              uint64_t iterations,
                                              int max_depth,
                                              int root_street,
                                              int root_active_players,
                                              int use_vector,
                                              int threads,
                                              uint64_t seed,
                                              const CFRSearchFrozenAction *frozen,
                                              int frozen_count,
                                              const int *injected_targets,
                                              int injected_target_count,
                                              int offtree_mode)
{
    CFRBlueprint *base_bp;
    CFRSearchWorkerContext *ctx;
    HANDLE *handles;
    int workers;
    int w;
    uint64_t per_worker;
    uint64_t remainder;
    uint64_t offset;

    if (sub_bp == NULL || blueprint == NULL || root_state == NULL || iterations == 0ULL || threads < 2)
    {
        return 0;
    }

    workers = threads;
    if (workers > 32)
    {
        workers = 32;
    }
    if ((uint64_t)workers > iterations)
    {
        workers = (int)iterations;
    }

    base_bp = (CFRBlueprint *)malloc(sizeof(*base_bp));
    ctx = (CFRSearchWorkerContext *)calloc((size_t)workers, sizeof(*ctx));
    handles = (HANDLE *)calloc((size_t)workers, sizeof(*handles));
    if (base_bp == NULL || ctx == NULL || handles == NULL)
    {
        free(base_bp);
        free(ctx);
        free(handles);
        return 0;
    }

    memset(base_bp, 0, sizeof(*base_bp));
    if (!cfr_blueprint_copy_from(base_bp, sub_bp))
    {
        free(base_bp);
        free(ctx);
        free(handles);
        return 0;
    }
    per_worker = iterations / (uint64_t)workers;
    remainder = iterations % (uint64_t)workers;
    offset = 0ULL;

    for (w = 0; w < workers; ++w)
    {
        uint64_t share;

        share = per_worker + ((uint64_t)w < remainder ? 1ULL : 0ULL);
        ctx[w].local_bp = (CFRBlueprint *)malloc(sizeof(CFRBlueprint));
        if (ctx[w].local_bp == NULL)
        {
            int j;
            for (j = 0; j < w; ++j)
            {
                WaitForSingleObject(handles[j], INFINITE);
                CloseHandle(handles[j]);
                cfr_blueprint_release(ctx[j].local_bp);
                free(ctx[j].local_bp);
            }
            cfr_blueprint_release(base_bp);
            free(base_bp);
            free(ctx);
            free(handles);
            return 0;
        }
        memset(ctx[w].local_bp, 0, sizeof(*ctx[w].local_bp));
        if (!cfr_blueprint_init(ctx[w].local_bp, seed ^ ((uint64_t)(w + 1) * 0x9E3779B97F4A7C15ULL)))
        {
            int j;
            for (j = 0; j < w; ++j)
            {
                WaitForSingleObject(handles[j], INFINITE);
                CloseHandle(handles[j]);
                cfr_blueprint_release(ctx[j].local_bp);
                free(ctx[j].local_bp);
            }
            cfr_blueprint_release(ctx[w].local_bp);
            free(ctx[w].local_bp);
            cfr_blueprint_release(base_bp);
            free(base_bp);
            free(ctx);
            free(handles);
            return 0;
        }

        ctx[w].base_bp = base_bp;
        ctx[w].blueprint = *blueprint;
        ctx[w].blueprint.use_runtime_cache = 0;
        ctx[w].root_state = *root_state;
        ctx[w].max_depth = max_depth;
        ctx[w].root_street = root_street;
        ctx[w].root_active_players = root_active_players;
        ctx[w].use_vector = use_vector;
        ctx[w].frozen = frozen;
        ctx[w].frozen_count = frozen_count;
        ctx[w].injected_targets = injected_targets;
        ctx[w].injected_target_count = injected_target_count;
        ctx[w].offtree_mode = offtree_mode;
        ctx[w].iter_start = offset;
        ctx[w].iter_count = share;
        ctx[w].worker_id = w;
        ctx[w].seed = seed ^ ((uint64_t)(w + 1) * 0x9E3779B97F4A7C15ULL);

        handles[w] = CreateThread(NULL, 0, cfr_search_worker_proc, &ctx[w], 0, NULL);
        if (handles[w] == NULL)
        {
            int j;
            for (j = 0; j < w; ++j)
            {
                WaitForSingleObject(handles[j], INFINITE);
                CloseHandle(handles[j]);
                cfr_blueprint_release(ctx[j].local_bp);
                free(ctx[j].local_bp);
            }
            cfr_blueprint_release(ctx[w].local_bp);
            free(ctx[w].local_bp);
            cfr_blueprint_release(base_bp);
            free(base_bp);
            free(ctx);
            free(handles);
            return 0;
        }

        offset += share;
    }

    WaitForMultipleObjects((DWORD)workers, handles, TRUE, INFINITE);
    if (!cfr_blueprint_copy_from(sub_bp, base_bp))
    {
        for (w = 0; w < workers; ++w)
        {
            CloseHandle(handles[w]);
            cfr_blueprint_release(ctx[w].local_bp);
            free(ctx[w].local_bp);
        }
        cfr_blueprint_release(base_bp);
        free(base_bp);
        free(ctx);
        free(handles);
        return 0;
    }
    for (w = 0; w < workers; ++w)
    {
        cfr_search_merge_worker_delta(sub_bp, base_bp, ctx[w].local_bp);
    }

    for (w = 0; w < workers; ++w)
    {
        CloseHandle(handles[w]);
        cfr_blueprint_release(ctx[w].local_bp);
        free(ctx[w].local_bp);
    }
    cfr_blueprint_release(base_bp);
    free(base_bp);
    free(ctx);
    free(handles);
    return 1;
}
#endif

static void cfr_search_run_iterations(CFRBlueprint *sub_bp,
                                      CFRPolicyProvider *blueprint,
                                      const CFRHandState *root_state,
                                      uint64_t iterations,
                                      int max_depth,
                                      int root_street,
                                      int root_active_players,
                                      int use_vector,
                                      int threads,
                                      uint64_t seed,
                                      const CFRSearchFrozenAction *frozen,
                                      int frozen_count,
                                      const int *injected_targets,
                                      int injected_target_count,
                                      int offtree_mode)
{
    uint64_t i;
    uint64_t rng;

    if (sub_bp == NULL || blueprint == NULL || root_state == NULL)
    {
        return;
    }
    if (iterations == 0ULL)
    {
        return;
    }

#ifdef _WIN32
    if (threads > 1 &&
        cfr_search_run_iterations_parallel(sub_bp,
                                           blueprint,
                                           root_state,
                                           iterations,
                                           max_depth,
                                           root_street,
                                           root_active_players,
                                           use_vector,
                                           threads,
                                           seed,
                                           frozen,
                                           frozen_count,
                                           injected_targets,
                                           injected_target_count,
                                           offtree_mode))
    {
        return;
    }
#else
    (void)threads;
#endif

    rng = seed;
    cfr_search_touch_reset(sub_bp);
    for (i = 0; i < iterations; ++i)
    {
        if (use_vector)
        {
            cfr_search_iteration_vector(sub_bp,
                                        blueprint,
                                        root_state,
                                        max_depth,
                                        root_street,
                                        root_active_players,
                                        i,
                                        &rng,
                                        frozen,
                                        frozen_count,
                                        injected_targets,
                                        injected_target_count,
                                        offtree_mode);
        }
        else
        {
            cfr_search_iteration(sub_bp,
                                 blueprint,
                                 root_state,
                                 max_depth,
                                 root_street,
                                 root_active_players,
                                 i,
                                 &rng,
                                 frozen,
                                 frozen_count,
                                 injected_targets,
                                 injected_target_count,
                                 offtree_mode);
        }
    }
    sub_bp->rng_state = rng;
}

static void cfr_search_context_init(CFRSearchContext *ctx, uint64_t seed)
{
    if (ctx == NULL)
    {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    cfr_blueprint_init(&ctx->subgame_bp, seed);
    ctx->root_street = -1;
    ctx->root_history_hash = 0u;
    ctx->initialized = 0;
    ctx->seed = seed;
}

static void cfr_search_context_prepare_nested(CFRSearchContext *ctx, const CFRHandState *state)
{
    uint32_t hist_hash;

    if (ctx == NULL || state == NULL)
    {
        return;
    }
    hist_hash = cfr_history_key_from_state(state);
    if (!ctx->initialized || ctx->root_street != state->street || ctx->root_history_hash != hist_hash)
    {
        cfr_blueprint_init(&ctx->subgame_bp, ctx->seed);
        ctx->root_street = state->street;
        ctx->root_history_hash = hist_hash;
        ctx->initialized = 1;
    }
}

static int cfr_search_apply_observed_action(CFRHandState *st,
                                            int player,
                                            int requested_action,
                                            int requested_target,
                                            int offtree_mode,
                                            uint64_t *rng,
                                            int *out_off_tree)
{
    int action;
    int target;
    int off_tree;

    if (st == NULL)
    {
        return 0;
    }
    if (!cfr_search_map_observed_action(st,
                                        player,
                                        requested_action,
                                        requested_target,
                                        offtree_mode,
                                        rng,
                                        &action,
                                        &target,
                                        &off_tree))
    {
        return 0;
    }
    if (out_off_tree != NULL)
    {
        *out_off_tree = off_tree;
    }
    return cfr_apply_action(st, player, action, target);
}

static int cfr_search_map_observed_action(const CFRHandState *st,
                                          int player,
                                          int requested_action,
                                          int requested_target,
                                          int offtree_mode,
                                          uint64_t *rng,
                                          int *out_action,
                                          int *out_target,
                                          int *out_off_tree)
{
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int legal_count;
    int i;
    int min_raise_target;
    int max_raise_target;
    int saw_raise;

    if (st == NULL || out_action == NULL || out_target == NULL)
    {
        return 0;
    }
    legal_count = cfr_get_legal_actions(st, player, legal_actions, legal_targets);
    if (legal_count <= 0)
    {
        return 0;
    }

    min_raise_target = 0;
    max_raise_target = 0;
    saw_raise = 0;
    for (i = 0; i < legal_count; ++i)
    {
        if (!cfr_is_raise_action_code(legal_actions[i]))
        {
            continue;
        }
        if (!saw_raise)
        {
            min_raise_target = legal_targets[i];
            max_raise_target = legal_targets[i];
            saw_raise = 1;
        }
        else
        {
            if (legal_targets[i] < min_raise_target)
            {
                min_raise_target = legal_targets[i];
            }
            if (legal_targets[i] > max_raise_target)
            {
                max_raise_target = legal_targets[i];
            }
        }
    }

    for (i = 0; i < legal_count; ++i)
    {
        if (legal_actions[i] == requested_action && legal_targets[i] == requested_target)
        {
            *out_action = requested_action;
            *out_target = requested_target;
            if (out_off_tree != NULL) *out_off_tree = 0;
            return 1;
        }
    }

    if (offtree_mode == CFR_OFFTREE_MODE_TRANSLATE)
    {
        if (!cfr_translate_action_to_legal_pseudo_harmonic(st,
                                                            player,
                                                            requested_action,
                                                            requested_target,
                                                            rng,
                                                            out_action,
                                                            out_target,
                                                            out_off_tree))
        {
            return 0;
        }
        return 1;
    }

    if (offtree_mode == CFR_OFFTREE_MODE_INJECT &&
        cfr_is_raise_action_code(requested_action) &&
        saw_raise &&
        requested_target >= min_raise_target &&
        requested_target <= max_raise_target &&
        requested_target > st->to_call)
    {
        *out_action = (requested_target == max_raise_target) ? CFR_ACT_ALL_IN : CFR_ACT_RAISE_TO;
        *out_target = requested_target;
        if (out_off_tree != NULL)
        {
            *out_off_tree = 1;
        }
        return 1;
    }
    return 0;
}

static int cfr_search_update_belief_from_frozen(CFRPolicyProvider *blueprint,
                                                const CFRBlueprint *search_sigma,
                                                const CFRHandState *round_root_state,
                                                int observer_player,
                                                const CFRSearchFrozenAction *frozen,
                                                int frozen_count,
                                                int root_street,
                                                int offtree_mode,
                                                CFRBeliefState *out_belief,
                                                int *out_updates)
{
    CFRBeliefState belief;
    CFRHandState sim;
    int blockers[CFR_MAX_BOARD + 2];
    int blocker_n;
    int p;
    int updates;
    uint64_t map_rng;
    int prev_street;
    int i;

    if (blueprint == NULL || round_root_state == NULL || out_belief == NULL || out_updates == NULL)
    {
        return 0;
    }

    cfr_belief_init(&belief);
    blocker_n = cfr_belief_collect_round_root_blockers(round_root_state,
                                                       observer_player,
                                                       blockers,
                                                       (int)(sizeof(blockers) / sizeof(blockers[0])));
    for (p = 0; p < CFR_MAX_PLAYERS; ++p)
    {
        if (p == observer_player || !round_root_state->in_hand[p])
        {
            continue;
        }
        if (!cfr_belief_init_player_uniform(&belief, p, blockers, blocker_n))
        {
            return 0;
        }
    }

    sim = *round_root_state;
    updates = 0;
    map_rng = 0xA55A5AA55A5AA55AULL;

    for (i = 0; i < frozen_count; ++i)
    {
        int mapped_action;
        int mapped_target;
        int off_tree;

        if (sim.is_terminal)
        {
            break;
        }
        if (frozen[i].player < 0 || frozen[i].player >= CFR_MAX_PLAYERS)
        {
            continue;
        }
        if (frozen[i].player != sim.current_player)
        {
            break;
        }

        if (!cfr_search_map_observed_action(&sim,
                                            frozen[i].player,
                                            frozen[i].action,
                                            frozen[i].target,
                                            offtree_mode,
                                            &map_rng,
                                            &mapped_action,
                                            &mapped_target,
                                            &off_tree))
        {
            break;
        }
        (void)off_tree;

        if (frozen[i].player != observer_player)
        {
            blocker_n = cfr_belief_collect_round_root_blockers(&sim,
                                                               observer_player,
                                                               blockers,
                                                               (int)(sizeof(blockers) / sizeof(blockers[0])));
            if (cfr_belief_update_player_action(&belief,
                                                blueprint,
                                                search_sigma,
                                                root_street,
                                                &sim,
                                                frozen[i].player,
                                                mapped_action,
                                                mapped_target,
                                                blockers,
                                                blocker_n))
            {
                updates++;
            }
        }

        prev_street = sim.street;
        if (!cfr_apply_action(&sim, frozen[i].player, mapped_action, mapped_target))
        {
            break;
        }
        if (!sim.is_terminal && sim.street != prev_street)
        {
            updates += cfr_belief_apply_round_root_blockers(&belief, &sim, observer_player);
        }
    }

    if (!sim.is_terminal)
    {
        updates += cfr_belief_apply_round_root_blockers(&belief, &sim, observer_player);
    }

    *out_belief = belief;
    *out_updates = updates;
    return 1;
}

static int cfr_search_decide(CFRSearchContext *ctx,
                             CFRPolicyProvider *blueprint,
                             const CFRHandState *state,
                             int player,
                             uint64_t iterations,
                             uint64_t time_budget_ms,
                             int depth,
                             int threads,
                             int search_pick_mode,
                             int offtree_mode,
                             const CFRSearchFrozenAction *frozen,
                             int frozen_count,
                             CFRSearchDecision *out)
{
    CFRHandState root_state;
    CFRHandState decision_state;
    CFRSearchFrozenAction own_frozen[CFR_MAX_HISTORY];
    const CFRSearchFrozenAction *observed_frozen;
    int legal_count;
    int root_legal_count;
    int root_raise_count;
    int own_frozen_count;
    int observed_count;
    int root_legal_actions[CFR_MAX_ACTIONS];
    int root_legal_targets[CFR_MAX_ACTIONS];
    CFRInfoKeyFields kf;
    uint64_t key;
    CFRNode *node;
    int use_vector;
    int i;
    int best_i;
    float best_p;
    uint64_t pick_rng;
    uint64_t replay_rng;
    uint64_t done_iterations;
    uint64_t batch_iters;
    double solve_start_sec;
    int strict_time_budget;
    int injected_targets[CFR_MAX_HISTORY];
    int injected_target_count;
    CFRBeliefState belief;
    int belief_updates;
    int root_active_players;

    if (ctx == NULL || blueprint == NULL || state == NULL || out == NULL)
    {
        return 0;
    }
    if (iterations == 0ULL)
    {
        iterations = 1ULL;
    }
    if (depth < 1)
    {
        depth = 1;
    }
    if (frozen_count < 0)
    {
        frozen_count = 0;
    }
    observed_frozen = frozen;
    observed_count = frozen_count;
    own_frozen_count = cfr_search_filter_frozen_for_player(frozen,
                                                           frozen_count,
                                                           player,
                                                           own_frozen,
                                                           (int)(sizeof(own_frozen) / sizeof(own_frozen[0])));

    root_state = *state;
    cfr_auto_advance_rounds_if_needed(&root_state);
    injected_target_count = cfr_search_collect_injected_raise_targets(observed_frozen,
                                                                      observed_count,
                                                                      offtree_mode,
                                                                      injected_targets,
                                                                      (int)(sizeof(injected_targets) / sizeof(injected_targets[0])));

    cfr_search_context_prepare_nested(ctx, &root_state);
    belief_updates = 0;
    if (observed_count > 0)
    {
        (void)cfr_search_update_belief_from_frozen(blueprint,
                                                   &ctx->subgame_bp,
                                                   &root_state,
                                                   player,
                                                   observed_frozen,
                                                   observed_count,
                                                   root_state.street,
                                                   offtree_mode,
                                                   &belief,
                                                   &belief_updates);
    }

    root_legal_count = cfr_get_legal_actions(&root_state,
                                             root_state.current_player,
                                             root_legal_actions,
                                             root_legal_targets);
    root_raise_count = 0;
    if (root_legal_count > 0)
    {
        for (i = 0; i < root_legal_count; ++i)
        {
            if (cfr_is_raise_action_code(root_legal_actions[i]))
            {
                root_raise_count++;
            }
        }
    }
    root_active_players = cfr_count_in_hand(&root_state);
    use_vector = cfr_search_should_use_vector(&root_state,
                                              depth,
                                              observed_count,
                                              root_active_players,
                                              root_legal_count,
                                              root_raise_count);
    done_iterations = 0ULL;
    strict_time_budget = (time_budget_ms > 0ULL) ? 1 : 0;
    if (strict_time_budget)
    {
        /* Under wall-clock budget, run 1 iteration per chunk to cap overshoot. */
        batch_iters = 1ULL;
    }
    else
    {
        batch_iters = (threads > 1) ? (uint64_t)threads * 2ULL : 8ULL;
    }
    if (batch_iters == 0ULL)
    {
        batch_iters = 1ULL;
    }
    solve_start_sec = cfr_wall_seconds();
    while (done_iterations < iterations)
    {
        uint64_t cur;
        uint64_t chunk_seed;

        if (strict_time_budget)
        {
            double elapsed_ms;
            elapsed_ms = (cfr_wall_seconds() - solve_start_sec) * 1000.0;
            if (elapsed_ms >= (double)time_budget_ms)
            {
                break;
            }
        }

        cur = iterations - done_iterations;
        if (cur > batch_iters)
        {
            cur = batch_iters;
        }
        if (cur == 0ULL)
        {
            break;
        }

        chunk_seed = ctx->seed ^ ((done_iterations + 1ULL) * 0x9E3779B97F4A7C15ULL);
        cfr_search_run_iterations(&ctx->subgame_bp,
                                  blueprint,
                                  &root_state,
                                  cur,
                                  depth,
                                  root_state.street,
                                  root_active_players,
                                  use_vector,
                                  threads,
                                  chunk_seed,
                                  own_frozen,
                                  own_frozen_count,
                                  injected_targets,
                                  injected_target_count,
                                  offtree_mode);
        done_iterations += cur;

        if (strict_time_budget)
        {
            double elapsed_ms;

            elapsed_ms = (cfr_wall_seconds() - solve_start_sec) * 1000.0;
            if (elapsed_ms >= (double)time_budget_ms)
            {
                break;
            }
        }
    }

    decision_state = root_state;
    replay_rng = ctx->seed ^ 0xBADC0FFEE0DDF00DULL;
    for (i = 0; i < observed_count; ++i)
    {
        if (decision_state.is_terminal)
        {
            return 0;
        }
        if (decision_state.current_player != observed_frozen[i].player)
        {
            return 0;
        }
        if (!cfr_search_apply_observed_action(&decision_state,
                                              observed_frozen[i].player,
                                              observed_frozen[i].action,
                                              observed_frozen[i].target,
                                              offtree_mode,
                                              &replay_rng,
                                              NULL))
        {
            return 0;
        }
    }
    cfr_auto_advance_rounds_if_needed(&decision_state);
    if (decision_state.is_terminal)
    {
        return 0;
    }
    if (decision_state.current_player != player)
    {
        return 0;
    }

    legal_count = cfr_get_legal_actions(&decision_state, player, out->legal_actions, out->legal_targets);
    if (legal_count <= 0)
    {
        return 0;
    }
    legal_count = cfr_search_limit_subgame_actions(out->legal_actions, out->legal_targets, legal_count);
    legal_count = cfr_search_ensure_injected_raise_support(&decision_state,
                                                           out->legal_actions,
                                                           out->legal_targets,
                                                           legal_count,
                                                           injected_targets,
                                                           injected_target_count);
    if (legal_count <= 0)
    {
        return 0;
    }

    if (!cfr_extract_infoset_fields_mode(&decision_state, player, CFR_ABS_MODE_SEARCH, root_state.street, &kf))
    {
        return 0;
    }
    key = cfr_make_infoset_key(&kf);
    node = cfr_blueprint_get_node(&ctx->subgame_bp, key, 0, legal_count);

    out->legal_count = legal_count;
    out->iterations_done = done_iterations;
    out->belief_updates = belief_updates;
    if (node == NULL)
    {
        float p;
        p = 1.0f / (float)legal_count;
        for (i = 0; i < legal_count; ++i)
        {
            out->final_policy[i] = p;
            out->avg_policy[i] = p;
        }
    }
    else
    {
        cfr_compute_strategy_n(node, legal_count, out->final_policy);
        cfr_compute_average_strategy_n(node, legal_count, out->avg_policy);
    }

    if (search_pick_mode == CFR_SEARCH_PICK_ARGMAX)
    {
        best_i = 0;
        best_p = out->final_policy[0];
        for (i = 1; i < legal_count; ++i)
        {
            if (out->final_policy[i] > best_p)
            {
                best_i = i;
                best_p = out->final_policy[i];
            }
        }
    }
    else
    {
        pick_rng = ctx->seed ^ key ^ (iterations * 0x9E3779B97F4A7C15ULL);
        best_i = cfr_search_sample_action_index(out->final_policy, legal_count, &pick_rng);
    }
    out->chosen_index = best_i;
    return 1;
}

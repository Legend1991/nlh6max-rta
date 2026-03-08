static int cfr_blueprint_save(const CFRBlueprint *bp, const char *path)
{
    FILE *f;
    CFRBlueprintFileHeader h;
    uint32_t i;

    if (bp == NULL || path == NULL)
    {
        return 0;
    }

    cfr_blueprint_materialize_all((CFRBlueprint *)bp);

    f = fopen(path, "wb");
    if (f == NULL)
    {
        return 0;
    }

    memset(&h, 0, sizeof(h));
    h.magic = CFR_BLUEPRINT_MAGIC;
    h.version = CFR_BLUEPRINT_VERSION;
    h.max_actions = CFR_MAX_ACTIONS;
    h.iteration = bp->iteration;
    h.total_hands = bp->total_hands;
    h.rng_state = bp->rng_state;
    h.node_count = (uint32_t)cfr_count_used_nodes(bp);
    h.compat_hash32 = bp->compat_hash32;
    h.abstraction_hash32 = bp->abstraction_hash32;
    h.elapsed_train_seconds = bp->elapsed_train_seconds;
    h.phase_flags = bp->phase_flags;
    h.next_discount_second = bp->next_discount_second;
    h.next_snapshot_second = bp->next_snapshot_second;
    h.discount_events_applied = bp->discount_events_applied;
    h.storage_flags = bp->omit_postflop_strategy_sum ? 1u : 0u;
    h.reserved0 = 0u;
    h.regret_actions_total = (uint64_t)bp->regret_used;
    h.strategy_actions_total = (uint64_t)bp->strategy_used;

    if (fwrite(&h, sizeof(h), 1, f) != 1)
    {
        fclose(f);
        return 0;
    }

    for (i = 0u; i < bp->used_node_count; ++i)
    {
        if (cfr_node_is_used(&bp->nodes[i]))
        {
            CFRBlueprintDiskNodeHeader dn;
            uint32_t count;

            memset(&dn, 0, sizeof(dn));
            dn.key = bp->nodes[i].key;
            dn.action_count = (uint32_t)bp->nodes[i].action_count;
            dn.street_hint = bp->nodes[i].street_hint;
            dn.payload_flags = cfr_blueprint_node_has_strategy_payload(&bp->nodes[i]) ? CFR_NODE_FLAG_HAS_STRATEGY : 0u;
            count = dn.action_count;

            if (fwrite(&dn, sizeof(dn), 1, f) != 1)
            {
                fclose(f);
                return 0;
            }
            if (count > 0u)
            {
                if (bp->nodes[i].regret == NULL)
                {
                    fclose(f);
                    return 0;
                }
                if (fwrite(bp->nodes[i].regret, sizeof(int32_t), (size_t)count, f) != (size_t)count)
                {
                    fclose(f);
                    return 0;
                }
                if ((dn.payload_flags & CFR_NODE_FLAG_HAS_STRATEGY) != 0u)
                {
                    if (bp->nodes[i].strategy_sum == NULL)
                    {
                        fclose(f);
                        return 0;
                    }
                    if (fwrite(bp->nodes[i].strategy_sum, sizeof(float), (size_t)count, f) != (size_t)count)
                    {
                        fclose(f);
                        return 0;
                    }
                }
            }
        }
    }

    fclose(f);
    return 1;
}

static int cfr_blueprint_load(CFRBlueprint *bp, const char *path)
{
    FILE *f;
    CFRBlueprintFileHeader h;
    uint32_t i;
    uint64_t hash_needed;
    uint32_t desired_hash_cap;
    uint32_t regret_used;
    uint32_t strategy_used;

    if (bp == NULL || path == NULL)
    {
        return 0;
    }

    f = fopen(path, "rb");
    if (f == NULL)
    {
        return 0;
    }

    if (fread(&h, sizeof(h), 1, f) != 1)
    {
        fclose(f);
        return 0;
    }

    if (h.magic != CFR_BLUEPRINT_MAGIC)
    {
        fprintf(stderr, "Invalid blueprint file magic (expected 0x%llX)\n", (unsigned long long)CFR_BLUEPRINT_MAGIC);
        fclose(f);
        return 0;
    }
    if (h.version != CFR_BLUEPRINT_VERSION)
    {
        fprintf(stderr,
                "Unsupported blueprint version: file=%u expected=%u (breaking format change; regenerate blueprint)\n",
                (unsigned int)h.version,
                (unsigned int)CFR_BLUEPRINT_VERSION);
        fclose(f);
        return 0;
    }
    if (h.max_actions != CFR_MAX_ACTIONS)
    {
        fprintf(stderr,
                "Unsupported blueprint action width: file=%u expected=%u\n",
                (unsigned int)h.max_actions,
                (unsigned int)CFR_MAX_ACTIONS);
        fclose(f);
        return 0;
    }

    if (h.regret_actions_total > (uint64_t)UINT32_MAX || h.strategy_actions_total > (uint64_t)UINT32_MAX)
    {
        fprintf(stderr, "Blueprint payload totals exceed supported loader range\n");
        fclose(f);
        return 0;
    }

    cfr_blueprint_release(bp);
    memset(bp, 0, sizeof(*bp));
    if (!cfr_blueprint_init(bp, h.rng_state))
    {
        fclose(f);
        return 0;
    }
    if (h.node_count > 0u)
    {
        if (!cfr_blueprint_resize_nodes(bp, h.node_count))
        {
            fprintf(stderr,
                    "Blueprint load failed allocating node table (%u nodes)\n",
                    (unsigned int)h.node_count);
            fclose(f);
            return 0;
        }

    }
    if (h.regret_actions_total > 0ULL)
    {
        if (!cfr_blueprint_resize_regret_storage(bp, (uint32_t)h.regret_actions_total))
        {
            fprintf(stderr,
                    "Blueprint load failed allocating regret storage (%llu actions)\n",
                    (unsigned long long)h.regret_actions_total);
            fclose(f);
            return 0;
        }
    }
    if (h.strategy_actions_total > 0ULL)
    {
        if (!cfr_blueprint_resize_strategy_storage(bp, (uint32_t)h.strategy_actions_total))
        {
            fprintf(stderr,
                    "Blueprint load failed allocating strategy storage (%llu actions)\n",
                    (unsigned long long)h.strategy_actions_total);
            fclose(f);
            return 0;
        }
    }

    bp->compat_hash32 = h.compat_hash32;
    bp->abstraction_hash32 = h.abstraction_hash32;
    bp->elapsed_train_seconds = h.elapsed_train_seconds;
    bp->phase_flags = h.phase_flags;
    bp->next_discount_second = h.next_discount_second;
    bp->next_snapshot_second = h.next_snapshot_second;
    bp->discount_events_applied = h.discount_events_applied;
    bp->iteration = h.iteration;
    bp->total_hands = h.total_hands;
    bp->omit_postflop_strategy_sum = (h.storage_flags & 1u) ? 1 : 0;
    regret_used = 0u;
    strategy_used = 0u;

    for (i = 0; i < h.node_count; ++i)
    {
        CFRBlueprintDiskNodeHeader dn;
        CFRNode *node;
        int action_count;
        uint32_t count_u32;
        uint32_t regret_offset;
        uint32_t strategy_offset;

        if (fread(&dn, sizeof(dn), 1, f) != 1)
        {
            fprintf(stderr, "Blueprint load failed at node header %u/%u\n",
                    (unsigned int)i,
                    (unsigned int)h.node_count);
            fclose(f);
            return 0;
        }

        action_count = (int)dn.action_count;
        if (action_count < 1) action_count = 1;
        if (action_count > CFR_MAX_ACTIONS) action_count = CFR_MAX_ACTIONS;
        count_u32 = (uint32_t)action_count;

        if (bp->used_node_count >= bp->node_capacity)
        {
            fprintf(stderr, "Blueprint load exceeded allocated node capacity at %u/%u\n",
                    (unsigned int)i,
                    (unsigned int)h.node_count);
            fclose(f);
            return 0;
        }
        if ((uint64_t)regret_used + (uint64_t)count_u32 > (uint64_t)bp->regret_capacity)
        {
            fprintf(stderr, "Blueprint load exceeded allocated regret payload at %u/%u\n",
                    (unsigned int)i,
                    (unsigned int)h.node_count);
            fclose(f);
            return 0;
        }
        regret_offset = regret_used;
        regret_used += count_u32;
        if ((dn.payload_flags & CFR_NODE_FLAG_HAS_STRATEGY) != 0u)
        {
            if ((uint64_t)strategy_used + (uint64_t)count_u32 > (uint64_t)bp->strategy_capacity)
            {
                fprintf(stderr, "Blueprint load exceeded allocated strategy payload at %u/%u\n",
                        (unsigned int)i,
                        (unsigned int)h.node_count);
                fclose(f);
                return 0;
            }
            strategy_offset = strategy_used;
            strategy_used += count_u32;
        }
        else
        {
            strategy_offset = CFR_STRATEGY_OFFSET_NONE;
        }

        node = &bp->nodes[bp->used_node_count];
        memset(node, 0, sizeof(*node));
        node->key = dn.key;
        cfr_node_set_overlay_meta(node, 0u, 0);
        node->action_count = (uint8_t)action_count;
        node->street_hint = dn.street_hint;
        node->regret_offset = regret_offset;
        node->strategy_offset = strategy_offset;
        node->regret = bp->regret_storage + regret_offset;
        node->strategy_sum = (strategy_offset != CFR_STRATEGY_OFFSET_NONE) ? (bp->strategy_storage + strategy_offset) : NULL;
        if (bp->node_discount_scale != NULL)
        {
            bp->node_discount_scale[bp->used_node_count] = (float)((bp->lazy_discount_scale > 0.0) ? bp->lazy_discount_scale : 1.0);
        }

        if (fread(node->regret, sizeof(int32_t), (size_t)action_count, f) != (size_t)action_count)
        {
            fprintf(stderr, "Blueprint load failed reading regrets %u/%u\n",
                    (unsigned int)i,
                    (unsigned int)h.node_count);
            fclose(f);
            return 0;
        }
        if ((dn.payload_flags & CFR_NODE_FLAG_HAS_STRATEGY) != 0u)
        {
            if (fread(node->strategy_sum, sizeof(float), (size_t)action_count, f) != (size_t)action_count)
            {
                fprintf(stderr, "Blueprint load failed reading strategy payload %u/%u\n",
                        (unsigned int)i,
                        (unsigned int)h.node_count);
                fclose(f);
                return 0;
            }
        }
        bp->used_node_count++;
    }

    bp->regret_used = regret_used;
    bp->strategy_used = strategy_used;

    hash_needed = ((uint64_t)h.node_count * (uint64_t)CFR_INFOSET_HASH_MAX_LOAD_DEN) /
                  (uint64_t)CFR_INFOSET_HASH_MAX_LOAD_NUM + 1ULL;
    if (hash_needed > (uint64_t)UINT32_MAX)
    {
        desired_hash_cap = UINT32_MAX;
    }
    else
    {
        desired_hash_cap = (uint32_t)hash_needed;
    }
    if (!cfr_blueprint_rehash(bp, desired_hash_cap))
    {
        fprintf(stderr,
                "Blueprint load failed allocating hash table (%u nodes)\n",
                (unsigned int)h.node_count);
        fclose(f);
        return 0;
    }
    if (!cfr_blueprint_rebuild_all_indexes(bp))
    {
        fprintf(stderr,
                "Blueprint load failed rebuilding indexes (%u nodes)\n",
                (unsigned int)h.node_count);
        fclose(f);
        return 0;
    }

    fclose(f);
    return 1;
}

static int cfr_snapshot_save_postflop_current(const CFRBlueprint *bp, const char *path)
{
    FILE *f;
    CFRSnapshotFileHeader h;
    uint32_t i;
    uint32_t count;

    if (bp == NULL || path == NULL)
    {
        return 0;
    }

    cfr_blueprint_materialize_all((CFRBlueprint *)bp);

    count = 0u;
    for (i = 0u; i < bp->used_node_count; ++i)
    {
        const CFRNode *n;
        n = &bp->nodes[i];
        if (!cfr_node_is_used(n) || n->action_count <= 0)
        {
            continue;
        }
        if (n->street_hint >= 1u && n->street_hint <= 3u)
        {
            count++;
        }
    }

    f = fopen(path, "wb");
    if (f == NULL)
    {
        return 0;
    }

    memset(&h, 0, sizeof(h));
    h.magic = CFR_SNAPSHOT_MAGIC;
    h.version = CFR_SNAPSHOT_VERSION;
    h.iteration = bp->iteration;
    h.elapsed_train_seconds = bp->elapsed_train_seconds;
    h.node_count = count;
    h.compat_hash32 = bp->compat_hash32;
    h.abstraction_hash32 = bp->abstraction_hash32;

    if (fwrite(&h, sizeof(h), 1, f) != 1)
    {
        fclose(f);
        return 0;
    }

    for (i = 0u; i < bp->used_node_count; ++i)
    {
        const CFRNode *n;
        CFRSnapshotDiskNodeHeader dn;
        float current[CFR_MAX_ACTIONS];
        uint32_t n_actions;

        n = &bp->nodes[i];
        if (!cfr_node_is_used(n) || n->action_count <= 0)
        {
            continue;
        }
        if (n->street_hint == 0u || n->street_hint > 3u)
        {
            continue;
        }

        memset(&dn, 0, sizeof(dn));
        dn.key = n->key;
        dn.action_count = (uint32_t)n->action_count;
        dn.street_hint = n->street_hint;
        if (fwrite(&dn, sizeof(dn), 1, f) != 1)
        {
            fclose(f);
            return 0;
        }

        cfr_compute_strategy_n(n, n->action_count, current);
        n_actions = (uint32_t)n->action_count;
        if (fwrite(current, sizeof(float), (size_t)n_actions, f) != (size_t)n_actions)
        {
            fclose(f);
            return 0;
        }
    }

    fclose(f);
    return 1;
}

static int cfr_snapshot_peek_header(const char *path, CFRSnapshotFileHeader *out_header)
{
    FILE *f;
    CFRSnapshotFileHeader h;

    if (path == NULL || out_header == NULL)
    {
        return 0;
    }

    f = fopen(path, "rb");
    if (f == NULL)
    {
        return 0;
    }
    if (fread(&h, sizeof(h), 1, f) != 1)
    {
        fclose(f);
        return 0;
    }
    fclose(f);

    if (h.magic != CFR_SNAPSHOT_MAGIC || h.version != CFR_SNAPSHOT_VERSION)
    {
        return 0;
    }
    *out_header = h;
    return 1;
}

static int cfr_snapshot_load_postflop_into_avg(CFRBlueprint *dst_avg,
                                               const char *path,
                                               uint64_t *out_elapsed_seconds,
                                               uint32_t *out_compat_hash32,
                                               uint32_t *out_abstraction_hash32)
{
    FILE *f;
    CFRSnapshotFileHeader h;
    uint32_t i;

    if (dst_avg == NULL || path == NULL)
    {
        return 0;
    }

    f = fopen(path, "rb");
    if (f == NULL)
    {
        return 0;
    }
    if (fread(&h, sizeof(h), 1, f) != 1)
    {
        fclose(f);
        return 0;
    }
    if (h.magic != CFR_SNAPSHOT_MAGIC || h.version != CFR_SNAPSHOT_VERSION)
    {
        fclose(f);
        return 0;
    }

    if (out_elapsed_seconds != NULL)
    {
        *out_elapsed_seconds = h.elapsed_train_seconds;
    }
    if (out_compat_hash32 != NULL)
    {
        *out_compat_hash32 = h.compat_hash32;
    }
    if (out_abstraction_hash32 != NULL)
    {
        *out_abstraction_hash32 = h.abstraction_hash32;
    }

    for (i = 0u; i < h.node_count; ++i)
    {
        CFRSnapshotDiskNodeHeader dn;
        CFRNode *d;
        int action_count;
        float vals[CFR_MAX_ACTIONS];
        int a;

        if (fread(&dn, sizeof(dn), 1, f) != 1)
        {
            fclose(f);
            return 0;
        }

        action_count = (int)dn.action_count;
        if (action_count < 1)
        {
            action_count = 1;
        }
        if (action_count > CFR_MAX_ACTIONS)
        {
            action_count = CFR_MAX_ACTIONS;
        }

        d = cfr_blueprint_get_node_ex(dst_avg, dn.key, 1, action_count, (int)dn.street_hint);
        if (d == NULL)
        {
            fclose(f);
            return 0;
        }
        d->street_hint = dn.street_hint;
        if (!cfr_blueprint_ensure_strategy_payload(dst_avg, d))
        {
            fclose(f);
            return 0;
        }
        if (fread(vals, sizeof(float), (size_t)action_count, f) != (size_t)action_count)
        {
            fclose(f);
            return 0;
        }
        for (a = 0; a < action_count; ++a)
        {
            d->strategy_sum[a] += vals[a];
        }
    }

    fclose(f);
    return 1;
}

static void cfr_fnv1a_mix_u32(uint64_t *h, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 4; ++i)
    {
        unsigned char b;
        b = (unsigned char)((v >> (i * 8)) & 0xFFu);
        *h ^= (uint64_t)b;
        *h *= 1099511628211ULL;
    }
}

static void cfr_fnv1a_mix_u64(uint64_t *h, uint64_t v)
{
    cfr_fnv1a_mix_u32(h, (uint32_t)(v & 0xFFFFFFFFu));
    cfr_fnv1a_mix_u32(h, (uint32_t)(v >> 32));
}

static uint64_t cfr_make_infoset_key(const CFRInfoKeyFields *k)
{
    uint64_t h;

    h = 1469598103934665603ULL;
    cfr_fnv1a_mix_u32(&h, (uint32_t)k->street);
    cfr_fnv1a_mix_u32(&h, (uint32_t)k->position);
    cfr_fnv1a_mix_u64(&h, k->hand_index);
    cfr_fnv1a_mix_u32(&h, (uint32_t)k->pot_bucket);
    cfr_fnv1a_mix_u32(&h, (uint32_t)k->to_call_bucket);
    cfr_fnv1a_mix_u32(&h, (uint32_t)k->active_players);
    cfr_fnv1a_mix_u32(&h, (uint32_t)k->history_hash);
    h &= 0x00FFFFFFFFFFFFFFULL;
    h |= ((CFR_INFOSET_KEY_TAG_HASHED_BASE | ((uint64_t)k->street & 0x3ULL)) << 56);
    return h;
}

static int cfr_count_in_hand(const CFRHandState *st)
{
    int i;
    int n;

    n = 0;
    for (i = 0; i < CFR_MAX_PLAYERS; ++i)
    {
        if (st->in_hand[i])
        {
            ++n;
        }
    }
    return n;
}

static int cfr_count_players_needing_action(const CFRHandState *st)
{
    int i;
    int n;

    n = 0;
    for (i = 0; i < CFR_MAX_PLAYERS; ++i)
    {
        if (st->in_hand[i] && st->stack[i] > 0 && st->needs_action[i])
        {
            ++n;
        }
    }
    return n;
}

static void cfr_append_history(CFRHandState *st, unsigned char code)
{
    if (st->history_len < CFR_MAX_HISTORY)
    {
        st->history[st->history_len++] = code;
    }
}

static unsigned char cfr_history_amount_bucket(int amount)
{
    if (amount <= 0) return 0;
    if (amount <= 2) return 1;
    if (amount <= 4) return 2;
    if (amount <= 8) return 3;
    if (amount <= 16) return 4;
    if (amount <= 32) return 5;
    if (amount <= 64) return 6;
    if (amount <= 128) return 7;
    return 8;
}

static void cfr_append_history_raise(CFRHandState *st, int raise_size)
{
    cfr_append_history(st, 250);
    cfr_append_history(st, cfr_history_amount_bucket(raise_size));
}

static uint32_t cfr_history_key_from_codes(const unsigned char *hist, int hist_len)
{
    uint32_t h;
    int start;
    int i;

    h = 2166136261u;
    start = 0;
    if (hist_len > CFR_HISTORY_WINDOW)
    {
        start = hist_len - CFR_HISTORY_WINDOW;
    }

    for (i = start; i < hist_len; ++i)
    {
        h ^= (uint32_t)hist[i];
        h *= 16777619u;
    }

    return h;
}

static uint32_t cfr_history_key_from_state(const CFRHandState *st)
{
    if (st == NULL)
    {
        return 0u;
    }
    return cfr_history_key_from_codes(st->history, st->history_len);
}

static int cfr_find_next_actor_from(const CFRHandState *st, int from_seat)
{
    int step;

    for (step = 1; step <= CFR_MAX_PLAYERS; ++step)
    {
        int seat;
        seat = (from_seat + step) % CFR_MAX_PLAYERS;
        if (st->in_hand[seat] && st->stack[seat] > 0 && st->needs_action[seat])
        {
            return seat;
        }
    }
    return -1;
}

static int cfr_first_actor_for_street(const CFRHandState *st)
{
    int start;

    if (st->street == 0)
    {
        start = (st->dealer + 2) % CFR_MAX_PLAYERS;
    }
    else
    {
        start = st->dealer;
    }

    return cfr_find_next_actor_from(st, start);
}

static void cfr_post_chips(CFRHandState *st, int seat, int amount)
{
    int pay;

    pay = cfr_min_int(st->stack[seat], amount);
    st->stack[seat] -= pay;
    st->committed_street[seat] += pay;
    st->contributed_total[seat] += pay;
    st->pot += pay;
}

static void cfr_deal_board_for_street(CFRHandState *st)
{
    int n;
    int i;

    if (st->street == 1)
    {
        n = 3;
    }
    else if (st->street == 2 || st->street == 3)
    {
        n = 1;
    }
    else
    {
        n = 0;
    }

    for (i = 0; i < n && st->board_count < CFR_MAX_BOARD; ++i)
    {
        st->board[st->board_count++] = cfr_draw_card(st);
    }
}

static void cfr_reset_for_new_street(CFRHandState *st)
{
    uint32_t i;

    for (i = 0; i < CFR_MAX_PLAYERS; ++i)
    {
        st->committed_street[i] = 0;
        st->acted_this_street[i] = 0;
        st->acted_on_full_raise_seq[i] = -1;
    }
    st->to_call = 0;
    st->num_raises_street = 0;
    st->last_full_raise = CFR_BIG_BLIND;
    st->full_raise_seq = 0;
}

static void cfr_auto_advance_rounds_if_needed(CFRHandState *st)
{
    while (!st->is_terminal)
    {
        int i;

        if (cfr_count_in_hand(st) <= 1)
        {
            st->is_terminal = 1;
            st->current_player = -1;
            return;
        }

        if (cfr_count_players_needing_action(st) > 0)
        {
            if (st->current_player < 0 || !st->in_hand[st->current_player] || st->stack[st->current_player] <= 0 || !st->needs_action[st->current_player])
            {
                st->current_player = cfr_first_actor_for_street(st);
            }
            return;
        }

        if (st->street >= 3)
        {
            st->is_terminal = 1;
            st->current_player = -1;
            return;
        }

        st->street++;
        cfr_append_history(st, (unsigned char)(240 + st->street));
        cfr_deal_board_for_street(st);
        cfr_reset_for_new_street(st);

        for (i = 0; i < CFR_MAX_PLAYERS; ++i)
        {
            st->needs_action[i] = (st->in_hand[i] && st->stack[i] > 0) ? 1 : 0;
        }

        st->current_player = cfr_first_actor_for_street(st);
    }
}

static void cfr_init_hand(CFRHandState *st, int dealer, uint64_t *rng)
{
    int i;
    int r;
    int sb;
    int bb;

    memset(st, 0, sizeof(*st));

    st->dealer = ((dealer % CFR_MAX_PLAYERS) + CFR_MAX_PLAYERS) % CFR_MAX_PLAYERS;
    st->street = 0;
    st->pot = 0;
    st->to_call = 0;
    st->current_player = -1;
    st->board_count = 0;
    st->num_raises_street = 0;
    st->last_full_raise = CFR_BIG_BLIND;
    st->full_raise_seq = 0;
    st->is_terminal = 0;
    st->terminal_resolved = 0;
    st->deck_pos = 0;
    st->history_len = 0;

    for (i = 0; i < CFR_MAX_PLAYERS; ++i)
    {
        st->stack[i] = CFR_START_STACK;
        st->in_hand[i] = 1;
        st->committed_street[i] = 0;
        st->contributed_total[i] = 0;
        st->needs_action[i] = 0;
        st->acted_this_street[i] = 0;
        st->acted_on_full_raise_seq[i] = -1;
    }

    cfr_shuffle_deck(st->deck, rng);

    for (r = 0; r < 2; ++r)
    {
        for (i = 0; i < CFR_MAX_PLAYERS; ++i)
        {
            int seat;
            seat = (st->dealer + 1 + i) % CFR_MAX_PLAYERS;
            st->hole[seat][r] = cfr_draw_card(st);
        }
    }

    sb = (st->dealer + 1) % CFR_MAX_PLAYERS;
    bb = (st->dealer + 2) % CFR_MAX_PLAYERS;

    cfr_post_chips(st, sb, CFR_SMALL_BLIND);
    cfr_post_chips(st, bb, CFR_BIG_BLIND);
    st->to_call = st->committed_street[bb];
    st->last_full_raise = CFR_BIG_BLIND;

    for (i = 0; i < CFR_MAX_PLAYERS; ++i)
    {
        st->needs_action[i] = (st->in_hand[i] && st->stack[i] > 0) ? 1 : 0;
    }

    st->current_player = cfr_find_next_actor_from(st, bb);
    cfr_auto_advance_rounds_if_needed(st);
}

static int cfr_add_unique_target(int *targets, int n, int target)
{
    int i;
    for (i = 0; i < n; ++i)
    {
        if (targets[i] == target)
        {
            return n;
        }
    }
    targets[n] = target;
    return n + 1;
}

static int cfr_compute_min_raise_target(const CFRHandState *st, int player)
{
    int committed;
    int base_to;
    int increment;

    committed = st->committed_street[player];
    base_to = st->to_call;
    if (base_to < committed)
    {
        base_to = committed;
    }
    increment = st->last_full_raise;
    if (increment < CFR_BIG_BLIND)
    {
        increment = CFR_BIG_BLIND;
    }
    return base_to + increment;
}

static int cfr_can_player_raise(const CFRHandState *st, int player, int diff)
{
    if (st->stack[player] <= diff)
    {
        return 0;
    }
    if (!st->acted_this_street[player])
    {
        return 1;
    }
    return st->acted_on_full_raise_seq[player] < st->full_raise_seq;
}

static int cfr_get_raise_fracs_for_state(const CFRHandState *st, int player, double *out, int max_out)
{
    static const double preflop_fracs[] = {
        0.25, 0.33, 0.50, 0.67, 0.75, 1.00, 1.25, 1.50, 2.00, 2.50, 3.00, 4.00, 6.00, 8.00
    };
    static const double flop_fracs[] = {
        0.25, 0.33, 0.50, 0.67, 0.75, 1.00, 1.25, 1.50, 2.00, 3.00
    };
    static const double turn_first_fracs[] = {
        0.50, 1.00
    };
    static const double turn_later_fracs[] = {
        1.00
    };
    static const double river_first_fracs[] = {
        0.50, 1.00
    };
    static const double river_later_fracs[] = {
        1.00
    };
    const double *base;
    int base_n;
    int n;
    int i;
    int stack_to_call;

    if (st == NULL || out == NULL || max_out <= 0)
    {
        return 0;
    }

    if (st->street <= 0)
    {
        base = preflop_fracs;
        base_n = (int)(sizeof(preflop_fracs) / sizeof(preflop_fracs[0]));
    }
    else if (st->street == 1)
    {
        base = flop_fracs;
        base_n = (int)(sizeof(flop_fracs) / sizeof(flop_fracs[0]));
    }
    else if (st->street == 2)
    {
        if (st->num_raises_street <= 0)
        {
            base = turn_first_fracs;
            base_n = (int)(sizeof(turn_first_fracs) / sizeof(turn_first_fracs[0]));
        }
        else
        {
            base = turn_later_fracs;
            base_n = (int)(sizeof(turn_later_fracs) / sizeof(turn_later_fracs[0]));
        }
    }
    else
    {
        if (st->num_raises_street <= 0)
        {
            base = river_first_fracs;
            base_n = (int)(sizeof(river_first_fracs) / sizeof(river_first_fracs[0]));
        }
        else
        {
            base = river_later_fracs;
            base_n = (int)(sizeof(river_later_fracs) / sizeof(river_later_fracs[0]));
        }
    }

    n = base_n;
    if (st->pot <= 16)
    {
        if (n > 6) n = 6;
    }
    if (st->pot <= 8)
    {
        if (n > 4) n = 4;
    }
    if (st->pot <= 4)
    {
        if (n > 2) n = 2;
    }

    stack_to_call = st->stack[player];
    if (stack_to_call <= st->to_call + st->last_full_raise)
    {
        if (n > 1) n = 1;
    }
    else if (stack_to_call <= st->to_call + 2 * st->last_full_raise)
    {
        if (n > 2) n = 2;
    }

    if (n < 1)
    {
        n = 1;
    }
    if (n > max_out)
    {
        n = max_out;
    }

    for (i = 0; i < n; ++i)
    {
        out[i] = base[i];
    }
    return n;
}

static int cfr_get_legal_actions(const CFRHandState *st, int player, int *actions, int *targets)
{
    double raise_fracs[16];
    int raise_frac_n;
    int candidate_targets[32];
    int candidate_count;
    int committed;
    int stack;
    int max_target;
    int min_target;
    int n;
    int diff;
    int i;

    n = 0;
    committed = st->committed_street[player];
    stack = st->stack[player];
    diff = st->to_call - st->committed_street[player];
    if (diff < 0)
    {
        diff = 0;
    }

    if (diff > 0)
    {
        actions[n] = CFR_ACT_FOLD;
        targets[n] = 0;
        ++n;
    }

    actions[n] = CFR_ACT_CALL_CHECK;
    targets[n] = st->to_call;
    ++n;

    if (cfr_can_player_raise(st, player, diff))
    {
        max_target = committed + stack;
        if (max_target > st->to_call)
        {
            min_target = cfr_compute_min_raise_target(st, player);
            if (min_target > max_target)
            {
                min_target = max_target;
            }
            if (min_target <= st->to_call)
            {
                min_target = st->to_call + 1;
            }

            candidate_count = 0;
            candidate_count = cfr_add_unique_target(candidate_targets, candidate_count, min_target);
            raise_frac_n = cfr_get_raise_fracs_for_state(st, player, raise_fracs, (int)(sizeof(raise_fracs) / sizeof(raise_fracs[0])));
            for (i = 0; i < raise_frac_n; ++i)
            {
                int raise_size;
                int target;

                raise_size = (int)((double)st->pot * raise_fracs[i] + 0.5);
                if (raise_size < CFR_BIG_BLIND)
                {
                    raise_size = CFR_BIG_BLIND;
                }

                target = cfr_max_int(st->to_call, committed) + raise_size;
                if (target > max_target)
                {
                    target = max_target;
                }
                if (target < min_target && target != max_target)
                {
                    continue;
                }
                if (target > st->to_call)
                {
                    candidate_count = cfr_add_unique_target(candidate_targets, candidate_count, target);
                }
            }
            candidate_count = cfr_add_unique_target(candidate_targets, candidate_count, max_target);

            for (i = 0; i < candidate_count; ++i)
            {
                int j;
                for (j = i + 1; j < candidate_count; ++j)
                {
                    if (candidate_targets[j] < candidate_targets[i])
                    {
                        int t;
                        t = candidate_targets[i];
                        candidate_targets[i] = candidate_targets[j];
                        candidate_targets[j] = t;
                    }
                }
            }

            for (i = 0; i < candidate_count && n < CFR_MAX_ACTIONS; ++i)
            {
                int target;
                target = candidate_targets[i];
                if (target <= st->to_call)
                {
                    continue;
                }
                actions[n] = (target == max_target) ? CFR_ACT_ALL_IN : CFR_ACT_RAISE_TO;
                targets[n] = target;
                ++n;
            }
        }
    }

    return n;
}

static int cfr_is_raise_action(int action)
{
    return (action == CFR_ACT_RAISE_TO || action == CFR_ACT_ALL_IN || action == CFR_ACT_BET_HALF || action == CFR_ACT_BET_POT);
}

static int cfr_translate_action_to_legal(const CFRHandState *st,
                                         int player,
                                         int requested_action,
                                         int requested_target,
                                         int *out_action,
                                         int *out_target,
                                         int *out_off_tree)
{
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int legal_count;
    int i;
    int exact_index;

    if (st == NULL || out_action == NULL || out_target == NULL || out_off_tree == NULL)
    {
        return 0;
    }

    legal_count = cfr_get_legal_actions(st, player, legal_actions, legal_targets);
    if (legal_count <= 0)
    {
        return 0;
    }

    exact_index = -1;
    for (i = 0; i < legal_count; ++i)
    {
        if (cfr_is_raise_action(requested_action))
        {
            if (cfr_is_raise_action(legal_actions[i]) && legal_targets[i] == requested_target)
            {
                exact_index = i;
                break;
            }
        }
        else if (legal_actions[i] == requested_action)
        {
            exact_index = i;
            break;
        }
    }

    if (exact_index >= 0)
    {
        *out_action = legal_actions[exact_index];
        *out_target = legal_targets[exact_index];
        *out_off_tree = 0;
        return 1;
    }

    if (cfr_is_raise_action(requested_action))
    {
        int best_index;
        int best_dist;

        best_index = -1;
        best_dist = 0x7FFFFFFF;
        for (i = 0; i < legal_count; ++i)
        {
            int dist;

            if (!cfr_is_raise_action(legal_actions[i]))
            {
                continue;
            }

            dist = legal_targets[i] - requested_target;
            if (dist < 0)
            {
                dist = -dist;
            }

            if (best_index < 0 || dist < best_dist || (dist == best_dist && legal_targets[i] < legal_targets[best_index]))
            {
                best_index = i;
                best_dist = dist;
            }
        }

        if (best_index >= 0)
        {
            *out_action = legal_actions[best_index];
            *out_target = legal_targets[best_index];
            *out_off_tree = 1;
            return 1;
        }
    }

    for (i = 0; i < legal_count; ++i)
    {
        if (legal_actions[i] == CFR_ACT_CALL_CHECK)
        {
            *out_action = legal_actions[i];
            *out_target = legal_targets[i];
            *out_off_tree = 1;
            return 1;
        }
    }

    *out_action = legal_actions[0];
    *out_target = legal_targets[0];
    *out_off_tree = 1;
    return 1;
}

static int cfr_translate_action_to_legal_pseudo_harmonic(const CFRHandState *st,
                                                         int player,
                                                         int requested_action,
                                                         int requested_target,
                                                         uint64_t *rng,
                                                         int *out_action,
                                                         int *out_target,
                                                         int *out_off_tree)
{
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int legal_count;
    int i;
    int exact_index;
    int low_idx;
    int high_idx;

    if (!cfr_is_raise_action(requested_action))
    {
        return cfr_translate_action_to_legal(st,
                                             player,
                                             requested_action,
                                             requested_target,
                                             out_action,
                                             out_target,
                                             out_off_tree);
    }

    if (st == NULL || out_action == NULL || out_target == NULL || out_off_tree == NULL)
    {
        return 0;
    }

    legal_count = cfr_get_legal_actions(st, player, legal_actions, legal_targets);
    if (legal_count <= 0)
    {
        return 0;
    }

    exact_index = -1;
    for (i = 0; i < legal_count; ++i)
    {
        if (cfr_is_raise_action(legal_actions[i]) && legal_targets[i] == requested_target)
        {
            exact_index = i;
            break;
        }
    }
    if (exact_index >= 0)
    {
        *out_action = legal_actions[exact_index];
        *out_target = legal_targets[exact_index];
        *out_off_tree = 0;
        return 1;
    }

    low_idx = -1;
    high_idx = -1;
    for (i = 0; i < legal_count; ++i)
    {
        if (!cfr_is_raise_action(legal_actions[i]))
        {
            continue;
        }
        if (legal_targets[i] < requested_target)
        {
            low_idx = i;
        }
        else if (legal_targets[i] > requested_target)
        {
            high_idx = i;
            break;
        }
    }

    if (low_idx < 0 && high_idx < 0)
    {
        return cfr_translate_action_to_legal(st,
                                             player,
                                             requested_action,
                                             requested_target,
                                             out_action,
                                             out_target,
                                             out_off_tree);
    }
    if (low_idx < 0)
    {
        *out_action = legal_actions[high_idx];
        *out_target = legal_targets[high_idx];
        *out_off_tree = 1;
        return 1;
    }
    if (high_idx < 0)
    {
        *out_action = legal_actions[low_idx];
        *out_target = legal_targets[low_idx];
        *out_off_tree = 1;
        return 1;
    }

    if (rng == NULL)
    {
        int d_low;
        int d_high;
        d_low = requested_target - legal_targets[low_idx];
        d_high = legal_targets[high_idx] - requested_target;
        if (d_low <= d_high)
        {
            *out_action = legal_actions[low_idx];
            *out_target = legal_targets[low_idx];
        }
        else
        {
            *out_action = legal_actions[high_idx];
            *out_target = legal_targets[high_idx];
        }
        *out_off_tree = 1;
        return 1;
    }
    else
    {
        double d_low;
        double d_high;
        double w_low;
        double w_high;
        double p_low;
        double r;

        d_low = (double)(requested_target - legal_targets[low_idx]);
        d_high = (double)(legal_targets[high_idx] - requested_target);
        if (d_low < 1.0)
        {
            d_low = 1.0;
        }
        if (d_high < 1.0)
        {
            d_high = 1.0;
        }

        w_low = 1.0 / d_low;
        w_high = 1.0 / d_high;
        p_low = w_low / (w_low + w_high);
        r = cfr_rng_unit(rng);

        if (r < p_low)
        {
            *out_action = legal_actions[low_idx];
            *out_target = legal_targets[low_idx];
        }
        else
        {
            *out_action = legal_actions[high_idx];
            *out_target = legal_targets[high_idx];
        }
        *out_off_tree = 1;
        return 1;
    }
}

static void cfr_apply_call(CFRHandState *st, int player)
{
    int diff;
    int pay;

    diff = st->to_call - st->committed_street[player];
    if (diff < 0)
    {
        diff = 0;
    }

    pay = cfr_min_int(diff, st->stack[player]);
    st->stack[player] -= pay;
    st->committed_street[player] += pay;
    st->contributed_total[player] += pay;
    st->pot += pay;
    st->needs_action[player] = 0;

    if (st->stack[player] <= 0)
    {
        st->needs_action[player] = 0;
    }
}

static int cfr_apply_raise_to(CFRHandState *st, int player, int target)
{
    int committed;
    int old_to_call;
    int pay;
    int raise_size;
    int full_raise_event;
    int i;

    committed = st->committed_street[player];
    old_to_call = st->to_call;
    full_raise_event = 0;

    if (target < committed)
    {
        target = committed;
    }
    if (target > committed + st->stack[player])
    {
        target = committed + st->stack[player];
    }

    if (target <= old_to_call)
    {
        cfr_apply_call(st, player);
        return 0;
    }

    pay = target - committed;
    if (pay < 0)
    {
        pay = 0;
    }
    if (pay > st->stack[player])
    {
        pay = st->stack[player];
    }

    st->stack[player] -= pay;
    st->committed_street[player] += pay;
    st->contributed_total[player] += pay;
    st->pot += pay;

    st->to_call = st->committed_street[player];
    raise_size = st->to_call - old_to_call;
    if (raise_size >= st->last_full_raise)
    {
        st->last_full_raise = raise_size;
        full_raise_event = 1;
    }
    st->num_raises_street++;
    st->needs_action[player] = 0;

    for (i = 0; i < CFR_MAX_PLAYERS; ++i)
    {
        if (i != player && st->in_hand[i] && st->stack[i] > 0 && st->committed_street[i] < st->to_call)
        {
            st->needs_action[i] = 1;
        }
    }

    return full_raise_event;
}

static int cfr_apply_action(CFRHandState *st, int player, int action, int target)
{
    int diff;

    if (st->is_terminal)
    {
        return 0;
    }

    diff = st->to_call - st->committed_street[player];
    if (diff < 0)
    {
        diff = 0;
    }

    if (action == CFR_ACT_FOLD && diff <= 0)
    {
        action = CFR_ACT_CALL_CHECK;
    }

    if (action == CFR_ACT_FOLD)
    {
        cfr_append_history(st, (unsigned char)CFR_ACT_FOLD);
        st->in_hand[player] = 0;
        st->needs_action[player] = 0;
        st->acted_this_street[player] = 1;
        st->acted_on_full_raise_seq[player] = st->full_raise_seq;
    }
    else if (action == CFR_ACT_CALL_CHECK)
    {
        cfr_append_history(st, (unsigned char)CFR_ACT_CALL_CHECK);
        cfr_apply_call(st, player);
        st->acted_this_street[player] = 1;
        st->acted_on_full_raise_seq[player] = st->full_raise_seq;
    }
    else
    {
        int old_to_call;
        int full_raise_event;

        old_to_call = st->to_call;
        cfr_append_history(st, (unsigned char)((action == CFR_ACT_ALL_IN) ? CFR_ACT_ALL_IN : CFR_ACT_RAISE_TO));
        full_raise_event = cfr_apply_raise_to(st, player, target);
        if (full_raise_event)
        {
            st->full_raise_seq++;
        }
        st->acted_this_street[player] = 1;
        st->acted_on_full_raise_seq[player] = st->full_raise_seq;
        cfr_append_history_raise(st, st->to_call - old_to_call);
    }

    if (cfr_count_in_hand(st) <= 1)
    {
        st->is_terminal = 1;
        st->current_player = -1;
        return 1;
    }

    st->current_player = cfr_find_next_actor_from(st, player);
    cfr_auto_advance_rounds_if_needed(st);

    return 1;
}

static void cfr_resolve_terminal(CFRHandState *st)
{
    if (st->terminal_resolved)
    {
        return;
    }

    if (cfr_count_in_hand(st) <= 1)
    {
        int i;
        int winner;

        winner = -1;
        for (i = 0; i < CFR_MAX_PLAYERS; ++i)
        {
            if (st->in_hand[i])
            {
                winner = i;
                break;
            }
        }

        if (winner >= 0)
        {
            st->stack[winner] += st->pot;
            st->pot = 0;
        }

        st->terminal_resolved = 1;
        st->is_terminal = 1;
        st->current_player = -1;
        return;
    }

    while (st->board_count < 5)
    {
        st->board[st->board_count++] = cfr_draw_card(st);
    }

    {
        uint32_t score_by_player[CFR_MAX_PLAYERS];
        int levels[CFR_MAX_PLAYERS];
        int level_count;
        int prev_level;
        int remaining_pot;
        int i;

        for (i = 0; i < CFR_MAX_PLAYERS; ++i)
        {
            score_by_player[i] = UINT32_MAX;
            if (st->in_hand[i])
            {
                int cards7[7];

                cards7[0] = st->hole[i][0];
                cards7[1] = st->hole[i][1];
                cards7[2] = st->board[0];
                cards7[3] = st->board[1];
                cards7[4] = st->board[2];
                cards7[5] = st->board[3];
                cards7[6] = st->board[4];
                score_by_player[i] = cfr_eval_best_hand(cards7);
            }
        }

        level_count = 0;
        for (i = 0; i < CFR_MAX_PLAYERS; ++i)
        {
            int amt;
            int j;
            int seen;

            amt = st->contributed_total[i];
            if (amt <= 0)
            {
                continue;
            }

            seen = 0;
            for (j = 0; j < level_count; ++j)
            {
                if (levels[j] == amt)
                {
                    seen = 1;
                    break;
                }
            }

            if (!seen)
            {
                levels[level_count++] = amt;
            }
        }

        for (i = 0; i < level_count; ++i)
        {
            int j;
            for (j = i + 1; j < level_count; ++j)
            {
                if (levels[j] < levels[i])
                {
                    int t;
                    t = levels[i];
                    levels[i] = levels[j];
                    levels[j] = t;
                }
            }
        }

        prev_level = 0;
        remaining_pot = st->pot;
        for (i = 0; i < level_count; ++i)
        {
            int level;
            int layer_size;
            int contrib_players;
            int layer_pot;
            uint32_t best_score;
            int winners[CFR_MAX_PLAYERS];
            int n_winners;
            int p;

            level = levels[i];
            layer_size = level - prev_level;
            prev_level = level;
            if (layer_size <= 0)
            {
                continue;
            }

            contrib_players = 0;
            for (p = 0; p < CFR_MAX_PLAYERS; ++p)
            {
                if (st->contributed_total[p] >= level)
                {
                    contrib_players++;
                }
            }
            if (contrib_players <= 0)
            {
                continue;
            }

            layer_pot = layer_size * contrib_players;
            if (layer_pot > remaining_pot)
            {
                layer_pot = remaining_pot;
            }
            if (layer_pot <= 0)
            {
                continue;
            }

            best_score = UINT32_MAX;
            n_winners = 0;
            for (p = 0; p < CFR_MAX_PLAYERS; ++p)
            {
                if (st->in_hand[p] && st->contributed_total[p] >= level)
                {
                    if (n_winners == 0 || score_by_player[p] < best_score)
                    {
                        best_score = score_by_player[p];
                        winners[0] = p;
                        n_winners = 1;
                    }
                    else if (score_by_player[p] == best_score)
                    {
                        winners[n_winners++] = p;
                    }
                }
            }

            if (n_winners > 0)
            {
                int share;
                int rem;

                share = layer_pot / n_winners;
                rem = layer_pot % n_winners;

                for (p = 0; p < n_winners; ++p)
                {
                    st->stack[winners[p]] += share;
                }
                for (p = 0; p < rem; ++p)
                {
                    st->stack[winners[p]] += 1;
                }
            }

            remaining_pot -= layer_pot;
            if (remaining_pot <= 0)
            {
                remaining_pot = 0;
                break;
            }
        }

        if (remaining_pot > 0)
        {
            uint32_t best_score;
            int winners[CFR_MAX_PLAYERS];
            int n_winners;

            best_score = UINT32_MAX;
            n_winners = 0;
            for (i = 0; i < CFR_MAX_PLAYERS; ++i)
            {
                if (st->in_hand[i])
                {
                    if (n_winners == 0 || score_by_player[i] < best_score)
                    {
                        best_score = score_by_player[i];
                        winners[0] = i;
                        n_winners = 1;
                    }
                    else if (score_by_player[i] == best_score)
                    {
                        winners[n_winners++] = i;
                    }
                }
            }

            if (n_winners > 0)
            {
                int share;
                int rem;

                share = remaining_pot / n_winners;
                rem = remaining_pot % n_winners;
                for (i = 0; i < n_winners; ++i)
                {
                    st->stack[winners[i]] += share;
                }
                for (i = 0; i < rem; ++i)
                {
                    st->stack[winners[i]] += 1;
                }
                remaining_pot = 0;
            }
        }

        st->pot = remaining_pot;
        if (st->pot < 0)
        {
            st->pot = 0;
        }
    }

    st->terminal_resolved = 1;
    st->is_terminal = 1;
    st->current_player = -1;
}


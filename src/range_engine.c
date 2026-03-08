typedef struct
{
    double weight[CFR_HOLDEM_COMBOS];
} CFRRange1326;

static int cfr_range_cards_to_index(int card_a, int card_b)
{
    int c1;
    int c2;
    int idx;
    int i;

    if (card_a < 0 || card_a >= CFR_DECK_SIZE || card_b < 0 || card_b >= CFR_DECK_SIZE || card_a == card_b)
    {
        return -1;
    }

    c1 = card_a;
    c2 = card_b;
    if (c1 > c2)
    {
        int t;
        t = c1;
        c1 = c2;
        c2 = t;
    }

    idx = 0;
    for (i = 0; i < c1; ++i)
    {
        idx += (CFR_DECK_SIZE - 1 - i);
    }
    idx += (c2 - c1 - 1);
    if (idx < 0 || idx >= CFR_HOLDEM_COMBOS)
    {
        return -1;
    }
    return idx;
}

static int cfr_range_index_to_cards(int index, int *out_card_a, int *out_card_b)
{
    int i;
    int rem;

    if (out_card_a == NULL || out_card_b == NULL || index < 0 || index >= CFR_HOLDEM_COMBOS)
    {
        return 0;
    }

    rem = index;
    for (i = 0; i < CFR_DECK_SIZE - 1; ++i)
    {
        int row_n;
        row_n = CFR_DECK_SIZE - 1 - i;
        if (rem < row_n)
        {
            *out_card_a = i;
            *out_card_b = i + 1 + rem;
            return 1;
        }
        rem -= row_n;
    }
    return 0;
}

static double cfr_range_weight_sum(const CFRRange1326 *range)
{
    int i;
    double s;

    if (range == NULL)
    {
        return 0.0;
    }
    s = 0.0;
    for (i = 0; i < CFR_HOLDEM_COMBOS; ++i)
    {
        s += range->weight[i];
    }
    return s;
}

static void cfr_range_normalize(CFRRange1326 *range)
{
    int i;
    double s;

    if (range == NULL)
    {
        return;
    }

    s = 0.0;
    for (i = 0; i < CFR_HOLDEM_COMBOS; ++i)
    {
        if (range->weight[i] < 0.0)
        {
            range->weight[i] = 0.0;
        }
        s += range->weight[i];
    }

    if (s <= 0.0)
    {
        return;
    }

    for (i = 0; i < CFR_HOLDEM_COMBOS; ++i)
    {
        range->weight[i] /= s;
    }
}

static void cfr_range_clear(CFRRange1326 *range)
{
    if (range == NULL)
    {
        return;
    }
    memset(range, 0, sizeof(*range));
}

static void cfr_range_init_uniform_blocked(CFRRange1326 *range, const int *blocked_cards, int blocked_count)
{
    unsigned char blocked[CFR_DECK_SIZE];
    int i;

    if (range == NULL)
    {
        return;
    }

    memset(blocked, 0, sizeof(blocked));
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

    for (i = 0; i < CFR_HOLDEM_COMBOS; ++i)
    {
        int a;
        int b;
        if (!cfr_range_index_to_cards(i, &a, &b))
        {
            range->weight[i] = 0.0;
            continue;
        }
        range->weight[i] = (blocked[a] || blocked[b]) ? 0.0 : 1.0;
    }
    cfr_range_normalize(range);
}

static void cfr_range_apply_likelihood(CFRRange1326 *range, const double *likelihood)
{
    int i;

    if (range == NULL || likelihood == NULL)
    {
        return;
    }
    for (i = 0; i < CFR_HOLDEM_COMBOS; ++i)
    {
        double l;
        l = likelihood[i];
        if (l < 0.0)
        {
            l = 0.0;
        }
        range->weight[i] *= l;
    }
    cfr_range_normalize(range);
}

static void cfr_range_apply_blockers(CFRRange1326 *range, const int *blocked_cards, int blocked_count)
{
    unsigned char blocked[CFR_DECK_SIZE];
    int i;

    if (range == NULL || blocked_cards == NULL || blocked_count <= 0)
    {
        return;
    }

    memset(blocked, 0, sizeof(blocked));
    for (i = 0; i < blocked_count; ++i)
    {
        int c;
        c = blocked_cards[i];
        if (c >= 0 && c < CFR_DECK_SIZE)
        {
            blocked[c] = 1u;
        }
    }

    for (i = 0; i < CFR_HOLDEM_COMBOS; ++i)
    {
        int c1;
        int c2;
        if (!cfr_range_index_to_cards(i, &c1, &c2))
        {
            range->weight[i] = 0.0;
            continue;
        }
        if (blocked[c1] || blocked[c2])
        {
            range->weight[i] = 0.0;
        }
    }
    cfr_range_normalize(range);
}

static int cfr_range_count_nonzero(const CFRRange1326 *range)
{
    int i;
    int n;

    if (range == NULL)
    {
        return 0;
    }
    n = 0;
    for (i = 0; i < CFR_HOLDEM_COMBOS; ++i)
    {
        if (range->weight[i] > 0.0)
        {
            n++;
        }
    }
    return n;
}

static int cfr_range_fill_action_likelihood_from_blueprint(const CFRBlueprint *bp,
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
    int mapped_action;
    int mapped_target;
    int off_tree;
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
    if (!cfr_translate_action_to_legal(st, player, action, target, &mapped_action, &mapped_target, &off_tree))
    {
        memset(out_likelihood, 0, sizeof(double) * CFR_HOLDEM_COMBOS);
        return 0;
    }

    action_idx = -1;
    for (i = 0; i < legal_count; ++i)
    {
        if (legal_actions[i] == mapped_action && legal_targets[i] == mapped_target)
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

        {
            CFRHandState tmp;
            CFRInfoKeyFields kf;
            uint64_t key;
            CFRNode *node;
            float strat[CFR_MAX_ACTIONS];
            double p;

            tmp = *st;
            tmp.hole[player][0] = c1;
            tmp.hole[player][1] = c2;

            if (!cfr_extract_infoset_fields(&tmp, player, &kf))
            {
                out_likelihood[i] = 0.0;
                continue;
            }

            key = cfr_make_infoset_key(&kf);
            node = cfr_blueprint_get_node((CFRBlueprint *)bp, key, 0, legal_count);
            if (node == NULL)
            {
                p = 1.0 / (double)legal_count;
            }
            else
            {
                cfr_compute_average_strategy_n(node, legal_count, strat);
                p = (double)strat[action_idx];
            }
            if (p < 0.0)
            {
                p = 0.0;
            }
            out_likelihood[i] = p;
            valid_n++;
        }
    }

    return valid_n;
}

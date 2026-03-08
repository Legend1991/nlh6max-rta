#define _CRT_SECURE_NO_WARNINGS

#include "../third_party/unity/src/unity.h"

#define CFR_TEST 1
#include "../src/main.c"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_regret_matching_uses_regret_plus(void)
{
    CFRBlueprint bp;
    CFRNode *node;
    float strat[CFR_MAX_ACTIONS];

    memset(&bp, 0, sizeof(bp));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 123ULL));
    node = cfr_blueprint_get_node(&bp, 0xAAULL, 1, 3);
    TEST_ASSERT_NOT_NULL(node);
    node->regret[0] = -5;
    node->regret[1] = 1;
    node->regret[2] = 3;

    cfr_compute_strategy(node, strat);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, strat[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, strat[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.75f, strat[2]);
}

static void test_regret_matching_uniform_when_all_non_positive(void)
{
    CFRBlueprint bp;
    CFRNode *node;
    float strat[CFR_MAX_ACTIONS];

    memset(&bp, 0, sizeof(bp));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 124ULL));
    node = cfr_blueprint_get_node(&bp, 0xABULL, 1, 4);
    TEST_ASSERT_NOT_NULL(node);
    node->regret[0] = -5;
    node->regret[1] = 0;
    node->regret[2] = -1;
    node->regret[3] = -3;

    cfr_compute_strategy(node, strat);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, strat[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, strat[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, strat[2]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, strat[3]);
}

static void test_infoset_key_is_deterministic(void)
{
    CFRInfoKeyFields k;
    uint64_t a;
    uint64_t b;

    memset(&k, 0, sizeof(k));
    k.street = 2;
    k.position = 4;
    k.hand_index = 12345678ULL;
    k.pot_bucket = 7;
    k.to_call_bucket = 2;
    k.active_players = 3;
    k.history_hash = 0xABCD1234u;

    a = cfr_make_infoset_key(&k);
    b = cfr_make_infoset_key(&k);

    TEST_ASSERT_EQUAL_UINT64(a, b);
}

static void test_hand_index_sizes_match_holdem_known_values(void)
{
    TEST_ASSERT_EQUAL_UINT64(169ULL, cfr_hand_index_size_for_street(0));
    TEST_ASSERT_EQUAL_UINT64(1286792ULL, cfr_hand_index_size_for_street(1));
    TEST_ASSERT_EQUAL_UINT64(55190538ULL, cfr_hand_index_size_for_street(2));
    TEST_ASSERT_EQUAL_UINT64(2428287420ULL, cfr_hand_index_size_for_street(3));
}

static void test_hand_index_is_suit_isomorphic_preflop(void)
{
    uint64_t idx_a;
    uint64_t idx_b;
    uint64_t idx_c;
    int as;
    int ks;
    int ah;
    int kh;
    int kd;

    as = cfr_parse_card_pair('A', 's');
    ks = cfr_parse_card_pair('K', 's');
    ah = cfr_parse_card_pair('A', 'h');
    kh = cfr_parse_card_pair('K', 'h');
    kd = cfr_parse_card_pair('K', 'd');

    TEST_ASSERT_TRUE(cfr_hand_index_for_state(0, as, ks, NULL, 0, &idx_a));
    TEST_ASSERT_TRUE(cfr_hand_index_for_state(0, ah, kh, NULL, 0, &idx_b));
    TEST_ASSERT_TRUE(cfr_hand_index_for_state(0, as, kd, NULL, 0, &idx_c));

    TEST_ASSERT_EQUAL_UINT64(idx_a, idx_b);
    TEST_ASSERT_TRUE(idx_a != idx_c);
}

static void test_hand_index_is_suit_isomorphic_flop(void)
{
    int hole_a[2];
    int board_a[3];
    int hole_b[2];
    int board_b[3];
    uint64_t idx_a;
    uint64_t idx_b;

    TEST_ASSERT_EQUAL_INT(2, cfr_parse_cards("AsKd", hole_a, 2));
    TEST_ASSERT_EQUAL_INT(3, cfr_parse_cards("QcJh2h", board_a, 3));

    TEST_ASSERT_EQUAL_INT(2, cfr_parse_cards("AcKh", hole_b, 2));
    TEST_ASSERT_EQUAL_INT(3, cfr_parse_cards("QdJs2s", board_b, 3));

    TEST_ASSERT_TRUE(cfr_hand_index_for_state(1, hole_a[0], hole_a[1], board_a, 3, &idx_a));
    TEST_ASSERT_TRUE(cfr_hand_index_for_state(1, hole_b[0], hole_b[1], board_b, 3, &idx_b));

    TEST_ASSERT_EQUAL_UINT64(idx_a, idx_b);
}

static void test_hand_index_rejects_duplicate_cards(void)
{
    int hole[2];
    int board[3];
    uint64_t idx;

    TEST_ASSERT_EQUAL_INT(2, cfr_parse_cards("AsKd", hole, 2));
    TEST_ASSERT_EQUAL_INT(3, cfr_parse_cards("AsJh2h", board, 3));

    TEST_ASSERT_FALSE(cfr_hand_index_for_state(1, hole[0], hole[1], board, 3, &idx));
}

static void test_blueprint_save_load_roundtrip(void)
{
    static CFRBlueprint bp_a;
    static CFRBlueprint bp_b;
    CFRNode *na;
    CFRNode *nb;
    const char *tmp_file = "tests_tmp_blueprint.bin";

    memset(&bp_a, 0, sizeof(bp_a));
    memset(&bp_b, 0, sizeof(bp_b));

    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp_a, 12345ULL));

    na = cfr_blueprint_get_node(&bp_a, 0x1122334455667788ULL, 1, 3);
    TEST_ASSERT_NOT_NULL(na);
    na->regret[0] = 1;
    na->regret[1] = 3;
    na->regret[2] = -4;
    na->strategy_sum[0] = 10.0f;
    na->strategy_sum[1] = 20.0f;
    na->strategy_sum[2] = 30.0f;
    bp_a.compat_hash32 = 0xA1B2C3D4u;
    bp_a.iteration = 77ULL;
    bp_a.total_hands = 123ULL;

    TEST_ASSERT_TRUE(cfr_blueprint_save(&bp_a, tmp_file));
    TEST_ASSERT_TRUE(cfr_blueprint_load(&bp_b, tmp_file));

    nb = cfr_blueprint_get_node(&bp_b, 0x1122334455667788ULL, 0, 3);
    TEST_ASSERT_NOT_NULL(nb);

    TEST_ASSERT_EQUAL_UINT32(bp_a.compat_hash32, bp_b.compat_hash32);
    TEST_ASSERT_EQUAL_UINT64(bp_a.iteration, bp_b.iteration);
    TEST_ASSERT_EQUAL_UINT64(bp_a.total_hands, bp_b.total_hands);
    TEST_ASSERT_EQUAL_INT32(na->regret[0], nb->regret[0]);
    TEST_ASSERT_EQUAL_INT32(na->regret[1], nb->regret[1]);
    TEST_ASSERT_EQUAL_INT32(na->regret[2], nb->regret[2]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, na->strategy_sum[0], nb->strategy_sum[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, na->strategy_sum[1], nb->strategy_sum[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, na->strategy_sum[2], nb->strategy_sum[2]);

    remove(tmp_file);
}

static void test_blueprint_infoset_table_grows_past_initial_capacity(void)
{
    static CFRBlueprint bp;
    uint32_t i;
    CFRNode *node;
    uint32_t target_nodes;

    memset(&bp, 0, sizeof(bp));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 4242ULL));

    target_nodes = (uint32_t)CFR_INFOSET_SOFT_CAP + 4096u;
    for (i = 0u; i < target_nodes; ++i)
    {
        uint64_t key;
        key = ((uint64_t)i * 11400714819323198485ULL) + 1ULL;
        node = cfr_blueprint_get_node(&bp, key, 1, 2);
        TEST_ASSERT_NOT_NULL(node);
    }

    TEST_ASSERT_EQUAL_UINT32(target_nodes, bp.used_node_count);
    TEST_ASSERT_EQUAL_INT((int)target_nodes, cfr_count_used_nodes(&bp));
    TEST_ASSERT_TRUE(bp.node_capacity > (uint32_t)CFR_INFOSET_SOFT_CAP);
}

static void test_blueprint_init_uses_small_eager_allocation_profile(void)
{
    CFRBlueprint bp;

    memset(&bp, 0, sizeof(bp));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 4243ULL));

    TEST_ASSERT_TRUE(bp.node_capacity < (uint32_t)(CFR_INFOSET_SOFT_CAP / 16u));
    TEST_ASSERT_TRUE(bp.key_hash_cap < (uint32_t)(CFR_INFOSET_SOFT_CAP / 16u));
    TEST_ASSERT_TRUE(bp.regret_capacity <= 1024u);
    TEST_ASSERT_TRUE(bp.strategy_capacity <= 1024u);
}

static void test_card_parse_and_format_roundtrip(void)
{
    int cards[7];
    int n;
    char txt0[3];
    char txt1[3];

    n = cfr_parse_cards("AsKd", cards, 7);
    TEST_ASSERT_EQUAL_INT(2, n);

    cfr_card_to_text(cards[0], txt0);
    cfr_card_to_text(cards[1], txt1);

    TEST_ASSERT_EQUAL_STRING("As", txt0);
    TEST_ASSERT_EQUAL_STRING("Kd", txt1);
}

static void test_phe_raw_rank_ordering_is_lower_better(void)
{
    int strong[7];
    int weak[7];
    uint32_t strong_rank;
    uint32_t weak_rank;

    TEST_ASSERT_EQUAL_INT(7, cfr_parse_cards("AsKsQsJsTs9d2c", strong, 7));
    TEST_ASSERT_EQUAL_INT(7, cfr_parse_cards("2c7d9hJcKd3s4h", weak, 7));

    strong_rank = cfr_eval_best_hand(strong);
    weak_rank = cfr_eval_best_hand(weak);

    TEST_ASSERT_TRUE(strong_rank < weak_rank);
}

static void test_range_combo_index_roundtrip(void)
{
    int i;

    for (i = 0; i < CFR_HOLDEM_COMBOS; ++i)
    {
        int a;
        int b;
        int back;
        TEST_ASSERT_TRUE(cfr_range_index_to_cards(i, &a, &b));
        TEST_ASSERT_TRUE(a >= 0 && a < CFR_DECK_SIZE);
        TEST_ASSERT_TRUE(b >= 0 && b < CFR_DECK_SIZE);
        TEST_ASSERT_TRUE(a < b);
        back = cfr_range_cards_to_index(a, b);
        TEST_ASSERT_EQUAL_INT(i, back);
    }
}

static void test_range_uniform_blocked_excludes_card(void)
{
    CFRRange1326 r;
    int blocked[1];
    int i;
    int nonzero;
    double sum;

    blocked[0] = cfr_parse_card_pair('A', 's');
    cfr_range_init_uniform_blocked(&r, blocked, 1);

    nonzero = cfr_range_count_nonzero(&r);
    TEST_ASSERT_EQUAL_INT(1275, nonzero);

    sum = cfr_range_weight_sum(&r);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 1.0f, (float)sum);

    for (i = 0; i < CFR_HOLDEM_COMBOS; ++i)
    {
        int a;
        int b;
        TEST_ASSERT_TRUE(cfr_range_index_to_cards(i, &a, &b));
        if (a == blocked[0] || b == blocked[0])
        {
            TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, (float)r.weight[i]);
        }
    }
}

static void test_range_apply_likelihood_bayes_update(void)
{
    CFRRange1326 r;
    static double like[CFR_HOLDEM_COMBOS];
    int i;
    int idx_a;
    int idx_b;

    cfr_range_init_uniform_blocked(&r, NULL, 0);
    for (i = 0; i < CFR_HOLDEM_COMBOS; ++i)
    {
        like[i] = 0.0;
    }

    idx_a = 10;
    idx_b = 20;
    like[idx_a] = 2.0;
    like[idx_b] = 1.0;

    cfr_range_apply_likelihood(&r, like);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 2.0f / 3.0f, (float)r.weight[idx_a]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f / 3.0f, (float)r.weight[idx_b]);
    TEST_ASSERT_EQUAL_INT(2, cfr_range_count_nonzero(&r));
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 1.0f, (float)cfr_range_weight_sum(&r));
}

static void test_range_apply_blockers_renormalizes(void)
{
    CFRRange1326 r;
    int blocked[2];
    int nonzero;
    int idx_blocked_combo;
    double sum;

    blocked[0] = cfr_parse_card_pair('A', 's');
    blocked[1] = cfr_parse_card_pair('K', 'd');

    cfr_range_init_uniform_blocked(&r, NULL, 0);
    cfr_range_apply_blockers(&r, blocked, 2);

    nonzero = cfr_range_count_nonzero(&r);
    TEST_ASSERT_EQUAL_INT(1225, nonzero);

    idx_blocked_combo = cfr_range_cards_to_index(blocked[0], blocked[1]);
    TEST_ASSERT_TRUE(idx_blocked_combo >= 0);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, (float)r.weight[idx_blocked_combo]);

    sum = cfr_range_weight_sum(&r);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 1.0f, (float)sum);
}

static void test_range_likelihood_uses_blueprint_action_prob(void)
{
    static CFRBlueprint bp;
    CFRHandState st;
    CFRHandState tmp;
    CFRInfoKeyFields kf;
    CFRNode *node;
    int actions[CFR_MAX_ACTIONS];
    int targets[CFR_MAX_ACTIONS];
    int legal_count;
    int call_idx;
    int i;
    int combo_as_kd;
    int combo_qc_jd;
    static double likelihood[CFR_HOLDEM_COMBOS];
    uint64_t key;

    memset(&bp, 0, sizeof(bp));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 999ULL));

    memset(&st, 0, sizeof(st));
    st.street = 0;
    st.dealer = 0;
    st.pot = 10;
    st.to_call = 2;
    st.last_full_raise = 2;
    st.stack[0] = 100;
    st.in_hand[0] = 1;
    st.in_hand[1] = 1;
    st.committed_street[0] = 0;

    legal_count = cfr_get_legal_actions(&st, 0, actions, targets);
    TEST_ASSERT_TRUE(legal_count > 1);

    call_idx = -1;
    for (i = 0; i < legal_count; ++i)
    {
        if (actions[i] == CFR_ACT_CALL_CHECK)
        {
            call_idx = i;
            break;
        }
    }
    TEST_ASSERT_TRUE(call_idx >= 0);

    tmp = st;
    tmp.hole[0][0] = cfr_parse_card_pair('A', 's');
    tmp.hole[0][1] = cfr_parse_card_pair('K', 'd');
    TEST_ASSERT_TRUE(cfr_extract_infoset_fields(&tmp, 0, &kf));
    key = cfr_make_infoset_key(&kf);

    node = cfr_blueprint_get_node(&bp, key, 1, legal_count);
    TEST_ASSERT_NOT_NULL(node);
    memset(node->strategy_sum, 0, sizeof(node->strategy_sum));
    node->strategy_sum[call_idx] = 1000.0f;

    TEST_ASSERT_TRUE(cfr_range_fill_action_likelihood_from_blueprint(&bp,
                                                                     &st,
                                                                     0,
                                                                     CFR_ACT_CALL_CHECK,
                                                                     st.to_call,
                                                                     NULL,
                                                                     0,
                                                                     likelihood) > 0);

    combo_as_kd = cfr_range_cards_to_index(cfr_parse_card_pair('A', 's'), cfr_parse_card_pair('K', 'd'));
    combo_qc_jd = cfr_range_cards_to_index(cfr_parse_card_pair('Q', 'c'), cfr_parse_card_pair('J', 'd'));

    TEST_ASSERT_TRUE(combo_as_kd >= 0);
    TEST_ASSERT_TRUE(combo_qc_jd >= 0);
    TEST_ASSERT_TRUE(likelihood[combo_as_kd] > 0.99);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f / (float)legal_count, (float)likelihood[combo_qc_jd]);
}

static void test_belief_update_player_action_shifts_probability_mass(void)
{
    static CFRBlueprint bp;
    CFRPolicyProvider provider;
    CFRBeliefState belief;
    CFRHandState st;
    CFRHandState tmp;
    CFRInfoKeyFields kf;
    CFRNode *node;
    int actions[CFR_MAX_ACTIONS];
    int targets[CFR_MAX_ACTIONS];
    int legal_count;
    int call_idx;
    int i;
    int combo_as_kd;
    int combo_qc_jd;
    double before_as_kd;
    double before_qc_jd;
    double after_as_kd;
    double after_qc_jd;
    uint64_t key;

    memset(&bp, 0, sizeof(bp));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 1001ULL));
    cfr_policy_provider_init_blueprint(&provider, &bp);

    memset(&st, 0, sizeof(st));
    st.street = 0;
    st.dealer = 0;
    st.pot = 10;
    st.to_call = 2;
    st.last_full_raise = 2;
    st.stack[0] = 100;
    st.in_hand[0] = 1;
    st.in_hand[1] = 1;
    st.committed_street[0] = 0;

    legal_count = cfr_get_legal_actions(&st, 0, actions, targets);
    TEST_ASSERT_TRUE(legal_count > 1);

    call_idx = -1;
    for (i = 0; i < legal_count; ++i)
    {
        if (actions[i] == CFR_ACT_CALL_CHECK)
        {
            call_idx = i;
            break;
        }
    }
    TEST_ASSERT_TRUE(call_idx >= 0);

    tmp = st;
    tmp.hole[0][0] = cfr_parse_card_pair('A', 's');
    tmp.hole[0][1] = cfr_parse_card_pair('K', 'd');
    TEST_ASSERT_TRUE(cfr_extract_infoset_fields(&tmp, 0, &kf));
    key = cfr_make_infoset_key(&kf);

    node = cfr_blueprint_get_node(&bp, key, 1, legal_count);
    TEST_ASSERT_NOT_NULL(node);
    memset(node->strategy_sum, 0, sizeof(node->strategy_sum));
    node->strategy_sum[call_idx] = 1000.0f;

    cfr_belief_init(&belief);
    TEST_ASSERT_TRUE(cfr_belief_init_player_uniform(&belief, 0, NULL, 0));

    combo_as_kd = cfr_range_cards_to_index(cfr_parse_card_pair('A', 's'), cfr_parse_card_pair('K', 'd'));
    combo_qc_jd = cfr_range_cards_to_index(cfr_parse_card_pair('Q', 'c'), cfr_parse_card_pair('J', 'd'));
    TEST_ASSERT_TRUE(combo_as_kd >= 0);
    TEST_ASSERT_TRUE(combo_qc_jd >= 0);

    before_as_kd = cfr_belief_player_combo_prob(&belief, 0, combo_as_kd);
    before_qc_jd = cfr_belief_player_combo_prob(&belief, 0, combo_qc_jd);

    TEST_ASSERT_TRUE(cfr_belief_update_player_action(&belief,
                                                     &provider,
                                                     NULL,
                                                     st.street,
                                                     &st,
                                                     0,
                                                     CFR_ACT_CALL_CHECK,
                                                     st.to_call,
                                                     NULL,
                                                     0));

    after_as_kd = cfr_belief_player_combo_prob(&belief, 0, combo_as_kd);
    after_qc_jd = cfr_belief_player_combo_prob(&belief, 0, combo_qc_jd);

    TEST_ASSERT_TRUE(after_as_kd > before_as_kd);
    TEST_ASSERT_TRUE(after_qc_jd < before_qc_jd);
    TEST_ASSERT_TRUE(after_as_kd > after_qc_jd);
}

static void test_belief_update_prefers_search_sigma_when_available(void)
{
    CFRBeliefState belief_sigma;
    CFRBeliefState belief_bp;
    CFRHandState st;
    CFRHandState tmp;
    CFRInfoKeyFields kf;
    uint64_t key;
    static CFRBlueprint bp;
    static CFRBlueprint sigma;
    CFRNode *node_bp;
    CFRNode *node_sigma;
    int actions[CFR_MAX_ACTIONS];
    int targets[CFR_MAX_ACTIONS];
    int legal_count;
    int call_idx;
    int i;
    int combo_as_kd;
    int combo_qc_jd;
    double sigma_as_kd;
    double bp_as_kd;
    CFRPolicyProvider provider;

    memset(&bp, 0, sizeof(bp));
    memset(&sigma, 0, sizeof(sigma));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 5151ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&sigma, 5152ULL));
    cfr_policy_provider_init_blueprint(&provider, &bp);

    memset(&st, 0, sizeof(st));
    st.dealer = 5;
    st.street = 0;
    st.current_player = 0;
    st.pot = 3;
    st.to_call = 2;
    st.last_full_raise = 2;
    st.stack[0] = 100;
    st.in_hand[0] = 1;
    st.in_hand[1] = 1;
    st.committed_street[0] = 0;

    legal_count = cfr_get_legal_actions(&st, 0, actions, targets);
    TEST_ASSERT_TRUE(legal_count > 1);

    call_idx = -1;
    for (i = 0; i < legal_count; ++i)
    {
        if (actions[i] == CFR_ACT_CALL_CHECK)
        {
            call_idx = i;
            break;
        }
    }
    TEST_ASSERT_TRUE(call_idx >= 0);

    tmp = st;
    tmp.hole[0][0] = cfr_parse_card_pair('A', 's');
    tmp.hole[0][1] = cfr_parse_card_pair('K', 'd');
    TEST_ASSERT_TRUE(cfr_extract_infoset_fields_mode(&tmp, 0, CFR_ABS_MODE_SEARCH, 0, &kf));
    key = cfr_make_infoset_key(&kf);

    node_bp = cfr_blueprint_get_node(&bp, key, 1, legal_count);
    node_sigma = cfr_blueprint_get_node(&sigma, key, 1, legal_count);
    TEST_ASSERT_NOT_NULL(node_bp);
    TEST_ASSERT_NOT_NULL(node_sigma);

    for (i = 0; i < legal_count; ++i)
    {
        node_bp->strategy_sum[i] = (i == call_idx) ? 1.0f : 1000.0f;
        /* Sigma belief path should use averaged strategy, not current regret-matching policy. */
        node_sigma->regret[i] = (i == call_idx) ? -1 : 1000;
        node_sigma->strategy_sum[i] = (i == call_idx) ? 1000.0f : 1.0f;
    }

    cfr_belief_init(&belief_sigma);
    cfr_belief_init(&belief_bp);
    TEST_ASSERT_TRUE(cfr_belief_init_player_uniform(&belief_sigma, 0, NULL, 0));
    TEST_ASSERT_TRUE(cfr_belief_init_player_uniform(&belief_bp, 0, NULL, 0));

    TEST_ASSERT_TRUE(cfr_belief_update_player_action(&belief_sigma,
                                                     &provider,
                                                     &sigma,
                                                     0,
                                                     &st,
                                                     0,
                                                     CFR_ACT_CALL_CHECK,
                                                     st.to_call,
                                                     NULL,
                                                     0));
    TEST_ASSERT_TRUE(cfr_belief_update_player_action(&belief_bp,
                                                     &provider,
                                                     NULL,
                                                     0,
                                                     &st,
                                                     0,
                                                     CFR_ACT_CALL_CHECK,
                                                     st.to_call,
                                                     NULL,
                                                     0));

    combo_as_kd = cfr_range_cards_to_index(cfr_parse_card_pair('A', 's'), cfr_parse_card_pair('K', 'd'));
    combo_qc_jd = cfr_range_cards_to_index(cfr_parse_card_pair('Q', 'c'), cfr_parse_card_pair('J', 'd'));
    TEST_ASSERT_TRUE(combo_as_kd >= 0);
    TEST_ASSERT_TRUE(combo_qc_jd >= 0);

    sigma_as_kd = cfr_belief_player_combo_prob(&belief_sigma, 0, combo_as_kd);
    bp_as_kd = cfr_belief_player_combo_prob(&belief_bp, 0, combo_as_kd);

    TEST_ASSERT_TRUE(sigma_as_kd > bp_as_kd);
    TEST_ASSERT_TRUE(cfr_belief_player_combo_prob(&belief_sigma, 0, combo_as_kd) >
                     cfr_belief_player_combo_prob(&belief_sigma, 0, combo_qc_jd));
}

static void test_belief_round_root_blockers_update_opponent_range(void)
{
    CFRBeliefState belief;
    CFRHandState st;
    int updated_n;
    int combo_with_board;
    int combo_with_hero;
    int combo_clean;
    double p_board;
    double p_hero;
    double p_clean;

    memset(&st, 0, sizeof(st));
    st.board_count = 1;
    st.board[0] = cfr_parse_card_pair('A', 's');
    st.hole[0][0] = cfr_parse_card_pair('K', 'd');
    st.hole[0][1] = cfr_parse_card_pair('Q', 'c');

    cfr_belief_init(&belief);
    TEST_ASSERT_TRUE(cfr_belief_init_player_uniform(&belief, 1, NULL, 0));

    updated_n = cfr_belief_apply_round_root_blockers(&belief, &st, 0);
    TEST_ASSERT_EQUAL_INT(1, updated_n);

    combo_with_board = cfr_range_cards_to_index(cfr_parse_card_pair('A', 's'), cfr_parse_card_pair('2', 'c'));
    combo_with_hero = cfr_range_cards_to_index(cfr_parse_card_pair('K', 'd'), cfr_parse_card_pair('3', 'h'));
    combo_clean = cfr_range_cards_to_index(cfr_parse_card_pair('J', 'd'), cfr_parse_card_pair('T', 'h'));

    p_board = cfr_belief_player_combo_prob(&belief, 1, combo_with_board);
    p_hero = cfr_belief_player_combo_prob(&belief, 1, combo_with_hero);
    p_clean = cfr_belief_player_combo_prob(&belief, 1, combo_clean);

    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, (float)p_board);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, (float)p_hero);
    TEST_ASSERT_TRUE(p_clean > 0.0);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 1.0f, (float)cfr_range_weight_sum(&belief.player_range[1]));
}

static void test_legal_actions_include_dynamic_raises_and_allin(void)
{
    CFRHandState st;
    int actions[CFR_MAX_ACTIONS];
    int targets[CFR_MAX_ACTIONS];
    int n;
    int i;
    int saw_raise_to;
    int saw_all_in;

    memset(&st, 0, sizeof(st));
    st.pot = 30;
    st.to_call = 10;
    st.last_full_raise = 10;
    st.in_hand[0] = 1;
    st.stack[0] = 200;
    st.committed_street[0] = 0;

    n = cfr_get_legal_actions(&st, 0, actions, targets);
    TEST_ASSERT_TRUE(n >= 3);

    saw_raise_to = 0;
    saw_all_in = 0;
    for (i = 0; i < n; ++i)
    {
        if (actions[i] == CFR_ACT_RAISE_TO)
        {
            TEST_ASSERT_TRUE(targets[i] > st.to_call);
            saw_raise_to = 1;
        }
        if (actions[i] == CFR_ACT_ALL_IN)
        {
            TEST_ASSERT_TRUE(targets[i] > st.to_call);
            saw_all_in = 1;
        }
    }

    TEST_ASSERT_TRUE(saw_raise_to);
    TEST_ASSERT_TRUE(saw_all_in);
}

static void test_preflop_action_abstraction_offers_many_raise_sizes(void)
{
    CFRHandState st;
    int actions[CFR_MAX_ACTIONS];
    int targets[CFR_MAX_ACTIONS];
    int n;
    int i;
    int raise_count;

    memset(&st, 0, sizeof(st));
    st.street = 0;
    st.pot = 120;
    st.to_call = 10;
    st.last_full_raise = 10;
    st.in_hand[0] = 1;
    st.stack[0] = 400;
    st.committed_street[0] = 0;

    n = cfr_get_legal_actions(&st, 0, actions, targets);
    TEST_ASSERT_TRUE(n >= 3);

    raise_count = 0;
    for (i = 0; i < n; ++i)
    {
        if (actions[i] == CFR_ACT_RAISE_TO || actions[i] == CFR_ACT_ALL_IN)
        {
            raise_count++;
            TEST_ASSERT_TRUE(targets[i] > st.to_call);
        }
    }

    TEST_ASSERT_TRUE(raise_count >= 8);
}

static void test_raise_action_appends_raise_bucket_history_marker(void)
{
    CFRHandState st;
    int ok;

    memset(&st, 0, sizeof(st));
    st.dealer = 0;
    st.current_player = 0;
    st.to_call = 2;
    st.last_full_raise = 2;
    st.in_hand[0] = 1;
    st.in_hand[1] = 1;
    st.stack[0] = 100;
    st.stack[1] = 100;
    st.needs_action[0] = 1;
    st.needs_action[1] = 1;

    ok = cfr_apply_action(&st, 0, CFR_ACT_RAISE_TO, 8);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(st.history_len >= 3);
    TEST_ASSERT_EQUAL_UINT8((unsigned char)CFR_ACT_RAISE_TO, st.history[0]);
    TEST_ASSERT_EQUAL_UINT8(250u, st.history[1]);
}

static void test_short_allin_does_not_reopen_raising(void)
{
    CFRHandState st;
    int actions[CFR_MAX_ACTIONS];
    int targets[CFR_MAX_ACTIONS];
    int n;
    int i;
    int saw_raise;

    memset(&st, 0, sizeof(st));
    st.dealer = 0;
    st.current_player = 2;
    st.pot = 30;
    st.to_call = 10;
    st.last_full_raise = 10;
    st.full_raise_seq = 1;

    st.in_hand[0] = 1;
    st.in_hand[1] = 1;
    st.in_hand[2] = 1;
    st.stack[0] = 100;
    st.stack[1] = 100;
    st.stack[2] = 5;
    st.committed_street[0] = 10;
    st.committed_street[1] = 10;
    st.committed_street[2] = 10;
    st.needs_action[2] = 1;
    st.acted_this_street[0] = 1;
    st.acted_this_street[1] = 1;
    st.acted_on_full_raise_seq[0] = 1;
    st.acted_on_full_raise_seq[1] = 1;

    TEST_ASSERT_TRUE(cfr_apply_action(&st, 2, CFR_ACT_ALL_IN, 15));
    TEST_ASSERT_EQUAL_INT(1, st.full_raise_seq);
    TEST_ASSERT_EQUAL_INT(1, st.needs_action[0]);

    n = cfr_get_legal_actions(&st, 0, actions, targets);
    TEST_ASSERT_TRUE(n >= 1);

    saw_raise = 0;
    for (i = 0; i < n; ++i)
    {
        if (actions[i] == CFR_ACT_RAISE_TO || actions[i] == CFR_ACT_ALL_IN)
        {
            saw_raise = 1;
        }
    }
    TEST_ASSERT_FALSE(saw_raise);
}

static void test_full_raise_reopens_raising(void)
{
    CFRHandState st;
    int actions[CFR_MAX_ACTIONS];
    int targets[CFR_MAX_ACTIONS];
    int n;
    int i;
    int saw_raise;

    memset(&st, 0, sizeof(st));
    st.dealer = 0;
    st.current_player = 2;
    st.pot = 30;
    st.to_call = 10;
    st.last_full_raise = 10;
    st.full_raise_seq = 1;

    st.in_hand[0] = 1;
    st.in_hand[1] = 1;
    st.in_hand[2] = 1;
    st.stack[0] = 100;
    st.stack[1] = 100;
    st.stack[2] = 30;
    st.committed_street[0] = 10;
    st.committed_street[1] = 10;
    st.committed_street[2] = 10;
    st.needs_action[2] = 1;
    st.acted_this_street[0] = 1;
    st.acted_this_street[1] = 1;
    st.acted_on_full_raise_seq[0] = 1;
    st.acted_on_full_raise_seq[1] = 1;

    TEST_ASSERT_TRUE(cfr_apply_action(&st, 2, CFR_ACT_RAISE_TO, 25));
    TEST_ASSERT_EQUAL_INT(2, st.full_raise_seq);
    TEST_ASSERT_EQUAL_INT(1, st.needs_action[0]);

    n = cfr_get_legal_actions(&st, 0, actions, targets);
    TEST_ASSERT_TRUE(n >= 1);

    saw_raise = 0;
    for (i = 0; i < n; ++i)
    {
        if (actions[i] == CFR_ACT_RAISE_TO || actions[i] == CFR_ACT_ALL_IN)
        {
            saw_raise = 1;
        }
    }
    TEST_ASSERT_TRUE(saw_raise);
}

static void test_short_allin_still_allows_raise_for_player_who_has_not_acted(void)
{
    CFRHandState st;
    int actions[CFR_MAX_ACTIONS];
    int targets[CFR_MAX_ACTIONS];
    int n;
    int i;
    int saw_raise;

    memset(&st, 0, sizeof(st));
    st.dealer = 0;
    st.current_player = 2;
    st.pot = 30;
    st.to_call = 10;
    st.last_full_raise = 10;
    st.full_raise_seq = 1;

    st.in_hand[0] = 1;
    st.in_hand[1] = 1;
    st.in_hand[2] = 1;
    st.stack[0] = 100;
    st.stack[1] = 100;
    st.stack[2] = 5;
    st.committed_street[0] = 0;
    st.committed_street[1] = 10;
    st.committed_street[2] = 10;
    st.needs_action[2] = 1;
    st.acted_this_street[1] = 1;
    st.acted_on_full_raise_seq[1] = 1;

    TEST_ASSERT_TRUE(cfr_apply_action(&st, 2, CFR_ACT_ALL_IN, 15));
    n = cfr_get_legal_actions(&st, 0, actions, targets);
    TEST_ASSERT_TRUE(n >= 1);

    saw_raise = 0;
    for (i = 0; i < n; ++i)
    {
        if (actions[i] == CFR_ACT_RAISE_TO || actions[i] == CFR_ACT_ALL_IN)
        {
            saw_raise = 1;
        }
    }
    TEST_ASSERT_TRUE(saw_raise);
}

static void test_min_raise_target_respects_last_full_raise(void)
{
    CFRHandState st;
    int min_target;
    int actions[CFR_MAX_ACTIONS];
    int targets[CFR_MAX_ACTIONS];
    int n;
    int i;

    memset(&st, 0, sizeof(st));
    st.to_call = 10;
    st.last_full_raise = 6;
    st.in_hand[0] = 1;
    st.stack[0] = 100;
    st.committed_street[0] = 0;

    min_target = cfr_compute_min_raise_target(&st, 0);
    TEST_ASSERT_EQUAL_INT(16, min_target);

    n = cfr_get_legal_actions(&st, 0, actions, targets);
    TEST_ASSERT_TRUE(n >= 1);
    for (i = 0; i < n; ++i)
    {
        if (actions[i] == CFR_ACT_RAISE_TO)
        {
            TEST_ASSERT_TRUE(targets[i] >= min_target);
        }
    }
}

static void test_offtree_raise_maps_to_nearest_legal_target(void)
{
    CFRHandState st;
    int mapped_action;
    int mapped_target;
    int off_tree;

    memset(&st, 0, sizeof(st));
    st.pot = 30;
    st.to_call = 10;
    st.last_full_raise = 10;
    st.in_hand[0] = 1;
    st.stack[0] = 200;
    st.committed_street[0] = 0;

    TEST_ASSERT_TRUE(cfr_translate_action_to_legal(&st, 0, CFR_ACT_RAISE_TO, 43, &mapped_action, &mapped_target, &off_tree));
    TEST_ASSERT_TRUE(off_tree);
    TEST_ASSERT_TRUE(mapped_action == CFR_ACT_RAISE_TO || mapped_action == CFR_ACT_ALL_IN);
    TEST_ASSERT_TRUE(mapped_target > st.to_call);
}

static void test_offtree_raise_pseudo_harmonic_maps_between_adjacent_targets(void)
{
    CFRHandState st;
    int actions[CFR_MAX_ACTIONS];
    int targets[CFR_MAX_ACTIONS];
    int raise_targets[CFR_MAX_ACTIONS];
    int n;
    int i;
    int rn;
    int low_t;
    int high_t;
    int requested;
    int low_hits;
    int high_hits;
    uint64_t rng;

    memset(&st, 0, sizeof(st));
    st.pot = 30;
    st.to_call = 10;
    st.last_full_raise = 10;
    st.in_hand[0] = 1;
    st.stack[0] = 200;
    st.committed_street[0] = 0;

    n = cfr_get_legal_actions(&st, 0, actions, targets);
    TEST_ASSERT_TRUE(n > 0);

    rn = 0;
    for (i = 0; i < n; ++i)
    {
        if (actions[i] == CFR_ACT_RAISE_TO || actions[i] == CFR_ACT_ALL_IN)
        {
            raise_targets[rn++] = targets[i];
        }
    }
    TEST_ASSERT_TRUE(rn >= 2);

    low_t = raise_targets[0];
    high_t = raise_targets[1];
    TEST_ASSERT_TRUE(high_t > low_t);
    requested = (low_t + high_t) / 2;
    if (requested <= low_t)
    {
        requested = low_t + 1;
    }
    if (requested >= high_t)
    {
        requested = high_t - 1;
    }

    low_hits = 0;
    high_hits = 0;
    rng = 123456789ULL;
    for (i = 0; i < 128; ++i)
    {
        int mapped_action;
        int mapped_target;
        int off_tree;
        TEST_ASSERT_TRUE(cfr_translate_action_to_legal_pseudo_harmonic(&st,
                                                                       0,
                                                                       CFR_ACT_RAISE_TO,
                                                                       requested,
                                                                       &rng,
                                                                       &mapped_action,
                                                                       &mapped_target,
                                                                       &off_tree));
        TEST_ASSERT_TRUE(off_tree);
        TEST_ASSERT_TRUE(mapped_action == CFR_ACT_RAISE_TO || mapped_action == CFR_ACT_ALL_IN);
        TEST_ASSERT_TRUE(mapped_target == low_t || mapped_target == high_t);
        if (mapped_target == low_t)
        {
            low_hits++;
        }
        else if (mapped_target == high_t)
        {
            high_hits++;
        }
    }

    TEST_ASSERT_TRUE(low_hits > 0);
    TEST_ASSERT_TRUE(high_hits > 0);
}

static int cfr_total_chips(const CFRHandState *st)
{
    int total;
    int i;

    total = st->pot;
    for (i = 0; i < CFR_MAX_PLAYERS; ++i)
    {
        total += st->stack[i];
    }
    return total;
}

static double cfr_abs_d(double x)
{
    return (x < 0.0) ? -x : x;
}

static double cfr_mean_strategy_l1_intersection(const CFRBlueprint *a, const CFRBlueprint *b)
{
    double sum_l1;
    int count;
    int i;

    sum_l1 = 0.0;
    count = 0;

    for (i = 0; i < (int)a->used_node_count; ++i)
    {
        if (a->nodes[i].used)
        {
            CFRNode *nb;
            int n;
            float sa[CFR_MAX_ACTIONS];
            float sb[CFR_MAX_ACTIONS];
            int k;
            double l1;

            nb = cfr_blueprint_get_node((CFRBlueprint *)b, a->nodes[i].key, 0, 1);
            if (nb == NULL)
            {
                continue;
            }

            n = a->nodes[i].action_count;
            if (nb->action_count < n)
            {
                n = nb->action_count;
            }
            if (n < 1)
            {
                continue;
            }
            if (n > CFR_MAX_ACTIONS)
            {
                n = CFR_MAX_ACTIONS;
            }

            cfr_compute_average_strategy_n(&a->nodes[i], n, sa);
            cfr_compute_average_strategy_n(nb, n, sb);

            l1 = 0.0;
            for (k = 0; k < n; ++k)
            {
                l1 += cfr_abs_d((double)sa[k] - (double)sb[k]);
            }
            sum_l1 += l1 / (double)n;
            count++;
        }
    }

    if (count <= 0)
    {
        return 0.0;
    }
    return sum_l1 / (double)count;
}

static void test_total_chips_conserved_through_hand(void)
{
    CFRHandState st;
    int actions[CFR_MAX_ACTIONS];
    int targets[CFR_MAX_ACTIONS];
    uint64_t rng;
    int baseline_total;
    int guard;

    rng = 123456789ULL;
    cfr_init_hand(&st, 0, &rng);
    baseline_total = CFR_MAX_PLAYERS * CFR_START_STACK;
    TEST_ASSERT_EQUAL_INT(baseline_total, cfr_total_chips(&st));

    guard = 256;
    while (!st.is_terminal && guard-- > 0)
    {
        int p;
        int n;
        int chosen;
        int i;
        int before_total;

        p = st.current_player;
        TEST_ASSERT_TRUE(p >= 0 && p < CFR_MAX_PLAYERS);

        n = cfr_get_legal_actions(&st, p, actions, targets);
        TEST_ASSERT_TRUE(n > 0);

        chosen = 0;
        for (i = 0; i < n; ++i)
        {
            if (actions[i] == CFR_ACT_CALL_CHECK)
            {
                chosen = i;
                break;
            }
        }

        before_total = cfr_total_chips(&st);
        TEST_ASSERT_TRUE(cfr_apply_action(&st, p, actions[chosen], targets[chosen]));
        TEST_ASSERT_EQUAL_INT(before_total, cfr_total_chips(&st));
        TEST_ASSERT_EQUAL_INT(baseline_total, cfr_total_chips(&st));
    }

    TEST_ASSERT_TRUE(guard > 0);
    cfr_resolve_terminal(&st);
    TEST_ASSERT_EQUAL_INT(0, st.pot);
    TEST_ASSERT_EQUAL_INT(baseline_total, cfr_total_chips(&st));
}

static void test_showdown_resolves_side_pots_correctly(void)
{
    CFRHandState st;
    int board[5];
    int h0[2];
    int h1[2];
    int h2[2];

    memset(&st, 0, sizeof(st));

    TEST_ASSERT_EQUAL_INT(5, cfr_parse_cards("2c3d4h5s9c", board, 5));
    TEST_ASSERT_EQUAL_INT(2, cfr_parse_cards("AsKd", h0, 2));
    TEST_ASSERT_EQUAL_INT(2, cfr_parse_cards("KcKh", h1, 2));
    TEST_ASSERT_EQUAL_INT(2, cfr_parse_cards("7c7d", h2, 2));

    st.board_count = 5;
    st.board[0] = board[0];
    st.board[1] = board[1];
    st.board[2] = board[2];
    st.board[3] = board[3];
    st.board[4] = board[4];

    st.in_hand[0] = 1;
    st.in_hand[1] = 1;
    st.in_hand[2] = 1;
    st.hole[0][0] = h0[0];
    st.hole[0][1] = h0[1];
    st.hole[1][0] = h1[0];
    st.hole[1][1] = h1[1];
    st.hole[2][0] = h2[0];
    st.hole[2][1] = h2[1];

    st.contributed_total[0] = 50;
    st.contributed_total[1] = 100;
    st.contributed_total[2] = 100;
    st.pot = 250;

    cfr_resolve_terminal(&st);

    TEST_ASSERT_EQUAL_INT(150, st.stack[0]);
    TEST_ASSERT_EQUAL_INT(100, st.stack[1]);
    TEST_ASSERT_EQUAL_INT(0, st.stack[2]);
    TEST_ASSERT_EQUAL_INT(0, st.pot);
}

static void test_trainer_node_growth_and_no_nan_state(void)
{
    static CFRBlueprint bp;
    CFRTrainOptions opt;
    int used_after_first;
    int used_after_second;
    int i;

    memset(&bp, 0, sizeof(bp));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 987654321ULL));

    memset(&opt, 0, sizeof(opt));
    opt.threads = 1;
    opt.chunk_iters = 1;
    opt.samples_per_player = 1;
    opt.enable_linear_discount = 0;
    opt.enable_pruning = 0;

    cfr_run_iterations(&bp, 2, &opt);
    used_after_first = cfr_count_used_nodes(&bp);
    TEST_ASSERT_TRUE(used_after_first > 0);

    cfr_run_iterations(&bp, 2, &opt);
    used_after_second = cfr_count_used_nodes(&bp);
    TEST_ASSERT_TRUE(used_after_second >= used_after_first);

    for (i = 0; i < (int)bp.used_node_count; ++i)
    {
        if (bp.nodes[i].used)
        {
            int a;

            TEST_ASSERT_TRUE(bp.nodes[i].action_count >= 1);
            TEST_ASSERT_TRUE(bp.nodes[i].action_count <= CFR_MAX_ACTIONS);
            for (a = 0; a < bp.nodes[i].action_count; ++a)
            {
                int32_t r;
                float s;

                r = bp.nodes[i].regret[a];
                s = bp.nodes[i].strategy_sum[a];
                TEST_ASSERT_TRUE(r >= INT32_MIN);
                TEST_ASSERT_TRUE(s == s);
            }
        }
    }
}

static void cfr_assert_blueprints_equal(const CFRBlueprint *a, const CFRBlueprint *b, float tol)
{
    uint32_t i;

    TEST_ASSERT_EQUAL_UINT32(a->used_node_count, b->used_node_count);
    for (i = 0u; i < a->used_node_count; ++i)
    {
        const CFRNode *na;
        CFRNode *nb;
        int k;

        na = &a->nodes[i];
        TEST_ASSERT_TRUE(na->used);
        nb = cfr_blueprint_get_node((CFRBlueprint *)b, na->key, 0, na->action_count);
        TEST_ASSERT_NOT_NULL(nb);
        TEST_ASSERT_EQUAL_INT(na->action_count, nb->action_count);
        TEST_ASSERT_EQUAL_UINT8(na->street_hint, nb->street_hint);
        for (k = 0; k < na->action_count; ++k)
        {
            TEST_ASSERT_EQUAL_INT32(na->regret[k], nb->regret[k]);
            TEST_ASSERT_FLOAT_WITHIN(tol, na->strategy_sum[k], nb->strategy_sum[k]);
        }
    }
}

static void test_seeded_parallel_training_is_deterministic(void)
{
#ifdef _WIN32
    static CFRBlueprint a;
    static CFRBlueprint b;
    CFRTrainOptions opt;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    TEST_ASSERT_TRUE(cfr_blueprint_init(&a, 555555ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&b, 555555ULL));

    memset(&opt, 0, sizeof(opt));
    opt.threads = 2;
    opt.chunk_iters = 2;
    opt.samples_per_player = 1;
    opt.enable_linear_discount = 0;
    opt.enable_pruning = 0;

    cfr_run_iterations(&a, 4, &opt);
    cfr_run_iterations(&b, 4, &opt);

    TEST_ASSERT_EQUAL_UINT64(a.iteration, b.iteration);
    TEST_ASSERT_EQUAL_UINT64(a.total_hands, b.total_hands);
    TEST_ASSERT_EQUAL_UINT64(a.rng_state, b.rng_state);

    cfr_assert_blueprints_equal(&a, &b, 1e-6f);
#else
    TEST_IGNORE_MESSAGE("parallel deterministic test is Windows-only in this project");
#endif
}

static void test_seeded_parallel_training_is_deterministic_24_threads(void)
{
#ifdef _WIN32
    static CFRBlueprint a;
    static CFRBlueprint b;
    CFRTrainOptions opt;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    TEST_ASSERT_TRUE(cfr_blueprint_init(&a, 2026027ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&b, 2026027ULL));

    memset(&opt, 0, sizeof(opt));
    opt.threads = 24;
    opt.chunk_iters = 96;
    opt.samples_per_player = 1;
    opt.enable_linear_discount = 0;
    opt.enable_pruning = 0;

    cfr_run_iterations(&a, 96, &opt);
    cfr_run_iterations(&b, 96, &opt);

    TEST_ASSERT_EQUAL_UINT64(a.iteration, b.iteration);
    TEST_ASSERT_EQUAL_UINT64(a.total_hands, b.total_hands);
    TEST_ASSERT_EQUAL_UINT64(a.rng_state, b.rng_state);

    cfr_assert_blueprints_equal(&a, &b, 1e-6f);
#else
    TEST_IGNORE_MESSAGE("parallel deterministic test is Windows-only in this project");
#endif
}

static void test_seeded_parallel_training_is_deterministic_sharded(void)
{
#ifdef _WIN32
    static CFRBlueprint a;
    static CFRBlueprint b;
    CFRTrainOptions opt;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    TEST_ASSERT_TRUE(cfr_blueprint_init(&a, 2026123ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&b, 2026123ULL));

    memset(&opt, 0, sizeof(opt));
    opt.threads = 24;
    opt.parallel_mode = CFR_PARALLEL_MODE_SHARDED;
    opt.chunk_iters = 96;
    opt.samples_per_player = 1;
    opt.enable_linear_discount = 0;
    opt.enable_pruning = 0;

    cfr_run_iterations(&a, 96, &opt);
    cfr_run_iterations(&b, 96, &opt);

    TEST_ASSERT_EQUAL_UINT64(a.iteration, b.iteration);
    TEST_ASSERT_EQUAL_UINT64(a.total_hands, b.total_hands);
    TEST_ASSERT_EQUAL_UINT64(a.rng_state, b.rng_state);

    cfr_assert_blueprints_equal(&a, &b, 1e-6f);
#else
    TEST_IGNORE_MESSAGE("parallel deterministic test is Windows-only in this project");
#endif
}

static void test_parallel_modes_deterministic_and_sharded_match(void)
{
#ifdef _WIN32
    static CFRBlueprint a;
    static CFRBlueprint b;
    CFRTrainOptions opt;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    TEST_ASSERT_TRUE(cfr_blueprint_init(&a, 2026124ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&b, 2026124ULL));

    memset(&opt, 0, sizeof(opt));
    opt.threads = 16;
    opt.parallel_mode = CFR_PARALLEL_MODE_DETERMINISTIC;
    opt.chunk_iters = 96;
    opt.samples_per_player = 1;
    opt.enable_linear_discount = 0;
    opt.enable_pruning = 0;
    cfr_run_iterations(&a, 96, &opt);

    opt.parallel_mode = CFR_PARALLEL_MODE_SHARDED;
    cfr_run_iterations(&b, 96, &opt);

    TEST_ASSERT_EQUAL_UINT64(a.iteration, b.iteration);
    TEST_ASSERT_EQUAL_UINT64(a.total_hands, b.total_hands);
    TEST_ASSERT_EQUAL_UINT64(a.rng_state, b.rng_state);
    cfr_assert_blueprints_equal(&a, &b, 1e-6f);
#else
    TEST_IGNORE_MESSAGE("parallel deterministic test is Windows-only in this project");
#endif
}

static void test_seeded_resume_equivalence_single_thread(void)
{
    static CFRBlueprint a;
    static CFRBlueprint b;
    CFRTrainOptions opt;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    TEST_ASSERT_TRUE(cfr_blueprint_init(&a, 2026001ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&b, 2026001ULL));

    memset(&opt, 0, sizeof(opt));
    opt.threads = 1;
    opt.chunk_iters = 1;
    opt.samples_per_player = 1;
    opt.enable_linear_discount = 0;
    opt.enable_pruning = 0;

    cfr_run_iterations(&a, 4, &opt);
    cfr_run_iterations(&b, 2, &opt);
    cfr_run_iterations(&b, 2, &opt);

    TEST_ASSERT_EQUAL_UINT64(a.iteration, b.iteration);
    TEST_ASSERT_EQUAL_UINT64(a.total_hands, b.total_hands);
    TEST_ASSERT_EQUAL_UINT64(a.rng_state, b.rng_state);

    cfr_assert_blueprints_equal(&a, &b, 1e-6f);
}

static void test_convergence_sanity_strategy_drift_tapers(void)
{
    static CFRBlueprint bp;
    static CFRBlueprint snap_prev;
    CFRTrainOptions opt;
    double d1;
    double d2;

    memset(&bp, 0, sizeof(bp));
    memset(&snap_prev, 0, sizeof(snap_prev));

    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 808080ULL));

    memset(&opt, 0, sizeof(opt));
    opt.threads = 1;
    opt.chunk_iters = 1;
    opt.samples_per_player = 1;
    opt.enable_linear_discount = 0;
    opt.enable_pruning = 0;

    cfr_run_iterations(&bp, 8, &opt);
    TEST_ASSERT_TRUE(cfr_blueprint_copy_from(&snap_prev, &bp));
    cfr_run_iterations(&bp, 8, &opt);
    d1 = cfr_mean_strategy_l1_intersection(&snap_prev, &bp);

    TEST_ASSERT_TRUE(cfr_blueprint_copy_from(&snap_prev, &bp));
    cfr_run_iterations(&bp, 8, &opt);
    d2 = cfr_mean_strategy_l1_intersection(&snap_prev, &bp);

    TEST_ASSERT_TRUE(d1 > 0.0);
    TEST_ASSERT_TRUE(d2 >= 0.0);
}

static int cfr_count_nonzero_strategy_sum(const CFRBlueprint *bp)
{
    int i;
    int k;
    int n;

    n = 0;
    for (i = 0; i < (int)bp->used_node_count; ++i)
    {
        if (!cfr_node_is_used(&bp->nodes[i]))
        {
            continue;
        }
        for (k = 0; k < bp->nodes[i].action_count; ++k)
        {
            if (bp->nodes[i].strategy_sum[k] != 0.0f)
            {
                n++;
            }
        }
    }
    return n;
}

static void cfr_test_advance_to_flop_by_calling(CFRHandState *st)
{
    int actions[CFR_MAX_ACTIONS];
    int targets[CFR_MAX_ACTIONS];
    int guard;

    guard = 64;
    while (!st->is_terminal && st->street == 0 && guard-- > 0)
    {
        int p;
        int n;
        int i;
        int chosen;

        p = st->current_player;
        TEST_ASSERT_TRUE(p >= 0 && p < CFR_MAX_PLAYERS);
        n = cfr_get_legal_actions(st, p, actions, targets);
        TEST_ASSERT_TRUE(n > 0);

        chosen = -1;
        for (i = 0; i < n; ++i)
        {
            if (actions[i] == CFR_ACT_CALL_CHECK)
            {
                chosen = i;
                break;
            }
        }
        TEST_ASSERT_TRUE(chosen >= 0);
        TEST_ASSERT_TRUE(cfr_apply_action(st, p, actions[chosen], targets[chosen]));
    }

    TEST_ASSERT_FALSE(st->is_terminal);
    TEST_ASSERT_EQUAL_INT(1, st->street);
    TEST_ASSERT_TRUE(st->current_player >= 0 && st->current_player < CFR_MAX_PLAYERS);
}

static void test_schedule_helpers_match_expected_cadence(void)
{
    CFRTrainOptions opt;

    memset(&opt, 0, sizeof(opt));
    opt.enable_pruning = 1;
    opt.prune_start_iter = 10ULL;
    opt.prune_full_every_iters = 20ULL;
    opt.strategy_interval = 4ULL;

    TEST_ASSERT_TRUE(cfr_is_full_traversal_iteration(&opt, 0ULL));
    TEST_ASSERT_TRUE(cfr_is_full_traversal_iteration(&opt, 10ULL));
    TEST_ASSERT_FALSE(cfr_is_full_traversal_iteration(&opt, 11ULL));
    TEST_ASSERT_TRUE(cfr_is_full_traversal_iteration(&opt, 30ULL));

    TEST_ASSERT_FALSE(cfr_should_accumulate_strategy(&opt, 0ULL));
    TEST_ASSERT_FALSE(cfr_should_accumulate_strategy(&opt, 1ULL));
    TEST_ASSERT_FALSE(cfr_should_accumulate_strategy(&opt, 2ULL));
    TEST_ASSERT_TRUE(cfr_should_accumulate_strategy(&opt, 3ULL));
}

static void test_time_discount_schedule_applies_event_cadence_and_stop(void)
{
    CFRTrainOptions opt;
    CFRBlueprint bp;

    memset(&opt, 0, sizeof(opt));
    memset(&bp, 0, sizeof(bp));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 7777ULL));

    opt.enable_linear_discount = 1;
    opt.linear_discount_scale = 1.0;
    opt.discount_every_seconds = 600ULL;
    opt.discount_stop_seconds = 24000ULL;
    opt.enable_preflop_avg = 1;

    cfr_train_apply_time_discount_events(&bp, &opt, 0ULL, 599ULL);
    TEST_ASSERT_EQUAL_UINT64(0ULL, bp.discount_events_applied);
    TEST_ASSERT_FLOAT_WITHIN(1e-9f, 1.0f, (float)bp.lazy_discount_scale);

    cfr_train_apply_time_discount_events(&bp, &opt, 599ULL, 600ULL);
    TEST_ASSERT_EQUAL_UINT64(1ULL, bp.discount_events_applied);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, (float)bp.lazy_discount_scale);

    cfr_train_apply_time_discount_events(&bp, &opt, 600ULL, 1800ULL);
    TEST_ASSERT_EQUAL_UINT64(3ULL, bp.discount_events_applied);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, (float)bp.lazy_discount_scale);

    cfr_train_apply_time_discount_events(&bp, &opt, 1800ULL, 25000ULL);
    TEST_ASSERT_EQUAL_UINT64(40ULL, bp.discount_events_applied);
}

static void test_pluribus_profile_warmup_gates_preflop_avg_and_snapshots(void)
{
    CFRTrainOptions opt;

    memset(&opt, 0, sizeof(opt));
    cfr_apply_pluribus_profile(&opt);

    TEST_ASSERT_TRUE(opt.enable_preflop_avg);
    TEST_ASSERT_EQUAL_UINT64(48000ULL, opt.avg_start_seconds);
    TEST_ASSERT_EQUAL_UINT64(48000ULL, opt.warmup_seconds);
    TEST_ASSERT_EQUAL_UINT64(48000ULL, opt.snapshot_start_seconds);

    TEST_ASSERT_FALSE(cfr_train_preflop_avg_phase_active(&opt, 47999ULL));
    TEST_ASSERT_TRUE(cfr_train_preflop_avg_phase_active(&opt, 48000ULL));
    TEST_ASSERT_FALSE(cfr_train_snapshot_phase_active(&opt, 47999ULL));
    TEST_ASSERT_TRUE(cfr_train_snapshot_phase_active(&opt, 48000ULL));
}

static void test_strategy_interval_gates_strategy_sum_updates(void)
{
    static CFRBlueprint bp;
    CFRTrainOptions opt;

    memset(&bp, 0, sizeof(bp));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 31337ULL));

    memset(&opt, 0, sizeof(opt));
    opt.threads = 1;
    opt.chunk_iters = 1;
    opt.samples_per_player = 1;
    opt.strategy_interval = 2ULL;
    opt.enable_linear_discount = 0;
    opt.enable_pruning = 0;

    cfr_run_iterations(&bp, 1, &opt);
    TEST_ASSERT_EQUAL_INT(0, cfr_count_nonzero_strategy_sum(&bp));

    cfr_run_iterations(&bp, 1, &opt);
    TEST_ASSERT_TRUE(cfr_count_nonzero_strategy_sum(&bp) > 0);
}

static void test_training_accumulates_preflop_strategy_only(void)
{
    static CFRBlueprint bp;
    CFRTrainOptions opt;
    int i;
    int k;
    int pre_nonzero;
    int post_nonzero;
    int saw_postflop;

    memset(&bp, 0, sizeof(bp));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 8081ULL));

    memset(&opt, 0, sizeof(opt));
    opt.threads = 1;
    opt.chunk_iters = 1;
    opt.samples_per_player = 1;
    opt.strategy_interval = 1ULL;
    opt.enable_linear_discount = 0;
    opt.enable_pruning = 0;

    cfr_run_iterations(&bp, 32ULL, &opt);

    pre_nonzero = 0;
    post_nonzero = 0;
    saw_postflop = 0;
    for (i = 0; i < (int)bp.used_node_count; ++i)
    {
        if (!cfr_node_is_used(&bp.nodes[i]))
        {
            continue;
        }
        if (bp.nodes[i].street_hint == 0u)
        {
            for (k = 0; k < bp.nodes[i].action_count; ++k)
            {
                if (bp.nodes[i].strategy_sum[k] != 0.0f)
                {
                    pre_nonzero++;
                    break;
                }
            }
        }
        else if (bp.nodes[i].street_hint >= 1u && bp.nodes[i].street_hint <= 3u)
        {
            saw_postflop = 1;
            for (k = 0; k < bp.nodes[i].action_count; ++k)
            {
                if (bp.nodes[i].strategy_sum[k] != 0.0f)
                {
                    post_nonzero++;
                    break;
                }
            }
        }
    }

    TEST_ASSERT_TRUE(pre_nonzero > 0);
    TEST_ASSERT_TRUE(saw_postflop);
    TEST_ASSERT_EQUAL_INT(0, post_nonzero);
}

static void test_postflop_sampled_opponent_path_does_not_create_read_only_root_node(void)
{
    CFRBlueprint bp;
    CFRHandState st;
    CFRInfoKeyFields kf;
    CFRTrainStepConfig cfg;
    uint64_t rng;
    uint64_t key;
    int traverser;

    memset(&bp, 0, sizeof(bp));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 515151ULL));

    rng = 20260306ULL;
    cfr_init_hand(&st, 0, &rng);
    cfr_test_advance_to_flop_by_calling(&st);

    traverser = cfr_find_next_actor_from(&st, st.current_player);
    TEST_ASSERT_TRUE(traverser >= 0 && traverser < CFR_MAX_PLAYERS);
    TEST_ASSERT_TRUE(traverser != st.current_player);

    memset(&cfg, 0, sizeof(cfg));
    cfg.iteration_index = 0ULL;
    cfg.strategy_weight = 1.0;
    cfg.accumulate_strategy = 0;

    TEST_ASSERT_TRUE(cfr_extract_infoset_fields(&st, st.current_player, &kf));
    key = cfr_make_infoset_key(&kf);
    TEST_ASSERT_NULL(cfr_blueprint_get_node_ex(&bp, key, 0, 1, kf.street));

    (void)cfr_traverse(&bp, &st, traverser, &cfg);

    TEST_ASSERT_NULL(cfr_blueprint_get_node_ex(&bp, key, 0, 1, kf.street));
    TEST_ASSERT_TRUE(bp.used_node_count > 0u);

    cfr_blueprint_release(&bp);
}

static void test_preflop_infosets_use_generic_sparse_hash_path(void)
{
    CFRBlueprint bp;
    CFRInfoKeyFields kf;
    CFRNode *a;
    CFRNode *b;
    uint64_t key_a;
    uint64_t key_b;

    memset(&bp, 0, sizeof(bp));
    memset(&kf, 0, sizeof(kf));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 7777ULL));

    kf.street = 0;
    kf.position = 1;
    kf.pot_bucket = 2;
    kf.to_call_bucket = 1;
    kf.active_players = 6;
    kf.history_hash = 0x99887766u;

    kf.hand_index = 5ULL;
    key_a = cfr_make_infoset_key(&kf);
    a = cfr_blueprint_get_node_ex(&bp, key_a, 1, 3, 0);
    TEST_ASSERT_NOT_NULL(a);

    kf.hand_index = 17ULL;
    key_b = cfr_make_infoset_key(&kf);
    b = cfr_blueprint_get_node_ex(&bp, key_b, 1, 3, 0);
    TEST_ASSERT_NOT_NULL(b);

    TEST_ASSERT_TRUE(key_a != key_b);
    TEST_ASSERT_EQUAL_UINT32(2u, bp.used_node_count);
    TEST_ASSERT_EQUAL_UINT32(2u, bp.key_hash_used_count);
    TEST_ASSERT_NOT_NULL(cfr_blueprint_get_node_ex(&bp, key_a, 0, 3, 0));
    TEST_ASSERT_NOT_NULL(cfr_blueprint_get_node_ex(&bp, key_b, 0, 3, 0));

    cfr_blueprint_release(&bp);
}

static void test_blueprint_rehash_preserves_preflop_and_postflop_nodes(void)
{
    CFRBlueprint bp;
    CFRInfoKeyFields kf;
    uint64_t pre_key;
    uint64_t post_key;
    uint32_t i;
    uint32_t post_count;

    memset(&bp, 0, sizeof(bp));
    memset(&kf, 0, sizeof(kf));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 9991ULL));

    kf.street = 0;
    kf.position = 1;
    kf.hand_index = 17ULL;
    kf.pot_bucket = 2;
    kf.to_call_bucket = 1;
    kf.active_players = 6;
    for (i = 0u; i < 1400u; ++i)
    {
        CFRNode *node;

        kf.history_hash = 0x1000u + i;
        pre_key = cfr_make_infoset_key(&kf);
        node = cfr_blueprint_get_node_ex(&bp, pre_key, 1, 3, 0);
        TEST_ASSERT_NOT_NULL(node);
    }

    post_count = 0u;
    memset(&kf, 0, sizeof(kf));
    kf.street = 2;
    kf.position = 3;
    kf.hand_index = 123456ULL;
    kf.pot_bucket = 4;
    kf.to_call_bucket = 2;
    kf.active_players = 4;
    for (i = 0u; i < 700u; ++i)
    {
        CFRNode *node;

        kf.history_hash = 0x2000u + i;
        post_key = cfr_make_infoset_key(&kf);
        node = cfr_blueprint_get_node_ex(&bp, post_key, 1, 2, 2);
        TEST_ASSERT_NOT_NULL(node);
        post_count++;
    }

    TEST_ASSERT_TRUE(cfr_blueprint_rehash(&bp, 4096u));
    TEST_ASSERT_EQUAL_UINT32(4096u, bp.key_hash_cap);
    TEST_ASSERT_EQUAL_UINT32(1400u + post_count, bp.key_hash_used_count);
    TEST_ASSERT_NOT_NULL(cfr_blueprint_get_node_ex(&bp, pre_key, 0, 3, 0));
    TEST_ASSERT_NOT_NULL(cfr_blueprint_get_node_ex(&bp, post_key, 0, 2, 2));

    cfr_blueprint_release(&bp);
}

static void test_overlay_preflop_read_only_lookup_returns_base_node(void)
{
    CFRBlueprint base;
    CFRBlueprint worker;
    CFRInfoKeyFields kf;
    CFRNode *base_node;
    CFRNode *found;
    uint64_t key;

    memset(&base, 0, sizeof(base));
    memset(&worker, 0, sizeof(worker));
    memset(&kf, 0, sizeof(kf));

    TEST_ASSERT_TRUE(cfr_blueprint_init(&base, 515152ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&worker, 515153ULL));

    kf.street = 0;
    kf.position = 2;
    kf.hand_index = 17ULL;
    kf.pot_bucket = 3;
    kf.to_call_bucket = 1;
    kf.active_players = 6;
    kf.history_hash = 0x12345678u;
    key = cfr_make_infoset_key(&kf);

    base_node = cfr_blueprint_get_node_ex(&base, key, 1, 4, 0);
    TEST_ASSERT_NOT_NULL(base_node);
    base_node->regret[0] = 7;
    base_node->regret[1] = -3;

    worker.overlay_base = &base;
    found = cfr_blueprint_get_node_ex(&worker, key, 0, 4, 0);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_TRUE(found == base_node);
    TEST_ASSERT_EQUAL_UINT32(0u, worker.used_node_count);
    TEST_ASSERT_EQUAL_INT32(7, found->regret[0]);
    TEST_ASSERT_EQUAL_INT32(-3, found->regret[1]);

    cfr_blueprint_release(&worker);
    cfr_blueprint_release(&base);
}

static void test_preflop_avg_sampled_mode_updates_single_action_counter(void)
{
    CFRNode node;
    CFRTrainStepConfig cfg;
    CFRTrainOptions opt;
    float strategy[CFR_MAX_ACTIONS];
    float storage[CFR_MAX_ACTIONS];
    uint64_t rng;
    int i;
    int nonzero_n;
    float sum;

    memset(&node, 0, sizeof(node));
    memset(&cfg, 0, sizeof(cfg));
    memset(&opt, 0, sizeof(opt));
    memset(strategy, 0, sizeof(strategy));
    memset(storage, 0, sizeof(storage));

    node.action_count = 3;
    node.street_hint = 0u;
    node.strategy_sum = storage;

    strategy[0] = 0.2f;
    strategy[1] = 0.3f;
    strategy[2] = 0.5f;

    cfg.opt = &opt;
    cfg.strategy_weight = 1.0;
    opt.preflop_avg_sampled = 1;

    rng = 12345ULL;
    cfr_accumulate_node_strategy(&node, strategy, 3, &cfg, &rng);

    nonzero_n = 0;
    sum = 0.0f;
    for (i = 0; i < 3; ++i)
    {
        if (storage[i] > 0.0f)
        {
            nonzero_n++;
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, storage[i]);
        }
        sum += storage[i];
    }
    TEST_ASSERT_EQUAL_INT(1, nonzero_n);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, sum);

    memset(storage, 0, sizeof(storage));
    node.street_hint = 1u;
    cfr_accumulate_node_strategy(&node, strategy, 3, &cfg, &rng);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.2f, storage[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.3f, storage[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, storage[2]);
}

static void test_worker_shard_partition_is_stable_and_complete(void)
{
    CFRBlueprint worker;
    const uint64_t keys[] = {
        0x101ULL, 0x202ULL, 0x303ULL, 0x404ULL,
        0x505ULL, 0x606ULL, 0x707ULL, 0x808ULL
    };
    uint32_t partitions[16];
    uint32_t offsets[5];
    uint32_t expected[16];
    uint32_t expected_count;
    uint32_t i;
    int shard;

    memset(&worker, 0, sizeof(worker));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&worker, 112233ULL));

    for (i = 0u; i < (uint32_t)(sizeof(keys) / sizeof(keys[0])); ++i)
    {
        CFRNode *node;
        node = cfr_blueprint_get_node_ex(&worker, keys[i], 1, 3, 0);
        TEST_ASSERT_NOT_NULL(node);
        cfr_blueprint_touch_node(&worker, node);
    }

    TEST_ASSERT_TRUE(cfr_build_shard_partitions(&worker, 4, partitions, offsets));
    TEST_ASSERT_EQUAL_UINT32(0u, offsets[0]);
    TEST_ASSERT_EQUAL_UINT32(worker.touched_count, offsets[4]);

    expected_count = 0u;
    for (shard = 0; shard < 4; ++shard)
    {
        for (i = 0u; i < worker.touched_count; ++i)
        {
            uint32_t idx;
            const CFRNode *node;

            idx = worker.touched_indices[i];
            node = &worker.nodes[idx];
            if (cfr_worker_key_shard(node->key, 4) == shard)
            {
                expected[expected_count++] = idx;
            }
        }
    }

    TEST_ASSERT_EQUAL_UINT32(expected_count, offsets[4]);
    for (i = 0u; i < expected_count; ++i)
    {
        TEST_ASSERT_EQUAL_UINT32(expected[i], partitions[i]);
    }

    cfr_blueprint_release(&worker);
}

static void test_worker_delta_marks_only_missing_base_nodes_for_precreate(void)
{
    CFRBlueprint base;
    CFRBlueprint worker;
    CFRNode *existing_base;
    CFRNode *existing_worker;
    CFRNode *new_worker;

    memset(&base, 0, sizeof(base));
    memset(&worker, 0, sizeof(worker));

    TEST_ASSERT_TRUE(cfr_blueprint_init(&base, 111ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&worker, 222ULL));

    existing_base = cfr_blueprint_get_node_ex(&base, 0x1111ULL, 1, 3, 0);
    TEST_ASSERT_NOT_NULL(existing_base);

    existing_worker = cfr_blueprint_get_node_ex(&worker, 0x1111ULL, 1, 3, 0);
    new_worker = cfr_blueprint_get_node_ex(&worker, 0x2222ULL, 1, 2, 1);
    TEST_ASSERT_NOT_NULL(existing_worker);
    TEST_ASSERT_NOT_NULL(new_worker);

    cfr_blueprint_touch_node(&worker, existing_worker);
    cfr_blueprint_touch_node(&worker, new_worker);

    cfr_prepare_worker_delta_against_base(&base, &worker);

    TEST_ASSERT_TRUE((existing_worker->used & CFR_WORKER_NODE_FLAG_NEW_BASE) == 0);
    TEST_ASSERT_TRUE((new_worker->used & CFR_WORKER_NODE_FLAG_NEW_BASE) != 0);

    TEST_ASSERT_TRUE(cfr_precreate_worker_nodes(&base, &worker));
    TEST_ASSERT_EQUAL_UINT32(2u, base.used_node_count);
    TEST_ASSERT_NOT_NULL(cfr_blueprint_get_node_ex(&base, 0x1111ULL, 0, 3, 0));
    TEST_ASSERT_NOT_NULL(cfr_blueprint_get_node_ex(&base, 0x2222ULL, 0, 2, 1));

    cfr_blueprint_release(&worker);
    cfr_blueprint_release(&base);
}

static void test_strict_time_phase_chunk_is_parallel_capped(void)
{
    CFRTrainOptions opt;
    CFRBlueprint bp;
    uint64_t chunk;

    memset(&opt, 0, sizeof(opt));
    memset(&bp, 0, sizeof(bp));

    opt.chunk_iters = 5000ULL;
    opt.threads = 24;
    opt.strict_time_phases = 1;
    opt.seconds_limit = 3600;
    opt.status_every_iters = 0ULL;
    opt.dump_every_iters = 0ULL;
    opt.snapshot_every_iters = 0ULL;

    bp.iteration = 0ULL;
    chunk = cfr_train_next_chunk_iters(&opt, &bp, 0ULL, 0ULL);
    TEST_ASSERT_EQUAL_UINT64(5000ULL, chunk);

    opt.threads = 1;
    chunk = cfr_train_next_chunk_iters(&opt, &bp, 0ULL, 0ULL);
    TEST_ASSERT_EQUAL_UINT64(1ULL, chunk);
}

static void test_int_regret_mode_quantizes_and_respects_floor(void)
{
    static CFRBlueprint bp;
    CFRTrainOptions opt;
    int i;

    memset(&bp, 0, sizeof(bp));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 424242ULL));

    memset(&opt, 0, sizeof(opt));
    opt.threads = 1;
    opt.chunk_iters = 1;
    opt.samples_per_player = 1;
    opt.strategy_interval = 1ULL;
    opt.enable_linear_discount = 0;
    opt.enable_pruning = 0;
    opt.use_int_regret = 1;
    opt.regret_floor = -2;

    cfr_run_iterations(&bp, 3, &opt);

    for (i = 0; i < (int)bp.used_node_count; ++i)
    {
        if (bp.nodes[i].used)
        {
            int k;
            for (k = 0; k < bp.nodes[i].action_count; ++k)
            {
                int32_t r;
                r = bp.nodes[i].regret[k];
                TEST_ASSERT_TRUE(r >= opt.regret_floor);
            }
        }
    }
}

static void test_train_compat_hash_changes_with_solver_settings(void)
{
    CFRTrainOptions a;
    CFRTrainOptions b;
    uint32_t ha;
    uint32_t hb;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    a.samples_per_player = 1;
    a.strategy_interval = 1ULL;
    a.enable_linear_discount = 1;
    a.linear_discount_every_iters = 1000ULL;
    a.linear_discount_stop_iter = 0ULL;
    a.linear_discount_scale = 1.0;
    a.enable_pruning = 1;
    a.prune_start_iter = 2000ULL;
    a.prune_full_every_iters = 0ULL;
    a.prune_threshold = -200.0;
    a.prune_prob = 0.95;
    a.use_int_regret = 0;
    a.regret_floor = -2000000000;

    b = a;
    ha = cfr_train_compat_hash32(&a);
    hb = cfr_train_compat_hash32(&b);
    TEST_ASSERT_EQUAL_UINT32(ha, hb);

    b.strategy_interval = 2ULL;
    hb = cfr_train_compat_hash32(&b);
    TEST_ASSERT_TRUE(ha != hb);
}

static void test_train_rejects_removed_snapshot_postflop_flag(void)
{
    char *argv1[1];
    char *argv2[1];
    int rc1;
    int rc2;

    argv1[0] = "--snapshot-postflop-avg";
    argv2[0] = "--no-snapshot-postflop-avg";

    rc1 = cfr_cmd_train(1, argv1);
    rc2 = cfr_cmd_train(1, argv2);

    TEST_ASSERT_EQUAL_INT(1, rc1);
    TEST_ASSERT_EQUAL_INT(1, rc2);
}

static void test_train_compat_hash_changes_with_abstraction_hash(void)
{
    CFRTrainOptions a;
    CFRTrainOptions b;
    uint32_t ha;
    uint32_t hb;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.samples_per_player = 1;
    a.strategy_interval = 1ULL;
    a.enable_linear_discount = 0;
    a.enable_pruning = 0;
    a.regret_floor = -2000000000;
    a.abstraction_hash32 = 0x11112222u;

    b = a;
    ha = cfr_train_compat_hash32(&a);
    hb = cfr_train_compat_hash32(&b);
    TEST_ASSERT_EQUAL_UINT32(ha, hb);

    b.abstraction_hash32 = 0x33334444u;
    hb = cfr_train_compat_hash32(&b);
    TEST_ASSERT_TRUE(ha != hb);
}

static void test_train_compat_hash_changes_with_parallel_mode(void)
{
    CFRTrainOptions a;
    CFRTrainOptions b;
    uint32_t ha;
    uint32_t hb;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.samples_per_player = 1;
    a.strategy_interval = 1ULL;
    a.enable_linear_discount = 0;
    a.enable_pruning = 0;
    a.regret_floor = -2000000000;
    a.abstraction_hash32 = 0x11112222u;
    a.parallel_mode = CFR_PARALLEL_MODE_DETERMINISTIC;

    b = a;
    ha = cfr_train_compat_hash32(&a);
    hb = cfr_train_compat_hash32(&b);
    TEST_ASSERT_EQUAL_UINT32(ha, hb);

    b.parallel_mode = CFR_PARALLEL_MODE_SHARDED;
    hb = cfr_train_compat_hash32(&b);
    TEST_ASSERT_TRUE(ha != hb);
}

static void test_train_compat_hash_changes_with_preflop_avg_mode(void)
{
    CFRTrainOptions a;
    CFRTrainOptions b;
    uint32_t ha;
    uint32_t hb;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.samples_per_player = 1;
    a.strategy_interval = 1ULL;
    a.enable_linear_discount = 0;
    a.enable_pruning = 0;
    a.regret_floor = -2000000000;
    a.abstraction_hash32 = 0x11112222u;
    a.enable_preflop_avg = 1;
    a.preflop_avg_sampled = 0;

    b = a;
    ha = cfr_train_compat_hash32(&a);
    hb = cfr_train_compat_hash32(&b);
    TEST_ASSERT_EQUAL_UINT32(ha, hb);

    b.preflop_avg_sampled = 1;
    hb = cfr_train_compat_hash32(&b);
    TEST_ASSERT_TRUE(ha != hb);
}

static void test_abstraction_default_config_is_valid_and_hash_stable(void)
{
    static CFRAbstractionConfig a;
    static CFRAbstractionConfig b;

    cfr_abstraction_set_defaults(&a);
    cfr_abstraction_set_defaults(&b);

    TEST_ASSERT_TRUE(cfr_abstraction_validate(&a));
    TEST_ASSERT_TRUE(cfr_abstraction_validate(&b));
    TEST_ASSERT_EQUAL_UINT32(a.hash32, b.hash32);
    TEST_ASSERT_EQUAL_UINT32(169u, a.street_bucket_count_blueprint[0]);
    TEST_ASSERT_EQUAL_UINT32(200u, a.street_bucket_count_blueprint[1]);
    TEST_ASSERT_EQUAL_UINT32(500u, a.street_bucket_count_search[1]);
    TEST_ASSERT_EQUAL_UINT32(CFR_ABS_CLUSTER_ALGO_EMD_KMEDOIDS, a.clustering_algo);
    TEST_ASSERT_EQUAL_UINT32(CFR_ABS_EMD_BINS, a.emd_bins);
}

static void test_abstraction_save_load_roundtrip(void)
{
    static CFRAbstractionConfig a;
    static CFRAbstractionConfig b;
    const char *tmp_file = "tests_tmp_abstraction.bin";

    cfr_abstraction_set_defaults(&a);
    a.seed = 1234567ULL;
    a.street_bucket_count_blueprint[1] = 321u;
    a.street_bucket_count_search[3] = 500u;
    TEST_ASSERT_TRUE(cfr_abstraction_validate(&a));
    a.hash32 = cfr_abstraction_hash32(&a);

    TEST_ASSERT_TRUE(cfr_abstraction_save(&a, tmp_file));
    TEST_ASSERT_TRUE(cfr_abstraction_load(&b, tmp_file));

    TEST_ASSERT_EQUAL_UINT32(a.hash32, b.hash32);
    TEST_ASSERT_EQUAL_UINT64(a.seed, b.seed);
    TEST_ASSERT_EQUAL_UINT32(a.street_bucket_count_blueprint[1], b.street_bucket_count_blueprint[1]);
    TEST_ASSERT_EQUAL_UINT32(a.street_bucket_count_search[3], b.street_bucket_count_search[3]);

    remove(tmp_file);
}

static void test_abstraction_bucket_preflop_is_lossless(void)
{
    static CFRAbstractionConfig cfg;
    uint64_t idx;
    uint64_t bucket;
    int as;
    int kd;

    cfr_abstraction_set_defaults(&cfg);
    TEST_ASSERT_TRUE(cfr_abstraction_use(&cfg));

    as = cfr_parse_card_pair('A', 's');
    kd = cfr_parse_card_pair('K', 'd');
    TEST_ASSERT_TRUE(cfr_hand_index_for_state(0, as, kd, NULL, 0, &idx));
    bucket = cfr_abstraction_bucket_for_hand(0, idx, CFR_ABS_MODE_BLUEPRINT);
    TEST_ASSERT_EQUAL_UINT64(idx, bucket);
}

static void test_abstraction_build_centroids_enables_feature_bucketing(void)
{
    static CFRAbstractionConfig cfg;
    int hole[2];
    int board[5];
    uint64_t idx;
    uint64_t bucket;

    cfr_abstraction_set_defaults(&cfg);
    cfg.street_bucket_count_blueprint[1] = 16u;
    cfg.street_bucket_count_blueprint[2] = 16u;
    cfg.street_bucket_count_blueprint[3] = 16u;
    cfg.street_bucket_count_search[1] = 24u;
    cfg.street_bucket_count_search[2] = 24u;
    cfg.street_bucket_count_search[3] = 24u;
    cfg.feature_mc_samples = 2u;
    cfg.kmeans_iters = 2u;
    cfg.build_samples_per_street = 256u;
    cfg.seed = 42u;

    TEST_ASSERT_TRUE(cfr_abstraction_build_centroids(&cfg));
    TEST_ASSERT_EQUAL_UINT32(1u, cfg.centroid_ready[CFR_ABS_MODE_BLUEPRINT][1]);
    TEST_ASSERT_EQUAL_UINT32(1u, cfg.centroid_ready[CFR_ABS_MODE_SEARCH][3]);
    TEST_ASSERT_EQUAL_UINT32(1u, cfg.emd_ready[CFR_ABS_MODE_BLUEPRINT][1]);
    TEST_ASSERT_EQUAL_UINT32(1u, cfg.emd_ready[CFR_ABS_MODE_SEARCH][3]);
    TEST_ASSERT_TRUE(cfg.emd_quality[CFR_ABS_MODE_BLUEPRINT][1][0] >= 0.0f);
    TEST_ASSERT_TRUE(cfg.emd_quality[CFR_ABS_MODE_SEARCH][3][1] >= 0.0f);
    TEST_ASSERT_TRUE(cfr_abstraction_use(&cfg));

    TEST_ASSERT_EQUAL_INT(2, cfr_parse_cards("AsKd", hole, 2));
    TEST_ASSERT_EQUAL_INT(3, cfr_parse_cards("AhKs2d", board, 5));
    TEST_ASSERT_TRUE(cfr_hand_index_for_state(1, hole[0], hole[1], board, 3, &idx));
    bucket = cfr_abstraction_bucket_for_state(1,
                                              hole[0],
                                              hole[1],
                                              board,
                                              3,
                                              idx,
                                              CFR_ABS_MODE_BLUEPRINT);
    TEST_ASSERT_TRUE(bucket < cfg.street_bucket_count_blueprint[1]);
}

static void test_blueprint_roundtrip_preserves_street_hint_and_abstraction_hash(void)
{
    static CFRBlueprint a;
    static CFRBlueprint b;
    CFRNode *na;
    CFRNode *nb;
    const char *tmp_file = "tests_tmp_blueprint_street.bin";

    TEST_ASSERT_TRUE(cfr_blueprint_init(&a, 123ULL));
    a.abstraction_hash32 = 0xDEADBEEFu;

    na = cfr_blueprint_get_node(&a, 0xABCULL, 1, 3);
    TEST_ASSERT_NOT_NULL(na);
    na->street_hint = 2u;
    na->strategy_sum[0] = 11.0f;
    na->strategy_sum[1] = 22.0f;
    na->strategy_sum[2] = 33.0f;

    TEST_ASSERT_TRUE(cfr_blueprint_save(&a, tmp_file));
    TEST_ASSERT_TRUE(cfr_blueprint_load(&b, tmp_file));

    nb = cfr_blueprint_get_node(&b, 0xABCULL, 0, 3);
    TEST_ASSERT_NOT_NULL(nb);
    TEST_ASSERT_EQUAL_UINT32(a.abstraction_hash32, b.abstraction_hash32);
    TEST_ASSERT_EQUAL_UINT8(2u, nb->street_hint);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 22.0f, nb->strategy_sum[1]);
    remove(tmp_file);
}

static void test_snapshot_postflop_average_applies_only_postflop_nodes(void)
{
    static CFRBlueprint src;
    static CFRBlueprint avg;
    static CFRBlueprint dst;
    CFRNode *pre;
    CFRNode *post;

    TEST_ASSERT_TRUE(cfr_blueprint_init(&src, 1ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&avg, 2ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&dst, 3ULL));

    pre = cfr_blueprint_get_node(&src, 0x111ULL, 1, 2);
    post = cfr_blueprint_get_node(&src, 0x222ULL, 1, 2);
    TEST_ASSERT_NOT_NULL(pre);
    TEST_ASSERT_NOT_NULL(post);

    pre->street_hint = 0u;
    pre->strategy_sum[0] = 5.0f;
    post->street_hint = 2u;
    post->regret[0] = 3;
    post->regret[1] = 1;

    cfr_snapshot_avg_accumulate_postflop(&avg, &src);

    TEST_ASSERT_TRUE(cfr_blueprint_copy_from(&dst, &src));
    pre = cfr_blueprint_get_node(&dst, 0x111ULL, 0, 2);
    post = cfr_blueprint_get_node(&dst, 0x222ULL, 0, 2);
    pre->strategy_sum[0] = 99.0f;
    post->strategy_sum[0] = 1.0f;

    cfr_snapshot_avg_apply_postflop(&dst, &avg, 1ULL);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 99.0f, pre->strategy_sum[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.75f, post->strategy_sum[0]);
}

static void test_blueprint_roundtrip_preserves_missing_postflop_strategy_payload(void)
{
    static CFRBlueprint a;
    static CFRBlueprint b;
    const char *tmp_file;
    CFRNode *pre_a;
    CFRNode *post_a;
    CFRNode *pre_b;
    CFRNode *post_b;
    float avg_post[CFR_MAX_ACTIONS];

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    tmp_file = "tests_tmp_blueprint_v8_payload.bin";

    TEST_ASSERT_TRUE(cfr_blueprint_init(&a, 112233ULL));
    a.omit_postflop_strategy_sum = 1;

    pre_a = cfr_blueprint_get_node_ex(&a, 0xAB01ULL, 1, 2, 0);
    post_a = cfr_blueprint_get_node_ex(&a, 0xAB02ULL, 1, 2, 2);
    TEST_ASSERT_NOT_NULL(pre_a);
    TEST_ASSERT_NOT_NULL(post_a);
    pre_a->street_hint = 0u;
    post_a->street_hint = 2u;

    TEST_ASSERT_NOT_NULL(pre_a->strategy_sum);
    TEST_ASSERT_NULL(post_a->strategy_sum);

    pre_a->regret[0] = 10;
    pre_a->regret[1] = 0;
    pre_a->strategy_sum[0] = 3.0f;
    pre_a->strategy_sum[1] = 1.0f;
    post_a->regret[0] = 3;
    post_a->regret[1] = 1;

    TEST_ASSERT_TRUE(cfr_blueprint_save(&a, tmp_file));
    TEST_ASSERT_TRUE(cfr_blueprint_load(&b, tmp_file));

    pre_b = cfr_blueprint_get_node(&b, 0xAB01ULL, 0, 2);
    post_b = cfr_blueprint_get_node(&b, 0xAB02ULL, 0, 2);
    TEST_ASSERT_NOT_NULL(pre_b);
    TEST_ASSERT_NOT_NULL(post_b);
    TEST_ASSERT_NOT_NULL(pre_b->strategy_sum);
    TEST_ASSERT_NULL(post_b->strategy_sum);

    cfr_compute_average_strategy_n(post_b, 2, avg_post);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.75f, avg_post[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, avg_post[1]);

    remove(tmp_file);
}

static void test_compact_snapshot_save_load_accumulates_postflop_current(void)
{
    static CFRBlueprint src;
    static CFRBlueprint avg;
    const char *tmp_file;
    CFRNode *post;
    CFRNode *acc;
    uint64_t elapsed;
    uint32_t compat_hash32;
    uint32_t abstraction_hash32;
    CFRSnapshotFileHeader sh;

    memset(&src, 0, sizeof(src));
    memset(&avg, 0, sizeof(avg));
    tmp_file = "tests_tmp_snapshot_compact.bin";

    TEST_ASSERT_TRUE(cfr_blueprint_init(&src, 4242ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&avg, 5252ULL));

    post = cfr_blueprint_get_node_ex(&src, 0xCC01ULL, 1, 2, 2);
    TEST_ASSERT_NOT_NULL(post);
    post->street_hint = 2u;
    post->regret[0] = 3;
    post->regret[1] = 1;
    src.elapsed_train_seconds = 1234ULL;
    src.compat_hash32 = 0x11223344u;
    src.abstraction_hash32 = 0x55667788u;

    TEST_ASSERT_TRUE(cfr_snapshot_save_postflop_current(&src, tmp_file));
    TEST_ASSERT_TRUE(cfr_snapshot_peek_header(tmp_file, &sh));
    TEST_ASSERT_EQUAL_UINT64(CFR_SNAPSHOT_MAGIC, sh.magic);
    TEST_ASSERT_EQUAL_UINT32(CFR_SNAPSHOT_VERSION, sh.version);
    TEST_ASSERT_EQUAL_UINT32(1u, sh.node_count);

    elapsed = 0ULL;
    compat_hash32 = 0u;
    abstraction_hash32 = 0u;
    TEST_ASSERT_TRUE(cfr_snapshot_load_postflop_into_avg(&avg,
                                                         tmp_file,
                                                         &elapsed,
                                                         &compat_hash32,
                                                         &abstraction_hash32));
    TEST_ASSERT_EQUAL_UINT64(1234ULL, elapsed);
    TEST_ASSERT_EQUAL_UINT32(0x11223344u, compat_hash32);
    TEST_ASSERT_EQUAL_UINT32(0x55667788u, abstraction_hash32);

    acc = cfr_blueprint_get_node(&avg, 0xCC01ULL, 0, 2);
    TEST_ASSERT_NOT_NULL(acc);
    TEST_ASSERT_NOT_NULL(acc->strategy_sum);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.75f, acc->strategy_sum[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, acc->strategy_sum[1]);

    remove(tmp_file);
}

static void test_finalize_blueprint_is_deterministic_and_preserves_preflop(void)
{
    static CFRBlueprint raw;
    static CFRBlueprint snap1;
    static CFRBlueprint snap2;
    static CFRBlueprint out_a;
    static CFRBlueprint out_b;
    CFRNode *n_raw_pre;
    CFRNode *n_raw_post;
    CFRNode *n_s1_post;
    CFRNode *n_s2_post;
    CFRNode *n_out_pre;
    CFRNode *n_out_post;
    char *argv1[8];
    char *argv2[8];
    int rc;
    const char *raw_file = "tests_tmp_finalize_raw.bin";
    const char *out_file_a = "tests_tmp_finalize_out_a.bin";
    const char *out_file_b = "tests_tmp_finalize_out_b.bin";
    const char *snap_dir = "tests_tmp_snapshots";
    const char *snap_file_1 = "tests_tmp_snapshots\\snapshot_000000000001.bin";
    const char *snap_file_2 = "tests_tmp_snapshots\\snapshot_000000000002.bin";

    TEST_ASSERT_TRUE(cfr_blueprint_init(&raw, 11ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&snap1, 12ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&snap2, 13ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&out_a, 14ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&out_b, 15ULL));

    n_raw_pre = cfr_blueprint_get_node(&raw, 0x100ULL, 1, 2);
    n_raw_post = cfr_blueprint_get_node(&raw, 0x200ULL, 1, 2);
    TEST_ASSERT_NOT_NULL(n_raw_pre);
    TEST_ASSERT_NOT_NULL(n_raw_post);
    n_raw_pre->street_hint = 0u;
    n_raw_post->street_hint = 2u;
    n_raw_pre->strategy_sum[0] = 7.0f;
    n_raw_pre->strategy_sum[1] = 3.0f;
    n_raw_post->strategy_sum[0] = 0.0f;
    n_raw_post->strategy_sum[1] = 0.0f;

    TEST_ASSERT_TRUE(cfr_blueprint_copy_from(&snap1, &raw));
    TEST_ASSERT_TRUE(cfr_blueprint_copy_from(&snap2, &raw));
    n_s1_post = cfr_blueprint_get_node(&snap1, 0x200ULL, 0, 2);
    n_s2_post = cfr_blueprint_get_node(&snap2, 0x200ULL, 0, 2);
    TEST_ASSERT_NOT_NULL(n_s1_post);
    TEST_ASSERT_NOT_NULL(n_s2_post);
    n_s1_post->regret[0] = 3;
    n_s1_post->regret[1] = 1;
    n_s2_post->regret[0] = 1;
    n_s2_post->regret[1] = 3;

    cfr_ensure_dir(snap_dir);
    TEST_ASSERT_TRUE(cfr_blueprint_save(&raw, raw_file));
    TEST_ASSERT_TRUE(cfr_blueprint_save(&snap1, snap_file_1));
    TEST_ASSERT_TRUE(cfr_blueprint_save(&snap2, snap_file_2));

    argv1[0] = "--raw";
    argv1[1] = (char *)raw_file;
    argv1[2] = "--snapshot-dir";
    argv1[3] = (char *)snap_dir;
    argv1[4] = "--out";
    argv1[5] = (char *)out_file_a;
    argv1[6] = "--ignore-abstraction-compat";
    argv1[7] = NULL;
    rc = cfr_cmd_finalize_blueprint(7, argv1);
    TEST_ASSERT_EQUAL_INT(0, rc);

    argv2[0] = "--raw";
    argv2[1] = (char *)raw_file;
    argv2[2] = "--snapshot-dir";
    argv2[3] = (char *)snap_dir;
    argv2[4] = "--out";
    argv2[5] = (char *)out_file_b;
    argv2[6] = "--ignore-abstraction-compat";
    argv2[7] = NULL;
    rc = cfr_cmd_finalize_blueprint(7, argv2);
    TEST_ASSERT_EQUAL_INT(0, rc);

    TEST_ASSERT_TRUE(cfr_blueprint_load(&out_a, out_file_a));
    TEST_ASSERT_TRUE(cfr_blueprint_load(&out_b, out_file_b));
    cfr_assert_blueprints_equal(&out_a, &out_b, 1e-6f);

    n_out_pre = cfr_blueprint_get_node(&out_a, 0x100ULL, 0, 2);
    n_out_post = cfr_blueprint_get_node(&out_a, 0x200ULL, 0, 2);
    TEST_ASSERT_NOT_NULL(n_out_pre);
    TEST_ASSERT_NOT_NULL(n_out_post);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 7.0f, n_out_pre->strategy_sum[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.0f, n_out_pre->strategy_sum[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, n_out_post->strategy_sum[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, n_out_post->strategy_sum[1]);

    remove(raw_file);
    remove(out_file_a);
    remove(out_file_b);
    remove(snap_file_1);
    remove(snap_file_2);
}

static void test_finalize_blueprint_snapshot_min_seconds_filters_old_snapshots(void)
{
    static CFRBlueprint raw;
    static CFRBlueprint snap1;
    static CFRBlueprint snap2;
    static CFRBlueprint out_bp;
    CFRNode *n_raw_post;
    CFRNode *n_s1_post;
    CFRNode *n_s2_post;
    CFRNode *n_out_post;
    char *argv1[10];
    int rc;
    const char *raw_file = "tests_tmp_finalize_min_raw.bin";
    const char *out_file = "tests_tmp_finalize_min_out.bin";
    const char *snap_dir = "tests_tmp_snapshots_min";
    const char *snap_file_1 = "tests_tmp_snapshots_min\\snapshot_000000000001.bin";
    const char *snap_file_2 = "tests_tmp_snapshots_min\\snapshot_000000000002.bin";

    TEST_ASSERT_TRUE(cfr_blueprint_init(&raw, 101ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&snap1, 102ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&snap2, 103ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&out_bp, 104ULL));

    n_raw_post = cfr_blueprint_get_node(&raw, 0x300ULL, 1, 2);
    TEST_ASSERT_NOT_NULL(n_raw_post);
    n_raw_post->street_hint = 2u;
    n_raw_post->strategy_sum[0] = 0.0f;
    n_raw_post->strategy_sum[1] = 0.0f;

    TEST_ASSERT_TRUE(cfr_blueprint_copy_from(&snap1, &raw));
    TEST_ASSERT_TRUE(cfr_blueprint_copy_from(&snap2, &raw));
    snap1.elapsed_train_seconds = 100ULL;
    snap2.elapsed_train_seconds = 200ULL;

    n_s1_post = cfr_blueprint_get_node(&snap1, 0x300ULL, 0, 2);
    n_s2_post = cfr_blueprint_get_node(&snap2, 0x300ULL, 0, 2);
    TEST_ASSERT_NOT_NULL(n_s1_post);
    TEST_ASSERT_NOT_NULL(n_s2_post);
    n_s1_post->regret[0] = 3;
    n_s1_post->regret[1] = 1;
    n_s2_post->regret[0] = 1;
    n_s2_post->regret[1] = 3;

    cfr_ensure_dir(snap_dir);
    TEST_ASSERT_TRUE(cfr_blueprint_save(&raw, raw_file));
    TEST_ASSERT_TRUE(cfr_blueprint_save(&snap1, snap_file_1));
    TEST_ASSERT_TRUE(cfr_blueprint_save(&snap2, snap_file_2));

    argv1[0] = "--raw";
    argv1[1] = (char *)raw_file;
    argv1[2] = "--snapshot-dir";
    argv1[3] = (char *)snap_dir;
    argv1[4] = "--snapshot-min-seconds";
    argv1[5] = "150";
    argv1[6] = "--out";
    argv1[7] = (char *)out_file;
    argv1[8] = "--ignore-abstraction-compat";
    argv1[9] = NULL;

    rc = cfr_cmd_finalize_blueprint(9, argv1);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_TRUE(cfr_blueprint_load(&out_bp, out_file));

    n_out_post = cfr_blueprint_get_node(&out_bp, 0x300ULL, 0, 2);
    TEST_ASSERT_NOT_NULL(n_out_post);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, n_out_post->strategy_sum[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.75f, n_out_post->strategy_sum[1]);

    remove(raw_file);
    remove(out_file);
    remove(snap_file_1);
    remove(snap_file_2);
}

static void test_finalize_blueprint_runtime_only_skips_full_output(void)
{
    static CFRBlueprint raw;
    CFRRuntimeBlueprint rt;
    CFRPolicyProvider provider;
    CFRNode *n_raw_post;
    FILE *fp_missing;
    float policy[2];
    char *argv1[9];
    int rc;
    const char *raw_file = "tests_tmp_finalize_rt_only_raw.bin";
    const char *runtime_file = "tests_tmp_finalize_rt_only_runtime.bin";
    const char *missing_full = "tests_tmp_finalize_rt_only_full.bin";
    const char *snap_dir = "tests_tmp_finalize_rt_only_snapshots";

    memset(&raw, 0, sizeof(raw));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&raw, 201ULL));

    n_raw_post = cfr_blueprint_get_node(&raw, 0x400ULL, 1, 2);
    TEST_ASSERT_NOT_NULL(n_raw_post);
    n_raw_post->street_hint = 2u;
    n_raw_post->strategy_sum[0] = 4.0f;
    n_raw_post->strategy_sum[1] = 1.0f;
    raw.compat_hash32 = 0x01020304u;
    raw.abstraction_hash32 = 0x05060708u;

    cfr_ensure_dir(snap_dir);
    TEST_ASSERT_TRUE(cfr_blueprint_save(&raw, raw_file));
    remove(runtime_file);
    remove(missing_full);

    argv1[0] = "--raw";
    argv1[1] = (char *)raw_file;
    argv1[2] = "--snapshot-dir";
    argv1[3] = (char *)snap_dir;
    argv1[4] = "--runtime-out";
    argv1[5] = (char *)runtime_file;
    argv1[6] = "--ignore-abstraction-compat";
    argv1[7] = "--runtime-shards";
    argv1[8] = "32";

    rc = cfr_cmd_finalize_blueprint(9, argv1);
    TEST_ASSERT_EQUAL_INT(0, rc);
    fp_missing = fopen(missing_full, "rb");
    TEST_ASSERT_NULL(fp_missing);
    TEST_ASSERT_TRUE(cfr_runtime_blueprint_open(&rt, runtime_file, 4096ULL, CFR_RUNTIME_PREFETCH_NONE));
    cfr_policy_provider_init_runtime(&provider, &rt);
    TEST_ASSERT_TRUE(cfr_policy_provider_get_average_policy(&provider, 0x400ULL, 2, 2, policy));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.8f, policy[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.2f, policy[1]);
    cfr_runtime_blueprint_close(&rt);

    remove(raw_file);
    remove(runtime_file);
}

static void test_search_decide_returns_legal_policy(void)
{
    static CFRSearchContext ctx;
    CFRSearchDecision d;
    static CFRBlueprint bp;
    CFRPolicyProvider provider;
    CFRHandState st;
    static CFRAbstractionConfig cfg;
    int hole[2];
    int board[5];
    unsigned char hist[CFR_MAX_HISTORY];
    uint64_t rng;
    int i;
    float sum_final;
    float sum_avg;

    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 555ULL));
    cfr_policy_provider_init_blueprint(&provider, &bp);
    cfr_abstraction_set_defaults(&cfg);
    TEST_ASSERT_TRUE(cfr_abstraction_use(&cfg));
    TEST_ASSERT_TRUE(cfr_hand_index_init());

    TEST_ASSERT_EQUAL_INT(2, cfr_parse_cards("AsKd", hole, 2));
    TEST_ASSERT_EQUAL_INT(0, cfr_parse_cards("", board, 5));
    TEST_ASSERT_EQUAL_INT(0, cfr_parse_history_text("", hist, CFR_MAX_HISTORY));

    rng = 123ULL;
    TEST_ASSERT_TRUE(cfr_build_state_with_known_hole(&st,
                                                     0,
                                                     5,
                                                     6,
                                                     6,
                                                     2,
                                                     200,
                                                     0,
                                                     0,
                                                     hole,
                                                     board,
                                                     0,
                                                     hist,
                                                     0,
                                                     &rng));

    cfr_search_context_init(&ctx, 77ULL);
    TEST_ASSERT_TRUE(cfr_search_decide(&ctx,
                                       &provider,
                                       &st,
                                       0,
                                       8ULL,
                                       0ULL,
                                       2,
                                       1,
                                       CFR_SEARCH_PICK_SAMPLE_FINAL,
                                       CFR_OFFTREE_MODE_INJECT,
                                       NULL,
                                       0,
                                       &d));
    TEST_ASSERT_TRUE(d.legal_count > 0);
    TEST_ASSERT_TRUE(d.chosen_index >= 0 && d.chosen_index < d.legal_count);
    TEST_ASSERT_EQUAL_INT(0, d.belief_updates);

    sum_final = 0.0f;
    sum_avg = 0.0f;
    for (i = 0; i < d.legal_count; ++i)
    {
        TEST_ASSERT_TRUE(d.final_policy[i] >= 0.0f);
        TEST_ASSERT_TRUE(d.avg_policy[i] >= 0.0f);
        sum_final += d.final_policy[i];
        sum_avg += d.avg_policy[i];
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, sum_final);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, sum_avg);
}

static void test_build_search_round_root_with_frozen_extracts_current_street_actions(void)
{
    CFRHandState root;
    CFRHandState sim;
    CFRSearchFrozenAction frozen[CFR_MAX_HISTORY];
    int hole[2];
    int board[5];
    unsigned char hist[CFR_MAX_HISTORY];
    int hist_n;
    int frozen_n;
    uint64_t rng;
    int i;

    TEST_ASSERT_EQUAL_INT(2, cfr_parse_cards("AsKd", hole, 2));
    TEST_ASSERT_EQUAL_INT(3, cfr_parse_cards("AhKs2d", board, 5));
    hist_n = cfr_parse_history_text("c c / c r5", hist, CFR_MAX_HISTORY);
    TEST_ASSERT_TRUE(hist_n > 0);

    rng = 9991ULL;
    TEST_ASSERT_TRUE(cfr_build_search_round_root_with_frozen(&root,
                                                             frozen,
                                                             &frozen_n,
                                                             CFR_MAX_HISTORY,
                                                             0,
                                                             5,
                                                             2,
                                                             20,
                                                             0,
                                                             200,
                                                             1,
                                                             0,
                                                             hole,
                                                             board,
                                                             3,
                                                             hist,
                                                             hist_n,
                                                             CFR_OFFTREE_MODE_INJECT,
                                                             &rng));

    TEST_ASSERT_EQUAL_INT(1, root.street);
    TEST_ASSERT_EQUAL_INT(0, root.to_call);
    TEST_ASSERT_EQUAL_INT(0, root.num_raises_street);
    TEST_ASSERT_TRUE(frozen_n >= 2);

    sim = root;
    for (i = 0; i < frozen_n; ++i)
    {
        int ok;
        ok = cfr_search_apply_observed_action(&sim,
                                              frozen[i].player,
                                              frozen[i].action,
                                              frozen[i].target,
                                              CFR_OFFTREE_MODE_INJECT,
                                              &rng,
                                              NULL);
        TEST_ASSERT_TRUE(ok);
        if (sim.is_terminal || sim.street != root.street)
        {
            break;
        }
    }
}

static void test_search_decide_reports_belief_updates_for_frozen_history(void)
{
    static CFRSearchContext ctx;
    static CFRBlueprint bp;
    CFRPolicyProvider provider;
    CFRSearchDecision d;
    CFRHandState root;
    CFRHandState sim;
    CFRSearchFrozenAction frozen[CFR_MAX_HISTORY];
    static CFRAbstractionConfig cfg;
    int hole[2];
    int board[5];
    unsigned char hist[CFR_MAX_HISTORY];
    int hist_n;
    int frozen_n;
    int decision_player;
    uint64_t rng;
    int i;

    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 777ULL));
    cfr_policy_provider_init_blueprint(&provider, &bp);
    cfr_abstraction_set_defaults(&cfg);
    TEST_ASSERT_TRUE(cfr_abstraction_use(&cfg));
    TEST_ASSERT_TRUE(cfr_hand_index_init());

    TEST_ASSERT_EQUAL_INT(2, cfr_parse_cards("AsKd", hole, 2));
    TEST_ASSERT_EQUAL_INT(3, cfr_parse_cards("AhKs2d", board, 5));
    hist_n = cfr_parse_history_text("c c / c r5", hist, CFR_MAX_HISTORY);
    TEST_ASSERT_TRUE(hist_n > 0);

    rng = 9911ULL;
    TEST_ASSERT_TRUE(cfr_build_search_round_root_with_frozen(&root,
                                                             frozen,
                                                             &frozen_n,
                                                             CFR_MAX_HISTORY,
                                                             0,
                                                             5,
                                                             2,
                                                             20,
                                                             0,
                                                             200,
                                                             1,
                                                             0,
                                                             hole,
                                                             board,
                                                             3,
                                                             hist,
                                                             hist_n,
                                                             CFR_OFFTREE_MODE_INJECT,
                                                             &rng));
    TEST_ASSERT_TRUE(frozen_n > 0);

    sim = root;
    for (i = 0; i < frozen_n; ++i)
    {
        TEST_ASSERT_TRUE(cfr_search_apply_observed_action(&sim,
                                                          frozen[i].player,
                                                          frozen[i].action,
                                                          frozen[i].target,
                                                          CFR_OFFTREE_MODE_INJECT,
                                                          &rng,
                                                          NULL));
    }
    cfr_auto_advance_rounds_if_needed(&sim);
    TEST_ASSERT_FALSE(sim.is_terminal);
    decision_player = sim.current_player;
    TEST_ASSERT_TRUE(decision_player >= 0 && decision_player < CFR_MAX_PLAYERS);

    cfr_search_context_init(&ctx, 778ULL);
    TEST_ASSERT_TRUE(cfr_search_decide(&ctx,
                                       &provider,
                                       &root,
                                       decision_player,
                                       8ULL,
                                       0ULL,
                                       2,
                                       1,
                                       CFR_SEARCH_PICK_SAMPLE_FINAL,
                                       CFR_OFFTREE_MODE_INJECT,
                                       frozen,
                                       frozen_n,
                                       &d));
    TEST_ASSERT_TRUE(d.belief_updates > 0);
}

static void test_search_decide_sample_final_is_seed_deterministic(void)
{
    static CFRSearchContext ctx_a;
    static CFRSearchContext ctx_b;
    static CFRBlueprint bp;
    CFRPolicyProvider provider;
    CFRSearchDecision d_a;
    CFRSearchDecision d_b;
    CFRHandState st;
    static CFRAbstractionConfig cfg;
    int hole[2];
    int board[5];
    unsigned char hist[CFR_MAX_HISTORY];
    uint64_t rng;
    int i;

    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 6060ULL));
    cfr_policy_provider_init_blueprint(&provider, &bp);
    cfr_abstraction_set_defaults(&cfg);
    TEST_ASSERT_TRUE(cfr_abstraction_use(&cfg));
    TEST_ASSERT_TRUE(cfr_hand_index_init());

    TEST_ASSERT_EQUAL_INT(2, cfr_parse_cards("AsKd", hole, 2));
    rng = 6061ULL;
    TEST_ASSERT_TRUE(cfr_build_state_with_known_hole(&st,
                                                     0,
                                                     5,
                                                     6,
                                                     6,
                                                     2,
                                                     200,
                                                     0,
                                                     0,
                                                     hole,
                                                     board,
                                                     0,
                                                     hist,
                                                     0,
                                                     &rng));

    cfr_search_context_init(&ctx_a, 6062ULL);
    TEST_ASSERT_TRUE(cfr_search_decide(&ctx_a,
                                       &provider,
                                       &st,
                                       0,
                                       24ULL,
                                       0ULL,
                                       2,
                                       1,
                                       CFR_SEARCH_PICK_SAMPLE_FINAL,
                                       CFR_OFFTREE_MODE_INJECT,
                                       NULL,
                                       0,
                                       &d_a));

    cfr_search_context_init(&ctx_b, 6062ULL);
    TEST_ASSERT_TRUE(cfr_search_decide(&ctx_b,
                                       &provider,
                                       &st,
                                       0,
                                       24ULL,
                                       0ULL,
                                       2,
                                       1,
                                       CFR_SEARCH_PICK_SAMPLE_FINAL,
                                       CFR_OFFTREE_MODE_INJECT,
                                       NULL,
                                       0,
                                       &d_b));

    TEST_ASSERT_EQUAL_INT(d_a.legal_count, d_b.legal_count);
    TEST_ASSERT_EQUAL_INT(d_a.chosen_index, d_b.chosen_index);
    for (i = 0; i < d_a.legal_count; ++i)
    {
        TEST_ASSERT_FLOAT_WITHIN(1e-7f, d_a.final_policy[i], d_b.final_policy[i]);
    }
}

static void test_search_parallel_with_frozen_is_seed_deterministic(void)
{
    static CFRSearchContext ctx_a;
    static CFRSearchContext ctx_b;
    static CFRBlueprint bp;
    CFRPolicyProvider provider;
    CFRSearchDecision d_a;
    CFRSearchDecision d_b;
    CFRHandState root;
    CFRHandState sim;
    CFRSearchFrozenAction frozen[CFR_MAX_HISTORY];
    static CFRAbstractionConfig cfg;
    int hole[2];
    int board[5];
    unsigned char hist[CFR_MAX_HISTORY];
    int hist_n;
    int frozen_n;
    int decision_player;
    uint64_t rng;
    int i;

    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 7070ULL));
    cfr_policy_provider_init_blueprint(&provider, &bp);
    cfr_abstraction_set_defaults(&cfg);
    TEST_ASSERT_TRUE(cfr_abstraction_use(&cfg));
    TEST_ASSERT_TRUE(cfr_hand_index_init());

    TEST_ASSERT_EQUAL_INT(2, cfr_parse_cards("AsKd", hole, 2));
    TEST_ASSERT_EQUAL_INT(3, cfr_parse_cards("AhKs2d", board, 5));
    hist_n = cfr_parse_history_text("c c / c r5", hist, CFR_MAX_HISTORY);
    TEST_ASSERT_TRUE(hist_n > 0);

    rng = 7071ULL;
    TEST_ASSERT_TRUE(cfr_build_search_round_root_with_frozen(&root,
                                                             frozen,
                                                             &frozen_n,
                                                             CFR_MAX_HISTORY,
                                                             0,
                                                             5,
                                                             2,
                                                             20,
                                                             0,
                                                             200,
                                                             1,
                                                             0,
                                                             hole,
                                                             board,
                                                             3,
                                                             hist,
                                                             hist_n,
                                                             CFR_OFFTREE_MODE_INJECT,
                                                             &rng));
    TEST_ASSERT_TRUE(frozen_n > 0);

    sim = root;
    for (i = 0; i < frozen_n; ++i)
    {
        TEST_ASSERT_TRUE(cfr_search_apply_observed_action(&sim,
                                                          frozen[i].player,
                                                          frozen[i].action,
                                                          frozen[i].target,
                                                          CFR_OFFTREE_MODE_INJECT,
                                                          &rng,
                                                          NULL));
    }
    cfr_auto_advance_rounds_if_needed(&sim);
    TEST_ASSERT_FALSE(sim.is_terminal);
    decision_player = sim.current_player;

    cfr_search_context_init(&ctx_a, 7072ULL);
    TEST_ASSERT_TRUE(cfr_search_decide(&ctx_a,
                                       &provider,
                                       &root,
                                       decision_player,
                                       48ULL,
                                       0ULL,
                                       3,
                                       4,
                                       CFR_SEARCH_PICK_SAMPLE_FINAL,
                                       CFR_OFFTREE_MODE_INJECT,
                                       frozen,
                                       frozen_n,
                                       &d_a));

    cfr_search_context_init(&ctx_b, 7072ULL);
    TEST_ASSERT_TRUE(cfr_search_decide(&ctx_b,
                                       &provider,
                                       &root,
                                       decision_player,
                                       48ULL,
                                       0ULL,
                                       3,
                                       4,
                                       CFR_SEARCH_PICK_SAMPLE_FINAL,
                                       CFR_OFFTREE_MODE_INJECT,
                                       frozen,
                                       frozen_n,
                                       &d_b));

    TEST_ASSERT_EQUAL_INT(d_a.legal_count, d_b.legal_count);
    TEST_ASSERT_EQUAL_INT(d_a.chosen_index, d_b.chosen_index);
    for (i = 0; i < d_a.legal_count; ++i)
    {
        TEST_ASSERT_FLOAT_WITHIN(1e-7f, d_a.final_policy[i], d_b.final_policy[i]);
    }
}

static void test_search_vector_switch_rule_matches_first_decision_policy(void)
{
    CFRHandState st;

    memset(&st, 0, sizeof(st));
    st.street = 0;
    st.current_player = 0;
    st.pot = 12;
    st.to_call = 2;
    st.last_full_raise = 2;
    st.in_hand[0] = 1;
    st.in_hand[1] = 1;
    st.stack[0] = 200;
    st.stack[1] = 200;
    st.committed_street[0] = 0;
    st.committed_street[1] = 0;

    TEST_ASSERT_FALSE(cfr_search_should_use_vector(&st, 3, 0, 2, 8, 5));
    TEST_ASSERT_TRUE(cfr_search_should_use_vector(&st, 3, 2, 2, 5, 3));
    TEST_ASSERT_FALSE(cfr_search_should_use_vector(&st, 3, 2, 4, 5, 3));

    st.street = 1;
    TEST_ASSERT_FALSE(cfr_search_should_use_vector(&st, 4, 0, 3, 7, 3));
    TEST_ASSERT_TRUE(cfr_search_should_use_vector(&st, 3, 2, 2, 5, 2));

    st.street = 2;
    TEST_ASSERT_TRUE(cfr_search_should_use_vector(&st, 3, 0, 3, 7, 2));
}

static void test_search_leaf_policy_stops_on_round_transition(void)
{
    CFRHandState st;

    memset(&st, 0, sizeof(st));
    st.street = 1;

    TEST_ASSERT_FALSE(cfr_search_should_stop_at_leaf(&st, 0, 3, 2, 2, 0));
    TEST_ASSERT_TRUE(cfr_search_should_stop_at_leaf(&st, 3, 3, 2, 2, 0));

    st.street = 2;
    TEST_ASSERT_TRUE(cfr_search_should_stop_at_leaf(&st, 0, 3, 1, 3, 0));
}

static void test_search_leaf_policy_stops_on_second_pot_increase_multiway_flop(void)
{
    CFRHandState st;

    memset(&st, 0, sizeof(st));
    st.street = 1;

    TEST_ASSERT_FALSE(cfr_search_should_stop_at_leaf(&st, 0, 5, 1, 3, 0));
    TEST_ASSERT_FALSE(cfr_search_should_stop_at_leaf(&st, 0, 5, 1, 3, 1));
    TEST_ASSERT_TRUE(cfr_search_should_stop_at_leaf(&st, 0, 5, 1, 3, 2));
}

static void test_search_limit_subgame_actions_caps_raises_to_five(void)
{
    int actions[CFR_MAX_ACTIONS];
    int targets[CFR_MAX_ACTIONS];
    int n;
    int i;
    int raise_n;
    int has_fold;
    int has_call;

    actions[0] = CFR_ACT_FOLD;
    targets[0] = 0;
    actions[1] = CFR_ACT_CALL_CHECK;
    targets[1] = 10;
    actions[2] = CFR_ACT_RAISE_TO;
    targets[2] = 20;
    actions[3] = CFR_ACT_RAISE_TO;
    targets[3] = 35;
    actions[4] = CFR_ACT_RAISE_TO;
    targets[4] = 55;
    actions[5] = CFR_ACT_RAISE_TO;
    targets[5] = 80;
    actions[6] = CFR_ACT_RAISE_TO;
    targets[6] = 120;
    actions[7] = CFR_ACT_ALL_IN;
    targets[7] = 200;

    n = cfr_search_limit_subgame_actions(actions, targets, 8);
    TEST_ASSERT_TRUE(n > 0);

    raise_n = 0;
    has_fold = 0;
    has_call = 0;
    for (i = 0; i < n; ++i)
    {
        if (actions[i] == CFR_ACT_FOLD)
        {
            has_fold = 1;
        }
        if (actions[i] == CFR_ACT_CALL_CHECK)
        {
            has_call = 1;
        }
        if (cfr_is_raise_action_code(actions[i]))
        {
            raise_n++;
        }
    }

    TEST_ASSERT_TRUE(has_fold);
    TEST_ASSERT_TRUE(has_call);
    TEST_ASSERT_TRUE(raise_n <= 5);
}

static void test_search_filter_frozen_for_player_keeps_only_own_actions(void)
{
    CFRSearchFrozenAction frozen[4];
    CFRSearchFrozenAction own[4];
    int n;

    frozen[0].player = 2;
    frozen[0].action = CFR_ACT_CALL_CHECK;
    frozen[0].target = 0;
    frozen[1].player = 1;
    frozen[1].action = CFR_ACT_RAISE_TO;
    frozen[1].target = 40;
    frozen[2].player = 3;
    frozen[2].action = CFR_ACT_CALL_CHECK;
    frozen[2].target = 0;
    frozen[3].player = 1;
    frozen[3].action = CFR_ACT_CALL_CHECK;
    frozen[3].target = 40;

    n = cfr_search_filter_frozen_for_player(frozen, 4, 1, own, 4);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT(1, own[0].player);
    TEST_ASSERT_EQUAL_INT(1, own[1].player);
    TEST_ASSERT_EQUAL_INT(CFR_ACT_RAISE_TO, own[0].action);
    TEST_ASSERT_EQUAL_INT(CFR_ACT_CALL_CHECK, own[1].action);
}

static void test_search_frozen_injection_keeps_dropped_raise_branch(void)
{
    static CFRBlueprint base_bp;
    static CFRBlueprint sub_a;
    static CFRBlueprint sub_b;
    CFRPolicyProvider provider;
    CFRHandState root;
    CFRHandState child;
    CFRSearchFrozenAction frozen[1];
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int limited_actions[CFR_MAX_ACTIONS];
    int limited_targets[CFR_MAX_ACTIONS];
    int legal_n;
    int limited_n;
    int i;
    int j;
    int picked_i;
    uint64_t deal_rng;
    uint64_t rng_a;
    uint64_t rng_b;
    double v_frozen;
    double v_direct;
    int traverser;

    TEST_ASSERT_TRUE(cfr_blueprint_init(&base_bp, 31337ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&sub_a, 31338ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&sub_b, 31338ULL));
    cfr_policy_provider_init_blueprint(&provider, &base_bp);

    deal_rng = 20260302ULL;
    cfr_init_hand(&root, 0, &deal_rng);
    TEST_ASSERT_TRUE(root.current_player >= 0 && root.current_player < CFR_MAX_PLAYERS);
    root.pot = 48;
    if (root.to_call <= root.committed_street[root.current_player])
    {
        root.to_call = root.committed_street[root.current_player] + 2;
    }
    root.last_full_raise = CFR_BIG_BLIND;
    root.num_raises_street = 0;
    root.full_raise_seq = 0;
    root.acted_this_street[root.current_player] = 0;
    root.acted_on_full_raise_seq[root.current_player] = 0;

    legal_n = cfr_get_legal_actions(&root, root.current_player, legal_actions, legal_targets);
    TEST_ASSERT_TRUE(legal_n > 0);
    for (i = 0; i < legal_n; ++i)
    {
        limited_actions[i] = legal_actions[i];
        limited_targets[i] = legal_targets[i];
    }
    limited_n = cfr_search_limit_subgame_actions(limited_actions, limited_targets, legal_n);
    TEST_ASSERT_TRUE(limited_n > 0);

    picked_i = -1;
    for (i = 0; i < legal_n; ++i)
    {
        int in_limited;
        if (!cfr_is_raise_action_code(legal_actions[i]))
        {
            continue;
        }
        in_limited = 0;
        for (j = 0; j < limited_n; ++j)
        {
            if (limited_actions[j] == legal_actions[i] && limited_targets[j] == legal_targets[i])
            {
                in_limited = 1;
                break;
            }
        }
        if (!in_limited)
        {
            picked_i = i;
            break;
        }
    }
    TEST_ASSERT_TRUE(picked_i >= 0);

    frozen[0].player = root.current_player;
    frozen[0].action = legal_actions[picked_i];
    frozen[0].target = legal_targets[picked_i];
    traverser = root.current_player;

    rng_a = 9001ULL;
    v_frozen = cfr_search_traverse(&sub_a,
                                   &provider,
                                   &root,
                                   traverser,
                                   0,
                                   2,
                                   root.street,
                                   cfr_count_in_hand(&root),
                                   0,
                                   1.0,
                                   0,
                                   &rng_a,
                                   frozen,
                                   1,
                                   NULL,
                                   0,
                                   CFR_OFFTREE_MODE_INJECT);

    child = root;
    rng_b = 9001ULL;
    TEST_ASSERT_TRUE(cfr_search_apply_observed_action(&child,
                                                      child.current_player,
                                                      frozen[0].action,
                                                      frozen[0].target,
                                                      CFR_OFFTREE_MODE_INJECT,
                                                      &rng_b,
                                                      NULL));
    v_direct = cfr_search_traverse(&sub_b,
                                   &provider,
                                   &child,
                                   traverser,
                                   1,
                                   2,
                                   root.street,
                                   cfr_count_in_hand(&root),
                                   cfr_search_pot_increase_step(&root, &child),
                                   1.0,
                                   0,
                                   &rng_b,
                                   NULL,
                                   0,
                                   NULL,
                                   0,
                                   CFR_OFFTREE_MODE_INJECT);

    TEST_ASSERT_TRUE(cfr_abs_d(v_direct - v_frozen) <= 1e-9);
}

static void test_search_inject_mode_accepts_non_template_raise_target(void)
{
    CFRHandState st;
    uint64_t rng;
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int legal_n;
    int p;
    int i;
    int min_raise;
    int max_raise;
    int req_target;
    int found_exact;
    int out_action;
    int out_target;
    int out_offtree;

    rng = 20260304ULL;
    cfr_init_hand(&st, 0, &rng);
    TEST_ASSERT_TRUE(st.current_player >= 0 && st.current_player < CFR_MAX_PLAYERS);
    p = st.current_player;
    st.pot = 48;
    if (st.to_call <= st.committed_street[p])
    {
        st.to_call = st.committed_street[p] + 2;
    }
    st.last_full_raise = CFR_BIG_BLIND;
    st.num_raises_street = 0;
    st.full_raise_seq = 0;
    st.acted_this_street[p] = 0;
    st.acted_on_full_raise_seq[p] = 0;

    legal_n = cfr_get_legal_actions(&st, p, legal_actions, legal_targets);
    TEST_ASSERT_TRUE(legal_n > 0);

    min_raise = -1;
    max_raise = -1;
    for (i = 0; i < legal_n; ++i)
    {
        if (!cfr_is_raise_action_code(legal_actions[i]))
        {
            continue;
        }
        if (min_raise < 0 || legal_targets[i] < min_raise)
        {
            min_raise = legal_targets[i];
        }
        if (max_raise < 0 || legal_targets[i] > max_raise)
        {
            max_raise = legal_targets[i];
        }
    }
    TEST_ASSERT_TRUE(min_raise > st.to_call);
    TEST_ASSERT_TRUE(max_raise > min_raise);

    req_target = min_raise + 1;
    found_exact = 1;
    while (req_target < max_raise && found_exact)
    {
        found_exact = 0;
        for (i = 0; i < legal_n; ++i)
        {
            if (cfr_is_raise_action_code(legal_actions[i]) && legal_targets[i] == req_target)
            {
                found_exact = 1;
                req_target++;
                break;
            }
        }
    }
    TEST_ASSERT_TRUE(req_target < max_raise);

    out_action = -1;
    out_target = -1;
    out_offtree = 0;
    TEST_ASSERT_TRUE(cfr_search_map_observed_action(&st,
                                                    p,
                                                    CFR_ACT_RAISE_TO,
                                                    req_target,
                                                    CFR_OFFTREE_MODE_INJECT,
                                                    &rng,
                                                    &out_action,
                                                    &out_target,
                                                    &out_offtree));
    TEST_ASSERT_TRUE(cfr_is_raise_action_code(out_action));
    TEST_ASSERT_EQUAL_INT(req_target, out_target);
    TEST_ASSERT_EQUAL_INT(1, out_offtree);

    out_action = -1;
    out_target = -1;
    out_offtree = 0;
    TEST_ASSERT_TRUE(cfr_search_map_observed_action(&st,
                                                    p,
                                                    CFR_ACT_RAISE_TO,
                                                    req_target,
                                                    CFR_OFFTREE_MODE_TRANSLATE,
                                                    &rng,
                                                    &out_action,
                                                    &out_target,
                                                    &out_offtree));
    TEST_ASSERT_TRUE(cfr_is_raise_action_code(out_action));
    TEST_ASSERT_TRUE(out_target != req_target);
}

static void test_search_inject_mode_materializes_addaction_support_in_subgame(void)
{
    CFRHandState st;
    CFRHandState child;
    CFRSearchFrozenAction frozen[1];
    CFRSearchDecision decision;
    CFRSearchContext ctx;
    static CFRBlueprint bp;
    uint64_t rng;
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int limited_actions[CFR_MAX_ACTIONS];
    int limited_targets[CFR_MAX_ACTIONS];
    int legal_n;
    int limited_n;
    int p;
    int i;
    int min_raise;
    int max_raise;
    int req_target;
    int found_exact;
    int next_player;
    CFRInfoKeyFields kf;
    uint64_t key;
    CFRNode *node;
    CFRPolicyProvider provider;

    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 20260306ULL));
    cfr_policy_provider_init_blueprint(&provider, &bp);
    cfr_search_context_init(&ctx, 20260306ULL);

    rng = 20260306ULL;
    cfr_init_hand(&st, 0, &rng);
    p = st.current_player;
    TEST_ASSERT_TRUE(p >= 0 && p < CFR_MAX_PLAYERS);
    st.pot = 48;
    if (st.to_call <= st.committed_street[p])
    {
        st.to_call = st.committed_street[p] + 2;
    }
    st.last_full_raise = CFR_BIG_BLIND;
    st.num_raises_street = 0;
    st.full_raise_seq = 0;
    st.acted_this_street[p] = 0;
    st.acted_on_full_raise_seq[p] = 0;

    legal_n = cfr_get_legal_actions(&st, p, legal_actions, legal_targets);
    TEST_ASSERT_TRUE(legal_n > 0);
    for (i = 0; i < legal_n; ++i)
    {
        limited_actions[i] = legal_actions[i];
        limited_targets[i] = legal_targets[i];
    }
    limited_n = cfr_search_limit_subgame_actions(limited_actions, limited_targets, legal_n);
    TEST_ASSERT_TRUE(limited_n > 0);

    min_raise = -1;
    max_raise = -1;
    for (i = 0; i < legal_n; ++i)
    {
        if (!cfr_is_raise_action_code(legal_actions[i]))
        {
            continue;
        }
        if (min_raise < 0 || legal_targets[i] < min_raise)
        {
            min_raise = legal_targets[i];
        }
        if (max_raise < 0 || legal_targets[i] > max_raise)
        {
            max_raise = legal_targets[i];
        }
    }
    TEST_ASSERT_TRUE(min_raise > st.to_call);
    TEST_ASSERT_TRUE(max_raise > min_raise);

    req_target = min_raise + 1;
    found_exact = 1;
    while (req_target < max_raise && found_exact)
    {
        found_exact = 0;
        for (i = 0; i < legal_n; ++i)
        {
            if (cfr_is_raise_action_code(legal_actions[i]) && legal_targets[i] == req_target)
            {
                found_exact = 1;
                req_target++;
                break;
            }
        }
    }
    TEST_ASSERT_TRUE(req_target < max_raise);

    for (i = 0; i < limited_n; ++i)
    {
        TEST_ASSERT_FALSE(cfr_is_raise_action_code(limited_actions[i]) && limited_targets[i] == req_target);
    }

    frozen[0].player = p;
    frozen[0].action = CFR_ACT_RAISE_TO;
    frozen[0].target = req_target;

    child = st;
    TEST_ASSERT_TRUE(cfr_search_apply_observed_action(&child,
                                                      p,
                                                      frozen[0].action,
                                                      frozen[0].target,
                                                      CFR_OFFTREE_MODE_INJECT,
                                                      &rng,
                                                      NULL));
    next_player = child.current_player;
    TEST_ASSERT_TRUE(next_player >= 0 && next_player < CFR_MAX_PLAYERS);

    TEST_ASSERT_TRUE(cfr_search_decide(&ctx,
                                       &provider,
                                       &st,
                                       next_player,
                                       32ULL,
                                       0ULL,
                                       2,
                                       1,
                                       CFR_SEARCH_PICK_SAMPLE_FINAL,
                                       CFR_OFFTREE_MODE_INJECT,
                                       frozen,
                                       1,
                                       &decision));

    TEST_ASSERT_TRUE(cfr_extract_infoset_fields_mode(&st, p, CFR_ABS_MODE_SEARCH, st.street, &kf));
    key = cfr_make_infoset_key(&kf);
    node = cfr_blueprint_get_node(&ctx.subgame_bp, key, 0, 1);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_TRUE(node->action_count > limited_n);
}

static void test_match_policy_picker_returns_legal_action(void)
{
    static CFRBlueprint bp;
    CFRHandState st;
    uint64_t rng;
    int action;
    int target;
    int legal_actions[CFR_MAX_ACTIONS];
    int legal_targets[CFR_MAX_ACTIONS];
    int n;
    int i;
    int found;

    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 999ULL));
    rng = 321ULL;
    cfr_init_hand(&st, 0, &rng);
    TEST_ASSERT_TRUE(st.current_player >= 0);

    TEST_ASSERT_TRUE(cfr_policy_pick_action(&bp, &st, st.current_player, &rng, &action, &target));
    n = cfr_get_legal_actions(&st, st.current_player, legal_actions, legal_targets);
    TEST_ASSERT_TRUE(n > 0);

    found = 0;
    for (i = 0; i < n; ++i)
    {
        if (legal_actions[i] == action && legal_targets[i] == target)
        {
            found = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

static void test_match_runtime_profile_pluribus_trigger_logic(void)
{
    CFRMatchOptions opt;
    CFRHandState st;
    uint64_t rng;
    int diff_small;
    int diff_large;
    int ok_small;
    int ok_large;

    memset(&opt, 0, sizeof(opt));
    memset(&st, 0, sizeof(st));
    opt.runtime_profile = CFR_RUNTIME_PROFILE_PLURIBUS;
    opt.use_search = 0;
    opt.offtree_mode = CFR_OFFTREE_MODE_INJECT;

    st.street = 0;
    st.dealer = 0;
    st.pot = 1600;
    st.to_call = 0;
    st.in_hand[0] = 1;
    st.in_hand[1] = 1;
    st.in_hand[2] = 1;
    st.in_hand[3] = 1;
    st.stack[0] = 4000;
    st.stack[1] = 4000;
    st.stack[2] = 4000;
    st.stack[3] = 4000;
    st.history_len = 3;
    st.history[0] = (unsigned char)CFR_ACT_RAISE_TO;
    st.history[1] = 250u;

    st.history[2] = 0u;
    rng = 12345ULL;
    ok_small = cfr_match_preflop_last_opponent_raise_diff(&st,
                                                           1,
                                                           opt.offtree_mode,
                                                           &rng,
                                                           &diff_small);
    TEST_ASSERT_TRUE(ok_small);

    st.history[2] = 255u;
    rng = 12345ULL;
    ok_large = cfr_match_preflop_last_opponent_raise_diff(&st,
                                                           1,
                                                           opt.offtree_mode,
                                                           &rng,
                                                           &diff_large);
    TEST_ASSERT_TRUE(ok_large);
    TEST_ASSERT_TRUE(diff_large >= diff_small);

    TEST_ASSERT_EQUAL_INT((diff_large > 100) ? 1 : 0,
                          cfr_match_should_use_search_for_state(&opt, &st, 1, 77ULL));

    st.street = 1;
    TEST_ASSERT_TRUE(cfr_match_should_use_search_for_state(&opt, &st, 1, 77ULL));

    st.street = 0;
    st.in_hand[4] = 1;
    st.stack[4] = 4000;
    TEST_ASSERT_FALSE(cfr_match_should_use_search_for_state(&opt, &st, 1, 77ULL));
}

static void test_cmd_match_search_mode_smoke(void)
{
    static CFRBlueprint bp;
    const char *tmp_a = "tests_tmp_match_search_a.bin";
    const char *tmp_b = "tests_tmp_match_search_b.bin";
    char *argv[22];
    int argc;
    int rc;

    memset(&bp, 0, sizeof(bp));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 8888ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_save(&bp, tmp_a));
    TEST_ASSERT_TRUE(cfr_blueprint_save(&bp, tmp_b));

    argc = 0;
    argv[argc++] = "--a";
    argv[argc++] = (char *)tmp_a;
    argv[argc++] = "--b";
    argv[argc++] = (char *)tmp_b;
    argv[argc++] = "--hands";
    argv[argc++] = "2";
    argv[argc++] = "--mode";
    argv[argc++] = "search";
    argv[argc++] = "--search-iters";
    argv[argc++] = "8";
    argv[argc++] = "--search-depth";
    argv[argc++] = "2";
    argv[argc++] = "--search-threads";
    argv[argc++] = "1";
    argv[argc++] = "--search-pick";
    argv[argc++] = "sample-final";
    argv[argc++] = "--offtree-mode";
    argv[argc++] = "inject";
    argv[argc++] = "--seed";
    argv[argc++] = "20260302";

    rc = cfr_cmd_match(argc, argv);
    TEST_ASSERT_EQUAL_INT(0, rc);

    remove(tmp_a);
    remove(tmp_b);
}

static void test_cmd_match_runtime_profile_pluribus_smoke(void)
{
    static CFRBlueprint bp;
    const char *tmp_a = "tests_tmp_match_runtime_a.bin";
    const char *tmp_b = "tests_tmp_match_runtime_b.bin";
    char *argv[20];
    int argc;
    int rc;

    memset(&bp, 0, sizeof(bp));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 7777ULL));
    TEST_ASSERT_TRUE(cfr_blueprint_save(&bp, tmp_a));
    TEST_ASSERT_TRUE(cfr_blueprint_save(&bp, tmp_b));

    argc = 0;
    argv[argc++] = "--a";
    argv[argc++] = (char *)tmp_a;
    argv[argc++] = "--b";
    argv[argc++] = (char *)tmp_b;
    argv[argc++] = "--hands";
    argv[argc++] = "2";
    argv[argc++] = "--runtime-profile";
    argv[argc++] = "pluribus";
    argv[argc++] = "--search-iters";
    argv[argc++] = "8";
    argv[argc++] = "--search-depth";
    argv[argc++] = "2";
    argv[argc++] = "--search-threads";
    argv[argc++] = "1";
    argv[argc++] = "--seed";
    argv[argc++] = "20260303";

    rc = cfr_cmd_match(argc, argv);
    TEST_ASSERT_EQUAL_INT(0, rc);

    remove(tmp_a);
    remove(tmp_b);
}

static void test_history_key_state_and_codes_match(void)
{
    CFRHandState st;
    unsigned char hist[CFR_MAX_HISTORY];
    int n;
    uint32_t h_codes;
    uint32_t h_state;

    memset(&st, 0, sizeof(st));

    n = cfr_parse_history_text("c r3 / c a", hist, CFR_MAX_HISTORY);
    TEST_ASSERT_TRUE(n > 0);
    memcpy(st.history, hist, (size_t)n);
    st.history_len = n;

    h_codes = cfr_history_key_from_codes(hist, n);
    h_state = cfr_history_key_from_state(&st);
    TEST_ASSERT_EQUAL_UINT32(h_codes, h_state);
}

static void test_infoset_key_is_history_sensitive(void)
{
    CFRInfoKeyFields a;
    CFRInfoKeyFields b;
    uint64_t ka;
    uint64_t kb;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));

    a.street = 1;
    a.position = 2;
    a.hand_index = 12345ULL;
    a.pot_bucket = 3;
    a.to_call_bucket = 2;
    a.active_players = 4;
    a.history_hash = cfr_history_key_from_codes((const unsigned char *)"abc", 3);

    b = a;
    b.history_hash = cfr_history_key_from_codes((const unsigned char *)"abd", 3);

    ka = cfr_make_infoset_key(&a);
    kb = cfr_make_infoset_key(&b);
    TEST_ASSERT_TRUE(ka != kb);
}

static void test_search_server_split_args_supports_quotes(void)
{
    char line[] = "--hole AsKd --history \"c r2 / c\" --time-ms 2000";
    char *argv[16];
    int argc;

    argc = cfr_search_server_split_args(line, argv, 16);
    TEST_ASSERT_EQUAL_INT(6, argc);
    TEST_ASSERT_EQUAL_STRING("--hole", argv[0]);
    TEST_ASSERT_EQUAL_STRING("AsKd", argv[1]);
    TEST_ASSERT_EQUAL_STRING("--history", argv[2]);
    TEST_ASSERT_EQUAL_STRING("c r2 / c", argv[3]);
    TEST_ASSERT_EQUAL_STRING("--time-ms", argv[4]);
    TEST_ASSERT_EQUAL_STRING("2000", argv[5]);
}

static void test_search_options_parse_disallows_model_flags_when_locked(void)
{
    CFRSearchOptions opt;
    char *argv[] = {"--blueprint", "x.bin"};
    int rc;

    cfr_search_options_set_defaults(&opt);
    rc = cfr_search_options_parse(&opt, 2, argv, 0, 0, "");
    TEST_ASSERT_EQUAL_INT(0, rc);

    cfr_search_options_set_defaults(&opt);
    argv[0] = "--runtime-blueprint";
    argv[1] = "x.rt.bin";
    rc = cfr_search_options_parse(&opt, 2, argv, 0, 0, "");
    TEST_ASSERT_EQUAL_INT(0, rc);
}

static void test_search_options_parse_accepts_runtime_blueprint(void)
{
    CFRSearchOptions opt;
    char *argv[] = {"--runtime-blueprint", "rt.bin", "--cache-bytes", "4096", "--prefetch", "preflop"};
    int rc;

    cfr_search_options_set_defaults(&opt);
    rc = cfr_search_options_parse(&opt, 6, argv, 1, 0, "");
    TEST_ASSERT_EQUAL_INT(1, rc);
    TEST_ASSERT_EQUAL_STRING("rt.bin", opt.runtime_blueprint_path);
    TEST_ASSERT_EQUAL_UINT64(4096ULL, opt.cache_bytes);
    TEST_ASSERT_EQUAL_INT(CFR_RUNTIME_PREFETCH_PREFLOP, opt.runtime_prefetch_mode);
}

static void test_runtime_blueprint_save_load_roundtrip(void)
{
    static CFRBlueprint bp;
    CFRRuntimeBlueprint rt;
    CFRPolicyProvider provider;
    CFRNode *n;
    float got[3];
    const char *runtime_file = "tests_tmp_runtime.bin";

    memset(&bp, 0, sizeof(bp));
    TEST_ASSERT_TRUE(cfr_blueprint_init(&bp, 8181ULL));
    bp.compat_hash32 = 0x12345678u;
    bp.abstraction_hash32 = 0x9ABCDEF0u;

    n = cfr_blueprint_get_node_ex(&bp, 0xABCDEFULL, 1, 3, 2);
    TEST_ASSERT_NOT_NULL(n);
    n->street_hint = 2u;
    n->strategy_sum[0] = 3.0f;
    n->strategy_sum[1] = 1.0f;
    n->strategy_sum[2] = 2.0f;

    TEST_ASSERT_TRUE(cfr_runtime_blueprint_save(&bp, runtime_file, CFR_RUNTIME_QUANT_U16, 32u));
    TEST_ASSERT_TRUE(cfr_runtime_blueprint_open(&rt, runtime_file, 4096ULL, CFR_RUNTIME_PREFETCH_NONE));
    cfr_policy_provider_init_runtime(&provider, &rt);

    TEST_ASSERT_EQUAL_UINT32(bp.compat_hash32, cfr_policy_provider_compat_hash32(&provider));
    TEST_ASSERT_EQUAL_UINT32(bp.abstraction_hash32, cfr_policy_provider_abstraction_hash32(&provider));
    TEST_ASSERT_TRUE(cfr_policy_provider_get_average_policy(&provider, 0xABCDEFULL, 2, 3, got));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, got[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f / 6.0f, got[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f / 3.0f, got[2]);

    cfr_runtime_blueprint_close(&rt);
    remove(runtime_file);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    UNITY_BEGIN();
    RUN_TEST(test_regret_matching_uses_regret_plus);
    RUN_TEST(test_regret_matching_uniform_when_all_non_positive);
    RUN_TEST(test_infoset_key_is_deterministic);
    RUN_TEST(test_hand_index_sizes_match_holdem_known_values);
    RUN_TEST(test_hand_index_is_suit_isomorphic_preflop);
    RUN_TEST(test_hand_index_is_suit_isomorphic_flop);
    RUN_TEST(test_hand_index_rejects_duplicate_cards);
    RUN_TEST(test_blueprint_save_load_roundtrip);
    RUN_TEST(test_blueprint_infoset_table_grows_past_initial_capacity);
    RUN_TEST(test_preflop_infosets_use_generic_sparse_hash_path);
    RUN_TEST(test_blueprint_rehash_preserves_preflop_and_postflop_nodes);
    RUN_TEST(test_blueprint_init_uses_small_eager_allocation_profile);
    RUN_TEST(test_card_parse_and_format_roundtrip);
    RUN_TEST(test_phe_raw_rank_ordering_is_lower_better);
    RUN_TEST(test_range_combo_index_roundtrip);
    RUN_TEST(test_range_uniform_blocked_excludes_card);
    RUN_TEST(test_range_apply_likelihood_bayes_update);
    RUN_TEST(test_range_apply_blockers_renormalizes);
    RUN_TEST(test_range_likelihood_uses_blueprint_action_prob);
    RUN_TEST(test_belief_update_player_action_shifts_probability_mass);
    RUN_TEST(test_belief_update_prefers_search_sigma_when_available);
    RUN_TEST(test_belief_round_root_blockers_update_opponent_range);
    RUN_TEST(test_legal_actions_include_dynamic_raises_and_allin);
    RUN_TEST(test_preflop_action_abstraction_offers_many_raise_sizes);
    RUN_TEST(test_raise_action_appends_raise_bucket_history_marker);
    RUN_TEST(test_short_allin_does_not_reopen_raising);
    RUN_TEST(test_full_raise_reopens_raising);
    RUN_TEST(test_short_allin_still_allows_raise_for_player_who_has_not_acted);
    RUN_TEST(test_min_raise_target_respects_last_full_raise);
    RUN_TEST(test_offtree_raise_maps_to_nearest_legal_target);
    RUN_TEST(test_offtree_raise_pseudo_harmonic_maps_between_adjacent_targets);
    RUN_TEST(test_total_chips_conserved_through_hand);
    RUN_TEST(test_showdown_resolves_side_pots_correctly);
    RUN_TEST(test_trainer_node_growth_and_no_nan_state);
    RUN_TEST(test_seeded_parallel_training_is_deterministic);
    RUN_TEST(test_seeded_parallel_training_is_deterministic_24_threads);
    RUN_TEST(test_seeded_parallel_training_is_deterministic_sharded);
    RUN_TEST(test_parallel_modes_deterministic_and_sharded_match);
    RUN_TEST(test_seeded_resume_equivalence_single_thread);
    RUN_TEST(test_convergence_sanity_strategy_drift_tapers);
    RUN_TEST(test_schedule_helpers_match_expected_cadence);
    RUN_TEST(test_time_discount_schedule_applies_event_cadence_and_stop);
    RUN_TEST(test_pluribus_profile_warmup_gates_preflop_avg_and_snapshots);
    RUN_TEST(test_strategy_interval_gates_strategy_sum_updates);
    RUN_TEST(test_training_accumulates_preflop_strategy_only);
    RUN_TEST(test_postflop_sampled_opponent_path_does_not_create_read_only_root_node);
    RUN_TEST(test_overlay_preflop_read_only_lookup_returns_base_node);
    RUN_TEST(test_preflop_avg_sampled_mode_updates_single_action_counter);
    RUN_TEST(test_worker_shard_partition_is_stable_and_complete);
    RUN_TEST(test_worker_delta_marks_only_missing_base_nodes_for_precreate);
    RUN_TEST(test_strict_time_phase_chunk_is_parallel_capped);
    RUN_TEST(test_int_regret_mode_quantizes_and_respects_floor);
    RUN_TEST(test_train_compat_hash_changes_with_solver_settings);
    RUN_TEST(test_train_rejects_removed_snapshot_postflop_flag);
    RUN_TEST(test_train_compat_hash_changes_with_abstraction_hash);
    RUN_TEST(test_train_compat_hash_changes_with_parallel_mode);
    RUN_TEST(test_train_compat_hash_changes_with_preflop_avg_mode);
    RUN_TEST(test_abstraction_default_config_is_valid_and_hash_stable);
    RUN_TEST(test_abstraction_save_load_roundtrip);
    RUN_TEST(test_abstraction_bucket_preflop_is_lossless);
    RUN_TEST(test_abstraction_build_centroids_enables_feature_bucketing);
    RUN_TEST(test_blueprint_roundtrip_preserves_street_hint_and_abstraction_hash);
    RUN_TEST(test_blueprint_roundtrip_preserves_missing_postflop_strategy_payload);
    RUN_TEST(test_snapshot_postflop_average_applies_only_postflop_nodes);
    RUN_TEST(test_compact_snapshot_save_load_accumulates_postflop_current);
    RUN_TEST(test_finalize_blueprint_is_deterministic_and_preserves_preflop);
    RUN_TEST(test_finalize_blueprint_snapshot_min_seconds_filters_old_snapshots);
    RUN_TEST(test_finalize_blueprint_runtime_only_skips_full_output);
    RUN_TEST(test_search_decide_returns_legal_policy);
    RUN_TEST(test_search_decide_reports_belief_updates_for_frozen_history);
    RUN_TEST(test_search_decide_sample_final_is_seed_deterministic);
    RUN_TEST(test_search_parallel_with_frozen_is_seed_deterministic);
    RUN_TEST(test_build_search_round_root_with_frozen_extracts_current_street_actions);
    RUN_TEST(test_search_vector_switch_rule_matches_first_decision_policy);
    RUN_TEST(test_search_leaf_policy_stops_on_round_transition);
    RUN_TEST(test_search_leaf_policy_stops_on_second_pot_increase_multiway_flop);
    RUN_TEST(test_search_limit_subgame_actions_caps_raises_to_five);
    RUN_TEST(test_search_filter_frozen_for_player_keeps_only_own_actions);
    RUN_TEST(test_search_frozen_injection_keeps_dropped_raise_branch);
    RUN_TEST(test_search_inject_mode_accepts_non_template_raise_target);
    RUN_TEST(test_search_inject_mode_materializes_addaction_support_in_subgame);
    RUN_TEST(test_match_policy_picker_returns_legal_action);
    RUN_TEST(test_match_runtime_profile_pluribus_trigger_logic);
    RUN_TEST(test_cmd_match_search_mode_smoke);
    RUN_TEST(test_cmd_match_runtime_profile_pluribus_smoke);
    RUN_TEST(test_history_key_state_and_codes_match);
    RUN_TEST(test_infoset_key_is_history_sensitive);
    RUN_TEST(test_search_server_split_args_supports_quotes);
    RUN_TEST(test_search_options_parse_disallows_model_flags_when_locked);
    RUN_TEST(test_search_options_parse_accepts_runtime_blueprint);
    RUN_TEST(test_runtime_blueprint_save_load_roundtrip);
    return UNITY_END();
}

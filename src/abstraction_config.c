static CFRAbstractionConfig g_cfr_abstraction_cfg;
static int g_cfr_abstraction_cfg_ready = 0;

#define CFR_ABS_FEATURE_CACHE_SIZE (1u << 18) /* 262,144 entries */

typedef struct
{
    uint64_t key;
    unsigned char used;
    unsigned char has_hist;
    unsigned char _pad0;
    unsigned char _pad1;
    float feat[CFR_ABS_FEATURE_DIM];
    float hist[CFR_ABS_EMD_BINS];
} CFRAbsFeatureCacheEntry;

static volatile LONG g_cfr_abs_feature_cache_epoch = 1;
#ifdef _WIN32
__declspec(thread) static CFRAbsFeatureCacheEntry *g_cfr_abs_feature_cache_tls = NULL;
__declspec(thread) static uint32_t g_cfr_abs_feature_cache_tls_epoch = 0u;
#else
static CFRAbsFeatureCacheEntry *g_cfr_abs_feature_cache_tls = NULL;
static uint32_t g_cfr_abs_feature_cache_tls_epoch = 0u;
#endif

static void cfr_abs_hash_mix_u32(uint32_t *h, uint32_t v)
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

static void cfr_abs_hash_mix_u64(uint32_t *h, uint64_t v)
{
    cfr_abs_hash_mix_u32(h, (uint32_t)(v & 0xFFFFFFFFu));
    cfr_abs_hash_mix_u32(h, (uint32_t)(v >> 32));
}

static void cfr_abs_hash_mix_f32(uint32_t *h, float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    cfr_abs_hash_mix_u32(h, bits);
}

static uint64_t cfr_abs_splitmix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
}

static double cfr_abs_feature_weight(int dim)
{
    /* Emphasize future potential dimensions over raw strength moments. */
    if (dim == 0) return 1.0;  /* E[HS] */
    if (dim == 1) return 0.75; /* E[HS^2] */
    if (dim == 2) return 1.30; /* potential proxy */
    return 1.50;               /* positive potential proxy */
}

static double cfr_abs_weighted_dist2_vec(const float *a, const float *b)
{
    int d;
    double dist_l2;
    double dist_emd_proxy;
    double ca;
    double cb;

    dist_l2 = 0.0;
    dist_emd_proxy = 0.0;
    ca = 0.0;
    cb = 0.0;
    for (d = 0; d < CFR_ABS_FEATURE_DIM; ++d)
    {
        double dv;
        double w;
        double cw;
        dv = (double)a[d] - (double)b[d];
        w = cfr_abs_feature_weight(d);
        cw = 0.6 + 0.2 * w;
        dist_l2 += w * dv * dv;
        ca += (double)a[d];
        cb += (double)b[d];
        if (ca >= cb)
        {
            dist_emd_proxy += cw * (ca - cb);
        }
        else
        {
            dist_emd_proxy += cw * (cb - ca);
        }
    }
    /*
     * Hybrid metric:
     * - weighted L2 keeps smooth centroid optimization behavior
     * - cumulative-difference term approximates EMD-style distribution mismatch
     */
    return dist_l2 + 0.75 * dist_emd_proxy;
}

static double cfr_abs_hist_emd_w1(const float *a, const float *b, int n_bins)
{
    int i;
    double ca;
    double cb;
    double emd;

    if (a == NULL || b == NULL || n_bins <= 1)
    {
        return 0.0;
    }

    ca = 0.0;
    cb = 0.0;
    emd = 0.0;
    for (i = 0; i < n_bins; ++i)
    {
        ca += (double)a[i];
        cb += (double)b[i];
        emd += (ca >= cb) ? (ca - cb) : (cb - ca);
    }
    /* Normalize by support width so score remains roughly in [0,1]. */
    return emd / (double)(n_bins - 1);
}

static double cfr_abs_profile_dist(const float *feat_a,
                                   const float *hist_a,
                                   const float *feat_b,
                                   const float *hist_b,
                                   int n_bins)
{
    double emd;
    double pot_l1;
    double shape_l2;
    double tail_l1;

    emd = cfr_abs_hist_emd_w1(hist_a, hist_b, n_bins);
    pot_l1 = 0.0;
    tail_l1 = 0.0;
    if (hist_a != NULL && hist_b != NULL && n_bins >= 4)
    {
        int i;
        int tail_begin;
        double tail_a;
        double tail_b;

        tail_begin = (3 * n_bins) / 4;
        tail_a = 0.0;
        tail_b = 0.0;
        for (i = tail_begin; i < n_bins; ++i)
        {
            tail_a += (double)hist_a[i];
            tail_b += (double)hist_b[i];
        }
        tail_l1 = (tail_a >= tail_b) ? (tail_a - tail_b) : (tail_b - tail_a);
    }
    if (feat_a != NULL && feat_b != NULL)
    {
        int d;
        for (d = 2; d < CFR_ABS_FEATURE_DIM; ++d)
        {
            double dv;
            dv = (double)feat_a[d] - (double)feat_b[d];
            pot_l1 += (dv >= 0.0) ? dv : -dv;
        }
        shape_l2 = cfr_abs_weighted_dist2_vec(feat_a, feat_b);
    }
    else
    {
        shape_l2 = 0.0;
    }

    /* EMD dominates; potential/tail terms tighten clustering on draw-sensitive regions. */
    return emd + 0.25 * pot_l1 + 0.08 * shape_l2 + 0.35 * tail_l1;
}

static uint64_t cfr_abs_feature_key(int street, uint64_t canonical_index)
{
    uint64_t k;
    k = canonical_index;
    k ^= ((uint64_t)(street & 0xFF) << 56);
    k ^= g_cfr_abstraction_cfg.seed;
    return cfr_abs_splitmix64(k);
}

static uint32_t cfr_abs_feature_cache_epoch_now(void)
{
#ifdef _WIN32
    return (uint32_t)InterlockedCompareExchange(&g_cfr_abs_feature_cache_epoch, 0, 0);
#else
    return (uint32_t)g_cfr_abs_feature_cache_epoch;
#endif
}

static void cfr_abs_feature_cache_clear(void)
{
#ifdef _WIN32
    (void)InterlockedIncrement(&g_cfr_abs_feature_cache_epoch);
#else
    g_cfr_abs_feature_cache_epoch++;
    if (g_cfr_abs_feature_cache_tls != NULL)
    {
        memset(g_cfr_abs_feature_cache_tls, 0, (size_t)CFR_ABS_FEATURE_CACHE_SIZE * sizeof(*g_cfr_abs_feature_cache_tls));
    }
#endif
}

static CFRAbsFeatureCacheEntry *cfr_abs_feature_cache_entries(void)
{
    uint32_t epoch_now;

    if (g_cfr_abs_feature_cache_tls == NULL)
    {
        g_cfr_abs_feature_cache_tls = (CFRAbsFeatureCacheEntry *)calloc((size_t)CFR_ABS_FEATURE_CACHE_SIZE, sizeof(*g_cfr_abs_feature_cache_tls));
        if (g_cfr_abs_feature_cache_tls == NULL)
        {
            return NULL;
        }
        g_cfr_abs_feature_cache_tls_epoch = 0u;
    }

    epoch_now = cfr_abs_feature_cache_epoch_now();
    if (g_cfr_abs_feature_cache_tls_epoch != epoch_now)
    {
        memset(g_cfr_abs_feature_cache_tls, 0, (size_t)CFR_ABS_FEATURE_CACHE_SIZE * sizeof(*g_cfr_abs_feature_cache_tls));
        g_cfr_abs_feature_cache_tls_epoch = epoch_now;
    }

    return g_cfr_abs_feature_cache_tls;
}

static int cfr_abs_feature_cache_lookup(uint64_t key,
                                        float out_feat[CFR_ABS_FEATURE_DIM],
                                        float out_hist[CFR_ABS_EMD_BINS])
{
    uint32_t idx;
    CFRAbsFeatureCacheEntry *entries;
    CFRAbsFeatureCacheEntry *e;

    entries = cfr_abs_feature_cache_entries();
    if (entries == NULL)
    {
        return 0;
    }

    idx = (uint32_t)(key % (uint64_t)CFR_ABS_FEATURE_CACHE_SIZE);
    e = &entries[idx];
    if (e->used && e->key == key)
    {
        if (out_feat != NULL)
        {
            memcpy(out_feat, e->feat, sizeof(e->feat));
        }
        if (out_hist != NULL && e->has_hist)
        {
            memcpy(out_hist, e->hist, sizeof(e->hist));
        }
        return 1;
    }
    return 0;
}

static void cfr_abs_feature_cache_store(uint64_t key,
                                        const float feat[CFR_ABS_FEATURE_DIM],
                                        const float hist[CFR_ABS_EMD_BINS])
{
    uint32_t idx;
    CFRAbsFeatureCacheEntry *entries;
    CFRAbsFeatureCacheEntry *e;

    entries = cfr_abs_feature_cache_entries();
    if (entries == NULL)
    {
        return;
    }

    idx = (uint32_t)(key % (uint64_t)CFR_ABS_FEATURE_CACHE_SIZE);
    e = &entries[idx];
    e->used = 1u;
    e->has_hist = (hist != NULL) ? 1u : 0u;
    e->key = key;
    memcpy(e->feat, feat, sizeof(e->feat));
    if (hist != NULL)
    {
        memcpy(e->hist, hist, sizeof(e->hist));
    }
}

static uint32_t cfr_abstraction_hash32(const CFRAbstractionConfig *cfg)
{
    uint32_t h;
    int i;
    int m;
    int s;

    if (cfg == NULL)
    {
        return 0u;
    }

    h = 2166136261u;
    cfr_abs_hash_mix_u64(&h, cfg->magic);
    cfr_abs_hash_mix_u32(&h, cfg->version);
    for (i = 0; i < 4; ++i)
    {
        cfr_abs_hash_mix_u32(&h, cfg->street_bucket_count_blueprint[i]);
    }
    for (i = 0; i < 4; ++i)
    {
        cfr_abs_hash_mix_u32(&h, cfg->street_bucket_count_search[i]);
    }
    cfr_abs_hash_mix_u64(&h, cfg->seed);
    cfr_abs_hash_mix_u32(&h, cfg->feature_mc_samples);
    cfr_abs_hash_mix_u32(&h, cfg->kmeans_iters);
    cfr_abs_hash_mix_u32(&h, cfg->build_samples_per_street);
    cfr_abs_hash_mix_u32(&h, cfg->clustering_algo);
    cfr_abs_hash_mix_u32(&h, cfg->emd_bins);

    for (m = 0; m < 2; ++m)
    {
        for (s = 0; s < 4; ++s)
        {
            uint32_t k;
            uint32_t bucket_n;
            cfr_abs_hash_mix_u32(&h, cfg->centroid_ready[m][s]);
            cfr_abs_hash_mix_u32(&h, cfg->emd_ready[m][s]);
            if (!cfg->centroid_ready[m][s] || s == 0)
            {
                bucket_n = 0u;
            }
            else
            {
                bucket_n = (m == CFR_ABS_MODE_SEARCH)
                               ? cfg->street_bucket_count_search[s]
                               : cfg->street_bucket_count_blueprint[s];
                if (bucket_n > CFR_ABS_MAX_BUCKETS)
                {
                    bucket_n = CFR_ABS_MAX_BUCKETS;
                }
                for (k = 0; k < bucket_n; ++k)
                {
                    int d;
                    for (d = 0; d < CFR_ABS_FEATURE_DIM; ++d)
                    {
                        cfr_abs_hash_mix_f32(&h, cfg->centroids[m][s][k][d]);
                    }
                }
            }

            if (!cfg->emd_ready[m][s] || s == 0)
            {
                continue;
            }
            bucket_n = (m == CFR_ABS_MODE_SEARCH)
                           ? cfg->street_bucket_count_search[s]
                           : cfg->street_bucket_count_blueprint[s];
            if (bucket_n > CFR_ABS_MAX_BUCKETS)
            {
                bucket_n = CFR_ABS_MAX_BUCKETS;
            }
            for (k = 0; k < bucket_n; ++k)
            {
                int b;
                for (b = 0; b < CFR_ABS_EMD_BINS; ++b)
                {
                    cfr_abs_hash_mix_f32(&h, cfg->emd_medoids[m][s][k][b]);
                }
            }
            cfr_abs_hash_mix_f32(&h, cfg->emd_quality[m][s][0]);
            cfr_abs_hash_mix_f32(&h, cfg->emd_quality[m][s][1]);
        }
    }

    return h;
}

static void cfr_abstraction_set_defaults(CFRAbstractionConfig *cfg)
{
    if (cfg == NULL)
    {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic = CFR_ABSTRACTION_MAGIC;
    cfg->version = CFR_ABSTRACTION_VERSION;
    cfg->street_bucket_count_blueprint[0] = 169u;
    cfg->street_bucket_count_blueprint[1] = 200u;
    cfg->street_bucket_count_blueprint[2] = 200u;
    cfg->street_bucket_count_blueprint[3] = 200u;
    cfg->street_bucket_count_search[0] = 169u;
    cfg->street_bucket_count_search[1] = 500u;
    cfg->street_bucket_count_search[2] = 500u;
    cfg->street_bucket_count_search[3] = 500u;
    cfg->seed = 0xA9D17C4E82B55D3FULL;
    cfg->feature_mc_samples = 12u;
    cfg->kmeans_iters = 24u;
    cfg->build_samples_per_street = 8000u;
    cfg->clustering_algo = CFR_ABS_CLUSTER_ALGO_EMD_KMEDOIDS;
    cfg->emd_bins = CFR_ABS_EMD_BINS;
    cfg->hash32 = cfr_abstraction_hash32(cfg);
}

static int cfr_abstraction_validate(CFRAbstractionConfig *cfg)
{
    int i;
    int m;
    int s;

    if (cfg == NULL)
    {
        return 0;
    }
    if (cfg->magic != CFR_ABSTRACTION_MAGIC || cfg->version != CFR_ABSTRACTION_VERSION)
    {
        return 0;
    }

    if (cfg->street_bucket_count_blueprint[0] != 169u || cfg->street_bucket_count_search[0] != 169u)
    {
        return 0;
    }

    for (i = 0; i < 4; ++i)
    {
        if (cfg->street_bucket_count_blueprint[i] == 0u || cfg->street_bucket_count_blueprint[i] > CFR_ABS_MAX_BUCKETS)
        {
            return 0;
        }
        if (cfg->street_bucket_count_search[i] == 0u || cfg->street_bucket_count_search[i] > CFR_ABS_MAX_BUCKETS)
        {
            return 0;
        }
    }

    if (cfg->feature_mc_samples == 0u)
    {
        cfg->feature_mc_samples = 1u;
    }
    if (cfg->kmeans_iters == 0u)
    {
        cfg->kmeans_iters = 1u;
    }
    if (cfg->build_samples_per_street == 0u)
    {
        cfg->build_samples_per_street = 1024u;
    }
    if (cfg->clustering_algo != CFR_ABS_CLUSTER_ALGO_LEGACY &&
        cfg->clustering_algo != CFR_ABS_CLUSTER_ALGO_EMD_KMEDOIDS)
    {
        cfg->clustering_algo = CFR_ABS_CLUSTER_ALGO_EMD_KMEDOIDS;
    }
    if (cfg->emd_bins == 0u)
    {
        cfg->emd_bins = CFR_ABS_EMD_BINS;
    }
    if (cfg->emd_bins > CFR_ABS_EMD_BINS)
    {
        cfg->emd_bins = CFR_ABS_EMD_BINS;
    }

    for (m = 0; m < 2; ++m)
    {
        for (s = 0; s < 4; ++s)
        {
            if (s == 0)
            {
                cfg->centroid_ready[m][s] = 0u;
                cfg->emd_ready[m][s] = 0u;
            }
            else if (cfg->centroid_ready[m][s] > 1u)
            {
                cfg->centroid_ready[m][s] = 1u;
            }
            if (cfg->emd_ready[m][s] > 1u)
            {
                cfg->emd_ready[m][s] = 1u;
            }
        }
    }

    return 1;
}

static int cfr_abstraction_use(const CFRAbstractionConfig *cfg)
{
    CFRAbstractionConfig *tmp;

    if (cfg == NULL)
    {
        return 0;
    }

    tmp = (CFRAbstractionConfig *)malloc(sizeof(*tmp));
    if (tmp == NULL)
    {
        return 0;
    }
    *tmp = *cfg;
    if (!cfr_abstraction_validate(tmp))
    {
        free(tmp);
        return 0;
    }
    tmp->hash32 = cfr_abstraction_hash32(tmp);

    g_cfr_abstraction_cfg = *tmp;
    free(tmp);
    g_cfr_abstraction_cfg_ready = 1;
    cfr_abs_feature_cache_clear();
    return 1;
}

static int cfr_abstraction_ensure_default_loaded(void)
{
    if (g_cfr_abstraction_cfg_ready)
    {
        return 1;
    }
    cfr_abstraction_set_defaults(&g_cfr_abstraction_cfg);
    g_cfr_abstraction_cfg_ready = 1;
    cfr_abs_feature_cache_clear();
    return 1;
}

static uint32_t cfr_abstraction_effective_hash32(void)
{
    if (!cfr_abstraction_ensure_default_loaded())
    {
        return 0u;
    }
    return g_cfr_abstraction_cfg.hash32;
}

static int cfr_abstraction_save(const CFRAbstractionConfig *cfg, const char *path)
{
    FILE *f;
    CFRAbstractionConfig *tmp;

    if (cfg == NULL || path == NULL)
    {
        return 0;
    }

    tmp = (CFRAbstractionConfig *)malloc(sizeof(*tmp));
    if (tmp == NULL)
    {
        return 0;
    }
    *tmp = *cfg;
    if (!cfr_abstraction_validate(tmp))
    {
        free(tmp);
        return 0;
    }
    tmp->hash32 = cfr_abstraction_hash32(tmp);

    f = fopen(path, "wb");
    if (f == NULL)
    {
        free(tmp);
        return 0;
    }

    if (fwrite(tmp, sizeof(*tmp), 1, f) != 1)
    {
        fclose(f);
        free(tmp);
        return 0;
    }

    fclose(f);
    free(tmp);
    return 1;
}

static int cfr_abstraction_load(CFRAbstractionConfig *cfg, const char *path)
{
    FILE *f;
    CFRAbstractionConfig *tmp;
    uint32_t file_hash;
    uint32_t expected_hash;

    if (cfg == NULL || path == NULL)
    {
        return 0;
    }
    tmp = (CFRAbstractionConfig *)malloc(sizeof(*tmp));
    if (tmp == NULL)
    {
        return 0;
    }

    f = fopen(path, "rb");
    if (f == NULL)
    {
        free(tmp);
        return 0;
    }

    if (fread(tmp, sizeof(*tmp), 1, f) != 1)
    {
        fclose(f);
        free(tmp);
        return 0;
    }
    fclose(f);

    file_hash = tmp->hash32;

    if (!cfr_abstraction_validate(tmp))
    {
        free(tmp);
        return 0;
    }

    expected_hash = cfr_abstraction_hash32(tmp);
    if (file_hash != expected_hash)
    {
        free(tmp);
        return 0;
    }
    tmp->hash32 = expected_hash;

    *cfg = *tmp;
    free(tmp);
    return 1;
}

static uint32_t cfr_abstraction_bucket_count_for_street(int street, int mode)
{
    if (!cfr_abstraction_ensure_default_loaded())
    {
        return 0u;
    }
    if (street < 0 || street > 3)
    {
        return 0u;
    }
    if (mode == CFR_ABS_MODE_SEARCH)
    {
        return g_cfr_abstraction_cfg.street_bucket_count_search[street];
    }
    return g_cfr_abstraction_cfg.street_bucket_count_blueprint[street];
}

static int cfr_abs_collect_available_cards(int hole0,
                                           int hole1,
                                           const int *board,
                                           int board_count,
                                           int *out_cards,
                                           int out_max)
{
    int used[CFR_DECK_SIZE];
    int i;
    int n;

    if (out_cards == NULL || out_max <= 0)
    {
        return 0;
    }

    memset(used, 0, sizeof(used));
    if (hole0 < 0 || hole0 >= CFR_DECK_SIZE || hole1 < 0 || hole1 >= CFR_DECK_SIZE || hole0 == hole1)
    {
        return 0;
    }
    used[hole0] = 1;
    used[hole1] = 1;

    for (i = 0; i < board_count; ++i)
    {
        int c;
        c = board[i];
        if (c < 0 || c >= CFR_DECK_SIZE || used[c])
        {
            return 0;
        }
        used[c] = 1;
    }

    n = 0;
    for (i = 0; i < CFR_DECK_SIZE && n < out_max; ++i)
    {
        if (!used[i])
        {
            out_cards[n++] = i;
        }
    }
    return n;
}

static int cfr_abs_compare_score(uint32_t hero, uint32_t opp)
{
    if (hero < opp)
    {
        return 1;
    }
    if (hero > opp)
    {
        return -1;
    }
    return 0;
}

static uint32_t cfr_abs_eval_showdown7(int h0, int h1, const int board5[5])
{
    int cards7[7];
    cards7[0] = h0;
    cards7[1] = h1;
    cards7[2] = board5[0];
    cards7[3] = board5[1];
    cards7[4] = board5[2];
    cards7[5] = board5[3];
    cards7[6] = board5[4];
    return cfr_eval_best_hand(cards7);
}

static int cfr_abstraction_compute_features_mc(int street,
                                               int hole0,
                                               int hole1,
                                               const int *board,
                                               int board_count,
                                               uint32_t mc_samples,
                                               uint64_t seed,
                                               float out_feat[CFR_ABS_FEATURE_DIM],
                                               float out_hist[CFR_ABS_EMD_BINS])
{
    int known_board[CFR_MAX_BOARD];
    int base_available[CFR_DECK_SIZE];
    int base_n;
    int missing_board;
    uint64_t rng;
    uint32_t s;
    double hs_sum;
    double hs2_sum;
    double pp_sum;
    double np_sum;

    if (out_feat == NULL || street < 1 || street > 3)
    {
        return 0;
    }

    if (board_count < 0 || board_count > 5)
    {
        return 0;
    }

    missing_board = 5 - board_count;
    if (missing_board < 0 || missing_board > 2)
    {
        return 0;
    }

    memcpy(known_board, board, (size_t)board_count * sizeof(int));
    base_n = cfr_abs_collect_available_cards(hole0, hole1, board, board_count, base_available, CFR_DECK_SIZE);
    if (base_n <= 0)
    {
        return 0;
    }
    if ((2 + (missing_board * 2)) > base_n)
    {
        return 0;
    }

    if (mc_samples == 0u)
    {
        mc_samples = 1u;
    }

    rng = seed;
    hs_sum = 0.0;
    hs2_sum = 0.0;
    pp_sum = 0.0;
    np_sum = 0.0;
    if (out_hist != NULL)
    {
        memset(out_hist, 0, (size_t)CFR_ABS_EMD_BINS * sizeof(*out_hist));
    }

    for (s = 0; s < mc_samples; ++s)
    {
        int avail[CFR_DECK_SIZE];
        int i;
        int opp0;
        int opp1;
        int board_now[5];
        int board_fin[5];
        int cmp_now;
        int cmp_fin;
        double hs;
        uint32_t hero_now_rank;
        uint32_t opp_now_rank;
        uint32_t hero_fin_rank;
        uint32_t opp_fin_rank;

        memcpy(avail, base_available, (size_t)base_n * sizeof(int));
        for (i = base_n - 1; i > 0; --i)
        {
            int j;
            int t;
            j = cfr_rng_int(&rng, i + 1);
            t = avail[i];
            avail[i] = avail[j];
            avail[j] = t;
        }

        opp0 = avail[0];
        opp1 = avail[1];

        for (i = 0; i < board_count; ++i)
        {
            board_now[i] = known_board[i];
            board_fin[i] = known_board[i];
        }
        for (i = 0; i < missing_board; ++i)
        {
            board_now[board_count + i] = avail[2 + i];
            board_fin[board_count + i] = avail[2 + missing_board + i];
        }

        hero_now_rank = cfr_abs_eval_showdown7(hole0, hole1, board_now);
        opp_now_rank = cfr_abs_eval_showdown7(opp0, opp1, board_now);
        hero_fin_rank = cfr_abs_eval_showdown7(hole0, hole1, board_fin);
        opp_fin_rank = cfr_abs_eval_showdown7(opp0, opp1, board_fin);

        cmp_now = cfr_abs_compare_score(hero_now_rank, opp_now_rank);
        cmp_fin = cfr_abs_compare_score(hero_fin_rank, opp_fin_rank);

        if (cmp_fin > 0)
        {
            hs = 1.0;
        }
        else if (cmp_fin == 0)
        {
            hs = 0.5;
        }
        else
        {
            hs = 0.0;
        }
        hs_sum += hs;
        hs2_sum += hs * hs;

        if (out_hist != NULL)
        {
            int bin;
            double pct;
            double improve;
            double score;
            /* PHE raw rank: lower is stronger. */
            pct = (7462.0 - (double)hero_fin_rank) / 7462.0;
            if (pct < 0.0)
            {
                pct = 0.0;
            }
            if (pct > 1.0)
            {
                pct = 1.0;
            }
            improve = ((double)cmp_fin - (double)cmp_now) * 0.5;
            if (improve < -1.0)
            {
                improve = -1.0;
            }
            if (improve > 1.0)
            {
                improve = 1.0;
            }
            score = 0.75 * pct + 0.20 * hs + 0.05 * (0.5 * (improve + 1.0));
            if (score < 0.0)
            {
                score = 0.0;
            }
            if (score > 1.0)
            {
                score = 1.0;
            }
            bin = (int)(score * (double)CFR_ABS_EMD_BINS);
            if (bin >= CFR_ABS_EMD_BINS)
            {
                bin = CFR_ABS_EMD_BINS - 1;
            }
            if (bin < 0)
            {
                bin = 0;
            }
            out_hist[bin] += 1.0f;
        }

        if (cmp_now < 0)
        {
            if (cmp_fin > 0)
            {
                pp_sum += 1.0;
            }
            else if (cmp_fin == 0)
            {
                pp_sum += 0.5;
            }
        }
        else if (cmp_now == 0 && cmp_fin > 0)
        {
            pp_sum += 0.5;
        }

        if (cmp_now > 0)
        {
            if (cmp_fin < 0)
            {
                np_sum += 1.0;
            }
            else if (cmp_fin == 0)
            {
                np_sum += 0.5;
            }
        }
        else if (cmp_now == 0 && cmp_fin < 0)
        {
            np_sum += 0.5;
        }
    }

    out_feat[0] = (float)(hs_sum / (double)mc_samples);
    out_feat[1] = (float)(hs2_sum / (double)mc_samples);
    out_feat[2] = (float)((pp_sum + np_sum) / (double)mc_samples);
    out_feat[3] = (float)(pp_sum / (double)mc_samples);
    if (out_hist != NULL)
    {
        int b;
        float inv_n;
        inv_n = 1.0f / (float)mc_samples;
        for (b = 0; b < CFR_ABS_EMD_BINS; ++b)
        {
            out_hist[b] *= inv_n;
        }
    }
    return 1;
}

static int cfr_abstraction_profile_for_state(int street,
                                             uint64_t canonical_index,
                                             int hole0,
                                             int hole1,
                                             const int *board,
                                             int board_count,
                                             float out_feat[CFR_ABS_FEATURE_DIM],
                                             float out_hist[CFR_ABS_EMD_BINS])
{
    uint64_t key;
    uint64_t seed;
    float tmp_hist[CFR_ABS_EMD_BINS];

    if (out_feat == NULL || street < 1 || street > 3)
    {
        return 0;
    }
    if (!cfr_abstraction_ensure_default_loaded())
    {
        return 0;
    }

    key = cfr_abs_feature_key(street, canonical_index);
    if (cfr_abs_feature_cache_lookup(key, out_feat, (out_hist != NULL) ? out_hist : tmp_hist))
    {
        return 1;
    }

    seed = g_cfr_abstraction_cfg.seed ^ (canonical_index * 0x9E3779B97F4A7C15ULL) ^ ((uint64_t)street << 48);
    if (!cfr_abstraction_compute_features_mc(street,
                                             hole0,
                                             hole1,
                                             board,
                                             board_count,
                                             g_cfr_abstraction_cfg.feature_mc_samples,
                                             seed,
                                             out_feat,
                                             (out_hist != NULL) ? out_hist : tmp_hist))
    {
        return 0;
    }
    cfr_abs_feature_cache_store(key, out_feat, (out_hist != NULL) ? out_hist : tmp_hist);
    return 1;
}

static uint64_t cfr_abstraction_bucket_legacy_hash(int street, uint64_t canonical_index, int mode)
{
    uint32_t bucket_count;
    uint64_t mixed;

    bucket_count = cfr_abstraction_bucket_count_for_street(street, mode);
    if (bucket_count == 0u)
    {
        return canonical_index;
    }

    if (street == 0)
    {
        return canonical_index % 169ULL;
    }

    mixed = canonical_index;
    mixed ^= g_cfr_abstraction_cfg.seed;
    mixed ^= (uint64_t)(street + 1) * 0xD1B54A32D192ED03ULL;
    mixed ^= (uint64_t)(mode + 1) * 0x9E3779B97F4A7C15ULL;
    mixed = cfr_abs_splitmix64(mixed);
    return mixed % (uint64_t)bucket_count;
}

static uint64_t cfr_abstraction_bucket_from_features(int street,
                                                     int mode,
                                                     uint64_t canonical_index,
                                                     const float feat[CFR_ABS_FEATURE_DIM])
{
    uint32_t bucket_n;
    uint32_t k;
    uint32_t best_k;
    double best_dist;

    if (street == 0)
    {
        return canonical_index % 169ULL;
    }

    if (!g_cfr_abstraction_cfg.centroid_ready[mode][street])
    {
        return cfr_abstraction_bucket_legacy_hash(street, canonical_index, mode);
    }

    bucket_n = (mode == CFR_ABS_MODE_SEARCH)
                   ? g_cfr_abstraction_cfg.street_bucket_count_search[street]
                   : g_cfr_abstraction_cfg.street_bucket_count_blueprint[street];
    if (bucket_n == 0u)
    {
        return cfr_abstraction_bucket_legacy_hash(street, canonical_index, mode);
    }
    if (bucket_n > CFR_ABS_MAX_BUCKETS)
    {
        bucket_n = CFR_ABS_MAX_BUCKETS;
    }

    best_k = 0u;
    best_dist = 1e100;
    for (k = 0u; k < bucket_n; ++k)
    {
        double dist;
        dist = cfr_abs_weighted_dist2_vec(feat, g_cfr_abstraction_cfg.centroids[mode][street][k]);
        if (dist < best_dist)
        {
            best_dist = dist;
            best_k = k;
        }
    }
    return (uint64_t)best_k;
}

static uint64_t cfr_abstraction_bucket_from_profile_emd(int street,
                                                        int mode,
                                                        uint64_t canonical_index,
                                                        const float feat[CFR_ABS_FEATURE_DIM],
                                                        const float hist[CFR_ABS_EMD_BINS])
{
    uint32_t bucket_n;
    uint32_t k;
    uint32_t best_k;
    double best_dist;

    if (street == 0)
    {
        return canonical_index % 169ULL;
    }
    if (!g_cfr_abstraction_cfg.emd_ready[mode][street])
    {
        return cfr_abstraction_bucket_from_features(street, mode, canonical_index, feat);
    }

    bucket_n = (mode == CFR_ABS_MODE_SEARCH)
                   ? g_cfr_abstraction_cfg.street_bucket_count_search[street]
                   : g_cfr_abstraction_cfg.street_bucket_count_blueprint[street];
    if (bucket_n == 0u)
    {
        return cfr_abstraction_bucket_from_features(street, mode, canonical_index, feat);
    }
    if (bucket_n > CFR_ABS_MAX_BUCKETS)
    {
        bucket_n = CFR_ABS_MAX_BUCKETS;
    }

    best_k = 0u;
    best_dist = 1e100;
    for (k = 0u; k < bucket_n; ++k)
    {
        double dist;
        dist = cfr_abs_profile_dist(feat,
                                    hist,
                                    g_cfr_abstraction_cfg.centroids[mode][street][k],
                                    g_cfr_abstraction_cfg.emd_medoids[mode][street][k],
                                    (int)g_cfr_abstraction_cfg.emd_bins);
        if (dist < best_dist)
        {
            best_dist = dist;
            best_k = k;
        }
    }
    return (uint64_t)best_k;
}

static uint64_t cfr_abstraction_bucket_for_state(int street,
                                                 int hole0,
                                                 int hole1,
                                                 const int *board,
                                                 int board_count,
                                                 uint64_t canonical_index,
                                                 int mode)
{
    float feat[CFR_ABS_FEATURE_DIM];
    float hist[CFR_ABS_EMD_BINS];

    if (!cfr_abstraction_ensure_default_loaded())
    {
        return canonical_index;
    }
    if (street < 0 || street > 3)
    {
        return canonical_index;
    }
    if (street == 0)
    {
        return canonical_index % 169ULL;
    }

    if (!g_cfr_abstraction_cfg.centroid_ready[mode][street])
    {
        return cfr_abstraction_bucket_legacy_hash(street, canonical_index, mode);
    }

    if (cfr_abstraction_profile_for_state(street,
                                          canonical_index,
                                          hole0,
                                          hole1,
                                          board,
                                          board_count,
                                          feat,
                                          hist))
    {
        if (g_cfr_abstraction_cfg.clustering_algo == CFR_ABS_CLUSTER_ALGO_EMD_KMEDOIDS &&
            g_cfr_abstraction_cfg.emd_ready[mode][street] &&
            g_cfr_abstraction_cfg.emd_bins > 1u)
        {
            return cfr_abstraction_bucket_from_profile_emd(street, mode, canonical_index, feat, hist);
        }
        return cfr_abstraction_bucket_from_features(street, mode, canonical_index, feat);
    }
    return cfr_abstraction_bucket_legacy_hash(street, canonical_index, mode);
}

static uint64_t cfr_abstraction_bucket_for_hand(int street, uint64_t canonical_index, int mode)
{
    if (!cfr_abstraction_ensure_default_loaded())
    {
        return canonical_index;
    }
    if (street < 0 || street > 3)
    {
        return canonical_index;
    }
    return cfr_abstraction_bucket_legacy_hash(street, canonical_index, mode);
}

static void cfr_abs_kmeans_init_kmeanspp(const float *samples,
                                         uint32_t sample_n,
                                         float *centroids,
                                         uint32_t k,
                                         uint64_t *rng)
{
    uint32_t i;
    double *min_dist;
    uint32_t first_idx;

    if (samples == NULL || centroids == NULL || rng == NULL || sample_n == 0u || k == 0u)
    {
        return;
    }

    min_dist = (double *)malloc((size_t)sample_n * sizeof(*min_dist));
    if (min_dist == NULL)
    {
        /* Fall back to deterministic random picks if memory is tight. */
        for (i = 0; i < k; ++i)
        {
            uint32_t sidx;
            uint32_t d;
            sidx = (uint32_t)cfr_rng_int(rng, (int)sample_n);
            for (d = 0; d < CFR_ABS_FEATURE_DIM; ++d)
            {
                centroids[i * CFR_ABS_FEATURE_DIM + d] = samples[sidx * CFR_ABS_FEATURE_DIM + d];
            }
        }
        return;
    }

    first_idx = (uint32_t)cfr_rng_int(rng, (int)sample_n);
    for (i = 0; i < CFR_ABS_FEATURE_DIM; ++i)
    {
        centroids[i] = samples[first_idx * CFR_ABS_FEATURE_DIM + i];
    }

    for (i = 0; i < sample_n; ++i)
    {
        min_dist[i] = cfr_abs_weighted_dist2_vec(&samples[i * CFR_ABS_FEATURE_DIM], &centroids[0]);
    }

    for (i = 1; i < k; ++i)
    {
        uint32_t s;
        double total;
        double pick;
        double acc;
        uint32_t chosen;

        total = 0.0;
        for (s = 0; s < sample_n; ++s)
        {
            total += min_dist[s];
        }
        if (total <= 1e-20)
        {
            chosen = (uint32_t)cfr_rng_int(rng, (int)sample_n);
        }
        else
        {
            pick = cfr_rng_unit(rng) * total;
            acc = 0.0;
            chosen = sample_n - 1u;
            for (s = 0; s < sample_n; ++s)
            {
                acc += min_dist[s];
                if (pick <= acc)
                {
                    chosen = s;
                    break;
                }
            }
        }

        for (s = 0; s < CFR_ABS_FEATURE_DIM; ++s)
        {
            centroids[i * CFR_ABS_FEATURE_DIM + s] = samples[chosen * CFR_ABS_FEATURE_DIM + s];
        }

        for (s = 0; s < sample_n; ++s)
        {
            double d2;
            d2 = cfr_abs_weighted_dist2_vec(&samples[s * CFR_ABS_FEATURE_DIM],
                                            &centroids[i * CFR_ABS_FEATURE_DIM]);
            if (d2 < min_dist[s])
            {
                min_dist[s] = d2;
            }
        }
    }

    free(min_dist);
}

static void cfr_abs_kmeans_fit(const float *samples,
                               uint32_t sample_n,
                               float *centroids,
                               uint32_t k,
                               uint32_t iters,
                               uint64_t *rng)
{
    uint32_t *assign;
    double *sums;
    uint32_t *counts;
    uint32_t it;

    assign = (uint32_t *)calloc((size_t)sample_n, sizeof(*assign));
    sums = (double *)calloc((size_t)k * CFR_ABS_FEATURE_DIM, sizeof(*sums));
    counts = (uint32_t *)calloc((size_t)k, sizeof(*counts));
    if (assign == NULL || sums == NULL || counts == NULL)
    {
        free(assign);
        free(sums);
        free(counts);
        return;
    }

    for (it = 0; it < iters; ++it)
    {
        uint32_t i;

        memset(sums, 0, (size_t)k * CFR_ABS_FEATURE_DIM * sizeof(*sums));
        memset(counts, 0, (size_t)k * sizeof(*counts));

        for (i = 0; i < sample_n; ++i)
        {
            uint32_t c;
            uint32_t best_c;
            double best_d;

            best_c = 0u;
            best_d = 1e100;
            for (c = 0; c < k; ++c)
            {
                double dist;
                dist = cfr_abs_weighted_dist2_vec(&samples[i * CFR_ABS_FEATURE_DIM],
                                                  &centroids[c * CFR_ABS_FEATURE_DIM]);
                if (dist < best_d)
                {
                    best_d = dist;
                    best_c = c;
                }
            }
            assign[i] = best_c;
            counts[best_c]++;
            for (c = 0; c < (uint32_t)CFR_ABS_FEATURE_DIM; ++c)
            {
                sums[best_c * CFR_ABS_FEATURE_DIM + c] += (double)samples[i * CFR_ABS_FEATURE_DIM + c];
            }
        }

        for (i = 0; i < k; ++i)
        {
            uint32_t d;
            if (counts[i] == 0u)
            {
                uint32_t sidx;
                sidx = (uint32_t)cfr_rng_int(rng, (int)sample_n);
                for (d = 0; d < (uint32_t)CFR_ABS_FEATURE_DIM; ++d)
                {
                    centroids[i * CFR_ABS_FEATURE_DIM + d] = samples[sidx * CFR_ABS_FEATURE_DIM + d];
                }
                continue;
            }
            for (d = 0; d < (uint32_t)CFR_ABS_FEATURE_DIM; ++d)
            {
                centroids[i * CFR_ABS_FEATURE_DIM + d] = (float)(sums[i * CFR_ABS_FEATURE_DIM + d] / (double)counts[i]);
            }
        }
    }

    free(assign);
    free(sums);
    free(counts);
}

static double cfr_abs_profile_dist_samples(const float *sample_feat,
                                           const float *sample_hist,
                                           uint32_t ia,
                                           uint32_t ib)
{
    return cfr_abs_profile_dist(&sample_feat[(size_t)ia * CFR_ABS_FEATURE_DIM],
                                &sample_hist[(size_t)ia * CFR_ABS_EMD_BINS],
                                &sample_feat[(size_t)ib * CFR_ABS_FEATURE_DIM],
                                &sample_hist[(size_t)ib * CFR_ABS_EMD_BINS],
                                CFR_ABS_EMD_BINS);
}

static void cfr_abs_kmedoids_init_kpp(const float *sample_feat,
                                      const float *sample_hist,
                                      uint32_t sample_n,
                                      uint32_t k,
                                      uint32_t *medoids,
                                      uint64_t *rng)
{
    double *min_dist;
    uint32_t i;
    uint32_t first_idx;

    if (sample_feat == NULL || sample_hist == NULL || medoids == NULL || rng == NULL || sample_n == 0u || k == 0u)
    {
        return;
    }

    min_dist = (double *)malloc((size_t)sample_n * sizeof(*min_dist));
    if (min_dist == NULL)
    {
        for (i = 0; i < k; ++i)
        {
            medoids[i] = (uint32_t)cfr_rng_int(rng, (int)sample_n);
        }
        return;
    }

    first_idx = (uint32_t)cfr_rng_int(rng, (int)sample_n);
    medoids[0] = first_idx;
    for (i = 0; i < sample_n; ++i)
    {
        min_dist[i] = cfr_abs_profile_dist_samples(sample_feat, sample_hist, i, first_idx);
    }

    for (i = 1; i < k; ++i)
    {
        uint32_t s;
        uint32_t chosen;
        double total;
        double pick;
        double acc;

        total = 0.0;
        for (s = 0; s < sample_n; ++s)
        {
            total += min_dist[s];
        }

        if (total <= 1e-20)
        {
            chosen = (uint32_t)cfr_rng_int(rng, (int)sample_n);
        }
        else
        {
            pick = cfr_rng_unit(rng) * total;
            acc = 0.0;
            chosen = sample_n - 1u;
            for (s = 0; s < sample_n; ++s)
            {
                acc += min_dist[s];
                if (pick <= acc)
                {
                    chosen = s;
                    break;
                }
            }
        }

        medoids[i] = chosen;
        for (s = 0; s < sample_n; ++s)
        {
            double d;
            d = cfr_abs_profile_dist_samples(sample_feat, sample_hist, s, chosen);
            if (d < min_dist[s])
            {
                min_dist[s] = d;
            }
        }
    }

    free(min_dist);
}

static int cfr_abs_kmedoids_fit(const float *sample_feat,
                                const float *sample_hist,
                                uint32_t sample_n,
                                uint32_t *medoids,
                                uint32_t k,
                                uint32_t iters,
                                uint64_t *rng,
                                double *out_mean_intra,
                                double *out_mean_sep)
{
    uint32_t *assign;
    uint32_t *counts;
    uint32_t *offsets;
    uint32_t *cursor;
    uint32_t *members;
    uint32_t it;
    uint32_t i;

    if (sample_feat == NULL || sample_hist == NULL || medoids == NULL || rng == NULL ||
        sample_n == 0u || k == 0u)
    {
        return 0;
    }

    assign = (uint32_t *)calloc((size_t)sample_n, sizeof(*assign));
    counts = (uint32_t *)calloc((size_t)k, sizeof(*counts));
    offsets = (uint32_t *)calloc((size_t)k + 1u, sizeof(*offsets));
    cursor = (uint32_t *)calloc((size_t)k, sizeof(*cursor));
    members = (uint32_t *)calloc((size_t)sample_n, sizeof(*members));
    if (assign == NULL || counts == NULL || offsets == NULL || cursor == NULL || members == NULL)
    {
        free(assign);
        free(counts);
        free(offsets);
        free(cursor);
        free(members);
        return 0;
    }

    if (iters == 0u)
    {
        iters = 1u;
    }

    for (it = 0u; it < iters; ++it)
    {
        memset(counts, 0, (size_t)k * sizeof(*counts));
        for (i = 0u; i < sample_n; ++i)
        {
            uint32_t c;
            uint32_t best_c;
            double best_d;

            best_c = 0u;
            best_d = 1e100;
            for (c = 0u; c < k; ++c)
            {
                double d;
                d = cfr_abs_profile_dist_samples(sample_feat, sample_hist, i, medoids[c]);
                if (d < best_d)
                {
                    best_d = d;
                    best_c = c;
                }
            }
            assign[i] = best_c;
            counts[best_c]++;
        }

        offsets[0] = 0u;
        for (i = 0u; i < k; ++i)
        {
            offsets[i + 1u] = offsets[i] + counts[i];
            cursor[i] = offsets[i];
        }
        for (i = 0u; i < sample_n; ++i)
        {
            uint32_t c;
            c = assign[i];
            members[cursor[c]++] = i;
        }

        for (i = 0u; i < k; ++i)
        {
            uint32_t start;
            uint32_t cnt;
            uint32_t candidate_n;
            uint32_t t;
            uint32_t best_idx;
            double best_cost;

            cnt = counts[i];
            if (cnt == 0u)
            {
                medoids[i] = (uint32_t)cfr_rng_int(rng, (int)sample_n);
                continue;
            }

            start = offsets[i];
            candidate_n = (cnt < 32u) ? cnt : 32u;
            best_idx = medoids[i];
            best_cost = 1e100;
            for (t = 0u; t < candidate_n; ++t)
            {
                uint32_t cand_pos;
                uint32_t cand_idx;
                uint32_t j;
                double cost;

                cand_pos = start + (uint32_t)(((uint64_t)t * (uint64_t)cnt) / (uint64_t)candidate_n);
                if (cand_pos >= start + cnt)
                {
                    cand_pos = start + cnt - 1u;
                }
                cand_idx = members[cand_pos];
                cost = 0.0;
                for (j = 0u; j < cnt; ++j)
                {
                    uint32_t other;
                    other = members[start + j];
                    cost += cfr_abs_profile_dist_samples(sample_feat, sample_hist, cand_idx, other);
                }
                if (cost < best_cost || (cost == best_cost && cand_idx < best_idx))
                {
                    best_cost = cost;
                    best_idx = cand_idx;
                }
            }
            medoids[i] = best_idx;
        }
    }

    /* Final assignment + quality metrics. */
    memset(counts, 0, (size_t)k * sizeof(*counts));
    if (out_mean_intra != NULL)
    {
        *out_mean_intra = 0.0;
    }
    for (i = 0u; i < sample_n; ++i)
    {
        uint32_t c;
        uint32_t best_c;
        double best_d;

        best_c = 0u;
        best_d = 1e100;
        for (c = 0u; c < k; ++c)
        {
            double d;
            d = cfr_abs_profile_dist_samples(sample_feat, sample_hist, i, medoids[c]);
            if (d < best_d)
            {
                best_d = d;
                best_c = c;
            }
        }
        assign[i] = best_c;
        counts[best_c]++;
        if (out_mean_intra != NULL)
        {
            *out_mean_intra += best_d;
        }
    }
    if (out_mean_intra != NULL && sample_n > 0u)
    {
        *out_mean_intra /= (double)sample_n;
    }

    if (out_mean_sep != NULL)
    {
        double sep_sum;
        uint32_t sep_n;
        uint32_t c;
        sep_sum = 0.0;
        sep_n = 0u;
        for (c = 0u; c < k; ++c)
        {
            uint32_t d;
            double best;
            if (counts[c] == 0u)
            {
                continue;
            }
            best = 1e100;
            for (d = 0u; d < k; ++d)
            {
                double dist;
                if (c == d || counts[d] == 0u)
                {
                    continue;
                }
                dist = cfr_abs_profile_dist_samples(sample_feat, sample_hist, medoids[c], medoids[d]);
                if (dist < best)
                {
                    best = dist;
                }
            }
            if (best < 1e99)
            {
                sep_sum += best;
                sep_n++;
            }
        }
        *out_mean_sep = (sep_n > 0u) ? (sep_sum / (double)sep_n) : 0.0;
    }

    free(assign);
    free(counts);
    free(offsets);
    free(cursor);
    free(members);
    return 1;
}

static int cfr_abstraction_build_street_centroids(CFRAbstractionConfig *cfg, int mode, int street)
{
    uint32_t bucket_n;
    uint32_t sample_n;
    float *sample_feat;
    float *sample_hist;
    float *centroids;
    uint32_t *medoids;
    uint64_t rng;
    uint32_t i;
    int board_n;

    if (cfg == NULL || street <= 0 || street > 3)
    {
        return 0;
    }

    bucket_n = (mode == CFR_ABS_MODE_SEARCH) ? cfg->street_bucket_count_search[street] : cfg->street_bucket_count_blueprint[street];
    if (bucket_n == 0u || bucket_n > CFR_ABS_MAX_BUCKETS)
    {
        return 0;
    }

    sample_n = cfg->build_samples_per_street;
    if (sample_n < bucket_n * 8u)
    {
        sample_n = bucket_n * 8u;
    }
    if (sample_n < 512u)
    {
        sample_n = 512u;
    }
    if (sample_n > 50000u)
    {
        sample_n = 50000u;
    }

    sample_feat = (float *)calloc((size_t)sample_n * CFR_ABS_FEATURE_DIM, sizeof(*sample_feat));
    sample_hist = (float *)calloc((size_t)sample_n * CFR_ABS_EMD_BINS, sizeof(*sample_hist));
    centroids = (float *)calloc((size_t)bucket_n * CFR_ABS_FEATURE_DIM, sizeof(*centroids));
    medoids = (uint32_t *)calloc((size_t)bucket_n, sizeof(*medoids));
    if (sample_feat == NULL || sample_hist == NULL || centroids == NULL || medoids == NULL)
    {
        free(sample_feat);
        free(sample_hist);
        free(centroids);
        free(medoids);
        return 0;
    }

    board_n = (street == 1) ? 3 : ((street == 2) ? 4 : 5);
    rng = cfg->seed ^ ((uint64_t)(mode + 1) * 0x9E3779B97F4A7C15ULL) ^ ((uint64_t)(street + 1) * 0xD1B54A32D192ED03ULL);

    for (i = 0; i < sample_n; ++i)
    {
        int deck[CFR_DECK_SIZE];
        int d;
        int hole0;
        int hole1;
        int board[CFR_MAX_BOARD];
        uint64_t idx;
        uint64_t fseed;

        for (d = 0; d < CFR_DECK_SIZE; ++d)
        {
            deck[d] = d;
        }
        for (d = CFR_DECK_SIZE - 1; d > 0; --d)
        {
            int j;
            int t;
            j = cfr_rng_int(&rng, d + 1);
            t = deck[d];
            deck[d] = deck[j];
            deck[j] = t;
        }

        hole0 = deck[0];
        hole1 = deck[1];
        for (d = 0; d < board_n; ++d)
        {
            board[d] = deck[2 + d];
        }

        if (!cfr_hand_index_for_state(street, hole0, hole1, board, board_n, &idx))
        {
            memset(&sample_feat[i * CFR_ABS_FEATURE_DIM], 0, CFR_ABS_FEATURE_DIM * sizeof(float));
            memset(&sample_hist[i * CFR_ABS_EMD_BINS], 0, CFR_ABS_EMD_BINS * sizeof(float));
            continue;
        }

        fseed = cfg->seed ^ idx ^ ((uint64_t)i * 0x94D049BB133111EBULL);
        if (!cfr_abstraction_compute_features_mc(street,
                                                 hole0,
                                                 hole1,
                                                 board,
                                                 board_n,
                                                 cfg->feature_mc_samples,
                                                 fseed,
                                                 &sample_feat[i * CFR_ABS_FEATURE_DIM],
                                                 &sample_hist[i * CFR_ABS_EMD_BINS]))
        {
            memset(&sample_feat[i * CFR_ABS_FEATURE_DIM], 0, CFR_ABS_FEATURE_DIM * sizeof(float));
            memset(&sample_hist[i * CFR_ABS_EMD_BINS], 0, CFR_ABS_EMD_BINS * sizeof(float));
        }
    }

    if (cfg->clustering_algo == CFR_ABS_CLUSTER_ALGO_EMD_KMEDOIDS)
    {
        double mean_intra;
        double mean_sep;

        cfr_abs_kmedoids_init_kpp(sample_feat, sample_hist, sample_n, bucket_n, medoids, &rng);
        if (!cfr_abs_kmedoids_fit(sample_feat,
                                  sample_hist,
                                  sample_n,
                                  medoids,
                                  bucket_n,
                                  cfg->kmeans_iters,
                                  &rng,
                                  &mean_intra,
                                  &mean_sep))
        {
            free(sample_feat);
            free(sample_hist);
            free(centroids);
            free(medoids);
            return 0;
        }
        cfg->emd_quality[mode][street][0] = (float)mean_intra;
        cfg->emd_quality[mode][street][1] = (float)mean_sep;
        cfg->emd_ready[mode][street] = 1u;

        for (i = 0; i < bucket_n; ++i)
        {
            uint32_t midx;
            int d;
            int b;

            midx = medoids[i];
            if (midx >= sample_n)
            {
                midx = (sample_n > 0u) ? (sample_n - 1u) : 0u;
            }
            for (d = 0; d < CFR_ABS_FEATURE_DIM; ++d)
            {
                centroids[i * CFR_ABS_FEATURE_DIM + d] = sample_feat[(size_t)midx * CFR_ABS_FEATURE_DIM + (uint32_t)d];
            }
            for (b = 0; b < CFR_ABS_EMD_BINS; ++b)
            {
                cfg->emd_medoids[mode][street][i][b] = sample_hist[(size_t)midx * CFR_ABS_EMD_BINS + (uint32_t)b];
            }
        }
    }
    else
    {
        cfr_abs_kmeans_init_kmeanspp(sample_feat, sample_n, centroids, bucket_n, &rng);
        cfr_abs_kmeans_fit(sample_feat, sample_n, centroids, bucket_n, cfg->kmeans_iters, &rng);
        cfg->emd_ready[mode][street] = 0u;
        cfg->emd_quality[mode][street][0] = 0.0f;
        cfg->emd_quality[mode][street][1] = 0.0f;
    }

    for (i = 0; i < bucket_n; ++i)
    {
        int d;
        for (d = 0; d < CFR_ABS_FEATURE_DIM; ++d)
        {
            cfg->centroids[mode][street][i][d] = centroids[i * CFR_ABS_FEATURE_DIM + d];
        }
    }
    cfg->centroid_ready[mode][street] = 1u;

    free(sample_feat);
    free(sample_hist);
    free(centroids);
    free(medoids);
    return 1;
}

static int cfr_abstraction_build_centroids(CFRAbstractionConfig *cfg)
{
    int mode;
    int street;
    double total_start_sec;

    if (cfg == NULL)
    {
        return 0;
    }

    if (!cfr_hand_index_init())
    {
        return 0;
    }

    total_start_sec = cfr_wall_seconds();
    for (mode = 0; mode < 2; ++mode)
    {
        cfg->centroid_ready[mode][0] = 0u;
        cfg->emd_ready[mode][0] = 0u;
        for (street = 1; street <= 3; ++street)
        {
            const char *mode_name;
            const char *street_name;
            double stage_start_sec;
            double stage_elapsed_sec;

            mode_name = (mode == CFR_ABS_MODE_SEARCH) ? "search" : "blueprint";
            street_name = (street == 1) ? "flop" : ((street == 2) ? "turn" : "river");
            stage_start_sec = cfr_wall_seconds();
            printf("  abstraction stage start: mode=%s street=%s\n", mode_name, street_name);
            fflush(stdout);

            cfg->centroid_ready[mode][street] = 0u;
            cfg->emd_ready[mode][street] = 0u;
            cfg->emd_quality[mode][street][0] = 0.0f;
            cfg->emd_quality[mode][street][1] = 0.0f;
            if (!cfr_abstraction_build_street_centroids(cfg, mode, street))
            {
                return 0;
            }

            stage_elapsed_sec = cfr_wall_seconds() - stage_start_sec;
            printf("  abstraction stage done: mode=%s street=%s elapsed_sec=%.2f\n",
                   mode_name,
                   street_name,
                   stage_elapsed_sec);
            fflush(stdout);
        }
    }

    cfg->hash32 = cfr_abstraction_hash32(cfg);
    printf("  abstraction centroid build done: elapsed_sec=%.2f\n", cfr_wall_seconds() - total_start_sec);
    fflush(stdout);
    return 1;
}

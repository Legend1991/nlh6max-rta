#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>
#include <ctype.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define CFR_MAX_PLAYERS 6
#define CFR_DECK_SIZE 52
#define CFR_MAX_BOARD 5
#define CFR_MAX_ACTIONS 16
#define CFR_MAX_HISTORY 128
#define CFR_HISTORY_WINDOW 48
#define CFR_INFOSET_SOFT_CAP 262144u
#define CFR_INFOSET_INIT_CAP 4096u
#define CFR_MAX_INFOSETS CFR_INFOSET_SOFT_CAP
#define CFR_HOLDEM_COMBOS 1326
#define CFR_START_STACK 200
#define CFR_SMALL_BLIND 1
#define CFR_BIG_BLIND 2
#define CFR_MAX_RAISES_PER_STREET 2
#define CFR_ABS_MAX_BUCKETS 512
#define CFR_ABS_FEATURE_DIM 4
#define CFR_ABS_EMD_BINS 24
/* Retained as a compatibility baseline in tests/docs; no longer a hard cap. */
/* Dedicated key->node index table (power-of-two for fast masking). */
#define CFR_INFOSET_HASH_INIT_CAP (1u << 13)
#define CFR_INFOSET_HASH_MAX_LOAD_NUM 7u
#define CFR_INFOSET_HASH_MAX_LOAD_DEN 10u
#define CFR_BLUEPRINT_ALLOC_GUARD 0xC0DEF00Du

#define CFR_BLUEPRINT_MAGIC 0x43465236504C414EULL
#define CFR_BLUEPRINT_VERSION 11u
#define CFR_ABSTRACTION_MAGIC 0x4346523641425354ULL
#define CFR_ABSTRACTION_VERSION 3u
#define CFR_SNAPSHOT_MAGIC 0x43465236534E4150ULL
#define CFR_SNAPSHOT_VERSION 1u
#define CFR_RUNTIME_BLUEPRINT_MAGIC 0x4346523652544D45ULL
#define CFR_RUNTIME_BLUEPRINT_VERSION 1u
#define CFR_STRATEGY_OFFSET_NONE 0xFFFFFFFFu
#define CFR_NODE_FLAG_HAS_STRATEGY 0x01u
#define CFR_NODE_USED_MASK 0x1u
#define CFR_NODE_NEW_BASE_MASK 0x2u
#define CFR_NODE_BASE_INDEX_SHIFT 2u
#define CFR_TOUCHED_WORD_BITS 32u
#define CFR_INFOSET_KEY_TAG_PREFLOP 0x1ULL
#define CFR_INFOSET_KEY_TAG_HASHED_BASE 0x8ULL

enum
{
    CFR_ACT_FOLD = 0,
    CFR_ACT_CALL_CHECK = 1,
    CFR_ACT_BET_HALF = 2,
    CFR_ACT_BET_POT = 3,
    CFR_ACT_ALL_IN = 4,
    CFR_ACT_RAISE_TO = 5
};

enum
{
    CFR_ABS_MODE_BLUEPRINT = 0,
    CFR_ABS_MODE_SEARCH = 1
};

enum
{
    CFR_PARALLEL_MODE_DETERMINISTIC = 0,
    CFR_PARALLEL_MODE_SHARDED = 1
};

enum
{
    CFR_SEARCH_PICK_SAMPLE_FINAL = 0,
    CFR_SEARCH_PICK_ARGMAX = 1
};

enum
{
    CFR_OFFTREE_MODE_INJECT = 0,
    CFR_OFFTREE_MODE_TRANSLATE = 1
};

enum
{
    CFR_RUNTIME_PROFILE_NONE = 0,
    CFR_RUNTIME_PROFILE_PLURIBUS = 1
};

enum
{
    CFR_RUNTIME_QUANT_U16 = 0,
    CFR_RUNTIME_QUANT_U8 = 1
};

enum
{
    CFR_RUNTIME_PREFETCH_NONE = 0,
    CFR_RUNTIME_PREFETCH_AUTO = 1,
    CFR_RUNTIME_PREFETCH_PREFLOP = 2
};

enum
{
    CFR_ABS_CLUSTER_ALGO_LEGACY = 0,
    CFR_ABS_CLUSTER_ALGO_EMD_KMEDOIDS = 1
};

typedef struct CFRNodeTag
{
    uint64_t key;
    int32_t *regret;
    float *strategy_sum;
    uint32_t used;
    uint32_t regret_offset;
    uint32_t strategy_offset;
    uint8_t action_count;
    uint8_t street_hint;
    uint8_t _pad0;
    uint8_t _pad1;
} CFRNode;

typedef struct
{
    uint64_t key;
    uint32_t value_plus1;
    uint32_t _pad0;
} CFRHashEntry;

#define CFR_PREFLOP_HAND_BUCKETS 169u

typedef struct
{
    uint8_t hand_index;
    uint8_t _pad0;
    uint8_t _pad1;
    uint8_t _pad2;
    uint32_t node_plus1;
} CFRPreflopHandEntry;

typedef struct
{
    uint64_t public_key;
    uint32_t entry_offset;
    uint16_t entry_count;
    uint16_t entry_capacity;
    uint8_t action_count;
    uint8_t _pad0;
    uint8_t _pad1;
    uint8_t _pad2;
} CFRPreflopBlock;

typedef struct CFRBlueprintTag
{
    CFRNode *nodes;
    uint32_t node_capacity;
    int32_t *regret_storage;
    float *strategy_storage;
    uint32_t regret_capacity;
    uint32_t regret_used;
    uint32_t strategy_capacity;
    uint32_t strategy_used;
    uint64_t *key_hash_keys;
    uint32_t *key_hash_node_plus1;
    uint32_t key_hash_cap;
    uint32_t key_hash_used_count;
    uint32_t key_hash_used_slots_cap;
    uint32_t *key_hash_used_slots;
    CFRPreflopBlock *preflop_blocks;
    uint32_t preflop_block_capacity;
    uint32_t preflop_block_count;
    CFRPreflopHandEntry *preflop_entry_storage;
    uint32_t preflop_entry_capacity;
    uint32_t preflop_entry_used;
    CFRHashEntry *preflop_hash_entries;
    uint32_t preflop_hash_cap;
    uint32_t preflop_hash_used_count;
    uint32_t preflop_hash_used_slots_cap;
    uint32_t *preflop_hash_used_slots;
    uint32_t touched_count;
    uint32_t touched_capacity;
    uint32_t *touched_indices;
    uint32_t *touched_mark;
    float *node_discount_scale;
    uint32_t used_node_count;
    double lazy_discount_scale;
    uint32_t alloc_guard;
    const struct CFRBlueprintTag *overlay_base;
    uint32_t compat_hash32;
    uint32_t abstraction_hash32;
    uint64_t elapsed_train_seconds;
    uint32_t phase_flags;
    uint64_t next_discount_second;
    uint64_t next_snapshot_second;
    uint64_t discount_events_applied;
    uint64_t iteration;
    uint64_t total_hands;
    uint64_t rng_state;
    time_t started_at;
    int omit_postflop_strategy_sum;
} CFRBlueprint;

typedef struct
{
    uint64_t magic;
    uint32_t version;
    uint32_t max_actions;
    uint64_t iteration;
    uint64_t total_hands;
    uint64_t rng_state;
    uint32_t node_count;
    uint32_t compat_hash32;
    uint32_t abstraction_hash32;
    uint64_t elapsed_train_seconds;
    uint32_t phase_flags;
    uint64_t next_discount_second;
    uint64_t next_snapshot_second;
    uint64_t discount_events_applied;
    uint32_t storage_flags;
    uint32_t reserved0;
    uint64_t regret_actions_total;
    uint64_t strategy_actions_total;
} CFRBlueprintFileHeader;

typedef struct
{
    uint64_t key;
    uint32_t action_count;
    uint8_t street_hint;
    uint8_t payload_flags;
    uint8_t _pad1;
    uint8_t _pad2;
} CFRBlueprintDiskNodeHeader;

typedef struct
{
    uint64_t magic;
    uint32_t version;
    uint64_t iteration;
    uint64_t elapsed_train_seconds;
    uint32_t node_count;
    uint32_t compat_hash32;
    uint32_t abstraction_hash32;
} CFRSnapshotFileHeader;

typedef struct
{
    uint64_t key;
    uint32_t action_count;
    uint8_t street_hint;
    uint8_t _pad0;
    uint8_t _pad1;
    uint8_t _pad2;
} CFRSnapshotDiskNodeHeader;

typedef struct
{
    uint64_t magic;
    uint32_t version;
    uint32_t street_bucket_count_blueprint[4];
    uint32_t street_bucket_count_search[4];
    uint64_t seed;
    uint32_t feature_mc_samples;
    uint32_t kmeans_iters;
    uint32_t build_samples_per_street;
    uint32_t clustering_algo;
    uint32_t emd_bins;
    uint32_t centroid_ready[2][4];
    float centroids[2][4][CFR_ABS_MAX_BUCKETS][CFR_ABS_FEATURE_DIM];
    uint32_t emd_ready[2][4];
    float emd_medoids[2][4][CFR_ABS_MAX_BUCKETS][CFR_ABS_EMD_BINS];
    float emd_quality[2][4][2];
    uint32_t hash32;
} CFRAbstractionConfig;

typedef struct
{
    uint64_t magic;
    uint32_t version;
    uint32_t quant_mode;
    uint32_t node_count;
    uint32_t total_shards;
    uint32_t buckets_per_street;
    uint32_t compat_hash32;
    uint32_t abstraction_hash32;
    uint32_t content_hash32;
    uint32_t reserved0;
    uint64_t shard_table_offset;
    uint64_t index_region_offset;
    uint64_t data_region_offset;
} CFRRuntimeBlueprintFileHeader;

typedef struct
{
    uint64_t index_offset;
    uint32_t index_count;
    uint8_t street_hint;
    uint8_t bucket_hint;
    uint16_t reserved0;
} CFRRuntimeShardHeader;

typedef struct
{
    uint64_t key;
    uint64_t node_offset;
} CFRRuntimeIndexEntry;

typedef struct
{
    uint32_t action_count;
    uint8_t street_hint;
    uint8_t quant_mode;
    uint16_t reserved0;
} CFRRuntimeNodeHeader;

typedef struct
{
    uint64_t key;
    uint32_t action_count;
    uint8_t street_hint;
    uint8_t valid;
    uint16_t reserved0;
    float policy[CFR_MAX_ACTIONS];
} CFRRuntimePolicyCacheEntry;

typedef struct
{
#ifdef _WIN32
    HANDLE file_handle;
    HANDLE mapping_handle;
#endif
    const unsigned char *mapped;
    uint64_t mapped_size;
    CFRRuntimeBlueprintFileHeader header;
    const CFRRuntimeShardHeader *shards;
    uint64_t shard_table_offset;
    uint64_t index_region_offset;
    uint64_t data_region_offset;
    uint32_t cache_entry_count;
    uint64_t cache_budget_bytes;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t decode_loads;
    uint64_t prefetch_loads;
    uint64_t prefetch_bytes;
    CFRRuntimePolicyCacheEntry *cache_entries;
} CFRRuntimeBlueprint;

typedef struct
{
    int kind;
    const CFRBlueprint *blueprint;
    CFRRuntimeBlueprint *runtime_bp;
    int use_runtime_cache;
} CFRPolicyProvider;

typedef struct
{
    int street;
    int position;
    uint64_t hand_index;
    int pot_bucket;
    int to_call_bucket;
    int active_players;
    uint32_t history_hash;
} CFRInfoKeyFields;

typedef struct
{
    int dealer;
    int street;
    int pot;
    int to_call;
    int current_player;
    int board_count;
    int board[CFR_MAX_BOARD];
    int hole[CFR_MAX_PLAYERS][2];
    int stack[CFR_MAX_PLAYERS];
    int in_hand[CFR_MAX_PLAYERS];
    int committed_street[CFR_MAX_PLAYERS];
    int contributed_total[CFR_MAX_PLAYERS];
    int needs_action[CFR_MAX_PLAYERS];
    int acted_this_street[CFR_MAX_PLAYERS];
    int acted_on_full_raise_seq[CFR_MAX_PLAYERS];
    int num_raises_street;
    int last_full_raise;
    int full_raise_seq;
    int is_terminal;
    int terminal_resolved;
    int deck[CFR_DECK_SIZE];
    int deck_pos;
    int history_len;
    unsigned char history[CFR_MAX_HISTORY];
} CFRHandState;

typedef struct
{
    uint64_t iterations;
    int seconds_limit;
    uint64_t dump_every_iters;
    int dump_every_seconds;
    uint64_t status_every_iters;
    int threads;
    int min_threads;
    int parallel_mode;
    uint64_t chunk_iters;
    int samples_per_player;
    uint64_t strategy_interval;
    int enable_linear_discount;
    uint64_t linear_discount_every_iters;
    uint64_t linear_discount_stop_iter;
    double linear_discount_scale;
    int enable_pruning;
    uint64_t prune_start_iter;
    uint64_t prune_full_every_iters;
    double prune_threshold;
    double prune_prob;
    int use_int_regret;
    int32_t regret_floor;
    uint64_t snapshot_every_iters;
    int snapshot_every_seconds;
    const char *snapshot_dir;
    int strict_time_phases;
    uint64_t discount_stop_seconds;
    uint64_t prune_start_seconds;
    uint64_t discount_every_seconds;
    uint64_t warmup_seconds;
    uint64_t snapshot_start_seconds;
    uint64_t avg_start_seconds;
    int enable_preflop_avg;
    int preflop_avg_sampled;
    int enable_async_checkpoint;
    int resume_ignore_compat;
    uint32_t abstraction_hash32;
    const char *abstraction_path;
    const char *out_path;
    const char *resume_path;
    uint64_t seed;
    int seed_set;
} CFRTrainOptions;

typedef struct
{
    const char *blueprint_path;
    char hole_text[32];
    char board_text[64];
    char history_text[256];
    int player_seat;
    int dealer_seat;
    int active_players;
    int pot;
    int to_call;
    int stack;
    int street;
    int raises_this_street;
    const char *abstraction_path;
    int ignore_abstraction_compat;
} CFRQueryOptions;

typedef struct
{
    const char *blueprint_path;
    const char *runtime_blueprint_path;
    const char *abstraction_path;
    int ignore_abstraction_compat;
    char hole_text[32];
    char board_text[64];
    char history_text[256];
    int player_seat;
    int dealer_seat;
    int active_players;
    int pot;
    int to_call;
    int stack;
    int street;
    int raises_this_street;
    uint64_t iters;
    uint64_t time_ms;
    int depth;
    int threads;
    int search_pick_mode;
    int offtree_mode;
    uint64_t cache_bytes;
    int runtime_prefetch_mode;
    uint64_t seed;
} CFRSearchOptions;

typedef struct
{
    const char *a_path;
    const char *b_path;
    const char *abstraction_path;
    int ignore_abstraction_compat;
    uint64_t hands;
    int use_search;
    uint64_t search_iters;
    uint64_t search_time_ms;
    int search_depth;
    int search_threads;
    int search_pick_mode;
    int offtree_mode;
    int runtime_profile;
    uint64_t seed;
} CFRMatchOptions;

typedef struct
{
    const char *raw_path;
    const char *out_path;
    const char *runtime_out_path;
    const char *snapshot_dir;
    const char *abstraction_path;
    int ignore_abstraction_compat;
    int write_full_output;
    uint64_t snapshot_min_seconds;
    int runtime_quant_mode;
    uint32_t runtime_shards;
} CFRFinalizeOptions;

/* Implemented in cfr_phe_bridge.c */
static uint32_t cfr_eval_best_hand(const int cards[7]);

static volatile sig_atomic_t g_cfr_stop_requested = 0;

static void cfr_on_signal(int sig)
{
    (void)sig;
    g_cfr_stop_requested = 1;
}

static int cfr_max_int(int a, int b)
{
    return (a > b) ? a : b;
}

static int cfr_node_is_used(const CFRNode *node)
{
    if (node == NULL)
    {
        return 0;
    }
    return (node->used & CFR_NODE_USED_MASK) != 0u;
}

static int cfr_node_has_new_base_flag(const CFRNode *node)
{
    if (node == NULL)
    {
        return 0;
    }
    return (node->used & CFR_NODE_NEW_BASE_MASK) != 0u;
}

static uint32_t cfr_node_overlay_base_index_plus1(const CFRNode *node)
{
    if (node == NULL)
    {
        return 0u;
    }
    return node->used >> CFR_NODE_BASE_INDEX_SHIFT;
}

static void cfr_node_set_overlay_meta(CFRNode *node, uint32_t base_index_plus1, int new_base)
{
    uint32_t meta;

    if (node == NULL)
    {
        return;
    }

    if (base_index_plus1 >= (1u << (32u - CFR_NODE_BASE_INDEX_SHIFT)))
    {
        base_index_plus1 = 0u;
    }

    meta = CFR_NODE_USED_MASK | (base_index_plus1 << CFR_NODE_BASE_INDEX_SHIFT);
    if (new_base)
    {
        meta |= CFR_NODE_NEW_BASE_MASK;
    }
    node->used = meta;
}

static void cfr_node_set_new_base_flag(CFRNode *node, int new_base)
{
    if (node == NULL)
    {
        return;
    }
    if (new_base)
    {
        node->used |= CFR_NODE_NEW_BASE_MASK;
    }
    else
    {
        node->used &= ~CFR_NODE_NEW_BASE_MASK;
    }
}

static uint32_t cfr_touched_mark_word_count(uint32_t node_capacity)
{
    return (node_capacity + (CFR_TOUCHED_WORD_BITS - 1u)) / CFR_TOUCHED_WORD_BITS;
}

static int cfr_touched_mark_test(const CFRBlueprint *bp, uint32_t idx)
{
    uint32_t word_index;
    uint32_t mask;

    if (bp == NULL || bp->touched_mark == NULL || idx >= bp->node_capacity)
    {
        return 0;
    }
    word_index = idx / CFR_TOUCHED_WORD_BITS;
    mask = 1u << (idx % CFR_TOUCHED_WORD_BITS);
    return (bp->touched_mark[word_index] & mask) != 0u;
}

static void cfr_touched_mark_set(CFRBlueprint *bp, uint32_t idx)
{
    uint32_t word_index;
    uint32_t mask;

    if (bp == NULL || bp->touched_mark == NULL || idx >= bp->node_capacity)
    {
        return;
    }
    word_index = idx / CFR_TOUCHED_WORD_BITS;
    mask = 1u << (idx % CFR_TOUCHED_WORD_BITS);
    bp->touched_mark[word_index] |= mask;
}

static void cfr_touched_mark_clear(CFRBlueprint *bp, uint32_t idx)
{
    uint32_t word_index;
    uint32_t mask;

    if (bp == NULL || bp->touched_mark == NULL || idx >= bp->node_capacity)
    {
        return;
    }
    word_index = idx / CFR_TOUCHED_WORD_BITS;
    mask = 1u << (idx % CFR_TOUCHED_WORD_BITS);
    bp->touched_mark[word_index] &= ~mask;
}

static int cfr_blueprint_preflop_rehash(CFRBlueprint *bp, uint32_t requested_cap);
static CFRNode *cfr_blueprint_create_node_copying_base(CFRBlueprint *bp,
                                                       uint64_t key,
                                                       int action_count,
                                                       int street_hint,
                                                       const CFRNode *base_node,
                                                       const CFRBlueprint *base_bp,
                                                       uint32_t base_index);

static int cfr_min_int(int a, int b)
{
    return (a < b) ? a : b;
}

static uint64_t cfr_preflop_public_key_from_infoset_key(uint64_t key)
{
    return key & ~(((uint64_t)0xFFu) << 6);
}

static uint32_t cfr_preflop_hand_index_from_infoset_key(uint64_t key)
{
    return (uint32_t)((key >> 6) & 0xFFULL);
}

static uint64_t cfr_parse_u64(const char *text)
{
#ifdef _MSC_VER
    return (uint64_t)_strtoui64(text, NULL, 10);
#else
    return (uint64_t)strtoull(text, NULL, 10);
#endif
}

static int cfr_parse_i32(const char *text)
{
    return (int)strtol(text, NULL, 10);
}

static double cfr_parse_f64(const char *text)
{
    return strtod(text, NULL);
}

static int cfr_detect_hw_threads(void)
{
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    if (info.dwNumberOfProcessors == 0)
    {
        return 1;
    }
    return (int)info.dwNumberOfProcessors;
#else
    return 1;
#endif
}

static double cfr_wall_seconds(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;

    if (freq.QuadPart == 0)
    {
        QueryPerformanceFrequency(&freq);
        if (freq.QuadPart == 0)
        {
            return 0.0;
        }
    }

    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
#else
    return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}

static int cfr_is_data_path(const char *path)
{
    if (path == NULL)
    {
        return 0;
    }
#ifdef _MSC_VER
    if (_stricmp(path, "data") == 0 || _strnicmp(path, "data\\", 5) == 0 || _strnicmp(path, "data/", 5) == 0)
    {
        return 1;
    }
#else
    if (strcmp(path, "data") == 0 || strncmp(path, "data/", 5) == 0)
    {
        return 1;
    }
#endif
    return 0;
}

static void cfr_ensure_data_dir(void)
{
#ifdef _WIN32
    (void)_mkdir("data");
#else
    (void)mkdir("data", 0777);
#endif
}

static void cfr_ensure_dir(const char *path)
{
    if (path == NULL || path[0] == '\0')
    {
        return;
    }
#ifdef _WIN32
    (void)_mkdir(path);
#else
    (void)mkdir(path, 0777);
#endif
}

static uint64_t cfr_rng_next_u64(uint64_t *state)
{
    uint64_t x;

    if (*state == 0ULL)
    {
        *state = 0x9E3779B97F4A7C15ULL;
    }

    x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 2685821657736338717ULL;
}

static int cfr_rng_int(uint64_t *state, int bound)
{
    uint64_t r;

    if (bound <= 0)
    {
        return 0;
    }

    r = cfr_rng_next_u64(state);
    return (int)(r % (uint64_t)bound);
}

static double cfr_rng_unit(uint64_t *state)
{
    uint64_t r;
    r = cfr_rng_next_u64(state) >> 11;
    return (double)r / 9007199254740992.0;
}

static int cfr_card_rank(int card)
{
    return card / 4;
}

static int cfr_card_suit(int card)
{
    return card & 3;
}

static int cfr_parse_rank_char(char c)
{
    static const char ranks[] = "23456789TJQKA";
    int i;
    char u;

    u = (char)toupper((unsigned char)c);
    for (i = 0; i < 13; ++i)
    {
        if (ranks[i] == u)
        {
            return i;
        }
    }
    return -1;
}

static int cfr_parse_suit_char(char c)
{
    char l;
    l = (char)tolower((unsigned char)c);
    if (l == 'c') return 0;
    if (l == 'd') return 1;
    if (l == 'h') return 2;
    if (l == 's') return 3;
    return -1;
}

static int cfr_parse_card_pair(char rank_c, char suit_c)
{
    int r;
    int s;

    r = cfr_parse_rank_char(rank_c);
    s = cfr_parse_suit_char(suit_c);
    if (r < 0 || s < 0)
    {
        return -1;
    }
    return r * 4 + s;
}

static void cfr_card_to_text(int card, char out[3])
{
    static const char ranks[] = "23456789TJQKA";
    static const char suits[] = "cdhs";
    int r;
    int s;

    r = cfr_card_rank(card);
    s = cfr_card_suit(card);
    out[0] = ranks[r];
    out[1] = suits[s];
    out[2] = '\0';
}

static int cfr_parse_cards(const char *text, int *out_cards, int max_cards)
{
    int i;
    int n;

    i = 0;
    n = 0;
    if (text == NULL)
    {
        return 0;
    }

    while (text[i] != '\0' && n < max_cards)
    {
        int card;
        int j;

        while (text[i] != '\0' && (text[i] == ' ' || text[i] == '\t' || text[i] == ',' || text[i] == ';' || text[i] == '-'))
        {
            ++i;
        }
        if (text[i] == '\0')
        {
            break;
        }
        if (text[i + 1] == '\0')
        {
            return -1;
        }

        card = cfr_parse_card_pair(text[i], text[i + 1]);
        if (card < 0)
        {
            return -1;
        }

        for (j = 0; j < n; ++j)
        {
            if (out_cards[j] == card)
            {
                return -1;
            }
        }

        out_cards[n++] = card;
        i += 2;
    }

    while (text[i] != '\0')
    {
        if (!(text[i] == ' ' || text[i] == '\t' || text[i] == ',' || text[i] == ';' || text[i] == '-'))
        {
            return -1;
        }
        ++i;
    }

    return n;
}

static void cfr_shuffle_deck(int *deck, uint64_t *rng)
{
    int i;

    for (i = 0; i < CFR_DECK_SIZE; ++i)
    {
        deck[i] = i;
    }
    for (i = CFR_DECK_SIZE - 1; i > 0; --i)
    {
        int j;
        int tmp;

        j = cfr_rng_int(rng, i + 1);
        tmp = deck[i];
        deck[i] = deck[j];
        deck[j] = tmp;
    }
}

static int cfr_draw_card(CFRHandState *st)
{
    if (st->deck_pos >= CFR_DECK_SIZE)
    {
        return 0;
    }
    return st->deck[st->deck_pos++];
}

static int cfr_count_used_nodes(const CFRBlueprint *bp)
{
    if (bp == NULL)
    {
        return 0;
    }
    return (int)bp->used_node_count;
}

static uint64_t cfr_blueprint_active_node_bytes(const CFRBlueprint *bp)
{
    uint64_t active_regret_actions;
    uint64_t active_strategy_actions;
    uint32_t i;

    if (bp == NULL)
    {
        return 0ULL;
    }

    active_regret_actions = 0ULL;
    active_strategy_actions = 0ULL;
    for (i = 0u; i < bp->used_node_count; ++i)
    {
        const CFRNode *node;
        node = &bp->nodes[i];
        if (cfr_node_is_used(node) && node->action_count > 0)
        {
            active_regret_actions += (uint64_t)node->action_count;
            if (node->strategy_offset != CFR_STRATEGY_OFFSET_NONE)
            {
                active_strategy_actions += (uint64_t)node->action_count;
            }
        }
    }

    return (uint64_t)cfr_count_used_nodes(bp) * (uint64_t)sizeof(CFRNode) +
           active_regret_actions * (uint64_t)sizeof(int32_t) +
           active_strategy_actions * (uint64_t)sizeof(float);
}

static void cfr_blueprint_release(CFRBlueprint *bp)
{
    if (bp == NULL)
    {
        return;
    }
    if (bp->alloc_guard == CFR_BLUEPRINT_ALLOC_GUARD)
    {
        free(bp->nodes);
        free(bp->regret_storage);
        free(bp->strategy_storage);
        free(bp->key_hash_keys);
        free(bp->key_hash_node_plus1);
        free(bp->key_hash_used_slots);
        free(bp->preflop_blocks);
        free(bp->preflop_entry_storage);
        free(bp->preflop_hash_entries);
        free(bp->preflop_hash_used_slots);
        free(bp->touched_indices);
        free(bp->touched_mark);
        free(bp->node_discount_scale);
    }
    memset(bp, 0, sizeof(*bp));
}

static uint64_t cfr_blueprint_allocated_bytes_for(const CFRBlueprint *bp)
{
    uint64_t bytes;

    if (bp == NULL)
    {
        return 0ULL;
    }

    bytes = (uint64_t)sizeof(*bp);
    bytes += (uint64_t)bp->node_capacity * (uint64_t)sizeof(CFRNode);
    bytes += (uint64_t)bp->key_hash_cap * (uint64_t)sizeof(uint64_t);
    bytes += (uint64_t)bp->key_hash_cap * (uint64_t)sizeof(uint32_t);
    bytes += (uint64_t)bp->key_hash_used_slots_cap * (uint64_t)sizeof(uint32_t);
    bytes += (uint64_t)bp->preflop_block_capacity * (uint64_t)sizeof(CFRPreflopBlock);
    bytes += (uint64_t)bp->preflop_entry_capacity * (uint64_t)sizeof(CFRPreflopHandEntry);
    bytes += (uint64_t)bp->preflop_hash_cap * (uint64_t)sizeof(CFRHashEntry);
    bytes += (uint64_t)bp->preflop_hash_used_slots_cap * (uint64_t)sizeof(uint32_t);
    bytes += (uint64_t)bp->touched_capacity * (uint64_t)sizeof(uint32_t);
    bytes += (uint64_t)cfr_touched_mark_word_count(bp->node_capacity) * (uint64_t)sizeof(uint32_t);
    bytes += (uint64_t)bp->node_capacity * (uint64_t)sizeof(float);
    bytes += (uint64_t)bp->regret_capacity * (uint64_t)sizeof(int32_t);
    bytes += (uint64_t)bp->strategy_capacity * (uint64_t)sizeof(float);
    return bytes;
}

static uint64_t cfr_blueprint_allocated_bytes(const CFRBlueprint *bp)
{
    return cfr_blueprint_allocated_bytes_for(bp);
}

static uint32_t cfr_next_pow2_u32(uint32_t v)
{
    if (v <= 1u)
    {
        return 1u;
    }
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1u;
}

static uint32_t cfr_infoset_hash_slot(uint64_t key, uint32_t cap)
{
    /* SplitMix64 finalizer for robust hash spread from 64-bit keys. */
    key ^= key >> 30;
    key *= 0xBF58476D1CE4E5B9ULL;
    key ^= key >> 27;
    key *= 0x94D049BB133111EBULL;
    key ^= key >> 31;
    return (uint32_t)(key & (uint64_t)(cap - 1u));
}

static uint32_t cfr_hash_find_slot(const CFRHashEntry *entries, uint32_t cap, uint64_t key, int *out_found)
{
    uint32_t slot;
    uint32_t probe;

    if (out_found != NULL)
    {
        *out_found = 0;
    }
    if (entries == NULL || cap == 0u)
    {
        return 0u;
    }

    slot = cfr_infoset_hash_slot(key, cap);
    for (probe = 0u; probe < cap; ++probe)
    {
        const CFRHashEntry *entry;

        entry = &entries[slot];
        if (entry->value_plus1 == 0u)
        {
            return slot;
        }
        if (entry->key == key)
        {
            if (out_found != NULL)
            {
                *out_found = 1;
            }
            return slot;
        }
        slot = (slot + 1u) & (cap - 1u);
    }
    return slot;
}

static uint32_t cfr_key_hash_find_slot(const uint64_t *keys,
                                       const uint32_t *node_plus1,
                                       uint32_t cap,
                                       uint64_t key,
                                       int *out_found)
{
    uint32_t slot;
    uint32_t probe;

    if (out_found != NULL)
    {
        *out_found = 0;
    }
    if (keys == NULL || node_plus1 == NULL || cap == 0u)
    {
        return 0u;
    }

    slot = cfr_infoset_hash_slot(key, cap);
    for (probe = 0u; probe < cap; ++probe)
    {
        if (node_plus1[slot] == 0u)
        {
            return slot;
        }
        if (keys[slot] == key)
        {
            if (out_found != NULL)
            {
                *out_found = 1;
            }
            return slot;
        }
        slot = (slot + 1u) & (cap - 1u);
    }
    return slot;
}

static void cfr_hash_clear_sparse(CFRHashEntry *entries,
                                  uint32_t cap,
                                  uint32_t *used_slots,
                                  uint32_t *used_count)
{
    uint32_t i;

    if (entries == NULL || used_slots == NULL || used_count == NULL)
    {
        return;
    }
    for (i = 0u; i < *used_count; ++i)
    {
        uint32_t slot;
        slot = used_slots[i];
        if (slot < cap)
        {
            entries[slot].value_plus1 = 0u;
            entries[slot].key = 0u;
        }
    }
    *used_count = 0u;
}

static void cfr_key_hash_clear_sparse(uint64_t *keys,
                                      uint32_t *node_plus1,
                                      uint32_t cap,
                                      uint32_t *used_slots,
                                      uint32_t *used_count)
{
    uint32_t i;

    if (keys == NULL || node_plus1 == NULL || used_slots == NULL || used_count == NULL)
    {
        return;
    }
    for (i = 0u; i < *used_count; ++i)
    {
        uint32_t slot;

        slot = used_slots[i];
        if (slot < cap)
        {
            keys[slot] = 0ULL;
            node_plus1[slot] = 0u;
        }
    }
    *used_count = 0u;
}

static void cfr_blueprint_refresh_node_payload_ptrs(CFRBlueprint *bp)
{
    uint32_t i;

    if (bp == NULL || bp->nodes == NULL)
    {
        return;
    }

    for (i = 0u; i < bp->used_node_count; ++i)
    {
        CFRNode *node;
        node = &bp->nodes[i];
        if (!cfr_node_is_used(node) || node->action_count <= 0)
        {
            node->regret = NULL;
            node->strategy_sum = NULL;
            continue;
        }

        if (node->regret_offset >= bp->regret_capacity)
        {
            node->regret = NULL;
            if (node->strategy_offset == CFR_STRATEGY_OFFSET_NONE)
            {
                node->strategy_sum = NULL;
            }
            else if (node->strategy_offset < bp->strategy_capacity)
            {
                node->strategy_sum = bp->strategy_storage + node->strategy_offset;
            }
            else
            {
                node->strategy_sum = NULL;
            }
            continue;
        }

        node->regret = bp->regret_storage + node->regret_offset;
        if (node->strategy_offset == CFR_STRATEGY_OFFSET_NONE)
        {
            node->strategy_sum = NULL;
        }
        else if (node->strategy_offset < bp->strategy_capacity)
        {
            node->strategy_sum = bp->strategy_storage + node->strategy_offset;
        }
        else
        {
            node->strategy_sum = NULL;
        }
    }
}

static int cfr_blueprint_resize_regret_storage(CFRBlueprint *bp, uint32_t min_capacity)
{
    uint32_t new_cap;
    int32_t *new_regret;

    if (bp == NULL)
    {
        return 0;
    }
    if (bp->regret_capacity >= min_capacity)
    {
        return 1;
    }

    new_cap = (bp->regret_capacity > 0u) ? bp->regret_capacity : 256u;
    while (new_cap < min_capacity)
    {
        if (new_cap > (UINT32_MAX / 2u))
        {
            new_cap = min_capacity;
            break;
        }
        new_cap *= 2u;
    }
    new_cap = cfr_next_pow2_u32(new_cap);

    new_regret = (int32_t *)malloc((size_t)new_cap * sizeof(int32_t));
    if (new_regret == NULL)
    {
        free(new_regret);
        return 0;
    }

    memset(new_regret, 0, (size_t)new_cap * sizeof(int32_t));
    if (bp->regret_used > 0u && bp->regret_storage != NULL)
    {
        memcpy(new_regret, bp->regret_storage, (size_t)bp->regret_used * sizeof(int32_t));
    }

    free(bp->regret_storage);
    bp->regret_storage = new_regret;
    bp->regret_capacity = new_cap;
    cfr_blueprint_refresh_node_payload_ptrs(bp);
    return 1;
}

static int cfr_blueprint_resize_strategy_storage(CFRBlueprint *bp, uint32_t min_capacity)
{
    uint32_t new_cap;
    float *new_strategy;

    if (bp == NULL)
    {
        return 0;
    }
    if (bp->strategy_capacity >= min_capacity)
    {
        return 1;
    }

    new_cap = (bp->strategy_capacity > 0u) ? bp->strategy_capacity : 256u;
    while (new_cap < min_capacity)
    {
        if (new_cap > (UINT32_MAX / 2u))
        {
            new_cap = min_capacity;
            break;
        }
        new_cap *= 2u;
    }
    new_cap = cfr_next_pow2_u32(new_cap);

    new_strategy = (float *)malloc((size_t)new_cap * sizeof(float));
    if (new_strategy == NULL)
    {
        return 0;
    }

    memset(new_strategy, 0, (size_t)new_cap * sizeof(float));
    if (bp->strategy_used > 0u && bp->strategy_storage != NULL)
    {
        memcpy(new_strategy, bp->strategy_storage, (size_t)bp->strategy_used * sizeof(float));
    }

    free(bp->strategy_storage);
    bp->strategy_storage = new_strategy;
    bp->strategy_capacity = new_cap;
    cfr_blueprint_refresh_node_payload_ptrs(bp);
    return 1;
}

static int cfr_blueprint_alloc_regret_payload(CFRBlueprint *bp,
                                              int action_count,
                                              uint32_t *out_regret_offset)
{
    uint32_t base;

    if (bp == NULL || out_regret_offset == NULL)
    {
        return 0;
    }
    if (action_count < 1)
    {
        action_count = 1;
    }
    if (action_count > CFR_MAX_ACTIONS)
    {
        action_count = CFR_MAX_ACTIONS;
    }
    if (!cfr_blueprint_resize_regret_storage(bp, bp->regret_used + (uint32_t)action_count))
    {
        return 0;
    }

    base = bp->regret_used;
    memset(bp->regret_storage + base, 0, (size_t)action_count * sizeof(int32_t));
    bp->regret_used += (uint32_t)action_count;

    *out_regret_offset = base;
    return 1;
}

static int cfr_blueprint_alloc_strategy_payload(CFRBlueprint *bp,
                                                int action_count,
                                                uint32_t *out_strategy_offset)
{
    uint32_t base;

    if (bp == NULL || out_strategy_offset == NULL)
    {
        return 0;
    }
    if (action_count < 1)
    {
        action_count = 1;
    }
    if (action_count > CFR_MAX_ACTIONS)
    {
        action_count = CFR_MAX_ACTIONS;
    }
    if (!cfr_blueprint_resize_strategy_storage(bp, bp->strategy_used + (uint32_t)action_count))
    {
        return 0;
    }

    base = bp->strategy_used;
    memset(bp->strategy_storage + base, 0, (size_t)action_count * sizeof(float));
    bp->strategy_used += (uint32_t)action_count;
    *out_strategy_offset = base;
    return 1;
}

static int cfr_blueprint_alloc_action_payload(CFRBlueprint *bp,
                                              int action_count,
                                              int need_strategy,
                                              uint32_t *out_regret_offset,
                                              uint32_t *out_strategy_offset)
{
    uint32_t before_regret_used;

    if (bp == NULL || out_regret_offset == NULL || out_strategy_offset == NULL)
    {
        return 0;
    }

    before_regret_used = bp->regret_used;
    if (!cfr_blueprint_alloc_regret_payload(bp, action_count, out_regret_offset))
    {
        return 0;
    }
    if (need_strategy)
    {
        if (!cfr_blueprint_alloc_strategy_payload(bp, action_count, out_strategy_offset))
        {
            bp->regret_used = before_regret_used;
            return 0;
        }
    }
    else
    {
        *out_strategy_offset = CFR_STRATEGY_OFFSET_NONE;
    }
    return 1;
}

static int cfr_blueprint_grow_node_payload(CFRBlueprint *bp, CFRNode *node, int new_action_count)
{
    uint32_t new_regret_offset;
    uint32_t new_strategy_offset;
    int copy_count;
    int need_strategy;

    if (bp == NULL || node == NULL)
    {
        return 0;
    }
    if (new_action_count <= node->action_count)
    {
        return 1;
    }
    if (new_action_count > CFR_MAX_ACTIONS)
    {
        new_action_count = CFR_MAX_ACTIONS;
    }

    need_strategy = (node->strategy_offset != CFR_STRATEGY_OFFSET_NONE);
    if (!cfr_blueprint_alloc_action_payload(bp,
                                            new_action_count,
                                            need_strategy,
                                            &new_regret_offset,
                                            &new_strategy_offset))
    {
        return 0;
    }

    copy_count = node->action_count;
    if (copy_count > 0 && node->regret != NULL)
    {
        memcpy(bp->regret_storage + new_regret_offset, node->regret, (size_t)copy_count * sizeof(int32_t));
        if (need_strategy && node->strategy_sum != NULL)
        {
            memcpy(bp->strategy_storage + new_strategy_offset, node->strategy_sum, (size_t)copy_count * sizeof(float));
        }
    }

    node->regret_offset = new_regret_offset;
    node->strategy_offset = new_strategy_offset;
    node->regret = bp->regret_storage + new_regret_offset;
    if (need_strategy)
    {
        node->strategy_sum = bp->strategy_storage + new_strategy_offset;
    }
    else
    {
        node->strategy_sum = NULL;
    }
    node->action_count = (uint8_t)new_action_count;
    return 1;
}

static int cfr_blueprint_node_has_strategy_payload(const CFRNode *node)
{
    if (node == NULL)
    {
        return 0;
    }
    return node->strategy_offset != CFR_STRATEGY_OFFSET_NONE;
}

static int cfr_blueprint_ensure_strategy_payload(CFRBlueprint *bp, CFRNode *node)
{
    uint32_t new_strategy_offset;

    if (bp == NULL || node == NULL)
    {
        return 0;
    }
    if (node->action_count < 1 || node->regret == NULL)
    {
        return 0;
    }
    if (cfr_blueprint_node_has_strategy_payload(node))
    {
        if (node->strategy_sum == NULL && node->strategy_offset < bp->strategy_capacity)
        {
            node->strategy_sum = bp->strategy_storage + node->strategy_offset;
        }
        return node->strategy_sum != NULL;
    }

    if (!cfr_blueprint_alloc_strategy_payload(bp, node->action_count, &new_strategy_offset))
    {
        return 0;
    }
    node->strategy_offset = new_strategy_offset;
    node->strategy_sum = bp->strategy_storage + new_strategy_offset;
    return 1;
}

static int cfr_blueprint_should_have_strategy_payload(const CFRBlueprint *bp, int street_hint)
{
    if (bp == NULL)
    {
        return 1;
    }
    if (street_hint == 0)
    {
        return 1;
    }
    if (street_hint > 0 && street_hint <= 3 && bp->omit_postflop_strategy_sum)
    {
        return 0;
    }
    return 1;
}

static int cfr_blueprint_resize_nodes(CFRBlueprint *bp, uint32_t min_capacity)
{
    CFRNode *new_nodes;
    uint32_t *new_touched_indices;
    uint32_t *new_touched_mark;
    float *new_discount_scale;
    uint32_t new_cap;
    uint32_t old_cap;
    uint32_t i;

    if (bp == NULL)
    {
        return 0;
    }

    if (bp->node_capacity >= min_capacity)
    {
        return 1;
    }

    old_cap = bp->node_capacity;
    new_cap = (old_cap > 0u) ? old_cap : CFR_INFOSET_INIT_CAP;
    if (new_cap < 1024u)
    {
        new_cap = 1024u;
    }
    while (new_cap < min_capacity)
    {
        if (new_cap > (UINT32_MAX / 2u))
        {
            new_cap = min_capacity;
            break;
        }
        new_cap *= 2u;
    }
    new_cap = cfr_next_pow2_u32(new_cap);

    new_nodes = (CFRNode *)malloc((size_t)new_cap * sizeof(CFRNode));
    if (new_nodes == NULL)
    {
        return 0;
    }
    memset(new_nodes, 0, (size_t)new_cap * sizeof(CFRNode));
    if (bp->nodes != NULL && bp->used_node_count > 0u)
    {
        memcpy(new_nodes, bp->nodes, (size_t)bp->used_node_count * sizeof(CFRNode));
    }

    new_touched_indices = (uint32_t *)malloc((size_t)new_cap * sizeof(uint32_t));
    if (new_touched_indices == NULL)
    {
        free(new_nodes);
        return 0;
    }
    memset(new_touched_indices, 0, (size_t)new_cap * sizeof(uint32_t));
    if (bp->touched_indices != NULL && bp->touched_count > 0u)
    {
        memcpy(new_touched_indices, bp->touched_indices, (size_t)bp->touched_count * sizeof(uint32_t));
    }

    new_touched_mark = (uint32_t *)malloc((size_t)cfr_touched_mark_word_count(new_cap) * sizeof(uint32_t));
    if (new_touched_mark == NULL)
    {
        free(new_touched_indices);
        free(new_nodes);
        return 0;
    }
    memset(new_touched_mark, 0, (size_t)cfr_touched_mark_word_count(new_cap) * sizeof(uint32_t));
    if (bp->touched_mark != NULL && old_cap > 0u)
    {
        memcpy(new_touched_mark,
               bp->touched_mark,
               (size_t)cfr_touched_mark_word_count(old_cap) * sizeof(uint32_t));
    }

    new_discount_scale = (float *)malloc((size_t)new_cap * sizeof(float));
    if (new_discount_scale == NULL)
    {
        free(new_touched_mark);
        free(new_touched_indices);
        free(new_nodes);
        return 0;
    }
    for (i = 0u; i < new_cap; ++i)
    {
        new_discount_scale[i] = (float)((bp->lazy_discount_scale > 0.0) ? bp->lazy_discount_scale : 1.0);
    }
    if (bp->node_discount_scale != NULL && old_cap > 0u)
    {
        memcpy(new_discount_scale, bp->node_discount_scale, (size_t)old_cap * sizeof(float));
    }

    free(bp->nodes);
    free(bp->touched_indices);
    free(bp->touched_mark);
    free(bp->node_discount_scale);
    bp->nodes = new_nodes;
    bp->touched_indices = new_touched_indices;
    bp->touched_mark = new_touched_mark;
    bp->node_discount_scale = new_discount_scale;
    bp->node_capacity = new_cap;
    bp->touched_capacity = new_cap;
    return 1;
}

static int cfr_blueprint_rehash(CFRBlueprint *bp, uint32_t requested_cap)
{
    uint32_t new_cap;
    uint64_t *new_keys;
    uint32_t *new_node_plus1;
    uint32_t *new_slots;
    uint32_t new_used;
    uint32_t i;

    if (bp == NULL)
    {
        return 0;
    }

    if (requested_cap < 1024u)
    {
        requested_cap = 1024u;
    }
    new_cap = cfr_next_pow2_u32(requested_cap);

    new_keys = (uint64_t *)calloc((size_t)new_cap, sizeof(uint64_t));
    new_node_plus1 = (uint32_t *)calloc((size_t)new_cap, sizeof(uint32_t));
    new_slots = (uint32_t *)malloc((size_t)new_cap * sizeof(uint32_t));
    if (new_keys == NULL || new_node_plus1 == NULL || new_slots == NULL)
    {
        free(new_keys);
        free(new_node_plus1);
        free(new_slots);
        return 0;
    }

    new_used = 0u;
    for (i = 0u; i < bp->used_node_count; ++i)
    {
        const CFRNode *node;
        uint32_t slot;
        int found;

        node = &bp->nodes[i];
        if (!cfr_node_is_used(node))
        {
            continue;
        }
        slot = cfr_key_hash_find_slot(new_keys, new_node_plus1, new_cap, node->key, &found);
        if (!found)
        {
            new_keys[slot] = node->key;
            new_node_plus1[slot] = i + 1u;
            new_slots[new_used++] = slot;
        }
    }

    free(bp->key_hash_keys);
    free(bp->key_hash_node_plus1);
    free(bp->key_hash_used_slots);
    bp->key_hash_keys = new_keys;
    bp->key_hash_node_plus1 = new_node_plus1;
    bp->key_hash_used_slots = new_slots;
    bp->key_hash_cap = new_cap;
    bp->key_hash_used_slots_cap = new_cap;
    bp->key_hash_used_count = new_used;
    return 1;
}

static int cfr_blueprint_hash_ensure_insert_capacity(CFRBlueprint *bp, uint32_t extra)
{
    uint64_t lhs;
    uint64_t rhs;

    if (bp == NULL)
    {
        return 0;
    }

    if (bp->key_hash_cap == 0u || bp->key_hash_keys == NULL || bp->key_hash_node_plus1 == NULL || bp->key_hash_used_slots == NULL)
    {
        if (!cfr_blueprint_rehash(bp, CFR_INFOSET_HASH_INIT_CAP))
        {
            return 0;
        }
    }

    lhs = (uint64_t)(bp->key_hash_used_count + extra) * (uint64_t)CFR_INFOSET_HASH_MAX_LOAD_DEN;
    rhs = (uint64_t)bp->key_hash_cap * (uint64_t)CFR_INFOSET_HASH_MAX_LOAD_NUM;
    if (lhs > rhs)
    {
        if (!cfr_blueprint_rehash(bp, bp->key_hash_cap * 2u))
        {
            return 0;
        }
    }
    return 1;
}

static int cfr_blueprint_init(CFRBlueprint *bp, uint64_t seed)
{
    if (bp == NULL)
    {
        return 0;
    }

    cfr_blueprint_release(bp);
    memset(bp, 0, sizeof(*bp));
    bp->alloc_guard = CFR_BLUEPRINT_ALLOC_GUARD;
    if (!cfr_blueprint_resize_nodes(bp, CFR_INFOSET_INIT_CAP))
    {
        cfr_blueprint_release(bp);
        return 0;
    }
    if (!cfr_blueprint_rehash(bp, CFR_INFOSET_HASH_INIT_CAP))
    {
        cfr_blueprint_release(bp);
        return 0;
    }
    if (!cfr_blueprint_resize_regret_storage(bp, 256u))
    {
        cfr_blueprint_release(bp);
        return 0;
    }
    if (!cfr_blueprint_resize_strategy_storage(bp, 256u))
    {
        cfr_blueprint_release(bp);
        return 0;
    }

    if (seed == 0ULL)
    {
        seed = (uint64_t)time(NULL);
        seed ^= 0xA5A5A5A5u;
    }
    bp->rng_state = seed;
    bp->compat_hash32 = 0u;
    bp->abstraction_hash32 = 0u;
    bp->elapsed_train_seconds = 0ULL;
    bp->phase_flags = 0u;
    bp->next_discount_second = 0ULL;
    bp->next_snapshot_second = 0ULL;
    bp->discount_events_applied = 0ULL;
    bp->lazy_discount_scale = 1.0;
    bp->started_at = time(NULL);
    bp->omit_postflop_strategy_sum = 0;
    return 1;
}

static int cfr_blueprint_preflop_block_resize(CFRBlueprint *bp, uint32_t min_capacity)
{
    CFRPreflopBlock *new_blocks;
    uint32_t new_cap;

    if (bp == NULL)
    {
        return 0;
    }
    if (bp->preflop_block_capacity >= min_capacity)
    {
        return 1;
    }

    new_cap = (bp->preflop_block_capacity > 0u) ? bp->preflop_block_capacity : 256u;
    while (new_cap < min_capacity)
    {
        if (new_cap > (UINT32_MAX / 2u))
        {
            new_cap = min_capacity;
            break;
        }
        new_cap *= 2u;
    }
    new_cap = cfr_next_pow2_u32(new_cap);

    new_blocks = (CFRPreflopBlock *)malloc((size_t)new_cap * sizeof(CFRPreflopBlock));
    if (new_blocks == NULL)
    {
        return 0;
    }
    memset(new_blocks, 0, (size_t)new_cap * sizeof(CFRPreflopBlock));
    if (bp->preflop_blocks != NULL && bp->preflop_block_count > 0u)
    {
        memcpy(new_blocks, bp->preflop_blocks, (size_t)bp->preflop_block_count * sizeof(CFRPreflopBlock));
    }
    free(bp->preflop_blocks);
    bp->preflop_blocks = new_blocks;
    bp->preflop_block_capacity = new_cap;
    return 1;
}

static int cfr_blueprint_preflop_entry_resize(CFRBlueprint *bp, uint32_t min_capacity)
{
    CFRPreflopHandEntry *new_entries;
    uint32_t new_cap;

    if (bp == NULL)
    {
        return 0;
    }
    if (bp->preflop_entry_capacity >= min_capacity)
    {
        return 1;
    }

    new_cap = (bp->preflop_entry_capacity > 0u) ? bp->preflop_entry_capacity : 256u;
    while (new_cap < min_capacity)
    {
        if (new_cap > (UINT32_MAX / 2u))
        {
            new_cap = min_capacity;
            break;
        }
        new_cap *= 2u;
    }
    new_cap = cfr_next_pow2_u32(new_cap);

    new_entries = (CFRPreflopHandEntry *)malloc((size_t)new_cap * sizeof(CFRPreflopHandEntry));
    if (new_entries == NULL)
    {
        return 0;
    }
    memset(new_entries, 0, (size_t)new_cap * sizeof(CFRPreflopHandEntry));
    if (bp->preflop_entry_storage != NULL && bp->preflop_entry_used > 0u)
    {
        memcpy(new_entries,
               bp->preflop_entry_storage,
               (size_t)bp->preflop_entry_used * sizeof(CFRPreflopHandEntry));
    }
    free(bp->preflop_entry_storage);
    bp->preflop_entry_storage = new_entries;
    bp->preflop_entry_capacity = new_cap;
    return 1;
}

static CFRPreflopHandEntry *cfr_blueprint_preflop_block_entries(CFRBlueprint *bp, CFRPreflopBlock *block)
{
    if (bp == NULL || block == NULL || bp->preflop_entry_storage == NULL)
    {
        return NULL;
    }
    if (block->entry_capacity == 0u || block->entry_offset >= bp->preflop_entry_capacity)
    {
        return NULL;
    }
    return bp->preflop_entry_storage + block->entry_offset;
}

static const CFRPreflopHandEntry *cfr_blueprint_preflop_block_entries_const(const CFRBlueprint *bp,
                                                                            const CFRPreflopBlock *block)
{
    if (bp == NULL || block == NULL || bp->preflop_entry_storage == NULL)
    {
        return NULL;
    }
    if (block->entry_capacity == 0u || block->entry_offset >= bp->preflop_entry_capacity)
    {
        return NULL;
    }
    return bp->preflop_entry_storage + block->entry_offset;
}

static CFRPreflopHandEntry *cfr_blueprint_preflop_find_entry(CFRBlueprint *bp,
                                                             CFRPreflopBlock *block,
                                                             uint32_t hand_index)
{
    CFRPreflopHandEntry *entries;
    uint32_t i;

    entries = cfr_blueprint_preflop_block_entries(bp, block);
    if (entries == NULL)
    {
        return NULL;
    }
    for (i = 0u; i < (uint32_t)block->entry_count; ++i)
    {
        if ((uint32_t)entries[i].hand_index == hand_index)
        {
            return &entries[i];
        }
    }
    return NULL;
}

static const CFRPreflopHandEntry *cfr_blueprint_preflop_find_entry_const(const CFRBlueprint *bp,
                                                                         const CFRPreflopBlock *block,
                                                                         uint32_t hand_index)
{
    const CFRPreflopHandEntry *entries;
    uint32_t i;

    entries = cfr_blueprint_preflop_block_entries_const(bp, block);
    if (entries == NULL)
    {
        return NULL;
    }
    for (i = 0u; i < (uint32_t)block->entry_count; ++i)
    {
        if ((uint32_t)entries[i].hand_index == hand_index)
        {
            return &entries[i];
        }
    }
    return NULL;
}

static int cfr_blueprint_preflop_block_ensure_entry_capacity(CFRBlueprint *bp,
                                                             CFRPreflopBlock *block,
                                                             uint32_t min_entries)
{
    uint32_t new_block_cap;
    uint32_t new_offset;

    if (bp == NULL || block == NULL)
    {
        return 0;
    }
    if ((uint32_t)block->entry_capacity >= min_entries)
    {
        return 1;
    }

    new_block_cap = (block->entry_capacity > 0u) ? (uint32_t)block->entry_capacity : 4u;
    while (new_block_cap < min_entries)
    {
        if (new_block_cap > (UINT32_MAX / 2u))
        {
            new_block_cap = min_entries;
            break;
        }
        new_block_cap *= 2u;
    }
    new_block_cap = cfr_next_pow2_u32(new_block_cap);

    if (!cfr_blueprint_preflop_entry_resize(bp, bp->preflop_entry_used + new_block_cap))
    {
        return 0;
    }

    new_offset = bp->preflop_entry_used;
    if (block->entry_count > 0u)
    {
        memcpy(bp->preflop_entry_storage + new_offset,
               bp->preflop_entry_storage + block->entry_offset,
               (size_t)block->entry_count * sizeof(CFRPreflopHandEntry));
    }
    memset(bp->preflop_entry_storage + new_offset + block->entry_count,
           0,
           (size_t)(new_block_cap - (uint32_t)block->entry_count) * sizeof(CFRPreflopHandEntry));
    block->entry_offset = new_offset;
    block->entry_capacity = (uint16_t)new_block_cap;
    bp->preflop_entry_used += new_block_cap;
    return 1;
}

static int cfr_blueprint_preflop_block_set_node(CFRBlueprint *bp,
                                                CFRPreflopBlock *block,
                                                uint32_t hand_index,
                                                uint32_t node_plus1)
{
    CFRPreflopHandEntry *entry;
    CFRPreflopHandEntry *entries;

    if (bp == NULL || block == NULL || hand_index >= CFR_PREFLOP_HAND_BUCKETS || node_plus1 == 0u)
    {
        return 0;
    }

    entry = cfr_blueprint_preflop_find_entry(bp, block, hand_index);
    if (entry != NULL)
    {
        entry->node_plus1 = node_plus1;
        return 1;
    }

    if (!cfr_blueprint_preflop_block_ensure_entry_capacity(bp, block, (uint32_t)block->entry_count + 1u))
    {
        return 0;
    }

    entries = cfr_blueprint_preflop_block_entries(bp, block);
    if (entries == NULL)
    {
        return 0;
    }
    entry = &entries[block->entry_count];
    entry->hand_index = (uint8_t)hand_index;
    entry->node_plus1 = node_plus1;
    block->entry_count = (uint16_t)((uint32_t)block->entry_count + 1u);
    return 1;
}

static int cfr_blueprint_preflop_rehash(CFRBlueprint *bp, uint32_t requested_cap)
{
    uint32_t new_cap;
    CFRHashEntry *new_entries;
    uint32_t *new_slots;
    uint32_t new_used;
    uint32_t i;

    if (bp == NULL)
    {
        return 0;
    }
    if (requested_cap < 256u)
    {
        requested_cap = 256u;
    }
    new_cap = cfr_next_pow2_u32(requested_cap);

    new_entries = (CFRHashEntry *)calloc((size_t)new_cap, sizeof(CFRHashEntry));
    new_slots = (uint32_t *)malloc((size_t)new_cap * sizeof(uint32_t));
    if (new_entries == NULL || new_slots == NULL)
    {
        free(new_entries);
        free(new_slots);
        return 0;
    }

    new_used = 0u;
    for (i = 0u; i < bp->preflop_block_count; ++i)
    {
        uint32_t slot;
        int found;

        slot = cfr_hash_find_slot(new_entries, new_cap, bp->preflop_blocks[i].public_key, &found);
        if (!found)
        {
            new_entries[slot].key = bp->preflop_blocks[i].public_key;
            new_entries[slot].value_plus1 = i + 1u;
            new_slots[new_used++] = slot;
        }
    }

    free(bp->preflop_hash_entries);
    free(bp->preflop_hash_used_slots);
    bp->preflop_hash_entries = new_entries;
    bp->preflop_hash_used_slots = new_slots;
    bp->preflop_hash_cap = new_cap;
    bp->preflop_hash_used_slots_cap = new_cap;
    bp->preflop_hash_used_count = new_used;
    return 1;
}

static int cfr_blueprint_preflop_hash_ensure_insert_capacity(CFRBlueprint *bp, uint32_t extra)
{
    uint64_t lhs;
    uint64_t rhs;

    if (bp == NULL)
    {
        return 0;
    }
    if (bp->preflop_hash_cap == 0u || bp->preflop_hash_entries == NULL || bp->preflop_hash_used_slots == NULL)
    {
        if (!cfr_blueprint_preflop_rehash(bp, 256u))
        {
            return 0;
        }
    }
    lhs = (uint64_t)(bp->preflop_hash_used_count + extra) * (uint64_t)CFR_INFOSET_HASH_MAX_LOAD_DEN;
    rhs = (uint64_t)bp->preflop_hash_cap * (uint64_t)CFR_INFOSET_HASH_MAX_LOAD_NUM;
    if (lhs > rhs)
    {
        if (!cfr_blueprint_preflop_rehash(bp, bp->preflop_hash_cap * 2u))
        {
            return 0;
        }
    }
    return 1;
}

static void cfr_blueprint_hash_clear_sparse(CFRBlueprint *bp)
{
    if (bp == NULL)
    {
        return;
    }
    cfr_key_hash_clear_sparse(bp->key_hash_keys,
                              bp->key_hash_node_plus1,
                              bp->key_hash_cap,
                              bp->key_hash_used_slots,
                              &bp->key_hash_used_count);
    cfr_hash_clear_sparse(bp->preflop_hash_entries,
                          bp->preflop_hash_cap,
                          bp->preflop_hash_used_slots,
                          &bp->preflop_hash_used_count);
    bp->preflop_block_count = 0u;
    bp->preflop_entry_used = 0u;
}

static void cfr_blueprint_reset_sparse(CFRBlueprint *bp)
{
    uint32_t i;

    if (bp == NULL)
    {
        return;
    }

    cfr_blueprint_hash_clear_sparse(bp);
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
    bp->used_node_count = 0u;
    bp->regret_used = 0u;
    bp->strategy_used = 0u;
    bp->touched_count = 0u;
    bp->lazy_discount_scale = 1.0;
    bp->overlay_base = NULL;
}

static int cfr_blueprint_sync_node_discount(CFRBlueprint *bp, uint32_t node_index)
{
    float mark;
    double target;
    double ratio;
    CFRNode *node;
    int a;

    if (bp == NULL || bp->nodes == NULL || bp->node_discount_scale == NULL)
    {
        return 0;
    }
    if (node_index >= bp->used_node_count)
    {
        return 0;
    }

    target = (bp->lazy_discount_scale > 0.0) ? bp->lazy_discount_scale : 1.0;
    mark = bp->node_discount_scale[node_index];
    if (!(mark > 0.0f))
    {
        mark = 1.0f;
    }

    if (((double)mark > target ? (double)mark - target : target - (double)mark) <= 1e-12)
    {
        return 1;
    }

    ratio = target / (double)mark;
    node = &bp->nodes[node_index];
    if (node->regret == NULL || node->action_count <= 0)
    {
        bp->node_discount_scale[node_index] = (float)target;
        return 1;
    }
    for (a = 0; a < node->action_count; ++a)
    {
        double scaled_regret;

        scaled_regret = (double)node->regret[a] * ratio;
        if (scaled_regret >= 0.0)
        {
            node->regret[a] = (int32_t)(scaled_regret + 0.5);
        }
        else
        {
            node->regret[a] = (int32_t)(scaled_regret - 0.5);
        }
        if (node->strategy_sum != NULL)
        {
            node->strategy_sum[a] = (float)((double)node->strategy_sum[a] * ratio);
        }
    }
    bp->node_discount_scale[node_index] = (float)target;
    return 1;
}

static void cfr_blueprint_materialize_all(CFRBlueprint *bp)
{
    uint32_t i;

    if (bp == NULL)
    {
        return;
    }
    for (i = 0u; i < bp->used_node_count; ++i)
    {
        if (cfr_node_is_used(&bp->nodes[i]))
        {
            (void)cfr_blueprint_sync_node_discount(bp, i);
        }
    }
}

static CFRPreflopBlock *cfr_blueprint_find_preflop_block_raw(const CFRBlueprint *bp, uint64_t public_key, uint32_t *out_index)
{
    uint32_t slot;
    int found;

    if (bp == NULL || bp->preflop_hash_cap == 0u || bp->preflop_hash_entries == NULL)
    {
        return NULL;
    }

    slot = cfr_hash_find_slot(bp->preflop_hash_entries, bp->preflop_hash_cap, public_key, &found);
    if (!found)
    {
        if (bp->overlay_base != NULL)
        {
            return cfr_blueprint_find_preflop_block_raw(bp->overlay_base, public_key, out_index);
        }
        return NULL;
    }
    if (out_index != NULL)
    {
        *out_index = bp->preflop_hash_entries[slot].value_plus1 - 1u;
    }
    return &bp->preflop_blocks[bp->preflop_hash_entries[slot].value_plus1 - 1u];
}

static CFRPreflopBlock *cfr_blueprint_find_preflop_block_local(const CFRBlueprint *bp, uint64_t public_key, uint32_t *out_index)
{
    uint32_t slot;
    int found;

    if (bp == NULL || bp->preflop_hash_cap == 0u || bp->preflop_hash_entries == NULL)
    {
        return NULL;
    }

    slot = cfr_hash_find_slot(bp->preflop_hash_entries, bp->preflop_hash_cap, public_key, &found);
    if (!found)
    {
        return NULL;
    }
    if (out_index != NULL)
    {
        *out_index = bp->preflop_hash_entries[slot].value_plus1 - 1u;
    }
    return &bp->preflop_blocks[bp->preflop_hash_entries[slot].value_plus1 - 1u];
}

static CFRPreflopBlock *cfr_blueprint_get_preflop_block(CFRBlueprint *bp, uint64_t public_key, int create, int action_count)
{
    uint32_t slot;
    int found;

    if (bp == NULL)
    {
        return NULL;
    }
    if (bp->preflop_hash_cap == 0u || bp->preflop_hash_entries == NULL)
    {
        if (!cfr_blueprint_preflop_rehash(bp, 256u))
        {
            return NULL;
        }
    }

    if (create && !cfr_blueprint_preflop_hash_ensure_insert_capacity(bp, 1u))
    {
        return NULL;
    }

    slot = cfr_hash_find_slot(bp->preflop_hash_entries, bp->preflop_hash_cap, public_key, &found);
    if (found)
    {
        CFRPreflopBlock *block;

        block = &bp->preflop_blocks[bp->preflop_hash_entries[slot].value_plus1 - 1u];
        if (action_count > (int)block->action_count)
        {
            block->action_count = (uint8_t)action_count;
        }
        return block;
    }

    if (!create)
    {
        if (bp->overlay_base != NULL)
        {
            return cfr_blueprint_find_preflop_block_raw(bp->overlay_base, public_key, NULL);
        }
        return NULL;
    }

    if (!cfr_blueprint_preflop_block_resize(bp, bp->preflop_block_count + 1u))
    {
        return NULL;
    }
    {
        CFRPreflopBlock *block;
        uint32_t block_index;

        block_index = bp->preflop_block_count++;
        block = &bp->preflop_blocks[block_index];
        memset(block, 0, sizeof(*block));
        block->public_key = public_key;
        block->action_count = (uint8_t)cfr_max_int(1, action_count);
        bp->preflop_hash_entries[slot].key = public_key;
        bp->preflop_hash_entries[slot].value_plus1 = block_index + 1u;
        if (bp->preflop_hash_used_count < bp->preflop_hash_used_slots_cap)
        {
            bp->preflop_hash_used_slots[bp->preflop_hash_used_count++] = slot;
        }
        return block;
    }
}

static CFRNode *cfr_blueprint_get_node_ex(CFRBlueprint *bp,
                                          uint64_t key,
                                          int create,
                                          int action_count,
                                          int street_hint);

static CFRNode *cfr_blueprint_get_preflop_node(CFRBlueprint *bp, uint64_t key, int create, int action_count)
{
    uint64_t public_key;
    uint32_t hand_index;
    CFRPreflopBlock *block;
    uint32_t node_plus1;

    public_key = cfr_preflop_public_key_from_infoset_key(key);
    hand_index = cfr_preflop_hand_index_from_infoset_key(key);
    if (hand_index >= CFR_PREFLOP_HAND_BUCKETS)
    {
        return NULL;
    }

    if (!create)
    {
        block = cfr_blueprint_find_preflop_block_local(bp, public_key, NULL);
        if (block != NULL)
        {
            const CFRPreflopHandEntry *entry;

            entry = cfr_blueprint_preflop_find_entry_const(bp, block, hand_index);
            if (entry != NULL && entry->node_plus1 != 0u)
            {
                CFRNode *node;

                node_plus1 = entry->node_plus1;
                node = &bp->nodes[node_plus1 - 1u];
                (void)cfr_blueprint_sync_node_discount(bp, node_plus1 - 1u);
                return node;
            }
        }
        if (bp->overlay_base != NULL)
        {
            return cfr_blueprint_get_node_ex((CFRBlueprint *)bp->overlay_base, key, 0, action_count, 0);
        }
        return NULL;
    }

    block = cfr_blueprint_get_preflop_block(bp, public_key, 1, action_count);
    if (block == NULL)
    {
        return NULL;
    }

    {
        const CFRPreflopHandEntry *entry;

        entry = cfr_blueprint_preflop_find_entry_const(bp, block, hand_index);
        node_plus1 = (entry != NULL) ? entry->node_plus1 : 0u;
    }
    if (node_plus1 != 0u)
    {
        CFRNode *node;
        node = &bp->nodes[node_plus1 - 1u];
        if (action_count > (int)node->action_count)
        {
            if (!cfr_blueprint_grow_node_payload(bp, node, action_count))
            {
                return NULL;
            }
            block->action_count = (uint8_t)action_count;
        }
        (void)cfr_blueprint_sync_node_discount(bp, node_plus1 - 1u);
        return node;
    }

    {
        const CFRBlueprint *base_bp;
        CFRPreflopBlock *base_block;
        CFRNode *base_node;
        uint32_t base_index;
        CFRNode *node;

        base_bp = bp->overlay_base;
        base_block = NULL;
        base_node = NULL;
        base_index = 0u;
        if (base_bp != NULL)
        {
            base_block = cfr_blueprint_find_preflop_block_raw(base_bp, public_key, NULL);
            if (base_block != NULL)
            {
                const CFRPreflopHandEntry *base_entry;

                base_entry = cfr_blueprint_preflop_find_entry_const(base_bp, base_block, hand_index);
                node_plus1 = (base_entry != NULL) ? base_entry->node_plus1 : 0u;
                if (node_plus1 != 0u)
                {
                    base_index = node_plus1 - 1u;
                    base_node = &((CFRBlueprint *)base_bp)->nodes[base_index];
                }
            }
        }

        node = cfr_blueprint_create_node_copying_base(bp, key, action_count, 0, base_node, base_bp, base_index);
        if (node == NULL)
        {
            return NULL;
        }
        if (!cfr_blueprint_preflop_block_set_node(bp, block, hand_index, (uint32_t)(node - bp->nodes) + 1u))
        {
            return NULL;
        }
        if (action_count > (int)block->action_count)
        {
            block->action_count = (uint8_t)action_count;
        }
        return node;
    }
}

static CFRNode *cfr_blueprint_create_node_copying_base(CFRBlueprint *bp,
                                                       uint64_t key,
                                                       int action_count,
                                                       int street_hint,
                                                       const CFRNode *base_node,
                                                       const CFRBlueprint *base_bp,
                                                       uint32_t base_index)
{
    CFRNode *node;
    int init_action_count;
    uint32_t new_regret_offset;
    uint32_t new_strategy_offset;
    int need_strategy_payload;

    if (bp == NULL)
    {
        return NULL;
    }
    if (!cfr_blueprint_resize_nodes(bp, bp->used_node_count + 1u))
    {
        return NULL;
    }

    node = &bp->nodes[bp->used_node_count];
    memset(node, 0, sizeof(*node));

    init_action_count = action_count;
    if (base_node != NULL && (int)base_node->action_count > init_action_count)
    {
        init_action_count = (int)base_node->action_count;
    }
    if (base_node != NULL)
    {
        need_strategy_payload = cfr_blueprint_node_has_strategy_payload(base_node);
    }
    else
    {
        need_strategy_payload = cfr_blueprint_should_have_strategy_payload(bp, street_hint);
    }
    if (!cfr_blueprint_alloc_action_payload(bp,
                                            init_action_count,
                                            need_strategy_payload,
                                            &new_regret_offset,
                                            &new_strategy_offset))
    {
        return NULL;
    }

    node->regret_offset = new_regret_offset;
    node->strategy_offset = new_strategy_offset;
    node->regret = bp->regret_storage + new_regret_offset;
    node->strategy_sum = (new_strategy_offset != CFR_STRATEGY_OFFSET_NONE)
                             ? (bp->strategy_storage + new_strategy_offset)
                             : NULL;

    if (base_node != NULL)
    {
        double bb_target;
        float bb_mark;
        double bb_ratio;
        int a;

        node->street_hint = base_node->street_hint;
        node->action_count = base_node->action_count;
        bb_target = (base_bp != NULL && base_bp->lazy_discount_scale > 0.0) ? base_bp->lazy_discount_scale : 1.0;
        bb_mark = 1.0f;
        if (base_bp != NULL && base_bp->node_discount_scale != NULL && base_index < base_bp->used_node_count)
        {
            bb_mark = base_bp->node_discount_scale[base_index];
        }
        if (!(bb_mark > 0.0f))
        {
            bb_mark = 1.0f;
        }
        bb_ratio = bb_target / (double)bb_mark;
        if (bb_ratio != 1.0)
        {
            for (a = 0; a < node->action_count; ++a)
            {
                double scaled_regret;

                scaled_regret = (double)base_node->regret[a] * bb_ratio;
                node->regret[a] = (scaled_regret >= 0.0)
                                      ? (int32_t)(scaled_regret + 0.5)
                                      : (int32_t)(scaled_regret - 0.5);
                if (node->strategy_sum != NULL && base_node->strategy_sum != NULL)
                {
                    node->strategy_sum[a] = (float)((double)base_node->strategy_sum[a] * bb_ratio);
                }
            }
        }
        else
        {
            memcpy(node->regret, base_node->regret, (size_t)base_node->action_count * sizeof(int32_t));
            if (node->strategy_sum != NULL && base_node->strategy_sum != NULL)
            {
                memcpy(node->strategy_sum, base_node->strategy_sum, (size_t)base_node->action_count * sizeof(float));
            }
        }

        if (init_action_count > (int)node->action_count)
        {
            for (a = (int)node->action_count; a < init_action_count; ++a)
            {
                node->regret[a] = 0;
                if (node->strategy_sum != NULL)
                {
                    node->strategy_sum[a] = 0.0f;
                }
            }
            node->action_count = (uint8_t)init_action_count;
        }
    }
    else
    {
        node->street_hint = (street_hint >= 0) ? (uint8_t)street_hint : 255u;
        node->action_count = (uint8_t)init_action_count;
    }

    cfr_node_set_overlay_meta(node, base_node != NULL ? (base_index + 1u) : 0u, 0);
    node->key = key;
    if (bp->node_discount_scale != NULL && bp->used_node_count < bp->node_capacity)
    {
        bp->node_discount_scale[bp->used_node_count] = (float)((bp->lazy_discount_scale > 0.0) ? bp->lazy_discount_scale : 1.0);
    }
    bp->used_node_count++;
    return node;
}

static CFRNode *cfr_blueprint_find_node_raw(const CFRBlueprint *bp, uint64_t key, uint32_t *out_index)
{
    uint32_t slot;
    int found;

    if (bp == NULL)
    {
        return NULL;
    }
    if (bp->key_hash_cap == 0u || bp->key_hash_keys == NULL || bp->key_hash_node_plus1 == NULL)
    {
        return NULL;
    }

    slot = cfr_key_hash_find_slot(bp->key_hash_keys, bp->key_hash_node_plus1, bp->key_hash_cap, key, &found);
    if (!found)
    {
        if (bp->overlay_base != NULL)
        {
            return cfr_blueprint_find_node_raw(bp->overlay_base, key, out_index);
        }
        return NULL;
    }
    if (out_index != NULL)
    {
        *out_index = bp->key_hash_node_plus1[slot] - 1u;
    }
    return (CFRNode *)&bp->nodes[bp->key_hash_node_plus1[slot] - 1u];
}

static CFRNode *cfr_blueprint_get_node_ex(CFRBlueprint *bp,
                                          uint64_t key,
                                          int create,
                                          int action_count,
                                          int street_hint)
{
    uint32_t slot;
    int found;

    if (bp == NULL)
    {
        return NULL;
    }
    if (action_count < 1)
    {
        action_count = 1;
    }
    if (action_count > CFR_MAX_ACTIONS)
    {
        action_count = CFR_MAX_ACTIONS;
    }

    if (bp->key_hash_cap == 0u || bp->key_hash_keys == NULL || bp->key_hash_node_plus1 == NULL)
    {
        if (!cfr_blueprint_rehash(bp, CFR_INFOSET_HASH_INIT_CAP))
        {
            return NULL;
        }
    }

    if (create && !cfr_blueprint_hash_ensure_insert_capacity(bp, 1u))
    {
        return NULL;
    }

    slot = cfr_key_hash_find_slot(bp->key_hash_keys, bp->key_hash_node_plus1, bp->key_hash_cap, key, &found);
    if (!found)
    {
        const CFRBlueprint *base_bp;
        CFRNode *base_node;
        uint32_t base_index;
        CFRNode *node;

        if (!create)
        {
            if (bp->overlay_base != NULL)
            {
                return cfr_blueprint_get_node_ex((CFRBlueprint *)bp->overlay_base,
                                                 key,
                                                 0,
                                                 action_count,
                                                 street_hint);
            }
            return NULL;
        }

        base_bp = bp->overlay_base;
        base_node = NULL;
        base_index = 0u;
        if (base_bp != NULL)
        {
            base_node = cfr_blueprint_find_node_raw(base_bp, key, &base_index);
        }
        node = cfr_blueprint_create_node_copying_base(bp, key, action_count, street_hint, base_node, base_bp, base_index);
        if (node == NULL)
        {
            return NULL;
        }

        bp->key_hash_keys[slot] = key;
        bp->key_hash_node_plus1[slot] = (uint32_t)(node - bp->nodes) + 1u;
        if (bp->key_hash_used_count < bp->key_hash_used_slots_cap)
        {
            bp->key_hash_used_slots[bp->key_hash_used_count++] = slot;
        }
        return node;
    }

    {
        CFRNode *node;

        node = &bp->nodes[bp->key_hash_node_plus1[slot] - 1u];
        if (create && action_count > (int)node->action_count)
        {
            if (!cfr_blueprint_grow_node_payload(bp, node, action_count))
            {
                return NULL;
            }
        }
        (void)cfr_blueprint_sync_node_discount(bp, bp->key_hash_node_plus1[slot] - 1u);
        return node;
    }
}

static CFRNode *cfr_blueprint_get_node(CFRBlueprint *bp, uint64_t key, int create, int action_count)
{
    return cfr_blueprint_get_node_ex(bp, key, create, action_count, -1);
}

static int cfr_blueprint_rebuild_all_indexes(CFRBlueprint *bp)
{
    uint32_t i;

    if (bp == NULL)
    {
        return 0;
    }
    if (bp->key_hash_cap == 0u && !cfr_blueprint_rehash(bp, CFR_INFOSET_HASH_INIT_CAP))
    {
        return 0;
    }
    cfr_blueprint_hash_clear_sparse(bp);
    for (i = 0u; i < bp->used_node_count; ++i)
    {
        CFRNode *node;

        node = &bp->nodes[i];
        if (!cfr_node_is_used(node))
        {
            continue;
        }

        {
            uint32_t slot;
            int found;

            slot = cfr_key_hash_find_slot(bp->key_hash_keys, bp->key_hash_node_plus1, bp->key_hash_cap, node->key, &found);
            if (found)
            {
                bp->key_hash_node_plus1[slot] = i + 1u;
            }
            else
            {
                bp->key_hash_keys[slot] = node->key;
                bp->key_hash_node_plus1[slot] = i + 1u;
                if (bp->key_hash_used_count < bp->key_hash_used_slots_cap)
                {
                    bp->key_hash_used_slots[bp->key_hash_used_count++] = slot;
                }
            }
        }
    }
    return 1;
}

static int cfr_blueprint_copy_from(CFRBlueprint *dst, const CFRBlueprint *src)
{
    if (dst == NULL || src == NULL)
    {
        return 0;
    }
    if (dst == src)
    {
        return 1;
    }

    cfr_blueprint_release(dst);
    memset(dst, 0, sizeof(*dst));
    dst->alloc_guard = CFR_BLUEPRINT_ALLOC_GUARD;
    if (!cfr_blueprint_resize_nodes(dst, src->node_capacity))
    {
        cfr_blueprint_release(dst);
        return 0;
    }
    if (!cfr_blueprint_rehash(dst, src->key_hash_cap))
    {
        cfr_blueprint_release(dst);
        return 0;
    }
    if (!cfr_blueprint_resize_regret_storage(dst, src->regret_capacity))
    {
        cfr_blueprint_release(dst);
        return 0;
    }
    if (!cfr_blueprint_resize_strategy_storage(dst, src->strategy_capacity))
    {
        cfr_blueprint_release(dst);
        return 0;
    }

    dst->used_node_count = src->used_node_count;
    dst->regret_used = src->regret_used;
    dst->strategy_used = src->strategy_used;
    if (src->regret_used > 0u)
    {
        memcpy(dst->regret_storage, src->regret_storage, (size_t)src->regret_used * sizeof(int32_t));
    }
    if (src->strategy_used > 0u)
    {
        memcpy(dst->strategy_storage, src->strategy_storage, (size_t)src->strategy_used * sizeof(float));
    }
    if (src->used_node_count > 0u)
    {
        memcpy(dst->nodes, src->nodes, (size_t)src->used_node_count * sizeof(CFRNode));
        cfr_blueprint_refresh_node_payload_ptrs(dst);
        if (dst->node_discount_scale != NULL && src->node_discount_scale != NULL)
        {
            memcpy(dst->node_discount_scale, src->node_discount_scale, (size_t)src->used_node_count * sizeof(float));
        }
    }

    if (!cfr_blueprint_rebuild_all_indexes(dst))
    {
        cfr_blueprint_release(dst);
        return 0;
    }

    dst->touched_count = src->touched_count;
    if (src->touched_count > 0u)
    {
        memcpy(dst->touched_indices, src->touched_indices, (size_t)src->touched_count * sizeof(uint32_t));
    }
    if (dst->touched_mark != NULL && dst->node_capacity > 0u)
    {
        memset(dst->touched_mark, 0, (size_t)cfr_touched_mark_word_count(dst->node_capacity) * sizeof(uint32_t));
        if (src->touched_mark != NULL)
        {
            uint32_t n;
            n = (src->node_capacity < dst->node_capacity) ? src->node_capacity : dst->node_capacity;
            memcpy(dst->touched_mark,
                   src->touched_mark,
                   (size_t)cfr_touched_mark_word_count(n) * sizeof(uint32_t));
        }
    }

    dst->overlay_base = src->overlay_base;
    dst->compat_hash32 = src->compat_hash32;
    dst->abstraction_hash32 = src->abstraction_hash32;
    dst->elapsed_train_seconds = src->elapsed_train_seconds;
    dst->phase_flags = src->phase_flags;
    dst->next_discount_second = src->next_discount_second;
    dst->next_snapshot_second = src->next_snapshot_second;
    dst->discount_events_applied = src->discount_events_applied;
    dst->iteration = src->iteration;
    dst->total_hands = src->total_hands;
    dst->rng_state = src->rng_state;
    dst->lazy_discount_scale = src->lazy_discount_scale;
    dst->started_at = src->started_at;
    dst->omit_postflop_strategy_sum = src->omit_postflop_strategy_sum;
    return 1;
}

static void cfr_compute_strategy_n(const CFRNode *node, int action_count, float *out)
{
    int available;
    int i;
    float sum;

    if (node == NULL || out == NULL || action_count <= 0)
    {
        return;
    }

    available = action_count;
    if (node->action_count > 0 && available > node->action_count)
    {
        available = node->action_count;
    }

    sum = 0.0f;
    if (node->regret == NULL)
    {
        float p;
        p = 1.0f / (float)action_count;
        for (i = 0; i < action_count; ++i)
        {
            out[i] = p;
        }
        for (i = action_count; i < CFR_MAX_ACTIONS; ++i)
        {
            out[i] = 0.0f;
        }
        return;
    }

    for (i = 0; i < available; ++i)
    {
        float r;
        r = (float)node->regret[i];
        if (r < 0.0f)
        {
            r = 0.0f;
        }
        out[i] = r;
        sum += r;
    }

    for (i = available; i < action_count; ++i)
    {
        out[i] = 0.0f;
    }

    if (sum <= 1e-12f)
    {
        float p;
        p = 1.0f / (float)action_count;
        for (i = 0; i < action_count; ++i)
        {
            out[i] = p;
        }
    }
    else
    {
        for (i = 0; i < action_count; ++i)
        {
            out[i] /= sum;
        }
    }

    for (i = action_count; i < CFR_MAX_ACTIONS; ++i)
    {
        out[i] = 0.0f;
    }
}

static void cfr_compute_strategy(const CFRNode *node, float *out)
{
    int n;

    n = node->action_count;
    if (n < 1)
    {
        n = 1;
    }
    if (n > CFR_MAX_ACTIONS)
    {
        n = CFR_MAX_ACTIONS;
    }

    cfr_compute_strategy_n(node, n, out);
}

static void cfr_compute_average_strategy_n(const CFRNode *node, int action_count, float *out)
{
    int available;
    int i;
    float sum;

    if (node == NULL || out == NULL || action_count <= 0)
    {
        return;
    }

    available = action_count;
    if (node->action_count > 0 && available > node->action_count)
    {
        available = node->action_count;
    }

    sum = 0.0f;
    if (node->strategy_sum == NULL)
    {
        cfr_compute_strategy_n(node, action_count, out);
        return;
    }
    for (i = 0; i < available; ++i)
    {
        float v;
        v = node->strategy_sum[i];
        if (v < 0.0f)
        {
            v = 0.0f;
        }
        out[i] = v;
        sum += v;
    }

    for (i = available; i < action_count; ++i)
    {
        out[i] = 0.0f;
    }

    if (sum <= 1e-12f)
    {
        cfr_compute_strategy_n(node, action_count, out);
        return;
    }

    for (i = 0; i < action_count; ++i)
    {
        out[i] /= sum;
    }

    for (i = action_count; i < CFR_MAX_ACTIONS; ++i)
    {
        out[i] = 0.0f;
    }
}


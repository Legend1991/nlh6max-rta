/*
 * hand-isomorphism integration.
 *
 * Maps (hole + board by street) to canonical suit-isomorphic indices for
 * preflop/flop/turn/river.
 */

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include "../third_party/hand-isomorphism/src/hand_index.h"

#ifdef _MSC_VER
static unsigned int cfr_hi_ctz_u32(uint32_t v)
{
    unsigned long idx;
    if (v == 0u)
    {
        return 32u;
    }
    _BitScanForward(&idx, (unsigned long)v);
    return (unsigned int)idx;
}

static unsigned int cfr_hi_popcount_u32(uint32_t v)
{
    return (unsigned int)__popcnt(v);
}

#define __builtin_ctz(x) cfr_hi_ctz_u32((uint32_t)(x))
#define __builtin_popcount(x) cfr_hi_popcount_u32((uint32_t)(x))
#define __attribute__(x)
#pragma warning(push)
#pragma warning(disable: 4100 4146 4244 4459)
#endif

#include "../third_party/hand-isomorphism/src/hand_index.c"

#ifdef _MSC_VER
#pragma warning(pop)
#undef __attribute__
#undef __builtin_popcount
#undef __builtin_ctz
#endif

static hand_indexer_t g_cfr_holdem_indexer;
static volatile LONG g_cfr_holdem_init_state = 0; /* 0=uninit, 1=initing, 2=ready, -1=failed */

static int cfr_hand_index_init(void)
{
    static const uint8_t cards_per_round[4] = {2, 3, 1, 1};
    LONG state;

    state = InterlockedCompareExchange(&g_cfr_holdem_init_state, 0, 0);
    if (state == 2)
    {
        return 1;
    }
    if (state == -1)
    {
        return 0;
    }

    if (InterlockedCompareExchange(&g_cfr_holdem_init_state, 1, 0) == 0)
    {
        hand_index_ctor();
        if (!hand_indexer_init(4, cards_per_round, &g_cfr_holdem_indexer))
        {
            InterlockedExchange(&g_cfr_holdem_init_state, -1);
            return 0;
        }
        InterlockedExchange(&g_cfr_holdem_init_state, 2);
        return 1;
    }

    for (;;)
    {
        state = InterlockedCompareExchange(&g_cfr_holdem_init_state, 0, 0);
        if (state == 2)
        {
            return 1;
        }
        if (state == -1)
        {
            return 0;
        }
        Sleep(0);
    }
}

static int cfr_required_board_cards_for_street(int street)
{
    if (street == 0) return 0;
    if (street == 1) return 3;
    if (street == 2) return 4;
    if (street == 3) return 5;
    return -1;
}

static int cfr_cards_are_unique_u8(const uint8_t *cards, int count)
{
    uint64_t used;
    int i;

    used = 0ULL;
    for (i = 0; i < count; ++i)
    {
        uint64_t bit;

        if (cards[i] >= 52u)
        {
            return 0;
        }

        bit = 1ULL << cards[i];
        if ((used & bit) != 0ULL)
        {
            return 0;
        }
        used |= bit;
    }

    return 1;
}

static int cfr_hand_index_for_state(int street, int hole0, int hole1, const int *board, int board_count, uint64_t *out_index)
{
    uint8_t cards[7];
    int i;
    int need_board;
    hand_indexer_state_t state;
    hand_index_t idx;

    if (out_index == NULL)
    {
        return 0;
    }

    need_board = cfr_required_board_cards_for_street(street);
    if (need_board < 0 || board_count != need_board)
    {
        return 0;
    }

    if (!cfr_hand_index_init())
    {
        return 0;
    }

    if (hole0 < 0 || hole0 >= 52 || hole1 < 0 || hole1 >= 52)
    {
        return 0;
    }

    cards[0] = (uint8_t)hole0;
    cards[1] = (uint8_t)hole1;
    for (i = 0; i < need_board; ++i)
    {
        if (board == NULL || board[i] < 0 || board[i] >= 52)
        {
            return 0;
        }
        cards[2 + i] = (uint8_t)board[i];
    }

    if (!cfr_cards_are_unique_u8(cards, 2 + need_board))
    {
        return 0;
    }

    hand_indexer_state_init(&g_cfr_holdem_indexer, &state);
    idx = hand_index_next_round(&g_cfr_holdem_indexer, cards, &state);

    if (street >= 1)
    {
        idx = hand_index_next_round(&g_cfr_holdem_indexer, cards + 2, &state);
    }
    if (street >= 2)
    {
        idx = hand_index_next_round(&g_cfr_holdem_indexer, cards + 5, &state);
    }
    if (street >= 3)
    {
        idx = hand_index_next_round(&g_cfr_holdem_indexer, cards + 6, &state);
    }

    *out_index = (uint64_t)idx;
    return 1;
}

static uint64_t cfr_hand_index_size_for_street(int street)
{
    if (street < 0 || street > 3)
    {
        return 0ULL;
    }
    if (!cfr_hand_index_init())
    {
        return 0ULL;
    }
    return (uint64_t)hand_indexer_size(&g_cfr_holdem_indexer, (uint_fast32_t)street);
}

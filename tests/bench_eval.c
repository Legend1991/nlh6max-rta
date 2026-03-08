#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CFR_TEST 1
#include "../src/main.c"

static uint64_t bench_rng_next(uint64_t *state)
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

static int bench_rng_int(uint64_t *state, int bound)
{
    if (bound <= 0)
    {
        return 0;
    }
    return (int)(bench_rng_next(state) % (uint64_t)bound);
}

static double bench_now_seconds(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;

    if (freq.QuadPart == 0)
    {
        QueryPerformanceFrequency(&freq);
    }

    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}

static void bench_generate_hands(int *hands, int hand_count, uint64_t seed)
{
    int i;
    uint64_t rng;

    rng = seed;

    for (i = 0; i < hand_count; ++i)
    {
        int deck[52];
        int j;

        for (j = 0; j < 52; ++j)
        {
            deck[j] = j;
        }

        for (j = 0; j < 7; ++j)
        {
            int k;
            int tmp;

            k = j + bench_rng_int(&rng, 52 - j);
            tmp = deck[j];
            deck[j] = deck[k];
            deck[k] = tmp;

            hands[i * 7 + j] = deck[j];
        }
    }
}

static double bench_cfr_wrapper(const int *hands, int hand_count, uint64_t *checksum)
{
    double t0;
    double t1;
    volatile uint64_t sum;
    int i;

    sum = 0ULL;
    t0 = bench_now_seconds();
    for (i = 0; i < hand_count; ++i)
    {
        const int *h;
        uint32_t score;

        h = &hands[i * 7];
        score = cfr_eval_best_hand(h);
        sum += (uint64_t)score;
    }
    t1 = bench_now_seconds();

    *checksum = (uint64_t)sum;
    return t1 - t0;
}

static double bench_phe_direct(const int *hands, int hand_count, uint64_t *checksum)
{
    double t0;
    double t1;
    volatile uint64_t sum;
    int i;

    sum = 0ULL;
    t0 = bench_now_seconds();
    for (i = 0; i < hand_count; ++i)
    {
        const int *h;
        int rank;

        h = &hands[i * 7];
        rank = evaluate_7cards(h[0], h[1], h[2], h[3], h[4], h[5], h[6]);
        sum += (uint64_t)rank;
    }
    t1 = bench_now_seconds();

    *checksum = (uint64_t)sum;
    return t1 - t0;
}

int main(int argc, char **argv)
{
    int hand_count;
    int *hands;
    uint64_t sum_wrapper;
    uint64_t sum_direct;
    double sec_wrapper;
    double sec_direct;
    double mps_wrapper;
    double mps_direct;

    hand_count = 1000000;
    if (argc >= 2)
    {
        int parsed;
        parsed = atoi(argv[1]);
        if (parsed > 1000)
        {
            hand_count = parsed;
        }
    }

    hands = (int *)malloc((size_t)hand_count * 7u * sizeof(int));
    if (hands == NULL)
    {
        fprintf(stderr, "Allocation failed for %d hands\n", hand_count);
        return 1;
    }

    printf("Generating %d random 7-card hands...\n", hand_count);
    bench_generate_hands(hands, hand_count, 0x123456789ABCDEF0ULL);

    printf("Running CFR showdown wrapper benchmark...\n");
    sec_wrapper = bench_cfr_wrapper(hands, hand_count, &sum_wrapper);

    printf("Running direct PHE benchmark...\n");
    sec_direct = bench_phe_direct(hands, hand_count, &sum_direct);

    mps_wrapper = (double)hand_count / sec_wrapper / 1000000.0;
    mps_direct = (double)hand_count / sec_direct / 1000000.0;

    printf("\n=== Benchmark Results ===\n");
    printf("Hands: %d\n", hand_count);
    printf("CFR wrapper (uses PHE) : %.3f s, %.2f M hands/s, checksum=%llu\n", sec_wrapper, mps_wrapper, (unsigned long long)sum_wrapper);
    printf("Direct evaluate_7cards : %.3f s, %.2f M hands/s, checksum=%llu\n", sec_direct, mps_direct, (unsigned long long)sum_direct);
    printf("Wrapper overhead ratio : %.3fx\n", sec_wrapper / sec_direct);

    free(hands);
    return 0;
}

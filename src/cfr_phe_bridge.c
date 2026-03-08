/*
 * HenryRLee/PokerHandEvaluator integration.
 *
 * CFR cards are encoded as rank * 4 + suit (same as PHE), so there is
 * no conversion overhead during showdown evaluation.
 */

#include "../third_party/PokerHandEvaluator/cpp/src/tables_bitwise.c"
#include "../third_party/PokerHandEvaluator/cpp/src/hashtable.c"
#include "../third_party/PokerHandEvaluator/cpp/src/hashtable7.c"
#include "../third_party/PokerHandEvaluator/cpp/src/dptables.c"
#include "../third_party/PokerHandEvaluator/cpp/src/hash.c"
#include "../third_party/PokerHandEvaluator/cpp/src/evaluator7.c"

static uint32_t cfr_eval_best_hand(const int cards[7])
{
    /*
     * Raw PHE ranking: 1 strongest .. 7462 weakest.
     */
    return (uint32_t)evaluate_7cards(cards[0], cards[1], cards[2], cards[3], cards[4], cards[5], cards[6]);
}

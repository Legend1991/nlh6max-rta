static int cfr_bucket_amount(int v)
{
    if (v <= 2) return 0;
    if (v <= 4) return 1;
    if (v <= 8) return 2;
    if (v <= 16) return 3;
    if (v <= 32) return 4;
    if (v <= 64) return 5;
    if (v <= 128) return 6;
    if (v <= 256) return 7;
    return 8;
}

static int cfr_extract_infoset_fields(const CFRHandState *st, int player, CFRInfoKeyFields *out)
{
    int to_call_player;
    uint64_t hand_index;
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
                                  &hand_index))
    {
        return 0;
    }

    to_call_player = st->to_call - st->committed_street[player];
    if (to_call_player < 0)
    {
        to_call_player = 0;
    }

    out->street = st->street;
    out->position = (player - st->dealer + CFR_MAX_PLAYERS) % CFR_MAX_PLAYERS;
    hand_bucket = cfr_abstraction_bucket_for_state(st->street,
                                                   st->hole[player][0],
                                                   st->hole[player][1],
                                                   st->board,
                                                   st->board_count,
                                                   hand_index,
                                                   CFR_ABS_MODE_BLUEPRINT);
    out->hand_index = hand_bucket;
    out->pot_bucket = cfr_bucket_amount(st->pot);
    out->to_call_bucket = cfr_bucket_amount(to_call_player);
    out->active_players = cfr_count_in_hand(st);
    out->history_hash = cfr_history_key_from_state(st);
    return 1;
}

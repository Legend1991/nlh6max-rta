static const char *cfr_action_name(int action, int to_call)
{
    if (action == CFR_ACT_FOLD) return "fold";
    if (action == CFR_ACT_CALL_CHECK) return (to_call > 0) ? "call" : "check";
    if (action == CFR_ACT_BET_HALF) return "raise";
    if (action == CFR_ACT_BET_POT) return "raise";
    if (action == CFR_ACT_RAISE_TO) return "raise";
    return "all_in";
}

static int cfr_parse_history_text(const char *text, unsigned char *out, int max_len)
{
    char buf[256];
    char *token;
    char *ctx;
    int n;

    if (text == NULL || text[0] == '\0')
    {
        return 0;
    }

    if (strlen(text) >= sizeof(buf))
    {
        return -1;
    }

    strcpy(buf, text);
    n = 0;

#ifdef _MSC_VER
    token = strtok_s(buf, ", ;\t", &ctx);
#else
    token = strtok(buf, ", ;\t");
#endif

    while (token != NULL)
    {
        char c;

        c = (char)tolower((unsigned char)token[0]);
        if (n >= max_len)
        {
            return -1;
        }

        if (c == '/' || c == '|')
        {
            out[n++] = 240;
        }
        else if (c == 'f')
        {
            out[n++] = (unsigned char)CFR_ACT_FOLD;
        }
        else if (c == 'c' || c == 'k')
        {
            out[n++] = (unsigned char)CFR_ACT_CALL_CHECK;
        }
        else if (c == 'h')
        {
            out[n++] = (unsigned char)CFR_ACT_RAISE_TO;
        }
        else if (c == 'p')
        {
            out[n++] = (unsigned char)CFR_ACT_RAISE_TO;
        }
        else if (c == 'a')
        {
            out[n++] = (unsigned char)CFR_ACT_ALL_IN;
        }
        else if (c == 'r')
        {
            int bucket;
            char *endptr;

            if (token[1] == '\0')
            {
                out[n++] = (unsigned char)CFR_ACT_RAISE_TO;
            }
            else
            {
                if (n + 2 >= max_len)
                {
                    return -1;
                }
                bucket = (int)strtol(token + 1, &endptr, 10);
                if (endptr == token + 1 || *endptr != '\0')
                {
                    return -1;
                }
                if (bucket < 0) bucket = 0;
                if (bucket > 255) bucket = 255;
                out[n++] = (unsigned char)CFR_ACT_RAISE_TO;
                out[n++] = 250;
                out[n++] = (unsigned char)bucket;
            }
        }
        else
        {
            return -1;
        }

#ifdef _MSC_VER
        token = strtok_s(NULL, ", ;\t", &ctx);
#else
        token = strtok(NULL, ", ;\t");
#endif
    }

    return n;
}

static int cfr_parse_street_text(const char *text)
{
    if (text == NULL || text[0] == '\0') return -1;
    if (_stricmp(text, "preflop") == 0) return 0;
    if (_stricmp(text, "flop") == 0) return 1;
    if (_stricmp(text, "turn") == 0) return 2;
    if (_stricmp(text, "river") == 0) return 3;
    return -1;
}

static int cfr_derive_street_from_board_count(int board_count)
{
    if (board_count == 0) return 0;
    if (board_count == 3) return 1;
    if (board_count == 4) return 2;
    if (board_count == 5) return 3;
    return -1;
}

static void cfr_print_usage(const char *exe)
{
    printf("Usage:\n");
    printf("  %s train [options]\n", exe);
    printf("  %s query [options]\n", exe);
    printf("  %s search [options]\n", exe);
    printf("  %s search-server [options]\n", exe);
    printf("  %s match [options]\n", exe);
    printf("  %s bench [options]\n", exe);
    printf("  %s abstraction-build [options]\n", exe);
    printf("  %s finalize-blueprint [options]\n", exe);
    printf("\n");
    printf("Train options:\n");
    printf("  --out <file>            Output blueprint path (default: data\\\\blueprint.bin)\n");
    printf("  --abstraction <file>    Abstraction config file (default: built-in Pluribus-like)\n");
    printf("  --resume <file>         Resume from existing blueprint file\n");
    printf("  --resume-ignore-compat  Ignore stored solver compatibility hash mismatch on resume\n");
    printf("  --iters <N>             Additional CFR+ iterations to run\n");
    printf("  --seconds <N>           Training wall-clock limit in seconds\n");
    printf("  --dump-iters <N>        Periodic checkpoint every N iterations\n");
    printf("  --dump-seconds <N>      Periodic checkpoint every N seconds\n");
    printf("  --snapshot-iters <N>    Periodic snapshot cadence for archive/finalize pipeline\n");
    printf("  --snapshot-seconds <N>  Periodic snapshot cadence in seconds\n");
    printf("  --snapshot-dir <dir>    Snapshot output directory (default: data\\\\snapshots)\n");
    printf("  --strict-time-phases    Enable wall-clock phase scheduler\n");
    printf("  --no-strict-time-phases Disable wall-clock phase scheduler\n");
    printf("  --discount-stop-seconds <N> Disable linear discount at elapsed second N\n");
    printf("  --prune-start-seconds <N> Enable pruning at elapsed second N\n");
    printf("  --discount-every-seconds <N> Apply linear discount event every N seconds\n");
    printf("  --warmup-seconds <N>    Warmup boundary for profile-gated averaging/snapshots\n");
    printf("  --snapshot-start-seconds <N> Enable snapshots at elapsed second N\n");
    printf("  --avg-start-seconds <N> Enable preflop average accumulation at elapsed second N\n");
    printf("  --preflop-avg           Enable preflop average accumulation (default on)\n");
    printf("  --no-preflop-avg        Disable preflop average accumulation\n");
    printf("  --preflop-avg-mode <m>  full|sampled preflop averaging updates (default: full)\n");
    printf("  --async-checkpoint      Enable async checkpoint writer (Windows)\n");
    printf("  --no-async-checkpoint   Disable async checkpoint writer\n");
    printf("  --status-iters <N>      Status print every N iterations\n");
    printf("  --threads <N>           Worker threads for blueprint training\n");
    printf("  --parallel-mode <name>  deterministic|sharded (default: deterministic)\n");
    printf("  --chunk-iters <N>       Iteration chunk size per scheduling slice\n");
    printf("  --samples-per-player <N> Traversals per player per iteration\n");
    printf("  --strategy-interval <N> Accumulate average strategy every N iterations\n");
    printf("  --linear-discount-iters <N> Apply periodic linear discount every N iters (0 disables)\n");
    printf("  --linear-discount-stop-iters <N> Stop discounting after iteration N (0 disables)\n");
    printf("  --linear-discount-scale <X> Linear discount scale (default: 1.0)\n");
    printf("  --prune-start <N>       Start negative-regret pruning at iteration N\n");
    printf("  --prune-full-iters <N>  Force full (no-prune) traversal every N iterations after prune start\n");
    printf("  --prune-threshold <X>   Prune actions with regret <= X\n");
    printf("  --prune-prob <X>        Prune probability in [0,1]\n");
    printf("  --int-regret            Quantize regrets to int32 (Pluribus-like memory mode)\n");
    printf("  --float-regret          Keep regrets in float mode\n");
    printf("  --regret-floor <N>      Minimum regret value (must be <= 0)\n");
    printf("  --pluribus-profile      Apply Pluribus-like trainer defaults\n");
    printf("  --no-prune              Disable pruning\n");
    printf("  --no-linear-discount    Disable periodic linear discount\n");
    printf("  --seed <N>              RNG seed (new training only)\n");
    printf("\n");
    printf("Finalize-blueprint options:\n");
    printf("  --raw <file>            Raw training blueprint file (required)\n");
    printf("  --out <file>            Finalized blueprint output path (default only when --runtime-out omitted)\n");
    printf("  --runtime-out <file>    Optional runtime-serving artifact output path; if used alone, skips full finalized output\n");
    printf("  --runtime-quant <mode>  u16|u8 runtime policy quantization (default: u16)\n");
    printf("  --runtime-shards <N>    Approximate runtime shard count (default: 256)\n");
    printf("  --snapshot-dir <dir>    Snapshot directory to average postflop current strategy\n");
    printf("  --snapshot-min-seconds <N> Use only snapshots with elapsed_train_seconds >= N\n");
    printf("  --abstraction <file>    Abstraction config file used by blueprint/snapshots\n");
    printf("  --ignore-abstraction-compat  Ignore abstraction hash mismatch\n");
    printf("\n");
    printf("Bench options:\n");
    printf("  --iters <N>             Iterations per thread-count run (default: 200)\n");
    printf("  --max-threads <N>       Max thread count from set {1,2,4,8,16,24,32}\n");
    printf("  --samples-per-player <N> Traversals per player per iteration (default: 1)\n");
    printf("  --parallel-mode <name>  deterministic|sharded (default: deterministic)\n");
    printf("  --chunk-iters <N>       Iteration chunk size (default: 256)\n");
    printf("  --seed <N>              Benchmark seed (default: 123456789)\n");
    printf("  --abstraction <file>    Abstraction file used by benchmark runs\n");
    printf("  --csv <file>            Write benchmark results as CSV\n");
    printf("  --json <file>           Write benchmark results as JSON\n");
    printf("\n");
    printf("Query options:\n");
    printf("  --blueprint <file>      Blueprint file path\n");
    printf("  --abstraction <file>    Abstraction config file used by blueprint\n");
    printf("  --ignore-abstraction-compat  Ignore blueprint abstraction hash mismatch\n");
    printf("  --hole <cards>          Two cards, e.g. AsKd\n");
    printf("  --board <cards>         Board cards (0/3/4/5 cards), e.g. AhKs2d (optional)\n");
    printf("  --history <tokens>      Action tokens, e.g. \"c h / c\" (optional)\n");
    printf("  --street <name>         preflop|flop|turn|river (optional)\n");
    printf("  --player-seat <N>       Seat index 0..5 (default: 0)\n");
    printf("  --dealer-seat <N>       Dealer index 0..5 (default: 5)\n");
    printf("  --active <N>            Active players count (default: 6)\n");
    printf("  --pot <N>               Pot size (default: 3)\n");
    printf("  --to-call <N>           Chips to call (default: 0)\n");
    printf("  --stack <N>             Effective stack (default: 200)\n");
    printf("  --raises <N>            Raises on current street (default: 0)\n");
    printf("                          history token extras: r or r<bucket> for raise markers\n");
    printf("\n");
    printf("Search options:\n");
    printf("  --blueprint <file>      Full finalized blueprint file path\n");
    printf("  --runtime-blueprint <file> Disk-backed runtime artifact path\n");
    printf("  --abstraction <file>    Abstraction file used by blueprint\n");
    printf("  --ignore-abstraction-compat  Ignore abstraction hash mismatch\n");
    printf("  --hole <cards>          Two cards, e.g. AsKd (required)\n");
    printf("  --board <cards>         Board cards (0/3/4/5 cards)\n");
    printf("  --history <tokens>      Action history tokens\n");
    printf("  --street <name>         preflop|flop|turn|river (optional)\n");
    printf("  --player-seat <N>       Seat index 0..5 (default: 0)\n");
    printf("  --dealer-seat <N>       Dealer seat 0..5 (default: 5)\n");
    printf("  --active <N>            Active players count (default: 6)\n");
    printf("  --pot <N>               Pot size (default: 3)\n");
    printf("  --to-call <N>           Chips to call (default: 0)\n");
    printf("  --stack <N>             Effective stack (default: 200)\n");
    printf("  --raises <N>            Raises on current street (default: 0)\n");
    printf("  --iters <N>             Subgame solve iterations (default: 400)\n");
    printf("  --time-ms <N>           Search solve time budget in milliseconds (0 disables)\n");
    printf("  --depth <N>             Subgame depth horizon (default: 3)\n");
    printf("  --threads <N>           Search worker threads (default: hw threads)\n");
    printf("  --search-pick <mode>    sample-final|argmax (default: sample-final)\n");
    printf("  --offtree-mode <mode>   inject|translate (default: inject)\n");
    printf("  --cache-bytes <N>       Runtime artifact decoded-policy cache bytes (default: 67108864)\n");
    printf("  --prefetch <mode>       none|auto|preflop runtime warmup mode (default: auto)\n");
    printf("  --seed <N>              Search seed (default: 20260227)\n");
    printf("\n");
    printf("Search-server options:\n");
    printf("  Same as search options, but only model/load options are applied at startup.\n");
    printf("  After startup it keeps the selected policy provider loaded and reads query lines from stdin.\n");
    printf("  Query line example: --hole AsKd --board AhKs2d --history c,r2,/,c --pot 8 --to-call 2 --time-ms 2000\n");
    printf("  Commands: defaults | set-default <opts> | stats | help | quit\n");
    printf("\n");
    printf("Match options:\n");
    printf("  --a <file>              Policy A blueprint (required)\n");
    printf("  --b <file>              Policy B blueprint (required)\n");
    printf("  --abstraction <file>    Abstraction file used by blueprints\n");
    printf("  --ignore-abstraction-compat  Ignore abstraction hash mismatch\n");
    printf("  --hands <N>             Number of self-play hands (default: 1000)\n");
    printf("  --mode <name>           blueprint|search (default: blueprint)\n");
    printf("  --use-search            Alias for --mode search\n");
    printf("  --runtime-profile <m>   none|pluribus (default: none)\n");
    printf("  --search-iters <N>      Per-decision search iterations in search mode\n");
    printf("  --search-time-ms <N>    Per-decision search time budget in milliseconds (0 disables)\n");
    printf("  --search-depth <N>      Per-decision search depth guard in search mode\n");
    printf("  --search-threads <N>    Per-decision search worker threads in search mode\n");
    printf("  --search-pick <mode>    sample-final|argmax (default: sample-final)\n");
    printf("  --offtree-mode <mode>   inject|translate (default: inject)\n");
    printf("  --seed <N>              Match seed (default: 20260227)\n");
    printf("\n");
    printf("Abstraction-build options:\n");
    printf("  --out <file>            Output abstraction path (default: data\\\\abstraction.bin)\n");
    printf("  --seed <N>              Deterministic abstraction seed\n");
    printf("  --bp-flop <N>           Blueprint flop bucket count (default: 200)\n");
    printf("  --bp-turn <N>           Blueprint turn bucket count (default: 200)\n");
    printf("  --bp-river <N>          Blueprint river bucket count (default: 200)\n");
    printf("  --search-flop <N>       Search flop bucket count (default: 500)\n");
    printf("  --search-turn <N>       Search turn bucket count (default: 500)\n");
    printf("  --search-river <N>      Search river bucket count (default: 500)\n");
    printf("  --mc-samples <N>        Monte Carlo samples per feature eval (default: 12)\n");
    printf("  --samples <N>           Build samples per postflop street (default: 8000)\n");
    printf("  --kmeans-iters <N>      K-means iterations (default: 24)\n");
    printf("  --cluster-algo <name>   legacy|emd-kmedoids (default: emd-kmedoids)\n");
    printf("  --no-centroids          Disable centroid build and use legacy hash fallback\n");
}


#include "core.c"
#include "cfr_phe_bridge.c"
#include "cfr_isomorphism.c"
#include "abstraction_config.c"
#include "poker_state.c"
#include "cfr_abstraction.c"
#include "range_engine.c"
#include "runtime_blueprint.c"
#include "search_engine.c"
#include "cfr_trainer.c"
#include "cli_utils.c"
#include "commands.c"

#ifndef CFR_TEST
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        cfr_print_usage(argv[0]);
        return 1;
    }

    if (_stricmp(argv[1], "train") == 0)
    {
        return cfr_cmd_train(argc - 2, argv + 2);
    }

    if (_stricmp(argv[1], "query") == 0)
    {
        return cfr_cmd_query(argc - 2, argv + 2);
    }

    if (_stricmp(argv[1], "bench") == 0)
    {
        return cfr_cmd_bench(argc - 2, argv + 2);
    }

    if (_stricmp(argv[1], "search") == 0)
    {
        return cfr_cmd_search(argc - 2, argv + 2);
    }

    if (_stricmp(argv[1], "search-server") == 0)
    {
        return cfr_cmd_search_server(argc - 2, argv + 2);
    }

    if (_stricmp(argv[1], "match") == 0)
    {
        return cfr_cmd_match(argc - 2, argv + 2);
    }

    if (_stricmp(argv[1], "abstraction-build") == 0)
    {
        return cfr_cmd_abstraction_build(argc - 2, argv + 2);
    }

    if (_stricmp(argv[1], "finalize-blueprint") == 0)
    {
        return cfr_cmd_finalize_blueprint(argc - 2, argv + 2);
    }

    cfr_print_usage(argv[0]);
    return 1;
}
#endif

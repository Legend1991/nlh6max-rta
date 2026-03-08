typedef struct
{
    uint64_t key;
    uint32_t node_index;
    uint32_t shard_id;
    uint64_t node_offset;
} CFRRuntimeBuildEntry;

static uint32_t cfr_runtime_hash32_mix_u32(uint32_t h, uint32_t v)
{
    h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2);
    return h;
}

static uint32_t cfr_runtime_hash32_mix_u64(uint32_t h, uint64_t v)
{
    h = cfr_runtime_hash32_mix_u32(h, (uint32_t)(v & 0xFFFFFFFFu));
    h = cfr_runtime_hash32_mix_u32(h, (uint32_t)(v >> 32));
    return h;
}

static uint64_t cfr_runtime_mix_u64(uint64_t x)
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

static uint32_t cfr_runtime_next_pow2_u32(uint32_t v)
{
    uint32_t p;

    if (v <= 1u)
    {
        return 1u;
    }
    p = 1u;
    while (p < v && p < 0x80000000u)
    {
        p <<= 1;
    }
    return p;
}

static uint32_t cfr_runtime_buckets_per_street(uint32_t requested_total_shards)
{
    uint32_t per_street;

    if (requested_total_shards == 0u)
    {
        requested_total_shards = 256u;
    }
    per_street = (requested_total_shards + 3u) / 4u;
    return cfr_runtime_next_pow2_u32(per_street);
}

static uint32_t cfr_runtime_shard_for_key(uint64_t key, int street_hint, uint32_t buckets_per_street)
{
    uint32_t bucket;
    uint32_t street;

    if (buckets_per_street == 0u)
    {
        return 0u;
    }
    street = (street_hint >= 0 && street_hint <= 3) ? (uint32_t)street_hint : 0u;
    bucket = (uint32_t)(cfr_runtime_mix_u64(key) & (uint64_t)(buckets_per_street - 1u));
    return street * buckets_per_street + bucket;
}

static int cfr_runtime_build_entry_compare(const void *a, const void *b)
{
    const CFRRuntimeBuildEntry *ea;
    const CFRRuntimeBuildEntry *eb;

    ea = (const CFRRuntimeBuildEntry *)a;
    eb = (const CFRRuntimeBuildEntry *)b;
    if (ea->key < eb->key)
    {
        return -1;
    }
    if (ea->key > eb->key)
    {
        return 1;
    }
    if (ea->node_index < eb->node_index)
    {
        return -1;
    }
    if (ea->node_index > eb->node_index)
    {
        return 1;
    }
    return 0;
}

static uint32_t cfr_runtime_quant_scale(int quant_mode)
{
    return (quant_mode == CFR_RUNTIME_QUANT_U8) ? 255u : 65535u;
}

static size_t cfr_runtime_quant_bytes(int quant_mode)
{
    return (quant_mode == CFR_RUNTIME_QUANT_U8) ? sizeof(uint8_t) : sizeof(uint16_t);
}

static uint64_t cfr_runtime_node_bytes(int action_count, int quant_mode)
{
    return (uint64_t)sizeof(CFRRuntimeNodeHeader) +
           ((uint64_t)action_count * (uint64_t)cfr_runtime_quant_bytes(quant_mode));
}

static void cfr_runtime_fill_uniform_policy(float *policy, int action_count)
{
    int i;
    float p;

    if (policy == NULL || action_count <= 0)
    {
        return;
    }
    p = 1.0f / (float)action_count;
    for (i = 0; i < action_count; ++i)
    {
        policy[i] = p;
    }
}

static void cfr_runtime_quantize_policy(const float *policy,
                                        int action_count,
                                        int quant_mode,
                                        uint16_t out_u16[CFR_MAX_ACTIONS],
                                        uint8_t out_u8[CFR_MAX_ACTIONS])
{
    uint32_t base[CFR_MAX_ACTIONS];
    float frac[CFR_MAX_ACTIONS];
    uint32_t total_scale;
    uint32_t assigned;
    int i;
    int pick;

    if (policy == NULL || action_count <= 0)
    {
        return;
    }

    total_scale = cfr_runtime_quant_scale(quant_mode);
    assigned = 0u;
    for (i = 0; i < action_count; ++i)
    {
        float clamped;
        float scaled;
        uint32_t q;

        clamped = policy[i];
        if (clamped < 0.0f)
        {
            clamped = 0.0f;
        }
        scaled = clamped * (float)total_scale;
        if (scaled < 0.0f)
        {
            scaled = 0.0f;
        }
        q = (uint32_t)scaled;
        base[i] = q;
        frac[i] = scaled - (float)q;
        assigned += q;
    }

    while (assigned < total_scale)
    {
        pick = 0;
        for (i = 1; i < action_count; ++i)
        {
            if (frac[i] > frac[pick] + 1e-9f)
            {
                pick = i;
            }
        }
        base[pick]++;
        frac[pick] = -1.0f;
        assigned++;
    }

    for (i = 0; i < action_count; ++i)
    {
        if (quant_mode == CFR_RUNTIME_QUANT_U8)
        {
            out_u8[i] = (uint8_t)((base[i] > 255u) ? 255u : base[i]);
        }
        else
        {
            out_u16[i] = (uint16_t)((base[i] > 65535u) ? 65535u : base[i]);
        }
    }
}

static void cfr_runtime_decode_policy(const CFRRuntimeNodeHeader *node_hdr,
                                      const unsigned char *payload,
                                      int action_count,
                                      float *out_policy)
{
    uint32_t sum;
    int i;

    if (node_hdr == NULL || payload == NULL || out_policy == NULL || action_count <= 0)
    {
        return;
    }

    sum = 0u;
    if (node_hdr->quant_mode == CFR_RUNTIME_QUANT_U8)
    {
        const uint8_t *q;
        q = (const uint8_t *)payload;
        for (i = 0; i < action_count; ++i)
        {
            sum += (uint32_t)q[i];
        }
        if (sum == 0u)
        {
            cfr_runtime_fill_uniform_policy(out_policy, action_count);
            return;
        }
        for (i = 0; i < action_count; ++i)
        {
            out_policy[i] = (float)q[i] / (float)sum;
        }
    }
    else
    {
        const uint16_t *q;
        q = (const uint16_t *)payload;
        for (i = 0; i < action_count; ++i)
        {
            sum += (uint32_t)q[i];
        }
        if (sum == 0u)
        {
            cfr_runtime_fill_uniform_policy(out_policy, action_count);
            return;
        }
        for (i = 0; i < action_count; ++i)
        {
            out_policy[i] = (float)q[i] / (float)sum;
        }
    }
}

static int cfr_runtime_blueprint_save(const CFRBlueprint *bp,
                                      const char *path,
                                      int quant_mode,
                                      uint32_t requested_total_shards)
{
    CFRRuntimeBlueprintFileHeader hdr;
    CFRRuntimeShardHeader *shards;
    CFRRuntimeBuildEntry *entries;
    uint32_t *counts;
    uint32_t *cursor;
    uint32_t used_nodes;
    uint32_t buckets_per_street;
    uint32_t total_shards;
    uint64_t shard_table_offset;
    uint64_t index_region_offset;
    uint64_t data_region_offset;
    uint64_t cur_data_offset;
    uint32_t content_hash32;
    FILE *fp;
    uint32_t node_pos;
    uint32_t i;

    if (bp == NULL || path == NULL)
    {
        return 0;
    }
    if (quant_mode != CFR_RUNTIME_QUANT_U8)
    {
        quant_mode = CFR_RUNTIME_QUANT_U16;
    }

    buckets_per_street = cfr_runtime_buckets_per_street(requested_total_shards);
    total_shards = buckets_per_street * 4u;
    counts = (uint32_t *)calloc((size_t)total_shards, sizeof(uint32_t));
    cursor = (uint32_t *)calloc((size_t)total_shards, sizeof(uint32_t));
    shards = (CFRRuntimeShardHeader *)calloc((size_t)total_shards, sizeof(CFRRuntimeShardHeader));
    if (counts == NULL || cursor == NULL || shards == NULL)
    {
        free(counts);
        free(cursor);
        free(shards);
        return 0;
    }

    used_nodes = 0u;
    for (i = 0; i < bp->used_node_count; ++i)
    {
        const CFRNode *n;
        uint32_t shard_id;
        int street_hint;

        n = &bp->nodes[i];
        if (!cfr_node_is_used(n) || n->action_count <= 0)
        {
            continue;
        }
        street_hint = (n->street_hint <= 3u) ? (int)n->street_hint : 0;
        shard_id = cfr_runtime_shard_for_key(n->key, street_hint, buckets_per_street);
        counts[shard_id]++;
        used_nodes++;
    }

    entries = (CFRRuntimeBuildEntry *)calloc((size_t)used_nodes, sizeof(CFRRuntimeBuildEntry));
    if (entries == NULL)
    {
        free(counts);
        free(cursor);
        free(shards);
        return 0;
    }

    cursor[0] = 0u;
    for (i = 1; i < total_shards; ++i)
    {
        cursor[i] = cursor[i - 1] + counts[i - 1];
    }

    node_pos = 0u;
    for (i = 0; i < bp->used_node_count; ++i)
    {
        const CFRNode *n;
        uint32_t shard_id;
        uint32_t pos;
        int street_hint;

        n = &bp->nodes[i];
        if (!cfr_node_is_used(n) || n->action_count <= 0)
        {
            continue;
        }
        street_hint = (n->street_hint <= 3u) ? (int)n->street_hint : 0;
        shard_id = cfr_runtime_shard_for_key(n->key, street_hint, buckets_per_street);
        pos = cursor[shard_id]++;
        entries[pos].key = n->key;
        entries[pos].node_index = i;
        entries[pos].shard_id = shard_id;
        node_pos++;
    }

    if (node_pos != used_nodes)
    {
        free(entries);
        free(counts);
        free(cursor);
        free(shards);
        return 0;
    }

    cursor[0] = 0u;
    for (i = 1; i < total_shards; ++i)
    {
        cursor[i] = cursor[i - 1] + counts[i - 1];
    }
    for (i = 0; i < total_shards; ++i)
    {
        if (counts[i] > 1u)
        {
            qsort(entries + cursor[i], (size_t)counts[i], sizeof(entries[0]), cfr_runtime_build_entry_compare);
        }
    }

    shard_table_offset = (uint64_t)sizeof(hdr);
    index_region_offset = shard_table_offset + ((uint64_t)total_shards * (uint64_t)sizeof(CFRRuntimeShardHeader));
    data_region_offset = index_region_offset + ((uint64_t)used_nodes * (uint64_t)sizeof(CFRRuntimeIndexEntry));
    cur_data_offset = data_region_offset;
    for (i = 0; i < total_shards; ++i)
    {
        uint32_t start;
        uint32_t j;

        shards[i].index_offset = index_region_offset + ((uint64_t)cursor[i] * (uint64_t)sizeof(CFRRuntimeIndexEntry));
        shards[i].index_count = counts[i];
        shards[i].street_hint = (uint8_t)(i / buckets_per_street);
        shards[i].bucket_hint = (uint8_t)(i % buckets_per_street);
        start = cursor[i];
        for (j = 0; j < counts[i]; ++j)
        {
            const CFRNode *n;
            entries[start + j].node_offset = cur_data_offset;
            n = &bp->nodes[entries[start + j].node_index];
            cur_data_offset += cfr_runtime_node_bytes(n->action_count, quant_mode);
        }
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = CFR_RUNTIME_BLUEPRINT_MAGIC;
    hdr.version = CFR_RUNTIME_BLUEPRINT_VERSION;
    hdr.quant_mode = (uint32_t)quant_mode;
    hdr.node_count = used_nodes;
    hdr.total_shards = total_shards;
    hdr.buckets_per_street = buckets_per_street;
    hdr.compat_hash32 = bp->compat_hash32;
    hdr.abstraction_hash32 = bp->abstraction_hash32;
    hdr.shard_table_offset = shard_table_offset;
    hdr.index_region_offset = index_region_offset;
    hdr.data_region_offset = data_region_offset;
    content_hash32 = 0x811C9DC5u;

    fp = fopen(path, "wb");
    if (fp == NULL)
    {
        free(entries);
        free(counts);
        free(cursor);
        free(shards);
        return 0;
    }

    if (fwrite(&hdr, sizeof(hdr), 1, fp) != 1 ||
        fwrite(shards, sizeof(shards[0]), (size_t)total_shards, fp) != (size_t)total_shards)
    {
        fclose(fp);
        free(entries);
        free(counts);
        free(cursor);
        free(shards);
        return 0;
    }

    for (i = 0; i < used_nodes; ++i)
    {
        CFRRuntimeIndexEntry idx;

        idx.key = entries[i].key;
        idx.node_offset = entries[i].node_offset;
        content_hash32 = cfr_runtime_hash32_mix_u64(content_hash32, idx.key);
        content_hash32 = cfr_runtime_hash32_mix_u64(content_hash32, idx.node_offset);
        if (fwrite(&idx, sizeof(idx), 1, fp) != 1)
        {
            fclose(fp);
            free(entries);
            free(counts);
            free(cursor);
            free(shards);
            return 0;
        }
    }

    for (i = 0; i < used_nodes; ++i)
    {
        const CFRNode *n;
        CFRRuntimeNodeHeader node_hdr;
        float policy[CFR_MAX_ACTIONS];
        uint16_t q16[CFR_MAX_ACTIONS];
        uint8_t q8[CFR_MAX_ACTIONS];

        n = &bp->nodes[entries[i].node_index];
        memset(&node_hdr, 0, sizeof(node_hdr));
        node_hdr.action_count = (uint32_t)n->action_count;
        node_hdr.street_hint = (n->street_hint <= 3u) ? n->street_hint : 0u;
        node_hdr.quant_mode = (uint8_t)quant_mode;
        cfr_compute_average_strategy_n(n, n->action_count, policy);
        memset(q16, 0, sizeof(q16));
        memset(q8, 0, sizeof(q8));
        cfr_runtime_quantize_policy(policy, n->action_count, quant_mode, q16, q8);

        content_hash32 = cfr_runtime_hash32_mix_u32(content_hash32, node_hdr.action_count);
        content_hash32 = cfr_runtime_hash32_mix_u32(content_hash32, (uint32_t)node_hdr.street_hint);
        if (fwrite(&node_hdr, sizeof(node_hdr), 1, fp) != 1)
        {
            fclose(fp);
            free(entries);
            free(counts);
            free(cursor);
            free(shards);
            return 0;
        }
        if (quant_mode == CFR_RUNTIME_QUANT_U8)
        {
            int j;
            for (j = 0; j < n->action_count; ++j)
            {
                content_hash32 = cfr_runtime_hash32_mix_u32(content_hash32, (uint32_t)q8[j]);
            }
            if (fwrite(q8, sizeof(uint8_t), (size_t)n->action_count, fp) != (size_t)n->action_count)
            {
                fclose(fp);
                free(entries);
                free(counts);
                free(cursor);
                free(shards);
                return 0;
            }
        }
        else
        {
            int j;
            for (j = 0; j < n->action_count; ++j)
            {
                content_hash32 = cfr_runtime_hash32_mix_u32(content_hash32, (uint32_t)q16[j]);
            }
            if (fwrite(q16, sizeof(uint16_t), (size_t)n->action_count, fp) != (size_t)n->action_count)
            {
                fclose(fp);
                free(entries);
                free(counts);
                free(cursor);
                free(shards);
                return 0;
            }
        }
    }

    hdr.content_hash32 = content_hash32;
#ifdef _MSC_VER
    _fseeki64(fp, 0, SEEK_SET);
#else
    fseeko(fp, 0, SEEK_SET);
#endif
    if (fwrite(&hdr, sizeof(hdr), 1, fp) != 1)
    {
        fclose(fp);
        free(entries);
        free(counts);
        free(cursor);
        free(shards);
        return 0;
    }

    fclose(fp);
    free(entries);
    free(counts);
    free(cursor);
    free(shards);
    return 1;
}

static void cfr_runtime_blueprint_close(CFRRuntimeBlueprint *rt)
{
    if (rt == NULL)
    {
        return;
    }
    free(rt->cache_entries);
    rt->cache_entries = NULL;
    rt->cache_entry_count = 0u;
#ifdef _WIN32
    if (rt->mapped != NULL)
    {
        UnmapViewOfFile((LPCVOID)rt->mapped);
        rt->mapped = NULL;
    }
    if (rt->mapping_handle != NULL && rt->mapping_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(rt->mapping_handle);
        rt->mapping_handle = NULL;
    }
    if (rt->file_handle != NULL && rt->file_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(rt->file_handle);
        rt->file_handle = NULL;
    }
#else
    rt->mapped = NULL;
#endif
    memset(&rt->header, 0, sizeof(rt->header));
    rt->mapped_size = 0ULL;
    rt->shards = NULL;
}

static int cfr_runtime_blueprint_open(CFRRuntimeBlueprint *rt,
                                      const char *path,
                                      uint64_t cache_bytes,
                                      int prefetch_mode)
{
#ifdef _WIN32
    LARGE_INTEGER size_li;
    SYSTEM_INFO sysinfo;
#endif
    uint32_t cache_entry_count;

    if (rt == NULL || path == NULL)
    {
        return 0;
    }
    memset(rt, 0, sizeof(*rt));

#ifndef _WIN32
    (void)cache_bytes;
    (void)prefetch_mode;
    fprintf(stderr, "Runtime blueprint mmap loader is currently implemented only for Windows builds\n");
    return 0;
#else
    rt->file_handle = CreateFileA(path,
                                  GENERIC_READ,
                                  FILE_SHARE_READ,
                                  NULL,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  NULL);
    if (rt->file_handle == INVALID_HANDLE_VALUE)
    {
        rt->file_handle = NULL;
        return 0;
    }
    if (!GetFileSizeEx(rt->file_handle, &size_li) || size_li.QuadPart <= 0)
    {
        cfr_runtime_blueprint_close(rt);
        return 0;
    }
    rt->mapping_handle = CreateFileMappingA(rt->file_handle, NULL, PAGE_READONLY, 0, 0, NULL);
    if (rt->mapping_handle == NULL)
    {
        cfr_runtime_blueprint_close(rt);
        return 0;
    }
    rt->mapped = (const unsigned char *)MapViewOfFile(rt->mapping_handle, FILE_MAP_READ, 0, 0, 0);
    if (rt->mapped == NULL)
    {
        cfr_runtime_blueprint_close(rt);
        return 0;
    }

    rt->mapped_size = (uint64_t)size_li.QuadPart;
    if (rt->mapped_size < (uint64_t)sizeof(CFRRuntimeBlueprintFileHeader))
    {
        cfr_runtime_blueprint_close(rt);
        return 0;
    }
    memcpy(&rt->header, rt->mapped, sizeof(rt->header));
    if (rt->header.magic != CFR_RUNTIME_BLUEPRINT_MAGIC ||
        rt->header.version != CFR_RUNTIME_BLUEPRINT_VERSION ||
        rt->header.total_shards == 0u ||
        rt->header.buckets_per_street == 0u)
    {
        cfr_runtime_blueprint_close(rt);
        return 0;
    }
    if (rt->header.shard_table_offset + ((uint64_t)rt->header.total_shards * (uint64_t)sizeof(CFRRuntimeShardHeader)) > rt->mapped_size)
    {
        cfr_runtime_blueprint_close(rt);
        return 0;
    }
    rt->shard_table_offset = rt->header.shard_table_offset;
    rt->index_region_offset = rt->header.index_region_offset;
    rt->data_region_offset = rt->header.data_region_offset;
    rt->shards = (const CFRRuntimeShardHeader *)(rt->mapped + rt->shard_table_offset);

    rt->cache_budget_bytes = cache_bytes;
    cache_entry_count = 0u;
    if (cache_bytes > 0ULL)
    {
        cache_entry_count = (uint32_t)(cache_bytes / (uint64_t)sizeof(CFRRuntimePolicyCacheEntry));
        if (cache_entry_count > 16384u)
        {
            cache_entry_count = 16384u;
        }
        if (cache_entry_count > 0u)
        {
            cache_entry_count = cfr_runtime_next_pow2_u32(cache_entry_count);
            rt->cache_entries = (CFRRuntimePolicyCacheEntry *)calloc((size_t)cache_entry_count, sizeof(CFRRuntimePolicyCacheEntry));
            if (rt->cache_entries == NULL)
            {
                cfr_runtime_blueprint_close(rt);
                return 0;
            }
        }
    }
    rt->cache_entry_count = cache_entry_count;

    if (prefetch_mode == CFR_RUNTIME_PREFETCH_AUTO || prefetch_mode == CFR_RUNTIME_PREFETCH_PREFLOP)
    {
        uint32_t s;
        for (s = 0; s < rt->header.buckets_per_street; ++s)
        {
            const CFRRuntimeShardHeader *sh;
            uint64_t start;
            uint64_t end;
            uint64_t pos;

            sh = &rt->shards[s];
            if (sh->index_count == 0u)
            {
                continue;
            }
            start = sh->index_offset;
            end = start + ((uint64_t)sh->index_count * (uint64_t)sizeof(CFRRuntimeIndexEntry));
            if (end > rt->mapped_size)
            {
                end = rt->mapped_size;
            }
            for (pos = start; pos < end; pos += 4096ULL)
            {
                volatile unsigned char touch;
                touch = rt->mapped[pos];
                (void)touch;
                rt->prefetch_bytes += 1ULL;
            }
            rt->prefetch_loads++;
        }
    }

    GetSystemInfo(&sysinfo);
    (void)sysinfo;
    return 1;
#endif
}

static int cfr_runtime_blueprint_lookup(CFRRuntimeBlueprint *rt,
                                        uint64_t key,
                                        int street_hint,
                                        int action_count,
                                        int use_cache,
                                        float *out_policy)
{
    uint32_t shard_id;
    const CFRRuntimeShardHeader *sh;
    uint32_t lo;
    uint32_t hi;
    uint32_t cache_slot;

    if (rt == NULL || rt->mapped == NULL || out_policy == NULL || action_count <= 0)
    {
        return 0;
    }
    if (street_hint < 0 || street_hint > 3)
    {
        street_hint = 0;
    }

    if (use_cache && rt->cache_entries != NULL && rt->cache_entry_count > 0u)
    {
        cache_slot = (uint32_t)(cfr_runtime_mix_u64(key) & (uint64_t)(rt->cache_entry_count - 1u));
        if (rt->cache_entries[cache_slot].valid &&
            rt->cache_entries[cache_slot].key == key &&
            rt->cache_entries[cache_slot].action_count == (uint32_t)action_count &&
            rt->cache_entries[cache_slot].street_hint == (uint8_t)street_hint)
        {
            int i;
        rt->cache_hits++;
            for (i = 0; i < action_count; ++i)
            {
                out_policy[i] = rt->cache_entries[cache_slot].policy[i];
            }
            return 1;
        }
    }

    shard_id = cfr_runtime_shard_for_key(key, street_hint, rt->header.buckets_per_street);
    if (shard_id >= rt->header.total_shards)
    {
        return 0;
    }
    sh = &rt->shards[shard_id];
    lo = 0u;
    hi = sh->index_count;
    while (lo < hi)
    {
        uint32_t mid;
        const CFRRuntimeIndexEntry *idx;

        mid = lo + ((hi - lo) >> 1);
        idx = (const CFRRuntimeIndexEntry *)(rt->mapped + sh->index_offset + ((uint64_t)mid * (uint64_t)sizeof(CFRRuntimeIndexEntry)));
        if (idx->key < key)
        {
            lo = mid + 1u;
        }
        else
        {
            hi = mid;
        }
    }

    if (lo < sh->index_count)
    {
        const CFRRuntimeIndexEntry *idx;
        const CFRRuntimeNodeHeader *node_hdr;
        const unsigned char *payload;

        idx = (const CFRRuntimeIndexEntry *)(rt->mapped + sh->index_offset + ((uint64_t)lo * (uint64_t)sizeof(CFRRuntimeIndexEntry)));
        if (idx->key != key || idx->node_offset + sizeof(CFRRuntimeNodeHeader) > rt->mapped_size)
        {
            return 0;
        }
        node_hdr = (const CFRRuntimeNodeHeader *)(rt->mapped + idx->node_offset);
        if ((int)node_hdr->action_count != action_count)
        {
            return 0;
        }
        payload = rt->mapped + idx->node_offset + sizeof(CFRRuntimeNodeHeader);
        if (idx->node_offset + cfr_runtime_node_bytes(action_count, (int)node_hdr->quant_mode) > rt->mapped_size)
        {
            return 0;
        }
        cfr_runtime_decode_policy(node_hdr, payload, action_count, out_policy);
        if (use_cache)
        {
            rt->cache_misses++;
        }
        rt->decode_loads++;
        if (use_cache && rt->cache_entries != NULL && rt->cache_entry_count > 0u)
        {
            int i;
            cache_slot = (uint32_t)(cfr_runtime_mix_u64(key) & (uint64_t)(rt->cache_entry_count - 1u));
            rt->cache_entries[cache_slot].valid = 1u;
            rt->cache_entries[cache_slot].key = key;
            rt->cache_entries[cache_slot].action_count = (uint32_t)action_count;
            rt->cache_entries[cache_slot].street_hint = (uint8_t)street_hint;
            for (i = 0; i < action_count; ++i)
            {
                rt->cache_entries[cache_slot].policy[i] = out_policy[i];
            }
        }
        return 1;
    }
    return 0;
}

static uint64_t cfr_runtime_blueprint_cache_resident_bytes(const CFRRuntimeBlueprint *rt)
{
    if (rt == NULL || rt->cache_entries == NULL)
    {
        return 0ULL;
    }
    return (uint64_t)rt->cache_entry_count * (uint64_t)sizeof(CFRRuntimePolicyCacheEntry);
}

static void cfr_policy_provider_init_blueprint(CFRPolicyProvider *provider, const CFRBlueprint *bp)
{
    if (provider == NULL)
    {
        return;
    }
    memset(provider, 0, sizeof(*provider));
    provider->kind = 0;
    provider->blueprint = bp;
    provider->use_runtime_cache = 0;
}

static void cfr_policy_provider_init_runtime(CFRPolicyProvider *provider, CFRRuntimeBlueprint *rt)
{
    if (provider == NULL)
    {
        return;
    }
    memset(provider, 0, sizeof(*provider));
    provider->kind = 1;
    provider->runtime_bp = rt;
    provider->use_runtime_cache = 1;
}

static uint32_t cfr_policy_provider_abstraction_hash32(const CFRPolicyProvider *provider)
{
    if (provider == NULL)
    {
        return 0u;
    }
    if (provider->kind == 1 && provider->runtime_bp != NULL)
    {
        return provider->runtime_bp->header.abstraction_hash32;
    }
    if (provider->blueprint != NULL)
    {
        return provider->blueprint->abstraction_hash32;
    }
    return 0u;
}

static uint32_t cfr_policy_provider_compat_hash32(const CFRPolicyProvider *provider)
{
    if (provider == NULL)
    {
        return 0u;
    }
    if (provider->kind == 1 && provider->runtime_bp != NULL)
    {
        return provider->runtime_bp->header.compat_hash32;
    }
    if (provider->blueprint != NULL)
    {
        return provider->blueprint->compat_hash32;
    }
    return 0u;
}

static uint32_t cfr_policy_provider_node_count(const CFRPolicyProvider *provider)
{
    if (provider == NULL)
    {
        return 0u;
    }
    if (provider->kind == 1 && provider->runtime_bp != NULL)
    {
        return provider->runtime_bp->header.node_count;
    }
    if (provider->blueprint != NULL)
    {
        return provider->blueprint->used_node_count;
    }
    return 0u;
}

static int cfr_policy_provider_get_average_policy(CFRPolicyProvider *provider,
                                                  uint64_t key,
                                                  int street_hint,
                                                  int action_count,
                                                  float *out_policy)
{
    CFRNode *node;

    if (provider == NULL || out_policy == NULL || action_count <= 0)
    {
        return 0;
    }
    if (provider->kind == 1)
    {
        return cfr_runtime_blueprint_lookup(provider->runtime_bp,
                                            key,
                                            street_hint,
                                            action_count,
                                            provider->use_runtime_cache,
                                            out_policy);
    }
    if (provider->blueprint == NULL)
    {
        return 0;
    }
    node = cfr_blueprint_get_node((CFRBlueprint *)provider->blueprint, key, 0, action_count);
    if (node == NULL)
    {
        return 0;
    }
    cfr_compute_average_strategy_n(node, action_count, out_policy);
    return 1;
}

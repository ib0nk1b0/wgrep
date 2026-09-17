
typedef struct
{
    size_t num_matches;
    size_t* indices;
} MatchResult;

internal MatchResult sv_contains_brute_force(Arena* arena, StringView sv, const char* pattern)
{
    MatchResult result = {0};
    size_t pattern_length = strlen(pattern);
    bool match_found = false;
    if (sv.size < pattern_length)
    {
        return result;
    }

    result.indices = ArenaPushStruct(arena, size_t);

    size_t i = 0;
    for (; i < sv.size - pattern_length + 1; i++)
    {
        match_found = true;
        for (size_t j = 0; j < pattern_length; j++)
        {
            if (sv.data[i + j] != pattern[j])
            {
                match_found = false;
                break;
            }
        }

        if (match_found)
        {
            result.indices[result.num_matches++] = i;
            ArenaPushStruct(arena, size_t);
        }
    }

    return result;
}

internal void compute_lps(Arena* arena, const char* pattern)
{
    size_t pattern_length = strlen(pattern);
    size_t i = 1; // NOTE: start from 1 as first is always 0
    size_t len = 0;
    g_lps = ArenaPushArray(arena, size_t, pattern_length); // NOTE: memory should always be zeroed but may want to guarantee it

    while (i < pattern_length)
    {
        if (pattern[i] == pattern[len])
        {
            len++;
            g_lps[i++] = len;
        }
        else if (pattern[i] != pattern[len] && len > 0)
        {
            len = g_lps[len - 1];
        }
        else
        {
            g_lps[i++] = len;
        }

    }
}

internal MatchResult sv_contains_kmp(Arena* arena, StringView sv, const char* pattern)
{
    MatchResult result = {0};
    size_t pattern_length = strlen(pattern);
    bool match_found = false;
    if (sv.size < pattern_length)
    {
        return result;
    }

    result.indices = ArenaPushStruct(arena, size_t);

    // NOTE: Compute LPS - Longest Proper Prefix which is also a Suffix
    size_t i = 0;
    size_t j = 0;
    size_t match_idx = 0;

    while (i < sv.size)
    {
        if (sv.data[i] == pattern[j])
        {
            i++;
            j++;

            if (j == pattern_length)
            {
                result.indices[result.num_matches++] = i - j;
                ArenaPushStruct(arena, size_t);

                j = g_lps[j - 1];
            }
        }
        else if (j != 0)
        {
            j = g_lps[j - 1];
        }
        else
        {
            i++;
        }
    }

    return result;
}

internal void bad_character_heuristic(const char* pattern, int pattern_len)
{
    int i;

    memset(g_badchar, -1, NUM_CHARS);

    for (i = 0; i < pattern_len; i++)
    {
        g_badchar[(int)pattern[i]] = i;
    }
}

internal void preprocess_strong_suffix(int* shift, int* bpos, const char* pattern, int m)
{
    int i = m, j = m + 1;

    bpos[i] = j;

    while (i > 0)
    {
        while (j <= m && pattern[i - 1] != pattern[j - 1])
        {
            if (shift[j] == 0)
            {
                shift[j] = j - i;
            }

            j = bpos[j];
        }

        i--;
        j--;

        bpos[i] = j;
    }
}

internal void preprocess_case2(int* shift, int* bpos, const char* pattern, int m)
{
    int i, j;
    j = bpos[0];

    for (i = 0; i <= m; i++)
    {
        if (shift[i] == 0)
        {
            shift[i] = j;
        }

        if (i == j)
        {
            j = bpos[j];
        }
    }
}

internal MatchResult sv_contains_bm(Arena* arena, StringView sv, const char* pattern)
{
    MatchResult result = {0};

    result.indices = ArenaPushStruct(arena, size_t);

    int s = 0, j;
    int m = strlen(pattern);
    int n = (int)sv.size; // TODO: do I want to use ints here?

    while (s <= n - m)
    {
        j = m - 1;

        while (j >= 0 && pattern[j] == sv.data[s + j])
        {
            j--;
        }

        if (j < 0)
        {
            // We have a match just don't know what to do with it yet?
            result.indices[result.num_matches++] = s;
            ArenaPushStruct(arena, size_t);

            // s += (s + m < n) ? m - badchar[sv.data[s + m]] : 1;
            s += g_shift[0];
        }
        else
        {
            s += max(g_shift[j + 1], j - g_badchar[sv.data[s + j]]);
            // s += g_shift[j + 1];
        }

    }

    return result;
}


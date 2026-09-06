#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Shlwapi.h>
#include <intrin.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <io.h>

#define CUTILS_IMPLEMENTATION
#define CUTILS_NO_PREFIX
#include "cutils.h"

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define ANSI_COLOR_RED_LEN     strlen(ANSI_COLOR_RED)
#define ANSI_COLOR_GREEN_LEN   strlen(ANSI_COLOR_GREEN)
#define ANSI_COLOR_YELLOW_LEN  strlen(ANSI_COLOR_YELLOW)
#define ANSI_COLOR_BLUE_LEN    strlen(ANSI_COLOR_BLUE)
#define ANSI_COLOR_MAGENTA_LEN strlen(ANSI_COLOR_MAGENTA)
#define ANSI_COLOR_CYAN_LEN    strlen(ANSI_COLOR_CYAN)
#define ANSI_COLOR_RESET_LEN   strlen(ANSI_COLOR_RESET)

#define WGREP_OPTION_n    (1 << 0)
#define WGREP_OPTION_o    (1 << 1)
#define WGREP_OPTION_r    (1 << 2)
#define WGREP_OPTION_c    (1 << 3)
#define WGREP_OPTION_H    (1 << 4)
#define WGREP_OPTION_perf (1 << 5)
#define WGREP_OPTION_kmp  (1 << 6)
#define WGREP_OPTION_bm   (1 << 7)

typedef struct
{
    LARGE_INTEGER  start;
    LARGE_INTEGER  end;
    LARGE_INTEGER  frequency;
    uint32_t       files_searched;
    size_t         bytes_searched;
    double         search_time;
    double         line_chop_time;
    double         file_read_time;
} Performance_Statistics;

#define NUM_CHARS 256

static Performance_Statistics g_Stats;
static char*                  g_Program;
static char*                  g_out_buffer;
static int*                   g_shift;
static int*                   g_bpos;
static int                    g_badchar[NUM_CHARS];
static size_t*                g_lps;
// TODO: Cleanup
static size_t buffer_idx = 0;

#define OUT_BUFFER_SIZE Megabytes(64)

void usage(FILE* stream, const char* program)
{
    fprintf(stream, "Usage: %s [OPTIONS] Patterns [FILE]\n", program);
    fprintf(stream, "       -n     print line number\n");
    fprintf(stream, "       -o     print only matching part of line\n");
    fprintf(stream, "       -r     search directory recursively\n");
    fprintf(stream, "       -c     print only count of how many matches were found\n");
    fprintf(stream, "       -H     print file names\n");
    fprintf(stream, "       --perf print performance stats\n");
    fprintf(stream, "       --kmp  use kmp searching algorithm. not implemented\n");
}

// TODO: figure out faster file reading
void wgrep_read_file(const char* filepath)
{

}

typedef struct
{
    size_t num_matches;
    size_t* indices;
} MatchResult;

MatchResult sv_contains_brute_force(Arena* arena, StringView sv, const char* pattern)
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

void compute_lps(Arena* arena, const char* pattern)
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

    // NOTE: print the computed array
    // For Debugging only
    // for (size_t idx = 0; idx < pattern_length; idx++)
    // {
    //     if (idx < strlen(pattern) - 1)
    //     {
    //         printf("%zu, ", g_lps[idx]);
    //     }
    //     else
    //     {
    //         printf("%zu\n", g_lps[idx]);
    //     }
    // }
}

// NOTE: kmp = Knuth, Morris, Pratt algorithm
MatchResult sv_contains_kmp(Arena* arena, StringView sv, const char* pattern)
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

void bad_character_heuristic(const char* pattern, int pattern_len)
{
    int i;

    memset(g_badchar, -1, NUM_CHARS);

    for (i = 0; i < pattern_len; i++)
    {
        g_badchar[(int)pattern[i]] = i;
    }
}

void preprocess_strong_suffix(int* shift, int* bpos, const char* pattern, int m)
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

void preprocess_case2(int* shift, int* bpos, const char* pattern, int m)
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

MatchResult sv_contains_bm(Arena* arena, StringView sv, const char* pattern)
{
    MatchResult result = {0};

    result.indices = ArenaPushStruct(arena, size_t);

    int s = 0, j;
    int m = strlen(pattern);
    int n = (int)sv.size; // TODO: do I want to use ints here?

    // int badchar[NUM_CHARS];
    // bad_character_heuristic(pattern, m, badchar);

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

size_t match_pattern_in_sv(Arena* arena, const char* pattern, StringView sv, const char* file, uint32_t flags)
{
    g_Stats.files_searched++;
    g_Stats.bytes_searched += sv.size;

    // TODO: figure out faster printing
    // The quick hack of batching stuff in a buffer speeds up when theres a lot of printing to be done but slows down when hardly any???
#if 1
    size_t total_matches = 0;
    size_t line_number = 0;
    while (sv.size > 0)
    {
        LARGE_INTEGER line_chop_start, line_chop_end;
        QueryPerformanceCounter(&line_chop_start);
        line_number += 1;
        StringView line = sv_chop_line(&sv);
        QueryPerformanceCounter(&line_chop_end);
        g_Stats.line_chop_time += (line_chop_end.QuadPart - line_chop_start.QuadPart) * 1000.0 / g_Stats.frequency.QuadPart;

        if (line.size == 0) continue;

        LARGE_INTEGER search_start, search_end;
        QueryPerformanceCounter(&search_start);
        size_t index = 0;
        MatchResult match_sv;
        if (flags & WGREP_OPTION_kmp)
        {
            match_sv = sv_contains_kmp(arena, line, pattern);
        }
        else if (flags & WGREP_OPTION_bm)
        {
            match_sv = sv_contains_bm(arena, line, pattern);
        }
        else
        {
            match_sv = sv_contains_brute_force(arena, line, pattern);
        }
        QueryPerformanceCounter(&search_end);
        g_Stats.search_time += (search_end.QuadPart - search_start.QuadPart) * 1000.0 / g_Stats.frequency.QuadPart;

        if (match_sv.num_matches)
        {
            total_matches += match_sv.num_matches;

            if (flags & WGREP_OPTION_c)
            {
                continue;
            }

            if (flags & WGREP_OPTION_H && file != NULL)
            {
                if (ANSI_COLOR_GREEN_LEN + ANSI_COLOR_RESET_LEN + strlen(file) + buffer_idx >= OUT_BUFFER_SIZE)
                {
                    printf(g_out_buffer);
                    memset(g_out_buffer, 0, buffer_idx);
                    buffer_idx = 0;
                }
                buffer_idx += (size_t)snprintf(g_out_buffer + buffer_idx, OUT_BUFFER_SIZE - buffer_idx, "%s%s:%s", ANSI_COLOR_GREEN, file, ANSI_COLOR_RESET);
            }

            if (flags & WGREP_OPTION_n)
            {
                size_t digits = (size_t)snprintf(NULL, 0, "%zu", line_number);
                if (ANSI_COLOR_CYAN_LEN + ANSI_COLOR_RESET_LEN + digits + buffer_idx >= OUT_BUFFER_SIZE)
                {
                    printf(g_out_buffer);
                    memset(g_out_buffer, 0, buffer_idx);
                    buffer_idx = 0;
                }
                buffer_idx += (size_t)snprintf(g_out_buffer + buffer_idx, OUT_BUFFER_SIZE - buffer_idx, "%s%zu:%s", ANSI_COLOR_CYAN, line_number, ANSI_COLOR_RESET);
            }

            if (flags & WGREP_OPTION_o)
            {
                if (ANSI_COLOR_RED_LEN + ANSI_COLOR_RESET_LEN + strlen(pattern) + buffer_idx >= OUT_BUFFER_SIZE)
                {
                    printf(g_out_buffer);
                    memset(g_out_buffer, 0, buffer_idx);
                    buffer_idx = 0;
                }
                buffer_idx += (size_t)snprintf(g_out_buffer + buffer_idx, OUT_BUFFER_SIZE - buffer_idx, "%s%s%s\n", ANSI_COLOR_RED, pattern, ANSI_COLOR_RESET);
            }
            else
            {
                // TODO: fix printing when 2 pattern indices are less than pattern len apart
                size_t pattern_len = strlen(pattern);
                StringView lhs = sv_from_parts(line.data, match_sv.indices[0]);

                if (ANSI_COLOR_RED_LEN + ANSI_COLOR_RESET_LEN + pattern_len + lhs.size + buffer_idx >= OUT_BUFFER_SIZE)
                {
                    printf(g_out_buffer);
                    memset(g_out_buffer, 0, buffer_idx);
                    buffer_idx = 0;
                }
                buffer_idx += (size_t)snprintf(g_out_buffer + buffer_idx, OUT_BUFFER_SIZE - buffer_idx, SV_FMT"%s%s%s", SV_ARG(lhs), ANSI_COLOR_RED, pattern,ANSI_COLOR_RESET);

                for (size_t i = 1; i < match_sv.num_matches; i++)
                {
                    if (match_sv.indices[i] - match_sv.indices[i - 1] < pattern_len)
                    {
                        // Need to print extra partial pattern
                        size_t partial_pattern_len = match_sv.indices[i] - match_sv.indices[i - 1];
                        if (ANSI_COLOR_RED_LEN + ANSI_COLOR_RESET_LEN + partial_pattern_len >= OUT_BUFFER_SIZE)
                        {
                            printf(g_out_buffer);
                            memset(g_out_buffer, 0, buffer_idx);
                            buffer_idx = 0;
                        }
                        StringView partial_pattern = sv_from_parts(pattern + pattern_len - partial_pattern_len, partial_pattern_len);
                        buffer_idx += (size_t)snprintf(g_out_buffer + buffer_idx, OUT_BUFFER_SIZE - buffer_idx, "%s"SV_FMT"%s", ANSI_COLOR_RED, SV_ARG(partial_pattern), ANSI_COLOR_RESET);
                    }
                    else
                    {
                        size_t start_pos = match_sv.indices[i - 1] + pattern_len;

                        lhs = sv_from_parts(line.data + start_pos, match_sv.indices[i] - start_pos);

                        if (ANSI_COLOR_RED_LEN + ANSI_COLOR_RESET_LEN + pattern_len + lhs.size + buffer_idx >= OUT_BUFFER_SIZE)
                        {
                            printf(g_out_buffer);
                            memset(g_out_buffer, 0, buffer_idx);
                            buffer_idx = 0;
                        }
                        buffer_idx += (size_t)snprintf(g_out_buffer + buffer_idx, OUT_BUFFER_SIZE - buffer_idx, SV_FMT"%s%s%s", SV_ARG(lhs), ANSI_COLOR_RED, pattern,ANSI_COLOR_RESET);
                    }
                }

                size_t start_pos = match_sv.indices[match_sv.num_matches - 1] + pattern_len;
                StringView rhs = sv_from_parts(line.data + start_pos, line.size - start_pos);

                if (rhs.size + buffer_idx >= OUT_BUFFER_SIZE)
                {
                    printf(g_out_buffer);
                    memset(g_out_buffer, 0, buffer_idx);
                    buffer_idx = 0;
                }

                buffer_idx += (size_t)snprintf(g_out_buffer + buffer_idx, OUT_BUFFER_SIZE - buffer_idx, SV_FMT"\n", SV_ARG(rhs));
            }
        }
    }

#else
    size_t total_matches = 0;
    size_t line_number = 0;
    while (sv.size > 0)
    {
        LARGE_INTEGER line_chop_start, line_chop_end;
        QueryPerformanceCounter(&line_chop_start);
        
        line_number += 1;
        StringView line = sv_chop_line(&sv);

        QueryPerformanceCounter(&line_chop_end);
        g_Stats.line_chop_time += (line_chop_end.QuadPart - line_chop_start.QuadPart) * 1000.0 / g_Stats.frequency.QuadPart;

        if (line.size == 0) continue;

        LARGE_INTEGER search_start, search_end;
        QueryPerformanceCounter(&search_start);

        size_t index = 0;
        MatchResult match_sv;

        if (flags & WGREP_OPTION_kmp)
        {
            match_sv = sv_contains_kmp(arena, line, pattern);
        }
        else if (flags & WGREP_OPTION_bm)
        {
            match_sv = sv_contains_bm(arena, line, pattern);
        }
        else
        {
            match_sv = sv_contains_brute_force(arena, line, pattern);
        }

        QueryPerformanceCounter(&search_end);
        g_Stats.search_time += (search_end.QuadPart - search_start.QuadPart) * 1000.0 / g_Stats.frequency.QuadPart;

        if (flags & WGREP_OPTION_kmp)
        {
            match_sv = sv_contains_kmp(arena, line, pattern);
        }
        else if (flags & WGREP_OPTION_bm)
        {
            match_sv = sv_contains_bm(arena, line, pattern);
        }
        else
        {
            match_sv = sv_contains_brute_force(arena, line, pattern);
        }

        if (match_sv.num_matches)
        {
            total_matches += match_sv.num_matches;
            if (flags & WGREP_OPTION_c)
            {
                continue;
            }
            if (flags & WGREP_OPTION_H && file != NULL)
            {
                printf(ANSI_COLOR_GREEN"%s:"ANSI_COLOR_RESET, file);
            }

            if (flags & WGREP_OPTION_n)
            {
                printf(ANSI_COLOR_CYAN"%zu:"ANSI_COLOR_RESET, line_number);
            }

            if (flags & WGREP_OPTION_o)
            {
                printf(ANSI_COLOR_RED"%s"ANSI_COLOR_RESET"\n", pattern);
            }
            else
            {
                size_t pattern_len = strlen(pattern);
                StringView lhs = sv_from_parts(line.data, match_sv.indices[0]);
                printf(SV_FMT, SV_ARG(lhs));
                printf(ANSI_COLOR_RED "%s"ANSI_COLOR_RESET, pattern);
                for (size_t i = 1; i < match_sv.num_matches; i++)
                {
                    if (match_sv.indices[i] - match_sv.indices[i - 1] < pattern_len)
                    {
                        // Need to print extra partial pattern
                        size_t partial_pattern_len = match_sv.indices[i] - match_sv.indices[i - 1];
                        StringView partial_pattern = sv_from_parts(pattern + pattern_len - partial_pattern_len, partial_pattern_len);
                        printf("%s"SV_FMT"%s", ANSI_COLOR_RED, SV_ARG(partial_pattern), ANSI_COLOR_RESET);
                    }
                    else
                    {
                        size_t start_pos = match_sv.indices[i - 1] + pattern_len;

                        lhs = sv_from_parts(line.data + start_pos, match_sv.indices[i] - start_pos);

                        printf(SV_FMT, SV_ARG(lhs));
                        printf(ANSI_COLOR_RED "%s"ANSI_COLOR_RESET, pattern);
                    }

                    // size_t start_pos = match_sv.indices[i - 1] + pattern_len + 1;
                    // lhs = sv_from_parts(line.data + start_pos, match_sv.indices[i] - start_pos);
                }
                size_t start_pos = match_sv.indices[match_sv.num_matches - 1] + pattern_len;
                StringView rhs = sv_from_parts(line.data + start_pos, line.size - start_pos);
                printf(SV_FMT"\n", SV_ARG(rhs));
            }
        }
    }

#endif

    // TODO: Cleanup
    if (buffer_idx > 0)
    {
        printf(g_out_buffer);
    }

    return total_matches;
}

size_t recurse_directory(Arena* arena, const char* dir, const char* pattern, uint32_t flags)
{
    WIN32_FIND_DATA ffd = {0};
    LARGE_INTEGER filesize;
    TCHAR szDir[MAX_PATH];
    size_t length_of_arg;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    DWORD dwError=0;

    size_t dir_len = strlen(dir);
    if (dir[dir_len-1] == '\\' || dir[dir_len-1] == '/')
    {
        snprintf(szDir, MAX_PATH, "%s*", dir);
    }
    else
    {
        snprintf(szDir, MAX_PATH, "%s\\*", dir);
    }

    hFind = FindFirstFile(szDir, &ffd);

    size_t total_matches = 0;
    do
    {
        char full_path[MAX_PATH];

        if (dir[dir_len-1] == '\\' || dir[dir_len-1] == '/')
        {
            snprintf(full_path, MAX_PATH, "%s%s", dir, ffd.cFileName);
        }
        else
        {
            snprintf(full_path, MAX_PATH, "%s\\%s", dir, ffd.cFileName);
        }
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (ffd.cFileName[0] != '.')
            {
                total_matches += recurse_directory(arena, full_path, pattern, flags);
            }
        }
        else
        {
            size_t arena_start = arena->pos;
            LARGE_INTEGER t1, t2;
            QueryPerformanceCounter(&t1);
            StringView result = sv_read_entire_file(arena, full_path);
            if (result.data == NULL)
            {
                printf("Failed to read file %s\n", full_path);
                return 0;
            }
            QueryPerformanceCounter(&t2);
            g_Stats.file_read_time += (t2.QuadPart - t1.QuadPart) * 1000.0 / g_Stats.frequency.QuadPart;

            total_matches += match_pattern_in_sv(arena, pattern, result, full_path, flags);

            arena_pop(arena, arena->pos - arena_start);
        }
    }
    while (FindNextFile(hFind, &ffd) != 0);

    return total_matches;
}

int main(int argc, char** argv)
{
    g_Stats = (Performance_Statistics){0};
    QueryPerformanceFrequency(&g_Stats.frequency);
    QueryPerformanceCounter(&g_Stats.start);

    Arena* arena = arena_create((size_t)Gigabytes(4));
    g_out_buffer = ArenaPushArray(arena, char, OUT_BUFFER_SIZE); // not sure why 4mb at a time...

    g_Program = next_cmd_line_arg(&argc, &argv);

    // NOTE: enable ansi escape codes if not already
    // TODO: what if any of this fails?
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode))
    {
        return false;
    }

    if (!(mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING))
    {
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(handle, mode); // TODO: what if this fails
    }

    //-------------------------------------------------------------------------

    char* pattern = NULL;
    char* file = NULL;
    uint32_t flags = 0;
    while (argc > 0)
    {
        char* flag = next_cmd_line_arg(&argc, &argv);
        if (strcmp(flag, "-n") == 0)
        {
            flags |= WGREP_OPTION_n;
        }
        else if (strcmp(flag, "-o") == 0)
        {
            flags |= WGREP_OPTION_o;
        }
        else if (strcmp(flag, "-r") == 0)
        {
            flags |= WGREP_OPTION_r;
        }
        else if (strcmp(flag, "-c") == 0)
        {
            flags |= WGREP_OPTION_c;
        }
        else if (strcmp(flag, "-H") == 0)
        {
            flags |= WGREP_OPTION_H;
        }
        else if (strcmp(flag, "--perf") == 0)
        {
            flags |= WGREP_OPTION_perf;
        }
        else if (strcmp(flag, "--kmp") == 0)
        {
            flags |= WGREP_OPTION_kmp;
        }
        else if (strcmp(flag, "--bm") == 0)
        {
            flags |= WGREP_OPTION_bm;
        }
        else if (flag[0] == '-')
        {
            size_t flag_len = strlen(flag);
            if (flag_len <= 2)
            {
                fprintf(stderr, "ERROR: Unkown flag provided %s\n", flag);
                usage(stderr, g_Program);
                exit(1);
            }
            else if (flag[1] == '-')
            {
                fprintf(stderr, "ERROR: Unkown flag provided %s\n", flag);
                usage(stderr, g_Program);
                exit(1);
            }

            for (size_t i = 1; i < flag_len; i++)
            {
                if (flag[i] == 'n')
                {
                    flags |= WGREP_OPTION_n;
                }
                else if (flag[i] == 'o')
                {
                    flags |= WGREP_OPTION_o;
                }
                else if (flag[i] == 'r')
                {
                    flags |= WGREP_OPTION_r;
                }
                else if (flag[i] == 'c')
                {
                    flags |= WGREP_OPTION_c;
                }
                else if (flag[i] == 'H')
                {
                    flags |= WGREP_OPTION_H;
                }
                else
                {
                    fprintf(stderr, "ERROR: Unkown flag provided -%c\n", flag[i]);
                    usage(stderr, g_Program);
                    exit(1);
                }
            }
        }
        else if (pattern == NULL)
        {
            pattern = flag;
            if (flags & WGREP_OPTION_bm)
            {
                int m = strlen(pattern);
                g_bpos = ArenaPushArray(arena, int, m + 1);
                g_shift = ArenaPushArray(arena, int, m + 1);

                bad_character_heuristic(pattern, m);
                preprocess_strong_suffix(g_shift, g_bpos, pattern, m);
                preprocess_case2(g_shift, g_bpos, pattern, m);
            }
            else if (flags & WGREP_OPTION_kmp)
            {
                compute_lps(arena, pattern);
            }
        }
        else if (file == NULL)
        {
            file = flag;
        }
    }

    bool recurse_dirs = (flags & WGREP_OPTION_r);
    bool print_count  = (flags & WGREP_OPTION_c);


    if (pattern != NULL && !_isatty(_fileno(stdin)))
    {
        size_t buf_size = Megabytes(64);
        size_t total_matches = 0;
        char* buf = NULL;
        char* result = NULL;
        do
        {
            buf = ArenaPushArray(arena, char, buf_size);
            result = fgets(buf, buf_size, stdin);
            size_t buffer_size = Megabytes(4);
            total_matches += match_pattern_in_sv(arena, pattern, sv_from_cstr(buf), NULL, flags);
            ArenaPopArray(arena, char, buf_size);
        } while (result != NULL);

        if (print_count)
        {
            printf("%zu\n", total_matches);
        }
    }
    else
    {
        if (pattern == NULL || (recurse_dirs == false && file == NULL))
        {
            fprintf(stderr, "ERROR: Not enough arguments provided!\n");
            usage(stderr, g_Program);
            exit(1);
        }

        if (!recurse_dirs)
        {
            size_t arena_start = arena->pos;
            LARGE_INTEGER t1, t2;
            QueryPerformanceCounter(&t1);
            StringView result = sv_read_entire_file(arena, file);
            if (result.data == NULL)
            {
                printf("Failed to read file %s\n", file);
                return 0;
            }
            QueryPerformanceCounter(&t2);
            g_Stats.file_read_time += (t2.QuadPart - t1.QuadPart) * 1000.0 / g_Stats.frequency.QuadPart;

            size_t total_matches = match_pattern_in_sv(arena, pattern, result, file, flags);

            if (print_count)
            {
                printf("%zu\n", total_matches);
            }

            arena_pop(arena, arena->pos - arena_start);
        }
        else if (recurse_dirs && file != NULL)
        {
            size_t total_matches = 0;
            if (PathIsDirectoryA(file))
            {
                total_matches = recurse_directory(arena, file, pattern, flags);
            }
            else
            {
                size_t arena_start = arena->pos;
                LARGE_INTEGER t1, t2;
                QueryPerformanceCounter(&t1);
                StringView result = sv_read_entire_file(arena, file);
                if (result.data == NULL)
                {
                    printf("Failed to read file %s\n", file);
                    return 0;
                }
                QueryPerformanceCounter(&t2);
                g_Stats.file_read_time += (t2.QuadPart - t1.QuadPart) * 1000.0 / g_Stats.frequency.QuadPart;

                total_matches = match_pattern_in_sv(arena, pattern, result, file, flags);

                arena_pop(arena, arena->pos - arena_start);
            }

            if (print_count)
            {
                printf("%zu\n", total_matches);
            }
        }
        else
        {
            size_t total_matches = 0;
            if (file == NULL)
            {
                total_matches = recurse_directory(arena, ".", pattern, flags);
            }

            if (print_count)
            {
                printf("%zu\n", total_matches);
            }
        }
    }

    if (flags & WGREP_OPTION_perf)
    {
        QueryPerformanceCounter(&g_Stats.end);

        double elapsed_time = (g_Stats.end.QuadPart - g_Stats.start.QuadPart) * 1000.0 / g_Stats.frequency.QuadPart;
        double other_time = elapsed_time - g_Stats.line_chop_time - g_Stats.search_time - g_Stats.file_read_time;

        printf("Total files searched: %d\n", g_Stats.files_searched);
        printf("Total bytes searched: %zu\n", g_Stats.bytes_searched);
        printf("\n");
        printf("%-24s | %10.4fms | %3d%%\n", "Total elapsed time", elapsed_time, 100);
        printf("%-24s | %10.4fms | %3d%%\n", "Total line chop time", g_Stats.line_chop_time, (int)(g_Stats.line_chop_time / elapsed_time * 100.0));
        printf("%-24s | %10.4fms | %3d%%\n", "Total string search time", g_Stats.search_time, (int)(g_Stats.search_time / elapsed_time * 100.0));
        printf("%-24s | %10.4fms | %3d%%\n", "Total file read time", g_Stats.file_read_time, (int)(g_Stats.file_read_time / elapsed_time * 100.0));
        printf("%-24s | %10.4fms | %3d%%\n", "Other time", other_time, (int)(other_time / elapsed_time * 100.0));
    }

    return 0;
}

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Shlwapi.h>
#include <intrin.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <io.h>

#define internal static
#define OUT_BUFFER_SIZE Kilobytes(64)
#define FILE_BUFFER_SIZE Megabytes(64)

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

#define WGREP_NUM_FLAGS    9
#define WGREP_OPTION_n     (1 << 0)
#define WGREP_OPTION_o     (1 << 1)
#define WGREP_OPTION_r     (1 << 2)
#define WGREP_OPTION_c     (1 << 3)
#define WGREP_OPTION_H     (1 << 4)
#define WGREP_OPTION_perf  (1 << 5)
#define WGREP_OPTION_kmp   (1 << 6)
#define WGREP_OPTION_bm    (1 << 7)
#define WGREP_OPTION_multi (1 << 8)

typedef struct
{
    const char* cmd;
    uint32_t    flag;
    char*       desc;
} Cmd_Line_Arg;

static const Cmd_Line_Arg command_line_args[WGREP_NUM_FLAGS] =
{
    { "-n",      WGREP_OPTION_n,     "print line number"                               },
    { "-o",      WGREP_OPTION_o,     "print only matching part of line"                },
    { "-r",      WGREP_OPTION_r,     "search directory recursively"                    },
    { "-c",      WGREP_OPTION_c,     "print only count of how many matches were found" },
    { "-H",      WGREP_OPTION_H,     "print file names"                                },
    { "--perf",  WGREP_OPTION_perf,  "print performance stats"                         },
    { "--kmp",   WGREP_OPTION_kmp,   "use kmp searching algorithm"                     },
    { "--bm",    WGREP_OPTION_bm,    "use boyer-moore algorithm"                       },
    { "--multi", WGREP_OPTION_multi, "use multi-threading"                             }
};

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
static int*                   g_shift;
static int*                   g_bpos;
static int                    g_badchar[NUM_CHARS];
static size_t*                g_lps;
// TODO: Cleanup
static char* g_out_buffer;
static uint32_t g_buffer_idx = 0;
static HANDLE StdOut;
static char*  file_buffer = NULL;
static size_t g_total_matches = 0;

// My Includes...
#define CUTILS_IMPLEMENTATION
#define CUTILS_NO_PREFIX
#include "cutils.h"
internal size_t match_pattern_in_sv(Arena* arena, char* out_buffer, uint32_t out_buffer_size, uint32_t* buffer_idx, const char* pattern, StringView sv, const char* file, uint32_t flags);
internal void print_buffer(const char* out_buffer, uint32_t* buffer_idx);
#include "threads.c"
#include "string_matching.c"

internal void usage(FILE* stream, const char* program)
{
    fprintf(stream, "Usage: %s [OPTIONS] Patterns [FILE]\n", program);
    for (int i = 0; i < WGREP_NUM_FLAGS; i++)
    {
        fprintf(stream, "       %-10s %s\n", command_line_args[i].cmd, command_line_args[i].desc);
    }
}

internal void print_buffer(const char* out_buffer, uint32_t* buffer_idx)
{
    WriteFile(StdOut, out_buffer, *buffer_idx, NULL, NULL);
    *buffer_idx = 0;
}

internal size_t match_pattern_in_sv(Arena* arena, char* out_buffer, uint32_t out_buffer_size, uint32_t* buffer_idx, const char* pattern, StringView sv, const char* file, uint32_t flags)
{
    size_t total_matches = 0;
    size_t line_number = 0;
    while (sv.size > 0)
    {
        LARGE_INTEGER line_chop_start, line_chop_end;
        QueryPerformanceCounter(&line_chop_start);

        // TODO: test just building an array of all line positions and processing file in one go
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
                if (ANSI_COLOR_GREEN_LEN + ANSI_COLOR_RESET_LEN + strlen(file) + *buffer_idx >= out_buffer_size)
                {
                    print_buffer(out_buffer, buffer_idx);
                }
                *buffer_idx += (size_t)snprintf(out_buffer + *buffer_idx, out_buffer_size - *buffer_idx, "%s%s:%s", ANSI_COLOR_GREEN, file, ANSI_COLOR_RESET);
            }

            if (flags & WGREP_OPTION_n)
            {
                size_t digits = (size_t)snprintf(NULL, 0, "%zu", line_number);
                if (ANSI_COLOR_CYAN_LEN + ANSI_COLOR_RESET_LEN + digits + *buffer_idx >= out_buffer_size)
                {
                    print_buffer(out_buffer, buffer_idx);
                }
                *buffer_idx += (size_t)snprintf(out_buffer + *buffer_idx, out_buffer_size - *buffer_idx, "%s%zu:%s", ANSI_COLOR_CYAN, line_number, ANSI_COLOR_RESET);
            }

            if (flags & WGREP_OPTION_o)
            {
                if (ANSI_COLOR_RED_LEN + ANSI_COLOR_RESET_LEN + strlen(pattern) + *buffer_idx >= out_buffer_size)
                {
                    print_buffer(out_buffer, buffer_idx);
                }
                *buffer_idx += (size_t)snprintf(out_buffer + *buffer_idx, out_buffer_size - *buffer_idx, "%s%s%s\n", ANSI_COLOR_RED, pattern, ANSI_COLOR_RESET);
            }
            else
            {
                // TODO: fix printing when 2 pattern indices are less than pattern len apart
                size_t pattern_len = strlen(pattern);
                StringView lhs = sv_from_parts(line.data, match_sv.indices[0]);

                if (ANSI_COLOR_RED_LEN + ANSI_COLOR_RESET_LEN + pattern_len + lhs.size + *buffer_idx >= out_buffer_size)
                {
                    print_buffer(out_buffer, buffer_idx);
                }
                *buffer_idx += (size_t)snprintf(out_buffer + *buffer_idx, out_buffer_size - *buffer_idx, SV_FMT"%s%s%s", SV_ARG(lhs), ANSI_COLOR_RED, pattern,ANSI_COLOR_RESET);

                for (size_t i = 1; i < match_sv.num_matches; i++)
                {
                    if (match_sv.indices[i] - match_sv.indices[i - 1] < pattern_len)
                    {
                        // Need to print extra partial pattern
                        size_t partial_pattern_len = match_sv.indices[i] - match_sv.indices[i - 1];
                        if (ANSI_COLOR_RED_LEN + ANSI_COLOR_RESET_LEN + partial_pattern_len >= out_buffer_size)
                        {
                            print_buffer(out_buffer, buffer_idx);
                        }
                        StringView partial_pattern = sv_from_parts(pattern + pattern_len - partial_pattern_len, partial_pattern_len);
                        *buffer_idx += (size_t)snprintf(out_buffer + *buffer_idx, out_buffer_size - *buffer_idx, "%s"SV_FMT"%s", ANSI_COLOR_RED, SV_ARG(partial_pattern), ANSI_COLOR_RESET);
                    }
                    else
                    {
                        size_t start_pos = match_sv.indices[i - 1] + pattern_len;

                        lhs = sv_from_parts(line.data + start_pos, match_sv.indices[i] - start_pos);

                        if (ANSI_COLOR_RED_LEN + ANSI_COLOR_RESET_LEN + pattern_len + lhs.size + *buffer_idx >= out_buffer_size)
                        {
                            print_buffer(out_buffer, buffer_idx);
                        }
                        *buffer_idx += (size_t)snprintf(out_buffer + *buffer_idx, out_buffer_size - *buffer_idx, SV_FMT"%s%s%s", SV_ARG(lhs), ANSI_COLOR_RED, pattern,ANSI_COLOR_RESET);
                    }
                }

                size_t start_pos = match_sv.indices[match_sv.num_matches - 1] + pattern_len;
                StringView rhs = sv_from_parts(line.data + start_pos, line.size - start_pos);

                if (rhs.size + *buffer_idx >= out_buffer_size)
                {
                    print_buffer(out_buffer, buffer_idx);
                }

                *buffer_idx += (size_t)snprintf(out_buffer + *buffer_idx, out_buffer_size - *buffer_idx, SV_FMT"\n", SV_ARG(rhs));
            }
        }
    }

    return total_matches;
}

internal size_t process_file(Arena* arena, char* file, const char* pattern, uint32_t flags)
{
    size_t arena_start = arena->pos;
    LARGE_INTEGER t1, t2;
    QueryPerformanceCounter(&t1);

    // TODO: read in loop using file_buffer
    // Should have no arena_push / pop at this point
    StringView result = sv_read_entire_file(arena, file);
    if (result.data == NULL)
    {
        printf("Failed to read file %s\n", file);
        return 0;
    }

    QueryPerformanceCounter(&t2);
    g_Stats.file_read_time += (t2.QuadPart - t1.QuadPart) * 1000.0 / g_Stats.frequency.QuadPart;

    size_t total_matches = match_pattern_in_sv(arena, g_out_buffer, OUT_BUFFER_SIZE, &g_buffer_idx, pattern, result, file, flags);

    atomic_uint32_inc(&g_Stats.files_searched);
    atomic_uint64_add(&g_Stats.bytes_searched, result.size);

    arena_pop(arena, arena->pos - arena_start);
    
    return total_matches;
}

internal size_t recurse_directory(Arena* arena, const char* dir, const char* pattern, uint32_t flags)
{
    WIN32_FIND_DATA ffd = {0};
    TCHAR szDir[MAX_PATH];
    HANDLE hFind = INVALID_HANDLE_VALUE;

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
            total_matches += process_file(arena, full_path, pattern, flags);
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

    StdOut = GetStdHandle(STD_OUTPUT_HANDLE);

    Arena* arena = arena_create((size_t)Gigabytes(4));
    g_out_buffer = ArenaPushArray(arena, char, OUT_BUFFER_SIZE);
    file_buffer = ArenaPushArray(arena, char, FILE_BUFFER_SIZE);

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

        bool flag_found = false;
        for (int i = 0; i < WGREP_NUM_FLAGS; i++)
        {
            if (strcmp(command_line_args[i].cmd, flag) == 0)
            {
                flags |= command_line_args[i].flag;
                flag_found = true;
                break;
            }
        }

        if (!flag_found)
        {
            if (flag[0] == '-')
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
                    flag_found = false;
                    for (int j = 0; j < WGREP_NUM_FLAGS; j++)
                    {
                        if (strlen(command_line_args[j].cmd) == 2)
                        {
                            if (command_line_args[j].cmd[1] == flag[i])
                            {
                                flags |= command_line_args[j].flag;
                                flag_found = true;
                                break;
                            }
                        }
                    }

                    if (!flag_found)
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
    }

    bool recurse_dirs = (flags & WGREP_OPTION_r);
    bool multi_threading = (flags & WGREP_OPTION_multi);
    bool print_count  = (flags & WGREP_OPTION_c);

    if (pattern != NULL && !_isatty(_fileno(stdin)))
    {
        char* result = NULL;
        // TODO: cleanup
        do
        {
            result = fgets(file_buffer, FILE_BUFFER_SIZE, stdin);
            StringView sv = sv_from_cstr(file_buffer);
            g_total_matches += match_pattern_in_sv(arena, g_out_buffer, OUT_BUFFER_SIZE, &g_buffer_idx, pattern, sv, NULL, flags);

            atomic_uint64_add(&g_Stats.bytes_searched, sv.size);
        }
        while (result != NULL);
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
            g_total_matches = process_file(arena, file, pattern, flags);
        }
        else if (recurse_dirs && file != NULL)
        {
            if (PathIsDirectoryA(file))
            {
                if (multi_threading)
                {
                    uint32_t logical_processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);

                    File_Queue* files_queue = ArenaPushStruct(arena, File_Queue);
                    InitializeCriticalSection(&files_queue->lock);
                    InitializeConditionVariable(&files_queue->not_empty);

                    File_Queue* free_list = ArenaPushStruct(arena, File_Queue);

                    InitializeCriticalSection(&free_list->lock);
                    InitializeConditionVariable(&free_list->not_empty);

                    atomic_grow_file_queue(arena, free_list, MAX_FILE_NODES);

                    HANDLE* thread_array = ArenaPushArray(arena, HANDLE, logical_processors);
                    DWORD* thread_ids = ArenaPushArray(arena, DWORD, logical_processors);
                    Thread_Data* thread_data = ArenaPushArray(arena, Thread_Data, logical_processors);

                    File_Node* node = atomic_queue_pop(free_list);
                    // NOTE: doing this as file might be small string and
                    // when it gets re-used later might be with larger string
                    // so allocating enough room
                    node->path = ArenaPushArray(arena, char, MAX_PATH);
                    strcpy(node->path, file);
                    atomic_queue_push(files_queue, node);

                    for (int i = 0; i < logical_processors; i++)
                    {
                        thread_data[i].index = i;
                        thread_data[i].queue = files_queue;
                        thread_data[i].free_list = free_list;
                        thread_data[i].pattern = pattern;
                        thread_data[i].flags = flags;
                        thread_array[i] = CreateThread(0, 0, thread_main, &thread_data[i], 0, &thread_ids[i]);
                    }

                    WaitForMultipleObjects(logical_processors, thread_array, TRUE, INFINITE);
                }
                else
                {
                    g_total_matches = recurse_directory(arena, file, pattern, flags);
                }
            }
            else
            {
                g_total_matches = process_file(arena, file, pattern, flags);
            }
        }
        else if (file == NULL)
        {
            g_total_matches = recurse_directory(arena, ".", pattern, flags);
        }
    }

    if (print_count)
    {
        printf("%zu\n", g_total_matches);
    }

    // TODO: Cleanup
    if (g_buffer_idx > 0)
    {
        print_buffer(g_out_buffer, &g_buffer_idx);
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
        // printf("%-24s | %10.4fms | %3d%%\n", "Total line chop time", g_Stats.line_chop_time, (int)(g_Stats.line_chop_time / elapsed_time * 100.0));
        // printf("%-24s | %10.4fms | %3d%%\n", "Total string search time", g_Stats.search_time, (int)(g_Stats.search_time / elapsed_time * 100.0));
        // printf("%-24s | %10.4fms | %3d%%\n", "Total file read time", g_Stats.file_read_time, (int)(g_Stats.file_read_time / elapsed_time * 100.0));
        // printf("%-24s | %10.4fms | %3d%%\n", "Other time", other_time, (int)(other_time / elapsed_time * 100.0));
    }

    return 0;
}

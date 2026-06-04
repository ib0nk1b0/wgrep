#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <Windows.h>
#include <Shlwapi.h>

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

#define WGREP_OPTION_n 1
#define WGREP_OPTION_o 2
#define WGREP_OPTION_r 4
#define WGREP_OPTION_c 8
#define WGREP_OPTION_H 16

static char* g_Program;

void usage(FILE* stream, const char* program)
{
    fprintf(stream, "Usage: %s [OPTIONS] Patterns [FILE]\n", program);
    fprintf(stream, "       -n    print line number\n");
    fprintf(stream, "       -o    print only matching part of line\n");
    fprintf(stream, "       -r    search directory recursively\n");
    fprintf(stream, "       -c    print only count of how many matches were found\n");
    fprintf(stream, "       -H    print file names\n");
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

size_t match_pattern_in_file(Arena* arena, const char* pattern, const char* file, uint32_t flags)
{
    size_t arena_start = arena->pos;
    StringView result = sv_read_entire_file(arena, file);
    if (result.data == NULL)
    {
        printf("Failed to read file %s\n", file);
        return 0;
    }

    size_t total_matches = 0;
    size_t line_number = 0;
    while (result.size > 0)
    {
        line_number += 1;
        StringView line = sv_chop_line(&result);

        if (line.size == 0) continue;

        size_t index = 0;
        MatchResult result = sv_contains_brute_force(arena, line, pattern);
        if (result.num_matches)
        {
            total_matches += result.num_matches;
            if (flags & WGREP_OPTION_c)
            {
                continue;
            }
            if (flags & WGREP_OPTION_H)
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
                size_t pattern_len = strlen(pattern) - 1;
                StringView lhs = sv_from_parts(line.data, result.indices[0]);
                printf(SV_FMT, SV_ARG(lhs));
                printf(ANSI_COLOR_RED "%s"ANSI_COLOR_RESET, pattern);
                for (size_t i = 1; i < result.num_matches; i++)
                {
                    size_t start_pos = result.indices[i - 1] + pattern_len + 1;
                    lhs = sv_from_parts(line.data + start_pos, result.indices[i] - start_pos);
                    printf(SV_FMT, SV_ARG(lhs));
                    printf(ANSI_COLOR_RED "%s"ANSI_COLOR_RESET, pattern);
                }
                size_t start_pos = result.indices[result.num_matches - 1] + pattern_len + 1;
                StringView rhs = sv_from_parts(line.data + start_pos, line.size - start_pos);
                printf(SV_FMT"\n", SV_ARG(rhs));
            }
        }
    }

    arena_pop(arena, arena->pos - arena_start);

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

    snprintf(szDir, MAX_PATH, "%s\\*", dir);
    // printf("recursing dir: %s\n", szDir);

    hFind = FindFirstFile(szDir, &ffd);

    size_t total_matches = 0;
    do
    {
        char full_path[MAX_PATH];
        snprintf(full_path, MAX_PATH, "%s\\%s", dir, ffd.cFileName);
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            // printf("  %s   <DIR>\n", ffd.cFileName);
            if (strcmp(ffd.cFileName, ".") != 0 && strcmp(ffd.cFileName, "..") != 0)
            if (ffd.cFileName[0] != '.')
            {
                total_matches += recurse_directory(arena, full_path, pattern, flags);
            }
        }
        else
        {
            // filesize.LowPart = ffd.nFileSizeLow;
            // filesize.HighPart = ffd.nFileSizeHigh;
            // printf("  %s   %lld bytes\n", ffd.cFileName, filesize.QuadPart);
            total_matches += match_pattern_in_file(arena, pattern, full_path, flags);
        }
    }
    while (FindNextFile(hFind, &ffd) != 0);

    return total_matches;
}

int main(int argc, char** argv)
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER t1, t2;

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&t1);

    Arena* arena = arena_create((size_t)Gigabytes(4));
    g_Program = next_cmd_line_arg(&argc, &argv);

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
        else if (flag[0] == '-')
        {
            size_t flag_len = strlen(flag);
            if (flag_len <= 2)
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
        }
        else if (file == NULL)
        {
            file = flag;
        }
    }

    bool recurse_dirs = (flags & WGREP_OPTION_r);
    bool print_count  = (flags & WGREP_OPTION_c);
    if (pattern == NULL || (recurse_dirs == false && file == NULL))
    {
        fprintf(stderr, "ERROR: Not enough arguments provided!\n");
        usage(stderr, g_Program);
        exit(1);
    }

    if (!recurse_dirs)
    {
        size_t total_matches = match_pattern_in_file(arena, pattern, file, flags);
        if (print_count)
        {
            printf("%zu\n", total_matches);
        }
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
            total_matches = match_pattern_in_file(arena, pattern, file, flags);
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

    QueryPerformanceCounter(&t2);
    double elapsed_time = (t2.QuadPart - t1.QuadPart) * 1000.0 / frequency.QuadPart;
    printf("Total elapsed time: %f ms.\n", elapsed_time);

    return 0;
}

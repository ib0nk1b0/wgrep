#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <Windows.h>

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

static char* g_Program;

void usage(FILE* stream, char* program, char* message)
{
    if (message)
    {
        fprintf(stream, "%s\n", message);
    }
    fprintf(stream, "Usage: %s [OPTIONS] Patterns [FILE]\n", program);
    fprintf(stream, "       -n    print line number\n");
    fprintf(stream, "       -o    print only matching part of line\n");
}

// returns num_matches
size_t sv_contains_brute_force(Arena* arena, StringView sv, const char* pattern, size_t* out_indices)
{
    size_t num_matches = 0;
    size_t pattern_length = strlen(pattern);
    bool match_found = false;
    if (sv.size < pattern_length)
    {
        return num_matches;
    }

    size_t i = 0;
    for (; i < sv.size - pattern_length + 1; i++)
    {
        for (size_t j = 0; j < pattern_length; j++)
        {
            if (sv.data[i + j] != pattern[j])
            {
                match_found = false;
                break;
            }
            match_found = true;
        }

        if (match_found)
        {
            match_found = false;
            if (out_indices)
            {
                out_indices[num_matches] = i;
            }
            num_matches += 1;
        }
    }

    return num_matches;
}

int main(int argc, char** argv)
{
    Arena* arena = arena_create(Megabytes(64));
    g_Program = next_cmd_line_arg(&argc, &argv);

    char* pattern = NULL;
    char* file = NULL;
    bool print_line_number = false;
    bool print_only_matching = false;
    while (argc > 0)
    {
        char* flag = next_cmd_line_arg(&argc, &argv);
        if (strcmp(flag, "-n") == 0)
        {
            print_line_number = true;
        }
        else if (strcmp(flag, "-o") == 0)
        {
            print_only_matching = true;
        }
        else if (flag[0] == '-')
        {
            usage(stderr, g_Program, "Unkown flag provided!");
            exit(1);
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

    if (pattern == NULL || file == NULL)
    {
        usage(stderr, g_Program, "Not enough arguments provided!");
        exit(1);
    }

    StringView result = sv_read_entire_file(arena, file);

    size_t line_number = 0;
    while (result.size > 0)
    {
        line_number += 1;
        StringView line = sv_chop_line(&result);

        if (line.size == 0) continue;

        size_t index = 0;
        size_t num_matches = sv_contains_brute_force(arena, line, pattern, NULL);
        size_t* indices = ArenaPushArray(arena, size_t, num_matches);
        sv_contains_brute_force(arena, line, pattern, indices);
        if (num_matches)
        {
            if (print_line_number)
            {
                printf(ANSI_COLOR_GREEN"%zu:"ANSI_COLOR_RESET, line_number);
            }

            if (print_only_matching)
            {
                printf(ANSI_COLOR_RED"%s"ANSI_COLOR_RESET"\n", pattern);
            }
            else
            {
                size_t pattern_len = strlen(pattern) - 1;
                StringView lhs = sv_from_parts(line.data, indices[0]);
                printf(SV_FMT, SV_ARG(lhs));
                printf(ANSI_COLOR_RED "%s"ANSI_COLOR_RESET, pattern);
                for (size_t i = 1; i < num_matches; i++)
                {
                    size_t start_pos = indices[i - 1] + pattern_len + 1;
                    lhs = sv_from_parts(line.data + start_pos, indices[i] - start_pos);
                    printf(SV_FMT, SV_ARG(lhs));
                    printf(ANSI_COLOR_RED "%s"ANSI_COLOR_RESET, pattern);
                }
                size_t start_pos = indices[num_matches - 1] + pattern_len + 1;
                StringView rhs = sv_from_parts(line.data + start_pos, line.size - start_pos);
                printf(SV_FMT"\n", SV_ARG(rhs));
            }
        }
    }

    return 0;
}

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
}

bool sv_contains(StringView sv, const char* pattern, size_t* out_index)
{
    bool contains = false;
    size_t pattern_length = strlen(pattern);
    if (sv.size < pattern_length)
    {
        return contains;
    }
    size_t i = 0;
    for (; i < sv.size - pattern_length + 1; i++)
    {
        if (contains)
        {
            break;
        }

        for (size_t j = 0; j < pattern_length; j++)
        {
            if (sv.data[i + j] != pattern[j])
            {
                contains = false;
                break;
            }
            contains = true;
        }
    }

    *out_index = i;

    return contains;
}

int main(int argc, char** argv)
{
    Arena* arena = arena_create(Megabytes(64));
    g_Program = next_cmd_line_arg(&argc, &argv);

    char* pattern = NULL;
    char* file = NULL;
    bool print_line_number = false;
    while (argc > 0)
    {
        char* flag = next_cmd_line_arg(&argc, &argv);
        if (strcmp(flag, "-n") == 0)
        {
            print_line_number = true;
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

    printf("Searching for `%s` in file `%s`\n", pattern, file);

    StringView result = sv_read_entire_file(arena, file);

    printf("Processing lines, skipping empties...\n");

    size_t line_number = 0;
    while (result.size > 0)
    {
        line_number += 1;
        StringView line = sv_chop_line(&result);

        if (line.size == 0) continue;

        size_t index = 0;
        bool contains = sv_contains(line, pattern, &index);
        if (contains)
        {
            // printf("("SV_FMT")\n", SV_ARG(line));
            StringView lhs = sv_from_parts(line.data, index - 1);
            size_t eop = index + strlen(pattern) - 1;
            StringView rhs = sv_from_parts(line.data + eop, line.size - eop);
            if (print_line_number)
            {
                printf(ANSI_COLOR_GREEN"%zu:"ANSI_COLOR_RESET, line_number);
            }
            printf(SV_FMT ANSI_COLOR_RED "%s"ANSI_COLOR_RESET SV_FMT"\n", SV_ARG(lhs), pattern, SV_ARG(rhs));
        }
    }

    return 0;
}

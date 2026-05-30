#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <Windows.h>

#define CUTILS_IMPLEMENTATION
#define CUTILS_NO_PREFIX
#include "cutils.h"

static char* g_Program;

void usage(FILE* stream, char* program, char* message)
{
    if (message)
    {
        fprintf(stream, "%s\n", message);
    }
    fprintf(stream, "Usage: %s [OPTIONS] Patterns [FILE]\n", program);
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
            char c1 = sv.data[i + j]; 
            char c2 = pattern[j];
            // printf("Comparing %c with %c\n", c1, c2);
            if (c1 != c2)
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
    while (argc > 0)
    {
        char* flag = next_cmd_line_arg(&argc, &argv);
        if (pattern == NULL)
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
            printf("Found %s in %s at line %zu column %zu\n", pattern, file, line_number, index);
        }
    }

    return 0;
}

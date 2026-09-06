/*
 * CUTILS header only library containing common c utilities
 *
 * This is an STB style library
 *
 * Must use define CUTILS_IMPLEMENTATION before including cutils.h
 *
 * All functions have been prefixed with cutils_ to avoid potention name
 * conflicts
 *
 * There are several macros available to opt-out of this prefix on function
 * names
 *
 * i.e. CUTILS_NO_PREFIX_ARENA
 *
 * When this is defined functions such as cutils_arena_create can be called as
 * arena_create cutting down on function name length
 *
 * If you know none of the function names are going to clash with other
 * functions in your program then you can define CUTILS_NO_PREFIX
 * which in turn defines all of the CUTILS_NO_PREFIX_ macros enabling shorter
 * function names across the board
 *
 *
 * STRING VIEW
 * ---------------------------------------------------------------------------
 *  EXAMPLE of problems to watch out for
 * ---------------------------------------------------------------------------
 *  StringView sv = sv_from_cstr(cstr);
 *  StringView line;
 *  while (sv.size > 0)
 *  {
 *      line = sv_chop_line(&sv);
 *      printf("("SV_FMT")\n", SV_ARG(line));
 *  }
 * ---------------------------------------------------------------------------
 *  The above code takes a c string, makes a StringView out of it and then
 *  proceeds to chop line by line, printing each line found until the end of
 *  the string has been reached.
 * ---------------------------------------------------------------------------
 *  StringView sv = sv_from_cstr(cstr);
 *  while (sv.size > 0)
 *  {
 *      printf("("SV_FMT")\n", SV_ARG(sv_chop_line(&sv)));
 *  }
 * ---------------------------------------------------------------------------
 * It might be tempting to do the following, however, due to using the macro
 * SV_ARG() for printing the StringView, this ends up behaving in an unexpected
 * way.
 * The Macro is defined as follows:
 *
 *     #define SV_ARG(sv) (int)sv.size, sv.data
 *
 * Due to calling the function inside the macro, this gets turned into two
 * seperate calls to sv_chop_line
 *
 * This is how the code would look with the macros expanded:
 *
 *     printf("(%.*s)\n", (int)sv_chop_line(&sv).size, sv_chop_line(&sv).data);
 *
 * Its obvious why this doesn't print each line correctly when you see it like
 * this but when you look at the two examples initially they appear to be equal
 *
 */

#ifndef CUTILS_H
#define  CUTILS_H

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
    #include <Windows.h>
#else
#endif

#define Kilobytes(x) 1024 * (x)
#define Megabytes(x) 1024 * Kilobytes(x)
#define Gigabytes(x) 1024 * Megabytes(x)
#define Terabytes(x) 1024 * Gigabytes(x)

typedef struct
{
    size_t pos;
    size_t size;
} Arena;

Arena* cutils_arena_create(size_t size);
void   cutils_arena_release(Arena* arena);
void*  cutils_arena_push(Arena* arena, size_t size);
void*  cutils_arena_pop(Arena* arena, size_t size);
#define ArenaPushStruct(arena, T) (T*)cutils_arena_push(arena, sizeof(T))
#define ArenaPushArray(arena, T, n) (T*)cutils_arena_push(arena, sizeof(T) * (n))
#define ArenaPopStruct(arena, T) (T*)cutils_arena_pop(arena, sizeof(T))
#define ArenaPopArray(arena, T, n) (T*)cutils_arena_pop(arena, sizeof(T) * (n))

typedef struct
{
    const char* data;
    size_t      size;
} StringView;

#define SV_FMT "%.*s"
#define SV_ARG(sv) (int)sv.size, sv.data

StringView cutils_sv_from_cstr(const char* cstr);
StringView cutils_sv_from_parts(const char* str, size_t size);
char*      cutils_sv_make_cstr(Arena* arena, StringView sv);
StringView cutils_sv_trim(StringView sv);
StringView cutils_sv_trim_left(StringView sv);
StringView cutils_sv_trim_right(StringView sv);
StringView cutils_sv_chop_by_delim(StringView* sv, char delim);
StringView cutils_sv_chop_line(StringView* sv);

char* cutils_read_entire_file(Arena* arena, const char* filepath, size_t* outSize);
StringView cutils_sv_read_entire_file(Arena* arena, const char* filepath);

char* cutils_next_cmd_line_arg(int* argc, char*** argv);

#ifdef CUTILS_NO_PREFIX

#define CUTILS_NO_PREFIX_ARENA
#define CUTILS_NO_PREFIX_STRING_VIEW
#define CUTILS_NO_PREFIX_FILE_IO
#define CUTILS_NO_PREFIX_OTHER

#endif // CUTILS_NO_PREFIX

#ifdef CUTILS_NO_PREFIX_ARENA

#define arena_create cutils_arena_create
#define arena_release cutils_arena_release
#define arena_push cutils_arena_push
#define arena_pop cutils_arena_pop

#endif // CUTILS_NO_PREFIX_ARENA

#ifdef CUTILS_NO_PREFIX_STRING_VIEW

#define sv_from_cstr cutils_sv_from_cstr
#define sv_from_parts cutils_sv_from_parts
#define sv_make_cstr cutils_sv_make_cstr
#define sv_trim cutils_sv_trim
#define sv_trim_left cutils_sv_trim_left
#define sv_trim_right cutils_sv_trim_right
#define sv_chop_by_delim cutils_sv_chop_by_delim
#define sv_chop_line cutils_sv_chop_line

#endif // CUTILS_NO_PREFIX_STRING_VIEW

#ifdef CUTILS_NO_PREFIX_FILE_IO

#define read_entire_file cutils_read_entire_file
#define sv_read_entire_file cutils_sv_read_entire_file

#endif // CUTILS_NO_PREFIX_FILE_IO

#ifdef CUTILS_NO_PREFIX_OTHER

#define next_cmd_line_arg cutils_next_cmd_line_arg

#endif // CUTILS_NO_PREFIX_OTHER

#endif // CUTILS_H

#ifdef CUTILS_IMPLEMENTATION

// Arena ----------------------------------------------------------------------

Arena* cutils_arena_create(size_t size)
{
    Arena* arena;
    size_t totalSize = size + sizeof(Arena);
#ifdef _WIN32
    arena = (Arena*)VirtualAlloc(0, totalSize, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
#else
    arena = (Arena*)malloc(totalSize);
    memset(arena, 0, totalSize);
#endif
    arena->size = size;
    return arena;
}

void cutils_arena_release(Arena* arena)
{
    if (arena)
    {
#ifdef _WIN32
        VirtualFree(arena, 0, MEM_RELEASE);
#else
        free(arena);
#endif
    }
}

void* cutils_arena_push(Arena* arena, size_t size)
{
    assert(arena->pos + size < arena->size);
    uint8_t* out = (uint8_t*)arena + sizeof(Arena) + arena->pos;
    arena->pos += size;
    return out;
}

void* cutils_arena_pop(Arena* arena, size_t size)
{
    assert(arena->pos >= size);
    arena->pos -= size; 
    uint8_t* out = (uint8_t*)arena + sizeof(Arena) + arena->pos;
    memset(out, 0, size);
    return out;
}

// StringView -----------------------------------------------------------------

StringView cutils_sv_from_cstr(const char* cstr)
{
    return (StringView) {
        .data = cstr,
        .size = strlen(cstr),
    };
}

StringView cutils_sv_from_parts(const char* str, size_t size)
{
    return (StringView) {
        .data = str,
        .size = size,
    };
}

char* cutils_sv_make_cstr(Arena* arena, StringView sv)
{
    char* result;
    if (arena)
    {
        result = ArenaPushArray(arena, char, sv.size + 1);
    }
    else
    {
        result = malloc(sv.size + 1);
    }

    memcpy(result, sv.data, sv.size);

    return result;
}

StringView cutils_sv_trim(StringView sv)
{
    return cutils_sv_trim_left(cutils_sv_trim_right(sv));
}

StringView cutils_sv_trim_left(StringView sv)
{
    while (sv.size > 0 && isspace(sv.data[0]))
    {
        sv.data += 1;
        sv.size -= 1;
    }

    return sv;
}

StringView cutils_sv_trim_right(StringView sv)
{
    while (sv.size > 0 && isspace(sv.data[sv.size - 1]))
    {
        sv.size -= 1;
    }

    return sv;
}

StringView cutils_sv_chop_by_delim(StringView* sv, char delim)
{
    size_t i = 0;
    while (i < sv->size && sv->data[i] != delim)
    {
        i += 1;
    }

    StringView lhs = {
        .data = sv->data,
        .size = i,
    };

    if (i < sv->size)
    {
        sv->data += i + 1;
        sv->size -= (i + 1);
    }
    else
    {
        sv->data += i;
        sv->size -= i;
    }

    return lhs;
}

StringView cutils_sv_chop_line(StringView* sv)
{
    __m128i new_line = _mm_set1_epi8('\n');

    size_t size = sv->size;
    const char* data = sv->data;

    while (size >= 16)
    {
        __m128i batch = _mm_loadu_si128((__m128i*)data);
        __m128i new_line_test = _mm_cmpeq_epi8(batch, new_line);

        int check = _mm_movemask_epi8(new_line_test);

        if (check)
        {
            int advance = _tzcnt_u32(check);
            size -= advance;
            data += advance;
            break;
        }

        size -= 16;
        data += 16;
    }

    StringView line = {0};
    StringView result = {
        .data = data,
        .size = size,
    };

    size_t i = 0;
    if (result.data[0] == '\n')
    {
        line.data = sv->data;
        line.size = sv->size - result.size;
        i = line.size;
    }
    else
    {
        while (i < result.size && result.data[i] != '\n')
        {
            i += 1;
        }
        
        i += sv->size - result.size;

        line.data = sv->data;
        line.size = i;
    }

    if (line.data[line.size - 1] == '\r')
    {
        line = cutils_sv_trim_right(line);
    }

    if (i < sv->size)
    {
        sv->data += i + 1;
        sv->size -= (i + 1);
    }
    else
    {
        sv->data += i;
        sv->size -= i;
    }

    return line;
}

// FILE I/O -----------------------------------------------------------------

char* cutils_read_entire_file(Arena* arena, const char* filepath, size_t* outSize)
{
    FILE* f = fopen(filepath, "rb");
    if (f == NULL)
    {
        return NULL;
    }
    // assert(f && "Could not open file!");

    fseek(f, 0, SEEK_END);
    size_t fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* result = ArenaPushArray(arena, char, fileSize);

    *outSize = fread(result, 1, fileSize, f);

    fclose(f);

    return result;
}

StringView cutils_sv_read_entire_file(Arena* arena, const char* filepath)
{
    StringView result = {0};

    result.data = cutils_read_entire_file(arena, filepath, &result.size);

    return result;
}

// Other ----------------------------------------------------------------------

char* cutils_next_cmd_line_arg(int* argc, char*** argv)
{
    assert(argc > 0);
    char* cmd = **argv;
    *argc -= 1;
    *argv += 1;
    return cmd;
}


#endif // CUTILS_IMPLEMENTATION

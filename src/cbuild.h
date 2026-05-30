#ifndef CBUILD_H
#define CBUILD_H

#include <stdbool.h>
#include <string.h>
#include <Windows.h>
#include <Shlwapi.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

void cbuild_rebuild_self(const char* cFile, char** argv);
#define CBUILD_REBUILD_SELF(argc, argv) cbuild_rebuild_self(__FILE__, argv)

#endif // CBUILD_H

#define CBUILD_IMPLEMENTATION // NOTE: for syntax highlighting
#ifdef CBUILD_IMPLEMENTATION

void cbuild_rebuild_self(const char* cFile, char** argv)
{
    // TODO: see if there is another way to determine file exists that doesn't require linking against shlwapi
    int exists = PathFileExistsA(argv[0]);
    char* oldName = NULL;
    if (!exists)
    {
        char* exe = ".exe";

        size_t exeLen = strlen(exe);
        size_t oldNameSize = strlen(argv[0]) + exeLen + 1;

        oldName = malloc(oldNameSize);
        memset(oldName, 0, oldNameSize);
        snprintf(oldName, oldNameSize, "%s%s", argv[0], exe);

        exists = PathFileExistsA(oldName);
    }
    else
    {
        size_t oldNameSize = strlen(argv[0]) + 1;

        oldName = malloc(oldNameSize);
        memset(oldName, 0, oldNameSize);
        snprintf(oldName, oldNameSize, "%s", argv[0]);
    }

    if (oldName != NULL && exists)
    {
        struct _stat buf0;
        struct _stat buf1;
        int stat_result0 = _stat(oldName, &buf0);
        int stat_result1 = _stat(cFile, &buf1);

        if (stat_result0 != 0 || stat_result1 != 0)
        {
            printf("There was an error\n");
            exit(1);
        }
        else
        {
            if (buf0.st_mtime < buf1.st_mtime)
            {
                printf("Program needs rebuilding\n");
                char* suffix = ".old";
                size_t suffixLen = strlen(suffix);
                size_t newNameSize = strlen(argv[0]) + suffixLen + 1;

                char* newName = malloc(newNameSize);
                memset(newName, 0, newNameSize);
                snprintf(newName, newNameSize, "%s%s", argv[0], suffix);

                printf("renaming %s to %s\n", oldName, newName);
                bool result = MoveFileExA(oldName, newName, MOVEFILE_REPLACE_EXISTING);
                if (!result)
                {
                    printf("ERROR\n");
                    exit(1);
                }

                // const char* buildCommand = "gcc -o build\\cbuild cbuild.c -lshlwapi";
                const char* buildCommand = "cl /nologo /Fo.\\build\\ /Fe:build\\cbuild.exe src\\cbuild.c /link shlwapi.lib";
                printf("cbuild `%s`\n", buildCommand);
                system(buildCommand);

                printf("%s\n", argv[0]);
                system(argv[0]);
                exit(0);
            }
        }
    }
}

#endif // CBUILD_IMPLEMENTATION

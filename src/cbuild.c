#include <stdio.h>

#define CBUILD_IMPLEMENTATION
#include "./cbuild.h"

int main(int argc, char** argv)
{
    CBUILD_REBUILD_SELF(argc, argv);

    const char* buildCommand = "cl /nologo /Zi /Fo.\\build\\ /Fe:build\\wgrep.exe src\\wgrep.c /link shlwapi.lib";

    printf("%s\n", buildCommand);
    system(buildCommand);

    const char* runCommand = "build\\wgrep \"hello, world\" test\\test.txt";
    printf("%s\n", runCommand);
    printf("\n-----------------------------------------\n\n");
    system(runCommand);

    return 0;
}

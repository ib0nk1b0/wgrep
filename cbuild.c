#include <stdio.h>

#define CBUILD_IMPLEMENTATION
#include "./cbuild.h"

#define NUM_TESTS 9

static char* CompilerFlagsDebug   = "/Od /Zi /MT /nologo";
static char* CompilerFlagsRelease = "/O2 /MT /nologo";
// /nologo /O2 /Zi

int main(int argc, char** argv)
{
    CBUILD_REBUILD_SELF(argc, argv);

    // TODO: take in --debug and --release arguments to switch up which compiler flags are used

    CBuild_Cmd cmd = {0};

    cbuild_cmd_begin(&cmd, CBUILD_COMPILER_CL);

    char* CompilerFlags = CompilerFlagsDebug;
    if (argc == 2 && strcmp(argv[1], "--release") == 0)
    {
        CompilerFlags = CompilerFlagsRelease;
    }

    cbuild_cmd_append(&cmd, CompilerFlags);
    cbuild_cmd_append(&cmd, "/Fo:build\\wgrep.obj");
    cbuild_cmd_append(&cmd, "/Fe:build\\wgrep.exe");
    cbuild_cmd_append(&cmd, "src\\wgrep.c");
    cbuild_cmd_append(&cmd, "/link");
    cbuild_cmd_append(&cmd, "/DEBUG");
    cbuild_cmd_append(&cmd, "shlwapi.lib");

    cbuild_cmd_end(&cmd);

    // printf("\n-----------------------------------------\n\n");
    // const char* runCommandKMP = "build\\wgrep --kmp --perf -nH \"hello, world\" test\\test.txt";
    // printf("%s\n\n", runCommandKMP);
    // system(runCommandKMP);

    // printf("\n-----------------------------------------\n\n");
    // const char* runCommandBruteForce = "build\\wgrep --perf -nH \"AABA\" test\\bmtest.txt";
    // printf("%s\n\n", runCommandBruteForce);
    // system(runCommandBruteForce);
    //
    // printf("\n-----------------------------------------\n\n");
    // const char* runCommandBoyerMoore = "build\\wgrep --perf --bm -nH \"AABA\" test\\bmtest.txt";
    // printf("%s\n\n", runCommandBoyerMoore);
    // system(runCommandBoyerMoore);

    // NOTE: Testing
    printf("\n-----------------------------------------\n\n");
    const char* tests[NUM_TESTS] = {
        "build\\wgrep -nH \"AABA\" test\\bmtest.txt",
        "build\\wgrep --bm -nH \"AABA\" test\\bmtest.txt",
        "build\\wgrep --bm -rnH \"AABA\" test",
        "build\\wgrep --bm -rnH \"hello, world\" test",
        "build\\wgrep -rc \"hello, world\" test",
        "build\\wgrep -c \"hello, world\" test\\test.txt",
        "build\\wgrep -rc \"AABA\" test",
        "build\\wgrep --kmp -rc \"AABA\" test",
        "dir | build\\wgrep \"build\"",
    };
    for (int i = 0; i < NUM_TESTS; i++)
    {
        printf("\n%s\n", tests[i]);
        system(tests[i]);
    }

    return 0;
}

/*
 * m2022-airbridge command line entry point.
 *
 * Subcommands are added milestone by milestone (see SPEC.md section 6.9).
 * M1 scaffold: version, help.
 */
#include "m2022/version.h"

#include <stdio.h>
#include <string.h>

static int usage(FILE *out)
{
    fputs("Usage: m2022-airbridge <command> [options]\n"
          "\n"
          "Commands:\n"
          "  version   print the version and exit\n"
          "  help      show this help\n",
          out);
    return out == stdout ? 0 : 2;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        return usage(stderr);
    }
    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("m2022-airbridge %s\n", m2022_version_string());
        return 0;
    }
    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "-h") == 0) {
        return usage(stdout);
    }
    fprintf(stderr, "m2022-airbridge: unknown command '%s'\n", argv[1]);
    return usage(stderr);
}

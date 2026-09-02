#include "m2022/version.h"

#include <stdio.h>

#ifndef M2022_VERSION_STRING
#define M2022_VERSION_STRING "0.0.0"
#endif

const char *m2022_version_string(void)
{
    return M2022_VERSION_STRING;
}

void m2022_version_components(int *major, int *minor, int *patch)
{
    int a = 0, b = 0, c = 0;
    if (sscanf(M2022_VERSION_STRING, "%d.%d.%d", &a, &b, &c) != 3) {
        a = b = c = 0;
    }
    if (major) {
        *major = a;
    }
    if (minor) {
        *minor = b;
    }
    if (patch) {
        *patch = c;
    }
}

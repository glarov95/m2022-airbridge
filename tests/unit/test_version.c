#include "m2022/version.h"

#include "m2022_test.h"

int main(void)
{
    int major = -1, minor = -1, patch = -1;

    CHECK(m2022_version_string() != NULL);
    CHECK(strlen(m2022_version_string()) >= 5);

    m2022_version_components(&major, &minor, &patch);
    CHECK(major >= 0);
    CHECK(minor >= 0);
    CHECK(patch >= 0);

    /* NULL outputs must be tolerated. */
    m2022_version_components(NULL, NULL, NULL);

    TEST_MAIN_END();
}

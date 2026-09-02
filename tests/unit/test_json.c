#include "m2022/json.h"

#include "m2022_test.h"

static void check_json(const char *in, const char *expected)
{
    char buf[256];
    FILE *f = fmemopen(buf, sizeof buf, "w");
    m2022_json_string(f, in);
    fclose(f);
    CHECK_EQ_STR(buf, expected);
}

int main(void)
{
    check_json("plain", "\"plain\"");
    check_json("q\"uote\\slash", "\"q\\\"uote\\\\slash\"");
    check_json("a\nb\tc\r", "\"a\\nb\\tc\\r\"");
    check_json("\x01", "\"\\u0001\"");
    check_json("", "\"\"");
    check_json(NULL, "null");
    check_json("M2020 Series", "\"M2020 Series\"");
    TEST_MAIN_END();
}

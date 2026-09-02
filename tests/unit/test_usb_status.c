#include "m2022/usb.h"

#include "m2022_test.h"

static size_t reasons_of(uint8_t st, const char **r)
{
    return m2022_usb_status_reasons(st, r, 4);
}

int main(void)
{
    const char *r[4];

    /* selected + no-error, paper present: ready */
    CHECK_EQ_INT(reasons_of(0x18, r), 0);
    /* paper empty */
    CHECK_EQ_INT(reasons_of(0x38, r), 1);
    CHECK_EQ_STR(r[0], "media-empty");
    /* not selected */
    CHECK_EQ_INT(reasons_of(0x08, r), 1);
    CHECK_EQ_STR(r[0], "offline");
    /* error asserted */
    CHECK_EQ_INT(reasons_of(0x10, r), 1);
    CHECK_EQ_STR(r[0], "other");
    /* everything wrong at once, in a stable order */
    CHECK_EQ_INT(reasons_of(0x20, r), 3);
    CHECK_EQ_STR(r[0], "media-empty");
    CHECK_EQ_STR(r[1], "offline");
    CHECK_EQ_STR(r[2], "other");
    /* max smaller than the count still reports the count */
    CHECK_EQ_INT(m2022_usb_status_reasons(0x20, r, 1), 3);

    TEST_MAIN_END();
}

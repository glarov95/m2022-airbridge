#include "m2022/usb.h"

#include "m2022_test.h"

/* The real device ID of the target printer, as returned by the CUPS usb backend. */
static const char *M2020_ID = "SERN:ZF45B8GF3C01YSD;MFG:Samsung;CMD:SPL,URF,FWV,PIC,EXT,DCU;"
                              "MDL:M2020 Series;CLS:PRINTER;CID:SA_SPLV3_BW;MODE:SPL3,R000105;"
                              "STATUS:MPS;";

int main(void)
{
    m2022_ieee1284_field_t f[16];
    char out[64];
    size_t n;

    n = m2022_ieee1284_parse(M2020_ID, f, 16);
    CHECK_EQ_INT(n, 8);
    CHECK(f[0].key_len == 4 && memcmp(f[0].key, "SERN", 4) == 0);
    CHECK(f[0].value_len == 15 && memcmp(f[0].value, "ZF45B8GF3C01YSD", 15) == 0);
    CHECK(f[7].key_len == 6 && memcmp(f[7].key, "STATUS", 6) == 0);

    CHECK(m2022_ieee1284_get(M2020_ID, "MFG", out, sizeof out));
    CHECK_EQ_STR(out, "Samsung");
    CHECK(m2022_ieee1284_get(M2020_ID, "manufacturer", out, sizeof out)); /* alias, any case */
    CHECK_EQ_STR(out, "Samsung");
    CHECK(m2022_ieee1284_get(M2020_ID, "MDL", out, sizeof out));
    CHECK_EQ_STR(out, "M2020 Series");
    CHECK(m2022_ieee1284_get(M2020_ID, "MODEL", out, sizeof out));
    CHECK_EQ_STR(out, "M2020 Series");
    CHECK(m2022_ieee1284_get(M2020_ID, "SN", out, sizeof out)); /* SERN alias */
    CHECK_EQ_STR(out, "ZF45B8GF3C01YSD");
    CHECK(m2022_ieee1284_get(M2020_ID, "CMD", out, sizeof out));
    CHECK_EQ_STR(out, "SPL,URF,FWV,PIC,EXT,DCU");
    CHECK(m2022_ieee1284_get(M2020_ID, "CLASS", out, sizeof out));
    CHECK_EQ_STR(out, "PRINTER");
    CHECK(m2022_ieee1284_get(M2020_ID, "CID", out, sizeof out));
    CHECK_EQ_STR(out, "SA_SPLV3_BW");
    CHECK(!m2022_ieee1284_get(M2020_ID, "DES", out, sizeof out));
    CHECK_EQ_STR(out, "");

    /* Whitespace, missing trailing semicolon, empty value, no colon, truncation. */
    n = m2022_ieee1284_parse(" MFG : Acme ; MDL:X 1; EMPTY:;NOCOLON;LAST:end", f, 16);
    CHECK_EQ_INT(n, 4);
    CHECK(f[0].key_len == 3 && memcmp(f[0].key, "MFG", 3) == 0);
    CHECK(f[0].value_len == 4 && memcmp(f[0].value, "Acme", 4) == 0);
    CHECK(f[1].value_len == 3 && memcmp(f[1].value, "X 1", 3) == 0);
    CHECK(f[2].value_len == 0);
    CHECK(f[3].value_len == 3 && memcmp(f[3].value, "end", 3) == 0);
    CHECK_EQ_INT(m2022_ieee1284_parse("", f, 16), 0);
    CHECK_EQ_INT(m2022_ieee1284_parse(";;;", f, 16), 0);
    CHECK_EQ_INT(m2022_ieee1284_parse("A:1;B:2;C:3", f, 2), 3); /* count exceeds max */
    CHECK(m2022_ieee1284_get("MDL:Very long model name;", "MDL", out, 5));
    CHECK_EQ_STR(out, "Very");

    TEST_MAIN_END();
}

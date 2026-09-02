#include "m2022/qpdl.h"

#include "m2022_test.h"

/*
 * The worked example from the SpliX SPL2 document, with its 1-based "pointer N" prose turned
 * into the 0-based indices the vendor stream uses (verified on the fixtures).
 */
static const uint8_t RAW[19] = {0x01, 0x04, 0x03, 0x06, 0x08, 0x0F, 0x0F, 0x0F, 0x0F, 0x04,
                                0x02, 0x05, 0x08, 0x01, 0x06, 0x03, 0x06, 0x01, 0x06};
static const uint8_t STREAM[] = {
    0x05, 0x0F, 0x0F, 0x01, 0x04, 0x03, 0x06, /* 6 literals */
    0x80, 0x03,                               /* match len 3, table[3] = 5 */
    0x00, 0x05,                               /* 1 literal */
    0x84, 0x03,                               /* match len 7, table[3] = 5 */
    0x81, 0x00,                               /* match len 4, table[0] = 1 */
};
static const uint8_t EXPECTED_TAIL[21] = {0x0F, 0x0F, 0x01, 0x04, 0x03, 0x06, 0x0F,
                                          0x01, 0x04, 0x05, 0x06, 0x0F, 0x01, 0x04,
                                          0x05, 0x06, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F};

/* Build a payload: magic + raw_len + table + raw + stream + checksum. Returns its length. */
static size_t build(uint8_t *p, bool le, const uint16_t *table, const uint8_t *raw,
                    size_t raw_len, const uint8_t *stream, size_t stream_len, int32_t sum_delta)
{
    size_t n = 0;
    uint32_t sum = 0;
    uint32_t magic = M2022_CODEC11_MAGIC;
    uint32_t rl = (uint32_t)raw_len;

    for (int i = 0; i < 4; i++) {
        p[n++] = (uint8_t)(le ? magic >> (8 * i) : magic >> (24 - 8 * i));
    }
    for (int i = 0; i < 4; i++) {
        p[n++] = (uint8_t)(le ? rl >> (8 * i) : rl >> (24 - 8 * i));
    }
    for (int i = 0; i < 64; i++) {
        uint16_t t = table[i];
        p[n++] = (uint8_t)(le ? t : t >> 8);
        p[n++] = (uint8_t)(le ? t >> 8 : t);
    }
    memcpy(p + n, raw, raw_len);
    n += raw_len;
    memcpy(p + n, stream, stream_len);
    n += stream_len;
    for (size_t i = 0; i < n; i++) {
        sum += p[i];
    }
    sum = (uint32_t)((int32_t)sum + sum_delta);
    p[n++] = (uint8_t)(sum >> 24);
    p[n++] = (uint8_t)(sum >> 16);
    p[n++] = (uint8_t)(sum >> 8);
    p[n++] = (uint8_t)sum;
    return n;
}

int main(void)
{
    uint16_t table[64] = {1, 3, 4, 5};
    uint8_t payload[512], out[256];
    size_t n, out_len;
    m2022_codec11_info_t info;
    int rc;

    /* the documented example, little-endian header */
    n = build(payload, true, table, RAW, sizeof RAW, STREAM, sizeof STREAM, 0);
    rc = m2022_codec11_decode(payload, n, out, sizeof out, &out_len, &info);
    CHECK_EQ_INT(rc, 0);
    CHECK_EQ_INT(out_len, 40);
    CHECK_MEM_EQ(out, RAW, 19);
    CHECK_MEM_EQ(out + 19, EXPECTED_TAIL, 21);
    CHECK(info.little_endian);
    CHECK_EQ_INT(info.raw_len, 19);
    CHECK_EQ_INT(info.table[3], 5);
    CHECK_EQ_INT(info.literal_tokens, 2);
    CHECK_EQ_INT(info.match_tokens, 3);
    CHECK_EQ_INT(info.max_literal, 6);
    CHECK_EQ_INT(info.max_match, 7);
    CHECK_EQ_INT(info.max_index, 3);
    CHECK_EQ_INT(info.stream_len, sizeof STREAM);
    CHECK(info.checksum_stored == info.checksum_computed);

    /* same data with a big-endian header decodes identically */
    n = build(payload, false, table, RAW, sizeof RAW, STREAM, sizeof STREAM, 0);
    rc = m2022_codec11_decode(payload, n, out, sizeof out, &out_len, &info);
    CHECK_EQ_INT(rc, 0);
    CHECK_EQ_INT(out_len, 40);
    CHECK(!info.little_endian);
    CHECK_MEM_EQ(out + 19, EXPECTED_TAIL, 21);

    /* checksum off by one: fully decoded, but reported */
    n = build(payload, true, table, RAW, sizeof RAW, STREAM, sizeof STREAM, 1);
    rc = m2022_codec11_decode(payload, n, out, sizeof out, &out_len, &info);
    CHECK_EQ_INT(rc, M2022_QPDL_ECHECKSUM);
    CHECK_EQ_INT(out_len, 40);
    CHECK(info.checksum_stored == info.checksum_computed + 1);

    /* NULL info is allowed */
    n = build(payload, true, table, RAW, sizeof RAW, STREAM, sizeof STREAM, 0);
    CHECK_EQ_INT(m2022_codec11_decode(payload, n, out, sizeof out, &out_len, NULL), 0);

    /* bad magic */
    payload[0] ^= 0xff;
    CHECK_EQ_INT(m2022_codec11_decode(payload, n, out, sizeof out, &out_len, &info),
                 M2022_QPDL_EMAGIC);
    payload[0] ^= 0xff;

    /* too short for the header */
    CHECK_EQ_INT(m2022_codec11_decode(payload, 100, out, sizeof out, &out_len, &info),
                 M2022_QPDL_ETRUNCATED);

    /* stream cut inside the last match token */
    {
        uint8_t cut[512];
        size_t m = build(cut, true, table, RAW, sizeof RAW, STREAM, sizeof STREAM - 1, 0);
        rc = m2022_codec11_decode(cut, m, out, sizeof out, &out_len, &info);
        CHECK_EQ_INT(rc, M2022_QPDL_ETRUNCATED);
        CHECK_EQ_INT(out_len, 36); /* everything before the broken token */
    }

    /* literal count running past the stream */
    {
        static const uint8_t bad[] = {0x10, 0xAA}; /* claims 17 literals, has 1 */
        n = build(payload, true, table, RAW, sizeof RAW, bad, sizeof bad, 0);
        CHECK_EQ_INT(m2022_codec11_decode(payload, n, out, sizeof out, &out_len, &info),
                     M2022_QPDL_ETRUNCATED);
        CHECK_EQ_INT(out_len, 19);
    }

    /* match offset before the start of the output */
    {
        static const uint8_t bad[] = {0x80, 0x05}; /* table[5] == 0 */
        uint16_t t2[64] = {1, 3, 4, 5, 0, 0};
        n = build(payload, true, t2, RAW, 2, bad, sizeof bad, 0);
        CHECK_EQ_INT(m2022_codec11_decode(payload, n, out, sizeof out, &out_len, &info),
                     M2022_QPDL_EOFFSET);
        t2[5] = 3; /* reaches 3 back with only 2 bytes written */
        n = build(payload, true, t2, RAW, 2, bad, sizeof bad, 0);
        CHECK_EQ_INT(m2022_codec11_decode(payload, n, out, sizeof out, &out_len, &info),
                     M2022_QPDL_EOFFSET);
    }

    /* output buffer too small */
    n = build(payload, true, table, RAW, sizeof RAW, STREAM, sizeof STREAM, 0);
    CHECK_EQ_INT(m2022_codec11_decode(payload, n, out, 30, &out_len, &info),
                 M2022_QPDL_EOVERFLOW);
    CHECK_EQ_INT(out_len, 29); /* 19 raw + 6 literals + match 3 + 1 literal; the 7-match overflows */
    CHECK_EQ_INT(m2022_codec11_decode(payload, n, out, 10, &out_len, &info),
                 M2022_QPDL_EOVERFLOW);

    /* raw length above 128 */
    {
        uint8_t big[200];
        memset(big, 0x5a, sizeof big);
        n = build(payload, true, table, big, 129, STREAM, 0, 0);
        CHECK_EQ_INT(m2022_codec11_decode(payload, n, out, sizeof out, &out_len, &info),
                     M2022_QPDL_ERAWLEN);
    }

    /* longest possible match (514) and a 128-byte literal run, overlapping copy of one byte */
    {
        uint8_t raw1[1] = {0xAB};
        uint8_t lit[129];
        uint8_t big_out[1024];
        uint8_t stream2[2 + 129];
        uint16_t t3[64] = {1};
        lit[0] = 0x7F; /* 128 literals follow */
        for (int i = 0; i < 128; i++) {
            lit[1 + i] = (uint8_t)i;
        }
        memcpy(stream2, lit, 129);
        stream2[129] = 0xFF; /* length = 127 + (0xC0 << 1) + 3 = 514 */
        stream2[130] = 0xC0; /* index 0 */
        n = build(payload, true, t3, raw1, 1, stream2, sizeof stream2, 0);
        rc = m2022_codec11_decode(payload, n, big_out, sizeof big_out, &out_len, &info);
        CHECK_EQ_INT(rc, 0);
        CHECK_EQ_INT(out_len, 1 + 128 + 514);
        CHECK_EQ_INT(info.max_literal, 128);
        CHECK_EQ_INT(info.max_match, 514);
        CHECK_EQ_INT(big_out[1 + 128 + 513], 127); /* repeats the last literal */
    }

    /* band layout round trip and orientation */
    {
        enum { BPR = 5 };
        uint8_t rows[128 * BPR], band[128 * BPR], back[128 * BPR];
        for (size_t i = 0; i < sizeof rows; i++) {
            rows[i] = (uint8_t)(i * 37 + 11);
        }
        m2022_qpdl_rows_to_band(rows, BPR, band);
        /* byte k of line l lives at band[k * 128 + l], inverted */
        CHECK_EQ_INT(band[3 * 128 + 7], (uint8_t)~rows[7 * BPR + 3]);
        CHECK_EQ_INT(band[0], (uint8_t)~rows[0]);
        m2022_qpdl_band_to_rows(band, BPR, back);
        CHECK_MEM_EQ(back, rows, sizeof rows);
    }

    TEST_MAIN_END();
}

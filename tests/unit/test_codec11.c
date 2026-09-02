#include "m2022/qpdl.h"

#include "m2022_test.h"

#include <stdlib.h>

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

/* ---- encoder ------------------------------------------------------------------------- */

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint8_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint8_t)(rng_state >> 24);
}

/* Encode, decode, compare; returns the payload length. */
static size_t round_trip(const uint8_t *band, size_t len, const uint16_t *table,
                         m2022_codec11_info_t *info)
{
    size_t cap = m2022_codec11_encode_bound(len), n = 0, out_len = 0;
    uint8_t *enc = malloc(cap), *dec = malloc(len + 1);
    CHECK_EQ_INT(m2022_codec11_encode(band, len, table, enc, cap, &n), 0);
    CHECK(n <= cap);
    CHECK_EQ_INT(m2022_codec11_decode(enc, n, dec, len, &out_len, info), 0);
    CHECK_EQ_INT(out_len, len);
    if (len > 0) {
        CHECK_MEM_EQ(dec, band, len);
    }
    free(enc);
    free(dec);
    return n;
}

static void table_is_valid(const uint16_t *table)
{
    for (size_t i = 0; i < 64; i++) {
        CHECK(table[i] != 0);
        for (size_t k = 0; k < i; k++) {
            CHECK(table[i] != table[k]);
        }
    }
}

static void encoder_tests(void)
{
    m2022_codec11_info_t info;
    uint16_t table[64];
    size_t n;

    /* the bound: header, every byte, one count byte per 128 literals, checksum */
    CHECK_EQ_INT(m2022_codec11_encode_bound(0), 140);
    CHECK_EQ_INT(m2022_codec11_encode_bound(128), 136 + 128 + 1 + 4);
    CHECK_EQ_INT(m2022_codec11_encode_bound(129), 136 + 129 + 2 + 4);

    table_is_valid(m2022_codec11_default_table);
    CHECK_EQ_INT(m2022_codec11_default_table[0], 1);

    /* header layout: little-endian magic, raw length and table, like the vendor's */
    {
        uint8_t band[300], enc[512];
        memset(band, 0xFF, sizeof band);
        memcpy(table, m2022_codec11_default_table, sizeof table);
        table[5] = 0x1234;
        CHECK_EQ_INT(m2022_codec11_encode(band, sizeof band, table, enc, sizeof enc, &n), 0);
        CHECK_EQ_INT(enc[0], 0xEF);
        CHECK_EQ_INT(enc[1], 0xCD);
        CHECK_EQ_INT(enc[2], 0xAB);
        CHECK_EQ_INT(enc[3], 0x09);
        CHECK_EQ_INT(enc[4], 64); /* white matches at distance 1 right after the minimum raw */
        CHECK_EQ_INT(enc[5] | enc[6] | enc[7], 0);
        CHECK_EQ_INT(enc[8 + 2 * 5], 0x34);
        CHECK_EQ_INT(enc[8 + 2 * 5 + 1], 0x12);
        n = round_trip(band, sizeof band, table, &info);
        CHECK(info.little_endian);
        CHECK_EQ_INT(info.raw_len, 64);
        CHECK_MEM_EQ(info.table, table, sizeof table);
        /* 64 raw, one match of 236 (2 bytes) */
        CHECK_EQ_INT(n, 136 + 64 + 2 + 4);
        CHECK_EQ_INT(info.match_tokens, 1);
        CHECK_EQ_INT(info.literal_tokens, 0);
    }

    /* an all-white A4 band: 64 raw bytes, then matches of the maximum length */
    {
        enum { LEN = 608 * 128 };
        uint8_t *band = malloc(LEN);
        size_t tokens = (LEN - 64 + 513) / 514; /* 152 */
        memset(band, 0xFF, LEN);
        n = round_trip(band, LEN, m2022_codec11_default_table, &info);
        CHECK_EQ_INT(info.raw_len, 64);
        CHECK_EQ_INT(info.match_tokens, tokens);
        CHECK_EQ_INT(info.literal_tokens, 0);
        CHECK_EQ_INT(info.max_match, 514);
        CHECK_EQ_INT(n, 136 + 64 + 2 * tokens + 4);
        memset(band, 0x00, LEN); /* all black: same shape */
        CHECK_EQ_INT(round_trip(band, LEN, m2022_codec11_default_table, &info), n);
        free(band);
    }

    /* nothing ever matches when every distance exceeds the band: all literals, 128 raw */
    {
        enum { LEN = 4096 };
        uint8_t band[LEN];
        for (size_t i = 0; i < 64; i++) {
            table[i] = (uint16_t)(5000 + i);
        }
        for (size_t i = 0; i < LEN; i++) {
            band[i] = rnd();
        }
        n = round_trip(band, LEN, table, &info);
        CHECK_EQ_INT(info.raw_len, 128);
        CHECK_EQ_INT(info.match_tokens, 0);
        CHECK_EQ_INT(info.literal_tokens, (LEN - 128) / 128);
        CHECK_EQ_INT(info.max_literal, 128);
        CHECK_EQ_INT(n, 136 + LEN + (LEN - 128) / 128 + 4);
        CHECK_EQ_INT(n, m2022_codec11_encode_bound(LEN) - 1);
    }

    /* the raw prefix ends where the first match begins, clamped to 64..128; zero table
     * entries are skipped */
    {
        uint8_t band[400];
        memset(table, 0, sizeof table);
        table[7] = 1; /* the only usable distance; a match needs 4 equal bytes in a row */
        for (size_t i = 0; i < sizeof band; i++) {
            band[i] = (uint8_t)(i & 1); /* alternating: never matches at distance 1 */
        }
        memset(band + 99, 0xAA, 4); /* first match at 100 */
        n = round_trip(band, sizeof band, table, &info);
        CHECK_EQ_INT(info.raw_len, 100);
        CHECK_EQ_INT(info.max_index, 7);
        CHECK_EQ_INT(info.match_tokens, 1);
        CHECK_EQ_INT(info.max_match, 3);

        for (size_t i = 0; i < sizeof band; i++) {
            band[i] = (uint8_t)(i & 1);
        }
        memset(band + 130, 0xAA, 5); /* first match at 131: raw stops at 128, 3 literals */
        n = round_trip(band, sizeof band, table, &info);
        CHECK_EQ_INT(info.raw_len, 128);
        CHECK_EQ_INT(info.literal_tokens, 4); /* 128..130, then 265 bytes as 128+128+9 */
        CHECK_EQ_INT(info.match_tokens, 1);
        CHECK_EQ_INT(info.max_match, 4);

        for (size_t i = 0; i < sizeof band; i++) {
            band[i] = (uint8_t)(i & 1);
        }
        memset(band + 20, 0xAA, 60); /* run from 20 to 79: raw never shrinks below 64 */
        n = round_trip(band, sizeof band, table, &info);
        CHECK_EQ_INT(info.raw_len, 64);
        CHECK_EQ_INT(info.max_match, 16); /* 64..79 */
    }

    /* two identical byte-columns: the second one is a single match at distance 128 */
    {
        uint8_t band[256];
        for (size_t i = 0; i < 128; i++) {
            band[i] = rnd();
            band[128 + i] = band[i];
        }
        n = round_trip(band, sizeof band, m2022_codec11_default_table, &info);
        CHECK_EQ_INT(info.raw_len, 128);
        CHECK_EQ_INT(info.match_tokens, 1);
        CHECK_EQ_INT(info.max_match, 128);
        CHECK_EQ_INT(m2022_codec11_default_table[info.max_index], 128);
        CHECK_EQ_INT(n, 136 + 128 + 2 + 4);
    }

    /* degenerate lengths */
    {
        uint8_t band[100], enc[512];
        memset(band, 0xFF, sizeof band);
        CHECK_EQ_INT(round_trip(NULL, 0, m2022_codec11_default_table, &info), 140);
        CHECK_EQ_INT(info.raw_len, 0);
        CHECK_EQ_INT(round_trip(band, 10, m2022_codec11_default_table, &info), 136 + 10 + 4);
        CHECK_EQ_INT(info.raw_len, 10);
        CHECK_EQ_INT(round_trip(band, 100, m2022_codec11_default_table, &info),
                     136 + 64 + 2 + 4);
        /* output capacity: exact fits, one less does not, and *out_len says how far it got */
        CHECK_EQ_INT(m2022_codec11_encode(band, 100, m2022_codec11_default_table, enc, 206, &n),
                     0);
        CHECK_EQ_INT(n, 206);
        CHECK_EQ_INT(m2022_codec11_encode(band, 100, m2022_codec11_default_table, enc, 205, &n),
                     M2022_QPDL_EOVERFLOW);
        CHECK(n < 206);
        CHECK_EQ_INT(m2022_codec11_encode(band, 100, m2022_codec11_default_table, enc, 0, &n),
                     M2022_QPDL_EOVERFLOW);
        CHECK_EQ_INT(m2022_codec11_encode(NULL, 100, m2022_codec11_default_table, enc, 512, &n),
                     M2022_QPDL_EINVAL);
        CHECK_EQ_INT(m2022_codec11_encode(band, 100, NULL, enc, 512, &n), M2022_QPDL_EINVAL);
        CHECK_EQ_INT(m2022_codec11_encode(band, 100, m2022_codec11_default_table, NULL, 512,
                                          &n),
                     M2022_QPDL_EINVAL);
    }

    /* table choice: a repeated 300-byte block is found at distance 300, and beats the default */
    {
        enum { BLOCK = 300, REPS = 40 };
        uint8_t band[BLOCK * REPS];
        size_t n_default, n_chosen;
        for (size_t i = 0; i < BLOCK; i++) {
            band[i] = rnd();
        }
        for (size_t r = 1; r < REPS; r++) {
            memcpy(band + r * BLOCK, band, BLOCK);
        }
        m2022_codec11_choose_table(band, sizeof band, table);
        table_is_valid(table);
        CHECK_EQ_INT(table[0], 300);
        n_chosen = round_trip(band, sizeof band, table, &info);
        n_default = round_trip(band, sizeof band, m2022_codec11_default_table, &info);
        CHECK(n_chosen < n_default);
        CHECK(n_chosen < 136 + 300 + 2 * 30 + 4); /* one block raw+literal, then matches */
    }

    /* a constant band chooses distance 1; a 7-byte vertical period chooses 7 */
    {
        uint8_t band[2000];
        static const uint8_t pattern[7] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70};
        memset(band, 0xFF, sizeof band);
        m2022_codec11_choose_table(band, sizeof band, table);
        table_is_valid(table);
        CHECK_EQ_INT(table[0], 1);
        for (size_t i = 0; i < sizeof band; i++) {
            band[i] = pattern[i % 7];
        }
        m2022_codec11_choose_table(band, sizeof band, table);
        table_is_valid(table);
        CHECK_EQ_INT(table[0], 7);
        n = round_trip(band, sizeof band, table, &info);
        CHECK(n < 136 + 128 + 2 * 5 + 4);
    }

    /* random data: no credit anywhere, the table is filled from the default; tiny inputs */
    {
        uint8_t band[512];
        for (size_t i = 0; i < sizeof band; i++) {
            band[i] = rnd();
        }
        m2022_codec11_choose_table(band, sizeof band, table);
        table_is_valid(table);
        m2022_codec11_choose_table(band, 2, table);
        CHECK_MEM_EQ(table, m2022_codec11_default_table, sizeof table);
        m2022_codec11_choose_table(band, 0, table);
        CHECK_MEM_EQ(table, m2022_codec11_default_table, sizeof table);
        round_trip(band, sizeof band, table, &info);
    }
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
    /* 19 raw + 6 literals + match 3 + 1 literal; the 7-byte match overflows */
    CHECK_EQ_INT(out_len, 29);
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

    encoder_tests();

    TEST_MAIN_END();
}

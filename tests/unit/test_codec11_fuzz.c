/*
 * Fuzz the 0x11 decoder with what the encoder produces, intact and damaged. Deterministic
 * (xorshift seed fixed), so a failure reproduces. The sanitizers in Debug builds turn any
 * out-of-bounds access into a failure; the checks here cover the contract: a valid payload
 * round-trips exactly, a damaged one returns a known error code and never writes past the
 * output buffer or reports more output than it has.
 */
#include "m2022/qpdl.h"

#include "m2022_test.h"

#include <stdlib.h>

static uint64_t rng_state = 0x2545F4914F6CDD1Dull;

static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 16);
}

/* Bands of the kinds the halftoner produces: white with sparse ink, screened patterns with
 * noise, or pure noise. */
static void make_band(uint8_t *band, size_t len, uint32_t kind)
{
    switch (kind % 4) {
    case 0: /* mostly white, a few black bytes */
        memset(band, 0xFF, len);
        for (size_t k = 0; k < len / 16 + 1; k++) {
            band[rnd() % (len ? len : 1)] = (uint8_t)rnd();
        }
        break;
    case 1: { /* a short period with occasional noise */
        size_t period = 1 + rnd() % 140;
        for (size_t i = 0; i < len; i++) {
            band[i] = i < period ? (uint8_t)rnd() : band[i - period];
            if (rnd() % 97 == 0) {
                band[i] ^= (uint8_t)(1u << (rnd() % 8));
            }
        }
        break;
    }
    case 2: /* noise */
        for (size_t i = 0; i < len; i++) {
            band[i] = (uint8_t)rnd();
        }
        break;
    default: /* runs of random length */
        for (size_t i = 0; i < len;) {
            size_t run = 1 + rnd() % 600;
            uint8_t v = (uint8_t)rnd();
            for (; run > 0 && i < len; run--, i++) {
                band[i] = v;
            }
        }
        break;
    }
}

static bool known_rc(int rc)
{
    return rc == M2022_QPDL_OK || rc == M2022_QPDL_ETRUNCATED || rc == M2022_QPDL_EMAGIC ||
           rc == M2022_QPDL_ERAWLEN || rc == M2022_QPDL_EOFFSET || rc == M2022_QPDL_EOVERFLOW ||
           rc == M2022_QPDL_ECHECKSUM;
}

int main(void)
{
    enum { MAX_LEN = 3000, ITERATIONS = 1500 };
    uint8_t *band = malloc(MAX_LEN), *enc = malloc(m2022_codec11_encode_bound(MAX_LEN) + 64),
            *dec = malloc(MAX_LEN + 8), *mut = malloc(m2022_codec11_encode_bound(MAX_LEN) + 64);
    uint16_t table[M2022_CODEC11_TABLE_ENTRIES];
    size_t n, out_len;
    int damaged = 0, damaged_ok = 0, damaged_bad = 0;

    for (uint32_t it = 0; it < ITERATIONS; it++) {
        size_t len = rnd() % (MAX_LEN + 1);
        uint32_t kind = rnd();
        int rc;

        make_band(band, len, kind);
        switch (kind / 4 % 3) {
        case 0:
            m2022_codec11_choose_table(band, len, table);
            break;
        case 1:
            memcpy(table, m2022_codec11_default_table, sizeof table);
            break;
        default: /* hostile tables: zeros, huge and duplicate entries */
            for (size_t i = 0; i < M2022_CODEC11_TABLE_ENTRIES; i++) {
                table[i] = (uint16_t)(rnd() % 4 == 0 ? 0 : rnd());
            }
            break;
        }

        /* intact: exact round trip */
        CHECK_EQ_INT(m2022_codec11_encode(band, len, table, enc, m2022_codec11_encode_bound(len),
                                          &n),
                     0);
        CHECK(n <= m2022_codec11_encode_bound(len));
        rc = m2022_codec11_decode(enc, n, dec, len, &out_len, NULL);
        CHECK_EQ_INT(rc, 0);
        CHECK_EQ_INT(out_len, len);
        if (len > 0 && (rc != 0 || out_len != len || memcmp(dec, band, len) != 0)) {
            fprintf(stderr, "iteration %u: round trip failed (len %zu, kind %u)\n", it, len,
                    kind);
            return EXIT_FAILURE;
        }

        /* a smaller output buffer than the band: never OK, never past the buffer */
        if (len > 1) {
            size_t cap = rnd() % len;
            rc = m2022_codec11_decode(enc, n, dec, cap, &out_len, NULL);
            CHECK_EQ_INT(rc, M2022_QPDL_EOVERFLOW);
            CHECK(out_len <= cap);
        }

        /* damaged copies: byte flips, truncation, extension */
        for (int m = 0; m < 4; m++) {
            size_t mlen = n;
            memcpy(mut, enc, n);
            switch (m) {
            case 0: /* flip 1..4 bytes anywhere */
                for (uint32_t f = 0, flips = 1 + rnd() % 4; f < flips; f++) {
                    mut[rnd() % n] ^= (uint8_t)(1 + rnd() % 255);
                }
                break;
            case 1: /* flip a byte in the token stream only */
                if (n > M2022_CODEC11_HEADER_LEN + M2022_CODEC11_CHECKSUM_LEN + 1) {
                    size_t at = M2022_CODEC11_HEADER_LEN +
                                rnd() % (n - M2022_CODEC11_HEADER_LEN - M2022_CODEC11_CHECKSUM_LEN);
                    mut[at] ^= (uint8_t)(1 + rnd() % 255);
                }
                break;
            case 2: /* cut */
                mlen = rnd() % (n + 1);
                break;
            default: /* append junk */
                for (size_t k = 0; k < 8; k++) {
                    mut[mlen++] = (uint8_t)rnd();
                }
                break;
            }
            damaged++;
            rc = m2022_codec11_decode(mut, mlen, dec, len, &out_len, NULL);
            CHECK(known_rc(rc));
            CHECK(out_len <= len);
            if (rc == 0) {
                damaged_ok++; /* a flip that kept the checksum: legal, only possible with junk
                                 appended? no: the checksum then covers the junk; count it */
            } else {
                damaged_bad++;
            }
        }
    }

    /* pure garbage behind a valid magic */
    for (uint32_t it = 0; it < 500; it++) {
        size_t mlen = 140 + rnd() % 400;
        int rc;
        mut[0] = 0xEF;
        mut[1] = 0xCD;
        mut[2] = 0xAB;
        mut[3] = 0x09;
        for (size_t k = 4; k < mlen; k++) {
            mut[k] = (uint8_t)rnd();
        }
        mut[4 + rnd() % 4] = 0; /* keep the raw length plausible sometimes */
        rc = m2022_codec11_decode(mut, mlen, dec, MAX_LEN, &out_len, NULL);
        CHECK(known_rc(rc));
        CHECK(out_len <= MAX_LEN);
    }

    fprintf(stderr, "%d round trips, %d damaged payloads (%d rejected, %d still consistent)\n",
            ITERATIONS, damaged, damaged_bad, damaged_ok);
    free(band);
    free(enc);
    free(dec);
    free(mut);
    TEST_MAIN_END();
}

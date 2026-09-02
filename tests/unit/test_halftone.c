#include "m2022/halftone.h"

#include "m2022_test.h"

#include <stdlib.h>

static unsigned count_black(const uint8_t *bits, uint32_t width)
{
    unsigned n = 0;
    for (uint32_t x = 0; x < width; x++) {
        n += (bits[x >> 3] >> (7 - (x & 7))) & 1u;
    }
    return n;
}

/* Halftone `rows` lines of constant ink, return the black fraction. */
static double constant_fraction(m2022_ht_method_t method, uint8_t ink_value, uint32_t width,
                                uint32_t rows)
{
    m2022_ht_params_t p;
    m2022_halftoner_t ht;
    uint8_t *ink = malloc(width), *bits = malloc((width + 7) / 8);
    void *state = malloc(m2022_halftoner_state_bytes(width));
    unsigned black = 0;

    m2022_ht_default(&p, method);
    memset(ink, ink_value, width);
    CHECK_EQ_INT(m2022_halftoner_init(&ht, &p, width, state, m2022_halftoner_state_bytes(width)), 0);
    for (uint32_t y = 0; y < rows; y++) {
        m2022_halftone_line(&ht, ink, y, bits);
        black += count_black(bits, width);
    }
    free(ink);
    free(bits);
    free(state);
    return (double)black / ((double)width * rows);
}

int main(void)
{
    const m2022_ht_method_t all[] = {M2022_HT_THRESHOLD, M2022_HT_BAYER4, M2022_HT_BAYER8,
                                     M2022_HT_CLUSTERED, M2022_HT_BLUE_NOISE,
                                     M2022_HT_FLOYD_STEINBERG};

    /* solid black and pure white are exact for every method */
    for (size_t i = 0; i < 6; i++) {
        CHECK(constant_fraction(all[i], 255, 100, 20) == 1.0);
        CHECK(constant_fraction(all[i], 0, 100, 20) == 0.0);
    }

    /* ordered dithers hit the exact level counts on constant input */
    CHECK(constant_fraction(M2022_HT_BAYER4, 128, 64, 64) == 8.0 / 16.0);
    CHECK(constant_fraction(M2022_HT_BAYER4, 64, 64, 64) == 4.0 / 16.0);
    CHECK(constant_fraction(M2022_HT_BAYER8, 64, 64, 64) == 16.0 / 64.0);
    CHECK(constant_fraction(M2022_HT_CLUSTERED, 128, 64, 64) == 32.0 / 64.0);
    /* mask and error diffusion approximate the level */
    {
        double bn = constant_fraction(M2022_HT_BLUE_NOISE, 128, 64, 64);
        double fs = constant_fraction(M2022_HT_FLOYD_STEINBERG, 128, 256, 64);
        double fs_q = constant_fraction(M2022_HT_FLOYD_STEINBERG, 64, 256, 64);
        CHECK(bn > 0.47 && bn < 0.53);
        CHECK(fs > 0.49 && fs < 0.51);
        CHECK(fs_q > 0.24 && fs_q < 0.26);
    }
    /* threshold is a hard cut at params.threshold */
    {
        m2022_ht_params_t p;
        m2022_halftoner_t ht;
        uint8_t ink[4] = {0, 127, 128, 255}, bits[1];
        m2022_ht_default(&p, M2022_HT_THRESHOLD);
        m2022_halftoner_init(&ht, &p, 4, NULL, 0);
        m2022_halftone_line(&ht, ink, 0, bits);
        CHECK_EQ_INT(bits[0], 0x30); /* 0011 0000: 128 and 255 are black */
        p.threshold = 200;
        m2022_halftoner_init(&ht, &p, 4, NULL, 0);
        m2022_halftone_line(&ht, ink, 0, bits);
        CHECK_EQ_INT(bits[0], 0x10);
    }
    /* monotonic: more ink never gives fewer black pixels, for every method */
    for (size_t i = 0; i < 6; i++) {
        double prev = -1.0;
        for (int v = 0; v <= 255; v += 15) {
            double f = constant_fraction(all[i], (uint8_t)v, 128, 64);
            CHECK(f >= prev);
            prev = f;
        }
    }
    /* determinism and odd widths: two runs identical, padding bits stay clear */
    {
        m2022_ht_params_t p;
        m2022_halftoner_t a, b;
        uint8_t ink[13], ba[2], bb[2];
        void *sa = malloc(m2022_halftoner_state_bytes(13)), *sb = malloc(m2022_halftoner_state_bytes(13));
        for (int i = 0; i < 13; i++) {
            ink[i] = (uint8_t)(i * 19);
        }
        m2022_ht_default(&p, M2022_HT_FLOYD_STEINBERG);
        m2022_halftoner_init(&a, &p, 13, sa, m2022_halftoner_state_bytes(13));
        m2022_halftoner_init(&b, &p, 13, sb, m2022_halftoner_state_bytes(13));
        for (uint32_t y = 0; y < 5; y++) {
            m2022_halftone_line(&a, ink, y, ba);
            m2022_halftone_line(&b, ink, y, bb);
            CHECK_MEM_EQ(ba, bb, 2);
            CHECK_EQ_INT(bb[1] & 0x07, 0); /* bits beyond pixel 12 never set */
        }
        /* Floyd-Steinberg needs its state buffer */
        CHECK_EQ_INT(m2022_halftoner_init(&a, &p, 13, NULL, 0), -1);
        free(sa);
        free(sb);
    }
    /* blue-noise table sanity: 4096 entries in 0..254, every level roughly 1/255 of the mask */
    {
        unsigned hist[256] = {0};
        for (int i = 0; i < 64 * 64; i++) {
            CHECK(m2022_bluenoise64[i] <= 254);
            hist[m2022_bluenoise64[i]]++;
        }
        for (int t = 0; t < 255; t++) {
            CHECK(hist[t] >= 12 && hist[t] <= 20); /* 4096 / 255 = 16.06 */
        }
    }
    /* names and presets */
    {
        m2022_ht_method_t m;
        m2022_preset_t pr;
        m2022_tone_params_t tone;
        m2022_ht_params_t hp;
        CHECK_EQ_INT(m2022_ht_method_parse("Floyd-Steinberg", &m), 0);
        CHECK_EQ_INT(m, M2022_HT_FLOYD_STEINBERG);
        CHECK_EQ_INT(m2022_ht_method_parse("nope", &m), -1);
        CHECK_EQ_STR(m2022_ht_method_name(M2022_HT_CLUSTERED), "clustered");
        CHECK_EQ_INT(m2022_preset_parse("photo", &pr), 0);
        m2022_preset(pr, &tone, &hp);
        CHECK_EQ_INT(hp.method, M2022_HT_FLOYD_STEINBERG);
        m2022_preset(M2022_PRESET_TEXT, &tone, &hp);
        CHECK_EQ_INT(hp.method, M2022_HT_THRESHOLD);
        CHECK_EQ_INT(hp.threshold, 96);
        m2022_preset(M2022_PRESET_DRAFT, &tone, &hp);
        CHECK(tone.coverage_scale < 1.0);
        CHECK_EQ_STR(m2022_preset_name(M2022_PRESET_VENDOR), "vendor");
    }
    TEST_MAIN_END();
}

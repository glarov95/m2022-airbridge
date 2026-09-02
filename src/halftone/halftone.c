#include "m2022/halftone.h"

#include <string.h>
#include <strings.h>

/* ---- ordered matrices ------------------------------------------------------------------ */

/* Bayer matrix of size n (power of two) by recursion: M(2n) = [[4M, 4M+2], [4M+3, 4M+1]]. */
static void bayer(unsigned n, uint8_t *m)
{
    m[0] = 0;
    for (unsigned s = 1; s < n; s *= 2) {
        for (unsigned y = 0; y < s; y++) {
            for (unsigned x = 0; x < s; x++) {
                uint8_t v = m[y * n + x];
                m[y * n + x] = (uint8_t)(4 * v);
                m[y * n + x + s] = (uint8_t)(4 * v + 2);
                m[(y + s) * n + x] = (uint8_t)(4 * v + 3);
                m[(y + s) * n + x + s] = (uint8_t)(4 * v + 1);
            }
        }
    }
}

/*
 * Clustered-dot screen at 45 degrees: two dot centres per cell, at (c/4, c/4) and (3c/4, 3c/4),
 * so dots sit on a checkerboard like a classic laser screen. Cells are ranked by distance to
 * the nearest centre (ties by angle), alternating between the two dots so both grow together.
 */
static void clustered(unsigned c, uint8_t *m)
{
    unsigned n = c * c;
    unsigned taken[M2022_HT_MAX_CELL * M2022_HT_MAX_CELL] = {0};
    double cx[2] = {c / 4.0, 3 * c / 4.0}, cy[2] = {c / 4.0, 3 * c / 4.0};

    for (unsigned rank = 0; rank < n; rank++) {
        unsigned dot = rank & 1;
        double best = 1e30;
        unsigned bi = 0;
        for (unsigned i = 0; i < n; i++) {
            double dx, dy, d, key;
            unsigned x = i % c, y = i / c;
            if (taken[i]) {
                continue;
            }
            /* toroidal distance to this dot's centre */
            dx = (double)x + 0.5 - cx[dot];
            dy = (double)y + 0.5 - cy[dot];
            if (dx > c / 2.0) {
                dx -= c;
            }
            if (dx < -c / 2.0) {
                dx += c;
            }
            if (dy > c / 2.0) {
                dy -= c;
            }
            if (dy < -c / 2.0) {
                dy += c;
            }
            d = dx * dx + dy * dy;
            key = d * 1000.0 + (dx * 0.37 + dy * 0.11); /* tiny deterministic tie-break */
            if (key < best) {
                best = key;
                bi = i;
            }
        }
        taken[bi] = 1;
        m[bi] = (uint8_t)rank;
    }
}

/* Scale ranks 0..levels-1 to thresholds 2..254 so that ink 255 is always black and 0 never. */
static void scale_matrix(uint8_t *m, unsigned n, unsigned levels)
{
    for (unsigned i = 0; i < n; i++) {
        m[i] = (uint8_t)(((2u * m[i] + 1u) * 256u) / (2u * levels));
    }
}

/* ---- API ------------------------------------------------------------------------------- */

void m2022_ht_default(m2022_ht_params_t *p, m2022_ht_method_t method)
{
    p->method = method;
    p->threshold = 127;
    p->cluster_cell = 8;
}

static const char *const METHOD_NAMES[] = {"threshold", "bayer4",     "bayer8",
                                           "clustered", "blue-noise", "floyd-steinberg"};

const char *m2022_ht_method_name(m2022_ht_method_t method)
{
    return (unsigned)method < 6 ? METHOD_NAMES[method] : "?";
}

int m2022_ht_method_parse(const char *name, m2022_ht_method_t *method)
{
    for (unsigned i = 0; i < 6; i++) {
        if (strcasecmp(name, METHOD_NAMES[i]) == 0) {
            *method = (m2022_ht_method_t)i;
            return 0;
        }
    }
    return -1;
}

size_t m2022_halftoner_state_bytes(uint32_t width)
{
    return 2 * ((size_t)width + 2) * sizeof(int16_t);
}

int m2022_halftoner_init(m2022_halftoner_t *ht, const m2022_ht_params_t *p, uint32_t width,
                         void *state, size_t state_bytes)
{
    memset(ht, 0, sizeof *ht);
    ht->params = *p;
    ht->width = width;
    switch (p->method) {
    case M2022_HT_BAYER4:
        ht->cell = 4;
        bayer(4, ht->matrix);
        scale_matrix(ht->matrix, 16, 16);
        break;
    case M2022_HT_BAYER8:
        ht->cell = 8;
        bayer(8, ht->matrix);
        scale_matrix(ht->matrix, 64, 64);
        break;
    case M2022_HT_CLUSTERED:
        ht->cell = p->cluster_cell >= 4 && p->cluster_cell <= M2022_HT_MAX_CELL ? p->cluster_cell : 8;
        clustered(ht->cell, ht->matrix);
        scale_matrix(ht->matrix, ht->cell * ht->cell, ht->cell * ht->cell);
        break;
    case M2022_HT_FLOYD_STEINBERG:
        if (state == NULL || state_bytes < m2022_halftoner_state_bytes(width)) {
            return -1;
        }
        ht->err_cur = state;
        ht->err_next = ht->err_cur + width + 2;
        m2022_halftoner_reset(ht);
        break;
    case M2022_HT_THRESHOLD:
    case M2022_HT_BLUE_NOISE:
        break;
    }
    return 0;
}

void m2022_halftoner_reset(m2022_halftoner_t *ht)
{
    if (ht->err_cur != NULL) {
        memset(ht->err_cur, 0, 2 * ((size_t)ht->width + 2) * sizeof(int16_t));
    }
}

static inline void set_black(uint8_t *bits, uint32_t x)
{
    bits[x >> 3] |= (uint8_t)(0x80u >> (x & 7));
}

static void floyd_steinberg(m2022_halftoner_t *ht, const uint8_t *ink, uint32_t y, uint8_t *bits)
{
    int16_t *cur = ht->err_cur, *next = ht->err_next;
    uint32_t w = ht->width;
    int reverse = (y & 1) != 0;

    memset(next, 0, ((size_t)w + 2) * sizeof(int16_t));
    for (uint32_t i = 0; i < w; i++) {
        uint32_t x = reverse ? w - 1 - i : i;
        int dir = reverse ? -1 : 1;
        int v = ink[x] + cur[x + 1];
        int out = v >= 128 ? 255 : 0;
        int err = v - out;
        if (out) {
            set_black(bits, x);
        }
        /* 7/16 ahead, 3/16 behind-below, 5/16 below, 1/16 ahead-below (serpentine-aware) */
        cur[x + 1 + (uint32_t)dir] = (int16_t)(cur[x + 1 + (uint32_t)dir] + (err * 7) / 16);
        next[x + 1 - (uint32_t)dir] = (int16_t)(next[x + 1 - (uint32_t)dir] + (err * 3) / 16);
        next[x + 1] = (int16_t)(next[x + 1] + (err * 5) / 16);
        next[x + 1 + (uint32_t)dir] = (int16_t)(next[x + 1 + (uint32_t)dir] + (err * 1) / 16);
    }
    ht->err_cur = next;
    ht->err_next = cur;
}

void m2022_halftone_line(m2022_halftoner_t *ht, const uint8_t *ink, uint32_t y, uint8_t *bits)
{
    uint32_t w = ht->width;

    memset(bits, 0, ((size_t)w + 7) / 8);
    switch (ht->params.method) {
    case M2022_HT_THRESHOLD:
        for (uint32_t x = 0; x < w; x++) {
            if (ink[x] > ht->params.threshold) {
                set_black(bits, x);
            }
        }
        break;
    case M2022_HT_BAYER4:
    case M2022_HT_BAYER8:
    case M2022_HT_CLUSTERED: {
        const uint8_t *row = ht->matrix + (y % ht->cell) * ht->cell;
        for (uint32_t x = 0; x < w; x++) {
            if (ink[x] > row[x % ht->cell]) {
                set_black(bits, x);
            }
        }
        break;
    }
    case M2022_HT_BLUE_NOISE: {
        const uint8_t *row = m2022_bluenoise64 + (y % 64) * 64;
        for (uint32_t x = 0; x < w; x++) {
            if (ink[x] > row[x % 64]) {
                set_black(bits, x);
            }
        }
        break;
    }
    case M2022_HT_FLOYD_STEINBERG:
        floyd_steinberg(ht, ink, y, bits);
        break;
    }
}

/* ---- presets --------------------------------------------------------------------------- */

static const char *const PRESET_NAMES[] = {"draft", "normal", "text", "photo", "vendor"};

void m2022_preset(m2022_preset_t preset, m2022_tone_params_t *tone, m2022_ht_params_t *ht)
{
    m2022_tone_default(tone);
    switch (preset) {
    case M2022_PRESET_DRAFT:
        m2022_ht_default(ht, M2022_HT_BAYER8);
        tone->dot_gain = 0.10;
        tone->coverage_scale = 0.75;
        break;
    case M2022_PRESET_TEXT:
        /* Threshold low in ink terms so anti-aliased edges and light hairlines survive:
         * ink 96 is about sGray 190. */
        m2022_ht_default(ht, M2022_HT_THRESHOLD);
        ht->threshold = 96;
        tone->dot_gain = 0.0;
        break;
    case M2022_PRESET_PHOTO:
        m2022_ht_default(ht, M2022_HT_FLOYD_STEINBERG);
        tone->dot_gain = 0.20;
        break;
    case M2022_PRESET_VENDOR:
        m2022_ht_default(ht, M2022_HT_CLUSTERED);
        ht->cluster_cell = 8;
        break;
    case M2022_PRESET_NORMAL:
    default:
        m2022_ht_default(ht, M2022_HT_BLUE_NOISE);
        break;
    }
}

int m2022_preset_parse(const char *name, m2022_preset_t *preset)
{
    for (unsigned i = 0; i < 5; i++) {
        if (strcasecmp(name, PRESET_NAMES[i]) == 0) {
            *preset = (m2022_preset_t)i;
            return 0;
        }
    }
    return -1;
}

const char *m2022_preset_name(m2022_preset_t preset)
{
    return (unsigned)preset < 5 ? PRESET_NAMES[preset] : "?";
}

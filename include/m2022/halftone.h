/*
 * Halftoning: ink coverage (0..255, 255 = solid) in, 1-bit rows (1 = black, MSB first) out,
 * one line at a time so a band-based pipeline never needs the whole page. Pure. (SPEC.md 6.3)
 */
#ifndef M2022_HALFTONE_H
#define M2022_HALFTONE_H

#include "m2022/raster.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    M2022_HT_THRESHOLD,       /* black when ink > threshold */
    M2022_HT_BAYER4,          /* 4x4 dispersed-dot ordered dither, 16 levels */
    M2022_HT_BAYER8,          /* 8x8 dispersed-dot ordered dither, 64 levels */
    M2022_HT_CLUSTERED,       /* 45-degree clustered-dot screen, cell x cell, like the vendor's */
    M2022_HT_BLUE_NOISE,      /* 64x64 void-and-cluster mask */
    M2022_HT_FLOYD_STEINBERG, /* error diffusion, serpentine scan */
} m2022_ht_method_t;

typedef struct {
    m2022_ht_method_t method;
    uint8_t threshold;    /* THRESHOLD only; default 127 */
    uint8_t cluster_cell; /* CLUSTERED only; 4..16, default 8 */
} m2022_ht_params_t;

#define M2022_HT_MAX_CELL 16

typedef struct {
    m2022_ht_params_t params;
    uint32_t width;
    uint8_t matrix[M2022_HT_MAX_CELL * M2022_HT_MAX_CELL]; /* ordered thresholds, scaled */
    unsigned cell;
    int16_t *err_cur;  /* error diffusion: width + 2 entries, index x + 1 */
    int16_t *err_next;
} m2022_halftoner_t;

extern const uint8_t m2022_bluenoise64[64 * 64];

void m2022_ht_default(m2022_ht_params_t *p, m2022_ht_method_t method);
const char *m2022_ht_method_name(m2022_ht_method_t method);
int m2022_ht_method_parse(const char *name, m2022_ht_method_t *method);

/* Bytes of scratch state needed for `width` (error diffusion buffers). */
size_t m2022_halftoner_state_bytes(uint32_t width);

/* Initialise for a page of `width` pixels; `state` must hold m2022_halftoner_state_bytes. */
int m2022_halftoner_init(m2022_halftoner_t *ht, const m2022_ht_params_t *p, uint32_t width,
                         void *state, size_t state_bytes);

/* Forget diffusion errors (start of a new page). */
void m2022_halftoner_reset(m2022_halftoner_t *ht);

/* Halftone one line of ink values into packed bits ((width + 7) / 8 bytes). `y` selects the
 * mask row and the serpentine direction. */
void m2022_halftone_line(m2022_halftoner_t *ht, const uint8_t *ink, uint32_t y, uint8_t *bits);

/* ---- presets (SPEC.md 7.1) ------------------------------------------------------------- */

typedef enum {
    M2022_PRESET_DRAFT,  /* Bayer 8x8, 75 % coverage */
    M2022_PRESET_NORMAL, /* blue-noise mask */
    M2022_PRESET_TEXT,   /* threshold, keeps light hairlines */
    M2022_PRESET_PHOTO,  /* Floyd-Steinberg */
    M2022_PRESET_VENDOR, /* clustered-dot 8x8, the vendor's kind of screen */
} m2022_preset_t;

void m2022_preset(m2022_preset_t preset, m2022_tone_params_t *tone, m2022_ht_params_t *ht);
int m2022_preset_parse(const char *name, m2022_preset_t *preset);
const char *m2022_preset_name(m2022_preset_t preset);

#endif /* M2022_HALFTONE_H */

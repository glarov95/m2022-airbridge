/*
 * Raster ingest: turn what clients send (sGray 8-bit, 1-bit black, sRGB, or CUPS "K" 8-bit)
 * into 8-bit sGray lines, apply a tone curve that yields ink coverage, and fit lines to the
 * printable area. Pure: memory in, memory out, no allocation. (SPEC.md 6.2)
 *
 * Conventions: "gray" is sGray, 0 = black, 255 = white (what PWG/Apple Raster carry).
 * "ink" is coverage, 0 = no toner, 255 = solid black (what the halftoners consume).
 */
#ifndef M2022_RASTER_H
#define M2022_RASTER_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    M2022_PIX_SGRAY_8,  /* 1 byte per pixel, 0 = black (PWG sgray_8, Apple W8) */
    M2022_PIX_BLACK_8,  /* 1 byte per pixel, 255 = black (CUPS colour space K, cgpdftoraster) */
    M2022_PIX_BLACK_1,  /* 1 bit per pixel, MSB first, 1 = black (PWG black_1) */
    M2022_PIX_SRGB_24,  /* 3 bytes per pixel (PWG srgb_8, Apple SRGB24) */
} m2022_pixel_format_t;

/* Bytes per line for `width` pixels in `fmt`. */
size_t m2022_raster_line_bytes(m2022_pixel_format_t fmt, uint32_t width);

/* Convert one line to sGray. `gray` must hold `width` bytes. */
void m2022_raster_line_to_gray(m2022_pixel_format_t fmt, const uint8_t *line, uint32_t width,
                               uint8_t *gray);

/* sRGB transfer functions on 8-bit values; luminance in [0,1]. */
double m2022_srgb_to_linear(uint8_t v);
uint8_t m2022_linear_to_srgb(double y);

/* ---- tone curve ------------------------------------------------------------------------ */

typedef struct {
    double gamma;          /* applied to linear darkness; 1.0 = coverage equals darkness */
    double dot_gain;       /* expected gain at 50 % coverage, 0..0.5; compensated for */
    double coverage_scale; /* multiplies the result; < 1 saves toner (Draft) */
    uint8_t black_point;   /* gray <= this prints solid black */
    uint8_t white_point;   /* gray >= this prints nothing */
} m2022_tone_params_t;

void m2022_tone_default(m2022_tone_params_t *p);

/* Build the 256-entry LUT mapping sGray to ink for these parameters. */
void m2022_tone_build(const m2022_tone_params_t *p, uint8_t lut[256]);

/* Apply a LUT to a line. */
void m2022_tone_apply(const uint8_t lut[256], const uint8_t *gray, uint32_t width, uint8_t *ink);

/* ---- geometry ------------------------------------------------------------------------- */

/* Copy `out_width` pixels starting at source x `src_x0` (may be negative), filling pixels outside
 * the source with `fill`. */
void m2022_raster_fit_line(const uint8_t *in, uint32_t in_width, int32_t src_x0, uint8_t *out,
                           uint32_t out_width, uint8_t fill);

/* Double a line's width with linear interpolation (300 dpi input on a 600 dpi engine). */
void m2022_raster_upscale2x_line(const uint8_t *in, uint32_t in_width, uint8_t *out);

/* ---- CUPS raster stream header (v2/v3, either byte order) ------------------------------ */

typedef struct {
    uint32_t width, height;
    uint32_t bits_per_color, bits_per_pixel, bytes_per_line;
    uint32_t color_space;   /* cupsColorSpace: 0 W, 3 K, 18 sGray, 19 sRGB, ... */
    uint32_t x_dpi, y_dpi;
    uint32_t num_copies;
    int little_endian;
    int compressed;         /* v2 "RaS2": run-length encoded pages (not supported yet) */
    char page_size_name[64];
} m2022_cupsraster_header_t;

#define M2022_CUPSRASTER_HEADER_BYTES 1800 /* 4-byte sync + cups_page_header2_t */

/* Parse the sync word and header at `data`. Returns 0 or a negative error. */
int m2022_cupsraster_parse_header(const uint8_t *data, size_t len, m2022_cupsraster_header_t *h);

/* Map a parsed header to a pixel format; returns -1 when unsupported. */
int m2022_cupsraster_pixel_format(const m2022_cupsraster_header_t *h, m2022_pixel_format_t *fmt);

#endif /* M2022_RASTER_H */

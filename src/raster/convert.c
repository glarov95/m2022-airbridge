#include "m2022/raster.h"

#include <math.h>
#include <string.h>

/* sRGB electro-optical transfer function, IEC 61966-2-1. */
double m2022_srgb_to_linear(uint8_t v)
{
    double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

uint8_t m2022_linear_to_srgb(double y)
{
    double c;
    if (y <= 0.0) {
        return 0;
    }
    if (y >= 1.0) {
        return 255;
    }
    c = y <= 0.0031308 ? y * 12.92 : 1.055 * pow(y, 1.0 / 2.4) - 0.055;
    return (uint8_t)lrint(c * 255.0);
}

static double LINEAR[256];
static uint8_t ENCODE[4097]; /* linear luminance * 4096 -> sGray */
static int tables_ready;

static void init_tables(void)
{
    if (tables_ready) {
        return;
    }
    for (int i = 0; i < 256; i++) {
        LINEAR[i] = m2022_srgb_to_linear((uint8_t)i);
    }
    for (int i = 0; i <= 4096; i++) {
        ENCODE[i] = m2022_linear_to_srgb(i / 4096.0);
    }
    tables_ready = 1;
}

size_t m2022_raster_line_bytes(m2022_pixel_format_t fmt, uint32_t width)
{
    switch (fmt) {
    case M2022_PIX_BLACK_1:
        return ((size_t)width + 7) / 8;
    case M2022_PIX_SRGB_24:
        return (size_t)width * 3;
    default:
        return width;
    }
}

void m2022_raster_line_to_gray(m2022_pixel_format_t fmt, const uint8_t *line, uint32_t width,
                               uint8_t *gray)
{
    switch (fmt) {
    case M2022_PIX_SGRAY_8:
        memcpy(gray, line, width);
        break;
    case M2022_PIX_BLACK_8:
        for (uint32_t x = 0; x < width; x++) {
            gray[x] = (uint8_t)(255 - line[x]);
        }
        break;
    case M2022_PIX_BLACK_1:
        for (uint32_t x = 0; x < width; x++) {
            gray[x] = (line[x >> 3] & (0x80u >> (x & 7))) ? 0 : 255;
        }
        break;
    case M2022_PIX_SRGB_24:
        init_tables();
        for (uint32_t x = 0; x < width; x++) {
            /* Rec. 709 luminance of the linearised channels, re-encoded as sGray. */
            double y = 0.2126 * LINEAR[line[3 * x]] + 0.7152 * LINEAR[line[3 * x + 1]] +
                       0.0722 * LINEAR[line[3 * x + 2]];
            gray[x] = ENCODE[(int)lrint(y * 4096.0)];
        }
        break;
    }
}

void m2022_raster_fit_line(const uint8_t *in, uint32_t in_width, int32_t src_x0, uint8_t *out,
                           uint32_t out_width, uint8_t fill)
{
    for (uint32_t x = 0; x < out_width; x++) {
        int64_t sx = (int64_t)src_x0 + x;
        out[x] = (sx >= 0 && sx < (int64_t)in_width) ? in[sx] : fill;
    }
}

void m2022_raster_upscale2x_line(const uint8_t *in, uint32_t in_width, uint8_t *out)
{
    if (in_width == 0) {
        return;
    }
    for (uint32_t x = 0; x < in_width; x++) {
        uint8_t a = in[x];
        uint8_t b = x + 1 < in_width ? in[x + 1] : in[x];
        uint8_t p = x > 0 ? in[x - 1] : in[x];
        out[2 * x] = (uint8_t)((3 * a + p + 2) / 4);
        out[2 * x + 1] = (uint8_t)((3 * a + b + 2) / 4);
    }
}

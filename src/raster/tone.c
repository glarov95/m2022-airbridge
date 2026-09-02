/*
 * Tone curve: sGray in, ink coverage out.
 *
 * A halftone patch with ink coverage c reflects about (1 - c) of the light (Murray-Davies), so
 * to reproduce the luminance Y that an sGray value encodes we need c = 1 - Y, in linear light.
 * Real toner dots spread ("dot gain"): a nominal 50 % patch prints darker, by `dot_gain`. We model
 * the printed coverage as p(c) = c + 4 g c (1 - c), which peaks at +g at 50 %, and invert it so
 * that the requested darkness comes out on paper. `gamma` bends the curve on top of that for
 * taste, `coverage_scale` scales everything (toner save), and the black/white points clip.
 */
#include "m2022/raster.h"

#include <math.h>

void m2022_tone_default(m2022_tone_params_t *p)
{
    p->gamma = 1.0;
    p->dot_gain = 0.15;
    p->coverage_scale = 1.0;
    p->black_point = 0;
    p->white_point = 255;
}

static double compensate_dot_gain(double target, double g)
{
    /* solve c + 4 g c (1 - c) = target for c in [0, 1] */
    double a, b, disc;
    if (g <= 1e-9) {
        return target;
    }
    a = 4.0 * g;
    b = 1.0 + 4.0 * g;
    disc = b * b - 4.0 * a * target;
    if (disc < 0.0) {
        disc = 0.0;
    }
    return (b - sqrt(disc)) / (2.0 * a);
}

void m2022_tone_build(const m2022_tone_params_t *p, uint8_t lut[256])
{
    for (int v = 0; v < 256; v++) {
        double darkness, c;
        if (v <= p->black_point) {
            lut[v] = 255;
            continue;
        }
        if (v >= p->white_point) {
            lut[v] = 0;
            continue;
        }
        darkness = 1.0 - m2022_srgb_to_linear((uint8_t)v);
        c = pow(darkness, p->gamma > 0.0 ? p->gamma : 1.0);
        c = compensate_dot_gain(c, p->dot_gain);
        c *= p->coverage_scale;
        if (c < 0.0) {
            c = 0.0;
        }
        if (c > 1.0) {
            c = 1.0;
        }
        lut[v] = (uint8_t)lrint(c * 255.0);
    }
}

void m2022_tone_apply(const uint8_t lut[256], const uint8_t *gray, uint32_t width, uint8_t *ink)
{
    for (uint32_t x = 0; x < width; x++) {
        ink[x] = lut[gray[x]];
    }
}

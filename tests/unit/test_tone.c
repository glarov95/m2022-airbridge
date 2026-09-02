#include "m2022/raster.h"

#include "m2022_test.h"

#include <math.h>

int main(void)
{
    m2022_tone_params_t p;
    uint8_t lut[256], lut2[256];

    /* sRGB transfer round trip and anchor values */
    CHECK(fabs(m2022_srgb_to_linear(0) - 0.0) < 1e-9);
    CHECK(fabs(m2022_srgb_to_linear(255) - 1.0) < 1e-9);
    CHECK(fabs(m2022_srgb_to_linear(128) - 0.2158) < 0.002);
    for (int v = 0; v < 256; v++) {
        CHECK_EQ_INT(m2022_linear_to_srgb(m2022_srgb_to_linear((uint8_t)v)), v);
    }

    m2022_tone_default(&p);
    m2022_tone_build(&p, lut);
    CHECK_EQ_INT(lut[0], 255);
    CHECK_EQ_INT(lut[255], 0);
    for (int v = 1; v < 256; v++) { /* ink never increases as gray gets lighter */
        CHECK(lut[v] <= lut[v - 1]);
    }
    /* mid gray: linear darkness 0.784, dot gain 0.15 compensation pulls it down */
    CHECK(lut[128] > 150 && lut[128] < 200);

    /* no dot gain, gamma 1: ink is exactly the linear darkness */
    p.dot_gain = 0.0;
    m2022_tone_build(&p, lut2);
    CHECK_EQ_INT(lut2[128], (int)lrint((1.0 - m2022_srgb_to_linear(128)) * 255.0));
    CHECK(lut2[128] > lut[128]); /* compensation reduces coverage in the midtones */

    /* coverage scale halves the ink; black and white points clip */
    p.coverage_scale = 0.5;
    m2022_tone_build(&p, lut2);
    CHECK(lut2[128] >= lut[128] / 2 - 30 && lut2[128] <= 128);
    p.coverage_scale = 1.0;
    p.black_point = 40;
    p.white_point = 220;
    m2022_tone_build(&p, lut2);
    CHECK_EQ_INT(lut2[40], 255);
    CHECK_EQ_INT(lut2[41] == 255, 0);
    CHECK_EQ_INT(lut2[220], 0);
    CHECK_EQ_INT(lut2[219] == 0, 0);

    /* apply */
    {
        uint8_t gray[4] = {0, 128, 200, 255}, ink[4];
        m2022_tone_apply(lut, gray, 4, ink);
        CHECK_EQ_INT(ink[0], 255);
        CHECK_EQ_INT(ink[1], lut[128]);
        CHECK_EQ_INT(ink[3], 0);
    }
    TEST_MAIN_END();
}

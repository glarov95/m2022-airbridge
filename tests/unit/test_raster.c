#include "m2022/raster.h"

#include "m2022_test.h"

int main(void)
{
    uint8_t gray[16], out[16];

    /* sGray passes through, K inverts, 1-bit unpacks, sRGB gray stays gray */
    {
        const uint8_t sg[4] = {0, 100, 200, 255};
        const uint8_t k[4] = {255, 155, 55, 0};
        const uint8_t bits[2] = {0xA5, 0x80}; /* 1010 0101 1000 0000 */
        const uint8_t rgb[9] = {0, 0, 0, 128, 128, 128, 255, 0, 0};
        m2022_raster_line_to_gray(M2022_PIX_SGRAY_8, sg, 4, gray);
        CHECK_MEM_EQ(gray, sg, 4);
        m2022_raster_line_to_gray(M2022_PIX_BLACK_8, k, 4, gray);
        CHECK_MEM_EQ(gray, sg, 4);
        m2022_raster_line_to_gray(M2022_PIX_BLACK_1, bits, 9, gray);
        CHECK_EQ_INT(gray[0], 0);
        CHECK_EQ_INT(gray[1], 255);
        CHECK_EQ_INT(gray[2], 0);
        CHECK_EQ_INT(gray[7], 0);
        CHECK_EQ_INT(gray[8], 0);
        m2022_raster_line_to_gray(M2022_PIX_SRGB_24, rgb, 3, gray);
        CHECK_EQ_INT(gray[0], 0);
        CHECK(gray[1] >= 127 && gray[1] <= 129);
        CHECK(gray[2] > 100 && gray[2] < 150); /* pure red is mid-dark in luminance */
    }
    CHECK_EQ_INT(m2022_raster_line_bytes(M2022_PIX_BLACK_1, 9), 2);
    CHECK_EQ_INT(m2022_raster_line_bytes(M2022_PIX_SRGB_24, 9), 27);
    CHECK_EQ_INT(m2022_raster_line_bytes(M2022_PIX_SGRAY_8, 9), 9);

    /* fit: offset, padding on both sides, negative start */
    {
        const uint8_t in[4] = {10, 20, 30, 40};
        m2022_raster_fit_line(in, 4, 1, out, 6, 255);
        CHECK_EQ_INT(out[0], 20);
        CHECK_EQ_INT(out[2], 40);
        CHECK_EQ_INT(out[3], 255);
        CHECK_EQ_INT(out[5], 255);
        m2022_raster_fit_line(in, 4, -2, out, 4, 0);
        CHECK_EQ_INT(out[0], 0);
        CHECK_EQ_INT(out[1], 0);
        CHECK_EQ_INT(out[2], 10);
        CHECK_EQ_INT(out[3], 20);
    }

    /* 2x upscale: constant stays constant, edge is interpolated, ends are clamped */
    {
        const uint8_t flat[3] = {77, 77, 77};
        const uint8_t step[4] = {0, 0, 255, 255};
        m2022_raster_upscale2x_line(flat, 3, out);
        for (int i = 0; i < 6; i++) {
            CHECK_EQ_INT(out[i], 77);
        }
        m2022_raster_upscale2x_line(step, 4, out);
        CHECK_EQ_INT(out[0], 0);
        CHECK_EQ_INT(out[2], 0);
        CHECK(out[3] > 0 && out[3] < 128);   /* blend toward the edge */
        CHECK(out[4] > 128 && out[4] < 255);
        CHECK_EQ_INT(out[7], 255);
    }

    /* CUPS raster header: build a little-endian v3 header like the fixtures */
    {
        uint8_t hdr[M2022_CUPSRASTER_HEADER_BYTES] = {0};
        m2022_cupsraster_header_t h;
        m2022_pixel_format_t fmt;
        memcpy(hdr, "3SaR", 4);
        hdr[280] = 0x58; hdr[281] = 0x02;            /* 600 */
        hdr[284] = 0x58; hdr[285] = 0x02;
        hdr[376] = 0x8E; hdr[377] = 0x12;            /* 4750 */
        hdr[380] = 0x98; hdr[381] = 0x1A;            /* 6808 */
        hdr[388] = 8; hdr[392] = 8;
        hdr[396] = 0x8E; hdr[397] = 0x12;
        hdr[404] = 3;                                /* K */
        memcpy(hdr + 1736, "A4", 3);
        CHECK_EQ_INT(m2022_cupsraster_parse_header(hdr, sizeof hdr, &h), 0);
        CHECK(h.little_endian);
        CHECK(!h.compressed);
        CHECK_EQ_INT(h.width, 4750);
        CHECK_EQ_INT(h.height, 6808);
        CHECK_EQ_INT(h.x_dpi, 600);
        CHECK_EQ_INT(h.bytes_per_line, 4750);
        CHECK_EQ_STR(h.page_size_name, "A4");
        CHECK_EQ_INT(m2022_cupsraster_pixel_format(&h, &fmt), 0);
        CHECK_EQ_INT(fmt, M2022_PIX_BLACK_8);
        h.color_space = 18;
        CHECK_EQ_INT(m2022_cupsraster_pixel_format(&h, &fmt), 0);
        CHECK_EQ_INT(fmt, M2022_PIX_SGRAY_8);
        h.bits_per_pixel = 16;
        CHECK_EQ_INT(m2022_cupsraster_pixel_format(&h, &fmt), -1);
        memcpy(hdr, "RaS2", 4); /* big-endian, compressed: same bytes read the other way */
        CHECK_EQ_INT(m2022_cupsraster_parse_header(hdr, sizeof hdr, &h), 0);
        CHECK(!h.little_endian);
        CHECK(h.compressed);
        CHECK_EQ_INT(h.width, 0x8E120000u);
        memcpy(hdr, "RaSt", 4);
        CHECK_EQ_INT(m2022_cupsraster_parse_header(hdr, sizeof hdr, &h), -2);
        CHECK_EQ_INT(m2022_cupsraster_parse_header(hdr, 100, &h), -1);
    }
    TEST_MAIN_END();
}

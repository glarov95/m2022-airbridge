/*
 * CUPS raster stream header (cups/raster.h): a 4-byte sync word followed by a 1796-byte
 * cups_page_header2_t whose integers share the byte order announced by the sync word.
 * Offsets below are file offsets (sync included), as verified on the captured vendor input.
 */
#include "m2022/raster.h"

#include <string.h>

static uint32_t rd32(const uint8_t *p, int le)
{
    return le ? ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0]
              : ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int m2022_cupsraster_parse_header(const uint8_t *d, size_t len, m2022_cupsraster_header_t *h)
{
    int le;

    if (len < M2022_CUPSRASTER_HEADER_BYTES) {
        return -1;
    }
    memset(h, 0, sizeof *h);
    if (memcmp(d, "RaS2", 4) == 0 || memcmp(d, "RaS3", 4) == 0) {
        le = 0;
    } else if (memcmp(d, "2SaR", 4) == 0 || memcmp(d, "3SaR", 4) == 0) {
        le = 1;
    } else {
        return -2; /* unknown sync word (v1 "RaSt" streams are not supported) */
    }
    h->little_endian = le;
    h->compressed = d[le ? 0 : 3] == '2';
    h->x_dpi = rd32(d + 280, le);
    h->y_dpi = rd32(d + 284, le);
    h->num_copies = rd32(d + 344, le);
    h->width = rd32(d + 376, le);
    h->height = rd32(d + 380, le);
    h->bits_per_color = rd32(d + 388, le);
    h->bits_per_pixel = rd32(d + 392, le);
    h->bytes_per_line = rd32(d + 396, le);
    h->color_space = rd32(d + 404, le);
    memcpy(h->page_size_name, d + 1736, 63);
    h->page_size_name[63] = '\0';
    if (h->width == 0 || h->height == 0 || h->bytes_per_line == 0) {
        return -3;
    }
    return 0;
}

int m2022_cupsraster_pixel_format(const m2022_cupsraster_header_t *h, m2022_pixel_format_t *fmt)
{
    if (h->bits_per_pixel == 8 && (h->color_space == 18 || h->color_space == 0)) {
        *fmt = M2022_PIX_SGRAY_8; /* sGray, or W (white) which is also 0 = black */
        return 0;
    }
    if (h->bits_per_pixel == 8 && h->color_space == 3) {
        *fmt = M2022_PIX_BLACK_8;
        return 0;
    }
    if (h->bits_per_pixel == 1 && h->color_space == 3) {
        *fmt = M2022_PIX_BLACK_1;
        return 0;
    }
    if (h->bits_per_pixel == 24 && (h->color_space == 19 || h->color_space == 1)) {
        *fmt = M2022_PIX_SRGB_24;
        return 0;
    }
    return -1;
}

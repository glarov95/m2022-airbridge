/*
 * Our own pipeline end to end, up to the band payloads: the vendor's input rasters
 * (fixtures/oracle/samsung/NAME.ras.gz) rendered with our presets, cut into 128-line bands at
 * the printer's band width, packed column-major and encoded with 0x11. Every band must
 * decode back to the same bytes, and the page's payload total is reported next to the
 * vendor's for the same page (docs/spl-qpdl.md section 3.4 keeps the numbers).
 */
#include "m2022/fileio.h"
#include "m2022/halftone.h"
#include "m2022/qpdl.h"
#include "m2022/raster.h"

#include "m2022_test.h"

#include <stdlib.h>

#ifndef M2022_FIXTURE_DIR
#error "M2022_FIXTURE_DIR must point at fixtures/oracle/samsung"
#endif

static void sum_band(void *c, size_t o, const m2022_qpdl_band_header_t *h, const uint8_t *p)
{
    (void)o;
    (void)p;
    *(size_t *)c += h->length;
}

static size_t vendor_payload_bytes(const char *name)
{
    char path[1024];
    size_t len = 0, total = 0, err = 0;
    uint8_t *data;
    const m2022_qpdl_visitor_t vis = {NULL, NULL, sum_band, NULL, NULL};
    snprintf(path, sizeof path, "%s/%s.spl", M2022_FIXTURE_DIR, name);
    data = m2022_read_file(path, &len);
    CHECK(data != NULL);
    CHECK_EQ_INT(m2022_qpdl_walk(data, len, &vis, &total, &err), 0);
    free(data);
    return total;
}

typedef struct {
    size_t bands, blank, ours, vendor;
    double ratio;
} result_t;

static void run(const char *name, m2022_preset_t preset, result_t *r)
{
    char path[1024];
    size_t len = 0;
    uint8_t *data;
    m2022_cupsraster_header_t h;
    m2022_pixel_format_t fmt;
    m2022_tone_params_t tone;
    m2022_ht_params_t hp;
    m2022_halftoner_t ht;
    uint8_t lut[256], *gray, *ink, *rows, *band, *enc, *dec;
    uint16_t table[M2022_CODEC11_TABLE_ENTRIES];
    void *state;
    uint32_t band_width, bpr;
    size_t band_bytes, enc_cap;

    memset(r, 0, sizeof *r);
    snprintf(path, sizeof path, "%s/%s.ras.gz", M2022_FIXTURE_DIR, name);
    data = m2022_read_file(path, &len);
    CHECK(data != NULL);
    CHECK_EQ_INT(m2022_cupsraster_parse_header(data, len, &h), 0);
    CHECK_EQ_INT(m2022_cupsraster_pixel_format(&h, &fmt), 0);

    band_width = (h.width + 255) / 256 * 256; /* docs/spl-qpdl.md 2.2 */
    bpr = band_width / 8;
    band_bytes = (size_t)bpr * M2022_QPDL_BAND_LINES;
    enc_cap = m2022_codec11_encode_bound(band_bytes);

    m2022_preset(preset, &tone, &hp);
    m2022_tone_build(&tone, lut);
    gray = malloc(h.width);
    ink = malloc(h.width);
    rows = malloc(band_bytes);
    band = malloc(band_bytes);
    dec = malloc(band_bytes);
    enc = malloc(enc_cap);
    state = malloc(m2022_halftoner_state_bytes(h.width));
    m2022_halftoner_init(&ht, &hp, h.width, state, m2022_halftoner_state_bytes(h.width));

    for (uint32_t y0 = 0; y0 < h.height; y0 += M2022_QPDL_BAND_LINES) {
        size_t n = 0, out_len = 0;
        bool blank = true;
        memset(rows, 0, band_bytes); /* 0 = white in row layout; padding stays white */
        for (uint32_t y = y0; y < y0 + M2022_QPDL_BAND_LINES && y < h.height; y++) {
            uint8_t *row = rows + (size_t)(y - y0) * bpr;
            m2022_raster_line_to_gray(fmt, data + M2022_CUPSRASTER_HEADER_BYTES +
                                               (size_t)y * h.bytes_per_line,
                                      h.width, gray);
            m2022_tone_apply(lut, gray, h.width, ink);
            m2022_halftone_line(&ht, ink, y, row);
        }
        for (size_t i = 0; i < band_bytes && blank; i++) {
            blank = rows[i] == 0;
        }
        if (blank) {
            r->blank++;
            continue; /* the vendor omits white bands; so will the QPDL encoder */
        }
        m2022_qpdl_rows_to_band(rows, bpr, band);
        m2022_codec11_choose_table(band, band_bytes, table);
        CHECK_EQ_INT(m2022_codec11_encode(band, band_bytes, table, enc, enc_cap, &n), 0);
        CHECK_EQ_INT(m2022_codec11_decode(enc, n, dec, band_bytes, &out_len, NULL), 0);
        CHECK_EQ_INT(out_len, band_bytes);
        CHECK_MEM_EQ(dec, band, band_bytes);
        r->bands++;
        r->ours += n;
    }
    r->vendor = vendor_payload_bytes(name);
    r->ratio = r->vendor ? (double)r->ours / (double)r->vendor : 0.0;
    fprintf(stderr, "%-16s %-7s %3zu bands (%2zu blank) %8zu B; vendor %8zu B; ratio %.2f\n",
            name, m2022_preset_name(preset), r->bands, r->blank, r->ours, r->vendor, r->ratio);

    free(data);
    free(gray);
    free(ink);
    free(rows);
    free(band);
    free(dec);
    free(enc);
    free(state);
}

int main(void)
{
    result_t r;

    /* text: both threshold, so the byte counts should be close (measured 0.76) */
    run("small-text-a4", M2022_PRESET_TEXT, &r);
    CHECK(r.bands > 20);
    CHECK(r.ratio < 0.9);

    /* solid square: tiny either way (measured 0.97) */
    run("black-square-a4", M2022_PRESET_NORMAL, &r);
    CHECK_EQ_INT(r.bands, 11);
    CHECK(r.ratio < 1.05);

    /* screens: ordered screens repeat and compress far better than the vendor's (0.24-0.26);
     * blue noise (2-2.5x) and error diffusion (6.8x) do not repeat, so they cost bytes */
    run("gray-ramp-a4", M2022_PRESET_VENDOR, &r);
    CHECK(r.bands > 10);
    CHECK(r.ratio < 0.5);
    run("gray-ramp-a4", M2022_PRESET_NORMAL, &r);
    run("gray-ramp-a4", M2022_PRESET_DRAFT, &r);
    run("photo-a4", M2022_PRESET_VENDOR, &r);
    run("photo-a4", M2022_PRESET_NORMAL, &r);
    run("photo-a4", M2022_PRESET_PHOTO, &r);
    CHECK(r.bands > 10);

    TEST_MAIN_END();
}

/*
 * The vendor's own input raster (fixtures/oracle/samsung/NAME.ras.gz, 8-bit K at 600 dpi) run
 * through our pipeline, compared with the vendor's output bitmap decoded from the matching
 * .spl. Text is thresholded by both, so the Text preset should agree closely; the exact
 * threshold and edge treatment differ, so this measures rather than demands identity.
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

typedef struct {
    uint8_t *rows;    /* page bitmap, 1 = black, band width wide */
    size_t bpr;
    uint32_t width, lines;
    uint8_t *band, *tmp;
    int bad;
} vendor_page_t;

static void on_band(void *c, size_t o, const m2022_qpdl_band_header_t *h, const uint8_t *payload)
{
    vendor_page_t *v = c;
    size_t bpr = h->width / 8u, expect = bpr * h->height, out_len = 0;
    (void)o;
    if (v->rows == NULL) {
        v->width = h->width;
        v->bpr = bpr;
        v->lines = 256u * M2022_QPDL_BAND_LINES;
        v->rows = calloc(v->lines, bpr);
        v->band = malloc(expect);
        v->tmp = malloc(expect);
    }
    if (h->width != v->width || m2022_codec11_decode(payload, h->length, v->band, expect, &out_len, NULL) != 0 || out_len != expect) {
        v->bad++;
        return;
    }
    m2022_qpdl_band_to_rows(v->band, bpr, v->rows + (size_t)h->number * M2022_QPDL_BAND_LINES * bpr);
}

static int load_vendor(const char *name, vendor_page_t *v)
{
    char path[1024];
    size_t len = 0, err = 0;
    uint8_t *data;
    const m2022_qpdl_visitor_t vis = {NULL, NULL, on_band, NULL, NULL};
    int rc;
    memset(v, 0, sizeof *v);
    snprintf(path, sizeof path, "%s/%s.spl", M2022_FIXTURE_DIR, name);
    data = m2022_read_file(path, &len);
    if (data == NULL) {
        return -1;
    }
    rc = m2022_qpdl_walk(data, len, &vis, v, &err);
    free(data);
    return rc == 0 && v->bad == 0 ? 0 : -2;
}

static inline int bit(const uint8_t *row, uint32_t x)
{
    return (row[x >> 3] >> (7 - (x & 7))) & 1;
}

/* Render the vendor input with a preset; compare against the vendor page. */
static void compare(const char *name, m2022_preset_t preset, double *agreement, double *ours_black,
                    double *vendor_black, double *recall, double *precision)
{
    char path[1024];
    size_t len = 0;
    uint8_t *data;
    m2022_cupsraster_header_t h;
    m2022_pixel_format_t fmt;
    vendor_page_t v;
    m2022_tone_params_t tone;
    m2022_ht_params_t hp;
    m2022_halftoner_t ht;
    uint8_t lut[256], *gray, *ink, *bits;
    void *state;
    uint64_t same = 0, both = 0, ours = 0, theirs = 0, total = 0;

    snprintf(path, sizeof path, "%s/%s.ras.gz", M2022_FIXTURE_DIR, name);
    data = m2022_read_file(path, &len);
    CHECK(data != NULL);
    CHECK_EQ_INT(m2022_cupsraster_parse_header(data, len, &h), 0);
    CHECK_EQ_INT(m2022_cupsraster_pixel_format(&h, &fmt), 0);
    CHECK_EQ_INT(fmt, M2022_PIX_BLACK_8);
    CHECK_EQ_INT(load_vendor(name, &v), 0);
    if (v.rows == NULL) { /* a page without bands (blank): an all-white vendor page */
        v.width = (h.width + 255) / 256 * 256;
        v.bpr = v.width / 8;
        v.lines = 256u * M2022_QPDL_BAND_LINES;
        v.rows = calloc(v.lines, v.bpr);
    }
    CHECK(v.width >= h.width);

    m2022_preset(preset, &tone, &hp);
    m2022_tone_build(&tone, lut);
    gray = malloc(h.width);
    ink = malloc(h.width);
    bits = malloc((h.width + 7) / 8);
    state = malloc(m2022_halftoner_state_bytes(h.width));
    m2022_halftoner_init(&ht, &hp, h.width, state, m2022_halftoner_state_bytes(h.width));
    for (uint32_t y = 0; y < h.height && y < v.lines; y++) {
        const uint8_t *vrow = v.rows + (size_t)y * v.bpr;
        m2022_raster_line_to_gray(fmt, data + M2022_CUPSRASTER_HEADER_BYTES + (size_t)y * h.bytes_per_line, h.width, gray);
        m2022_tone_apply(lut, gray, h.width, ink);
        m2022_halftone_line(&ht, ink, y, bits);
        for (uint32_t x = 0; x < h.width; x++) {
            int a = bit(bits, x), b = bit(vrow, x);
            total++;
            same += (uint64_t)(a == b);
            both += (uint64_t)(a && b);
            ours += (uint64_t)a;
            theirs += (uint64_t)b;
        }
    }
    *agreement = (double)same / (double)total;
    *ours_black = (double)ours / (double)total;
    *vendor_black = (double)theirs / (double)total;
    *recall = theirs ? (double)both / (double)theirs : 1.0;
    *precision = ours ? (double)both / (double)ours : 1.0;
    free(data);
    free(gray);
    free(ink);
    free(bits);
    free(state);
    free(v.rows);
    free(v.band);
    free(v.tmp);
}

int main(void)
{
    double agreement, ours, theirs, recall, precision;

    /* text page, Text preset: both threshold; expect close agreement */
    compare("small-text-a4", M2022_PRESET_TEXT, &agreement, &ours, &theirs, &recall, &precision);
    fprintf(stderr, "small-text-a4 text: agreement %.4f, black ours %.4f vendor %.4f, recall %.3f precision %.3f\n",
            agreement, ours, theirs, recall, precision);
    CHECK(agreement > 0.97);
    CHECK(recall > 0.85);

    /* black square: solid area, any preset must reproduce the same coverage */
    compare("black-square-a4", M2022_PRESET_NORMAL, &agreement, &ours, &theirs, &recall, &precision);
    fprintf(stderr, "black-square-a4 normal: agreement %.4f, black ours %.4f vendor %.4f\n", agreement, ours, theirs);
    CHECK(agreement > 0.995);
    CHECK(ours > theirs * 0.98 && ours < theirs * 1.02);

    /* blank page: nothing black */
    compare("blank-a4", M2022_PRESET_NORMAL, &agreement, &ours, &theirs, &recall, &precision);
    CHECK(ours == 0.0);
    CHECK(theirs == 0.0);

    /* gray ramp: our Normal preset and the vendor's screen should put similar ink on the page */
    compare("gray-ramp-a4", M2022_PRESET_NORMAL, &agreement, &ours, &theirs, &recall, &precision);
    fprintf(stderr, "gray-ramp-a4 normal: black ours %.4f vendor %.4f\n", ours, theirs);
    CHECK(ours > theirs * 0.7 && ours < theirs * 1.3);

    TEST_MAIN_END();
}

/*
 * The job encoder against the vendor's own jobs: the PJL envelope and records byte for byte
 * where they are deterministic (all 14 media page headers, the 1200 dpi header, the PJL SET
 * lines for every option), band mechanics (numbering, blank omission, padding, bit masking),
 * error handling, and a whole page (the black square) that must come out structurally like
 * the vendor's and decode to the same picture.
 */
#include "m2022/fileio.h"
#include "m2022/halftone.h"
#include "m2022/media.h"
#include "m2022/qpdl.h"
#include "m2022/raster.h"

#include "m2022_test.h"

#include <stdlib.h>

#ifndef M2022_FIXTURE_DIR
#error "M2022_FIXTURE_DIR must point at fixtures/oracle/samsung"
#endif

/* ---- a growable sink ------------------------------------------------------------------ */

typedef struct {
    uint8_t *data;
    size_t len, cap;
} buf_t;

static int buf_sink(void *ctx, const uint8_t *data, size_t len)
{
    buf_t *b = ctx;
    if (b->len + len > b->cap) {
        b->cap = (b->len + len) * 2 + 1024;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    return 0;
}

static int failing_sink(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;
    (void)data;
    (void)len;
    return -77;
}

/* ---- what a walk of a job collects ------------------------------------------------------ */

typedef struct {
    const uint8_t *data;
    int pages, end_pages, end_jobs, bad;
    uint16_t end_copies;
    uint8_t page_raw[M2022_QPDL_PAGE_HEADER_LEN];
    int bands;
    uint8_t band_numbers[256];
    uint16_t band_width;
    size_t bpr;
    uint8_t *bitmap; /* 256 bands of rows, 1 = black */
    int max_band;
    char lines[16][96]; /* "@PJL SET ..." and "@PJL ENTER ..." in order */
    int n_lines;
} walk_t;

static void w_pjl(void *c, size_t o, const char *line, size_t len)
{
    walk_t *w = c;
    (void)o;
    if ((len >= 8 && memcmp(line, "@PJL SET", 8) == 0) ||
        (len >= 10 && memcmp(line, "@PJL ENTER", 10) == 0)) {
        if (w->n_lines < 16) {
            snprintf(w->lines[w->n_lines++], sizeof w->lines[0], "%.*s", (int)len, line);
        }
    }
}

static void w_page(void *c, size_t o, const m2022_qpdl_page_header_t *h)
{
    walk_t *w = c;
    (void)h;
    w->pages++;
    memcpy(w->page_raw, w->data + o, sizeof w->page_raw);
}

static void w_band(void *c, size_t o, const m2022_qpdl_band_header_t *h, const uint8_t *payload)
{
    walk_t *w = c;
    size_t bpr = h->width / 8u, expect = bpr * h->height, out_len = 0;
    uint8_t *band;
    (void)o;
    if (w->bands < 256) {
        w->band_numbers[w->bands] = h->number;
    }
    w->bands++;
    w->band_width = h->width;
    if (w->bitmap == NULL) {
        w->bpr = bpr;
        w->bitmap = calloc(256u * M2022_QPDL_BAND_LINES, bpr);
    }
    band = malloc(expect);
    if (h->height != M2022_QPDL_BAND_LINES || bpr != w->bpr ||
        m2022_codec11_decode(payload, h->length, band, expect, &out_len, NULL) != 0 ||
        out_len != expect) {
        w->bad++;
    } else {
        m2022_qpdl_band_to_rows(band, bpr,
                                w->bitmap + (size_t)h->number * M2022_QPDL_BAND_LINES * bpr);
        if ((int)h->number > w->max_band) {
            w->max_band = h->number;
        }
    }
    free(band);
}

static void w_end_page(void *c, size_t o, uint16_t copies)
{
    walk_t *w = c;
    (void)o;
    w->end_pages++;
    w->end_copies = copies;
}

static void w_end_job(void *c, size_t o)
{
    (void)o;
    ((walk_t *)c)->end_jobs++;
}

static int walk(const uint8_t *data, size_t len, walk_t *w)
{
    const m2022_qpdl_visitor_t v = {w_pjl, w_page, w_band, w_end_page, w_end_job};
    size_t err = 0;
    int rc;
    memset(w, 0, sizeof *w);
    w->data = data;
    w->max_band = -1;
    rc = m2022_qpdl_walk(data, len, &v, w, &err);
    if (rc != 0) {
        fprintf(stderr, "walk failed at %zu: %s\n", err, m2022_qpdl_strerror(rc));
    }
    return rc;
}

static int walk_file(const char *name, walk_t *w, uint8_t **data)
{
    char path[1024];
    size_t len = 0;
    snprintf(path, sizeof path, "%s/%s", M2022_FIXTURE_DIR, name);
    *data = m2022_read_file(path, &len);
    if (*data == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return -1;
    }
    return walk(*data, len, w);
}

/* ---- tests --------------------------------------------------------------------------- */

static const char ENVELOPE_600[] =
    "\x1b%-12345X@PJL DEFAULT SERVICEDATE=20260902\r\n"
    "@PJL COMMENT Model Name : Samsung M2020 Series\r\n"
    "@PJL COMMENT Producer : test\r\n"
    "@PJL SET XIGNOREFF=OFF\r\n"
    "\r\n"
    "@PJL SET RESOLUTION = 600\r\n"
    "@PJL SET BITSPERPIXEL = 1\r\n"
    "@PJL SET PAPERTYPE = OFF\r\n"
    "@PJL SET DUPLEX = OFF\r\n"
    "@PJL ENTER LANGUAGE=QPDL\r\n";
static const uint8_t A4_600_HEADER[17] = {0x00, 0x06, 0x00, 0x01, 0x02, 0x09, 0xAF, 0x0D, 0xB4,
                                          0x01, 0x00, 0x00, 0x00, 0x00, 0x03, 0x01, 0x06};
static const uint8_t TRAILER[13] = {0x01, 0x00, 0x01, 0x09, 0x1b, 0x25, 0x2d,
                                    0x31, 0x32, 0x33, 0x34, 0x35, 0x58};

static void empty_page_job(const m2022_qpdl_job_t *job, buf_t *out)
{
    m2022_qpdl_encoder_t e;
    size_t ws_bytes = m2022_qpdl_encoder_workspace_bytes(256);
    void *ws = malloc(ws_bytes);
    memset(out, 0, sizeof *out);
    CHECK_EQ_INT(m2022_qpdl_begin_job(&e, job, buf_sink, out), 0);
    CHECK_EQ_INT(m2022_qpdl_begin_page(&e, 256, ws, ws_bytes), 0);
    CHECK_EQ_INT(m2022_qpdl_end_page(&e), 0);
    CHECK_EQ_INT(m2022_qpdl_end_job(&e), 0);
    CHECK_EQ_INT(e.pages, 1);
    CHECK_EQ_INT(e.bytes_out, out->len);
    free(ws);
}

static void test_envelope_and_records(void)
{
    m2022_qpdl_job_t job;
    buf_t out;
    walk_t w;
    size_t n = sizeof ENVELOPE_600 - 1;

    m2022_qpdl_job_default(&job);
    job.service_date = "20260902";
    job.producer = "test";
    empty_page_job(&job, &out);
    CHECK_EQ_INT(out.len, n + 17 + 13);
    CHECK_MEM_EQ(out.data, ENVELOPE_600, n);
    CHECK_MEM_EQ(out.data + n, A4_600_HEADER, 17);
    CHECK_MEM_EQ(out.data + n + 17, TRAILER, 13);
    CHECK_EQ_INT(walk(out.data, out.len, &w), 0);
    CHECK_EQ_INT(w.pages, 1);
    CHECK_EQ_INT(w.bands, 0);
    CHECK_EQ_INT(w.end_pages, 1);
    CHECK_EQ_INT(w.end_copies, 1);
    CHECK_EQ_INT(w.end_jobs, 1);
    CHECK_EQ_INT(w.n_lines, 6);
    free(out.data);

    /* no date, no producer: those lines vanish, nothing else moves */
    job.service_date = NULL;
    job.producer = NULL;
    empty_page_job(&job, &out);
    CHECK(memmem(out.data, out.len, "SERVICEDATE", 11) == NULL);
    CHECK(memmem(out.data, out.len, "Producer", 8) == NULL);
    CHECK(memmem(out.data, out.len, "@PJL COMMENT Model Name : Samsung M2020 Series\r\n", 47) !=
          NULL);
    free(out.data);

    /* 1200 dpi: no BITSPERPIXEL line, resolution bytes 0C, paper size in 1/150 in */
    m2022_qpdl_job_default(&job);
    job.dpi = 1200;
    empty_page_job(&job, &out);
    CHECK(memmem(out.data, out.len, "@PJL SET RESOLUTION = 1200\r\n", 28) != NULL);
    CHECK(memmem(out.data, out.len, "BITSPERPIXEL", 12) == NULL);
    CHECK_EQ_INT(walk(out.data, out.len, &w), 0);
    CHECK_EQ_INT(w.page_raw[1], 0x0C);
    CHECK_EQ_INT(w.page_raw[16], 0x0C);
    CHECK_EQ_INT((w.page_raw[5] << 8) | w.page_raw[6], 0x04D8); /* vendor Best mode */
    CHECK_EQ_INT((w.page_raw[7] << 8) | w.page_raw[8], 0x06DA);
    free(out.data);

    /* manual duplex, manual feed, thick paper, skip blank pages, copies 2 */
    m2022_qpdl_job_default(&job);
    job.duplex = M2022_QPDL_DUPLEX_MANUAL_LONG_EDGE;
    job.feeder = M2022_QPDL_FEEDER_MANUAL;
    job.paper_type = "THICK";
    job.skip_blank_pages = true;
    job.copies = 2;
    empty_page_job(&job, &out);
    CHECK(memmem(out.data, out.len, "@PJL SET XIGNOREFF=ON\r\n\r\n", 25) != NULL);
    CHECK(memmem(out.data, out.len, "@PJL SET PAPERTYPE = THICK\r\n", 28) != NULL);
    CHECK(memmem(out.data, out.len, "@PJL SET DUPLEX = MANUAL\r\n@PJL SET BINDING = LONGEDGE\r\n",
                 55) != NULL);
    CHECK_EQ_INT(walk(out.data, out.len, &w), 0);
    CHECK_EQ_INT(w.page_raw[9], 2);  /* feeder manual */
    CHECK_EQ_INT(w.page_raw[11], 0); /* duplex bytes stay 0, as the vendor's do */
    CHECK_EQ_INT(w.page_raw[3], 2);  /* copies in the header */
    CHECK_EQ_INT(w.end_copies, 2);   /* and in the footer */
    free(out.data);
    job.duplex = M2022_QPDL_DUPLEX_MANUAL_SHORT_EDGE;
    empty_page_job(&job, &out);
    CHECK(memmem(out.data, out.len, "@PJL SET BINDING = SHORTEDGE\r\n", 30) != NULL);
    free(out.data);
}

/* Every media: our page header equals the vendor's from the media sweep, byte for byte. */
static void test_media_headers(void)
{
    size_t count;
    const m2022_media_t *table = m2022_media_table(&count);
    m2022_qpdl_job_t job;
    buf_t out;
    walk_t ours, theirs;
    uint8_t *data;
    char name[64];

    CHECK_EQ_INT(count, 14);
    for (size_t i = 0; i < count; i++) {
        snprintf(name, sizeof name, "media/%s.spl", table[i].vendor_name);
        CHECK_EQ_INT(walk_file(name, &theirs, &data), 0);
        CHECK_EQ_INT(theirs.pages, 1);
        m2022_qpdl_job_default(&job);
        job.paper_code = table[i].qpdl_code;
        job.paper_width_pt = table[i].width_pt;
        job.paper_height_pt = table[i].height_pt;
        empty_page_job(&job, &out);
        CHECK_EQ_INT(walk(out.data, out.len, &ours), 0);
        if (memcmp(ours.page_raw, theirs.page_raw, 17) != 0) {
            fprintf(stderr,
                    "%s: header differs (ours %02x%02x/%02x%02x, vendor %02x%02x/%02x%02x)\n",
                    table[i].vendor_name, ours.page_raw[5], ours.page_raw[6], ours.page_raw[7],
                    ours.page_raw[8], theirs.page_raw[5], theirs.page_raw[6], theirs.page_raw[7],
                    theirs.page_raw[8]);
        }
        CHECK_MEM_EQ(ours.page_raw, theirs.page_raw, 17);
        free(out.data);
        free(data);
        free(theirs.bitmap);
        free(ours.bitmap);
    }

    /* the conversion behind it: half values round up (Env C5 459 pt) */
    CHECK_EQ_INT(m2022_qpdl_paper_dots(459, 600), 1913);
    CHECK_EQ_INT(m2022_qpdl_paper_dots(297, 600), 1238);
    CHECK_EQ_INT(m2022_qpdl_paper_dots(595, 600), 2479);
    CHECK_EQ_INT(m2022_qpdl_paper_dots(842, 600), 3508);
    CHECK_EQ_INT(m2022_qpdl_paper_dots(595, 1200), 1240);
    CHECK_EQ_INT(m2022_qpdl_paper_dots(842, 1200), 1754);
}

static void test_band_mechanics(void)
{
    enum { WIDTH = 4750, ROW = (WIDTH + 7) / 8 };
    m2022_qpdl_job_t job;
    m2022_qpdl_encoder_t e;
    buf_t out;
    walk_t w;
    size_t ws_bytes = m2022_qpdl_encoder_workspace_bytes(WIDTH);
    void *ws = malloc(ws_bytes);
    uint8_t row[ROW], white[ROW];

    memset(white, 0, sizeof white);
    m2022_qpdl_job_default(&job);
    CHECK_EQ_INT(ws_bytes, 2 * 608 * 128 + m2022_codec11_encode_bound(608 * 128));

    /* 300 lines; only line 130 has ink: a single band record, number 1, at width 4864 */
    memset(&out, 0, sizeof out);
    CHECK_EQ_INT(m2022_qpdl_begin_job(&e, &job, buf_sink, &out), 0);
    CHECK_EQ_INT(m2022_qpdl_begin_page(&e, WIDTH, ws, ws_bytes), 0);
    CHECK_EQ_INT(e.band_width, 4864);
    for (uint32_t y = 0; y < 300; y++) {
        memset(row, 0, sizeof row);
        if (y == 130) {
            row[0] = 0x80;      /* pixel 0 */
            row[ROW - 1] = 0xFF; /* pixels 4744..4749 and two bits past the width */
        }
        CHECK_EQ_INT(m2022_qpdl_write_line(&e, row), 0);
    }
    CHECK_EQ_INT(m2022_qpdl_end_page(&e), 0);
    CHECK_EQ_INT(m2022_qpdl_end_job(&e), 0);
    CHECK_EQ_INT(e.bands_written, 1);
    CHECK_EQ_INT(e.bands_blank, 2);
    CHECK_EQ_INT(walk(out.data, out.len, &w), 0);
    CHECK_EQ_INT(w.bad, 0);
    CHECK_EQ_INT(w.bands, 1);
    CHECK_EQ_INT(w.band_numbers[0], 1);
    CHECK_EQ_INT(w.band_width, 4864);
    {
        const uint8_t *line130 = w.bitmap + (size_t)130 * w.bpr;
        const uint8_t *line129 = w.bitmap + (size_t)129 * w.bpr;
        CHECK_EQ_INT(line130[0], 0x80);
        CHECK_EQ_INT(line130[ROW - 1], 0xFC); /* the two bits past 4750 were masked */
        CHECK_EQ_INT(line130[ROW], 0x00);     /* padding columns are white */
        CHECK_EQ_INT(line129[0], 0x00);
    }
    free(out.data);
    free(w.bitmap);

    /* ink on the very last line of a partial band: band 2 appears, padded white below */
    memset(&out, 0, sizeof out);
    CHECK_EQ_INT(m2022_qpdl_begin_job(&e, &job, buf_sink, &out), 0);
    CHECK_EQ_INT(m2022_qpdl_begin_page(&e, WIDTH, ws, ws_bytes), 0);
    for (uint32_t y = 0; y < 300; y++) {
        if (y == 299) {
            memset(row, 0xFF, sizeof row);
        }
        CHECK_EQ_INT(m2022_qpdl_write_line(&e, y == 299 ? row : white), 0);
    }
    CHECK_EQ_INT(m2022_qpdl_end_page(&e), 0);
    CHECK_EQ_INT(m2022_qpdl_end_job(&e), 0);
    CHECK_EQ_INT(walk(out.data, out.len, &w), 0);
    CHECK_EQ_INT(w.bands, 1);
    CHECK_EQ_INT(w.band_numbers[0], 2);
    CHECK_EQ_INT(w.bitmap[(size_t)299 * w.bpr], 0xFF);
    CHECK_EQ_INT(w.bitmap[(size_t)300 * w.bpr], 0x00);
    CHECK_EQ_INT(w.bitmap[(size_t)298 * w.bpr], 0x00);
    free(out.data);
    free(w.bitmap);

    /* two pages in one job, the second one white: header and footer only */
    memset(&out, 0, sizeof out);
    CHECK_EQ_INT(m2022_qpdl_begin_job(&e, &job, buf_sink, &out), 0);
    CHECK_EQ_INT(m2022_qpdl_begin_page(&e, WIDTH, ws, ws_bytes), 0);
    memset(row, 0x0F, sizeof row);
    CHECK_EQ_INT(m2022_qpdl_write_line(&e, row), 0);
    CHECK_EQ_INT(m2022_qpdl_end_page(&e), 0);
    CHECK_EQ_INT(m2022_qpdl_begin_page(&e, 1000, ws, ws_bytes), 0);
    CHECK_EQ_INT(m2022_qpdl_write_line(&e, white), 0);
    CHECK_EQ_INT(m2022_qpdl_end_page(&e), 0);
    CHECK_EQ_INT(m2022_qpdl_end_job(&e), 0);
    CHECK_EQ_INT(e.pages, 2);
    CHECK_EQ_INT(e.bands_written, 1);
    CHECK_EQ_INT(walk(out.data, out.len, &w), 0);
    CHECK_EQ_INT(w.pages, 2);
    CHECK_EQ_INT(w.end_pages, 2);
    CHECK_EQ_INT(w.bands, 1);
    free(out.data);
    free(w.bitmap);

    /* errors: order, ranges, workspace, sink, options */
    memset(&out, 0, sizeof out);
    CHECK_EQ_INT(m2022_qpdl_begin_page(&e, WIDTH, ws, ws_bytes), M2022_QPDL_ESTATE); /* no job */
    CHECK_EQ_INT(m2022_qpdl_begin_job(&e, &job, buf_sink, &out), 0);
    CHECK_EQ_INT(m2022_qpdl_write_line(&e, white), M2022_QPDL_ESTATE);
    CHECK_EQ_INT(m2022_qpdl_end_page(&e), M2022_QPDL_ESTATE);
    CHECK_EQ_INT(m2022_qpdl_begin_page(&e, 0, ws, ws_bytes), M2022_QPDL_ERANGE);
    CHECK_EQ_INT(m2022_qpdl_begin_page(&e, 65281, ws, ws_bytes), M2022_QPDL_ERANGE);
    CHECK_EQ_INT(m2022_qpdl_begin_page(&e, WIDTH, ws, ws_bytes - 1), M2022_QPDL_EINVAL);
    CHECK_EQ_INT(m2022_qpdl_begin_page(&e, WIDTH, ws, ws_bytes), 0);
    CHECK_EQ_INT(m2022_qpdl_begin_page(&e, WIDTH, ws, ws_bytes), M2022_QPDL_ESTATE);
    CHECK_EQ_INT(m2022_qpdl_end_job(&e), M2022_QPDL_ESTATE); /* page still open */
    CHECK_EQ_INT(m2022_qpdl_write_line(&e, NULL), M2022_QPDL_EINVAL);
    CHECK_EQ_INT(m2022_qpdl_end_page(&e), 0);
    CHECK_EQ_INT(m2022_qpdl_end_job(&e), 0);
    CHECK_EQ_INT(m2022_qpdl_end_job(&e), M2022_QPDL_ESTATE);
    free(out.data);

    /* 256 bands are one too many for the one-byte band number */
    memset(&out, 0, sizeof out);
    CHECK_EQ_INT(m2022_qpdl_begin_job(&e, &job, buf_sink, &out), 0);
    CHECK_EQ_INT(m2022_qpdl_begin_page(&e, 256, ws, ws_bytes), 0);
    for (uint32_t y = 0; y < 256u * 128u; y++) {
        CHECK_EQ_INT(m2022_qpdl_write_line(&e, white), 0);
    }
    CHECK_EQ_INT(e.band_number, 256);
    CHECK_EQ_INT(m2022_qpdl_write_line(&e, white), 0);
    CHECK_EQ_INT(m2022_qpdl_end_page(&e), M2022_QPDL_ERANGE);
    free(out.data);

    CHECK_EQ_INT(m2022_qpdl_begin_job(&e, &job, failing_sink, NULL), -77);
    job.dpi = 300;
    CHECK_EQ_INT(m2022_qpdl_begin_job(&e, &job, buf_sink, &out), M2022_QPDL_EINVAL);
    m2022_qpdl_job_default(&job);
    job.paper_type = NULL;
    CHECK_EQ_INT(m2022_qpdl_begin_job(&e, &job, buf_sink, &out), M2022_QPDL_EINVAL);
    m2022_qpdl_job_default(&job);
    job.paper_type = "TH\nICK";
    CHECK_EQ_INT(m2022_qpdl_begin_job(&e, &job, buf_sink, &out), M2022_QPDL_EINVAL);
    m2022_qpdl_job_default(&job);
    job.copies = 0;
    CHECK_EQ_INT(m2022_qpdl_begin_job(&e, &job, buf_sink, &out), M2022_QPDL_EINVAL);
    m2022_qpdl_job_default(&job);
    job.feeder = 3;
    CHECK_EQ_INT(m2022_qpdl_begin_job(&e, &job, buf_sink, &out), M2022_QPDL_EINVAL);
    free(ws);
}

/* The vendor's input raster through our pipeline and encoder; compared with the vendor's
 * job for the same page. Returns pixel agreement over the vendor's bands. */
static double whole_page(const char *name, m2022_preset_t preset, walk_t *ours, walk_t *theirs,
                         uint8_t **their_data, buf_t *out)
{
    char path[1024];
    size_t len = 0;
    uint8_t *data;
    m2022_cupsraster_header_t h;
    m2022_pixel_format_t fmt;
    m2022_tone_params_t tone;
    m2022_ht_params_t hp;
    m2022_halftoner_t ht;
    m2022_qpdl_job_t job;
    m2022_qpdl_encoder_t e;
    const m2022_media_t *media;
    uint8_t lut[256], *gray, *ink, *bits;
    void *state, *ws;
    size_t ws_bytes;
    uint64_t same = 0, total = 0;

    snprintf(path, sizeof path, "%s/%s.ras.gz", M2022_FIXTURE_DIR, name);
    data = m2022_read_file(path, &len);
    CHECK(data != NULL);
    CHECK_EQ_INT(m2022_cupsraster_parse_header(data, len, &h), 0);
    CHECK_EQ_INT(m2022_cupsraster_pixel_format(&h, &fmt), 0);
    media = NULL;
    {
        size_t count;
        const m2022_media_t *table = m2022_media_table(&count);
        for (size_t i = 0; i < count; i++) {
            if (strcmp(table[i].vendor_name, h.page_size_name) == 0) {
                media = &table[i];
            }
        }
    }
    CHECK(media != NULL);

    m2022_qpdl_job_default(&job);
    job.paper_code = media->qpdl_code;
    job.paper_width_pt = media->width_pt;
    job.paper_height_pt = media->height_pt;
    job.service_date = "20260902";
    m2022_preset(preset, &tone, &hp);
    m2022_tone_build(&tone, lut);
    gray = malloc(h.width);
    ink = malloc(h.width);
    bits = malloc((h.width + 7) / 8);
    state = malloc(m2022_halftoner_state_bytes(h.width));
    m2022_halftoner_init(&ht, &hp, h.width, state, m2022_halftoner_state_bytes(h.width));
    ws_bytes = m2022_qpdl_encoder_workspace_bytes(h.width);
    ws = malloc(ws_bytes);

    memset(out, 0, sizeof *out);
    CHECK_EQ_INT(m2022_qpdl_begin_job(&e, &job, buf_sink, out), 0);
    CHECK_EQ_INT(m2022_qpdl_begin_page(&e, h.width, ws, ws_bytes), 0);
    for (uint32_t y = 0; y < h.height; y++) {
        const uint8_t *line = data + M2022_CUPSRASTER_HEADER_BYTES + (size_t)y * h.bytes_per_line;
        m2022_raster_line_to_gray(fmt, line, h.width, gray);
        m2022_tone_apply(lut, gray, h.width, ink);
        m2022_halftone_line(&ht, ink, y, bits);
        CHECK_EQ_INT(m2022_qpdl_write_line(&e, bits), 0);
    }
    CHECK_EQ_INT(m2022_qpdl_end_page(&e), 0);
    CHECK_EQ_INT(m2022_qpdl_end_job(&e), 0);

    CHECK_EQ_INT(walk(out->data, out->len, ours), 0);
    snprintf(path, sizeof path, "%s.spl", name);
    CHECK_EQ_INT(walk_file(path, theirs, their_data), 0);
    CHECK_EQ_INT(ours->bad, 0);
    CHECK_EQ_INT(theirs->bad, 0);
    CHECK_EQ_INT(ours->band_width, theirs->band_width);
    CHECK_MEM_EQ(ours->page_raw, theirs->page_raw, 17);
    CHECK_EQ_INT(ours->n_lines, theirs->n_lines);
    for (int i = 0; i < ours->n_lines && i < theirs->n_lines; i++) {
        CHECK_EQ_STR(ours->lines[i], theirs->lines[i]);
    }
    if (ours->bitmap != NULL && theirs->bitmap != NULL) {
        size_t lines = (size_t)(theirs->max_band + 1) * M2022_QPDL_BAND_LINES;
        for (size_t i = 0; i < lines * theirs->bpr; i++) {
            uint8_t a = ours->bitmap[i], b = theirs->bitmap[i];
            same += 8u - (uint64_t)__builtin_popcount((unsigned)(a ^ b));
            total += 8;
        }
    }
    free(data);
    free(gray);
    free(ink);
    free(bits);
    free(state);
    free(ws);
    return total ? (double)same / (double)total : 0.0;
}

static void test_whole_pages(void)
{
    walk_t ours, theirs;
    uint8_t *their_data;
    buf_t out;
    double agreement;

    /* the black square: same 11 bands, same picture */
    agreement = whole_page("black-square-a4", M2022_PRESET_TEXT, &ours, &theirs, &their_data, &out);
    fprintf(stderr, "black-square-a4: ours %d bands / %zu B, vendor %d bands, agreement %.5f\n",
            ours.bands, out.len, theirs.bands, agreement);
    CHECK_EQ_INT(theirs.bands, 11);
    CHECK_EQ_INT(ours.bands, theirs.bands);
    CHECK_MEM_EQ(ours.band_numbers, theirs.band_numbers, 11);
    CHECK(agreement > 0.9999);
    free(out.data);
    free(their_data);
    free(ours.bitmap);
    free(theirs.bitmap);

    /* the text page: same set of bands, the M3 pixel agreement */
    agreement = whole_page("small-text-a4", M2022_PRESET_TEXT, &ours, &theirs, &their_data, &out);
    fprintf(stderr, "small-text-a4: ours %d bands / %zu B, vendor %d bands, agreement %.5f\n",
            ours.bands, out.len, theirs.bands, agreement);
    CHECK_EQ_INT(ours.bands, theirs.bands);
    CHECK_MEM_EQ(ours.band_numbers, theirs.band_numbers, (size_t)theirs.bands);
    CHECK(agreement > 0.999);
    free(out.data);
    free(their_data);
    free(ours.bitmap);
    free(theirs.bitmap);

    /* a blank page: page header and footer, no bands, like the vendor's */
    agreement = whole_page("blank-a4", M2022_PRESET_NORMAL, &ours, &theirs, &their_data, &out);
    CHECK_EQ_INT(ours.bands, 0);
    CHECK_EQ_INT(theirs.bands, 0);
    CHECK_EQ_INT(ours.pages, 1);
    free(out.data);
    free(their_data);
    free(ours.bitmap);
    free(theirs.bitmap);
}

int main(void)
{
    test_envelope_and_records();
    test_media_headers();
    test_band_mechanics();
    test_whole_pages();
    TEST_MAIN_END();
}

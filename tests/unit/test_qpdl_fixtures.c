/*
 * Decode every captured vendor job under fixtures/oracle/samsung (and media/): the walker
 * must accept them all, every 0x11 band must decode to exactly width/8 * 128 bytes with a
 * matching checksum, and the black-square page must contain the square where it belongs.
 * Every decoded band is then re-encoded with our encoder (default table and per-band table),
 * decoded again and compared, and the byte totals are held against the vendor's.
 */
#include "m2022/qpdl.h"

#include "m2022_test.h"

#include <dirent.h>
#include <stdlib.h>

#ifndef M2022_FIXTURE_DIR
#error "M2022_FIXTURE_DIR must point at fixtures/oracle/samsung"
#endif

typedef struct {
    int pages, bands, bad;
    int raw64, raw128, raw_other;
    uint32_t min_raw;
    size_t max_literal;
    uint8_t *band_buf, *rows_buf, *enc_buf, *dec_buf;
    size_t cap;
    /* re-encoding */
    size_t vendor_bytes, ours_default, ours_chosen;
    double worst_ratio; /* chosen-table payload / vendor payload, per band */
    const char *file;   /* being decoded */
    char worst_file[64];
    int worst_band, reencode_bad;
    size_t worst_vendor, worst_ours;
    /* black-square check */
    bool check_square;
    int square_lines_ok, square_lines_seen;
} ctx_t;

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *buf;
    if (f == NULL || fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        return NULL;
    }
    buf = malloc((size_t)size);
    if (buf == NULL || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)size;
    return buf;
}

static void on_page(void *c, size_t o, const m2022_qpdl_page_header_t *h)
{
    (void)o;
    (void)h;
    ((ctx_t *)c)->pages++;
}

/* First black run on a row: start pixel and length; -1 when the row is white. */
static void first_black_run(const uint8_t *row, size_t bpr, int *start, int *length)
{
    *start = -1;
    *length = 0;
    for (size_t x = 0; x < bpr * 8; x++) {
        bool black = (row[x / 8] >> (7 - (x % 8))) & 1;
        if (*start < 0 && black) {
            *start = (int)x;
        } else if (*start >= 0 && !black) {
            *length = (int)x - *start;
            return;
        }
    }
    if (*start >= 0) {
        *length = (int)(bpr * 8) - *start;
    }
}

/* Our encoder on the vendor's band: exact round trip with both table strategies, sizes kept. */
static void reencode(ctx_t *d, const m2022_qpdl_band_header_t *h, size_t expect)
{
    uint16_t table[M2022_CODEC11_TABLE_ENTRIES];
    size_t cap = m2022_codec11_encode_bound(expect), n = 0, out_len = 0;
    double ratio;

    d->vendor_bytes += h->length;
    if (m2022_codec11_encode(d->band_buf, expect, m2022_codec11_default_table, d->enc_buf, cap,
                             &n) != 0 ||
        m2022_codec11_decode(d->enc_buf, n, d->dec_buf, expect, &out_len, NULL) != 0 ||
        out_len != expect || memcmp(d->dec_buf, d->band_buf, expect) != 0) {
        d->reencode_bad++;
        return;
    }
    d->ours_default += n;

    m2022_codec11_choose_table(d->band_buf, expect, table);
    if (m2022_codec11_encode(d->band_buf, expect, table, d->enc_buf, cap, &n) != 0 ||
        m2022_codec11_decode(d->enc_buf, n, d->dec_buf, expect, &out_len, NULL) != 0 ||
        out_len != expect || memcmp(d->dec_buf, d->band_buf, expect) != 0) {
        d->reencode_bad++;
        return;
    }
    d->ours_chosen += n;
    ratio = (double)n / (double)h->length;
    if (ratio > d->worst_ratio) {
        const char *base = strrchr(d->file, '/');
        d->worst_ratio = ratio;
        snprintf(d->worst_file, sizeof d->worst_file, "%s", base ? base + 1 : d->file);
        d->worst_band = h->number;
        d->worst_vendor = h->length;
        d->worst_ours = n;
    }
}

static void ctx_free(ctx_t *d)
{
    free(d->band_buf);
    free(d->rows_buf);
    free(d->enc_buf);
    free(d->dec_buf);
    memset(d, 0, sizeof *d);
}

static void on_band(void *c, size_t o, const m2022_qpdl_band_header_t *h, const uint8_t *payload)
{
    ctx_t *d = c;
    size_t bpr = h->width / 8u, expect = bpr * h->height, out_len = 0;
    m2022_codec11_info_t info;
    int rc;

    (void)o;
    d->bands++;
    if (expect > d->cap) {
        free(d->band_buf);
        free(d->rows_buf);
        free(d->enc_buf);
        free(d->dec_buf);
        d->band_buf = malloc(expect);
        d->rows_buf = malloc(expect);
        d->enc_buf = malloc(m2022_codec11_encode_bound(expect));
        d->dec_buf = malloc(expect);
        d->cap = expect;
    }
    rc = m2022_codec11_decode(payload, h->length, d->band_buf, d->cap, &out_len, &info);
    if (rc != 0 || out_len != expect || h->height != 128 || h->compression != 0x11 ||
        !info.little_endian) {
        d->bad++;
        return;
    }
    /* Vendor habits worth knowing: raw length varies (64 and 128 dominate), literal runs go
     * up to the format's maximum of 128. */
    if (info.raw_len == 64) {
        d->raw64++;
    } else if (info.raw_len == 128) {
        d->raw128++;
    } else {
        d->raw_other++;
    }
    if (info.raw_len < d->min_raw || d->min_raw == 0) {
        d->min_raw = info.raw_len;
    }
    if (info.max_literal > d->max_literal) {
        d->max_literal = info.max_literal;
    }
    reencode(d, h, expect);
    if (d->check_square && h->number >= 22 && h->number <= 30) {
        /* rows fully inside the 50 mm square: black run starts at 1785, 1182 px wide */
        m2022_qpdl_band_to_rows(d->band_buf, bpr, d->rows_buf);
        for (size_t line = 0; line < 128; line++) {
            int start, length;
            first_black_run(d->rows_buf + line * bpr, bpr, &start, &length);
            d->square_lines_seen++;
            if (start == 1785 && length == 1182) {
                d->square_lines_ok++;
            }
        }
    }
}

static int decode_file(const char *path, ctx_t *d, bool check_square)
{
    size_t len = 0, err_off = 0;
    uint8_t *data = read_file(path, &len);
    const m2022_qpdl_visitor_t v = {NULL, on_page, on_band, NULL, NULL};
    int rc;

    if (data == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return -1;
    }
    d->check_square = check_square;
    d->file = path;
    rc = m2022_qpdl_walk(data, len, &v, d, &err_off);
    if (rc != 0) {
        fprintf(stderr, "%s: walk failed at %zu: %s\n", path, err_off, m2022_qpdl_strerror(rc));
    }
    free(data);
    return rc;
}

static void decode_dir(const char *dir, ctx_t *d, int *files)
{
    DIR *dp = opendir(dir);
    struct dirent *e;
    char path[2048];
    if (dp == NULL) {
        return;
    }
    while ((e = readdir(dp)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n < 5 || strcmp(e->d_name + n - 4, ".spl") != 0) {
            continue;
        }
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        CHECK_EQ_INT(decode_file(path, d, false), 0);
        (*files)++;
    }
    closedir(dp);
}

int main(void)
{
    ctx_t d;
    int files = 0;

    memset(&d, 0, sizeof d);

    /* every job in the oracle directory and the media sweep */
    decode_dir(M2022_FIXTURE_DIR, &d, &files);
    decode_dir(M2022_FIXTURE_DIR "/media", &d, &files);
    CHECK(files >= 40);
    CHECK(d.pages >= files);
    CHECK(d.bands > 700);
    CHECK_EQ_INT(d.bad, 0);
    CHECK(d.max_literal <= M2022_CODEC11_MAX_LITERAL);
    CHECK(d.min_raw >= 1);
    fprintf(stderr, "decoded %d files, %d pages, %d bands, %d bad; raw 64: %d, raw 128: %d, "
                    "other: %d (min %u), max literal run %zu\n",
            files, d.pages, d.bands, d.bad, d.raw64, d.raw128, d.raw_other, d.min_raw,
            d.max_literal);

    /* re-encoding: exact everywhere, and not much bigger than the vendor's own streams */
    CHECK_EQ_INT(d.reencode_bad, 0);
    fprintf(stderr, "re-encoded: vendor %zu B, ours default table %zu B (%.3f), per-band table "
                    "%zu B (%.3f), worst band %.2f (%s #%d: vendor %zu B, ours %zu B)\n",
            d.vendor_bytes, d.ours_default, (double)d.ours_default / (double)d.vendor_bytes,
            d.ours_chosen, (double)d.ours_chosen / (double)d.vendor_bytes, d.worst_ratio,
            d.worst_file, d.worst_band, d.worst_vendor, d.worst_ours);
    /* measured 2026-09-02: 0.764 with the per-band table, 0.912 with the default one, and no
     * single band larger than the vendor's; the margins below catch a regression, not noise */
    CHECK(d.ours_chosen <= d.vendor_bytes * 85 / 100);
    CHECK(d.ours_default <= d.vendor_bytes);
    CHECK(d.worst_ratio <= 1.1);
    ctx_free(&d);

    /* geometry of the black square */
    CHECK_EQ_INT(decode_file(M2022_FIXTURE_DIR "/black-square-a4.spl", &d, true), 0);
    CHECK_EQ_INT(d.pages, 1);
    CHECK_EQ_INT(d.bands, 11);
    CHECK_EQ_INT(d.square_lines_seen, 9 * 128);
    CHECK_EQ_INT(d.square_lines_ok, 9 * 128);
    ctx_free(&d);

    /* blank page: one page, no bands */
    CHECK_EQ_INT(decode_file(M2022_FIXTURE_DIR "/blank-a4.spl", &d, false), 0);
    CHECK_EQ_INT(d.pages, 1);
    CHECK_EQ_INT(d.bands, 0);
    ctx_free(&d);

    TEST_MAIN_END();
}

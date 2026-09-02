/*
 * decode FILE [--pbm PREFIX] [--quiet]
 *
 * Explain a captured SPL/QPDL job byte by byte: PJL lines, page headers, every band with its
 * 0x11 payload statistics and checksum, and optionally the reassembled pages as PBM files
 * (PREFIX-p1.pbm, ...), 1 = black, at the band width (which includes right padding).
 */
#include "cli.h"
#include "m2022/qpdl.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BANDS 256

typedef struct {
    bool quiet;
    const char *pbm_prefix;
    int pages;
    int bands_in_page;
    int errors;
    uint16_t band_width;
    int max_band;
    /* per page: decoded rows for each band, assembled at end_page */
    uint8_t *page_rows;   /* MAX_BANDS * 128 * bytes_per_row */
    size_t bytes_per_row;
    uint8_t *band_buf;    /* one decoded band */
    uint8_t *rows_buf;
    size_t distinct_tables;
    uint16_t last_table[M2022_CODEC11_TABLE_ENTRIES];
    bool have_table;
} decode_ctx_t;

static void on_pjl(void *c, size_t offset, const char *line, size_t len)
{
    decode_ctx_t *d = c;
    if (d->quiet) {
        return;
    }
    printf("%08zx  PJL   ", offset);
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)line[i];
        if (ch == 0x1b) {
            fputs("<ESC>", stdout);
        } else {
            putchar(ch);
        }
    }
    putchar('\n');
}

static void on_page(void *c, size_t offset, const m2022_qpdl_page_header_t *h)
{
    decode_ctx_t *d = c;
    d->pages++;
    d->bands_in_page = 0;
    d->max_band = -1;
    d->band_width = 0;
    d->have_table = false;
    printf("%08zx  PAGE  %d: %u00x%u00 dpi, copies %u, paper 0x%02x (%s) %ux%u/300in, feeder %u "
           "(%s), duplex %u tumble %u, qpdl v%u, reserved %u/%u/%u\n",
           offset, d->pages, h->y_res_100, h->x_res_100, h->copies, h->paper_code,
           m2022_qpdl_paper_name(h->paper_code), h->paper_width, h->paper_height, h->feeder,
           m2022_qpdl_feeder_name(h->feeder), h->duplex, h->tumble, h->qpdl_version,
           h->reserved_a, h->reserved_d, h->reserved_f);
}

static void on_band(void *c, size_t offset, const m2022_qpdl_band_header_t *h,
                    const uint8_t *payload)
{
    decode_ctx_t *d = c;
    m2022_codec11_info_t info;
    size_t out_len = 0;
    size_t bpr = h->width / 8u;
    size_t expect = bpr * h->height;
    int rc;

    d->bands_in_page++;
    if (d->band_width == 0) {
        d->band_width = h->width;
    }
    if (h->compression != M2022_QPDL_COMPRESSION_0X11) {
        printf("%08zx  BAND  #%u %ux%u compression 0x%02x (not decoded), %u bytes\n", offset,
               h->number, h->width, h->height, h->compression, h->length);
        return;
    }
    if (d->band_buf == NULL || d->bytes_per_row != bpr) {
        free(d->band_buf);
        free(d->rows_buf);
        free(d->page_rows);
        d->bytes_per_row = bpr;
        d->band_buf = malloc(expect);
        d->rows_buf = malloc(expect);
        d->page_rows = d->pbm_prefix != NULL ? calloc((size_t)MAX_BANDS, expect) : NULL;
        if (d->band_buf == NULL || d->rows_buf == NULL) {
            d->errors++;
            return;
        }
    }
    rc = m2022_codec11_decode(payload, h->length, d->band_buf, expect, &out_len, &info);
    if (!d->have_table || memcmp(d->last_table, info.table, sizeof info.table) != 0) {
        d->distinct_tables++;
        memcpy(d->last_table, info.table, sizeof info.table);
        d->have_table = true;
    }
    if (!d->quiet || rc != 0 || out_len != expect) {
        printf("%08zx  BAND  #%-3u %ux%u 0x%02x %6u B -> %6zu B%s; raw %u, %zu literal (max %zu), "
               "%zu match (max len %zu, max index %zu), checksum %08x %s%s\n",
               offset, h->number, h->width, h->height, h->compression, h->length, out_len,
               out_len == expect ? "" : " SIZE MISMATCH", info.raw_len, info.literal_tokens,
               info.max_literal, info.match_tokens, info.max_match, info.max_index,
               info.checksum_stored, rc == 0 ? "ok" : m2022_qpdl_strerror(rc),
               info.little_endian ? "" : " (big-endian header)");
    }
    if (rc != 0 || out_len != expect) {
        d->errors++;
        return;
    }
    if (d->page_rows != NULL) { /* band numbers are 8-bit, MAX_BANDS covers them all */
        m2022_qpdl_band_to_rows(d->band_buf, bpr, d->page_rows + (size_t)h->number * expect);
        if ((int)h->number > d->max_band) {
            d->max_band = h->number;
        }
    }
}

static void write_pbm(decode_ctx_t *d)
{
    char path[1024];
    FILE *f;
    size_t height;

    if (d->page_rows == NULL || d->max_band < 0) {
        return;
    }
    height = ((size_t)d->max_band + 1) * M2022_QPDL_BAND_LINES;
    snprintf(path, sizeof path, "%s-p%d.pbm", d->pbm_prefix, d->pages);
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "decode: cannot write %s: %s\n", path, strerror(errno));
        d->errors++;
        return;
    }
    fprintf(f, "P4\n%u %zu\n", d->band_width, height);
    fwrite(d->page_rows, 1, height * d->bytes_per_row, f);
    fclose(f);
    printf("          wrote %s (%ux%zu px, bands 0..%d, missing bands white)\n", path,
           d->band_width, height, d->max_band);
    memset(d->page_rows, 0, (size_t)MAX_BANDS * M2022_QPDL_BAND_LINES * d->bytes_per_row);
}

static void on_end_page(void *c, size_t offset, uint16_t copies)
{
    decode_ctx_t *d = c;
    printf("%08zx  ENDPG copies %u; page %d had %d bands, width %u, %zu distinct offset table%s\n",
           offset, copies, d->pages, d->bands_in_page, d->band_width, d->distinct_tables,
           d->distinct_tables == 1 ? "" : "s");
    write_pbm(d);
}

static void on_end_job(void *c, size_t offset)
{
    (void)c;
    printf("%08zx  ENDJOB\n", offset);
}

int cmd_decode(int argc, char **argv)
{
    const char *path = NULL;
    decode_ctx_t d;
    FILE *f;
    long size;
    uint8_t *data;
    size_t err_off = 0;
    int rc;
    m2022_qpdl_visitor_t v = {on_pjl, on_page, on_band, on_end_page, on_end_job};

    memset(&d, 0, sizeof d);
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--pbm") == 0 && i + 1 < argc) {
            d.pbm_prefix = argv[++i];
        } else if (strcmp(argv[i], "--quiet") == 0) {
            d.quiet = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "decode: unknown option '%s'\n", argv[i]);
            return 2;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            fprintf(stderr, "decode: only one FILE is accepted\n");
            return 2;
        }
    }
    if (path == NULL) {
        fprintf(stderr, "Usage: m2022-airbridge decode FILE [--pbm PREFIX] [--quiet]\n");
        return 2;
    }
    f = fopen(path, "rb");
    if (f == NULL || fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "decode: cannot read %s: %s\n", path, strerror(errno));
        return 2;
    }
    data = malloc((size_t)size + 1);
    if (data == NULL || fread(data, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "decode: cannot read %s\n", path);
        free(data);
        fclose(f);
        return 2;
    }
    fclose(f);

    printf("%s: %ld bytes\n", path, size);
    rc = m2022_qpdl_walk(data, (size_t)size, &v, &d, &err_off);
    if (rc != 0) {
        printf("%08zx  ERROR %s\n", err_off, m2022_qpdl_strerror(rc));
        d.errors++;
    }
    printf("%d page(s), %d error(s)\n", d.pages, d.errors);
    free(d.band_buf);
    free(d.rows_buf);
    free(d.page_rows);
    free(data);
    return d.errors == 0 ? 0 : 1;
}

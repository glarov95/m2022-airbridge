/*
 * Job encoder: PJL envelope, page and band records, end of page and job (docs/spl-qpdl.md
 * sections 1 and 2). Every literal below is what the vendor filter wrote in the fixtures;
 * which of the PJL SET lines the printer needs is still an open question (SPEC.md 16.5),
 * so all of them are emitted.
 */
#include "m2022/qpdl.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void m2022_qpdl_job_default(m2022_qpdl_job_t *job)
{
    memset(job, 0, sizeof *job);
    job->paper_code = 0x02; /* A4 */
    job->paper_width_pt = 595;
    job->paper_height_pt = 842;
    job->dpi = 600;
    job->feeder = M2022_QPDL_FEEDER_AUTO;
    job->paper_type = "OFF";
    job->duplex = M2022_QPDL_DUPLEX_OFF;
    job->copies = 1;
}

uint16_t m2022_qpdl_paper_dots(int points, unsigned dpi)
{
    /* 300 dots per inch at 600 dpi, 150 at 1200 (the vendor halves the unit in Best mode) */
    unsigned per_inch = dpi == 1200 ? 150 : 300;
    long dots = ((long)points * (long)per_inch + 36) / 72;
    return dots < 0 ? 0 : dots > 65535 ? 65535 : (uint16_t)dots;
}

size_t m2022_qpdl_encoder_workspace_bytes(uint32_t width)
{
    size_t bpr = ((size_t)width + 255) / 256 * 256 / 8;
    size_t band = bpr * M2022_QPDL_BAND_LINES;
    return band + band + m2022_codec11_encode_bound(band);
}

static int emit(m2022_qpdl_encoder_t *e, const void *data, size_t len)
{
    int rc = e->sink(e->sink_ctx, data, len);
    if (rc == 0) {
        e->bytes_out += len;
    }
    return rc;
}

static int emitf(m2022_qpdl_encoder_t *e, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static int emitf(m2022_qpdl_encoder_t *e, const char *fmt, ...)
{
    char line[256];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof line) {
        return M2022_QPDL_EINVAL;
    }
    return emit(e, line, (size_t)n);
}

static bool plain_text(const char *s, size_t max)
{
    size_t n = strnlen(s, max + 1);
    if (n == 0 || n > max) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (s[i] < 0x20 || s[i] > 0x7e) {
            return false;
        }
    }
    return true;
}

int m2022_qpdl_begin_job(m2022_qpdl_encoder_t *e, const m2022_qpdl_job_t *job,
                         m2022_qpdl_sink_fn sink, void *sink_ctx)
{
    int rc;

    if (e == NULL || job == NULL || sink == NULL) {
        return M2022_QPDL_EINVAL;
    }
    if ((job->dpi != 600 && job->dpi != 1200) ||
        (job->feeder != M2022_QPDL_FEEDER_AUTO && job->feeder != M2022_QPDL_FEEDER_MANUAL) ||
        job->paper_type == NULL || !plain_text(job->paper_type, 32) || job->copies == 0 ||
        job->paper_width_pt <= 0 || job->paper_height_pt <= 0 ||
        job->duplex > M2022_QPDL_DUPLEX_MANUAL_SHORT_EDGE ||
        (job->service_date != NULL && !plain_text(job->service_date, 8)) ||
        (job->producer != NULL && !plain_text(job->producer, 128))) {
        return M2022_QPDL_EINVAL;
    }
    memset(e, 0, sizeof *e);
    e->job = *job;
    e->sink = sink;
    e->sink_ctx = sink_ctx;
    e->in_job = true;

    /* the UEL runs straight into the first PJL line, as in every vendor job */
    if ((rc = emit(e, M2022_QPDL_UEL, M2022_QPDL_UEL_LEN)) != 0) {
        return rc;
    }
    if (job->service_date != NULL &&
        (rc = emitf(e, "@PJL DEFAULT SERVICEDATE=%s\r\n", job->service_date)) != 0) {
        return rc;
    }
    if ((rc = emitf(e, "@PJL COMMENT Model Name : Samsung M2020 Series\r\n")) != 0) {
        return rc;
    }
    if (job->producer != NULL &&
        (rc = emitf(e, "@PJL COMMENT Producer : %s\r\n", job->producer)) != 0) {
        return rc;
    }
    if ((rc = emitf(e, "@PJL SET XIGNOREFF=%s\r\n\r\n",
                    job->skip_blank_pages ? "ON" : "OFF")) != 0) {
        return rc;
    }
    if ((rc = emitf(e, "@PJL SET RESOLUTION = %u\r\n", job->dpi)) != 0) {
        return rc;
    }
    if (job->dpi == 600 && (rc = emitf(e, "@PJL SET BITSPERPIXEL = 1\r\n")) != 0) {
        return rc; /* the vendor drops this line in Best mode */
    }
    if ((rc = emitf(e, "@PJL SET PAPERTYPE = %s\r\n", job->paper_type)) != 0) {
        return rc;
    }
    if ((rc = emitf(e, "@PJL SET DUPLEX = %s\r\n",
                    job->duplex == M2022_QPDL_DUPLEX_OFF ? "OFF" : "MANUAL")) != 0) {
        return rc;
    }
    if (job->duplex != M2022_QPDL_DUPLEX_OFF &&
        (rc = emitf(e, "@PJL SET BINDING = %s\r\n",
                    job->duplex == M2022_QPDL_DUPLEX_MANUAL_LONG_EDGE ? "LONGEDGE"
                                                                       : "SHORTEDGE")) != 0) {
        return rc;
    }
    return emitf(e, "@PJL ENTER LANGUAGE=QPDL\r\n");
}

int m2022_qpdl_begin_page(m2022_qpdl_encoder_t *e, uint32_t width, void *workspace,
                          size_t workspace_bytes)
{
    m2022_qpdl_page_header_t h;
    uint8_t rec[M2022_QPDL_PAGE_HEADER_LEN];
    size_t band_bytes;

    if (e == NULL || !e->in_job || e->in_page) {
        return M2022_QPDL_ESTATE;
    }
    if (width == 0 || width > 65280) {
        return M2022_QPDL_ERANGE;
    }
    if (workspace == NULL || workspace_bytes < m2022_qpdl_encoder_workspace_bytes(width)) {
        return M2022_QPDL_EINVAL;
    }
    e->width = width;
    e->band_width = (uint16_t)((width + 255) / 256 * 256); /* docs/spl-qpdl.md 2.2 */
    e->bytes_per_row = e->band_width / 8u;
    e->row_bytes_used = ((size_t)width + 7) / 8;
    band_bytes = e->bytes_per_row * M2022_QPDL_BAND_LINES;
    e->rows = workspace;
    e->band = e->rows + band_bytes;
    e->payload = e->band + band_bytes;
    e->payload_cap = m2022_codec11_encode_bound(band_bytes);
    memset(e->rows, 0, band_bytes); /* 0 = white in row layout */
    e->lines = 0;
    e->band_number = 0;
    e->in_page = true;

    memset(&h, 0, sizeof h);
    h.y_res_100 = (uint8_t)(e->job.dpi / 100);
    h.x_res_100 = h.y_res_100;
    h.copies = e->job.copies;
    h.paper_code = e->job.paper_code;
    h.paper_width = m2022_qpdl_paper_dots(e->job.paper_width_pt, e->job.dpi);
    h.paper_height = m2022_qpdl_paper_dots(e->job.paper_height_pt, e->job.dpi);
    h.feeder = e->job.feeder;
    h.duplex = 0; /* stays 0 for manual duplex in the vendor's jobs; PJL carries it */
    h.tumble = 0;
    h.qpdl_version = 3;
    h.reserved_f = 1;
    m2022_qpdl_write_page_header(&h, rec);
    return emit(e, rec, sizeof rec);
}

/* Encode and write the band in e->rows, or skip it when it is all white. */
static int flush_band(m2022_qpdl_encoder_t *e)
{
    size_t band_bytes = e->bytes_per_row * M2022_QPDL_BAND_LINES, n = 0;
    uint16_t table[M2022_CODEC11_TABLE_ENTRIES];
    m2022_qpdl_band_header_t h;
    uint8_t rec[M2022_QPDL_BAND_HEADER_LEN];
    bool blank = true;
    int rc;

    if (e->band_number > 255) {
        return M2022_QPDL_ERANGE; /* the band number is one byte */
    }
    for (size_t i = 0; i < band_bytes && blank; i++) {
        blank = e->rows[i] == 0;
    }
    if (blank) {
        e->bands_blank++;
        e->band_number++;
        return 0; /* the vendor omits white bands; rows are already clear */
    }
    m2022_qpdl_rows_to_band(e->rows, e->bytes_per_row, e->band);
    m2022_codec11_choose_table(e->band, band_bytes, table);
    rc = m2022_codec11_encode(e->band, band_bytes, table, e->payload, e->payload_cap, &n);
    if (rc != 0) {
        return rc;
    }
    h.number = (uint8_t)e->band_number;
    h.width = e->band_width;
    h.height = M2022_QPDL_BAND_LINES;
    h.compression = M2022_QPDL_COMPRESSION_0X11;
    h.length = (uint32_t)n;
    m2022_qpdl_write_band_header(&h, rec);
    if ((rc = emit(e, rec, sizeof rec)) != 0 || (rc = emit(e, e->payload, n)) != 0) {
        return rc;
    }
    memset(e->rows, 0, band_bytes);
    e->bands_written++;
    e->band_number++;
    return 0;
}

int m2022_qpdl_write_line(m2022_qpdl_encoder_t *e, const uint8_t *bits)
{
    uint8_t *row;

    if (e == NULL || !e->in_page) {
        return M2022_QPDL_ESTATE;
    }
    if (bits == NULL) {
        return M2022_QPDL_EINVAL;
    }
    row = e->rows + (size_t)(e->lines % M2022_QPDL_BAND_LINES) * e->bytes_per_row;
    memcpy(row, bits, e->row_bytes_used);
    if (e->width % 8 != 0) { /* bits past the raster width are padding, and padding is white */
        row[e->row_bytes_used - 1] &= (uint8_t)(0xFF << (8 - e->width % 8));
    }
    e->lines++;
    return e->lines % M2022_QPDL_BAND_LINES == 0 ? flush_band(e) : 0;
}

int m2022_qpdl_end_page(m2022_qpdl_encoder_t *e)
{
    uint8_t rec[M2022_QPDL_END_PAGE_LEN];
    int rc;

    if (e == NULL || !e->in_page) {
        return M2022_QPDL_ESTATE;
    }
    if (e->lines % M2022_QPDL_BAND_LINES != 0 && (rc = flush_band(e)) != 0) {
        return rc; /* the unwritten lines of the last band are white already */
    }
    e->in_page = false;
    e->pages++;
    m2022_qpdl_write_end_page(e->job.copies, rec);
    return emit(e, rec, sizeof rec);
}

int m2022_qpdl_end_job(m2022_qpdl_encoder_t *e)
{
    static const uint8_t end_job = M2022_QPDL_REC_END_JOB;
    int rc;

    if (e == NULL || !e->in_job || e->in_page) {
        return M2022_QPDL_ESTATE;
    }
    e->in_job = false;
    if ((rc = emit(e, &end_job, 1)) != 0) {
        return rc;
    }
    return emit(e, M2022_QPDL_UEL, M2022_QPDL_UEL_LEN);
}

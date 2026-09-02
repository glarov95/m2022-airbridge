#include "m2022/qpdl.h"

#include "m2022_test.h"

/* Bytes taken verbatim from the vendor fixtures (docs/spl-qpdl.md). */
static const uint8_t A4_600[17] = {0x00, 0x06, 0x00, 0x01, 0x02, 0x09, 0xaf, 0x0d, 0xb4,
                                   0x01, 0x00, 0x00, 0x00, 0x00, 0x03, 0x01, 0x06};
static const uint8_t LETTER_600[17] = {0x00, 0x06, 0x00, 0x01, 0x00, 0x09, 0xf6, 0x0c, 0xe4,
                                       0x01, 0x00, 0x00, 0x00, 0x00, 0x03, 0x01, 0x06};
static const uint8_t A4_1200[17] = {0x00, 0x0c, 0x00, 0x01, 0x02, 0x04, 0xd8, 0x06, 0xda,
                                    0x01, 0x00, 0x00, 0x00, 0x00, 0x03, 0x01, 0x0c};
static const uint8_t A4_MANUAL[17] = {0x00, 0x06, 0x00, 0x01, 0x02, 0x09, 0xaf, 0x0d, 0xb4,
                                      0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x01, 0x06};
static const uint8_t BAND21[11] = {0x0c, 0x15, 0x13, 0x00, 0x00, 0x80, 0x11, 0x00, 0x00, 0x02, 0x4c};

typedef struct {
    int pjl, pages, bands, end_pages, end_jobs;
    uint16_t copies;
} counts_t;

static void c_pjl(void *c, size_t o, const char *l, size_t n)
{
    (void)o;
    (void)l;
    (void)n;
    ((counts_t *)c)->pjl++;
}
static void c_page(void *c, size_t o, const m2022_qpdl_page_header_t *h)
{
    (void)o;
    (void)h;
    ((counts_t *)c)->pages++;
}
static void c_band(void *c, size_t o, const m2022_qpdl_band_header_t *h, const uint8_t *p)
{
    (void)o;
    (void)h;
    (void)p;
    ((counts_t *)c)->bands++;
}
static void c_end_page(void *c, size_t o, uint16_t copies)
{
    (void)o;
    ((counts_t *)c)->end_pages++;
    ((counts_t *)c)->copies = copies;
}
static void c_end_job(void *c, size_t o)
{
    (void)o;
    ((counts_t *)c)->end_jobs++;
}

int main(void)
{
    m2022_qpdl_page_header_t h;
    m2022_qpdl_band_header_t b;
    uint8_t buf[17];
    counts_t counts;
    size_t err_off = 0;
    const m2022_qpdl_visitor_t v = {c_pjl, c_page, c_band, c_end_page, c_end_job};

    /* page headers */
    CHECK_EQ_INT(m2022_qpdl_parse_page_header(A4_600, sizeof A4_600, &h), 17);
    CHECK_EQ_INT(h.y_res_100, 6);
    CHECK_EQ_INT(h.x_res_100, 6);
    CHECK_EQ_INT(h.copies, 1);
    CHECK_EQ_INT(h.paper_code, 2);
    CHECK_EQ_STR(m2022_qpdl_paper_name(h.paper_code), "A4");
    CHECK_EQ_INT(h.paper_width, 2479);
    CHECK_EQ_INT(h.paper_height, 3508);
    CHECK_EQ_INT(h.feeder, 1);
    CHECK_EQ_STR(m2022_qpdl_feeder_name(h.feeder), "auto");
    CHECK_EQ_INT(h.duplex, 0);
    CHECK_EQ_INT(h.qpdl_version, 3);
    CHECK_EQ_INT(h.reserved_f, 1);
    m2022_qpdl_write_page_header(&h, buf);
    CHECK_MEM_EQ(buf, A4_600, 17);

    CHECK_EQ_INT(m2022_qpdl_parse_page_header(LETTER_600, 17, &h), 17);
    CHECK_EQ_STR(m2022_qpdl_paper_name(h.paper_code), "Letter");
    CHECK_EQ_INT(h.paper_width, 2550);
    CHECK_EQ_INT(h.paper_height, 3300);

    CHECK_EQ_INT(m2022_qpdl_parse_page_header(A4_1200, 17, &h), 17);
    CHECK_EQ_INT(h.y_res_100, 12);
    CHECK_EQ_INT(h.x_res_100, 12);
    CHECK_EQ_INT(h.paper_width, 1240); /* the vendor halves the unit at 1200 dpi */

    CHECK_EQ_INT(m2022_qpdl_parse_page_header(A4_MANUAL, 17, &h), 17);
    CHECK_EQ_STR(m2022_qpdl_feeder_name(h.feeder), "manual");

    CHECK_EQ_INT(m2022_qpdl_parse_page_header(A4_600, 16, &h), M2022_QPDL_ETRUNCATED);
    CHECK_EQ_INT(m2022_qpdl_parse_page_header(BAND21, 17, &h), M2022_QPDL_ERECORD);

    /* band header */
    CHECK_EQ_INT(m2022_qpdl_parse_band_header(BAND21, 11, &b), 11);
    CHECK_EQ_INT(b.number, 21);
    CHECK_EQ_INT(b.width, 4864);
    CHECK_EQ_INT(b.height, 128);
    CHECK_EQ_INT(b.compression, 0x11);
    CHECK_EQ_INT(b.length, 588);
    m2022_qpdl_write_band_header(&b, buf);
    CHECK_MEM_EQ(buf, BAND21, 11);
    CHECK_EQ_INT(m2022_qpdl_parse_band_header(BAND21, 10, &b), M2022_QPDL_ETRUNCATED);

    /* paper table spot checks from the media sweep */
    CHECK_EQ_STR(m2022_qpdl_paper_name(0x10), "A5");
    CHECK_EQ_STR(m2022_qpdl_paper_name(0x18), "US-Folio");
    CHECK_EQ_STR(m2022_qpdl_paper_name(0x1c), "Oficio");
    CHECK_EQ_STR(m2022_qpdl_paper_name(0x0d), "Postcard_S");
    CHECK_EQ_STR(m2022_qpdl_paper_name(0x7f), "unknown");

    /* walker on a synthetic minimal job: UEL + PJL, page, end page, end job, UEL */
    {
        uint8_t job[256];
        size_t n = 0;
        const char *pjl = "\x1b%-12345X@PJL DEFAULT SERVICEDATE=20260902\r\n"
                          "@PJL ENTER LANGUAGE=QPDL\r\n";
        memcpy(job + n, pjl, strlen(pjl));
        n += strlen(pjl);
        memcpy(job + n, A4_600, 17);
        n += 17;
        m2022_qpdl_write_end_page(2, job + n);
        n += 3;
        job[n++] = 0x09;
        memcpy(job + n, M2022_QPDL_UEL, M2022_QPDL_UEL_LEN);
        n += M2022_QPDL_UEL_LEN;

        memset(&counts, 0, sizeof counts);
        CHECK_EQ_INT(m2022_qpdl_walk(job, n, &v, &counts, &err_off), 0);
        CHECK_EQ_INT(counts.pjl, 3); /* two header lines + the trailing UEL line */
        CHECK_EQ_INT(counts.pages, 1);
        CHECK_EQ_INT(counts.bands, 0);
        CHECK_EQ_INT(counts.end_pages, 1);
        CHECK_EQ_INT(counts.copies, 2);
        CHECK_EQ_INT(counts.end_jobs, 1);

        /* truncated inside the page header */
        memset(&counts, 0, sizeof counts);
        CHECK_EQ_INT(m2022_qpdl_walk(job, strlen(pjl) + 10, &v, &counts, &err_off),
                     M2022_QPDL_ETRUNCATED);
        CHECK_EQ_INT(err_off, strlen(pjl));

        /* unknown record */
        job[strlen(pjl)] = 0x42;
        CHECK_EQ_INT(m2022_qpdl_walk(job, n, &v, &counts, &err_off), M2022_QPDL_ERECORD);
        CHECK_EQ_INT(err_off, strlen(pjl));
        job[strlen(pjl)] = 0x00;

        /* no PJL at all */
        CHECK_EQ_INT(m2022_qpdl_walk(A4_600, 17, &v, &counts, &err_off), M2022_QPDL_ENOPJL);

        /* NULL callbacks are fine */
        CHECK_EQ_INT(m2022_qpdl_walk(job, n, NULL, NULL, NULL), 0);
    }

    TEST_MAIN_END();
}

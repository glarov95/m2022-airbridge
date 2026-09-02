#include "m2022/qpdl.h"

#include <string.h>

static uint16_t rd16be(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t rd32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void wr16be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void wr32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

int m2022_qpdl_parse_page_header(const uint8_t *p, size_t len, m2022_qpdl_page_header_t *h)
{
    if (len < M2022_QPDL_PAGE_HEADER_LEN) {
        return M2022_QPDL_ETRUNCATED;
    }
    if (p[0] != M2022_QPDL_REC_PAGE) {
        return M2022_QPDL_ERECORD;
    }
    memset(h, 0, sizeof *h);
    h->y_res_100 = p[1];
    h->copies = rd16be(p + 2);
    h->paper_code = p[4];
    h->paper_width = rd16be(p + 5);
    h->paper_height = rd16be(p + 7);
    h->feeder = p[9];
    h->reserved_a = p[10];
    h->duplex = p[11];
    h->tumble = p[12];
    h->reserved_d = p[13];
    h->qpdl_version = p[14];
    h->reserved_f = p[15];
    h->x_res_100 = p[16];
    return M2022_QPDL_PAGE_HEADER_LEN;
}

void m2022_qpdl_write_page_header(const m2022_qpdl_page_header_t *h, uint8_t *out)
{
    out[0] = M2022_QPDL_REC_PAGE;
    out[1] = h->y_res_100;
    wr16be(out + 2, h->copies);
    out[4] = h->paper_code;
    wr16be(out + 5, h->paper_width);
    wr16be(out + 7, h->paper_height);
    out[9] = h->feeder;
    out[10] = h->reserved_a;
    out[11] = h->duplex;
    out[12] = h->tumble;
    out[13] = h->reserved_d;
    out[14] = h->qpdl_version;
    out[15] = h->reserved_f;
    out[16] = h->x_res_100;
}

int m2022_qpdl_parse_band_header(const uint8_t *p, size_t len, m2022_qpdl_band_header_t *h)
{
    if (len < M2022_QPDL_BAND_HEADER_LEN) {
        return M2022_QPDL_ETRUNCATED;
    }
    if (p[0] != M2022_QPDL_REC_BAND) {
        return M2022_QPDL_ERECORD;
    }
    h->number = p[1];
    h->width = rd16be(p + 2);
    h->height = rd16be(p + 4);
    h->compression = p[6];
    h->length = rd32be(p + 7);
    return M2022_QPDL_BAND_HEADER_LEN;
}

void m2022_qpdl_write_band_header(const m2022_qpdl_band_header_t *h, uint8_t *out)
{
    out[0] = M2022_QPDL_REC_BAND;
    out[1] = h->number;
    wr16be(out + 2, h->width);
    wr16be(out + 4, h->height);
    out[6] = h->compression;
    wr32be(out + 7, h->length);
}

void m2022_qpdl_write_end_page(uint16_t copies, uint8_t *out)
{
    out[0] = M2022_QPDL_REC_END_PAGE;
    wr16be(out + 1, copies);
}

const char *m2022_qpdl_paper_name(uint8_t code)
{
    /* Codes 0x00-0x18 per the SpliX document; 0x0D, 0x18, 0x1C confirmed or discovered in the
     * vendor media sweep (docs/spl-qpdl.md 2.1). Names follow the vendor PPD. */
    switch (code) {
    case 0x00:
        return "Letter";
    case 0x01:
        return "Legal";
    case 0x02:
        return "A4";
    case 0x03:
        return "Executive";
    case 0x04:
        return "Ledger";
    case 0x05:
        return "A3";
    case 0x06:
        return "Env10";
    case 0x07:
        return "EnvMonarch";
    case 0x08:
        return "EnvC5";
    case 0x09:
        return "EnvDL";
    case 0x0A:
        return "B4-JIS";
    case 0x0B:
        return "B5-JIS";
    case 0x0C:
        return "B5-ISO";
    case 0x0D:
        return "Postcard_S";
    case 0x0E:
        return "JPost";
    case 0x0F:
        return "JDouble";
    case 0x10:
        return "A5";
    case 0x11:
        return "A6";
    case 0x12:
        return "B6-JIS";
    case 0x15:
        return "Custom";
    case 0x17:
        return "EnvC6";
    case 0x18:
        return "US-Folio";
    case 0x1C:
        return "Oficio";
    default:
        return "unknown";
    }
}

const char *m2022_qpdl_feeder_name(uint8_t code)
{
    switch (code) {
    case 1:
        return "auto";
    case 2:
        return "manual";
    case 3:
        return "multi";
    case 4:
        return "top";
    case 5:
        return "bottom";
    case 6:
        return "envelopes";
    case 7:
        return "third";
    default:
        return "unknown";
    }
}

const char *m2022_qpdl_strerror(int err)
{
    switch (err) {
    case M2022_QPDL_OK:
        return "success";
    case M2022_QPDL_ETRUNCATED:
        return "data ends inside a record or token";
    case M2022_QPDL_EMAGIC:
        return "band payload does not start with the 0x09ABCDEF magic";
    case M2022_QPDL_ERAWLEN:
        return "raw data length above 128";
    case M2022_QPDL_EOFFSET:
        return "match offset reaches before the start of the band";
    case M2022_QPDL_EOVERFLOW:
        return "decoded band larger than the output buffer";
    case M2022_QPDL_ECHECKSUM:
        return "band checksum mismatch";
    case M2022_QPDL_ERECORD:
        return "unknown record type";
    case M2022_QPDL_ENOPJL:
        return "no @PJL ENTER LANGUAGE line";
    case M2022_QPDL_EINVAL:
        return "invalid argument";
    case M2022_QPDL_ERANGE:
        return "page wider than 65280 pixels or taller than 255 bands";
    case M2022_QPDL_ESTATE:
        return "encoder call out of order";
    default:
        return "unknown error";
    }
}

/*
 * SPL/QPDL as the Samsung M2020 Series speaks it (docs/spl-qpdl.md).
 *
 * Pure module: memory in, memory or callbacks out; no I/O, no allocation per line. Record
 * layouts and the 0x11 band codec are taken from the unofficial SpliX SPL2 document and
 * confirmed byte by byte against the vendor output in fixtures/oracle/samsung/.
 */
#ifndef M2022_QPDL_H
#define M2022_QPDL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define M2022_QPDL_BAND_LINES 128
#define M2022_QPDL_PAGE_HEADER_LEN 17
#define M2022_QPDL_BAND_HEADER_LEN 11
#define M2022_QPDL_END_PAGE_LEN 3
#define M2022_QPDL_UEL "\x1b%-12345X"
#define M2022_QPDL_UEL_LEN 9

#define M2022_CODEC11_MAGIC 0x09ABCDEFu
#define M2022_CODEC11_TABLE_ENTRIES 64
#define M2022_CODEC11_HEADER_LEN 136 /* magic, raw length, 64 table entries */
#define M2022_CODEC11_MAX_RAW 128
#define M2022_CODEC11_MAX_LITERAL 128 /* 1..128; the vendor uses the full range */
#define M2022_CODEC11_MAX_MATCH 514   /* 3 + 511 */
#define M2022_CODEC11_CHECKSUM_LEN 4

enum {
    M2022_QPDL_REC_PAGE = 0x00,
    M2022_QPDL_REC_END_PAGE = 0x01,
    M2022_QPDL_REC_END_JOB = 0x09,
    M2022_QPDL_REC_BAND = 0x0C,
};

enum { M2022_QPDL_COMPRESSION_0X11 = 0x11 };

enum {
    M2022_QPDL_OK = 0,
    M2022_QPDL_ETRUNCATED = -2000, /* data ends inside a record or token */
    M2022_QPDL_EMAGIC = -2001,     /* 0x11 payload does not start with 0x09ABCDEF */
    M2022_QPDL_ERAWLEN = -2002,    /* raw length above 128 */
    M2022_QPDL_EOFFSET = -2003,    /* match reaches before the start of the output */
    M2022_QPDL_EOVERFLOW = -2004,  /* decoded data exceeds the output buffer */
    M2022_QPDL_ECHECKSUM = -2005,  /* payload decoded, but the stored checksum differs */
    M2022_QPDL_ERECORD = -2006,    /* unknown record type */
    M2022_QPDL_ENOPJL = -2007,     /* no "@PJL ENTER LANGUAGE=QPDL" line */
    M2022_QPDL_EINVAL = -2008,
};

const char *m2022_qpdl_strerror(int err);

/* ---- records ------------------------------------------------------------------------- */

typedef struct {
    uint8_t y_res_100; /* 6 = 600 dpi, 12 = 1200 dpi */
    uint16_t copies;
    uint8_t paper_code;
    uint16_t paper_width;  /* 1/300 in at 600 dpi; the vendor halves the unit at 1200 dpi */
    uint16_t paper_height;
    uint8_t feeder; /* 1 auto, 2 manual, ... (m2022_qpdl_feeder_name) */
    uint8_t reserved_a; /* always 0 in the fixtures */
    uint8_t duplex;
    uint8_t tumble;
    uint8_t reserved_d; /* always 0 */
    uint8_t qpdl_version; /* 3 */
    uint8_t reserved_f;   /* always 1 */
    uint8_t x_res_100;
} m2022_qpdl_page_header_t;

typedef struct {
    uint8_t number;
    uint16_t width;  /* pixels; ceil(raster width / 256) * 256 on this printer */
    uint16_t height; /* lines; 128 */
    uint8_t compression;
    uint32_t length; /* payload bytes that follow the header */
} m2022_qpdl_band_header_t;

/* Parse fixed-size records. Return the bytes consumed, or M2022_QPDL_ETRUNCATED. */
int m2022_qpdl_parse_page_header(const uint8_t *p, size_t len, m2022_qpdl_page_header_t *h);
int m2022_qpdl_parse_band_header(const uint8_t *p, size_t len, m2022_qpdl_band_header_t *h);

/* Serialise records (the encoder side). */
void m2022_qpdl_write_page_header(const m2022_qpdl_page_header_t *h, uint8_t *out);
void m2022_qpdl_write_band_header(const m2022_qpdl_band_header_t *h, uint8_t *out);
void m2022_qpdl_write_end_page(uint16_t copies, uint8_t *out);

const char *m2022_qpdl_paper_name(uint8_t code);  /* "A4", "Letter", ... or "unknown" */
const char *m2022_qpdl_feeder_name(uint8_t code); /* "auto", "manual", ... */

/* ---- job walker ---------------------------------------------------------------------- */

typedef struct {
    /* Each PJL line without its line ending; the UEL that opens the file arrives glued to the
     * first line, as in the stream. Also called for PJL text after the end-of-job UEL. */
    void (*pjl_line)(void *ctx, size_t offset, const char *line, size_t len);
    void (*page)(void *ctx, size_t offset, const m2022_qpdl_page_header_t *h);
    void (*band)(void *ctx, size_t offset, const m2022_qpdl_band_header_t *h,
                 const uint8_t *payload);
    void (*end_page)(void *ctx, size_t offset, uint16_t copies);
    void (*end_job)(void *ctx, size_t offset);
} m2022_qpdl_visitor_t;

/* Walk a complete job. Callbacks may be NULL. On error, *error_offset (if given) points at
 * the offending byte. */
int m2022_qpdl_walk(const uint8_t *data, size_t len, const m2022_qpdl_visitor_t *v, void *ctx,
                    size_t *error_offset);

/* ---- band codec 0x11 ----------------------------------------------------------------- */

typedef struct {
    bool little_endian; /* endianness of the magic, raw length and table */
    uint32_t raw_len;
    uint16_t table[M2022_CODEC11_TABLE_ENTRIES];
    uint32_t checksum_stored;
    uint32_t checksum_computed;
    size_t stream_len; /* token bytes */
    size_t literal_tokens, match_tokens;
    size_t max_literal, max_match, max_index;
} m2022_codec11_info_t;

/*
 * Decode one band payload (everything after the 11-byte band header, checksum included).
 * *out_len is set even on error (partial output). Returns 0, M2022_QPDL_ECHECKSUM when the
 * data decoded fully but the checksum differs, or another error.
 */
int m2022_codec11_decode(const uint8_t *payload, size_t len, uint8_t *out, size_t out_cap,
                         size_t *out_len, m2022_codec11_info_t *info);

/* ---- band layout --------------------------------------------------------------------- */

/*
 * A decoded band is column-major by byte (byte k of line 0, byte k of line 1, ... for the
 * 128 lines, then byte k+1) with 1 = white. Rows are the usual row-major layout with
 * 1 = black (PBM convention). Both buffers hold 128 * bytes_per_row bytes.
 */
void m2022_qpdl_band_to_rows(const uint8_t *band, size_t bytes_per_row, uint8_t *rows);
void m2022_qpdl_rows_to_band(const uint8_t *rows, size_t bytes_per_row, uint8_t *band);

#endif /* M2022_QPDL_H */

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
#define M2022_CODEC11_MIN_RAW 64 /* shortest raw prefix in 867 vendor bands; we never go lower */

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
    M2022_QPDL_EOVERFLOW = -2004,  /* output buffer too small (decoded data, or encoded payload) */
    M2022_QPDL_ECHECKSUM = -2005,  /* payload decoded, but the stored checksum differs */
    M2022_QPDL_ERECORD = -2006,    /* unknown record type */
    M2022_QPDL_ENOPJL = -2007,     /* no "@PJL ENTER LANGUAGE=QPDL" line */
    M2022_QPDL_EINVAL = -2008,
    M2022_QPDL_ERANGE = -2009, /* page wider than 65280 px or taller than 255 bands */
    M2022_QPDL_ESTATE = -2010, /* encoder call out of order (page without job, line without page) */
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

/*
 * Encode one band (column-major, 1 = white; see the band layout below) into a payload the
 * printer accepts: little-endian header, a raw prefix of 64..128 bytes holding the leading
 * literals (the vendor's habit, measured on every fixture band), greedy longest-match tokens
 * over the table's 64 distances, and the big-endian checksum. Table entries of 0 are never
 * used; duplicates are harmless. Pure, deterministic, no allocation.
 *
 * Returns 0, or M2022_QPDL_EOVERFLOW when out_cap is smaller than the payload; *out_len is
 * the payload length on success. Size the buffer with m2022_codec11_encode_bound().
 */
int m2022_codec11_encode(const uint8_t *band, size_t len,
                         const uint16_t table[M2022_CODEC11_TABLE_ENTRIES], uint8_t *out,
                         size_t out_cap, size_t *out_len);

/* Largest payload m2022_codec11_encode() can produce for a band of len bytes. */
size_t m2022_codec11_encode_bound(size_t len);

/*
 * The 64 distances that cover the most bytes of this band under a greedy parse (found with a
 * 3-byte hash of recent positions, distances up to 4096); slots left over are filled from the
 * default table. The result has no zero and no duplicate entries. Bands are at most a few
 * hundred KB, so positions fit 32 bits.
 */
void m2022_codec11_choose_table(const uint8_t *band, size_t len,
                                uint16_t table[M2022_CODEC11_TABLE_ENTRIES]);

/*
 * A fixed table for callers that skip the per-band choice: the distances that carry the
 * vendor's own streams (docs/spl-qpdl.md section 3.3): the line above (1) and the
 * byte-columns to the left (multiples of 128) with their diagonal neighbours, small vertical
 * periods for ordered screens, and a few further columns.
 */
extern const uint16_t m2022_codec11_default_table[M2022_CODEC11_TABLE_ENTRIES];

/* ---- band layout --------------------------------------------------------------------- */

/*
 * A decoded band is column-major by byte (byte k of line 0, byte k of line 1, ... for the
 * 128 lines, then byte k+1) with 1 = white. Rows are the usual row-major layout with
 * 1 = black (PBM convention). Both buffers hold 128 * bytes_per_row bytes.
 */
void m2022_qpdl_band_to_rows(const uint8_t *band, size_t bytes_per_row, uint8_t *rows);
void m2022_qpdl_rows_to_band(const uint8_t *rows, size_t bytes_per_row, uint8_t *band);

/* ---- job encoder --------------------------------------------------------------------- */

/*
 * Writes a complete job the way the vendor filter does (docs/spl-qpdl.md 1, 2): the PJL
 * envelope, one page header per page, a 0x0C record per non-blank 128-line band (column-major,
 * 0x11-compressed with a per-band table), the end-of-page record, and the end-of-job byte
 * followed by the UEL. Lines arrive one at a time as packed 1-bit rows, 1 = black, so a
 * band-based caller (PAPPL delivers lines) never holds a whole page. Bytes go to a sink
 * callback; the only memory is a per-page workspace the caller provides.
 */

enum { M2022_QPDL_FEEDER_AUTO = 1, M2022_QPDL_FEEDER_MANUAL = 2 };

typedef enum {
    M2022_QPDL_DUPLEX_OFF = 0,
    M2022_QPDL_DUPLEX_MANUAL_LONG_EDGE,  /* @PJL SET DUPLEX = MANUAL, BINDING = LONGEDGE */
    M2022_QPDL_DUPLEX_MANUAL_SHORT_EDGE, /* ..., BINDING = SHORTEDGE */
} m2022_qpdl_duplex_t;

typedef struct {
    uint8_t paper_code;                  /* docs/spl-qpdl.md 2.1; the table is in m2022/media.h */
    int paper_width_pt, paper_height_pt; /* the PPD PaperDimension, converted as the vendor does */
    unsigned dpi;                        /* 600 or 1200 */
    uint8_t feeder;                      /* M2022_QPDL_FEEDER_AUTO or _MANUAL */
    const char *paper_type;   /* PJL PAPERTYPE value: OFF, NORMAL, THICK, THIN, BOND, ... */
    m2022_qpdl_duplex_t duplex;
    bool skip_blank_pages;    /* @PJL SET XIGNOREFF=ON */
    uint16_t copies;          /* page header and end-of-page copies fields; the vendor writes 1 */
    const char *service_date; /* "YYYYMMDD" for @PJL DEFAULT SERVICEDATE; NULL omits the line */
    const char *producer;     /* text for a @PJL COMMENT line; NULL omits it */
} m2022_qpdl_job_t;

/* Sink for encoded bytes. Return 0, or a negative value that the encoder passes back. */
typedef int (*m2022_qpdl_sink_fn)(void *ctx, const uint8_t *data, size_t len);

typedef struct {
    m2022_qpdl_job_t job;
    m2022_qpdl_sink_fn sink;
    void *sink_ctx;
    uint32_t width;        /* raster width of the current page, pixels */
    uint16_t band_width;   /* ceil(width / 256) * 256 */
    size_t bytes_per_row;  /* band_width / 8 */
    size_t row_bytes_used; /* (width + 7) / 8 */
    uint32_t lines;        /* lines received on the current page */
    unsigned band_number;  /* of the band being filled; blank bands count too */
    uint8_t *rows, *band, *payload; /* slices of the page workspace */
    size_t payload_cap;
    bool in_job, in_page;
    unsigned pages, bands_written, bands_blank;
    size_t bytes_out;
} m2022_qpdl_encoder_t;

/* A4, 600 dpi, auto feeder, PAPERTYPE OFF, no duplex, one copy, no date, no producer. */
void m2022_qpdl_job_default(m2022_qpdl_job_t *job);

/* Page header paper size: points to 1/300 in (600 dpi) or 1/150 in (1200 dpi), rounded half
 * up; reproduces the vendor's value for all 14 sizes (Env C5 459 pt -> 1913). */
uint16_t m2022_qpdl_paper_dots(int points, unsigned dpi);

/* Workspace a page of `width` pixels needs (rows, column-major band, payload). */
size_t m2022_qpdl_encoder_workspace_bytes(uint32_t width);

int m2022_qpdl_begin_job(m2022_qpdl_encoder_t *e, const m2022_qpdl_job_t *job,
                         m2022_qpdl_sink_fn sink, void *sink_ctx);
int m2022_qpdl_begin_page(m2022_qpdl_encoder_t *e, uint32_t width, void *workspace,
                          size_t workspace_bytes);
/* One packed row of (width + 7) / 8 bytes, 1 = black. Bits past `width` are ignored. */
int m2022_qpdl_write_line(m2022_qpdl_encoder_t *e, const uint8_t *bits);
/* Flushes a partial last band padded with white, writes the end-of-page record. */
int m2022_qpdl_end_page(m2022_qpdl_encoder_t *e);
int m2022_qpdl_end_job(m2022_qpdl_encoder_t *e);

#endif /* M2022_QPDL_H */

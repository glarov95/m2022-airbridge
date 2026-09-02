#include "m2022/qpdl.h"

#include <string.h>

#define MAX_PJL_LINE 4096

static bool is_text_byte(uint8_t c)
{
    return c == '\r' || c == '\n' || c == '\t' || c == 0x1b || (c >= 0x20 && c < 0x7f);
}

/* Emit text lines from `pos` until `len` or the first non-text byte. Returns the new pos. */
static size_t emit_lines(const uint8_t *data, size_t len, size_t pos,
                         const m2022_qpdl_visitor_t *v, void *ctx, bool *saw_enter_language)
{
    while (pos < len && is_text_byte(data[pos])) {
        size_t nl = pos;
        size_t end;
        while (nl < len && data[nl] != '\n' && nl - pos < MAX_PJL_LINE) {
            if (!is_text_byte(data[nl])) {
                return pos; /* binary inside a line: not PJL */
            }
            nl++;
        }
        end = nl;
        if (end > pos && data[end - 1] == '\r') {
            end--;
        }
        if (v != NULL && v->pjl_line != NULL) {
            v->pjl_line(ctx, pos, (const char *)data + pos, end - pos);
        }
        if (saw_enter_language != NULL && end - pos >= 19 &&
            memmem(data + pos, end - pos, "ENTER LANGUAGE", 14) != NULL) {
            *saw_enter_language = true;
            return nl < len ? nl + 1 : len;
        }
        pos = nl < len ? nl + 1 : len;
    }
    return pos;
}

int m2022_qpdl_walk(const uint8_t *data, size_t len, const m2022_qpdl_visitor_t *v, void *ctx,
                    size_t *error_offset)
{
    size_t pos;
    bool entered = false;
    int rc;

#define FAIL(code)                                                                             \
    do {                                                                                       \
        if (error_offset != NULL) {                                                            \
            *error_offset = pos;                                                               \
        }                                                                                      \
        return (code);                                                                         \
    } while (0)

    pos = emit_lines(data, len, 0, v, ctx, &entered);
    if (!entered) {
        FAIL(M2022_QPDL_ENOPJL);
    }

    while (pos < len) {
        uint8_t t = data[pos];
        if (t == M2022_QPDL_REC_PAGE) {
            m2022_qpdl_page_header_t h;
            rc = m2022_qpdl_parse_page_header(data + pos, len - pos, &h);
            if (rc < 0) {
                FAIL(rc);
            }
            if (v != NULL && v->page != NULL) {
                v->page(ctx, pos, &h);
            }
            pos += (size_t)rc;
        } else if (t == M2022_QPDL_REC_BAND) {
            m2022_qpdl_band_header_t h;
            rc = m2022_qpdl_parse_band_header(data + pos, len - pos, &h);
            if (rc < 0) {
                FAIL(rc);
            }
            if (len - pos - (size_t)rc < h.length) {
                FAIL(M2022_QPDL_ETRUNCATED);
            }
            if (v != NULL && v->band != NULL) {
                v->band(ctx, pos, &h, data + pos + (size_t)rc);
            }
            pos += (size_t)rc + h.length;
        } else if (t == M2022_QPDL_REC_END_PAGE) {
            if (len - pos < M2022_QPDL_END_PAGE_LEN) {
                FAIL(M2022_QPDL_ETRUNCATED);
            }
            if (v != NULL && v->end_page != NULL) {
                v->end_page(ctx, pos, (uint16_t)((data[pos + 1] << 8) | data[pos + 2]));
            }
            pos += M2022_QPDL_END_PAGE_LEN;
        } else if (t == M2022_QPDL_REC_END_JOB) {
            if (v != NULL && v->end_job != NULL) {
                v->end_job(ctx, pos);
            }
            pos++;
            /* Trailer: the UEL, optionally followed by PJL text (e.g. "@PJL EOJ"). */
            if (len - pos >= M2022_QPDL_UEL_LEN &&
                memcmp(data + pos, M2022_QPDL_UEL, M2022_QPDL_UEL_LEN) == 0) {
                pos = emit_lines(data, len, pos, v, ctx, NULL);
            }
            if (pos != len) {
                FAIL(M2022_QPDL_ERECORD);
            }
            return 0;
        } else {
            FAIL(M2022_QPDL_ERECORD);
        }
    }
    return 0;
#undef FAIL
}

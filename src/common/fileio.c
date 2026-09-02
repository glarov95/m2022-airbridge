#include "m2022/fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

uint8_t *m2022_read_file(const char *path, size_t *len)
{
    gzFile f = gzopen(path, "rb");
    size_t cap = 1 << 20, n = 0;
    uint8_t *buf;

    *len = 0;
    if (f == NULL) {
        return NULL;
    }
    buf = malloc(cap);
    while (buf != NULL) {
        int got;
        if (n == cap) {
            uint8_t *bigger = realloc(buf, cap * 2);
            if (bigger == NULL) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = bigger;
            cap *= 2;
        }
        got = gzread(f, buf + n, (unsigned)(cap - n));
        if (got < 0) {
            free(buf);
            buf = NULL;
            break;
        }
        if (got == 0) {
            break;
        }
        n += (size_t)got;
    }
    gzclose(f);
    if (buf != NULL) {
        *len = n;
    }
    return buf;
}

int m2022_write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (f == NULL) {
        return -1;
    }
    ok = fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok ? 0 : -1;
}

static int read_token(const uint8_t *d, size_t len, size_t *pos, uint32_t *value)
{
    uint32_t v = 0;
    int digits = 0;
    while (*pos < len && (d[*pos] == ' ' || d[*pos] == '\t' || d[*pos] == '\r' || d[*pos] == '\n')) {
        (*pos)++;
    }
    while (*pos < len && d[*pos] == '#') { /* comment to end of line */
        while (*pos < len && d[*pos] != '\n') {
            (*pos)++;
        }
        while (*pos < len && (d[*pos] == ' ' || d[*pos] == '\n' || d[*pos] == '\r')) {
            (*pos)++;
        }
    }
    while (*pos < len && d[*pos] >= '0' && d[*pos] <= '9') {
        v = v * 10 + (uint32_t)(d[*pos] - '0');
        (*pos)++;
        digits++;
    }
    if (digits == 0) {
        return -1;
    }
    *value = v;
    return 0;
}

int m2022_pnm_parse(const uint8_t *d, size_t len, m2022_pnm_t *out)
{
    size_t pos = 2;
    uint32_t w, h, maxval = 1;

    memset(out, 0, sizeof *out);
    if (len < 3 || d[0] != 'P' || (d[1] != '4' && d[1] != '5')) {
        return -1;
    }
    out->type = d[1] - '0';
    if (read_token(d, len, &pos, &w) != 0 || read_token(d, len, &pos, &h) != 0) {
        return -2;
    }
    if (out->type == 5 && read_token(d, len, &pos, &maxval) != 0) {
        return -2;
    }
    pos++; /* single whitespace byte after the header */
    out->width = w;
    out->height = h;
    out->maxval = maxval;
    out->pixel_bytes = out->type == 4 ? ((size_t)w + 7) / 8 * h : (size_t)w * h;
    if (pos + out->pixel_bytes > len || w == 0 || h == 0 || (out->type == 5 && maxval != 255)) {
        return -3;
    }
    out->pixels = d + pos;
    return 0;
}

int m2022_pbm_write(const char *path, uint32_t width, uint32_t height, const uint8_t *rows)
{
    FILE *f = fopen(path, "wb");
    size_t bpr = ((size_t)width + 7) / 8;
    int ok;
    if (f == NULL) {
        return -1;
    }
    fprintf(f, "P4\n%u %u\n", width, height);
    ok = fwrite(rows, 1, bpr * height, f) == bpr * height;
    return fclose(f) == 0 && ok ? 0 : -1;
}

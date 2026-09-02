/* Small file helpers for the CLI and tests (not for the pure modules). */
#ifndef M2022_FILEIO_H
#define M2022_FILEIO_H

#include <stddef.h>
#include <stdint.h>

/* Read a whole file, transparently inflating gzip. Returns malloc'd data or NULL. */
uint8_t *m2022_read_file(const char *path, size_t *len);
int m2022_write_file(const char *path, const void *data, size_t len);

/* Portable anymap parsing: P4 (1-bit, 1 = black) and P5 (8-bit gray) binary forms. */
typedef struct {
    int type;        /* 4 or 5 */
    uint32_t width, height;
    uint32_t maxval; /* P5 only */
    const uint8_t *pixels;
    size_t pixel_bytes;
} m2022_pnm_t;

int m2022_pnm_parse(const uint8_t *data, size_t len, m2022_pnm_t *out);

/* Write a P4 bitmap from packed rows ((width + 7) / 8 bytes each, 1 = black). */
int m2022_pbm_write(const char *path, uint32_t width, uint32_t height, const uint8_t *rows);

#endif /* M2022_FILEIO_H */

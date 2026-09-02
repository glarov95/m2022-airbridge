#include "m2022/qpdl.h"

void m2022_qpdl_band_to_rows(const uint8_t *band, size_t bytes_per_row, uint8_t *rows)
{
    for (size_t col = 0; col < bytes_per_row; col++) {
        const uint8_t *src = band + col * M2022_QPDL_BAND_LINES;
        for (size_t line = 0; line < M2022_QPDL_BAND_LINES; line++) {
            rows[line * bytes_per_row + col] = (uint8_t)~src[line];
        }
    }
}

void m2022_qpdl_rows_to_band(const uint8_t *rows, size_t bytes_per_row, uint8_t *band)
{
    for (size_t col = 0; col < bytes_per_row; col++) {
        uint8_t *dst = band + col * M2022_QPDL_BAND_LINES;
        for (size_t line = 0; line < M2022_QPDL_BAND_LINES; line++) {
            dst[line] = (uint8_t)~rows[line * bytes_per_row + col];
        }
    }
}

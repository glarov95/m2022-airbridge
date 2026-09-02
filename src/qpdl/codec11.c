/*
 * Band compression 0x11 (docs/spl-qpdl.md section 3).
 *
 * Payload: magic 0x09ABCDEF, raw length N (<= 128), 64 x 16-bit offsets, N raw bytes, then a
 * token stream, then a 32-bit big-endian checksum of every payload byte before it.
 *
 *   match:    b1 & 0x80 set;  length = (b1 & 0x7F) + ((b2 & 0xC0) << 1) + 3;  offset = table[b2 & 0x3F]
 *   literals: b1 & 0x80 clear; count = b1 + 1 bytes follow
 *
 * The magic, raw length and table share one byte order, which the printer autodetects; the
 * vendor writes them little-endian. Table indices are 0-based and the checksum excludes the
 * band record header: both verified on the vendor fixtures.
 */
#include "m2022/qpdl.h"

#include <string.h>

static uint32_t rd32(const uint8_t *p, bool le)
{
    return le ? ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0]
              : ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t rd16(const uint8_t *p, bool le)
{
    return le ? (uint16_t)((p[1] << 8) | p[0]) : (uint16_t)((p[0] << 8) | p[1]);
}

int m2022_codec11_decode(const uint8_t *payload, size_t len, uint8_t *out, size_t out_cap,
                         size_t *out_len, m2022_codec11_info_t *info)
{
    m2022_codec11_info_t local;
    const uint8_t *s, *end;
    size_t pos = 0;
    uint32_t sum = 0;

    if (info == NULL) {
        info = &local;
    }
    memset(info, 0, sizeof *info);
    *out_len = 0;
    if (payload == NULL || out == NULL) {
        return M2022_QPDL_EINVAL;
    }
    if (len < M2022_CODEC11_HEADER_LEN + M2022_CODEC11_CHECKSUM_LEN) {
        return M2022_QPDL_ETRUNCATED;
    }
    if (rd32(payload, true) == M2022_CODEC11_MAGIC) {
        info->little_endian = true;
    } else if (rd32(payload, false) == M2022_CODEC11_MAGIC) {
        info->little_endian = false;
    } else {
        return M2022_QPDL_EMAGIC;
    }
    info->raw_len = rd32(payload + 4, info->little_endian);
    for (size_t i = 0; i < M2022_CODEC11_TABLE_ENTRIES; i++) {
        info->table[i] = rd16(payload + 8 + 2 * i, info->little_endian);
    }
    if (info->raw_len > M2022_CODEC11_MAX_RAW) {
        return M2022_QPDL_ERAWLEN;
    }
    if (len - M2022_CODEC11_CHECKSUM_LEN - M2022_CODEC11_HEADER_LEN < info->raw_len) {
        return M2022_QPDL_ETRUNCATED;
    }
    for (size_t i = 0; i < len - M2022_CODEC11_CHECKSUM_LEN; i++) {
        sum += payload[i];
    }
    info->checksum_computed = sum;
    info->checksum_stored = rd32(payload + len - M2022_CODEC11_CHECKSUM_LEN, false);

    if (info->raw_len > out_cap) {
        return M2022_QPDL_EOVERFLOW;
    }
    memcpy(out, payload + M2022_CODEC11_HEADER_LEN, info->raw_len);
    pos = info->raw_len;

    s = payload + M2022_CODEC11_HEADER_LEN + info->raw_len;
    end = payload + len - M2022_CODEC11_CHECKSUM_LEN;
    info->stream_len = (size_t)(end - s);

    while (s < end) {
        uint8_t b1 = *s++;
        if ((b1 & 0x80) != 0) {
            size_t length, index, offset;
            uint8_t b2;
            if (s >= end) {
                *out_len = pos;
                return M2022_QPDL_ETRUNCATED;
            }
            b2 = *s++;
            length = (size_t)(b1 & 0x7F) + ((size_t)(b2 & 0xC0) << 1) + 3;
            index = b2 & 0x3F;
            offset = info->table[index];
            info->match_tokens++;
            if (length > info->max_match) {
                info->max_match = length;
            }
            if (index > info->max_index) {
                info->max_index = index;
            }
            if (offset == 0 || offset > pos) {
                *out_len = pos;
                return M2022_QPDL_EOFFSET;
            }
            if (out_cap - pos < length) {
                *out_len = pos;
                return M2022_QPDL_EOVERFLOW;
            }
            for (size_t k = 0; k < length; k++) { /* overlapping copies are the point */
                out[pos] = out[pos - offset];
                pos++;
            }
        } else {
            size_t count = (size_t)b1 + 1;
            info->literal_tokens++;
            if (count > info->max_literal) {
                info->max_literal = count;
            }
            if ((size_t)(end - s) < count) {
                *out_len = pos;
                return M2022_QPDL_ETRUNCATED;
            }
            if (out_cap - pos < count) {
                *out_len = pos;
                return M2022_QPDL_EOVERFLOW;
            }
            memcpy(out + pos, s, count);
            pos += count;
            s += count;
        }
    }
    *out_len = pos;
    return info->checksum_computed == info->checksum_stored ? M2022_QPDL_OK
                                                             : M2022_QPDL_ECHECKSUM;
}

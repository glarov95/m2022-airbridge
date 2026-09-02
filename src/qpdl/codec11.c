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

/* ---- encoder ------------------------------------------------------------------------- */

/*
 * What the vendor's 867 fixture bands taught us about distances (scratch survey, 2026-09-02,
 * numbers in docs/spl-qpdl.md 3.3): distance 1 (the line above) carries 52 % of all matched
 * bytes, 128 (the byte-column to the left) 16 %, the next multiples of 128 and their +-1
 * diagonals most of the rest; small distances 2..24 appear in nearly every vendor table
 * (periods of the ordered screen); no vendor table entry exceeds 1024. This fixed table is
 * built from that; m2022_codec11_choose_table() beats it by looking at the band itself.
 */
const uint16_t m2022_codec11_default_table[M2022_CODEC11_TABLE_ENTRIES] = {
    1,    2,    3,    4,    5,    6,    7,    8,    9,    10,   11,   12,   13,   14,   15,   16,
    17,   18,   19,   20,   21,   22,   23,   24,   32,   40,   48,   56,   64,   126,  130,  136,
    127,  128,  129,  255,  256,  257,  383,  384,  385,  511,  512,  513,  639,  640,  641,  767,
    768,  769,  895,  896,  897,  1023, 1024, 1025, 1152, 1280, 1408, 1536, 1664, 1792, 1920, 2048,
};

static void wr32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

size_t m2022_codec11_encode_bound(size_t len)
{
    /* header, every byte as raw or literal, one count byte per 128 literals, checksum */
    return M2022_CODEC11_HEADER_LEN + len +
           (len + M2022_CODEC11_MAX_LITERAL - 1) / M2022_CODEC11_MAX_LITERAL +
           M2022_CODEC11_CHECKSUM_LEN;
}

/* Bytes matching between band+pos and band+pos-d, at most max. */
static size_t match_len(const uint8_t *band, size_t pos, size_t d, size_t max)
{
    const uint8_t *p = band + pos, *s = p - d;
    size_t n = 0;
    while (n < max && p[n] == s[n]) { /* s may overlap p (d < n): that is a run, and fine */
        n++;
    }
    return n;
}

/*
 * Longest match at pos among the table's distances; 0 when no distance gives 3 bytes.
 * *index is the table slot of the winner (the first slot on ties).
 */
static size_t longest_match(const uint8_t *band, size_t len, size_t pos,
                            const uint16_t table[M2022_CODEC11_TABLE_ENTRIES], size_t *index)
{
    const uint8_t *p = band + pos;
    size_t max = len - pos, best = 0;

    if (max > M2022_CODEC11_MAX_MATCH) {
        max = M2022_CODEC11_MAX_MATCH;
    }
    if (max < 3) {
        return 0;
    }
    for (size_t i = 0; i < M2022_CODEC11_TABLE_ENTRIES; i++) {
        size_t d = table[i], n;
        const uint8_t *s;
        if (d == 0 || d > pos) {
            continue;
        }
        s = p - d;
        /* cheap rejection: a candidate only wins if it matches where the best one stopped */
        if (best > 0 ? s[best] != p[best] : (s[0] != p[0] || s[1] != p[1] || s[2] != p[2])) {
            continue;
        }
        n = match_len(band, pos, d, max);
        if (n > best) {
            best = n;
            *index = i;
            if (best == max) {
                break;
            }
        }
    }
    return best >= 3 ? best : 0;
}

int m2022_codec11_encode(const uint8_t *band, size_t len,
                         const uint16_t table[M2022_CODEC11_TABLE_ENTRIES], uint8_t *out,
                         size_t out_cap, size_t *out_len)
{
    size_t n = 0, raw, raw_max, pos, lit_start, index = 0;
    uint32_t sum = 0;

#define NEED(k)                                                                            \
    do {                                                                                   \
        if (out_cap - n < (k)) {                                                           \
            *out_len = n;                                                                  \
            return M2022_QPDL_EOVERFLOW;                                                   \
        }                                                                                  \
    } while (0)

    *out_len = 0;
    if ((band == NULL && len > 0) || table == NULL || out == NULL) {
        return M2022_QPDL_EINVAL;
    }

    /* Raw prefix: the leading bytes up to the first match, clamped to 64..128 like the
     * vendor's (raw 64 with a match right after it in 216 bands, 128 in 632, in between
     * exactly when the first match sat there). */
    raw = len < M2022_CODEC11_MIN_RAW ? len : M2022_CODEC11_MIN_RAW;
    raw_max = len < M2022_CODEC11_MAX_RAW ? len : M2022_CODEC11_MAX_RAW;
    while (raw < raw_max && longest_match(band, len, raw, table, &index) == 0) {
        raw++;
    }

    NEED(M2022_CODEC11_HEADER_LEN + raw);
    wr32le(out + n, M2022_CODEC11_MAGIC);
    n += 4;
    wr32le(out + n, (uint32_t)raw);
    n += 4;
    for (size_t i = 0; i < M2022_CODEC11_TABLE_ENTRIES; i++) {
        out[n++] = (uint8_t)table[i];
        out[n++] = (uint8_t)(table[i] >> 8);
    }
    if (raw > 0) {
        memcpy(out + n, band, raw);
        n += raw;
    }

    /* Token stream: greedy longest match; bytes without one collect into literal runs that
     * are cut into tokens of at most 128 bytes. */
    pos = raw;
    lit_start = raw;
    while (pos < len) {
        size_t m = longest_match(band, len, pos, table, &index);
        if (m == 0) {
            pos++;
            continue;
        }
        while (lit_start < pos) {
            size_t count = pos - lit_start;
            if (count > M2022_CODEC11_MAX_LITERAL) {
                count = M2022_CODEC11_MAX_LITERAL;
            }
            NEED(1 + count);
            out[n++] = (uint8_t)(count - 1);
            memcpy(out + n, band + lit_start, count);
            n += count;
            lit_start += count;
        }
        NEED(2);
        out[n++] = (uint8_t)(0x80 | ((m - 3) & 0x7F));
        out[n++] = (uint8_t)((((m - 3) >> 7) << 6) | index);
        pos += m;
        lit_start = pos;
    }
    while (lit_start < len) {
        size_t count = len - lit_start;
        if (count > M2022_CODEC11_MAX_LITERAL) {
            count = M2022_CODEC11_MAX_LITERAL;
        }
        NEED(1 + count);
        out[n++] = (uint8_t)(count - 1);
        memcpy(out + n, band + lit_start, count);
        n += count;
        lit_start += count;
    }

    for (size_t i = 0; i < n; i++) {
        sum += out[i];
    }
    NEED(M2022_CODEC11_CHECKSUM_LEN);
    out[n++] = (uint8_t)(sum >> 24);
    out[n++] = (uint8_t)(sum >> 16);
    out[n++] = (uint8_t)(sum >> 8);
    out[n++] = (uint8_t)sum;
    *out_len = n;
    return M2022_QPDL_OK;
#undef NEED
}

/* ---- table choice -------------------------------------------------------------------- */

/*
 * A greedy parse needs to know, at every position, whether the bytes ahead occurred before
 * at one of 64 distances. To choose those distances we run a cheaper parse first: a hash of
 * the next 3 bytes finds the most recent positions with the same 3 bytes, the longest match
 * among them is credited to its distance, and the parse skips over it as the real one would.
 * The 64 distances with the most credit become the table. Runs of white credit distance 1,
 * a repeated column credits 128, an 8-line screen credits 8, and so on.
 *
 * The hash alone misses one thing: inside a run of white or black every recent position has
 * the same 3 bytes, so the slots hold distances 1..4 and a match at distance 1 ends where
 * the run ends. The byte-columns to the left (multiples of 128) would match across whole
 * columns instead; the checkerboard fixture compressed 5 times worse than the vendor's
 * until they were tried at every position as well.
 */
#define CHOOSE_HASH_BITS 11
#define CHOOSE_SLOTS 4
#define CHOOSE_MAX_DIST 4096

static const uint16_t choose_columns[] = {128,  256,  384,  512,  640,  768,  896,  1024,
                                          1152, 1280, 1408, 1536, 1664, 1792, 1920, 2048};

static uint32_t hash3(const uint8_t *p)
{
    uint32_t v = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    return (v * 2654435761u) >> (32 - CHOOSE_HASH_BITS);
}

static void slot_insert(uint32_t slots[][CHOOSE_SLOTS], const uint8_t *band, size_t pos)
{
    uint32_t *s = slots[hash3(band + pos)];
    for (size_t k = CHOOSE_SLOTS - 1; k > 0; k--) {
        s[k] = s[k - 1];
    }
    s[0] = (uint32_t)pos + 1; /* 0 marks an empty slot */
}

void m2022_codec11_choose_table(const uint8_t *band, size_t len,
                                uint16_t table[M2022_CODEC11_TABLE_ENTRIES])
{
    uint32_t credit[CHOOSE_MAX_DIST + 1];
    uint32_t slots[1u << CHOOSE_HASH_BITS][CHOOSE_SLOTS];
    size_t pos = 0, count = 0;

    memset(credit, 0, sizeof credit);
    memset(slots, 0, sizeof slots);

    while (pos + 3 <= len) {
        const uint32_t *s = slots[hash3(band + pos)];
        size_t max = len - pos, best = 0, best_d = 0;
        if (max > M2022_CODEC11_MAX_MATCH) {
            max = M2022_CODEC11_MAX_MATCH;
        }
        for (size_t k = 0; k < CHOOSE_SLOTS && s[k] != 0; k++) {
            size_t d = pos - (s[k] - 1), n;
            if (d > CHOOSE_MAX_DIST) {
                continue;
            }
            n = match_len(band, pos, d, max);
            if (n >= 3 && n > best) {
                best = n;
                best_d = d;
            }
        }
        for (size_t k = 0; k < sizeof choose_columns / sizeof choose_columns[0]; k++) {
            size_t d = choose_columns[k], n;
            if (d > pos) {
                break;
            }
            n = match_len(band, pos, d, max);
            if (n >= 3 && n > best) {
                best = n;
                best_d = d;
            }
        }
        slot_insert(slots, band, pos);
        if (best == 0) {
            pos++;
            continue;
        }
        credit[best_d] += (uint32_t)best;
        for (size_t q = pos + 1; q < pos + best && q + 3 <= len; q++) {
            slot_insert(slots, band, q);
        }
        pos += best;
    }

    /* the 64 best-credited distances, most credit first */
    while (count < M2022_CODEC11_TABLE_ENTRIES) {
        size_t best_d = 0;
        uint32_t best_c = 0;
        for (size_t d = 1; d <= CHOOSE_MAX_DIST; d++) {
            if (credit[d] > best_c) {
                best_c = credit[d];
                best_d = d;
            }
        }
        if (best_c == 0) {
            break;
        }
        table[count++] = (uint16_t)best_d;
        credit[best_d] = 0;
    }
    /* fill the rest from the default table, without duplicates */
    for (size_t i = 0; i < M2022_CODEC11_TABLE_ENTRIES && count < M2022_CODEC11_TABLE_ENTRIES;
         i++) {
        bool present = false;
        for (size_t k = 0; k < count; k++) {
            if (table[k] == m2022_codec11_default_table[i]) {
                present = true;
                break;
            }
        }
        if (!present) {
            table[count++] = m2022_codec11_default_table[i];
        }
    }
}

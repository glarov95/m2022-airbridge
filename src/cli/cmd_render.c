/*
 * render IN [--preset P | --method M] [--gamma G] [--dot-gain D] [--coverage S] [--threshold T]
 *        [--cell N] [--out OUT.pbm]
 *
 * IN is a PGM (P5, sGray) such as a captured client page, or a CUPS raster (.ras / .ras.gz)
 * such as the vendor's own input in fixtures/oracle/samsung. Output is a PBM, 1 = black.
 */
#include "cli.h"
#include "m2022/fileio.h"
#include "m2022/halftone.h"
#include "m2022/raster.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int cmd_render(int argc, char **argv)
{
    const char *in_path = NULL, *out_path = "page.pbm";
    m2022_preset_t preset = M2022_PRESET_NORMAL;
    m2022_tone_params_t tone;
    m2022_ht_params_t ht_params;
    bool have_method = false;
    double gamma = -1, dot_gain = -1, coverage = -1;
    int threshold = -1, cell = -1;
    m2022_ht_method_t method = M2022_HT_BLUE_NOISE;
    uint8_t *data, *gray, *ink, *bits, lut[256];
    size_t len, bpr, state_bytes;
    void *state;
    uint32_t width, height, line_bytes;
    const uint8_t *pixels;
    m2022_pixel_format_t fmt;
    m2022_halftoner_t ht;
    uint64_t black = 0;
    double t0, t1;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--preset") == 0 && i + 1 < argc) {
            if (m2022_preset_parse(argv[++i], &preset) != 0) {
                fprintf(stderr, "render: unknown preset '%s' (draft, normal, text, photo, vendor)\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--method") == 0 && i + 1 < argc) {
            if (m2022_ht_method_parse(argv[++i], &method) != 0) {
                fprintf(stderr, "render: unknown method '%s' (threshold, bayer4, bayer8, clustered, blue-noise, floyd-steinberg)\n", argv[i]);
                return 2;
            }
            have_method = true;
        } else if (strcmp(argv[i], "--gamma") == 0 && i + 1 < argc) {
            gamma = atof(argv[++i]);
        } else if (strcmp(argv[i], "--dot-gain") == 0 && i + 1 < argc) {
            dot_gain = atof(argv[++i]);
        } else if (strcmp(argv[i], "--coverage") == 0 && i + 1 < argc) {
            coverage = atof(argv[++i]);
        } else if (strcmp(argv[i], "--threshold") == 0 && i + 1 < argc) {
            threshold = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--cell") == 0 && i + 1 < argc) {
            cell = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "render: unknown option '%s'\n", argv[i]);
            return 2;
        } else if (in_path == NULL) {
            in_path = argv[i];
        } else {
            fprintf(stderr, "render: only one input\n");
            return 2;
        }
    }
    if (in_path == NULL) {
        fprintf(stderr, "Usage: m2022-airbridge render IN.pgm|IN.ras[.gz] [--preset P | --method M] "
                        "[--gamma G] [--dot-gain D] [--coverage S] [--threshold T] [--cell N] [--out OUT.pbm]\n");
        return 2;
    }

    m2022_preset(preset, &tone, &ht_params);
    if (have_method) {
        m2022_ht_default(&ht_params, method);
    }
    if (gamma > 0) {
        tone.gamma = gamma;
    }
    if (dot_gain >= 0) {
        tone.dot_gain = dot_gain;
    }
    if (coverage >= 0) {
        tone.coverage_scale = coverage;
    }
    if (threshold >= 0) {
        ht_params.threshold = (uint8_t)threshold;
    }
    if (cell > 0) {
        ht_params.cluster_cell = (uint8_t)cell;
    }

    data = m2022_read_file(in_path, &len);
    if (data == NULL) {
        fprintf(stderr, "render: cannot read %s\n", in_path);
        return 2;
    }
    if (len > 2 && data[0] == 'P') {
        m2022_pnm_t pnm;
        if (m2022_pnm_parse(data, len, &pnm) != 0 || pnm.type != 5) {
            fprintf(stderr, "render: %s is not a binary PGM (P5)\n", in_path);
            free(data);
            return 2;
        }
        width = pnm.width;
        height = pnm.height;
        fmt = M2022_PIX_SGRAY_8;
        line_bytes = width;
        pixels = pnm.pixels;
        printf("input: PGM %ux%u sGray\n", width, height);
    } else {
        m2022_cupsraster_header_t h;
        if (m2022_cupsraster_parse_header(data, len, &h) != 0) {
            fprintf(stderr, "render: %s is neither a PGM nor a CUPS raster\n", in_path);
            free(data);
            return 2;
        }
        if (h.compressed || m2022_cupsraster_pixel_format(&h, &fmt) != 0 ||
            len < M2022_CUPSRASTER_HEADER_BYTES + (size_t)h.bytes_per_line * h.height) {
            fprintf(stderr, "render: unsupported CUPS raster (compressed=%d, %u bpp, cspace %u)\n",
                    h.compressed, h.bits_per_pixel, h.color_space);
            free(data);
            return 2;
        }
        width = h.width;
        height = h.height;
        line_bytes = h.bytes_per_line;
        pixels = data + M2022_CUPSRASTER_HEADER_BYTES;
        printf("input: CUPS raster %ux%u, %u bpp, colour space %u, %ux%u dpi, %s\n", width, height,
               h.bits_per_pixel, h.color_space, h.x_dpi, h.y_dpi, h.page_size_name);
    }

    bpr = ((size_t)width + 7) / 8;
    gray = malloc(width);
    ink = malloc(width);
    bits = calloc(bpr, height);
    state_bytes = m2022_halftoner_state_bytes(width);
    state = malloc(state_bytes);
    if (gray == NULL || ink == NULL || bits == NULL || state == NULL ||
        m2022_halftoner_init(&ht, &ht_params, width, state, state_bytes) != 0) {
        fprintf(stderr, "render: out of memory\n");
        return 2;
    }
    m2022_tone_build(&tone, lut);
    printf("preset %s: method %s, threshold %u, cell %u, gamma %.2f, dot gain %.2f, coverage %.2f\n",
           have_method ? "(custom)" : m2022_preset_name(preset), m2022_ht_method_name(ht_params.method),
           ht_params.threshold, ht_params.cluster_cell, tone.gamma, tone.dot_gain, tone.coverage_scale);

    t0 = now_seconds();
    for (uint32_t y = 0; y < height; y++) {
        uint8_t *row = bits + (size_t)y * bpr;
        m2022_raster_line_to_gray(fmt, pixels + (size_t)y * line_bytes, width, gray);
        m2022_tone_apply(lut, gray, width, ink);
        m2022_halftone_line(&ht, ink, y, row);
        for (size_t i = 0; i < bpr; i++) {
            black += (uint64_t)__builtin_popcount(row[i]);
        }
    }
    t1 = now_seconds();
    if (m2022_pbm_write(out_path, width, height, bits) != 0) {
        fprintf(stderr, "render: cannot write %s\n", out_path);
        return 2;
    }
    printf("wrote %s: %ux%u, %.2f %% black, %.3f s (%.1f Mpx/s)\n", out_path, width, height,
           100.0 * (double)black / ((double)width * height), t1 - t0,
           (double)width * height / 1e6 / (t1 - t0 > 0 ? t1 - t0 : 1e-9));
    free(data);
    free(gray);
    free(ink);
    free(bits);
    free(state);
    return 0;
}

/*
 * render IN [halftone options] [--out OUT.pbm]
 *
 * IN is a PGM (P5, sGray) such as a captured client page, a CUPS raster (.ras / .ras.gz) such
 * as the vendor's own input in fixtures/oracle/samsung, or a PBM that is passed through.
 * Output is a PBM, 1 = black. The halftone options are listed in pipeline.h.
 */
#include "cli.h"
#include "m2022/fileio.h"
#include "m2022/halftone.h"
#include "m2022/raster.h"
#include "pipeline.h"

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
    const char *in_path = NULL, *out_path = "page.pbm", *preset_name;
    cli_halftone_opts_t opts;
    cli_raster_t in;
    m2022_tone_params_t tone;
    m2022_ht_params_t ht_params;
    uint8_t *gray, *ink, *bits, lut[256];
    size_t bpr, state_bytes;
    void *state;
    m2022_halftoner_t ht;
    uint64_t black = 0;
    double t0, t1;

    cli_halftone_opts_init(&opts);
    for (int i = 0; i < argc; i++) {
        int r = cli_halftone_opt(&opts, "render", argc, argv, &i);
        if (r < 0) {
            return 2;
        }
        if (r > 0) {
            continue;
        }
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
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
        fprintf(stderr, "Usage: m2022-airbridge render IN.pgm|IN.pbm|IN.ras[.gz] "
                        CLI_HALFTONE_USAGE " [--out OUT.pbm]\n");
        return 2;
    }

    cli_halftone_resolve(&opts, &tone, &ht_params, &preset_name);
    if (cli_raster_open("render", in_path, &in) != 0) {
        return 2;
    }
    printf("input: %s\n", in.description);

    bpr = ((size_t)in.width + 7) / 8;
    gray = malloc(in.width);
    ink = malloc(in.width);
    bits = calloc(bpr, in.height);
    state_bytes = m2022_halftoner_state_bytes(in.width);
    state = malloc(state_bytes);
    if (gray == NULL || ink == NULL || bits == NULL || state == NULL ||
        m2022_halftoner_init(&ht, &ht_params, in.width, state, state_bytes) != 0) {
        fprintf(stderr, "render: out of memory\n");
        return 2;
    }
    m2022_tone_build(&tone, lut);
    printf("preset %s: method %s, threshold %u, cell %u, gamma %.2f, dot gain %.2f, "
           "coverage %.2f\n",
           preset_name, m2022_ht_method_name(ht_params.method), ht_params.threshold,
           ht_params.cluster_cell, tone.gamma, tone.dot_gain, tone.coverage_scale);

    t0 = now_seconds();
    for (uint32_t y = 0; y < in.height; y++) {
        uint8_t *row = bits + (size_t)y * bpr;
        if (in.bilevel) {
            memcpy(row, in.pixels + (size_t)y * in.line_bytes, bpr);
        } else {
            m2022_raster_line_to_gray(in.fmt, in.pixels + (size_t)y * in.line_bytes, in.width,
                                      gray);
            m2022_tone_apply(lut, gray, in.width, ink);
            m2022_halftone_line(&ht, ink, y, row);
        }
        for (size_t i = 0; i < bpr; i++) {
            black += (uint64_t)__builtin_popcount(row[i]);
        }
    }
    t1 = now_seconds();
    if (m2022_pbm_write(out_path, in.width, in.height, bits) != 0) {
        fprintf(stderr, "render: cannot write %s\n", out_path);
        return 2;
    }
    printf("wrote %s: %ux%u, %.2f %% black, %.3f s (%.1f Mpx/s)\n", out_path, in.width, in.height,
           100.0 * (double)black / ((double)in.width * in.height), t1 - t0,
           (double)in.width * in.height / 1e6 / (t1 - t0 > 0 ? t1 - t0 : 1e-9));
    free(in.data);
    free(gray);
    free(ink);
    free(bits);
    free(state);
    return 0;
}

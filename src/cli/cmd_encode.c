/*
 * encode IN [halftone options] [--media NAME] [--dpi 600|1200] [--source auto|manual]
 *        [--type OFF|NORMAL|THICK|...] [--duplex off|long|short] [--copies N] [--skip-blank]
 *        [--out JOB.spl]
 *
 * Turn one page (PGM, PBM or CUPS raster) into a complete job for the printer: our halftone
 * (M3), our band codec (M4) and the vendor's envelope and records (M5). `decode` explains the
 * result byte by byte; `send` prints it. --media takes a PWG name (iso_a4_210x297mm) or the
 * vendor's (A4); a CUPS raster supplies its own. At 1200 dpi a 600 dpi input is doubled.
 */
#include "cli.h"
#include "m2022/fileio.h"
#include "m2022/halftone.h"
#include "m2022/media.h"
#include "m2022/qpdl.h"
#include "m2022/raster.h"
#include "m2022/version.h"
#include "pipeline.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

typedef struct {
    FILE *f;
    int err;
} file_sink_t;

static int file_sink(void *ctx, const uint8_t *data, size_t len)
{
    file_sink_t *s = ctx;
    if (fwrite(data, 1, len, s->f) != len) {
        s->err = errno;
        return -1;
    }
    return 0;
}

static const m2022_media_t *find_media(const char *name)
{
    const m2022_media_t *m = m2022_media_by_pwg(name);
    size_t count;
    const m2022_media_t *table = m2022_media_table(&count);
    if (m != NULL) {
        return m;
    }
    for (size_t i = 0; i < count; i++) {
        if (strcasecmp(table[i].vendor_name, name) == 0) {
            return &table[i];
        }
    }
    return NULL;
}

int cmd_encode(int argc, char **argv)
{
    const char *in_path = NULL, *out_path = "job.spl", *media_name = NULL, *preset_name;
    cli_halftone_opts_t opts;
    cli_raster_t in;
    m2022_qpdl_job_t job;
    m2022_qpdl_encoder_t enc;
    m2022_tone_params_t tone;
    m2022_ht_params_t ht_params;
    m2022_halftoner_t ht;
    const m2022_media_t *media;
    file_sink_t sink = {NULL, 0};
    char date[16], producer[64];
    uint8_t *gray, *gray2, *ink, *bits, lut[256];
    void *state, *workspace;
    size_t state_bytes, workspace_bytes;
    uint32_t out_width, out_lines;
    bool upscale;
    time_t t = time(NULL);
    double t0, t1;
    int rc;

    cli_halftone_opts_init(&opts);
    m2022_qpdl_job_default(&job);
    for (int i = 0; i < argc; i++) {
        int r = cli_halftone_opt(&opts, "encode", argc, argv, &i);
        if (r < 0) {
            return 2;
        }
        if (r > 0) {
            continue;
        }
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "--media") == 0 && i + 1 < argc) {
            media_name = argv[++i];
        } else if (strcmp(argv[i], "--dpi") == 0 && i + 1 < argc) {
            job.dpi = (unsigned)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            if (strcmp(v, "auto") == 0) {
                job.feeder = M2022_QPDL_FEEDER_AUTO;
            } else if (strcmp(v, "manual") == 0) {
                job.feeder = M2022_QPDL_FEEDER_MANUAL;
            } else {
                fprintf(stderr, "encode: --source is auto or manual\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--type") == 0 && i + 1 < argc) {
            job.paper_type = argv[++i];
        } else if (strcmp(argv[i], "--duplex") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            if (strcmp(v, "off") == 0) {
                job.duplex = M2022_QPDL_DUPLEX_OFF;
            } else if (strcmp(v, "long") == 0) {
                job.duplex = M2022_QPDL_DUPLEX_MANUAL_LONG_EDGE;
            } else if (strcmp(v, "short") == 0) {
                job.duplex = M2022_QPDL_DUPLEX_MANUAL_SHORT_EDGE;
            } else {
                fprintf(stderr, "encode: --duplex is off, long or short\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--copies") == 0 && i + 1 < argc) {
            job.copies = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--skip-blank") == 0) {
            job.skip_blank_pages = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "encode: unknown option '%s'\n", argv[i]);
            return 2;
        } else if (in_path == NULL) {
            in_path = argv[i];
        } else {
            fprintf(stderr, "encode: only one input\n");
            return 2;
        }
    }
    if (in_path == NULL) {
        fprintf(stderr,
                "Usage: m2022-airbridge encode IN.pgm|IN.pbm|IN.ras[.gz] " CLI_HALFTONE_USAGE
                "\n       [--media NAME] [--dpi 600|1200] [--source auto|manual] [--type T]"
                " [--duplex off|long|short] [--copies N] [--skip-blank] [--out JOB.spl]\n");
        return 2;
    }
    if (job.dpi != 600 && job.dpi != 1200) {
        fprintf(stderr, "encode: --dpi is 600 or 1200\n");
        return 2;
    }
    if (job.copies == 0) {
        fprintf(stderr, "encode: --copies must be at least 1\n");
        return 2;
    }

    cli_halftone_resolve(&opts, &tone, &ht_params, &preset_name);
    if (cli_raster_open("encode", in_path, &in) != 0) {
        return 2;
    }
    printf("input: %s\n", in.description);

    if (media_name == NULL && in.page_size_name[0] != '\0') {
        media_name = in.page_size_name;
    }
    media = find_media(media_name != NULL ? media_name : m2022_media_default_pwg());
    if (media == NULL) {
        fprintf(stderr, "encode: unknown media '%s' (PWG or vendor name; see `probe`)\n",
                media_name);
        free(in.data);
        return 2;
    }
    job.paper_code = media->qpdl_code;
    job.paper_width_pt = media->width_pt;
    job.paper_height_pt = media->height_pt;
    strftime(date, sizeof date, "%Y%m%d", localtime(&t));
    job.service_date = date;
    snprintf(producer, sizeof producer, "M2022 AirBridge %s", m2022_version_string());
    job.producer = producer;

    upscale = job.dpi == 1200 && (in.x_dpi == 600 || in.x_dpi == 0);
    if (upscale && in.bilevel) {
        fprintf(stderr, "encode: 1200 dpi needs a gray input (PGM or CUPS raster)\n");
        free(in.data);
        return 2;
    }
    out_width = upscale ? in.width * 2 : in.width;
    out_lines = upscale ? in.height * 2 : in.height;

    gray = malloc(in.width);
    gray2 = malloc(out_width);
    ink = malloc(out_width);
    bits = calloc(((size_t)out_width + 7) / 8, 1);
    state_bytes = m2022_halftoner_state_bytes(out_width);
    state = malloc(state_bytes);
    workspace_bytes = m2022_qpdl_encoder_workspace_bytes(out_width);
    workspace = malloc(workspace_bytes);
    if (gray == NULL || gray2 == NULL || ink == NULL || bits == NULL || state == NULL ||
        workspace == NULL ||
        m2022_halftoner_init(&ht, &ht_params, out_width, state, state_bytes) != 0) {
        fprintf(stderr, "encode: out of memory\n");
        return 2;
    }
    m2022_tone_build(&tone, lut);
    printf("preset %s: method %s, threshold %u, cell %u, gamma %.2f, dot gain %.2f, "
           "coverage %.2f\n",
           preset_name, m2022_ht_method_name(ht_params.method), ht_params.threshold,
           ht_params.cluster_cell, tone.gamma, tone.dot_gain, tone.coverage_scale);
    printf("job: media %s (%s, code 0x%02x, %dx%d pt), %u dpi, feeder %s, type %s, duplex %s, "
           "copies %u%s\n",
           media->vendor_name, media->pwg_name, media->qpdl_code, media->width_pt,
           media->height_pt, job.dpi, m2022_qpdl_feeder_name(job.feeder), job.paper_type,
           job.duplex == M2022_QPDL_DUPLEX_OFF ? "off"
           : job.duplex == M2022_QPDL_DUPLEX_MANUAL_LONG_EDGE ? "manual long edge"
                                                              : "manual short edge",
           job.copies, job.skip_blank_pages ? ", skip blank pages" : "");

    sink.f = fopen(out_path, "wb");
    if (sink.f == NULL) {
        fprintf(stderr, "encode: cannot write %s: %s\n", out_path, strerror(errno));
        return 2;
    }
    t0 = now_seconds();
    rc = m2022_qpdl_begin_job(&enc, &job, file_sink, &sink);
    if (rc == 0) {
        rc = m2022_qpdl_begin_page(&enc, out_width, workspace, workspace_bytes);
    }
    for (uint32_t y = 0; y < out_lines && rc == 0; y++) {
        uint32_t src_y = upscale ? y / 2 : y;
        const uint8_t *line = in.pixels + (size_t)src_y * in.line_bytes;
        if (in.bilevel) {
            rc = m2022_qpdl_write_line(&enc, line);
            continue;
        }
        m2022_raster_line_to_gray(in.fmt, line, in.width, gray);
        if (upscale) {
            m2022_raster_upscale2x_line(gray, in.width, gray2);
            m2022_tone_apply(lut, gray2, out_width, ink);
        } else {
            m2022_tone_apply(lut, gray, out_width, ink);
        }
        m2022_halftone_line(&ht, ink, y, bits);
        rc = m2022_qpdl_write_line(&enc, bits);
    }
    if (rc == 0) {
        rc = m2022_qpdl_end_page(&enc);
    }
    if (rc == 0) {
        rc = m2022_qpdl_end_job(&enc);
    }
    t1 = now_seconds();
    if (fclose(sink.f) != 0 && rc == 0) {
        rc = -1;
        sink.err = errno;
    }
    if (rc != 0) {
        if (sink.err != 0) {
            fprintf(stderr, "encode: writing %s failed: %s\n", out_path, strerror(sink.err));
        } else {
            fprintf(stderr, "encode: %s\n", m2022_qpdl_strerror(rc));
        }
        return 2;
    }
    printf("wrote %s: %u page, %u bands (%u blank omitted), band width %u, %zu bytes, %.3f s\n",
           out_path, enc.pages, enc.bands_written, enc.bands_blank, enc.band_width,
           enc.bytes_out, t1 - t0);
    free(in.data);
    free(gray);
    free(gray2);
    free(ink);
    free(bits);
    free(state);
    free(workspace);
    return 0;
}

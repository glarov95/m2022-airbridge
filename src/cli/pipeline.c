#include "pipeline.h"
#include "m2022/fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cli_halftone_opts_init(cli_halftone_opts_t *o)
{
    memset(o, 0, sizeof *o);
    o->preset = M2022_PRESET_NORMAL;
    o->method = M2022_HT_BLUE_NOISE;
    o->gamma = o->dot_gain = o->coverage = -1;
    o->threshold = o->cell = -1;
}

int cli_halftone_opt(cli_halftone_opts_t *o, const char *cmd, int argc, char **argv, int *i)
{
    const char *opt = argv[*i];
    const char *val;

    if (strcmp(opt, "--preset") != 0 && strcmp(opt, "--method") != 0 &&
        strcmp(opt, "--gamma") != 0 && strcmp(opt, "--dot-gain") != 0 &&
        strcmp(opt, "--coverage") != 0 && strcmp(opt, "--threshold") != 0 &&
        strcmp(opt, "--cell") != 0) {
        return 0;
    }
    if (*i + 1 >= argc) {
        fprintf(stderr, "%s: %s needs a value\n", cmd, opt);
        return -1;
    }
    val = argv[++*i];
    if (strcmp(opt, "--preset") == 0) {
        if (m2022_preset_parse(val, &o->preset) != 0) {
            fprintf(stderr, "%s: unknown preset '%s' (draft, normal, text, photo, vendor)\n", cmd,
                    val);
            return -1;
        }
    } else if (strcmp(opt, "--method") == 0) {
        if (m2022_ht_method_parse(val, &o->method) != 0) {
            fprintf(stderr,
                    "%s: unknown method '%s' (threshold, bayer4, bayer8, clustered, blue-noise, "
                    "floyd-steinberg)\n",
                    cmd, val);
            return -1;
        }
        o->have_method = true;
    } else if (strcmp(opt, "--gamma") == 0) {
        o->gamma = atof(val);
    } else if (strcmp(opt, "--dot-gain") == 0) {
        o->dot_gain = atof(val);
    } else if (strcmp(opt, "--coverage") == 0) {
        o->coverage = atof(val);
    } else if (strcmp(opt, "--threshold") == 0) {
        o->threshold = atoi(val);
    } else {
        o->cell = atoi(val);
    }
    return 1;
}

void cli_halftone_resolve(const cli_halftone_opts_t *o, m2022_tone_params_t *tone,
                          m2022_ht_params_t *ht, const char **name)
{
    m2022_preset(o->preset, tone, ht);
    if (o->have_method) {
        m2022_ht_default(ht, o->method);
    }
    if (o->gamma > 0) {
        tone->gamma = o->gamma;
    }
    if (o->dot_gain >= 0) {
        tone->dot_gain = o->dot_gain;
    }
    if (o->coverage >= 0) {
        tone->coverage_scale = o->coverage;
    }
    if (o->threshold >= 0) {
        ht->threshold = (uint8_t)o->threshold;
    }
    if (o->cell > 0) {
        ht->cluster_cell = (uint8_t)o->cell;
    }
    *name = o->have_method ? "(custom)" : m2022_preset_name(o->preset);
}

int cli_raster_open(const char *cmd, const char *path, cli_raster_t *r)
{
    size_t len;

    memset(r, 0, sizeof *r);
    r->data = m2022_read_file(path, &len);
    if (r->data == NULL) {
        fprintf(stderr, "%s: cannot read %s\n", cmd, path);
        return 2;
    }
    if (len > 2 && r->data[0] == 'P') {
        m2022_pnm_t pnm;
        if (m2022_pnm_parse(r->data, len, &pnm) != 0 || (pnm.type != 4 && pnm.type != 5)) {
            fprintf(stderr, "%s: %s is not a binary PGM (P5) or PBM (P4)\n", cmd, path);
            free(r->data);
            r->data = NULL;
            return 2;
        }
        r->width = pnm.width;
        r->height = pnm.height;
        r->pixels = pnm.pixels;
        if (pnm.type == 4) {
            r->bilevel = true;
            r->line_bytes = (pnm.width + 7) / 8;
            snprintf(r->description, sizeof r->description, "PBM %ux%u, 1 = black", r->width,
                     r->height);
        } else {
            r->fmt = M2022_PIX_SGRAY_8;
            r->line_bytes = pnm.width;
            snprintf(r->description, sizeof r->description, "PGM %ux%u sGray", r->width,
                     r->height);
        }
        return 0;
    }
    {
        m2022_cupsraster_header_t h;
        if (m2022_cupsraster_parse_header(r->data, len, &h) != 0) {
            fprintf(stderr, "%s: %s is neither a PNM nor a CUPS raster\n", cmd, path);
            free(r->data);
            r->data = NULL;
            return 2;
        }
        if (h.compressed || m2022_cupsraster_pixel_format(&h, &r->fmt) != 0 ||
            len < M2022_CUPSRASTER_HEADER_BYTES + (size_t)h.bytes_per_line * h.height) {
            fprintf(stderr, "%s: unsupported CUPS raster (compressed=%d, %u bpp, cspace %u)\n",
                    cmd, h.compressed, h.bits_per_pixel, h.color_space);
            free(r->data);
            r->data = NULL;
            return 2;
        }
        r->width = h.width;
        r->height = h.height;
        r->line_bytes = h.bytes_per_line;
        r->pixels = r->data + M2022_CUPSRASTER_HEADER_BYTES;
        r->x_dpi = h.x_dpi;
        r->y_dpi = h.y_dpi;
        snprintf(r->page_size_name, sizeof r->page_size_name, "%s", h.page_size_name);
        snprintf(r->description, sizeof r->description,
                 "CUPS raster %ux%u, %u bpp, colour space %u, %ux%u dpi, %s", r->width,
                 r->height, h.bits_per_pixel, h.color_space, h.x_dpi, h.y_dpi,
                 h.page_size_name);
        return 0;
    }
}

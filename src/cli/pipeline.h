/*
 * Shared by the commands that run pages through the raster and halftone modules (render,
 * encode): the halftone options and the page loader.
 */
#ifndef M2022_CLI_PIPELINE_H
#define M2022_CLI_PIPELINE_H

#include "m2022/halftone.h"
#include "m2022/raster.h"

#include <stdbool.h>
#include <stdint.h>

#define CLI_HALFTONE_USAGE                                                                    \
    "[--preset P | --method M] [--gamma G] [--dot-gain D] [--coverage S] [--threshold T] "     \
    "[--cell N]"

typedef struct {
    m2022_preset_t preset;
    bool have_method;
    m2022_ht_method_t method;
    double gamma, dot_gain, coverage; /* negative: keep the preset's value */
    int threshold, cell;              /* negative: keep the preset's value */
} cli_halftone_opts_t;

void cli_halftone_opts_init(cli_halftone_opts_t *o);

/* If argv[*i] is a halftone option, consume it and its value (advancing *i to the value) and
 * return 1; return 0 when it is not one; return -1 after printing an error prefixed with cmd. */
int cli_halftone_opt(cli_halftone_opts_t *o, const char *cmd, int argc, char **argv, int *i);

/* Turn the options into parameters; *name is the preset name or "(custom)". */
void cli_halftone_resolve(const cli_halftone_opts_t *o, m2022_tone_params_t *tone,
                          m2022_ht_params_t *ht, const char **name);

/* One input page: PGM (P5, sGray), PBM (P4, 1 = black, already halftoned), or a CUPS raster
 * (.ras, .ras.gz) such as the vendor's own input in fixtures/oracle/samsung. */
typedef struct {
    uint8_t *data; /* file contents; free() when done */
    uint32_t width, height;
    bool bilevel;             /* PBM: `pixels` are packed rows, 1 = black; no halftoning */
    m2022_pixel_format_t fmt; /* when !bilevel */
    uint32_t line_bytes;
    const uint8_t *pixels;
    uint32_t x_dpi, y_dpi;   /* CUPS raster only, else 0 */
    char page_size_name[64]; /* CUPS raster only, else "" */
    char description[160];   /* one line for the summary */
} cli_raster_t;

/* Load and describe; prints an error prefixed with cmd and returns nonzero on failure. */
int cli_raster_open(const char *cmd, const char *path, cli_raster_t *r);

#endif /* M2022_CLI_PIPELINE_H */

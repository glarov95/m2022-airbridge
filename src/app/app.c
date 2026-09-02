/*
 * PAPPL system, printer driver data (SPEC.md 6.1) and driver callbacks.
 *
 * M2: the raster callbacks capture what PAPPL hands us (the exact job file the client sent,
 * plus each decoded page as PBM/PGM/PPM) so we can see what iOS and macOS actually send. The
 * capture stays as a debugging device once the real pipeline exists (M3-M6).
 */
#include "m2022/app.h"
#include "m2022/media.h"
#include "m2022/version.h"

#include <errno.h>
#include <pappl/pappl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DRIVER_NAME "m2022"
#define MAKE_MODEL "Samsung Xpress SL-M2022"
#define DEVICE_ID "MFG:Samsung;MDL:M2020 Series;CMD:SPL;CLS:PRINTER;"
#define NATIVE_FORMAT "application/vnd.samsung-spl"

typedef struct {
    char capture_dir[1024]; /* empty = no capture */
} app_t;

typedef struct {
    char base[1100]; /* capture_dir/job-NNN */
    FILE *page_file;
    unsigned page;
    unsigned bytes_per_line;
    unsigned lines_written;
    bool invert; /* 8-bit K raster: 255 = black, PGM wants 0 = black */
} capture_job_t;

static app_t *job_app(pappl_job_t *job)
{
    pappl_pr_driver_data_t data;
    papplPrinterGetDriverData(papplJobGetPrinter(job), &data);
    return data.extension;
}

static const char *format_extension(const char *format)
{
    if (format == NULL) {
        return "bin";
    }
    if (strcmp(format, "image/urf") == 0) {
        return "urf";
    }
    if (strcmp(format, "image/pwg-raster") == 0) {
        return "pwg";
    }
    if (strcmp(format, "image/jpeg") == 0) {
        return "jpg";
    }
    if (strcmp(format, "image/png") == 0) {
        return "png";
    }
    if (strcmp(format, "application/pdf") == 0) {
        return "pdf";
    }
    if (strcmp(format, NATIVE_FORMAT) == 0) {
        return "spl";
    }
    return "bin";
}

static bool copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb"), *out = dst ? fopen(dst, "wb") : NULL;
    char buf[65536];
    size_t n;
    bool ok = in != NULL && out != NULL;

    while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0) {
        ok = fwrite(buf, 1, n, out) == n;
    }
    if (in != NULL) {
        fclose(in);
    }
    if (out != NULL) {
        fclose(out);
    }
    return ok;
}

/* ---- raster callbacks ---------------------------------------------------------------- */

static bool cb_rstartjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
    app_t *app = job_app(job);
    capture_job_t *c = calloc(1, sizeof *c);
    const char *format = papplJobGetFormat(job);

    (void)device;
    if (c == NULL) {
        return false;
    }
    papplJobSetData(job, c);
    papplLogJob(job, PAPPL_LOGLEVEL_INFO,
                "start: format %s, %u page(s), copies %d, %ux%u dpi requested, media %s, "
                "quality %d, raster %ux%u px %u bpp cspace %u",
                format ? format : "?", options->num_pages, options->copies,
                options->printer_resolution[0], options->printer_resolution[1],
                options->media.size_name, (int)options->print_quality, options->header.cupsWidth,
                options->header.cupsHeight, options->header.cupsBitsPerPixel,
                options->header.cupsColorSpace);
    if (app != NULL && app->capture_dir[0] != '\0') {
        const char *src = papplJobGetFilename(job);
        char dst[1200];
        snprintf(c->base, sizeof c->base, "%s/job-%03d", app->capture_dir, papplJobGetID(job));
        snprintf(dst, sizeof dst, "%s.%s", c->base, format_extension(format));
        if (src == NULL) {
            /* PAPPL streams raster jobs straight from the connection; only the decoded pages
             * can be captured. */
            papplLogJob(job, PAPPL_LOGLEVEL_INFO, "streamed job, no spool file to capture");
        } else if (copy_file(src, dst)) {
            papplLogJob(job, PAPPL_LOGLEVEL_INFO, "captured job file to %s", dst);
        } else {
            papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "cannot capture job file to %s: %s", dst,
                        strerror(errno));
        }
    }
    return true;
}

static bool cb_rstartpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device,
                          unsigned page)
{
    capture_job_t *c = papplJobGetData(job);
    const cups_page_header2_t *h = &options->header;
    char path[1200];

    (void)device;
    if (c == NULL) {
        return false;
    }
    c->page = page + 1; /* PAPPL counts pages from 0 */
    c->bytes_per_line = h->cupsBytesPerLine;
    c->lines_written = 0;
    c->invert = h->cupsBitsPerPixel == 8 && h->cupsColorSpace == CUPS_CSPACE_K;
    papplLogJob(job, PAPPL_LOGLEVEL_INFO,
                "page %u: %ux%u px at %ux%u dpi, %u bpc %u bpp, cspace %u, %u bytes/line, "
                "media %s %s",
                c->page, h->cupsWidth, h->cupsHeight, h->HWResolution[0], h->HWResolution[1],
                h->cupsBitsPerColor, h->cupsBitsPerPixel, h->cupsColorSpace, h->cupsBytesPerLine,
                h->cupsPageSizeName, h->MediaType);
    if (c->base[0] == '\0') {
        return true;
    }
    if (h->cupsBitsPerPixel == 1) {
        snprintf(path, sizeof path, "%s-p%u.pbm", c->base, c->page);
    } else if (h->cupsBitsPerPixel == 8) {
        snprintf(path, sizeof path, "%s-p%u.pgm", c->base, c->page);
    } else if (h->cupsBitsPerPixel == 24) {
        snprintf(path, sizeof path, "%s-p%u.ppm", c->base, c->page);
    } else {
        snprintf(path, sizeof path, "%s-p%u.raw", c->base, c->page);
    }
    c->page_file = fopen(path, "wb");
    if (c->page_file == NULL) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "cannot write %s: %s", path, strerror(errno));
        return true; /* keep printing; capture is best effort */
    }
    if (h->cupsBitsPerPixel == 1) {
        fprintf(c->page_file, "P4\n%u %u\n", h->cupsWidth, h->cupsHeight);
    } else if (h->cupsBitsPerPixel == 8) {
        fprintf(c->page_file, "P5\n%u %u\n255\n", h->cupsWidth, h->cupsHeight);
    } else if (h->cupsBitsPerPixel == 24) {
        fprintf(c->page_file, "P6\n%u %u\n255\n", h->cupsWidth, h->cupsHeight);
    }
    papplLogJob(job, PAPPL_LOGLEVEL_INFO, "capturing page %u to %s", c->page, path);
    return true;
}

static bool cb_rwriteline(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device,
                          unsigned y, const unsigned char *line)
{
    capture_job_t *c = papplJobGetData(job);

    (void)options;
    (void)device;
    (void)y;
    if (c == NULL) {
        return false;
    }
    c->lines_written++;
    if (c->page_file == NULL) {
        return true;
    }
    if (c->invert) {
        unsigned char buf[8192];
        size_t left = c->bytes_per_line, off = 0;
        while (left > 0) {
            size_t n = left < sizeof buf ? left : sizeof buf;
            for (size_t i = 0; i < n; i++) {
                buf[i] = (unsigned char)~line[off + i];
            }
            fwrite(buf, 1, n, c->page_file);
            off += n;
            left -= n;
        }
    } else {
        fwrite(line, 1, c->bytes_per_line, c->page_file);
    }
    return true;
}

static bool cb_rendpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device,
                        unsigned page)
{
    capture_job_t *c = papplJobGetData(job);

    (void)options;
    (void)device;
    if (c == NULL) {
        return false;
    }
    if (c->page_file != NULL) {
        fclose(c->page_file);
        c->page_file = NULL;
    }
    (void)page;
    papplLogJob(job, PAPPL_LOGLEVEL_INFO, "page %u done: %u lines", c->page, c->lines_written);
    papplJobSetImpressionsCompleted(job, 1);
    return true;
}

static bool cb_rendjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
    capture_job_t *c = papplJobGetData(job);

    (void)options;
    (void)device;
    if (c != NULL && c->page_file != NULL) {
        fclose(c->page_file);
    }
    free(c);
    papplJobSetData(job, NULL);
    papplLogJob(job, PAPPL_LOGLEVEL_INFO, "end of job");
    return true;
}

/* Raw jobs in our native format: keep a copy and pass the bytes to the device unchanged. */
static bool cb_printfile(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
    app_t *app = job_app(job);
    const char *src = papplJobGetFilename(job);
    FILE *in;
    char buf[65536];
    size_t n;
    bool ok = true;

    (void)options;
    if (app != NULL && app->capture_dir[0] != '\0') {
        char dst[1200];
        snprintf(dst, sizeof dst, "%s/job-%03d.spl", app->capture_dir, papplJobGetID(job));
        if (copy_file(src, dst)) {
            papplLogJob(job, PAPPL_LOGLEVEL_INFO, "captured raw job to %s", dst);
        }
    }
    in = fopen(src, "rb");
    if (in == NULL) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "cannot read %s: %s", src, strerror(errno));
        return false;
    }
    while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0) {
        ok = papplDeviceWrite(device, buf, n) == (ssize_t)n;
    }
    fclose(in);
    papplDeviceFlush(device);
    papplJobSetImpressionsCompleted(job, 1);
    return ok;
}

static bool cb_status(pappl_printer_t *printer)
{
    (void)printer; /* M6: map USB port status to printer-state-reasons */
    return true;
}

/* ---- driver data (SPEC.md 6.1) --------------------------------------------------------- */

static const char *const SOURCES[] = {"main", "manual"};
static const char *const TYPES[] = {"stationery",        "stationery-heavyweight",
                                    "stationery-lightweight", "stationery-preprinted",
                                    "cardstock",         "labels",
                                    "envelope"};

static void fill_media_col(pappl_media_col_t *col, const m2022_media_t *m)
{
    memset(col, 0, sizeof *col);
    strncpy(col->size_name, m->pwg_name, sizeof col->size_name - 1);
    col->size_width = m->width_pt * 2540 / 72;
    col->size_length = m->height_pt * 2540 / 72;
    col->left_margin = col->right_margin = M2022_MEDIA_MARGIN_HMM;
    col->top_margin = col->bottom_margin = M2022_MEDIA_MARGIN_HMM;
    strncpy(col->source, "main", sizeof col->source - 1);
    strncpy(col->type, "stationery", sizeof col->type - 1);
}

static bool cb_driver(pappl_system_t *system, const char *driver_name, const char *device_uri,
                      const char *device_id, pappl_pr_driver_data_t *data, ipp_t **driver_attrs,
                      void *cbdata)
{
    size_t n_media;
    const m2022_media_t *media = m2022_media_table(&n_media);

    (void)system;
    (void)device_uri;
    (void)device_id;
    (void)driver_attrs;
    if (strcmp(driver_name, DRIVER_NAME) != 0) {
        return false;
    }

    data->extension = cbdata;
    data->printfile_cb = cb_printfile;
    data->rstartjob_cb = cb_rstartjob;
    data->rstartpage_cb = cb_rstartpage;
    data->rwriteline_cb = cb_rwriteline;
    data->rendpage_cb = cb_rendpage;
    data->rendjob_cb = cb_rendjob;
    data->status_cb = cb_status;

    data->format = NATIVE_FORMAT;
    strncpy(data->make_and_model, MAKE_MODEL, sizeof data->make_and_model - 1);
    data->ppm = 20;
    data->ppm_color = 0;
    data->kind = PAPPL_KIND_DOCUMENT;
    data->has_supplies = false;
    data->input_face_up = false;
    data->output_face_up = false;
    data->orient_default = IPP_ORIENT_PORTRAIT;
    /* Clients (macOS, iOS) send print-color-mode=auto; a mono printer advertises auto and
     * monochrome, as the vendor's own AirPrint models do. */
    data->color_supported = PAPPL_COLOR_MODE_AUTO | PAPPL_COLOR_MODE_MONOCHROME;
    data->color_default = PAPPL_COLOR_MODE_MONOCHROME;
    data->content_default = PAPPL_CONTENT_AUTO;
    data->quality_default = IPP_QUALITY_NORMAL;
    data->scaling_default = PAPPL_SCALING_AUTO;
    /* Gray only. Advertising srgb_8 makes macOS's driverless PPD default to RGB and send
     * print-color-mode=color, which a monochrome printer must refuse (measured 2026-09-02). */
    data->raster_types = PAPPL_PWG_RASTER_TYPE_SGRAY_8 | PAPPL_PWG_RASTER_TYPE_BLACK_1;
    data->force_raster_type = PAPPL_PWG_RASTER_TYPE_NONE;
    data->duplex = PAPPL_DUPLEX_NONE;
    data->sides_supported = PAPPL_SIDES_ONE_SIDED;
    data->sides_default = PAPPL_SIDES_ONE_SIDED;
    data->finishings = PAPPL_FINISHINGS_NONE;

    /* 600 dpi only. With RS300-600 advertised, macOS renders at 300 dpi for normal quality
     * and 600 dpi only for high (measured 2026-09-02); the engine is 600 dpi and the vendor
     * driver always rendered at 600, so we advertise nothing else. */
    data->num_resolution = 1;
    data->x_resolution[0] = data->y_resolution[0] = 600;
    data->x_default = data->y_default = 600;

    data->borderless = false;
    data->left_right = M2022_MEDIA_MARGIN_HMM;
    data->bottom_top = M2022_MEDIA_MARGIN_HMM;

    data->num_media = (int)n_media;
    for (size_t i = 0; i < n_media && i < PAPPL_MAX_MEDIA; i++) {
        data->media[i] = media[i].pwg_name;
    }
    fill_media_col(&data->media_default, m2022_media_by_pwg(m2022_media_default_pwg()));
    data->media_ready[0] = data->media_default;
    data->num_source = (int)(sizeof SOURCES / sizeof SOURCES[0]);
    for (size_t i = 0; i < sizeof SOURCES / sizeof SOURCES[0]; i++) {
        data->source[i] = SOURCES[i];
    }
    data->num_type = (int)(sizeof TYPES / sizeof TYPES[0]);
    for (size_t i = 0; i < sizeof TYPES / sizeof TYPES[0]; i++) {
        data->type[i] = TYPES[i];
    }
    data->identify_supported = PAPPL_IDENTIFY_ACTIONS_NONE;
    data->identify_default = PAPPL_IDENTIFY_ACTIONS_NONE;
    return true;
}

/* ---- system ---------------------------------------------------------------------------- */

int m2022_app_run(const m2022_app_config_t *cfg)
{
    static app_t app;
    pappl_soptions_t soptions = PAPPL_SOPTIONS_WEB_INTERFACE | PAPPL_SOPTIONS_WEB_LOG |
                                PAPPL_SOPTIONS_WEB_NETWORK | PAPPL_SOPTIONS_WEB_SECURITY |
                                PAPPL_SOPTIONS_WEB_TLS;
    pappl_pr_driver_t drivers[1] = {{DRIVER_NAME, MAKE_MODEL, DEVICE_ID, NULL}};
    pappl_version_t versions[1];
    pappl_system_t *system;
    pappl_printer_t *printer;
    const char *name = cfg->name != NULL ? cfg->name : "Samsung M2022";
    char device_uri[1200];

    memset(&app, 0, sizeof app);
    if (cfg->capture_dir != NULL) {
        strncpy(app.capture_dir, cfg->capture_dir, sizeof app.capture_dir - 1);
        if (mkdir(app.capture_dir, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "cannot create capture directory %s: %s\n", app.capture_dir,
                    strerror(errno));
            return 2;
        }
    }
    if (cfg->device_uri != NULL) {
        strncpy(device_uri, cfg->device_uri, sizeof device_uri - 1);
        device_uri[sizeof device_uri - 1] = '\0';
    } else {
        snprintf(device_uri, sizeof device_uri, "file://%s/device.out",
                 app.capture_dir[0] ? app.capture_dir : "/tmp");
    }
    if (cfg->no_tls) {
        soptions |= PAPPL_SOPTIONS_NO_TLS;
    }

    system = papplSystemCreate(soptions, "M2022 AirBridge", cfg->port > 0 ? cfg->port : 8000,
                               "_print,_universal", cfg->spool_dir, cfg->log_file,
                               cfg->debug ? PAPPL_LOGLEVEL_DEBUG : PAPPL_LOGLEVEL_INFO, NULL,
                               false);
    if (system == NULL) {
        fprintf(stderr, "cannot create the PAPPL system\n");
        return 2;
    }
    memset(versions, 0, sizeof versions);
    strncpy(versions[0].name, "m2022-airbridge", sizeof versions[0].name - 1);
    strncpy(versions[0].sversion, m2022_version_string(), sizeof versions[0].sversion - 1);
    {
        int major = 0, minor = 0, patch = 0;
        m2022_version_components(&major, &minor, &patch);
        versions[0].version[0] = (unsigned short)major;
        versions[0].version[1] = (unsigned short)minor;
        versions[0].version[2] = (unsigned short)patch;
    }
    papplSystemSetVersions(system, 1, versions);
    papplSystemSetFooterHTML(system, "M2022 AirBridge &mdash; a Printer Application for the "
                                     "Samsung SL-M2022");
    papplSystemAddListeners(system, NULL);
    papplSystemSetPrinterDrivers(system, 1, drivers, NULL, NULL, cb_driver, &app);

    printer = papplPrinterCreate(system, 0, name, DRIVER_NAME, DEVICE_ID, device_uri);
    if (printer == NULL) {
        fprintf(stderr, "cannot create printer \"%s\" on %s\n", name, device_uri);
        papplSystemDelete(system);
        return 2;
    }
    papplSystemSetDefaultPrinterID(system, papplPrinterGetID(printer));
    papplLog(system, PAPPL_LOGLEVEL_INFO, "printer \"%s\" ready on port %d, device %s%s%s", name,
             cfg->port > 0 ? cfg->port : 8000, device_uri,
             app.capture_dir[0] ? ", capturing to " : "", app.capture_dir);

    papplSystemRun(system);
    papplSystemDelete(system);
    return 0;
}

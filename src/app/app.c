/*
 * PAPPL system, printer driver data (SPEC.md 6.1), the print pipeline behind the raster
 * callbacks, and the printer status callback.
 *
 * PAPPL hands us the client's page one line at a time. Each line is converted to gray, cropped
 * to the imageable area (clients send the whole page, the printer starts at the imageable
 * area's corner), tone-mapped, halftoned and fed to the job encoder, whose bytes go to the
 * device as they are produced: one band of state, never a whole page. Copies go into the
 * page records, which the printer honours (SPEC.md 16.11: three sheets for copies 3). The
 * optional capture from M2 keeps the client's decoded pages on disk for debugging.
 */
#include "m2022/app.h"
#include "m2022/halftone.h"
#include "m2022/jobmap.h"
#include "m2022/media.h"
#include "m2022/qpdl.h"
#include "m2022/raster.h"
#include "m2022/version.h"
#include "usbdev.h"

#include <errno.h>
#include <pappl/pappl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define DRIVER_NAME "m2022"
#define MAKE_MODEL "Samsung Xpress SL-M2022"
#define DEVICE_ID "MFG:Samsung;MDL:M2020 Series;CMD:SPL;CLS:PRINTER;"
#define NATIVE_FORMAT "application/vnd.samsung-spl"
#define ENGINE_DPI 600

typedef struct {
    char capture_dir[1024]; /* empty = no capture */
} app_t;

typedef struct {
    /* pipeline */
    pappl_device_t *device;
    const m2022_media_t *media;
    m2022_qpdl_encoder_t enc;
    bool job_open, page_open;
    m2022_preset_t preset;
    m2022_tone_params_t tone;
    m2022_ht_params_t hp;
    uint8_t lut[256];
    m2022_halftoner_t ht;
    void *ht_state;
    size_t ht_state_bytes;
    m2022_jobmap_geometry_t geo;
    m2022_pixel_format_t fmt;
    uint32_t raster_width;
    uint8_t *gray_full, *gray, *ink, *bits;
    void *workspace;
    size_t workspace_bytes;
    uint32_t alloc_raster_width, alloc_width;
    int copies;
    unsigned page;
    uint32_t lines_in;
    size_t bytes_out;
    double t_start;
    char date[16], producer[64];
    /* capture (optional) */
    char base[1100]; /* capture_dir/job-NNN */
    FILE *page_file;
    unsigned bytes_per_line;
    bool invert; /* 8-bit K raster: 255 = black, PGM wants 0 = black */
} job_t;

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

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

static m2022_content_t content_from(pappl_content_t c)
{
    switch (c) {
    case PAPPL_CONTENT_TEXT:
        return M2022_CONTENT_TEXT;
    case PAPPL_CONTENT_PHOTO:
        return M2022_CONTENT_PHOTO;
    case PAPPL_CONTENT_GRAPHIC:
        return M2022_CONTENT_GRAPHIC;
    case PAPPL_CONTENT_TEXT_AND_GRAPHIC:
        return M2022_CONTENT_TEXT_AND_GRAPHIC;
    default:
        return M2022_CONTENT_AUTO;
    }
}

static const char *content_name(m2022_content_t c)
{
    static const char *const NAMES[] = {"auto", "text", "graphic", "photo", "text-and-graphic"};
    return NAMES[c];
}

/* The encoder's sink: straight to the device. */
static int device_sink(void *ctx, const uint8_t *data, size_t len)
{
    job_t *j = ctx;

    if (papplDeviceWrite(j->device, data, len) != (ssize_t)len) {
        return -1;
    }
    j->bytes_out += len;
    return 0;
}

static void job_free(job_t *j)
{
    if (j == NULL) {
        return;
    }
    if (j->page_file != NULL) {
        fclose(j->page_file);
    }
    free(j->gray_full);
    free(j->gray);
    free(j->ink);
    free(j->bits);
    free(j->ht_state);
    free(j->workspace);
    free(j);
}

/* ---- raster callbacks ---------------------------------------------------------------- */

static bool cb_rstartjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
    app_t *app = job_app(job);
    job_t *j = calloc(1, sizeof *j);
    const char *format = papplJobGetFormat(job);
    m2022_qpdl_job_t qjob;
    m2022_content_t content;
    time_t now = time(NULL);
    int rc;

    if (j == NULL) {
        return false;
    }
    papplJobSetData(job, j);
    j->device = device;
    j->t_start = now_seconds();
    j->media = m2022_media_by_pwg(options->media.size_name);
    if (j->media == NULL) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "unsupported media size '%s'",
                    options->media.size_name);
        return false;
    }
    content = content_from(options->print_content_optimize);
    j->preset = m2022_jobmap_preset((int)options->print_quality, content);
    m2022_preset(j->preset, &j->tone, &j->hp);
    m2022_tone_build(&j->tone, j->lut);
    j->copies = options->copies > 0 ? options->copies : 1;
    strftime(j->date, sizeof j->date, "%Y%m%d", localtime(&now));
    snprintf(j->producer, sizeof j->producer, "M2022 AirBridge %s", m2022_version_string());

    m2022_qpdl_job_default(&qjob);
    qjob.paper_code = j->media->qpdl_code;
    qjob.paper_width_pt = j->media->width_pt;
    qjob.paper_height_pt = j->media->height_pt;
    qjob.dpi = ENGINE_DPI;
    qjob.feeder = m2022_jobmap_feeder(options->media.source);
    qjob.paper_type = m2022_jobmap_paper_type(options->media.type);
    qjob.copies = (uint16_t)(j->copies > 65535 ? 65535 : j->copies); /* the printer repeats */
    qjob.service_date = j->date;
    qjob.producer = j->producer;

    papplLogJob(job, PAPPL_LOGLEVEL_INFO,
                "start: format %s, %u page(s), copies %d, media %s (%s) source %s type %s -> "
                "feeder %s PAPERTYPE %s, quality %d content %s -> preset %s",
                format ? format : "?", options->num_pages, j->copies, options->media.size_name,
                j->media->vendor_name, options->media.source, options->media.type,
                m2022_qpdl_feeder_name(qjob.feeder), qjob.paper_type, (int)options->print_quality,
                content_name(content), m2022_preset_name(j->preset));

    rc = m2022_qpdl_begin_job(&j->enc, &qjob, device_sink, j);
    if (rc != 0) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "cannot start the job: %s",
                    m2022_qpdl_strerror(rc));
        return false;
    }
    j->job_open = true;

    if (app != NULL && app->capture_dir[0] != '\0') {
        const char *src = papplJobGetFilename(job);
        char dst[1200];
        snprintf(j->base, sizeof j->base, "%s/job-%03d", app->capture_dir, papplJobGetID(job));
        snprintf(dst, sizeof dst, "%s.%s", j->base, format_extension(format));
        if (src == NULL) {
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

static bool page_buffers(job_t *j, uint32_t raster_width, uint32_t width)
{
    if (j->alloc_raster_width == raster_width && j->alloc_width == width) {
        return true;
    }
    free(j->gray_full);
    free(j->gray);
    free(j->ink);
    free(j->bits);
    free(j->ht_state);
    free(j->workspace);
    j->ht_state_bytes = m2022_halftoner_state_bytes(width);
    j->workspace_bytes = m2022_qpdl_encoder_workspace_bytes(width);
    j->gray_full = malloc(raster_width);
    j->gray = malloc(width);
    j->ink = malloc(width);
    j->bits = malloc(((size_t)width + 7) / 8);
    j->ht_state = malloc(j->ht_state_bytes);
    j->workspace = malloc(j->workspace_bytes);
    if (j->gray_full == NULL || j->gray == NULL || j->ink == NULL || j->bits == NULL ||
        j->ht_state == NULL || j->workspace == NULL) {
        return false;
    }
    j->alloc_raster_width = raster_width;
    j->alloc_width = width;
    return true;
}

static bool cb_rstartpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device,
                          unsigned page)
{
    job_t *j = papplJobGetData(job);
    const cups_page_header2_t *h = &options->header;
    char path[1200];
    int rc;

    (void)device;
    if (j == NULL || !j->job_open) {
        return false;
    }
    j->page = page + 1; /* PAPPL counts pages from 0 */
    j->lines_in = 0;
    j->raster_width = h->cupsWidth;

    if (h->cupsBitsPerPixel == 8 &&
        (h->cupsColorSpace == CUPS_CSPACE_SW || h->cupsColorSpace == CUPS_CSPACE_W)) {
        j->fmt = M2022_PIX_SGRAY_8;
    } else if (h->cupsBitsPerPixel == 8 && h->cupsColorSpace == CUPS_CSPACE_K) {
        j->fmt = M2022_PIX_BLACK_8;
    } else if (h->cupsBitsPerPixel == 1 && h->cupsColorSpace == CUPS_CSPACE_K) {
        j->fmt = M2022_PIX_BLACK_1;
    } else {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "page %u: unsupported raster (%u bpp, cspace %u)",
                    j->page, h->cupsBitsPerPixel, h->cupsColorSpace);
        return false;
    }
    if (h->HWResolution[0] != ENGINE_DPI || h->HWResolution[1] != ENGINE_DPI) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "page %u: rendered at %ux%u dpi, need %u",
                    j->page, h->HWResolution[0], h->HWResolution[1], ENGINE_DPI);
        return false;
    }
    if (!m2022_jobmap_geometry(j->media, ENGINE_DPI, h->cupsWidth, h->cupsHeight, &j->geo)) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "page %u: empty raster", j->page);
        return false;
    }
    if (!page_buffers(j, h->cupsWidth, j->geo.width) ||
        m2022_halftoner_init(&j->ht, &j->hp, j->geo.width, j->ht_state, j->ht_state_bytes) != 0) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "page %u: out of memory", j->page);
        return false;
    }
    rc = m2022_qpdl_begin_page(&j->enc, j->geo.width, j->workspace, j->workspace_bytes);
    if (rc != 0) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "page %u: cannot start: %s", j->page,
                    m2022_qpdl_strerror(rc));
        return false;
    }
    j->page_open = true;
    papplLogJob(job, PAPPL_LOGLEVEL_INFO,
                "page %u: raster %ux%u px, %u bpp cspace %u, %s; printable %ux%u at %u,%u; "
                "band width %u",
                j->page, h->cupsWidth, h->cupsHeight, h->cupsBitsPerPixel, h->cupsColorSpace,
                h->cupsPageSizeName, j->geo.width, j->geo.height, j->geo.x0, j->geo.y0,
                j->enc.band_width);

    /* optional capture of the client's page as it arrived */
    if (j->base[0] == '\0') {
        return true;
    }
    j->bytes_per_line = h->cupsBytesPerLine;
    j->invert = h->cupsBitsPerPixel == 8 && h->cupsColorSpace == CUPS_CSPACE_K;
    if (h->cupsBitsPerPixel == 1) {
        snprintf(path, sizeof path, "%s-p%u.pbm", j->base, j->page);
    } else if (h->cupsBitsPerPixel == 8) {
        snprintf(path, sizeof path, "%s-p%u.pgm", j->base, j->page);
    } else {
        snprintf(path, sizeof path, "%s-p%u.raw", j->base, j->page);
    }
    j->page_file = fopen(path, "wb");
    if (j->page_file == NULL) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "cannot write %s: %s", path, strerror(errno));
        return true; /* keep printing; capture is best effort */
    }
    if (h->cupsBitsPerPixel == 1) {
        fprintf(j->page_file, "P4\n%u %u\n", h->cupsWidth, h->cupsHeight);
    } else if (h->cupsBitsPerPixel == 8) {
        fprintf(j->page_file, "P5\n%u %u\n255\n", h->cupsWidth, h->cupsHeight);
    }
    papplLogJob(job, PAPPL_LOGLEVEL_INFO, "capturing page %u to %s", j->page, path);
    return true;
}

static bool cb_rwriteline(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device,
                          unsigned y, const unsigned char *line)
{
    job_t *j = papplJobGetData(job);
    int rc;

    (void)options;
    (void)device;
    if (j == NULL || !j->page_open) {
        return false;
    }
    if (j->page_file != NULL) {
        if (j->invert) {
            unsigned char buf[8192];
            size_t left = j->bytes_per_line, off = 0;
            while (left > 0) {
                size_t n = left < sizeof buf ? left : sizeof buf;
                for (size_t i = 0; i < n; i++) {
                    buf[i] = (unsigned char)~line[off + i];
                }
                fwrite(buf, 1, n, j->page_file);
                off += n;
                left -= n;
            }
        } else {
            fwrite(line, 1, j->bytes_per_line, j->page_file);
        }
    }
    if (y < j->geo.y0 || y >= j->geo.y0 + j->geo.height) {
        return true; /* margin: the printer cannot reach it */
    }
    m2022_raster_line_to_gray(j->fmt, line, j->raster_width, j->gray_full);
    m2022_raster_fit_line(j->gray_full, j->raster_width, (int32_t)j->geo.x0, j->gray,
                          j->geo.width, 255);
    m2022_tone_apply(j->lut, j->gray, j->geo.width, j->ink);
    m2022_halftone_line(&j->ht, j->ink, y - j->geo.y0, j->bits);
    rc = m2022_qpdl_write_line(&j->enc, j->bits);
    if (rc != 0) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "page %u line %u: %s", j->page, y,
                    rc == -1 ? "device write failed" : m2022_qpdl_strerror(rc));
        return false;
    }
    j->lines_in++;
    return true;
}

static bool cb_rendpage(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device,
                        unsigned page)
{
    job_t *j = papplJobGetData(job);
    int rc;

    (void)options;
    (void)device;
    (void)page;
    if (j == NULL || !j->page_open) {
        return false;
    }
    if (j->page_file != NULL) {
        fclose(j->page_file);
        j->page_file = NULL;
    }
    j->page_open = false;
    rc = m2022_qpdl_end_page(&j->enc);
    if (rc != 0) {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "page %u: cannot finish: %s", j->page,
                    rc == -1 ? "device write failed" : m2022_qpdl_strerror(rc));
        return false;
    }
    papplLogJob(job, PAPPL_LOGLEVEL_INFO, "page %u done: %u lines in, %u bands so far (%u blank)",
                j->page, j->lines_in, j->enc.bands_written, j->enc.bands_blank);
    return true; /* PAPPL counts impressions for raster jobs itself */
}

static bool cb_rendjob(pappl_job_t *job, pappl_pr_options_t *options, pappl_device_t *device)
{
    job_t *j = papplJobGetData(job);
    bool ok = true;
    char summary[64];
    int rc;

    (void)options;
    if (j == NULL) {
        return false;
    }
    if (j->page_open) { /* cancelled mid-page: keep the stream well-formed */
        j->page_open = false;
        ok = m2022_qpdl_end_page(&j->enc) == 0;
    }
    if (j->job_open) {
        j->job_open = false;
        rc = m2022_qpdl_end_job(&j->enc);
        if (rc != 0) {
            papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "cannot finish the job: %s",
                        rc == -1 ? "device write failed" : m2022_qpdl_strerror(rc));
            ok = false;
        }
    }
    papplDeviceFlush(device);
    /* PAPPL's logger formats %u and %s itself; no %zu, no %f */
    snprintf(summary, sizeof summary, "%lu bytes, %.2f s", (unsigned long)j->bytes_out,
             now_seconds() - j->t_start);
    papplLogJob(job, PAPPL_LOGLEVEL_INFO,
                "end of job: %u page(s), %u bands (%u blank omitted), %s%s",
                j->enc.pages, j->enc.bands_written, j->enc.bands_blank, summary,
                ok ? "" : ", with errors");
    job_free(j);
    papplJobSetData(job, NULL);
    return ok;
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

/*
 * PAPPL asks between jobs, at most once a second. Opening the device is what tells us the
 * printer is there at all; the port status byte gives paper-out and error (docs/usb.md).
 */
static bool cb_status(pappl_printer_t *printer)
{
    pappl_device_t *device = papplPrinterOpenDevice(printer);
    pappl_preason_t reasons, clear = PAPPL_PREASON_DEVICE_STATUS | PAPPL_PREASON_OFFLINE;

    if (device == NULL) {
        papplPrinterSetReasons(printer, PAPPL_PREASON_OFFLINE,
                               clear & ~(pappl_preason_t)PAPPL_PREASON_OFFLINE);
        return true;
    }
    reasons = papplDeviceGetStatus(device);
    papplPrinterCloseDevice(printer);
    papplPrinterSetReasons(printer, reasons, clear & ~reasons);
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
    data->x_resolution[0] = data->y_resolution[0] = ENGINE_DPI;
    data->x_default = data->y_default = ENGINE_DPI;

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

static bool save_state(pappl_system_t *system, void *data)
{
    return papplSystemSaveState(system, (const char *)data);
}

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
    const char *device_uri = cfg->device_uri != NULL ? cfg->device_uri : M2022_USBDEV_DEFAULT_URI;

    memset(&app, 0, sizeof app);
    if (cfg->capture_dir != NULL) {
        strncpy(app.capture_dir, cfg->capture_dir, sizeof app.capture_dir - 1);
        if (mkdir(app.capture_dir, 0755) != 0 && errno != EEXIST) {
            fprintf(stderr, "cannot create capture directory %s: %s\n", app.capture_dir,
                    strerror(errno));
            return 2;
        }
    }
    if (cfg->no_tls) {
        soptions |= PAPPL_SOPTIONS_NO_TLS;
    }

    m2022_usbdev_register();
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

    /* The state file keeps the printer's ID and the settings made in the web interface across
     * restarts; a saved printer wins over the command line (delete the file to reset). */
    if (cfg->state_file != NULL) {
        papplSystemLoadState(system, cfg->state_file);
        papplSystemSetSaveCallback(system, save_state, (void *)(uintptr_t)cfg->state_file);
    }
    printer = papplSystemFindPrinter(system, "/ipp/print", 0, NULL);
    if (printer == NULL) {
        printer = papplPrinterCreate(system, 0, name, DRIVER_NAME, DEVICE_ID, device_uri);
        if (printer == NULL) {
            fprintf(stderr, "cannot create printer \"%s\" on %s\n", name, device_uri);
            papplSystemDelete(system);
            return 2;
        }
        if (cfg->state_file != NULL) {
            papplSystemSaveState(system, cfg->state_file);
        }
    }
    papplSystemSetDefaultPrinterID(system, papplPrinterGetID(printer));
    papplLog(system, PAPPL_LOGLEVEL_INFO, "printer \"%s\" ready on port %d, device %s%s%s", name,
             cfg->port > 0 ? cfg->port : 8000, device_uri,
             app.capture_dir[0] ? ", capturing to " : "", app.capture_dir);

    papplSystemRun(system);
    papplSystemDelete(system);
    return 0;
}

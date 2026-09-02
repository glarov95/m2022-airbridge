#include "m2022/jobmap.h"

#include <math.h>
#include <string.h>

m2022_preset_t m2022_jobmap_preset(int ipp_quality, m2022_content_t content)
{
    if (ipp_quality == M2022_IPP_QUALITY_DRAFT) {
        return M2022_PRESET_DRAFT;
    }
    if (content == M2022_CONTENT_TEXT) {
        return M2022_PRESET_TEXT;
    }
    if (content == M2022_CONTENT_PHOTO || ipp_quality == M2022_IPP_QUALITY_HIGH) {
        return M2022_PRESET_PHOTO;
    }
    return M2022_PRESET_NORMAL;
}

const char *m2022_jobmap_paper_type(const char *pwg_media_type)
{
    /* PJL values from the vendor PPD's MediaType choices (docs/spl-qpdl.md 1) */
    static const struct {
        const char *pwg, *pjl;
    } MAP[] = {
        {"stationery", "OFF"}, /* the vendor's "Printer Default" */
        {"stationery-heavyweight", "THICK"},
        {"stationery-lightweight", "THIN"},
        {"stationery-preprinted", "USED"},
        {"cardstock", "CARD"},
        {"labels", "LABEL"},
        {"envelope", "ENV"},
    };
    if (pwg_media_type != NULL) {
        for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++) {
            if (strcmp(MAP[i].pwg, pwg_media_type) == 0) {
                return MAP[i].pjl;
            }
        }
    }
    return "OFF";
}

uint8_t m2022_jobmap_feeder(const char *pwg_media_source)
{
    return pwg_media_source != NULL && strcmp(pwg_media_source, "manual") == 0 ? 2 : 1;
}

/* Points (to a hundredth, as the PPD writes them) to pixels, rounded half up: the vendor's
 * EnvDL raster is 2368 px wide for 284.10 pt (2367.5), so the exact half must round up, which
 * integer arithmetic on hundredths guarantees where floating point does not. */
static uint32_t px(double points, unsigned dpi)
{
    long hundredths = lround(points * 100.0);
    long v = (hundredths * (long)dpi + 3600) / 7200;
    return v <= 0 ? 0 : (uint32_t)v;
}

bool m2022_jobmap_geometry(const m2022_media_t *media, unsigned dpi, uint32_t raster_width,
                           uint32_t raster_height, m2022_jobmap_geometry_t *g)
{
    uint32_t pw, ph, left, top;

    memset(g, 0, sizeof *g);
    if (media == NULL || dpi == 0 || raster_width == 0 || raster_height == 0) {
        return false;
    }
    pw = px(media->imageable[2] - media->imageable[0], dpi);
    ph = px(media->imageable[3] - media->imageable[1], dpi);
    left = px(media->imageable[0], dpi);
    top = px((double)media->height_pt - media->imageable[3], dpi);
    if (pw == 0 || ph == 0) {
        return false;
    }
    /* a raster wide enough to hold the imageable area plus its margin is a whole page */
    g->x0 = raster_width >= pw + left ? left : 0;
    g->y0 = raster_height >= ph + top ? top : 0;
    g->width = raster_width - g->x0 < pw ? raster_width - g->x0 : pw;
    g->height = raster_height - g->y0 < ph ? raster_height - g->y0 : ph;
    return true;
}

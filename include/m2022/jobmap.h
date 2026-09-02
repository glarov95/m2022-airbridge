/*
 * From IPP job options to what the pipeline and the printer need: preset from print-quality
 * and print-content-optimize, PJL paper type from the PWG media type, feeder from the media
 * source, and where the printable area lies in the client's raster. Pure; unit-tested.
 */
#ifndef M2022_JOBMAP_H
#define M2022_JOBMAP_H

#include "m2022/halftone.h"
#include "m2022/media.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    M2022_CONTENT_AUTO,
    M2022_CONTENT_TEXT,
    M2022_CONTENT_GRAPHIC,
    M2022_CONTENT_PHOTO,
    M2022_CONTENT_TEXT_AND_GRAPHIC,
} m2022_content_t;

enum { M2022_IPP_QUALITY_DRAFT = 3, M2022_IPP_QUALITY_NORMAL = 4, M2022_IPP_QUALITY_HIGH = 5 };

/*
 * draft -> Draft for any content; text content -> Text; photo content -> Photo; otherwise
 * normal -> Normal (blue noise) and high -> Photo (error diffusion). Documented in
 * docs/ipp-airprint.md; the M10 quality program may move these.
 */
m2022_preset_t m2022_jobmap_preset(int ipp_quality, m2022_content_t content);

/* PWG media type -> PJL PAPERTYPE value (docs/spl-qpdl.md 1); unknown or NULL -> "OFF". */
const char *m2022_jobmap_paper_type(const char *pwg_media_type);

/* PWG media source -> QPDL feeder: "manual" -> manual, anything else -> auto. */
uint8_t m2022_jobmap_feeder(const char *pwg_media_source);

typedef struct {
    uint32_t x0, y0;          /* top-left of the printable area in the client raster */
    uint32_t width, height;   /* pixels sent to the printer (the page's band content) */
} m2022_jobmap_geometry_t;

/*
 * Clients send the whole page (iOS and macOS: 4960x7015 for A4 at 600 dpi); the vendor filter
 * sent just the imageable area (4750x6808). The printer starts at the imageable area's
 * top-left, so a full-page raster is cropped by the PPD margins and a smaller raster is used
 * from its own origin, never wider or taller than the imageable area. Returns false for a
 * raster with a zero dimension.
 */
bool m2022_jobmap_geometry(const m2022_media_t *media, unsigned dpi, uint32_t raster_width,
                           uint32_t raster_height, m2022_jobmap_geometry_t *g);

#endif /* M2022_JOBMAP_H */

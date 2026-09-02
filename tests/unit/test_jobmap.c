/*
 * Job option mapping, and the geometry rule against every vendor input raster: the imageable
 * area from the PPD must give exactly the raster size the vendor filter produced.
 */
#include "m2022/fileio.h"
#include "m2022/jobmap.h"
#include "m2022/raster.h"

#include "m2022_test.h"

#include <stdlib.h>

#ifndef M2022_FIXTURE_DIR
#error "M2022_FIXTURE_DIR must point at fixtures/oracle/samsung"
#endif

int main(void)
{
    const m2022_media_t *a4 = m2022_media_by_pwg("iso_a4_210x297mm");
    const m2022_media_t *letter = m2022_media_by_pwg("na_letter_8.5x11in");
    m2022_jobmap_geometry_t g;
    size_t count;
    const m2022_media_t *table = m2022_media_table(&count);

    /* presets */
    CHECK_EQ_INT(m2022_jobmap_preset(M2022_IPP_QUALITY_DRAFT, M2022_CONTENT_AUTO),
                 M2022_PRESET_DRAFT);
    CHECK_EQ_INT(m2022_jobmap_preset(M2022_IPP_QUALITY_DRAFT, M2022_CONTENT_PHOTO),
                 M2022_PRESET_DRAFT);
    CHECK_EQ_INT(m2022_jobmap_preset(M2022_IPP_QUALITY_NORMAL, M2022_CONTENT_AUTO),
                 M2022_PRESET_NORMAL);
    CHECK_EQ_INT(m2022_jobmap_preset(M2022_IPP_QUALITY_NORMAL, M2022_CONTENT_TEXT),
                 M2022_PRESET_TEXT);
    CHECK_EQ_INT(m2022_jobmap_preset(M2022_IPP_QUALITY_NORMAL, M2022_CONTENT_PHOTO),
                 M2022_PRESET_PHOTO);
    CHECK_EQ_INT(m2022_jobmap_preset(M2022_IPP_QUALITY_NORMAL, M2022_CONTENT_GRAPHIC),
                 M2022_PRESET_NORMAL);
    CHECK_EQ_INT(m2022_jobmap_preset(M2022_IPP_QUALITY_NORMAL, M2022_CONTENT_TEXT_AND_GRAPHIC),
                 M2022_PRESET_NORMAL);
    CHECK_EQ_INT(m2022_jobmap_preset(M2022_IPP_QUALITY_HIGH, M2022_CONTENT_AUTO),
                 M2022_PRESET_PHOTO);
    CHECK_EQ_INT(m2022_jobmap_preset(M2022_IPP_QUALITY_HIGH, M2022_CONTENT_TEXT),
                 M2022_PRESET_TEXT);
    CHECK_EQ_INT(m2022_jobmap_preset(0, M2022_CONTENT_AUTO), M2022_PRESET_NORMAL); /* unset */

    /* paper types and feeder */
    CHECK_EQ_STR(m2022_jobmap_paper_type("stationery"), "OFF");
    CHECK_EQ_STR(m2022_jobmap_paper_type("stationery-heavyweight"), "THICK");
    CHECK_EQ_STR(m2022_jobmap_paper_type("stationery-lightweight"), "THIN");
    CHECK_EQ_STR(m2022_jobmap_paper_type("stationery-preprinted"), "USED");
    CHECK_EQ_STR(m2022_jobmap_paper_type("cardstock"), "CARD");
    CHECK_EQ_STR(m2022_jobmap_paper_type("labels"), "LABEL");
    CHECK_EQ_STR(m2022_jobmap_paper_type("envelope"), "ENV");
    CHECK_EQ_STR(m2022_jobmap_paper_type("photographic-glossy"), "OFF");
    CHECK_EQ_STR(m2022_jobmap_paper_type(NULL), "OFF");
    CHECK_EQ_INT(m2022_jobmap_feeder("manual"), 2);
    CHECK_EQ_INT(m2022_jobmap_feeder("main"), 1);
    CHECK_EQ_INT(m2022_jobmap_feeder(NULL), 1);

    /* geometry: what iOS and macOS send for A4 (docs/ipp-airprint.md) */
    CHECK(a4 != NULL && letter != NULL);
    CHECK(m2022_jobmap_geometry(a4, 600, 4960, 7015, &g));
    CHECK_EQ_INT(g.x0, 104);
    CHECK_EQ_INT(g.y0, 104);
    CHECK_EQ_INT(g.width, 4750);
    CHECK_EQ_INT(g.height, 6808);
    /* the vendor's own raster: already the imageable area */
    CHECK(m2022_jobmap_geometry(a4, 600, 4750, 6808, &g));
    CHECK_EQ_INT(g.x0, 0);
    CHECK_EQ_INT(g.y0, 0);
    CHECK_EQ_INT(g.width, 4750);
    CHECK_EQ_INT(g.height, 6808);
    /* Letter: asymmetric PPD margins */
    CHECK(m2022_jobmap_geometry(letter, 600, 5100, 6600, &g));
    CHECK_EQ_INT(g.x0, 102);
    CHECK_EQ_INT(g.y0, 100);
    CHECK_EQ_INT(g.width, 4896);
    CHECK_EQ_INT(g.height, 6400);
    /* a small raster is used as it is; a huge one is clipped to the imageable area */
    CHECK(m2022_jobmap_geometry(a4, 600, 1000, 500, &g));
    CHECK_EQ_INT(g.x0 + g.y0, 0);
    CHECK_EQ_INT(g.width, 1000);
    CHECK_EQ_INT(g.height, 500);
    CHECK(m2022_jobmap_geometry(a4, 600, 9000, 9000, &g));
    CHECK_EQ_INT(g.width, 4750);
    CHECK_EQ_INT(g.height, 6808);
    /* 1200 dpi doubles everything */
    CHECK(m2022_jobmap_geometry(a4, 1200, 9921, 14031, &g));
    CHECK_EQ_INT(g.x0, 208);
    CHECK_EQ_INT(g.width, 9500);
    CHECK(!m2022_jobmap_geometry(a4, 600, 0, 7015, &g));
    CHECK(!m2022_jobmap_geometry(NULL, 600, 4960, 7015, &g));

    /* every vendor input raster in the media sweep has exactly the imageable area's size */
    for (size_t i = 0; i < count; i++) {
        char path[1024];
        size_t len = 0;
        uint8_t *data;
        m2022_cupsraster_header_t h;
        snprintf(path, sizeof path, "%s/media/%s.ras.gz", M2022_FIXTURE_DIR, table[i].vendor_name);
        data = m2022_read_file(path, &len);
        CHECK(data != NULL);
        if (data == NULL) {
            continue;
        }
        CHECK_EQ_INT(m2022_cupsraster_parse_header(data, len, &h), 0);
        CHECK(m2022_jobmap_geometry(&table[i], 600, h.width, h.height, &g));
        if (g.width != h.width || g.height != h.height || g.x0 != 0 || g.y0 != 0) {
            fprintf(stderr, "%s: vendor raster %ux%u, imageable %ux%u at %u,%u\n",
                    table[i].vendor_name, h.width, h.height, g.width, g.height, g.x0, g.y0);
        }
        CHECK_EQ_INT(g.width, h.width);
        CHECK_EQ_INT(g.height, h.height);
        free(data);
    }

    TEST_MAIN_END();
}

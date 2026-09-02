/*
 * The paper sizes this printer supports, in the three vocabularies that meet in this project:
 * PWG self-describing media names (IPP, what clients and PAPPL use), the vendor PPD names
 * (what the fixtures are labelled with), and the QPDL paper code the printer expects in the
 * page header (docs/spl-qpdl.md 2.1, verified on the media sweep).
 */
#ifndef M2022_MEDIA_H
#define M2022_MEDIA_H

#include <stddef.h>
#include <stdint.h>

#define M2022_MEDIA_MARGIN_HMM 441 /* 12.5 pt hardware margin in hundredths of a millimetre */

typedef struct {
    const char *pwg_name;
    const char *vendor_name;
    uint8_t qpdl_code;
    int width_pt, height_pt; /* from the vendor PPD PaperDimension */
} m2022_media_t;

const m2022_media_t *m2022_media_table(size_t *count);
const m2022_media_t *m2022_media_by_pwg(const char *pwg_name);
const m2022_media_t *m2022_media_by_qpdl_code(uint8_t code);
const char *m2022_media_default_pwg(void);

#endif /* M2022_MEDIA_H */

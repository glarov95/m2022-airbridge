#include "m2022/media.h"

#include <string.h>

/* PWG names as the vendor CUPS queue advertised them in media-supported (fixtures/oracle/
 * samsung/ipp-printer-attributes.txt); QPDL codes and points from the media sweep; imageable
 * areas from the PPD (fixtures/oracle/samsung/Samsung_M2020_Series.ppd, *ImageableArea). */
static const m2022_media_t MEDIA[] = {
    {"iso_a4_210x297mm", "A4", 0x02, 595, 842, {12.5, 12.5, 582.5, 829.5}},
    {"na_letter_8.5x11in", "Letter", 0x00, 612, 792, {12.24, 12.06, 599.76, 780.06}},
    {"na_legal_8.5x14in", "Legal", 0x01, 612, 1008, {12.5, 12.5, 599.5, 995.5}},
    {"na_executive_7.25x10.5in", "Executive", 0x03, 522, 756, {13.3, 12.1, 508.7, 744.1}},
    {"iso_a5_148x210mm", "A5", 0x10, 420, 595, {12.5, 12.5, 407.5, 582.5}},
    {"jis_b5_182x257mm", "B5-JIS", 0x0B, 516, 729, {12.5, 12.5, 503.5, 716.5}},
    {"iso_b5_176x250mm", "B5-ISO", 0x0C, 499, 709, {13.2, 12.06, 485.52, 696.54}},
    {"na_foolscap_8.5x13in", "US-Folio", 0x18, 612, 936, {12.5, 12.5, 599.5, 923.5}},
    {"custom_8.5x13.5in_8.5x13.5in", "Oficio", 0x1C, 612, 972, {12.5, 12.5, 599.5, 959.5}},
    {"na_index-4x6_4x6in", "Postcard_S", 0x0D, 288, 432, {12.5, 12.5, 275.5, 419.5}},
    {"iso_c5_162x229mm", "EnvC5", 0x08, 459, 649, {12.6, 12.1, 446.5, 637.0}},
    {"iso_dl_110x220mm", "EnvDL", 0x09, 312, 624, {13.8, 12.1, 297.9, 611.6}},
    {"na_number-10_4.125x9.5in", "Env10", 0x06, 297, 684, {13.0, 13.0, 284.5, 671.5}},
    {"na_monarch_3.875x7.5in", "EnvMonarch", 0x07, 279, 540, {12.7, 12.1, 266.2, 528.1}},
};

const m2022_media_t *m2022_media_table(size_t *count)
{
    if (count != NULL) {
        *count = sizeof MEDIA / sizeof MEDIA[0];
    }
    return MEDIA;
}

const m2022_media_t *m2022_media_by_pwg(const char *pwg_name)
{
    if (pwg_name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof MEDIA / sizeof MEDIA[0]; i++) {
        if (strcmp(MEDIA[i].pwg_name, pwg_name) == 0) {
            return &MEDIA[i];
        }
    }
    return NULL;
}

const m2022_media_t *m2022_media_by_qpdl_code(uint8_t code)
{
    for (size_t i = 0; i < sizeof MEDIA / sizeof MEDIA[0]; i++) {
        if (MEDIA[i].qpdl_code == code) {
            return &MEDIA[i];
        }
    }
    return NULL;
}

const char *m2022_media_default_pwg(void)
{
    return MEDIA[0].pwg_name;
}

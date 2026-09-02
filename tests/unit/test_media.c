/*
 * Every media name we advertise must be one libcups (and therefore PAPPL and every IPP client)
 * can resolve to dimensions, and those dimensions must match the vendor PPD.
 */
#include "m2022/media.h"

#include "m2022_test.h"

#include <cups/pwg.h>

int main(void)
{
    size_t n = 0;
    const m2022_media_t *m = m2022_media_table(&n);

    CHECK_EQ_INT(n, 14);
    for (size_t i = 0; i < n; i++) {
        pwg_media_t *pwg = pwgMediaForPWG(m[i].pwg_name);
        int w_hmm = m[i].width_pt * 2540 / 72, h_hmm = m[i].height_pt * 2540 / 72;
        CHECK(pwg != NULL);
        if (pwg == NULL) {
            fprintf(stderr, "unresolvable media name %s\n", m[i].pwg_name);
            continue;
        }
        /* libcups sizes are in hundredths of millimetres; allow rounding of a point. */
        CHECK(abs(pwg->width - w_hmm) <= 40);
        CHECK(abs(pwg->length - h_hmm) <= 40);
        if (abs(pwg->width - w_hmm) > 40 || abs(pwg->length - h_hmm) > 40) {
            fprintf(stderr, "%s: pwg %dx%d vs ppd %dx%d\n", m[i].pwg_name, pwg->width, pwg->length,
                    w_hmm, h_hmm);
        }
        CHECK(m2022_media_by_pwg(m[i].pwg_name) == &m[i]);
        CHECK(m2022_media_by_qpdl_code(m[i].qpdl_code) == &m[i]);
    }
    CHECK_EQ_STR(m2022_media_default_pwg(), "iso_a4_210x297mm");
    CHECK_EQ_INT(m2022_media_by_pwg("iso_a4_210x297mm")->qpdl_code, 0x02);
    CHECK_EQ_INT(m2022_media_by_pwg("na_letter_8.5x11in")->qpdl_code, 0x00);
    CHECK_EQ_INT(m2022_media_by_qpdl_code(0x1C)->width_pt, 612);
    CHECK(m2022_media_by_pwg("iso_a3_297x420mm") == NULL);
    CHECK(m2022_media_by_pwg(NULL) == NULL);
    CHECK(m2022_media_by_qpdl_code(0x7f) == NULL);
    CHECK_EQ_INT(M2022_MEDIA_MARGIN_HMM, 441);

    TEST_MAIN_END();
}

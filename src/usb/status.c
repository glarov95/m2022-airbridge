#include "m2022/usb.h"

size_t m2022_usb_status_reasons(uint8_t status, const char **reasons, size_t max)
{
    size_t n = 0;

    if ((status & M2022_USB_STATUS_PAPER_EMPTY) != 0) {
        if (n < max) {
            reasons[n] = "media-empty";
        }
        n++;
    }
    if ((status & M2022_USB_STATUS_SELECTED) == 0) {
        if (n < max) {
            reasons[n] = "offline";
        }
        n++;
    }
    if ((status & M2022_USB_STATUS_NO_ERROR) == 0) {
        if (n < max) {
            reasons[n] = "other";
        }
        n++;
    }
    return n;
}

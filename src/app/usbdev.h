/* PAPPL device scheme "m2022usb" over our USB printer-class transport (src/usb/). */
#ifndef M2022_APP_USBDEV_H
#define M2022_APP_USBDEV_H

#define M2022_USBDEV_SCHEME "m2022usb"
/* Any SL-M2022 on the bus; add ?serial=S to pick one. */
#define M2022_USBDEV_DEFAULT_URI "m2022usb://04e8:3321"

/* Register the scheme with PAPPL; call once before creating the system. */
void m2022_usbdev_register(void);

#endif /* M2022_APP_USBDEV_H */

/*
 * USB printer-class transport (USB Device Class Definition for Printing Devices 1.1).
 *
 * The device is identified by VID/PID/serial, never by enumeration order. Bulk OUT carries
 * the job; the class-specific control requests give the IEEE 1284 device ID, the port
 * status byte and a soft reset. Bulk IN (bidirectional printers) is exposed for later
 * back-channel work.
 *
 * Errors: 0 on success; negative libusb error codes; or one of the M2022_USB_E* values.
 * m2022_usb_strerror() explains both.
 */
#ifndef M2022_USB_H
#define M2022_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define M2022_USB_STR_MAX 128

enum {
    M2022_USB_OK = 0,
    M2022_USB_ENOTFOUND = -1000,   /* no printer-class device matches */
    M2022_USB_EAMBIGUOUS = -1001,  /* more than one device matches; give a serial */
    M2022_USB_ENOENDPOINT = -1002, /* no bulk endpoint of the requested direction */
    M2022_USB_EINVAL = -1003,
    M2022_USB_ETIMEOUT = -7, /* libusb's LIBUSB_ERROR_TIMEOUT, passed through by read/write */
};

/* GET_PORT_STATUS bits. */
enum {
    M2022_USB_STATUS_PAPER_EMPTY = 0x20,
    M2022_USB_STATUS_SELECTED = 0x10,
    M2022_USB_STATUS_NO_ERROR = 0x08,
};

typedef struct {
    uint16_t vid, pid, bcd_device;
    uint8_t bus, address;
    uint8_t interface_number, alt_setting;
    uint8_t protocol;      /* 1 unidirectional, 2 bidirectional, 3 IEEE 1284.4 */
    uint8_t ep_out, ep_in; /* bulk endpoint addresses; ep_in is 0 when absent */
    uint16_t ep_out_max_packet;
    char manufacturer[M2022_USB_STR_MAX];
    char product[M2022_USB_STR_MAX];
    char serial[M2022_USB_STR_MAX];
} m2022_usb_info_t;

typedef struct {
    uint16_t vid, pid;  /* 0 = any */
    const char *serial; /* NULL or "" = any */
} m2022_usb_match_t;

typedef struct m2022_usb_device m2022_usb_device_t;

/* Enumerate printer-class devices (class 7, subclass 1, with a bulk OUT endpoint). */
int m2022_usb_list(m2022_usb_info_t *out, size_t max, size_t *count);

/* Open the single device matching `match` (NULL = any) and claim its printer interface. */
int m2022_usb_open(const m2022_usb_match_t *match, m2022_usb_device_t **dev);
void m2022_usb_close(m2022_usb_device_t *dev);
const m2022_usb_info_t *m2022_usb_info(const m2022_usb_device_t *dev);

/* Bulk transfers. *written / *nread are set even on error. */
int m2022_usb_write(m2022_usb_device_t *dev, const void *buf, size_t len, unsigned timeout_ms,
                    size_t *written);
int m2022_usb_read(m2022_usb_device_t *dev, void *buf, size_t len, unsigned timeout_ms,
                   size_t *nread);

/* Class requests. */
int m2022_usb_get_device_id(m2022_usb_device_t *dev, char *buf, size_t buflen);
int m2022_usb_get_port_status(m2022_usb_device_t *dev, uint8_t *status);
int m2022_usb_soft_reset(m2022_usb_device_t *dev);

const char *m2022_usb_strerror(int err);

/* ---- pure helpers, no hardware ------------------------------------------------------ */

typedef struct {
    const char *key;
    size_t key_len;
    const char *value;
    size_t value_len;
} m2022_ieee1284_field_t;

/* Split "KEY:value;KEY:value;" into fields (pointers into `id`). Returns the number of
 * fields present, which may exceed `max`; whitespace around keys and values is trimmed. */
size_t m2022_ieee1284_parse(const char *id, m2022_ieee1284_field_t *fields, size_t max);

/* Look up a key case-insensitively, honouring the standard aliases (MFG/MANUFACTURER,
 * MDL/MODEL, CMD/COMMAND SET, SN/SERN/SERIALNUMBER, CLS/CLASS, DES/DESCRIPTION). */
bool m2022_ieee1284_get(const char *id, const char *key, char *out, size_t outlen);

/* Map a port status byte to IPP-style reason keywords ("media-empty", "offline", "other"). */
size_t m2022_usb_status_reasons(uint8_t status, const char **reasons, size_t max);

#endif /* M2022_USB_H */

#include "m2022/usb.h"

#include <libusb.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK_BYTES (64u * 1024u)
#define CTRL_TIMEOUT_MS 5000u

/* Printer class requests (USB printer class 1.1, section 4.2). */
#define REQ_GET_DEVICE_ID 0
#define REQ_GET_PORT_STATUS 1
#define REQ_SOFT_RESET 2

struct m2022_usb_device {
    libusb_context *ctx;
    libusb_device_handle *handle;
    m2022_usb_info_t info;
    unsigned char *xfer; /* CHUNK_BYTES staging buffer: libusb wants a non-const pointer */
};

static void copy_string(libusb_device_handle *h, uint8_t index, char *dst, size_t dstlen)
{
    unsigned char tmp[M2022_USB_STR_MAX];
    int n;

    dst[0] = '\0';
    if (h == NULL || index == 0) {
        return;
    }
    n = libusb_get_string_descriptor_ascii(h, index, tmp, (int)sizeof tmp);
    if (n <= 0) {
        return;
    }
    if ((size_t)n > dstlen - 1) {
        n = (int)(dstlen - 1);
    }
    memcpy(dst, tmp, (size_t)n);
    dst[n] = '\0';
}

/* Fill `info` if `dev` exposes a printer-class interface with a bulk OUT endpoint. */
static int describe(libusb_device *dev, const struct libusb_device_descriptor *dd,
                    m2022_usb_info_t *info)
{
    struct libusb_config_descriptor *cfg = NULL;
    int rc = libusb_get_active_config_descriptor(dev, &cfg);
    int found = M2022_USB_ENOENDPOINT;

    if (rc < 0) {
        rc = libusb_get_config_descriptor(dev, 0, &cfg);
    }
    if (rc < 0) {
        return rc;
    }

    for (uint8_t i = 0; i < cfg->bNumInterfaces && found != 0; i++) {
        const struct libusb_interface *itf = &cfg->interface[i];
        for (int a = 0; a < itf->num_altsetting && found != 0; a++) {
            const struct libusb_interface_descriptor *id = &itf->altsetting[a];
            if (id->bInterfaceClass != LIBUSB_CLASS_PRINTER || id->bInterfaceSubClass != 1) {
                continue;
            }
            memset(info, 0, sizeof *info);
            info->vid = dd->idVendor;
            info->pid = dd->idProduct;
            info->bcd_device = dd->bcdDevice;
            info->bus = libusb_get_bus_number(dev);
            info->address = libusb_get_device_address(dev);
            info->interface_number = id->bInterfaceNumber;
            info->alt_setting = id->bAlternateSetting;
            info->protocol = id->bInterfaceProtocol;
            for (uint8_t e = 0; e < id->bNumEndpoints; e++) {
                const struct libusb_endpoint_descriptor *ep = &id->endpoint[e];
                if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK) {
                    continue;
                }
                if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT) {
                    if (info->ep_out == 0) {
                        info->ep_out = ep->bEndpointAddress;
                        info->ep_out_max_packet = ep->wMaxPacketSize;
                    }
                } else if (info->ep_in == 0) {
                    info->ep_in = ep->bEndpointAddress;
                }
            }
            if (info->ep_out != 0) {
                found = 0;
            }
        }
    }
    libusb_free_config_descriptor(cfg);
    return found;
}

static bool matches(const m2022_usb_match_t *m, const m2022_usb_info_t *info)
{
    if (m == NULL) {
        return true;
    }
    if (m->vid != 0 && m->vid != info->vid) {
        return false;
    }
    if (m->pid != 0 && m->pid != info->pid) {
        return false;
    }
    if (m->serial != NULL && m->serial[0] != '\0' && strcmp(m->serial, info->serial) != 0) {
        return false;
    }
    return true;
}

/*
 * Walk all devices. Matching printer-class devices are copied to `out` (up to `max`) and
 * counted in *count. When `opened` is non-NULL the first match is left open in *opened.
 */
static int enumerate(libusb_context *ctx, const m2022_usb_match_t *m, m2022_usb_info_t *out,
                     size_t max, size_t *count, libusb_device_handle **opened,
                     m2022_usb_info_t *opened_info)
{
    libusb_device **list = NULL;
    ssize_t n = libusb_get_device_list(ctx, &list);
    size_t found = 0;

    if (n < 0) {
        return (int)n;
    }
    for (ssize_t i = 0; i < n; i++) {
        struct libusb_device_descriptor dd;
        m2022_usb_info_t info;
        libusb_device_handle *h = NULL;

        if (libusb_get_device_descriptor(list[i], &dd) < 0 || describe(list[i], &dd, &info) != 0) {
            continue;
        }
        if (libusb_open(list[i], &h) == 0) {
            copy_string(h, dd.iManufacturer, info.manufacturer, sizeof info.manufacturer);
            copy_string(h, dd.iProduct, info.product, sizeof info.product);
            copy_string(h, dd.iSerialNumber, info.serial, sizeof info.serial);
        }
        if (!matches(m, &info)) {
            if (h != NULL) {
                libusb_close(h);
            }
            continue;
        }
        if (out != NULL && found < max) {
            out[found] = info;
        }
        found++;
        if (opened != NULL && *opened == NULL && h != NULL) {
            *opened = h;
            *opened_info = info;
            h = NULL;
        }
        if (h != NULL) {
            libusb_close(h);
        }
    }
    libusb_free_device_list(list, 1);
    if (count != NULL) {
        *count = found;
    }
    return 0;
}

int m2022_usb_list(m2022_usb_info_t *out, size_t max, size_t *count)
{
    libusb_context *ctx = NULL;
    int rc = libusb_init(&ctx);

    if (rc != 0) {
        return rc;
    }
    rc = enumerate(ctx, NULL, out, max, count, NULL, NULL);
    libusb_exit(ctx);
    return rc;
}

int m2022_usb_open(const m2022_usb_match_t *match, m2022_usb_device_t **dev)
{
    libusb_context *ctx = NULL;
    libusb_device_handle *h = NULL;
    m2022_usb_info_t info;
    size_t count = 0;
    int rc;

    *dev = NULL;
    memset(&info, 0, sizeof info);
    rc = libusb_init(&ctx);
    if (rc != 0) {
        return rc;
    }
    rc = enumerate(ctx, match, NULL, 0, &count, &h, &info);
    if (rc == 0 && count == 0) {
        rc = M2022_USB_ENOTFOUND;
    } else if (rc == 0 && count > 1) {
        rc = M2022_USB_EAMBIGUOUS;
    } else if (rc == 0 && h == NULL) {
        rc = LIBUSB_ERROR_ACCESS;
    }
    if (rc == 0) {
        rc = libusb_claim_interface(h, info.interface_number);
    }
    if (rc == 0 && info.alt_setting != 0) {
        rc = libusb_set_interface_alt_setting(h, info.interface_number, info.alt_setting);
    }
    if (rc == 0) {
        m2022_usb_device_t *d = calloc(1, sizeof *d);
        if (d == NULL || (d->xfer = malloc(CHUNK_BYTES)) == NULL) {
            free(d);
            rc = LIBUSB_ERROR_NO_MEM;
        } else {
            d->ctx = ctx;
            d->handle = h;
            d->info = info;
            *dev = d;
            return 0;
        }
    }
    if (h != NULL) {
        libusb_close(h);
    }
    libusb_exit(ctx);
    return rc;
}

void m2022_usb_close(m2022_usb_device_t *dev)
{
    if (dev == NULL) {
        return;
    }
    libusb_release_interface(dev->handle, dev->info.interface_number);
    libusb_close(dev->handle);
    libusb_exit(dev->ctx);
    free(dev->xfer);
    free(dev);
}

const m2022_usb_info_t *m2022_usb_info(const m2022_usb_device_t *dev)
{
    return &dev->info;
}

int m2022_usb_write(m2022_usb_device_t *dev, const void *buf, size_t len, unsigned timeout_ms,
                    size_t *written)
{
    const unsigned char *p = buf;
    size_t done = 0;
    int rc = 0;

    while (done < len) {
        size_t want = len - done;
        int transferred = 0;
        if (want > CHUNK_BYTES) {
            want = CHUNK_BYTES;
        }
        memcpy(dev->xfer, p + done, want);
        rc = libusb_bulk_transfer(dev->handle, dev->info.ep_out, dev->xfer, (int)want,
                                  &transferred, timeout_ms);
        if (transferred > 0) {
            done += (size_t)transferred;
        }
        if (rc != 0) {
            break;
        }
    }
    if (written != NULL) {
        *written = done;
    }
    return rc;
}

int m2022_usb_read(m2022_usb_device_t *dev, void *buf, size_t len, unsigned timeout_ms,
                   size_t *nread)
{
    int transferred = 0;
    int rc;

    if (nread != NULL) {
        *nread = 0;
    }
    if (dev->info.ep_in == 0) {
        return M2022_USB_ENOENDPOINT;
    }
    if (len > CHUNK_BYTES) {
        len = CHUNK_BYTES;
    }
    rc = libusb_bulk_transfer(dev->handle, dev->info.ep_in, buf, (int)len, &transferred,
                              timeout_ms);
    if (nread != NULL && transferred > 0) {
        *nread = (size_t)transferred;
    }
    return rc;
}

static uint8_t class_in(void)
{
    return (uint8_t)((unsigned)LIBUSB_ENDPOINT_IN | (unsigned)LIBUSB_REQUEST_TYPE_CLASS |
                     (unsigned)LIBUSB_RECIPIENT_INTERFACE);
}

static uint8_t class_out(void)
{
    return (uint8_t)((unsigned)LIBUSB_ENDPOINT_OUT | (unsigned)LIBUSB_REQUEST_TYPE_CLASS |
                     (unsigned)LIBUSB_RECIPIENT_INTERFACE);
}

int m2022_usb_get_device_id(m2022_usb_device_t *dev, char *buf, size_t buflen)
{
    unsigned char data[1024];
    uint16_t windex = (uint16_t)(((unsigned)dev->info.interface_number << 8) |
                                 dev->info.alt_setting);
    int n = libusb_control_transfer(dev->handle, class_in(), REQ_GET_DEVICE_ID, 0, windex, data,
                                    (uint16_t)sizeof data, CTRL_TIMEOUT_MS);
    size_t len;

    if (buflen == 0) {
        return M2022_USB_EINVAL;
    }
    buf[0] = '\0';
    if (n < 0) {
        return n;
    }
    if (n < 2) {
        return 0;
    }
    /* Two-byte big-endian length that includes itself; tolerate devices that get it wrong. */
    len = ((size_t)data[0] << 8) | data[1];
    if (len < 2 || len > (size_t)n) {
        len = (size_t)n;
    }
    len -= 2;
    if (len > buflen - 1) {
        len = buflen - 1;
    }
    memcpy(buf, data + 2, len);
    buf[len] = '\0';
    return 0;
}

int m2022_usb_get_port_status(m2022_usb_device_t *dev, uint8_t *status)
{
    unsigned char data[1] = {0};
    int n = libusb_control_transfer(dev->handle, class_in(), REQ_GET_PORT_STATUS, 0,
                                    dev->info.interface_number, data, 1, CTRL_TIMEOUT_MS);
    if (n < 0) {
        return n;
    }
    if (n != 1) {
        return LIBUSB_ERROR_IO;
    }
    *status = data[0];
    return 0;
}

int m2022_usb_soft_reset(m2022_usb_device_t *dev)
{
    int n = libusb_control_transfer(dev->handle, class_out(), REQ_SOFT_RESET, 0,
                                    dev->info.interface_number, NULL, 0, CTRL_TIMEOUT_MS);
    return n < 0 ? n : 0;
}

const char *m2022_usb_strerror(int err)
{
    switch (err) {
    case M2022_USB_OK:
        return "success";
    case M2022_USB_ENOTFOUND:
        return "no matching USB printer found";
    case M2022_USB_EAMBIGUOUS:
        return "more than one USB printer matches; select one with --serial";
    case M2022_USB_ENOENDPOINT:
        return "device has no bulk endpoint in that direction";
    case M2022_USB_EINVAL:
        return "invalid argument";
    default:
        return libusb_strerror(err);
    }
}

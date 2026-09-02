/*
 * PAPPL device scheme "m2022usb://VVVV:PPPP[?serial=S]" backed by src/usb/ (libusb, ADR-009).
 * PAPPL opens the device for each job and, between jobs, for status; both go through here.
 *
 * Writes block while the printer's buffer is full (the bulk pipe stalls), so the write
 * timeout is generous; a page of the engine's 20 ppm takes 3 s. Reads are short polls.
 */
#include "usbdev.h"
#include "m2022/usb.h"

#include <pappl/pappl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WRITE_TIMEOUT_MS 60000
#define READ_TIMEOUT_MS 250

static void make_uri(const m2022_usb_info_t *info, char *uri, size_t size)
{
    snprintf(uri, size, "%s://%04x:%04x?serial=%s", M2022_USBDEV_SCHEME, info->vid, info->pid,
             info->serial);
}

/* "m2022usb://04e8:3321?serial=ZF45B8GF3C01YSD"; the vid:pid and the serial are optional. */
static bool parse_uri(const char *uri, m2022_usb_match_t *match, char *serial, size_t serial_size)
{
    const char *p = strstr(uri, "://"), *q;
    unsigned vid = 0, pid = 0;

    memset(match, 0, sizeof *match);
    serial[0] = '\0';
    if (p == NULL) {
        return false;
    }
    p += 3;
    if (sscanf(p, "%4x:%4x", &vid, &pid) == 2) {
        match->vid = (uint16_t)vid;
        match->pid = (uint16_t)pid;
    }
    if ((q = strstr(p, "serial=")) != NULL) {
        size_t n = strcspn(q + 7, "&/");
        if (n >= serial_size) {
            n = serial_size - 1;
        }
        memcpy(serial, q + 7, n);
        serial[n] = '\0';
        match->serial = serial;
    }
    return true;
}

static bool usbdev_list(pappl_device_cb_t cb, void *data, pappl_deverror_cb_t err_cb,
                        void *err_data)
{
    m2022_usb_info_t infos[8];
    size_t count = 0;
    int rc = m2022_usb_list(infos, sizeof infos / sizeof infos[0], &count);

    if (rc != 0) {
        if (err_cb != NULL) {
            err_cb(m2022_usb_strerror(rc), err_data);
        }
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        char uri[256], info[256], id[512];
        make_uri(&infos[i], uri, sizeof uri);
        snprintf(info, sizeof info, "%s %s (USB, serial %s)", infos[i].manufacturer,
                 infos[i].product, infos[i].serial);
        snprintf(id, sizeof id, "MFG:%s;MDL:%s;SN:%s;CLS:PRINTER;", infos[i].manufacturer,
                 infos[i].product, infos[i].serial);
        if (cb(info, uri, id, data)) {
            return true;
        }
    }
    return false;
}

static bool usbdev_open(pappl_device_t *device, const char *device_uri, const char *name)
{
    m2022_usb_match_t match;
    char serial[128];
    m2022_usb_device_t *dev = NULL;
    int rc;

    (void)name;
    if (!parse_uri(device_uri, &match, serial, sizeof serial)) {
        papplDeviceError(device, "bad device URI '%s'", device_uri);
        return false;
    }
    rc = m2022_usb_open(&match, &dev);
    if (rc != 0) {
        papplDeviceError(device, "cannot open %s: %s", device_uri, m2022_usb_strerror(rc));
        return false;
    }
    papplDeviceSetData(device, dev);
    return true;
}

static void usbdev_close(pappl_device_t *device)
{
    m2022_usb_close(papplDeviceGetData(device));
    papplDeviceSetData(device, NULL);
}

static ssize_t usbdev_read(pappl_device_t *device, void *buffer, size_t bytes)
{
    m2022_usb_device_t *dev = papplDeviceGetData(device);
    size_t nread = 0;
    int rc = m2022_usb_read(dev, buffer, bytes, READ_TIMEOUT_MS, &nread);

    if (rc != 0 && nread == 0 && rc != M2022_USB_ETIMEOUT) {
        papplDeviceError(device, "read: %s", m2022_usb_strerror(rc));
        return -1;
    }
    return (ssize_t)nread;
}

static ssize_t usbdev_write(pappl_device_t *device, const void *buffer, size_t bytes)
{
    m2022_usb_device_t *dev = papplDeviceGetData(device);
    const uint8_t *p = buffer;
    size_t left = bytes;

    while (left > 0) {
        size_t written = 0;
        int rc = m2022_usb_write(dev, p, left, WRITE_TIMEOUT_MS, &written);
        p += written;
        left -= written;
        if (rc != 0 && written == 0) {
            papplDeviceError(device, "write: %s", m2022_usb_strerror(rc));
            return -1;
        }
    }
    return (ssize_t)bytes;
}

static pappl_preason_t usbdev_status(pappl_device_t *device)
{
    m2022_usb_device_t *dev = papplDeviceGetData(device);
    uint8_t status = 0;
    pappl_preason_t reasons = PAPPL_PREASON_NONE;

    if (m2022_usb_get_port_status(dev, &status) != 0) {
        return PAPPL_PREASON_OTHER;
    }
    /* IEEE 1284 status byte as the printer class reports it (docs/usb.md) */
    if (status & M2022_USB_STATUS_PAPER_EMPTY) {
        reasons |= PAPPL_PREASON_MEDIA_EMPTY;
    }
    if (!(status & M2022_USB_STATUS_SELECTED)) {
        reasons |= PAPPL_PREASON_OFFLINE;
    }
    if (!(status & M2022_USB_STATUS_NO_ERROR)) {
        reasons |= PAPPL_PREASON_OTHER;
    }
    return reasons;
}

static char *usbdev_id(pappl_device_t *device, char *buffer, size_t bufsize)
{
    m2022_usb_device_t *dev = papplDeviceGetData(device);
    return m2022_usb_get_device_id(dev, buffer, bufsize) == 0 ? buffer : NULL;
}

void m2022_usbdev_register(void)
{
    papplDeviceAddScheme2(M2022_USBDEV_SCHEME, PAPPL_DEVTYPE_USB, usbdev_list, usbdev_open,
                          usbdev_close, usbdev_read, usbdev_write, usbdev_status, NULL,
                          usbdev_id);
}

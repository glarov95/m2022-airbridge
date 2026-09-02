/*
 * send FILE [--vid 0x04e8] [--pid 0x3321] [--serial S] [--timeout MS] [--dry-run]
 *
 * Write a printer-native job (for example a captured vendor .spl) to the USB printer.
 * The first hardware test of the project: a replayed vendor job must print correctly.
 */
#include "cli.h"
#include "m2022/usb.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static unsigned char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf = NULL;
    long size;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)size + 1);
    if (buf == NULL || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)size;
    return buf;
}

static void report_status(m2022_usb_device_t *dev, const char *when)
{
    uint8_t st = 0;
    const char *reasons[4];
    size_t nr;
    int rc = m2022_usb_get_port_status(dev, &st);

    if (rc != 0) {
        fprintf(stderr, "send: port status %s: %s\n", when, m2022_usb_strerror(rc));
        return;
    }
    nr = m2022_usb_status_reasons(st, reasons, 4);
    fprintf(stderr, "send: port status %s: 0x%02x%s", when, st, nr ? " (" : " (ready");
    for (size_t i = 0; i < nr; i++) {
        fprintf(stderr, "%s%s", i ? ", " : "", reasons[i]);
    }
    fprintf(stderr, ")\n");
}

int cmd_send(int argc, char **argv)
{
    const char *path = NULL;
    m2022_usb_match_t match = {0, 0, NULL};
    unsigned timeout_ms = 30000;
    bool dry_run = false;
    unsigned char *data;
    size_t len = 0, written = 0;
    m2022_usb_device_t *dev = NULL;
    const m2022_usb_info_t *info;
    char device_id[1024];
    double t0, t1;
    int rc;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--vid") == 0 && i + 1 < argc) {
            match.vid = (uint16_t)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) {
            match.pid = (uint16_t)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--serial") == 0 && i + 1 < argc) {
            match.serial = argv[++i];
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            timeout_ms = (unsigned)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "send: unknown option '%s'\n", argv[i]);
            return 2;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            fprintf(stderr, "send: only one FILE is accepted\n");
            return 2;
        }
    }
    if (path == NULL) {
        fprintf(stderr, "Usage: m2022-airbridge send FILE [--vid V] [--pid P] [--serial S] "
                        "[--timeout MS] [--dry-run]\n");
        return 2;
    }
    data = read_file(path, &len);
    if (data == NULL) {
        fprintf(stderr, "send: cannot read %s: %s\n", path, strerror(errno));
        return 2;
    }

    rc = m2022_usb_open(&match, &dev);
    if (rc != 0) {
        fprintf(stderr, "send: cannot open printer: %s\n", m2022_usb_strerror(rc));
        free(data);
        return 3;
    }
    info = m2022_usb_info(dev);
    fprintf(stderr, "send: %04x:%04x \"%s\" serial %s, bulk out 0x%02x\n", info->vid, info->pid,
            info->product, info->serial, info->ep_out);
    if (m2022_usb_get_device_id(dev, device_id, sizeof device_id) == 0 && device_id[0] != '\0') {
        fprintf(stderr, "send: device id: %s\n", device_id);
    }
    report_status(dev, "before");

    if (dry_run) {
        fprintf(stderr, "send: dry run, %zu bytes not written\n", len);
        m2022_usb_close(dev);
        free(data);
        return 0;
    }

    t0 = now_seconds();
    rc = m2022_usb_write(dev, data, len, timeout_ms, &written);
    t1 = now_seconds();
    if (rc != 0) {
        fprintf(stderr, "send: write failed after %zu of %zu bytes: %s\n", written, len,
                m2022_usb_strerror(rc));
    } else {
        fprintf(stderr, "send: wrote %zu bytes in %.3f s (%.0f KiB/s)\n", written, t1 - t0,
                (double)written / 1024.0 / (t1 - t0 > 0 ? t1 - t0 : 1e-9));
    }
    report_status(dev, "after");
    m2022_usb_close(dev);
    free(data);
    return rc == 0 ? 0 : 4;
}

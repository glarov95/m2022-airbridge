/*
 * probe [--json] [--quiet]: what this host, its USB printers and its CUPS queues look like.
 * Exit status 0 when a USB printer-class device is present and could be opened, 1 when there
 * is none, 2 when one is present but cannot be opened (permissions, another owner).
 */
#include "cli.h"
#include "m2022/cups.h"
#include "m2022/json.h"
#include "m2022/usb.h"
#include "m2022/version.h"

#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#define MAX_DEVICES 8
#define MAX_QUEUES 32

typedef struct {
    m2022_usb_info_t info;
    char device_id[1024];
    int open_error;   /* 0 when opened and queried */
    int status_error; /* 0 when port status was read */
    uint8_t status;
} probed_printer_t;

static void query(probed_printer_t *p)
{
    m2022_usb_match_t m = {p->info.vid, p->info.pid, p->info.serial};
    m2022_usb_device_t *dev = NULL;

    p->device_id[0] = '\0';
    p->open_error = m2022_usb_open(&m, &dev);
    if (p->open_error != 0) {
        p->status_error = p->open_error;
        return;
    }
    (void)m2022_usb_get_device_id(dev, p->device_id, sizeof p->device_id);
    p->status_error = m2022_usb_get_port_status(dev, &p->status);
    m2022_usb_close(dev);
}

static void os_version(char *buf, size_t len)
{
    buf[0] = '\0';
#ifdef __APPLE__
    size_t l = len;
    if (sysctlbyname("kern.osproductversion", buf, &l, NULL, 0) != 0) {
        buf[0] = '\0';
    }
#else
    (void)len;
#endif
}

static void print_text(const struct utsname *u, const char *osver, const probed_printer_t *p,
                       size_t np, const m2022_cups_queue_t *q, size_t nq)
{
    printf("m2022-airbridge %s probe\n\nHost:\n  %s %s %s%s%s\n", m2022_version_string(),
           u->sysname, u->release, u->machine, osver[0] ? ", OS " : "", osver);
    printf("\nUSB printers: %zu\n", np);
    for (size_t i = 0; i < np; i++) {
        const m2022_usb_info_t *d = &p[i].info;
        const char *reasons[4];
        size_t nr;
        printf("  [%zu] %04x:%04x bus %u addr %u  \"%s\" \"%s\" serial \"%s\"\n", i, d->vid, d->pid,
               d->bus, d->address, d->manufacturer, d->product, d->serial);
        printf("      interface %u alt %u protocol %u (%s), bulk out 0x%02x (%u bytes/packet), "
               "bulk in 0x%02x, bcdDevice %x.%02x\n",
               d->interface_number, d->alt_setting, d->protocol,
               d->protocol == 1 ? "unidirectional" : d->protocol == 2 ? "bidirectional"
                                                                        : "1284.4",
               d->ep_out, d->ep_out_max_packet, d->ep_in, d->bcd_device >> 8,
               d->bcd_device & 0xff);
        if (p[i].open_error != 0) {
            printf("      open: %s\n", m2022_usb_strerror(p[i].open_error));
            continue;
        }
        printf("      device id: %s\n", p[i].device_id[0] ? p[i].device_id : "(none)");
        if (p[i].status_error != 0) {
            printf("      port status: %s\n", m2022_usb_strerror(p[i].status_error));
        } else {
            nr = m2022_usb_status_reasons(p[i].status, reasons, 4);
            printf("      port status: 0x%02x%s", p[i].status, nr ? " (" : " (ready");
            for (size_t r = 0; r < nr; r++) {
                printf("%s%s", r ? ", " : "", reasons[r]);
            }
            printf(")\n");
        }
    }
    printf("\nCUPS queues: %zu\n", nq);
    for (size_t i = 0; i < nq; i++) {
        printf("  %s%s: %s, %s, %s, reasons: %s\n", q[i].name, q[i].is_default ? " (default)" : "",
               q[i].make_model, q[i].device_uri, m2022_cups_state_name(q[i].state),
               q[i].state_reasons);
    }
}

static void print_json(const struct utsname *u, const char *osver, const probed_printer_t *p,
                       size_t np, const m2022_cups_queue_t *q, size_t nq)
{
    printf("{\n  \"version\": ");
    m2022_json_string(stdout, m2022_version_string());
    printf(",\n  \"host\": {\"sysname\": ");
    m2022_json_string(stdout, u->sysname);
    printf(", \"release\": ");
    m2022_json_string(stdout, u->release);
    printf(", \"machine\": ");
    m2022_json_string(stdout, u->machine);
    printf(", \"os_version\": ");
    m2022_json_string(stdout, osver[0] ? osver : NULL);
    printf("},\n  \"usb_printers\": [");
    for (size_t i = 0; i < np; i++) {
        const m2022_usb_info_t *d = &p[i].info;
        const char *reasons[4];
        size_t nr = p[i].status_error == 0 ? m2022_usb_status_reasons(p[i].status, reasons, 4) : 0;
        printf("%s\n    {\"vid\": \"0x%04x\", \"pid\": \"0x%04x\", \"bcd_device\": \"0x%04x\", "
               "\"bus\": %u, \"address\": %u, \"manufacturer\": ",
               i ? "," : "", d->vid, d->pid, d->bcd_device, d->bus, d->address);
        m2022_json_string(stdout, d->manufacturer);
        printf(", \"product\": ");
        m2022_json_string(stdout, d->product);
        printf(", \"serial\": ");
        m2022_json_string(stdout, d->serial);
        printf(",\n     \"interface\": %u, \"alt_setting\": %u, \"protocol\": %u, "
               "\"ep_out\": \"0x%02x\", \"ep_out_max_packet\": %u, \"ep_in\": \"0x%02x\",\n"
               "     \"open_error\": ",
               d->interface_number, d->alt_setting, d->protocol, d->ep_out, d->ep_out_max_packet,
               d->ep_in);
        m2022_json_string(stdout, p[i].open_error ? m2022_usb_strerror(p[i].open_error) : NULL);
        printf(", \"device_id\": ");
        m2022_json_string(stdout, p[i].open_error ? NULL : p[i].device_id);
        if (p[i].status_error == 0) {
            printf(", \"port_status\": \"0x%02x\", \"status_reasons\": [", p[i].status);
            for (size_t r = 0; r < nr; r++) {
                printf("%s", r ? ", " : "");
                m2022_json_string(stdout, reasons[r]);
            }
            printf("]}");
        } else {
            printf(", \"port_status\": null, \"status_reasons\": []}");
        }
    }
    printf("\n  ],\n  \"cups_queues\": [");
    for (size_t i = 0; i < nq; i++) {
        printf("%s\n    {\"name\": ", i ? "," : "");
        m2022_json_string(stdout, q[i].name);
        printf(", \"make_model\": ");
        m2022_json_string(stdout, q[i].make_model);
        printf(", \"device_uri\": ");
        m2022_json_string(stdout, q[i].device_uri);
        printf(", \"state\": ");
        m2022_json_string(stdout, m2022_cups_state_name(q[i].state));
        printf(", \"state_reasons\": ");
        m2022_json_string(stdout, q[i].state_reasons);
        printf(", \"is_default\": %s}", q[i].is_default ? "true" : "false");
    }
    printf("\n  ]\n}\n");
}

int cmd_probe(int argc, char **argv)
{
    bool json = false, quiet = false;
    struct utsname u;
    char osver[64];
    m2022_usb_info_t infos[MAX_DEVICES];
    probed_printer_t printers[MAX_DEVICES];
    m2022_cups_queue_t queues[MAX_QUEUES];
    size_t np = 0, nq;
    int rc;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            json = true;
        } else if (strcmp(argv[i], "--quiet") == 0) {
            quiet = true;
        } else {
            fprintf(stderr, "probe: unknown option '%s'\n", argv[i]);
            return 2;
        }
    }
    memset(&u, 0, sizeof u);
    (void)uname(&u);
    os_version(osver, sizeof osver);

    rc = m2022_usb_list(infos, MAX_DEVICES, &np);
    if (rc != 0) {
        fprintf(stderr, "probe: USB enumeration failed: %s\n", m2022_usb_strerror(rc));
        np = 0;
    }
    if (np > MAX_DEVICES) {
        np = MAX_DEVICES;
    }
    for (size_t i = 0; i < np; i++) {
        memset(&printers[i], 0, sizeof printers[i]);
        printers[i].info = infos[i];
        query(&printers[i]);
    }
    nq = m2022_cups_queues(queues, MAX_QUEUES);
    if (nq > MAX_QUEUES) {
        nq = MAX_QUEUES;
    }
    if (json) {
        print_json(&u, osver, printers, np, queues, nq);
    } else if (!quiet) {
        print_text(&u, osver, printers, np, queues, nq);
    }
    /* 0: a printer is present and could be opened; 1: none; 2: present but not openable
     * (the installer runs this as the service user to prove USB access) */
    if (np == 0) {
        return 1;
    }
    for (size_t i = 0; i < np; i++) {
        if (printers[i].open_error == 0) {
            return 0;
        }
    }
    return 2;
}

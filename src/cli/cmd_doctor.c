/*
 * doctor [--port N] [--name NAME] [--no-service]
 *
 * One line per check, a hint for every failure, exit 0 when everything a working printer
 * needs is in place: binary, service user, launchd job, USB printer, Bonjour advertisement,
 * IPP endpoint, CUPS queue, log. --no-service checks only the network side of a server that
 * was started by hand (what the integration test does).
 */
#include "m2022/cups.h"
#include "m2022/service.h"
#include "m2022/usb.h"
#include "m2022/version.h"

#include "cli.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int fails, warns;
} tally_t;

static void report(tally_t *t, int level, const char *check, const char *detail, const char *hint)
{
    static const char *const LABEL[] = {"ok  ", "WARN", "FAIL"};
    printf("%s %-16s %s", LABEL[level], check, detail);
    if (level > 0 && hint != NULL) {
        printf("\n     -> %s", hint);
    }
    putchar('\n');
    if (level == 2) {
        t->fails++;
    } else if (level == 1) {
        t->warns++;
    }
}

static void check_log(tally_t *t)
{
    FILE *f = fopen(M2022_SERVICE_LOG_FILE, "r");
    char line[1024], last_error[1024] = "";
    int lines = 0;
    if (f == NULL) {
        if (errno == EACCES) {
            report(
                t, 1, "log", M2022_SERVICE_LOG_FILE " is not readable by this user",
                "run `sudo m2022-airbridge install` again (it makes the log readable) or use sudo");
        } else {
            report(t, 1, "log", "no log file yet", "the service writes " M2022_SERVICE_LOG_FILE);
        }
        return;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        lines++;
        if (line[0] == 'E' && line[1] == ' ') {
            snprintf(last_error, sizeof last_error, "%s", line);
        }
    }
    fclose(f);
    if (last_error[0] != '\0') {
        last_error[strcspn(last_error, "\n")] = '\0';
        report(t, 1, "log", last_error, "the last error line; `m2022-airbridge logs` shows more");
    } else {
        char detail[64];
        snprintf(detail, sizeof detail, "%d lines, no errors", lines);
        report(t, 0, "log", detail, NULL);
    }
}

int cmd_doctor(int argc, char **argv)
{
    int port = M2022_SERVICE_PORT;
    const char *name = M2022_SERVICE_PRINTER_NAME;
    bool no_service = false;
    m2022_service_info_t info;
    m2022_ipp_printer_t ipp;
    tally_t t = {0, 0};
    char detail[600];
    int found_port = -1;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            name = argv[++i];
        } else if (strcmp(argv[i], "--no-service") == 0) {
            no_service = true;
        } else {
            fprintf(stderr,
                    "Usage: m2022-airbridge doctor [--port N] [--name NAME] [--no-service]\n");
            return 2;
        }
    }
    m2022_service_inspect(&info);
    printf("m2022-airbridge %s doctor\n", m2022_version_string());

    if (!no_service) {
        if (info.binary_installed) {
            const char *const vargv[] = {M2022_SERVICE_BINARY, "version", NULL};
            char out[128] = "";
            m2022_run(vargv, out, sizeof out);
            out[strcspn(out, "\n")] = '\0';
            if (strstr(out, m2022_version_string()) != NULL) {
                snprintf(detail, sizeof detail, "%s %s", M2022_SERVICE_BINARY,
                         m2022_version_string());
                report(&t, 0, "binary", detail, NULL);
            } else {
                snprintf(detail, sizeof detail, "%s is %s, this is %s", M2022_SERVICE_BINARY, out,
                         m2022_version_string());
                report(&t, 1, "binary", detail,
                       "run `sudo m2022-airbridge install` from the new build");
            }
        } else {
            report(&t, 2, "binary", "not installed at " M2022_SERVICE_BINARY,
                   "run `sudo ./build/src/m2022-airbridge install`");
        }
        if (info.user_exists) {
            snprintf(detail, sizeof detail, "%s (uid %d)", M2022_SERVICE_USER, info.user_uid);
            report(&t, 0, "service user", detail, NULL);
        } else {
            report(&t, 2, "service user", M2022_SERVICE_USER " does not exist",
                   "install creates it");
        }
        if (!info.plist_exists) {
            report(&t, 2, "launchd job", "no " M2022_SERVICE_PLIST, "install writes it");
        } else if (!info.launchd.loaded) {
            report(&t, 2, "launchd job", "written but not loaded",
                   "run `sudo m2022-airbridge start`");
        } else if (!info.launchd.running) {
            report(&t, 2, "launchd job", "loaded but not running",
                   "launchd retries every 5 s; see " M2022_SERVICE_LOG_DIR "/launchd.log");
        } else {
            snprintf(detail, sizeof detail, "running, pid %d", info.launchd.pid);
            report(&t, 0, "launchd job", detail, NULL);
        }
    }

    if (info.usb_printer) {
        report(&t, 0, "usb printer", "Samsung SL-M2022 (04e8:3321) on the bus", NULL);
    } else {
        report(&t, 2, "usb printer", "no SL-M2022 on USB",
               "switch the printer on and check the cable; `m2022-airbridge probe` lists devices");
    }

    if (m2022_dnssd_find(name, 3000, &found_port)) {
        if (found_port == port || found_port < 0) {
            snprintf(detail, sizeof detail, "\"%s\" advertised on port %d", name, found_port);
            report(&t, 0, "bonjour", detail, NULL);
        } else {
            snprintf(detail, sizeof detail, "\"%s\" advertised on port %d, expected %d", name,
                     found_port, port);
            report(&t, 1, "bonjour", detail, "another instance with the same name? check `status`");
        }
    } else {
        snprintf(detail, sizeof detail, "no _ipp._tcp service called \"%s\" within 3 s", name);
        report(&t, 2, "bonjour", detail,
               "the service is not running, or mDNSResponder is blocked; `dns-sd -B _ipp._tcp`");
    }

    if (m2022_ipp_printer_state("localhost", port, "/ipp/print", &ipp) == 0) {
        snprintf(detail, sizeof detail, "%s on localhost:%d: %s%s%s", ipp.name, port,
                 m2022_cups_state_name(ipp.state),
                 ipp.reasons[0] && strcmp(ipp.reasons, "none") != 0 ? ", " : "",
                 ipp.reasons[0] && strcmp(ipp.reasons, "none") != 0 ? ipp.reasons : "");
        report(&t, strstr(ipp.reasons, "offline") != NULL ? 1 : 0, "ipp", detail,
               "the printer is off or not answering; check `usb printer` above");
    } else {
        snprintf(detail, sizeof detail, "no answer on localhost:%d (%s)", port, ipp.reasons);
        report(&t, 2, "ipp", detail, "the service is not running on that port");
    }

    if (!no_service) {
        if (info.our_queue) {
            snprintf(detail, sizeof detail, "%s points at this printer", info.our_queue_name);
            report(&t, 0, "cups queue", detail, NULL);
        } else {
            report(
                &t, 1, "cups queue", "no queue on this Mac points at the printer",
                "System Settings > Printers & Scanners > Add, or `sudo m2022-airbridge install`");
        }
        if (info.vendor_queue) {
            report(&t, 1, "vendor queue", M2022_VENDOR_QUEUE " still exists",
                   info.vendor_driver
                       ? "macOS re-adds it while the Samsung driver is installed: `sudo "
                         "m2022-airbridge remove-vendor-driver` (backs it up first)"
                       : "two USB owners fight (ADR-006); `lpadmin -x " M2022_VENDOR_QUEUE "`");
        }
        check_log(&t);
    }

    printf("%d failure(s), %d warning(s)\n", t.fails, t.warns);
    return t.fails == 0 ? 0 : 1;
}

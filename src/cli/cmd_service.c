/*
 * install   [--dry-run] [--keep-vendor-queue] [--test-page] [--name NAME] [--port N]
 * uninstall [--dry-run] [--purge]
 * start | stop | restart | status | logs [-n N] [-f]
 *
 * The service on this Mac: a launchd daemon running as the hidden user _m2022airbridge
 * (docs/macos-service.md). install and uninstall show every command they run; --dry-run
 * shows them without running anything. Both need sudo.
 */
#include "m2022/cups.h"
#include "m2022/service.h"
#include "m2022/version.h"

#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int need_root(const char *what)
{
    if (geteuid() != 0) {
        fprintf(stderr, "%s changes the system: run it with sudo\n", what);
        return 2;
    }
    return 0;
}

static int run_plan(m2022_plan_t *plan, bool dry_run, const char *what)
{
    int rc = 0;
    if (dry_run) {
        printf("# dry run: nothing below is executed\n");
        m2022_plan_print(plan, stdout);
    } else if ((rc = need_root(what)) == 0) {
        rc = m2022_service_execute(plan, stdout) == 0 ? 0 : 1;
    }
    m2022_plan_free(plan);
    return rc;
}

int cmd_install(int argc, char **argv)
{
    m2022_service_info_t info;
    m2022_service_params_t params = {NULL, 0};
    m2022_plan_options_t opts = {false, false, false};
    m2022_plan_t plan;
    char why[256];
    bool dry_run = false;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = true;
        } else if (strcmp(argv[i], "--keep-vendor-queue") == 0) {
            opts.keep_vendor = true;
        } else if (strcmp(argv[i], "--test-page") == 0) {
            opts.test_page = true;
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            params.printer_name = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            params.port = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Usage: m2022-airbridge install [--dry-run] [--keep-vendor-queue] "
                            "[--test-page] [--name NAME] [--port N]\n");
            return 2;
        }
    }
    m2022_service_inspect(&info);
    if (!m2022_service_install_plan(&info, &params, &opts, &plan, why, sizeof why)) {
        fprintf(stderr, "install: %s\n", why);
        return 2;
    }
    return run_plan(&plan, dry_run, "install");
}

int cmd_uninstall(int argc, char **argv)
{
    m2022_service_info_t info;
    m2022_plan_options_t opts = {false, false, false};
    m2022_plan_t plan;
    char why[256];
    bool dry_run = false;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = true;
        } else if (strcmp(argv[i], "--purge") == 0) {
            opts.purge = true;
        } else {
            fprintf(stderr, "Usage: m2022-airbridge uninstall [--dry-run] [--purge]\n");
            return 2;
        }
    }
    m2022_service_inspect(&info);
    if (!m2022_service_uninstall_plan(&info, &opts, &plan, why, sizeof why)) {
        fprintf(stderr, "uninstall: %s\n", why);
        return 2;
    }
    return run_plan(&plan, dry_run, "uninstall");
}

int cmd_remove_vendor(int argc, char **argv)
{
    m2022_service_info_t info;
    m2022_plan_t plan;
    char why[256];
    bool dry_run = false;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--dry-run") == 0) {
            dry_run = true;
        } else {
            fprintf(stderr, "Usage: m2022-airbridge remove-vendor-driver [--dry-run]\n");
            return 2;
        }
    }
    m2022_service_inspect(&info);
    if (!m2022_service_remove_vendor_plan(&info, &plan, why, sizeof why)) {
        fprintf(stderr, "remove-vendor-driver: %s\n", why);
        return 2;
    }
    return run_plan(&plan, dry_run, "remove-vendor-driver");
}

static int launchctl(const char *a, const char *b, const char *c)
{
    const char *const argv[] = {"launchctl", a, b, c, NULL};
    int rc;
    printf("+ launchctl %s %s%s%s\n", a, b, c ? " " : "", c ? c : "");
    rc = m2022_run(argv, NULL, 0);
    if (rc != 0) {
        fprintf(stderr, "launchctl failed (%d)\n", rc);
    }
    return rc == 0 ? 0 : 1;
}

int cmd_service(const char *verb, int argc, char **argv)
{
    m2022_service_info_t info;
    int rc;

    (void)argc;
    (void)argv;
    if ((rc = need_root(verb)) != 0) {
        return rc;
    }
    m2022_service_inspect(&info);
    if (!info.plist_exists) {
        fprintf(stderr, "%s: not installed (no %s); run `m2022-airbridge install`\n", verb,
                M2022_SERVICE_PLIST);
        return 2;
    }
    if (strcmp(verb, "start") == 0) {
        return info.launchd.loaded ? launchctl("kickstart", "system/" M2022_SERVICE_LABEL, NULL)
                                   : launchctl("bootstrap", "system", M2022_SERVICE_PLIST);
    }
    if (strcmp(verb, "stop") == 0) {
        return info.launchd.loaded ? launchctl("bootout", "system/" M2022_SERVICE_LABEL, NULL) : 0;
    }
    /* restart */
    return info.launchd.loaded ? launchctl("kickstart", "-k", "system/" M2022_SERVICE_LABEL)
                               : launchctl("bootstrap", "system", M2022_SERVICE_PLIST);
}

int cmd_status(int argc, char **argv)
{
    m2022_service_info_t info;
    m2022_ipp_printer_t ipp;
    int port = M2022_SERVICE_PORT, rc;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        }
    }
    m2022_service_inspect(&info);
    printf("service:  %s\n", !info.plist_exists ? "not installed"
                             : !info.launchd.loaded
                                 ? "installed, not loaded (m2022-airbridge start)"
                             : info.launchd.running ? "running"
                                                    : "loaded, not running (launchd will retry)");
    if (info.launchd.pid > 0) {
        printf("pid:      %d\n", info.launchd.pid);
    }
    printf("printer:  %s\n", info.usb_printer ? "Samsung SL-M2022 on USB" : "no SL-M2022 on USB");
    rc = m2022_ipp_printer_state("localhost", port, "/ipp/print", &ipp);
    if (rc == 0) {
        printf("ipp:      %s on port %d, %s%s%s\n", ipp.name, port,
               m2022_cups_state_name(ipp.state),
               ipp.reasons[0] && strcmp(ipp.reasons, "none") != 0 ? ", " : "",
               ipp.reasons[0] && strcmp(ipp.reasons, "none") != 0 ? ipp.reasons : "");
    } else {
        printf("ipp:      no answer on localhost:%d (%s)\n", port, ipp.reasons);
    }
    printf("queue:    %s\n", info.our_queue ? info.our_queue_name : "none on this Mac");
    if (info.vendor_queue) {
        printf("warning:  the Samsung queue %s still exists; do not print to both (ADR-006)\n",
               M2022_VENDOR_QUEUE);
    }
    printf("log:      %s\n", M2022_SERVICE_LOG_FILE);
    /* 0 only when the service runs, answers IPP and the printer is not stopped (5) */
    return info.launchd.running && rc == 0 && ipp.state != 5 ? 0 : 1;
}

int cmd_logs(int argc, char **argv)
{
    const char *lines = "100";
    bool follow = false;
    const char *tail_argv[8];
    size_t n = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            lines = argv[++i];
        } else if (strcmp(argv[i], "-f") == 0) {
            follow = true;
        } else {
            fprintf(stderr, "Usage: m2022-airbridge logs [-n N] [-f]\n");
            return 2;
        }
    }
    if (access(M2022_SERVICE_LOG_FILE, R_OK) != 0) {
        fprintf(stderr, "logs: cannot read %s (not installed, or run with sudo)\n",
                M2022_SERVICE_LOG_FILE);
        return 2;
    }
    tail_argv[n++] = "tail";
    tail_argv[n++] = "-n";
    tail_argv[n++] = lines;
    if (follow) {
        tail_argv[n++] = "-f";
    }
    tail_argv[n++] = M2022_SERVICE_LOG_FILE;
    tail_argv[n] = NULL;
    execvp("tail", (char *const *)(uintptr_t)tail_argv);
    perror("tail");
    return 2;
}

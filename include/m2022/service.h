/*
 * The macOS service adapter (SPEC.md 6.7-6.9, ADR-011): where the installed pieces live, the
 * launchd property list, the install and uninstall plans, and the helpers that look at the
 * system (launchctl, users, queues, DNS-SD). Plans are data, so tests can check them and
 * `install --dry-run` can print them; only m2022_service_execute() touches the machine.
 */
#ifndef M2022_SERVICE_H
#define M2022_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define M2022_SERVICE_LABEL "com.m2022airbridge.daemon"
#define M2022_SERVICE_USER "_m2022airbridge"
#define M2022_SERVICE_BINARY "/usr/local/bin/m2022-airbridge"
#define M2022_SERVICE_PLIST "/Library/LaunchDaemons/" M2022_SERVICE_LABEL ".plist"
#define M2022_SERVICE_SUPPORT_DIR "/Library/Application Support/M2022AirBridge"
#define M2022_SERVICE_BACKUP_DIR M2022_SERVICE_SUPPORT_DIR "/backup"
#define M2022_SERVICE_STATE_FILE M2022_SERVICE_SUPPORT_DIR "/state.conf"
#define M2022_SERVICE_QUEUE_MARKER M2022_SERVICE_SUPPORT_DIR "/queue-created-by-installer"
#define M2022_SERVICE_SPOOL_DIR "/var/spool/m2022-airbridge"
#define M2022_SERVICE_LOG_DIR "/Library/Logs/M2022AirBridge"
#define M2022_SERVICE_LOG_FILE M2022_SERVICE_LOG_DIR "/m2022-airbridge.log"
#define M2022_SERVICE_NEWSYSLOG "/etc/newsyslog.d/" M2022_SERVICE_LABEL ".conf"
#define M2022_SERVICE_PORT 8000
#define M2022_SERVICE_PRINTER_NAME "Samsung M2022"
#define M2022_SERVICE_QUEUE "Samsung_M2022"
#define M2022_VENDOR_QUEUE "Samsung_M2020_Series"
#define M2022_VENDOR_PPD "/etc/cups/ppd/" M2022_VENDOR_QUEUE ".ppd"
#define M2022_VENDOR_DRIVER_DIR "/Library/Printers/Samsung"
#define M2022_VENDOR_PPD_DIR "/Library/Printers/PPDs/Contents/Resources"
#define M2022_VENDOR_CACHE_DIR "/Library/Caches/com.sec.printer"
#define M2022_VENDOR_RECEIPT "com.samsung.PrinterDriverInstaller.pkg"
#define M2022_VENDOR_BACKUP_TAR M2022_SERVICE_BACKUP_DIR "/vendor-driver.tar.gz"
#define M2022_SERVICE_UID_MIN 300 /* hidden system accounts live below 500 */
#define M2022_SERVICE_UID_MAX 499

/* ---- pure ------------------------------------------------------------------------------ */

typedef struct {
    const char *printer_name; /* NULL = M2022_SERVICE_PRINTER_NAME */
    int port;                 /* 0 = M2022_SERVICE_PORT */
} m2022_service_params_t;

/* The launchd property list for the daemon. */
void m2022_service_plist(const m2022_service_params_t *p, char *buf, size_t size);

/* The newsyslog(8) rotation entry for the log file. */
void m2022_service_newsyslog(char *buf, size_t size);

/* Parse `launchctl print system/<label>` output. */
typedef struct {
    bool loaded;  /* launchctl knew the service */
    bool running; /* state = running */
    int pid;      /* 0 when not running */
} m2022_launchd_state_t;
void m2022_service_parse_launchctl(const char *text, m2022_launchd_state_t *st);

/* First free UID in the hidden range given the UIDs in use (sorted or not); 0 if none. */
int m2022_service_pick_uid(const int *used, size_t n);

/* ---- what the machine looks like ------------------------------------------------------ */

typedef struct {
    bool root;        /* running as root */
    bool user_exists; /* the service user */
    int user_uid, user_gid;
    int free_uid;          /* for creating the user */
    bool binary_installed; /* M2022_SERVICE_BINARY exists */
    bool plist_exists;
    m2022_launchd_state_t launchd;
    bool usb_printer;   /* an SL-M2022 is on the bus */
    bool vendor_queue;  /* the Samsung CUPS queue exists */
    bool vendor_ppd;    /* its PPD is on disk */
    bool vendor_driver; /* the Samsung driver package is installed (M2022_VENDOR_DRIVER_DIR) */
    bool our_queue;     /* a CUPS queue points at our printer */
    char our_queue_name[128];
    bool backup_exists; /* a vendor queue backup is in the support directory */
    bool queue_marker;  /* the installer created the CUPS queue */
    bool support_dir, spool_dir, log_dir, state_file, log_file;
    char self_path[1024]; /* this executable */
} m2022_service_info_t;

void m2022_service_inspect(m2022_service_info_t *info);

/* ---- plans ----------------------------------------------------------------------------- */

typedef enum {
    M2022_STEP_RUN,      /* argv */
    M2022_STEP_WRITE,    /* write `content` to `path` with `mode` */
    M2022_STEP_MKDIR,    /* create `path` with `mode` (parents too), owner `owner` */
    M2022_STEP_COPY,     /* copy `src` to `path`, mode `mode` */
    M2022_STEP_REMOVE,   /* remove `path` (file or empty directory tree with --purge) */
    M2022_STEP_WAIT_IPP, /* wait until the IPP endpoint on `port` answers */
    M2022_STEP_NOTE,     /* print `text` */
} m2022_step_kind_t;

#define M2022_STEP_MAX_ARGS 16

typedef struct {
    m2022_step_kind_t kind;
    char text[160];                        /* what this step does, one line */
    const char *argv[M2022_STEP_MAX_ARGS]; /* RUN: NULL-terminated */
    char argbuf[1024];                     /* storage for argv strings */
    char path[512];
    char src[1024];
    char owner[64]; /* "user:group" or "" */
    unsigned mode;
    char *content; /* WRITE: malloc'd */
    int port;
    bool ignore_failure;
    int retries; /* RUN: extra attempts one second apart before it counts as failed */
} m2022_step_t;

#define M2022_PLAN_MAX 48

typedef struct {
    m2022_step_t steps[M2022_PLAN_MAX];
    size_t count;
} m2022_plan_t;

typedef struct {
    bool purge;       /* uninstall: remove data, logs and the service user too */
    bool keep_vendor; /* install: leave the Samsung queue alone (two USB owners, ADR-006) */
    bool test_page;   /* install: print a test page at the end */
} m2022_plan_options_t;

/* Build the plans from an inspection. Return false with `why` when installing is impossible. */
bool m2022_service_install_plan(const m2022_service_info_t *info, const m2022_service_params_t *p,
                                const m2022_plan_options_t *o, m2022_plan_t *plan, char *why,
                                size_t why_size);
bool m2022_service_uninstall_plan(const m2022_service_info_t *info, const m2022_plan_options_t *o,
                                  m2022_plan_t *plan, char *why, size_t why_size);
/* Back up and remove the Samsung driver package, which macOS otherwise uses to re-create the
 * vendor queue whenever the printer appears on USB (docs/macos-service.md). */
bool m2022_service_remove_vendor_plan(const m2022_service_info_t *info, m2022_plan_t *plan,
                                      char *why, size_t why_size);
void m2022_plan_free(m2022_plan_t *plan);

/* Print the plan as shell-like lines ("+ launchctl ..."). */
void m2022_plan_print(const m2022_plan_t *plan, FILE *out);

/* Run the plan, printing each step; stops at the first failure. Returns 0 or the step index + 1. */
int m2022_service_execute(const m2022_plan_t *plan, FILE *out);

/* ---- system helpers used by the commands and doctor ----------------------------------- */

/* Run argv, capture stdout into buf (may be NULL); returns the exit status or -1. */
int m2022_run(const char *const argv[], char *buf, size_t size);

/* Browse DNS-SD for an _ipp._tcp instance called `name` for up to `timeout_ms`. */
bool m2022_dnssd_find(const char *name, unsigned timeout_ms, int *port);

#endif /* M2022_SERVICE_H */

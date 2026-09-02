/*
 * macOS service adapter: launchd, the hidden service user, CUPS queue handling and the
 * install/uninstall plans (docs/macos-service.md, ADR-011). Everything that touches the
 * machine goes through m2022_service_execute(), so `--dry-run` can show the exact commands.
 */
#include "m2022/service.h"

#include "m2022/cups.h"
#include "m2022/usb.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <netinet/in.h>
#include <pwd.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

extern char **environ;

/* ---- pure ------------------------------------------------------------------------------ */

static void xml_escape(const char *s, char *out, size_t size)
{
    size_t n = 0;
    for (; *s != '\0' && n + 6 < size; s++) {
        const char *rep = *s == '&' ? "&amp;" : *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : NULL;
        if (rep != NULL) {
            n += (size_t)snprintf(out + n, size - n, "%s", rep);
        } else {
            out[n++] = *s;
        }
    }
    out[n] = '\0';
}

void m2022_service_plist(const m2022_service_params_t *p, char *buf, size_t size)
{
    char name[256];
    int port = p != NULL && p->port > 0 ? p->port : M2022_SERVICE_PORT;

    xml_escape(p != NULL && p->printer_name != NULL ? p->printer_name : M2022_SERVICE_PRINTER_NAME,
               name, sizeof name);
    snprintf(
        buf, size,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n"
        "<dict>\n"
        "\t<key>Label</key>\n\t<string>" M2022_SERVICE_LABEL "</string>\n"
        "\t<key>ProgramArguments</key>\n"
        "\t<array>\n"
        "\t\t<string>" M2022_SERVICE_BINARY "</string>\n"
        "\t\t<string>server</string>\n"
        "\t\t<string>--spool</string>\n\t\t<string>" M2022_SERVICE_SPOOL_DIR "</string>\n"
        "\t\t<string>--log</string>\n\t\t<string>" M2022_SERVICE_LOG_FILE "</string>\n"
        "\t\t<string>--state</string>\n\t\t<string>" M2022_SERVICE_STATE_FILE "</string>\n"
        "\t\t<string>--port</string>\n\t\t<string>%d</string>\n"
        "\t\t<string>--name</string>\n\t\t<string>%s</string>\n"
        "\t</array>\n"
        "\t<key>UserName</key>\n\t<string>" M2022_SERVICE_USER "</string>\n"
        "\t<key>GroupName</key>\n\t<string>" M2022_SERVICE_USER "</string>\n"
        "\t<key>RunAtLoad</key>\n\t<true/>\n"
        "\t<key>KeepAlive</key>\n\t<true/>\n"
        "\t<key>ThrottleInterval</key>\n\t<integer>5</integer>\n"
        "\t<key>ProcessType</key>\n\t<string>Background</string>\n"
        "\t<key>EnvironmentVariables</key>\n"
        "\t<dict>\n\t\t<key>HOME</key>\n\t\t<string>" M2022_SERVICE_SUPPORT_DIR "</string>\n"
        "\t</dict>\n"
        "\t<key>StandardOutPath</key>\n\t<string>" M2022_SERVICE_LOG_DIR "/launchd.log</string>\n"
        "\t<key>StandardErrorPath</key>\n\t<string>" M2022_SERVICE_LOG_DIR "/launchd.log</string>\n"
        "</dict>\n"
        "</plist>\n",
        port, name);
}

void m2022_service_newsyslog(char *buf, size_t size)
{
    /* logfilename owner:group mode count size(KB) when flags: rotate at 5 MB, keep 5, bzip2 */
    snprintf(buf, size, "%s\t%s:%s\t644\t5\t5000\t*\tJ\n", M2022_SERVICE_LOG_FILE,
             M2022_SERVICE_USER, M2022_SERVICE_USER);
}

void m2022_service_parse_launchctl(const char *text, m2022_launchd_state_t *st)
{
    const char *p;

    memset(st, 0, sizeof *st);
    if (text == NULL || (p = strstr(text, "state = ")) == NULL) {
        return;
    }
    st->loaded = true;
    st->running = strncmp(p + 8, "running", 7) == 0;
    if ((p = strstr(text, "pid = ")) != NULL) {
        st->pid = atoi(p + 6);
    }
    if (!st->running) {
        st->pid = 0;
    }
}

int m2022_service_pick_uid(const int *used, size_t n)
{
    for (int uid = M2022_SERVICE_UID_MIN; uid <= M2022_SERVICE_UID_MAX; uid++) {
        bool taken = false;
        for (size_t i = 0; i < n && !taken; i++) {
            taken = used[i] == uid;
        }
        if (!taken) {
            return uid;
        }
    }
    return 0;
}

/* ---- running things -------------------------------------------------------------------- */

int m2022_run(const char *const argv[], char *buf, size_t size)
{
    posix_spawn_file_actions_t fa;
    pid_t pid;
    int fds[2] = {-1, -1}, status = 0, rc;

    if (buf != NULL) {
        buf[0] = '\0';
        if (pipe(fds) != 0) {
            return -1;
        }
    }
    posix_spawn_file_actions_init(&fa);
    if (buf != NULL) { /* a query: its stderr is noise ("Could not find service ...") */
        posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
        posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addclose(&fa, fds[0]);
        posix_spawn_file_actions_addclose(&fa, fds[1]);
    }
    rc = posix_spawnp(&pid, argv[0], &fa, NULL, (char *const *)(uintptr_t)argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (buf != NULL) {
        close(fds[1]);
    }
    if (rc != 0) {
        if (buf != NULL) {
            close(fds[0]);
        }
        return -1;
    }
    if (buf != NULL) {
        size_t n = 0;
        ssize_t got;
        while (n + 1 < size && (got = read(fds[0], buf + n, size - 1 - n)) > 0) {
            n += (size_t)got;
        }
        buf[n] = '\0';
        close(fds[0]);
    }
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* ---- inspection ------------------------------------------------------------------------ */

static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

void m2022_service_inspect(m2022_service_info_t *info)
{
    struct passwd *pw;
    int used[256];
    size_t n_used = 0;
    char out[4096];
    const char *const print_argv[] = {"launchctl", "print", "system/" M2022_SERVICE_LABEL, NULL};
    m2022_usb_info_t devs[8];
    size_t ndev = 0;
    m2022_cups_queue_t queues[32];
    size_t nq;
    char needle_dnssd[256], needle_local[64];

    memset(info, 0, sizeof *info);
    info->root = geteuid() == 0;
    if ((pw = getpwnam(M2022_SERVICE_USER)) != NULL) {
        info->user_exists = true;
        info->user_uid = (int)pw->pw_uid;
        info->user_gid = (int)pw->pw_gid;
    }
    setpwent();
    while ((pw = getpwent()) != NULL && n_used < sizeof used / sizeof used[0]) {
        if (pw->pw_uid >= M2022_SERVICE_UID_MIN && pw->pw_uid <= M2022_SERVICE_UID_MAX) {
            used[n_used++] = (int)pw->pw_uid;
        }
    }
    endpwent();
    info->free_uid = m2022_service_pick_uid(used, n_used);

    info->binary_installed = access(M2022_SERVICE_BINARY, X_OK) == 0;
    info->plist_exists = exists(M2022_SERVICE_PLIST);
    if (m2022_run(print_argv, out, sizeof out) >= 0) {
        m2022_service_parse_launchctl(out, &info->launchd);
    }

    if (m2022_usb_list(devs, sizeof devs / sizeof devs[0], &ndev) == 0) {
        for (size_t i = 0; i < ndev; i++) {
            if (devs[i].vid == 0x04e8 && devs[i].pid == 0x3321) {
                info->usb_printer = true;
            }
        }
    }

    snprintf(needle_dnssd, sizeof needle_dnssd, "%s._ipp", M2022_SERVICE_PRINTER_NAME);
    for (char *c = needle_dnssd; *c; c++) {
        if (*c == ' ') { /* DNS-SD URIs carry %20 */
            memmove(c + 3, c + 1, strlen(c + 1) + 1);
            memcpy(c, "%20", 3);
            c += 2;
        }
    }
    snprintf(needle_local, sizeof needle_local, "localhost:%d/ipp/print", M2022_SERVICE_PORT);
    nq = m2022_cups_queues(queues, sizeof queues / sizeof queues[0]);
    for (size_t i = 0; i < nq; i++) {
        if (strcmp(queues[i].name, M2022_VENDOR_QUEUE) == 0) {
            info->vendor_queue = true;
        } else if (strstr(queues[i].device_uri, needle_dnssd) != NULL ||
                   strstr(queues[i].device_uri, needle_local) != NULL) {
            info->our_queue = true;
            snprintf(info->our_queue_name, sizeof info->our_queue_name, "%s", queues[i].name);
        }
    }
    info->vendor_ppd = exists(M2022_VENDOR_PPD);
    info->vendor_driver = exists(M2022_VENDOR_DRIVER_DIR);
    info->backup_exists = exists(M2022_SERVICE_BACKUP_DIR "/" M2022_VENDOR_QUEUE ".ppd");
    info->queue_marker = exists(M2022_SERVICE_QUEUE_MARKER);
    info->support_dir = exists(M2022_SERVICE_SUPPORT_DIR);
    info->spool_dir = exists(M2022_SERVICE_SPOOL_DIR);
    info->log_dir = exists(M2022_SERVICE_LOG_DIR);
    info->state_file = exists(M2022_SERVICE_STATE_FILE);
    info->log_file = exists(M2022_SERVICE_LOG_FILE);

#ifdef __APPLE__
    {
        char raw[1024];
        uint32_t size = sizeof raw;
        if (_NSGetExecutablePath(raw, &size) == 0 && realpath(raw, info->self_path) == NULL) {
            snprintf(info->self_path, sizeof info->self_path, "%s", raw);
        }
    }
#endif
}

/* ---- plans ----------------------------------------------------------------------------- */

static m2022_step_t *add_step(m2022_plan_t *plan, m2022_step_kind_t kind, const char *text)
{
    m2022_step_t *s;
    if (plan->count >= M2022_PLAN_MAX) {
        return NULL;
    }
    s = &plan->steps[plan->count++];
    memset(s, 0, sizeof *s);
    s->kind = kind;
    snprintf(s->text, sizeof s->text, "%s", text);
    return s;
}

/* RUN step: NULL-terminated list of argument strings, copied into the step. */
static m2022_step_t *add_run(m2022_plan_t *plan, const char *text, ...)
{
    m2022_step_t *s = add_step(plan, M2022_STEP_RUN, text);
    va_list ap;
    size_t used = 0, n = 0;
    const char *arg;

    if (s == NULL) {
        return NULL;
    }
    va_start(ap, text);
    while ((arg = va_arg(ap, const char *)) != NULL && n + 1 < M2022_STEP_MAX_ARGS) {
        size_t len = strlen(arg) + 1;
        if (used + len > sizeof s->argbuf) {
            break;
        }
        memcpy(s->argbuf + used, arg, len);
        s->argv[n++] = s->argbuf + used;
        used += len;
    }
    va_end(ap);
    s->argv[n] = NULL;
    return s;
}

static m2022_step_t *add_write(m2022_plan_t *plan, const char *text, const char *path,
                               const char *content, unsigned mode)
{
    m2022_step_t *s = add_step(plan, M2022_STEP_WRITE, text);
    if (s == NULL) {
        return NULL;
    }
    snprintf(s->path, sizeof s->path, "%s", path);
    s->content = strdup(content);
    s->mode = mode;
    return s;
}

static m2022_step_t *add_mkdir(m2022_plan_t *plan, const char *path, unsigned mode,
                               const char *owner)
{
    char text[200];
    m2022_step_t *s;
    snprintf(text, sizeof text, "create %s", path);
    s = add_step(plan, M2022_STEP_MKDIR, text);
    if (s == NULL) {
        return NULL;
    }
    snprintf(s->path, sizeof s->path, "%s", path);
    snprintf(s->owner, sizeof s->owner, "%s", owner != NULL ? owner : "");
    s->mode = mode;
    return s;
}

static m2022_step_t *add_copy(m2022_plan_t *plan, const char *text, const char *src,
                              const char *path, unsigned mode)
{
    m2022_step_t *s = add_step(plan, M2022_STEP_COPY, text);
    if (s == NULL) {
        return NULL;
    }
    snprintf(s->src, sizeof s->src, "%s", src);
    snprintf(s->path, sizeof s->path, "%s", path);
    s->mode = mode;
    return s;
}

static m2022_step_t *add_remove(m2022_plan_t *plan, const char *path)
{
    char text[200];
    m2022_step_t *s;
    snprintf(text, sizeof text, "remove %s", path);
    s = add_step(plan, M2022_STEP_REMOVE, text);
    if (s != NULL) {
        snprintf(s->path, sizeof s->path, "%s", path);
    }
    return s;
}

static void add_note(m2022_plan_t *plan, const char *text)
{
    add_step(plan, M2022_STEP_NOTE, text);
}

bool m2022_service_install_plan(const m2022_service_info_t *info, const m2022_service_params_t *p,
                                const m2022_plan_options_t *o, m2022_plan_t *plan, char *why,
                                size_t why_size)
{
    char uid[16], plist[4096], rotate[256], port[16];
    const char *name =
        p != NULL && p->printer_name != NULL ? p->printer_name : M2022_SERVICE_PRINTER_NAME;
    const char *owner = M2022_SERVICE_USER ":" M2022_SERVICE_USER;
    m2022_step_t *s;

    memset(plan, 0, sizeof *plan);
    if (!info->usb_printer) {
        snprintf(why, why_size,
                 "no Samsung SL-M2022 (04e8:3321) on USB; switch it on and "
                 "connect it, then run install again");
        return false;
    }
    if (!info->user_exists && info->free_uid == 0) {
        snprintf(why, why_size, "no free UID between %d and %d for the service user",
                 M2022_SERVICE_UID_MIN, M2022_SERVICE_UID_MAX);
        return false;
    }
    if (info->self_path[0] == '\0') {
        snprintf(why, why_size, "cannot find this executable to install it");
        return false;
    }
    snprintf(port, sizeof port, "%d", p != NULL && p->port > 0 ? p->port : M2022_SERVICE_PORT);

    if (info->launchd.loaded) {
        /* an upgrade: stop the old service before its binary is replaced, and so that the
         * USB check below does not collide with the daemon's own status polls */
        s = add_run(plan, "stop the running service", "launchctl", "bootout",
                    "system/" M2022_SERVICE_LABEL, NULL);
        if (s != NULL) {
            s->ignore_failure = true;
        }
    }
    if (!info->user_exists) {
        snprintf(uid, sizeof uid, "%d", info->free_uid);
        add_run(plan, "create the service group", "dscl", ".", "-create",
                "/Groups/" M2022_SERVICE_USER, NULL);
        add_run(plan, "group id", "dscl", ".", "-create", "/Groups/" M2022_SERVICE_USER,
                "PrimaryGroupID", uid, NULL);
        add_run(plan, "group name", "dscl", ".", "-create", "/Groups/" M2022_SERVICE_USER,
                "RealName", "M2022 AirBridge", NULL);
        add_run(plan, "create the hidden service user", "dscl", ".", "-create",
                "/Users/" M2022_SERVICE_USER, NULL);
        add_run(plan, "user id", "dscl", ".", "-create", "/Users/" M2022_SERVICE_USER, "UniqueID",
                uid, NULL);
        add_run(plan, "user group", "dscl", ".", "-create", "/Users/" M2022_SERVICE_USER,
                "PrimaryGroupID", uid, NULL);
        add_run(plan, "no login shell", "dscl", ".", "-create", "/Users/" M2022_SERVICE_USER,
                "UserShell", "/usr/bin/false", NULL);
        add_run(plan, "home is the support directory", "dscl", ".", "-create",
                "/Users/" M2022_SERVICE_USER, "NFSHomeDirectory", M2022_SERVICE_SUPPORT_DIR, NULL);
        add_run(plan, "user name", "dscl", ".", "-create", "/Users/" M2022_SERVICE_USER, "RealName",
                "M2022 AirBridge", NULL);
        add_run(plan, "hidden from the login window", "dscl", ".", "-create",
                "/Users/" M2022_SERVICE_USER, "IsHidden", "1", NULL);
    }
    add_mkdir(plan, M2022_SERVICE_SUPPORT_DIR, 0750, owner);
    add_mkdir(plan, M2022_SERVICE_BACKUP_DIR, 0750, owner);
    add_mkdir(plan, M2022_SERVICE_SPOOL_DIR, 0700, owner);
    add_mkdir(plan, M2022_SERVICE_LOG_DIR, 0755, owner);
    /* the log holds job names and errors, nothing secret: readable without sudo, and kept
     * that way by the newsyslog entry (mode 644) */
    add_run(plan, "create the log file", "touch", M2022_SERVICE_LOG_FILE, NULL);
    add_run(plan, "owned by the service user", "chown", owner, M2022_SERVICE_LOG_FILE, NULL);
    add_run(plan, "readable by everyone", "chmod", "644", M2022_SERVICE_LOG_FILE, NULL);
    add_copy(plan, "install the binary", info->self_path, M2022_SERVICE_BINARY, 0755);
    s = add_run(plan, "check that the service user can open the printer", "sudo", "-u",
                M2022_SERVICE_USER, M2022_SERVICE_BINARY, "probe", "--quiet", NULL);
    if (s != NULL) {
        s->retries = 5; /* another process may hold the printer for a moment */
    }

    if (info->vendor_queue && !(o != NULL && o->keep_vendor)) {
        if (info->vendor_ppd) {
            add_copy(plan, "back up the Samsung queue's PPD", M2022_VENDOR_PPD,
                     M2022_SERVICE_BACKUP_DIR "/" M2022_VENDOR_QUEUE ".ppd", 0644);
        }
        s = add_run(plan, "back up the Samsung queue's device URI", "sh", "-c",
                    "lpstat -v " M2022_VENDOR_QUEUE
                    " | sed 's/^.*: //' > '" M2022_SERVICE_BACKUP_DIR "/device-uri.txt'",
                    NULL);
        if (s != NULL) {
            s->ignore_failure = true;
        }
        s = add_run(plan, "back up the Samsung queue's options", "sh", "-c",
                    "lpoptions -p " M2022_VENDOR_QUEUE " -l > '" M2022_SERVICE_BACKUP_DIR
                    "/lpoptions.txt'",
                    NULL);
        if (s != NULL) {
            s->ignore_failure = true;
        }
        add_run(plan, "remove the Samsung queue (one USB owner, ADR-006)", "lpadmin", "-x",
                M2022_VENDOR_QUEUE, NULL);
    } else if (info->vendor_queue) {
        add_note(plan, "keeping the Samsung queue as asked; do not print to both at once");
    }

    m2022_service_plist(p, plist, sizeof plist);
    add_write(plan, "write the launchd job", M2022_SERVICE_PLIST, plist, 0644);
    m2022_service_newsyslog(rotate, sizeof rotate);
    add_write(plan, "rotate the log with newsyslog", M2022_SERVICE_NEWSYSLOG, rotate, 0644);
    add_run(plan, "start the service", "launchctl", "bootstrap", "system", M2022_SERVICE_PLIST,
            NULL);
    s = add_step(plan, M2022_STEP_WAIT_IPP, "wait for the printer to answer IPP");
    if (s != NULL) {
        s->port = atoi(port);
    }
    if (!info->our_queue) {
        char uri[128];
        snprintf(uri, sizeof uri, "ipp://localhost:%s/ipp/print", port);
        add_run(plan, "add the printer to this Mac (driverless queue)", "lpadmin", "-p",
                M2022_SERVICE_QUEUE, "-E", "-v", uri, "-m", "everywhere", "-D", name, "-L", "USB",
                NULL);
        add_write(plan, "remember that the installer created the queue", M2022_SERVICE_QUEUE_MARKER,
                  "created by m2022-airbridge install\n", 0644);
    }
    if (o != NULL && o->test_page) {
        s = add_run(plan, "print a test page", "lp", "-d",
                    info->our_queue ? info->our_queue_name : M2022_SERVICE_QUEUE,
                    "/usr/share/cups/data/testprint", NULL);
        if (s != NULL) {
            s->ignore_failure = true;
        }
    }
    add_note(plan, "installed; `m2022-airbridge doctor` checks everything, `status` and `logs` "
                   "watch it");
    return true;
}

bool m2022_service_uninstall_plan(const m2022_service_info_t *info, const m2022_plan_options_t *o,
                                  m2022_plan_t *plan, char *why, size_t why_size)
{
    m2022_step_t *s;
    bool purge = o != NULL && o->purge;

    memset(plan, 0, sizeof *plan);
    if (!info->plist_exists && !info->binary_installed && !info->user_exists) {
        snprintf(why, why_size, "nothing is installed");
        return false;
    }
    if (info->launchd.loaded) {
        s = add_run(plan, "stop the service", "launchctl", "bootout", "system/" M2022_SERVICE_LABEL,
                    NULL);
        if (s != NULL) {
            s->ignore_failure = true;
        }
    }
    add_remove(plan, M2022_SERVICE_PLIST);
    add_remove(plan, M2022_SERVICE_NEWSYSLOG);
    if (info->queue_marker && info->our_queue) {
        s = add_run(plan, "remove the queue the installer created", "lpadmin", "-x",
                    info->our_queue_name, NULL);
        if (s != NULL) {
            s->ignore_failure = true;
        }
        add_remove(plan, M2022_SERVICE_QUEUE_MARKER);
    }
    if (info->backup_exists && !info->vendor_queue) {
        s = add_run(
            plan, "restore the Samsung queue from the backup (needs the vendor driver)", "sh", "-c",
            "lpadmin -p " M2022_VENDOR_QUEUE " -E -v \"$(cat '" M2022_SERVICE_BACKUP_DIR
            "/device-uri.txt')\" -P '" M2022_SERVICE_BACKUP_DIR "/" M2022_VENDOR_QUEUE ".ppd'",
            NULL);
        if (s != NULL) {
            s->ignore_failure = true;
        }
    }
    add_remove(plan, M2022_SERVICE_BINARY);
    if (purge) {
        add_run(plan, "remove the spool", "rm", "-rf", M2022_SERVICE_SPOOL_DIR, NULL);
        add_run(plan, "remove the logs", "rm", "-rf", M2022_SERVICE_LOG_DIR, NULL);
        add_run(plan, "remove state, backups and the support directory", "rm", "-rf",
                M2022_SERVICE_SUPPORT_DIR, NULL);
        if (info->user_exists) {
            add_run(plan, "delete the service user", "dscl", ".", "-delete",
                    "/Users/" M2022_SERVICE_USER, NULL);
            s = add_run(plan, "delete the service group", "dscl", ".", "-delete",
                        "/Groups/" M2022_SERVICE_USER, NULL);
            if (s != NULL) {
                s->ignore_failure = true;
            }
        }
    } else {
        add_note(plan, "kept the state, logs, backups and the service user (uninstall --purge "
                       "removes them)");
    }
    return true;
}

bool m2022_service_remove_vendor_plan(const m2022_service_info_t *info, m2022_plan_t *plan,
                                      char *why, size_t why_size)
{
    m2022_step_t *s;
    const char *owner = M2022_SERVICE_USER ":" M2022_SERVICE_USER;

    memset(plan, 0, sizeof *plan);
    if (!info->vendor_driver) {
        snprintf(why, why_size, "no Samsung driver package at " M2022_VENDOR_DRIVER_DIR);
        return false;
    }
    add_note(plan, "removing Samsung's driver package (Intel-only, unusable after macOS 28); "
                   "a backup is made first");
    add_mkdir(plan, M2022_SERVICE_BACKUP_DIR, 0750, info->user_exists ? owner : NULL);
    add_run(plan, "back up the driver files and PPDs", "sh", "-c",
            "cd / && find " M2022_VENDOR_PPD_DIR
            "/. -maxdepth 1 -name 'Samsung *' -print0 | tar czf '" M2022_VENDOR_BACKUP_TAR
            "' --null -T - " M2022_VENDOR_DRIVER_DIR,
            NULL);
    if (info->vendor_queue) {
        s = add_run(plan, "remove the Samsung queue", "lpadmin", "-x", M2022_VENDOR_QUEUE, NULL);
        if (s != NULL) {
            s->ignore_failure = true;
        }
    }
    add_run(plan, "delete the driver files", "rm", "-rf", M2022_VENDOR_DRIVER_DIR, NULL);
    add_run(plan, "delete the Samsung PPDs", "sh", "-c",
            "rm -f '" M2022_VENDOR_PPD_DIR "/Samsung '*", NULL);
    s = add_run(plan, "delete the driver cache", "rm", "-rf", M2022_VENDOR_CACHE_DIR, NULL);
    if (s != NULL) {
        s->ignore_failure = true;
    }
    s = add_run(plan, "forget the package receipt", "pkgutil", "--forget", M2022_VENDOR_RECEIPT,
                NULL);
    if (s != NULL) {
        s->ignore_failure = true;
    }
    add_note(plan, "restore with: sudo tar xzf '" M2022_VENDOR_BACKUP_TAR "' -C /");
    return true;
}

void m2022_plan_free(m2022_plan_t *plan)
{
    for (size_t i = 0; i < plan->count; i++) {
        free(plan->steps[i].content);
        plan->steps[i].content = NULL;
    }
    plan->count = 0;
}

static void print_step(const m2022_step_t *s, FILE *out)
{
    switch (s->kind) {
    case M2022_STEP_RUN:
        fputs("+", out);
        for (size_t i = 0; s->argv[i] != NULL; i++) {
            const char *a = s->argv[i];
            if (strchr(a, '\'') != NULL) {
                fprintf(out, " \"%s\"", a); /* a shell snippet with its own single quotes */
            } else if (strchr(a, ' ') != NULL || a[0] == '\0') {
                fprintf(out, " '%s'", a);
            } else {
                fprintf(out, " %s", a);
            }
        }
        fputc('\n', out);
        break;
    case M2022_STEP_WRITE:
        fprintf(out, "+ write %s (%zu bytes, mode %04o)\n", s->path,
                s->content != NULL ? strlen(s->content) : 0, s->mode);
        break;
    case M2022_STEP_MKDIR:
        fprintf(out, "+ mkdir -p '%s' (mode %04o%s%s)\n", s->path, s->mode,
                s->owner[0] ? ", owner " : "", s->owner);
        break;
    case M2022_STEP_COPY:
        fprintf(out, "+ cp '%s' '%s' (mode %04o)\n", s->src, s->path, s->mode);
        break;
    case M2022_STEP_REMOVE:
        fprintf(out, "+ rm -f '%s'\n", s->path);
        break;
    case M2022_STEP_WAIT_IPP:
        fprintf(out, "+ wait for IPP on localhost:%d\n", s->port);
        break;
    case M2022_STEP_NOTE:
        fprintf(out, "# %s\n", s->text);
        break;
    }
}

void m2022_plan_print(const m2022_plan_t *plan, FILE *out)
{
    for (size_t i = 0; i < plan->count; i++) {
        if (plan->steps[i].kind != M2022_STEP_NOTE) {
            fprintf(out, "# %s\n", plan->steps[i].text);
        }
        print_step(&plan->steps[i], out);
    }
}

static int mkdir_p(const char *path, unsigned mode)
{
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *c = tmp + 1; *c; c++) {
        if (*c == '/') {
            *c = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *c = '/';
        }
    }
    if (mkdir(tmp, (mode_t)mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return chmod(tmp, (mode_t)mode);
}

static int chown_named(const char *path, const char *owner)
{
    char user[64], *colon;
    struct passwd *pw;
    struct group *gr;
    if (owner == NULL || owner[0] == '\0') {
        return 0;
    }
    snprintf(user, sizeof user, "%s", owner);
    if ((colon = strchr(user, ':')) != NULL) {
        *colon = '\0';
    }
    pw = getpwnam(user);
    gr = colon != NULL ? getgrnam(colon + 1) : NULL;
    if (pw == NULL) {
        return -1;
    }
    return chown(path, pw->pw_uid, gr != NULL ? gr->gr_gid : pw->pw_gid);
}

/* Copy through a temporary name and rename: a process still running the old file keeps its
 * inode, so its code signature stays valid (overwriting a running binary in place gets it
 * killed by macOS). */
static int copy_file(const char *src, const char *dst, unsigned mode)
{
    FILE *in = fopen(src, "rb"), *out;
    char tmp[600];
    char buf[65536];
    size_t n;
    int rc = 0;
    if (in == NULL) {
        return -1;
    }
    snprintf(tmp, sizeof tmp, "%s.new", dst);
    out = fopen(tmp, "wb");
    if (out == NULL) {
        fclose(in);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            rc = -1;
            break;
        }
    }
    fclose(in);
    if (fclose(out) != 0) {
        rc = -1;
    }
    if (rc == 0) {
        rc = chmod(tmp, (mode_t)mode);
    }
    if (rc == 0) {
        rc = rename(tmp, dst);
    }
    if (rc != 0) {
        unlink(tmp);
    }
    return rc;
}

static bool ipp_port_open(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    bool ok;
    if (fd < 0) {
        return false;
    }
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ok = connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0;
    close(fd);
    return ok;
}

int m2022_service_execute(const m2022_plan_t *plan, FILE *out)
{
    for (size_t i = 0; i < plan->count; i++) {
        const m2022_step_t *s = &plan->steps[i];
        int rc = 0;
        print_step(s, out);
        fflush(out);
        switch (s->kind) {
        case M2022_STEP_RUN:
            rc = m2022_run(s->argv, NULL, 0);
            for (int attempt = 0; rc != 0 && attempt < s->retries; attempt++) {
                fprintf(out, "  (retrying)\n");
                sleep(1);
                rc = m2022_run(s->argv, NULL, 0);
            }
            break;
        case M2022_STEP_WRITE: {
            FILE *f = fopen(s->path, "w");
            rc =
                f == NULL || fputs(s->content != NULL ? s->content : "", f) == EOF || fclose(f) != 0
                    ? -1
                    : chmod(s->path, (mode_t)s->mode);
            break;
        }
        case M2022_STEP_MKDIR:
            rc = mkdir_p(s->path, s->mode);
            if (rc == 0) {
                rc = chown_named(s->path, s->owner);
            }
            break;
        case M2022_STEP_COPY:
            rc = copy_file(s->src, s->path, s->mode);
            break;
        case M2022_STEP_REMOVE:
            rc = unlink(s->path) == 0 || errno == ENOENT ? 0 : -1;
            break;
        case M2022_STEP_WAIT_IPP: {
            int tries = 0;
            while (!ipp_port_open(s->port) && tries++ < 60) {
                usleep(500000);
            }
            rc = tries <= 60 ? 0 : -1;
            break;
        }
        case M2022_STEP_NOTE:
            break;
        }
        if (rc != 0) {
            if (s->ignore_failure) {
                fprintf(out, "  (failed, continuing: %s)\n", s->text);
            } else {
                fprintf(out, "failed: %s\n", s->text);
                return (int)i + 1;
            }
        }
    }
    return 0;
}

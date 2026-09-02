/*
 * The service adapter's pure parts: the launchd plist, the newsyslog entry, launchctl output
 * parsing, UID choice, and the install/uninstall plans for the situations the installer meets
 * (fresh Mac, vendor queue present, already installed, nothing installed).
 */
#include "m2022/service.h"

#include "m2022_test.h"

#include <stdlib.h>

/* Index of the first RUN step whose first two arguments match, or -1. */
static int find_run(const m2022_plan_t *plan, const char *a0, const char *a1)
{
    for (size_t i = 0; i < plan->count; i++) {
        const m2022_step_t *s = &plan->steps[i];
        if (s->kind == M2022_STEP_RUN && s->argv[0] != NULL && strcmp(s->argv[0], a0) == 0 &&
            (a1 == NULL || (s->argv[1] != NULL && strcmp(s->argv[1], a1) == 0))) {
            return (int)i;
        }
    }
    return -1;
}

static int find_kind(const m2022_plan_t *plan, m2022_step_kind_t kind, const char *path_part)
{
    for (size_t i = 0; i < plan->count; i++) {
        const m2022_step_t *s = &plan->steps[i];
        if (s->kind == kind && (path_part == NULL || strstr(s->path, path_part) != NULL)) {
            return (int)i;
        }
    }
    return -1;
}

static void fresh_mac(m2022_service_info_t *info)
{
    memset(info, 0, sizeof *info);
    info->usb_printer = true;
    info->free_uid = 302;
    info->vendor_queue = true;
    info->vendor_ppd = true;
    snprintf(info->self_path, sizeof info->self_path, "/Users/me/build/src/m2022-airbridge");
}

int main(void)
{
    char buf[4096], why[256];
    m2022_service_params_t params = {NULL, 0};
    m2022_plan_options_t opts = {false, false, false};
    m2022_service_info_t info;
    m2022_plan_t plan;
    m2022_launchd_state_t st;

    /* plist: label, arguments in order, user, keep-alive, home */
    m2022_service_plist(&params, buf, sizeof buf);
    CHECK(strstr(buf, "<key>Label</key>\n\t<string>com.m2022airbridge.daemon</string>") != NULL);
    CHECK(strstr(buf,
                 "<string>/usr/local/bin/m2022-airbridge</string>\n\t\t<string>server</string>") !=
          NULL);
    CHECK(strstr(buf, "<string>--port</string>\n\t\t<string>8000</string>") != NULL);
    CHECK(strstr(buf, "<string>--name</string>\n\t\t<string>Samsung M2022</string>") != NULL);
    CHECK(strstr(buf, "<string>--state</string>\n\t\t<string>/Library/Application "
                      "Support/M2022AirBridge/state.conf</string>") != NULL);
    CHECK(strstr(buf, "<key>UserName</key>\n\t<string>_m2022airbridge</string>") != NULL);
    CHECK(strstr(buf, "<key>KeepAlive</key>\n\t<true/>") != NULL);
    CHECK(strstr(buf, "<key>RunAtLoad</key>\n\t<true/>") != NULL);
    CHECK(
        strstr(
            buf,
            "<key>HOME</key>\n\t\t<string>/Library/Application Support/M2022AirBridge</string>") !=
        NULL);
    CHECK(strstr(buf, "</plist>\n") != NULL);
    params.printer_name = "Study <Laser> & Co";
    params.port = 8100;
    m2022_service_plist(&params, buf, sizeof buf);
    CHECK(strstr(buf, "<string>Study &lt;Laser&gt; &amp; Co</string>") != NULL);
    CHECK(strstr(buf, "<string>8100</string>") != NULL);
    params.printer_name = NULL;
    params.port = 0;

    /* newsyslog */
    m2022_service_newsyslog(buf, sizeof buf);
    CHECK(strstr(buf,
                 "/Library/Logs/M2022AirBridge/"
                 "m2022-airbridge.log\t_m2022airbridge:_m2022airbridge\t644\t5\t5000\t*\tJ\n") !=
          NULL);

    /* launchctl print */
    m2022_service_parse_launchctl(
        "system/com.m2022airbridge.daemon = {\n\tactive count = 1\n\tpath = "
        "/Library/LaunchDaemons/x.plist\n\tstate = running\n\n\tprogram = "
        "/usr/local/bin/m2022-airbridge\n\tpid = 4242\n",
        &st);
    CHECK(st.loaded && st.running);
    CHECK_EQ_INT(st.pid, 4242);
    m2022_service_parse_launchctl("\tstate = waiting\n\tlast exit code = 1\n", &st);
    CHECK(st.loaded && !st.running);
    CHECK_EQ_INT(st.pid, 0);
    m2022_service_parse_launchctl("", &st);
    CHECK(!st.loaded && !st.running);
    m2022_service_parse_launchctl(NULL, &st);
    CHECK(!st.loaded);

    /* UID choice */
    {
        int used[] = {300, 301, 305};
        int all[200];
        CHECK_EQ_INT(m2022_service_pick_uid(used, 3), 302);
        CHECK_EQ_INT(m2022_service_pick_uid(used, 0), 300);
        for (int i = 0; i < 200; i++) {
            all[i] = 300 + i;
        }
        CHECK_EQ_INT(m2022_service_pick_uid(all, 200), 0);
    }

    /* install on a fresh Mac with the vendor queue: user, dirs, binary, backup, remove queue,
     * plist, bootstrap, wait, our queue */
    fresh_mac(&info);
    CHECK(m2022_service_install_plan(&info, &params, &opts, &plan, why, sizeof why));
    CHECK(plan.count > 20);
    {
        int group = find_run(&plan, "dscl", "."),
            copy = find_kind(&plan, M2022_STEP_COPY, "/usr/local/bin/m2022-airbridge");
        int probe = find_run(&plan, "sudo", "-u"),
            backup = find_kind(&plan, M2022_STEP_COPY, "backup/Samsung_M2020_Series.ppd");
        int remove = find_run(&plan, "lpadmin", "-x"),
            plist = find_kind(&plan, M2022_STEP_WRITE, "LaunchDaemons");
        int boot = find_run(&plan, "launchctl", "bootstrap"),
            wait = find_kind(&plan, M2022_STEP_WAIT_IPP, NULL);
        int queue = find_run(&plan, "lpadmin", "-p"),
            marker = find_kind(&plan, M2022_STEP_WRITE, "queue-created");
        CHECK(group >= 0 && copy > group && probe > copy && backup > probe && remove > backup);
        CHECK_EQ_INT(plan.steps[probe].retries, 5);
        CHECK(find_run(&plan, "launchctl", "bootout") < 0); /* nothing to stop on a fresh Mac */
        CHECK(plist > remove && boot > plist && wait > boot && queue > wait && marker > queue);
        CHECK_EQ_STR(plan.steps[remove].argv[2], "Samsung_M2020_Series");
        CHECK_EQ_STR(plan.steps[copy].src, "/Users/me/build/src/m2022-airbridge");
        CHECK_EQ_INT(plan.steps[copy].mode, 0755);
        CHECK_EQ_INT(plan.steps[wait].port, 8000);
        CHECK_EQ_STR(plan.steps[queue].argv[2], "Samsung_M2022");
        CHECK_EQ_STR(plan.steps[queue].argv[5], "ipp://localhost:8000/ipp/print");
        CHECK(strstr(plan.steps[plist].content, "com.m2022airbridge.daemon") != NULL);
        /* the service user's UID goes into dscl */
        CHECK(find_run(&plan, "dscl", NULL) >= 0);
        {
            bool saw_uid = false;
            for (size_t i = 0; i < plan.count; i++) {
                const m2022_step_t *s = &plan.steps[i];
                if (s->kind == M2022_STEP_RUN && s->argv[4] != NULL && s->argv[5] != NULL &&
                    strcmp(s->argv[4], "UniqueID") == 0 && strcmp(s->argv[5], "302") == 0) {
                    saw_uid = true;
                }
            }
            CHECK(saw_uid);
        }
        CHECK(find_run(&plan, "lp", "-d") < 0);
        /* the log file is created readable before the service starts */
        CHECK(find_run(&plan, "touch", M2022_SERVICE_LOG_FILE) > 0);
        CHECK(find_run(&plan, "chmod", "644") > find_run(&plan, "touch", M2022_SERVICE_LOG_FILE));
        CHECK(find_run(&plan, "chmod", "644") < boot);
    }
    m2022_plan_free(&plan);

    /* keep the vendor queue, print a test page, user already exists, service already loaded */
    fresh_mac(&info);
    info.user_exists = true;
    info.launchd.loaded = true;
    opts.keep_vendor = true;
    opts.test_page = true;
    CHECK(m2022_service_install_plan(&info, &params, &opts, &plan, why, sizeof why));
    CHECK(find_run(&plan, "lpadmin", "-x") < 0);
    CHECK(find_run(&plan, "dscl", NULL) < 0);
    CHECK_EQ_INT(find_run(&plan, "launchctl", "bootout"), 0); /* first: before the binary moves */
    CHECK(find_run(&plan, "launchctl", "bootout") < find_run(&plan, "sudo", "-u"));
    CHECK(find_run(&plan, "launchctl", "bootout") < find_run(&plan, "launchctl", "bootstrap"));
    CHECK(find_run(&plan, "lp", "-d") >= 0);
    m2022_plan_free(&plan);
    opts.keep_vendor = false;
    opts.test_page = false;

    /* a queue already points at us: none is created */
    fresh_mac(&info);
    info.our_queue = true;
    snprintf(info.our_queue_name, sizeof info.our_queue_name, "Samsung_M2022");
    CHECK(m2022_service_install_plan(&info, &params, &opts, &plan, why, sizeof why));
    CHECK(find_run(&plan, "lpadmin", "-p") < 0);
    m2022_plan_free(&plan);

    /* no printer: refused with a reason */
    fresh_mac(&info);
    info.usb_printer = false;
    CHECK(!m2022_service_install_plan(&info, &params, &opts, &plan, why, sizeof why));
    CHECK(strstr(why, "04e8:3321") != NULL);
    fresh_mac(&info);
    info.free_uid = 0;
    CHECK(!m2022_service_install_plan(&info, &params, &opts, &plan, why, sizeof why));
    fresh_mac(&info);
    info.self_path[0] = '\0';
    CHECK(!m2022_service_install_plan(&info, &params, &opts, &plan, why, sizeof why));

    /* uninstall: stop, remove files, remove our queue, restore the vendor's, keep data */
    memset(&info, 0, sizeof info);
    info.plist_exists = info.binary_installed = info.user_exists = true;
    info.launchd.loaded = info.launchd.running = true;
    info.our_queue = info.queue_marker = info.backup_exists = true;
    snprintf(info.our_queue_name, sizeof info.our_queue_name, "Samsung_M2022");
    CHECK(m2022_service_uninstall_plan(&info, &opts, &plan, why, sizeof why));
    CHECK_EQ_INT(find_run(&plan, "launchctl", "bootout"), 0);
    CHECK(find_kind(&plan, M2022_STEP_REMOVE, "LaunchDaemons") > 0);
    CHECK(find_kind(&plan, M2022_STEP_REMOVE, "newsyslog.d") > 0);
    CHECK(find_run(&plan, "lpadmin", "-x") > 0);
    CHECK_EQ_STR(plan.steps[find_run(&plan, "lpadmin", "-x")].argv[2], "Samsung_M2022");
    CHECK(find_run(&plan, "sh", "-c") > 0); /* the vendor queue restore */
    CHECK(strstr(plan.steps[find_run(&plan, "sh", "-c")].argv[2],
                 "lpadmin -p Samsung_M2020_Series") != NULL);
    CHECK(find_kind(&plan, M2022_STEP_REMOVE, "/usr/local/bin/m2022-airbridge") > 0);
    CHECK(find_run(&plan, "rm", "-rf") < 0);
    CHECK(find_run(&plan, "dscl", NULL) < 0);
    m2022_plan_free(&plan);

    /* uninstall --purge: data and user go too; no restore when the vendor queue still exists */
    info.vendor_queue = true;
    opts.purge = true;
    CHECK(m2022_service_uninstall_plan(&info, &opts, &plan, why, sizeof why));
    CHECK(find_run(&plan, "sh", "-c") < 0);
    CHECK(find_run(&plan, "rm", "-rf") > 0);
    CHECK(find_run(&plan, "dscl", ".") > 0);
    CHECK_EQ_STR(plan.steps[find_run(&plan, "dscl", ".")].argv[3], "/Users/_m2022airbridge");
    m2022_plan_free(&plan);
    opts.purge = false;

    /* nothing installed */
    memset(&info, 0, sizeof info);
    CHECK(!m2022_service_uninstall_plan(&info, &opts, &plan, why, sizeof why));

    /* the vendor driver: backup before deletion, queue removed, receipt forgotten */
    memset(&info, 0, sizeof info);
    CHECK(!m2022_service_remove_vendor_plan(&info, &plan, why, sizeof why));
    info.vendor_driver = true;
    info.vendor_queue = true;
    info.user_exists = true;
    CHECK(m2022_service_remove_vendor_plan(&info, &plan, why, sizeof why));
    {
        int backup = find_run(&plan, "sh", "-c"), queue = find_run(&plan, "lpadmin", "-x");
        int rm = find_run(&plan, "rm", "-rf"), forget = find_run(&plan, "pkgutil", "--forget");
        CHECK(backup > 0 && strstr(plan.steps[backup].argv[2], "tar czf") != NULL);
        CHECK(strstr(plan.steps[backup].argv[2], "/Library/Printers/Samsung") != NULL);
        CHECK(queue > backup && rm > queue && forget > rm);
        CHECK_EQ_STR(plan.steps[rm].argv[2], "/Library/Printers/Samsung");
    }
    m2022_plan_free(&plan);
    info.vendor_queue = false;
    CHECK(m2022_service_remove_vendor_plan(&info, &plan, why, sizeof why));
    CHECK(find_run(&plan, "lpadmin", "-x") < 0);
    m2022_plan_free(&plan);

    /* the printed plan is shell-like and quotes paths with spaces */
    fresh_mac(&info);
    CHECK(m2022_service_install_plan(&info, &params, &opts, &plan, why, sizeof why));
    {
        char *text = NULL;
        size_t len = 0;
        FILE *f = open_memstream(&text, &len);
        m2022_plan_print(&plan, f);
        fclose(f);
        CHECK(strstr(text, "+ launchctl bootstrap system "
                           "/Library/LaunchDaemons/com.m2022airbridge.daemon.plist\n") != NULL);
        CHECK(strstr(text, "+ mkdir -p '/Library/Application Support/M2022AirBridge' (mode 0750, "
                           "owner _m2022airbridge:_m2022airbridge)\n") != NULL);
        CHECK(strstr(text, "+ lpadmin -x Samsung_M2020_Series\n") != NULL);
        CHECK(strstr(text, "# ") != NULL);
        free(text);
    }
    m2022_plan_free(&plan);

    /* running a command and capturing its output */
    {
        const char *const argv[] = {"sh", "-c", "echo hi; exit 3", NULL};
        char out[64];
        CHECK_EQ_INT(m2022_run(argv, out, sizeof out), 3);
        CHECK_EQ_STR(out, "hi\n");
        CHECK_EQ_INT(m2022_run(argv, NULL, 0), 3);
    }
    {
        const char *const argv[] = {"true", NULL};
        CHECK_EQ_INT(m2022_run(argv, NULL, 0), 0);
    }

    TEST_MAIN_END();
}

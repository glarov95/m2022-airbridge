/*
 * server [--port N] [--name NAME] [--capture DIR] [--device URI] [--spool DIR] [--log FILE]
 *        [--debug] [--no-tls]
 *
 * Run the Printer Application in the foreground until Ctrl-C.
 */
#include "cli.h"
#include "m2022/app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_server(int argc, char **argv)
{
    m2022_app_config_t cfg;

    memset(&cfg, 0, sizeof cfg);
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            cfg.port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            cfg.name = argv[++i];
        } else if (strcmp(argv[i], "--capture") == 0 && i + 1 < argc) {
            cfg.capture_dir = argv[++i];
        } else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            cfg.device_uri = argv[++i];
        } else if (strcmp(argv[i], "--spool") == 0 && i + 1 < argc) {
            cfg.spool_dir = argv[++i];
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            cfg.log_file = argv[++i];
        } else if (strcmp(argv[i], "--debug") == 0) {
            cfg.debug = true;
        } else if (strcmp(argv[i], "--no-tls") == 0) {
            cfg.no_tls = true;
        } else {
            fprintf(stderr, "server: unknown option '%s'\n", argv[i]);
            fprintf(stderr, "Usage: m2022-airbridge server [--port N] [--name NAME] "
                            "[--capture DIR] [--device URI] [--spool DIR] [--log FILE] "
                            "[--debug] [--no-tls]\n");
            return 2;
        }
    }
    return m2022_app_run(&cfg);
}

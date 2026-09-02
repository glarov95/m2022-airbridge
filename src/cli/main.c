/*
 * m2022-airbridge command line entry point.
 *
 * Subcommands are added milestone by milestone (see SPEC.md section 6.9).
 */
#include "m2022/version.h"

#include "cli.h"

#include <stdio.h>
#include <string.h>

static int usage(FILE *out)
{
    fputs("Usage: m2022-airbridge <command> [options]\n"
          "\n"
          "Commands:\n"
          "  probe [--json]        host, USB printers (device id, port status), CUPS queues\n"
          "  send FILE [options]   write a printer-native job to the USB printer\n"
          "  decode FILE [options] explain a captured SPL/QPDL job; --pbm PREFIX writes pages\n"
          "  server [options]      run the Printer Application in the foreground\n"
          "  render IN [options]   halftone a PGM or CUPS raster into a PBM (--preset, --method)\n"
          "  encode IN [options]   build a complete printer job (.spl) from a PGM, PBM or CUPS "
          "raster\n"
          "  install [--dry-run]   install the launchd service on this Mac (sudo)\n"
          "  uninstall [--purge]   remove it (sudo)\n"
          "  remove-vendor-driver  back up and delete the Samsung driver package (sudo)\n"
          "  start|stop|restart    control the service (sudo)\n"
          "  status                service, printer and queue state\n"
          "  logs [-n N] [-f]      show the service log\n"
          "  doctor                check everything a working printer needs\n"
          "  version               print the version and exit\n"
          "  help                  show this help\n",
          out);
    return out == stdout ? 0 : 2;
}

int main(int argc, char **argv)
{
    const char *cmd;

    if (argc < 2) {
        return usage(stderr);
    }
    cmd = argv[1];
    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0) {
        printf("m2022-airbridge %s\n", m2022_version_string());
        return 0;
    }
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        return usage(stdout);
    }
    if (strcmp(cmd, "probe") == 0) {
        return cmd_probe(argc - 2, argv + 2);
    }
    if (strcmp(cmd, "send") == 0) {
        return cmd_send(argc - 2, argv + 2);
    }
    if (strcmp(cmd, "decode") == 0) {
        return cmd_decode(argc - 2, argv + 2);
    }
    if (strcmp(cmd, "server") == 0) {
        return cmd_server(argc - 2, argv + 2);
    }
    if (strcmp(cmd, "render") == 0) {
        return cmd_render(argc - 2, argv + 2);
    }
    if (strcmp(cmd, "encode") == 0) {
        return cmd_encode(argc - 2, argv + 2);
    }
    if (strcmp(cmd, "install") == 0) {
        return cmd_install(argc - 2, argv + 2);
    }
    if (strcmp(cmd, "uninstall") == 0) {
        return cmd_uninstall(argc - 2, argv + 2);
    }
    if (strcmp(cmd, "remove-vendor-driver") == 0) {
        return cmd_remove_vendor(argc - 2, argv + 2);
    }
    if (strcmp(cmd, "start") == 0 || strcmp(cmd, "stop") == 0 || strcmp(cmd, "restart") == 0) {
        return cmd_service(cmd, argc - 2, argv + 2);
    }
    if (strcmp(cmd, "status") == 0) {
        return cmd_status(argc - 2, argv + 2);
    }
    if (strcmp(cmd, "logs") == 0) {
        return cmd_logs(argc - 2, argv + 2);
    }
    if (strcmp(cmd, "doctor") == 0) {
        return cmd_doctor(argc - 2, argv + 2);
    }
    fprintf(stderr, "m2022-airbridge: unknown command '%s'\n", cmd);
    return usage(stderr);
}

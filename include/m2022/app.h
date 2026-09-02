/*
 * The Printer Application: a PAPPL system with one printer, our driver callbacks, and (until
 * the encoder exists) a capture device that keeps every received job and decoded page on disk.
 */
#ifndef M2022_APP_H
#define M2022_APP_H

#include <stdbool.h>

typedef struct {
    const char *name;        /* printer name and DNS-SD instance, default "Samsung M2022" */
    int port;                /* 0 = 8000 */
    const char *spool_dir;   /* NULL = PAPPL default (temporary directory) */
    const char *capture_dir; /* NULL = do not capture jobs */
    const char *device_uri;  /* NULL = file device under capture_dir or /tmp */
    const char *log_file;    /* NULL = stderr */
    const char *state_file;  /* NULL = nothing persists across restarts */
    bool debug;              /* verbose PAPPL logging */
    bool no_tls;             /* disable TLS (diagnostics only) */
} m2022_app_config_t;

/* Run the application until it is told to stop (SIGTERM/SIGINT). Returns an exit status. */
int m2022_app_run(const m2022_app_config_t *cfg);

#endif /* M2022_APP_H */

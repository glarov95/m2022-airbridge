#include "m2022/cups.h"

#include <cups/cups.h>
#include <stdlib.h>
#include <string.h>

static void copy_opt(cups_dest_t *d, const char *name, char *dst, size_t dstlen)
{
    const char *v = cupsGetOption(name, d->num_options, d->options);
    if (v == NULL) {
        v = "";
    }
    strncpy(dst, v, dstlen - 1);
    dst[dstlen - 1] = '\0';
}

size_t m2022_cups_queues(m2022_cups_queue_t *out, size_t max)
{
    cups_dest_t *dests = NULL;
    int n = cupsGetDests(&dests);
    size_t count = 0;

    for (int i = 0; i < n; i++) {
        cups_dest_t *d = &dests[i];
        m2022_cups_queue_t q;
        const char *state;

        if (d->instance != NULL) {
            continue; /* per-user option instances, not queues */
        }
        memset(&q, 0, sizeof q);
        strncpy(q.name, d->name, sizeof q.name - 1);
        copy_opt(d, "printer-make-and-model", q.make_model, sizeof q.make_model);
        copy_opt(d, "device-uri", q.device_uri, sizeof q.device_uri);
        copy_opt(d, "printer-state-reasons", q.state_reasons, sizeof q.state_reasons);
        state = cupsGetOption("printer-state", d->num_options, d->options);
        q.state = state != NULL ? atoi(state) : 0;
        q.is_default = d->is_default != 0;
        if (count < max) {
            out[count] = q;
        }
        count++;
    }
    cupsFreeDests(n, dests);
    return count;
}

const char *m2022_cups_state_name(int state)
{
    switch (state) {
    case 3:
        return "idle";
    case 4:
        return "processing";
    case 5:
        return "stopped";
    default:
        return "unknown";
    }
}

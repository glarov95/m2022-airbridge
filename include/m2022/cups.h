/* Read-only view of the local CUPS queues through libcups (used by probe and doctor). */
#ifndef M2022_CUPS_H
#define M2022_CUPS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char name[128];
    char make_model[256];
    char device_uri[512];
    int state; /* IPP printer-state: 3 idle, 4 processing, 5 stopped; 0 unknown */
    char state_reasons[512];
    bool is_default;
} m2022_cups_queue_t;

size_t m2022_cups_queues(m2022_cups_queue_t *out, size_t max);
const char *m2022_cups_state_name(int state);

#endif /* M2022_CUPS_H */

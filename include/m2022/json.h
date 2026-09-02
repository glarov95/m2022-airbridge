#ifndef M2022_JSON_H
#define M2022_JSON_H

#include <stdio.h>

/* Write `s` as a quoted, escaped JSON string ("null" when s is NULL). */
void m2022_json_string(FILE *out, const char *s);

#endif /* M2022_JSON_H */

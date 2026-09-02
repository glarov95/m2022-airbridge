#include "m2022/usb.h"

#include <string.h>
#include <strings.h>

static void trim(const char **s, size_t *len)
{
    while (*len > 0 && (**s == ' ' || **s == '\t')) {
        (*s)++;
        (*len)--;
    }
    while (*len > 0 && ((*s)[*len - 1] == ' ' || (*s)[*len - 1] == '\t')) {
        (*len)--;
    }
}

size_t m2022_ieee1284_parse(const char *id, m2022_ieee1284_field_t *fields, size_t max)
{
    size_t n = 0;
    const char *p = id;

    while (p != NULL && *p != '\0') {
        const char *end = strchr(p, ';');
        size_t seglen = end != NULL ? (size_t)(end - p) : strlen(p);
        const char *colon = memchr(p, ':', seglen);

        if (colon != NULL) {
            const char *k = p;
            size_t kl = (size_t)(colon - p);
            const char *v = colon + 1;
            size_t vl = seglen - kl - 1;
            trim(&k, &kl);
            trim(&v, &vl);
            if (kl > 0) {
                if (n < max) {
                    fields[n].key = k;
                    fields[n].key_len = kl;
                    fields[n].value = v;
                    fields[n].value_len = vl;
                }
                n++;
            }
        }
        p = end != NULL ? end + 1 : NULL;
    }
    return n;
}

static const char *const ALIASES[][3] = {
    {"MFG", "MANUFACTURER", NULL},   {"MDL", "MODEL", NULL}, {"CMD", "COMMAND SET", NULL},
    {"SN", "SERN", "SERIALNUMBER"},  {"CLS", "CLASS", NULL}, {"DES", "DESCRIPTION", NULL},
};

static bool key_equal(const char *key, size_t key_len, const char *name)
{
    return strlen(name) == key_len && strncasecmp(key, name, key_len) == 0;
}

static bool key_matches(const char *key, size_t key_len, const char *wanted)
{
    if (key_equal(key, key_len, wanted)) {
        return true;
    }
    for (size_t g = 0; g < sizeof ALIASES / sizeof ALIASES[0]; g++) {
        bool wanted_in_group = false;
        for (size_t a = 0; a < 3 && ALIASES[g][a] != NULL; a++) {
            if (strcasecmp(ALIASES[g][a], wanted) == 0) {
                wanted_in_group = true;
            }
        }
        if (!wanted_in_group) {
            continue;
        }
        for (size_t a = 0; a < 3 && ALIASES[g][a] != NULL; a++) {
            if (key_equal(key, key_len, ALIASES[g][a])) {
                return true;
            }
        }
    }
    return false;
}

bool m2022_ieee1284_get(const char *id, const char *key, char *out, size_t outlen)
{
    m2022_ieee1284_field_t fields[32];
    size_t n = m2022_ieee1284_parse(id, fields, sizeof fields / sizeof fields[0]);

    if (outlen == 0) {
        return false;
    }
    out[0] = '\0';
    if (n > sizeof fields / sizeof fields[0]) {
        n = sizeof fields / sizeof fields[0];
    }
    for (size_t i = 0; i < n; i++) {
        if (key_matches(fields[i].key, fields[i].key_len, key)) {
            size_t len = fields[i].value_len < outlen - 1 ? fields[i].value_len : outlen - 1;
            memcpy(out, fields[i].value, len);
            out[len] = '\0';
            return true;
        }
    }
    return false;
}

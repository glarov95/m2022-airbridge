#ifndef M2022_VERSION_H
#define M2022_VERSION_H

/* Semantic version of the build, e.g. "0.1.0". Never NULL. */
const char *m2022_version_string(void);

/* Numeric components of m2022_version_string(). */
void m2022_version_components(int *major, int *minor, int *patch);

#endif /* M2022_VERSION_H */

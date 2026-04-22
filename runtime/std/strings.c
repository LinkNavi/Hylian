#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

int64_t hylian_length(char *s) {
    return (int64_t)strlen(s);
}

int64_t hylian_is_empty(char *s) {
    return s == NULL || s[0] == '\0';
}

int64_t hylian_contains(char *s, char *needle) {
    return strstr(s, needle) != NULL;
}

int64_t hylian_starts_with(char *s, char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

int64_t hylian_ends_with(char *s, char *suffix) {
    int64_t slen = strlen(s);
    int64_t suflen = strlen(suffix);
    if (suflen > slen) return 0;
    return strcmp(s + slen - suflen, suffix) == 0;
}

int64_t hylian_index_of(char *s, char *needle) {
    char *p = strstr(s, needle);
    if (!p) return -1;
    return (int64_t)(p - s);
}

// caller must free
char *hylian_slice(char *s, int64_t start, int64_t end) {
    int64_t len = strlen(s);
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) return strdup("");
    int64_t size = end - start;
    char *out = malloc(size + 1);
    if (!out) return NULL;
    memcpy(out, s + start, size);
    out[size] = '\0';
    return out;
}

// caller must free
char *hylian_trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return strdup("");
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    int64_t len = end - s + 1;
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

// caller must free
char *hylian_trim_start(char *s) {
    while (isspace((unsigned char)*s)) s++;
    return strdup(s);
}

// caller must free
char *hylian_trim_end(char *s) {
    char *out = strdup(s);
    if (!out) return NULL;
    char *end = out + strlen(out) - 1;
    while (end > out && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return out;
}

// caller must free
char *hylian_to_upper(char *s) {
    char *out = strdup(s);
    if (!out) return NULL;
    for (char *p = out; *p; p++) *p = toupper((unsigned char)*p);
    return out;
}

// caller must free
char *hylian_to_lower(char *s) {
    char *out = strdup(s);
    if (!out) return NULL;
    for (char *p = out; *p; p++) *p = tolower((unsigned char)*p);
    return out;
}

// caller must free
char *hylian_replace(char *s, char *old, char *new) {
    int64_t slen   = strlen(s);
    int64_t oldlen = strlen(old);
    int64_t newlen = strlen(new);
    if (oldlen == 0) return strdup(s);

    // count occurrences
    int64_t count = 0;
    char *p = s;
    while ((p = strstr(p, old))) { count++; p += oldlen; }

    char *out = malloc(slen + count * (newlen - oldlen) + 1);
    if (!out) return NULL;

    char *dst = out;
    p = s;
    char *match;
    while ((match = strstr(p, old))) {
        int64_t prefix = match - p;
        memcpy(dst, p, prefix);
        dst += prefix;
        memcpy(dst, new, newlen);
        dst += newlen;
        p = match + oldlen;
    }
    strcpy(dst, p);
    return out;
}

// returns array of char*, last element is NULL — caller must free each + array
char **hylian_split(char *s, char *delim) {
    int64_t dlen = strlen(delim);
    int64_t count = 1;
    char *p = s;
    while ((p = strstr(p, delim))) { count++; p += dlen; }

    char **out = malloc((count + 1) * sizeof(char *));
    if (!out) return NULL;

    int64_t i = 0;
    p = s;
    char *match;
    while ((match = strstr(p, delim))) {
        int64_t len = match - p;
        out[i] = malloc(len + 1);
        memcpy(out[i], p, len);
        out[i][len] = '\0';
        i++;
        p = match + dlen;
    }
    out[i++] = strdup(p);
    out[i]   = NULL;
    return out;
}

// caller must free
char *hylian_join(char **parts, int64_t count, char *delim) {
    if (count == 0) return strdup("");
    int64_t dlen = strlen(delim);
    int64_t total = 0;
    for (int64_t i = 0; i < count; i++) total += strlen(parts[i]);
    total += dlen * (count - 1);

    char *out = malloc(total + 1);
    if (!out) return NULL;

    char *dst = out;
    for (int64_t i = 0; i < count; i++) {
        int64_t len = strlen(parts[i]);
        memcpy(dst, parts[i], len);
        dst += len;
        if (i < count - 1) { memcpy(dst, delim, dlen); dst += dlen; }
    }
    *dst = '\0';
    return out;
}

// returns 1 on success, 0 on failure
int64_t hylian_to_int(char *s, int64_t *out) {
    char *end;
    *out = strtoll(s, &end, 10);
    return end != s && *end == '\0';
}

int64_t hylian_to_float(char *s, double *out) {
    char *end;
    *out = strtod(s, &end);
    return end != s && *end == '\0';
}

// caller must free
char *hylian_from_int(int64_t n) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)n);
    return strdup(buf);
}

int64_t hylian_equals(char *a, char *b) {
    return strcmp(a, b) == 0;
}

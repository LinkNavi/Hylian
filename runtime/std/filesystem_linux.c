#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int64_t hylian_file_read(char *path, int64_t path_len, char *buf, int64_t buf_len) {
    char tmp[4097];
    memcpy(tmp, path, path_len);
    tmp[path_len] = 0;
    FILE *f = fopen(tmp, "rb");
    if (!f) return -1;
    int64_t n = fread(buf, 1, buf_len, f);
    fclose(f);
    return n;
}

int64_t hylian_mkdir(char *path, int64_t path_len) {
	char tmp[4097];
	memcpy(tmp, path, path_len);
	tmp[path_len] = 0;
	int result = mkdir(tmp, 0755);
	return (result == 0) ? 0 : -1;
}

int64_t hylian_file_write(char *path, int64_t path_len, char *buf, int64_t buf_len) {
    char tmp[4097];
    memcpy(tmp, path, path_len);
    tmp[path_len] = 0;
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    int64_t n = fwrite(buf, 1, buf_len, f);
    fclose(f);
    return n;
}

int64_t hylian_file_append(char *path, int64_t path_len, char *buf, int64_t buf_len) {
    char tmp[4097];
    memcpy(tmp, path, path_len);
    tmp[path_len] = 0;
    FILE *f = fopen(tmp, "ab");
    if (!f) return -1;
    int64_t n = fwrite(buf, 1, buf_len, f);
    fclose(f);
    return n;
}

int64_t hylian_file_exists(char *path, int64_t path_len) {
    char tmp[4097];
    memcpy(tmp, path, path_len);
    tmp[path_len] = 0;
    FILE *f = fopen(tmp, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int64_t hylian_file_size(char *path, int64_t path_len) {
    char tmp[4097];
    memcpy(tmp, path, path_len);
    tmp[path_len] = 0;
    FILE *f = fopen(tmp, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    int64_t size = ftell(f);
    fclose(f);
    return size;
}
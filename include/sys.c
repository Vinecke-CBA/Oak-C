#include <stdio.h>
#include <stdlib.h>


void *sys_open(const char *path, const char *mode) {
    return fopen(path, mode);
}

int sys_close(void *f) {
    return fclose((FILE *)f);
}

int sys_fprint(void *f, const char *s) {
    return fprintf((FILE *)f, "%s", s);
}

char *sys_fread(void *f) {
    FILE *file = (FILE *)f;

    if (fseek(file, 0, SEEK_END) != 0) {
        return NULL;
    }

    long length = ftell(file);
    if (length < 0) {
        return NULL;
    }


    rewind(file);

    char *buffer = malloc((size_t)length + 1);

    if (!buffer) {
        return NULL;
    }

    size_t read_bytes = fread(buffer, 1, (size_t)length, file);
    buffer[read_bytes] = '\0';

    return buffer;

}


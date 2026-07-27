#include "hermas2/image.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("usage: hermas2_image_check IMAGE\n", stderr);
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return 2;
    }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 1;
    }
    uint8_t *bytes = malloc((size_t)length);
    if (bytes == NULL ||
        fread(bytes, 1u, (size_t)length, file) !=
            (size_t)length) {
        free(bytes);
        fclose(file);
        return 2;
    }
    fclose(file);
    hermas2_image_result result =
        hermas2_image_validate(bytes, (size_t)length, NULL);
    free(bytes);
    if (result != HERMAS2_IMAGE_OK) {
        fprintf(stderr, "%s\n", hermas2_image_result_name(result));
        return 1;
    }
    return 0;
}

#ifndef VOIDNSRUN_MACROS_H
#define VOIDNSRUN_MACROS_H

#include <stdio.h>
#include <string.h>

extern bool g_verbose;

#define ARRAY_SIZE(x)  (sizeof(x) / sizeof((x)[0]))
#define UNUSED(x)      (void)(x)
#define ERROR(f_, ...) fprintf(stderr, (f_), ##__VA_ARGS__)
#define DEBUG(f_, ...) if (g_verbose) {       \
        fprintf(stderr, "debug: ");           \
        fprintf(stderr, (f_), ##__VA_ARGS__);  \
    }

#define ERROR_EXIT(f_, ...) { \
        fprintf(stderr, (f_), ##__VA_ARGS__); \
        goto end; \
    }

#define SOCK_DIR_PATH_MAX (108 - strlen(SOCK_NAME) - 1)

#endif //VOIDNSRUN_MACROS_H

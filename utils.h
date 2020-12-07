#ifndef VOIDNSRUN_UTILS_H
#define VOIDNSRUN_UTILS_H

#include <stdbool.h>
#include "config.h"

struct strarray {
    size_t end;
    size_t size;
    char **list;
};

struct intarray {
    size_t end;
    size_t size;
    int *list;
};

bool isdir(const char *s);
bool isexe(const char *s);
bool exists(const char *s);
bool mkfile(const char *s);

int send_fd(int sock, int fd);
int recv_fd(int sock);

bool isxbpscommand(const char *s);

void strarray_alloc(struct strarray *a, size_t size);
bool strarray_append(struct strarray *a, char *s);

void intarray_alloc(struct intarray *i, size_t size);
bool intarray_append(struct intarray *a, int i);

#endif //VOIDNSRUN_UTILS_H

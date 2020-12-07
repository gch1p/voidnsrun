#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include "macros.h"
#include "utils.h"

bool isdir(const char *s)
{
    struct stat st;
    int result = stat(s, &st);
    if (result == -1)
        ERROR("stat(%s): %s\n", s, strerror(errno));
    return result == 0 && S_ISDIR(st.st_mode);
}

bool isexe(const char *s)
{
    struct stat st;
    int result = stat(s, &st);
    if (result == -1)
        ERROR("stat(%s): %s\n", s, strerror(errno));
    return result == 0 && !S_ISDIR(st.st_mode) && st.st_mode & S_IXUSR;
}

bool exists(const char *s)
{
    struct stat st;
    return stat(s, &st) == 0;
}

bool mkfile(const char *s)
{
    int fd;
    if ((fd = creat(s, 0700)) == -1)
        return false;
    close(fd);
    return true;
}

int send_fd(int sock, int fd)
{
    struct msghdr msg = {0};
    struct iovec iov[1];
    struct cmsghdr *cmsg = NULL;
    char ctrl_buf[CMSG_SPACE(sizeof(int))];
    char data[1];

    memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));

    data[0] = ' ';
    iov[0].iov_base = data;
    iov[0].iov_len = sizeof(data);

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_controllen =  CMSG_SPACE(sizeof(int));
    msg.msg_control = ctrl_buf;

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));

    *((int *)CMSG_DATA(cmsg)) = fd;

    return sendmsg(sock, &msg, 0);
}

int recv_fd(int sock)
{
    struct msghdr msg = {0};
    struct iovec iov[1];
    struct cmsghdr *cmsg = NULL;
    char ctrl_buf[CMSG_SPACE(sizeof(int))];
    char data[1];

    memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));

    iov[0].iov_base = data;
    iov[0].iov_len = sizeof(data);

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_control = ctrl_buf;
    msg.msg_controllen = CMSG_SPACE(sizeof(int));
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    recvmsg(sock, &msg, 0);

    cmsg = CMSG_FIRSTHDR(&msg);

    return *((int *) CMSG_DATA(cmsg));
}

bool isxbpscommand(const char *s)
{
    const char *commands[] = {
        "/xbps-install",
        "/xbps-remove",
        "/xbps-reconfigure"
    };
    for (size_t i = 0; i < ARRAY_SIZE(commands); i++) {
        const char *command = commands[i];
        if (!strcmp(s, command+1))
            return true;
        char *slash = strrchr(s, '/');
        if (slash && !strcmp(slash, command))
            return true;
    }
    return false;
}

bool strarray_append(struct strarray *a, char *s)
{
    if (a->end == a->size - 1)
        return false;
    else
        a->list[a->end++] = s;
    return true;
}

bool intarray_append(struct intarray *a, int i)
{
    if (a->end == a->size - 1)
        return false;
    else
        a->list[a->end++] = i;
    return true;
}

void strarray_alloc(struct strarray *a, size_t size)
{
    a->end = 0;
    a->size = size;
    a->list = malloc(sizeof(char *) * size);
    assert(a->list != NULL);
}

void intarray_alloc(struct intarray *i, size_t size)
{
    i->end = 0;
    i->size = size;
    i->list = malloc(sizeof(int) * size);
    assert(i->list != NULL);
}
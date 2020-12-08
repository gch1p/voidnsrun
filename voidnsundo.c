#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <stdbool.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/limits.h>

#include "config.h"
#include "utils.h"
#include "macros.h"

bool g_verbose = false;

void usage(const char *progname)
{
    printf("Usage: %s [OPTIONS] PROGRAM [ARGS]\n", progname);
    printf("\n"
           "Options:\n"
           "    -V:  Enable verbose output.\n"
           "    -h:  Print this help.\n"
           "    -v:  Print version.\n");
}

int main(int argc, char **argv)
{
    bool binded = strcmp(basename(argv[0]), VOIDNSUNDO_NAME) != 0;
    int c;
    int sock_fd = -1;
    int exit_code = 1;
    char realpath_buf[PATH_MAX];
    char cwd[PATH_MAX];
    if (!binded) {
        if (argc < 2) {
            usage(argv[0]);
            return 0;
        }

        while ((c = getopt(argc, argv, "vhs:V")) != -1) {
            switch (c) {
            case 'v':
                printf("%s\n", PROG_VERSION);
                return 0;
            case 'h':
                usage(argv[0]);
                return 0;
            case 'V':
                g_verbose = true;
                break;
            case '?':
                return 1;
            }
        }

        if (!argv[optind]) {
            usage(argv[0]);
            return 1;
        }
    } else {
        int bytes = readlink("/proc/self/exe", realpath_buf, PATH_MAX);
        realpath_buf[bytes] = '\0';
        /* DEBUG("/proc/self/exe points to %s\n", realpath_buf); */
    }

    /* Get current working directory. */
    getcwd(cwd, PATH_MAX);
    DEBUG("cwd=%s\n", cwd);

    /* Get namespace's fd. */
    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1)
        ERROR_EXIT("socket: %s.\n", strerror(errno));

    struct sockaddr_un sock_addr = {0};
    sock_addr.sun_family  = AF_UNIX;

    /* The size of sun_path is 108 bytes, our SOCK_PATH is definitely
     * smaller. */
    strcpy(sock_addr.sun_path, SOCK_PATH);

    if (connect(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) == -1)
        ERROR_EXIT("connect: %s\n", strerror(errno));

    int nsfd = recv_fd(sock_fd);
    if (!nsfd)
        ERROR_EXIT("error: failed to get nsfd.\n");

    /* Change namespace. */
    if (setns(nsfd, CLONE_NEWNS) == -1)
        ERROR_EXIT("setns: %s.\n", strerror(errno));

    /* Drop root. */
    uid_t uid = getuid();
    gid_t gid = getgid();

    if (setreuid(uid, uid) == -1)
        ERROR_EXIT("setreuid: %s\n", strerror(errno));

    if (setregid(gid, gid) == -1)
        ERROR_EXIT("setregid: %s\n", strerror(errno));

    /* Restore working directory. */
    if (chdir(cwd) == -1)
        DEBUG("chdir: %s\n", strerror(errno));

    /* Launch program. */
    int argind = binded ? 0 : optind;
    if (binded)
        argv[0] = realpath_buf;
    if (execvp(argv[argind], (char *const *)argv+argind) == -1)
        ERROR_EXIT("execvp(%s): %s\n", argv[argind], strerror(errno));

    exit_code = 0;

end:
    if (sock_fd != -1)
        close(sock_fd);

    return exit_code;
}
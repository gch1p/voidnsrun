#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <stdbool.h>
#include <getopt.h>
#include <dirent.h>
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
           "    -s:        Socket directory path. When this option is not present,\n"
           "               " SOCK_DIR_VAR " environment variable is used. If both are\n"
           "               missing, defaults to " SOCK_DIR_DEFAULT ".\n"
           "    -V:        Verbose output.\n"
           "    -h:        Print this help.\n"
           "    -v:        Print version.\n");
}

int main(int argc, char **argv)
{
    bool binded = strcmp(basename(argv[0]), VOIDNSUNDO_NAME) != 0;
    int c;
    char *sock_dir = NULL;
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
            case 's':
                sock_dir = optarg;
                break;
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

    /* Check socket directory. */
    DIR *dirptr = NULL;
    if (!sock_dir)
        sock_dir = getenv(SOCK_DIR_VAR);
    if (!sock_dir)
        sock_dir = SOCK_DIR_DEFAULT;
    if (strlen(sock_dir) > SOCK_DIR_PATH_MAX)
        ERROR_EXIT("error: socket directory path is too long.\n");
    if (!isdir(sock_dir))
        ERROR_EXIT("error: %s is not a directory.\n", sock_dir);
    if (access(sock_dir, F_OK) == -1) {
        ERROR_EXIT("error: failed to access socket directory: %s.\n",
                   strerror(errno));
    } else {
        if ((dirptr = opendir(sock_dir)) == NULL)
            ERROR_EXIT("error: %s is not a directory.\n", sock_dir);
    }
    DEBUG("sock_dir=%s\n", sock_dir);

    /* Get current working directory. */
    getcwd(cwd, PATH_MAX);
    DEBUG("cwd=%s\n", cwd);

    /* Get namespace's fd. */
    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1)
        ERROR_EXIT("socket: %s.\n", strerror(errno));

    struct sockaddr_un sock_addr = {0};
    sock_addr.sun_family  = AF_UNIX;
    strcpy(sock_addr.sun_path, sock_dir);
    strcat(sock_addr.sun_path, SOCK_NAME);

    if (connect(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) == -1)
        ERROR_EXIT("connect: %s\n", strerror(errno));

    int nsfd = recv_fd(sock_fd);
    if (!nsfd)
        ERROR_EXIT("error: failed to get nsfd.\n");

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
    if (dirptr != NULL)
        closedir(dirptr);

    if (sock_fd != -1)
        close(sock_fd);

    return exit_code;
}
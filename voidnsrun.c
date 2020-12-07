#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <stdbool.h>
#include <dirent.h>
#include <signal.h>
#include <libgen.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/limits.h>

#include "config.h"
#include "utils.h"
#include "macros.h"

volatile sig_atomic_t term_caught = 0;
bool g_verbose = false;

void usage(const char *progname)
{
    printf("Usage: %s [OPTIONS] PROGRAM [ARGS]\n", progname);
    printf("\n"
            "Options:\n"
            "    -r <path>: Container path. When this option is not present,\n"
            "               " CONTAINER_DIR_VAR " environment variable is used.\n"
            "    -m <path>: Add bind mount. You can add up to %d paths.\n"
            "    -u <path>: Add undo utility bind mount. You can add up to %d paths.\n"
            "    -U <path>: Undo program path. When this option is not present,\n"
            "               " UNDO_BIN_VAR " environment variable is used.\n"
            "    -i:        Don't treat missing source or target for an added mount\n"
            "               as an error.\n"
            "    -V:        Verbose output.\n"
            "    -h:        Print this help.\n"
            "    -v:        Print version.\n",
           USER_LISTS_MAX, USER_LISTS_MAX);
}

size_t mount_dirs(const char *source_prefix, size_t source_prefix_len, struct strarray *targets)
{
    char buf[PATH_MAX];
    int successful = 0;
    for (size_t i = 0; i < targets->end; i++) {
        /* Check if it's safe to proceed. */
        if (source_prefix_len + strlen(targets->list[i]) >= PATH_MAX) {
            ERROR("error: path %s%s is too large.\n", source_prefix, targets->list[i]);
            continue;
        }

        strcpy(buf, source_prefix);
        strcat(buf, targets->list[i]);
        if (!isdir(buf)) {
            ERROR("error: source mount dir %s does not exists.\n", buf);
            continue;
        }

        if (!isdir(targets->list[i])) {
            ERROR("error: mount point %s does not exists.\n", targets->list[i]);
            continue;
        }

        if (mount(buf, targets->list[i], NULL, MS_BIND|MS_REC, NULL) == -1)
            ERROR("mount: failed to mount %s: %s\n", targets->list[i], strerror(errno));
        else
            successful++;
    }
    return successful;
}

size_t mount_undo(const char *source, const struct strarray *targets, struct intarray *created)
{
    int successful = 0;
    for (size_t i = 0; i < targets->end; i++) {
        if (!exists(targets->list[i])) {
            if (mkfile(targets->list[i]))
                intarray_append(created, i);
            else
                continue;
        }

        DEBUG("%s: source=%s, target=%s\n", __func__, source, targets->list[i]);
        if (mount(source, targets->list[i], NULL, MS_BIND, NULL) == -1)
            ERROR("mount: failed to mount %s to %s: %s",
                 source, targets->list[i], strerror(errno));
        else
            successful++;
    }
    return successful;
}

void onterm(int sig)
{
    UNUSED(sig);
    term_caught = 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 0;
    }

    int nsfd = -1;
    char *dir = NULL;
    char buf[PATH_MAX];
    char *undo_bin = NULL;
    int sock_fd = -1, sock_conn = -1;
    size_t dirlen;
    int c;
    int exit_code = 1;
    DIR *dirptr = NULL;
    bool ignore_missing = false;
    bool forked = false;
    pid_t pid = 0;
    char cwd[PATH_MAX];

    struct strarray user_mounts;
    strarray_alloc(&user_mounts, USER_LISTS_MAX);

    struct strarray undo_mounts;
    strarray_alloc(&undo_mounts, USER_LISTS_MAX);

    struct intarray tounlink;
    intarray_alloc(&tounlink, USER_LISTS_MAX);

    while ((c = getopt(argc, argv, "vhm:r:u:U:iV")) != -1) {
        switch (c) {
        case 'v':
            printf("%s\n", PROG_VERSION);
            return 0;
        case 'h':
            usage(argv[0]);
            return 0;
        case 'i':
            ignore_missing = true;
            break;
        case 'r':
            dir = optarg;
            break;
        case 'U':
            undo_bin = optarg;
            break;
        case 'V':
            g_verbose = true;
            break;
        case 'm':
            if (!strarray_append(&user_mounts, optarg))
                ERROR_EXIT("error: only up to %lu user mounts allowed.\n",
                           user_mounts.size);
            break;
        case 'u':
            if (!strarray_append(&undo_mounts, optarg))
                ERROR_EXIT("error: only up to %lu user mounts allowed.\n",
                           undo_mounts.size);
            break;
        case '?':
            return 1;
        }
    }

    if (!argv[optind]) {
        usage(argv[0]);
        return 1;
    }

    /* Get container path. */
    if (!dir)
        dir = getenv(CONTAINER_DIR_VAR);
    if (!dir)
        ERROR_EXIT("error: environment variable %s not found.\n",
             CONTAINER_DIR_VAR);

    /* Validate it. */
    if (!isdir(dir))
        ERROR_EXIT("error: %s is not a directory.\n", dir);

    dirlen = strlen(dir);

    DEBUG("dir=%s\n", dir);

    /* Get undo binary path, if needed. */
    if (undo_mounts.end > 0) {
        if (!undo_bin)
            undo_bin = getenv(UNDO_BIN_VAR);
        if (!undo_bin) {
            ERROR_EXIT("error: environment variable %s not found.\n",
                UNDO_BIN_VAR);
        } else if (strlen(undo_bin) > PATH_MAX)
            ERROR_EXIT("error: undo binary path is too long.\n");

        /* Validate it. */
        if (!isexe(undo_bin))
            ERROR_EXIT("error: %s is not an executable.\n", undo_bin);

        DEBUG("undo_bin=%s\n", undo_bin);
    }

    /* Get current namespace's file descriptor. */
    nsfd = open("/proc/self/ns/mnt", O_RDONLY);
    if (nsfd == -1)
        ERROR_EXIT("error: failed to acquire mount namespace's fd.%s\n",
                   strerror(errno));

    /* Check socket directory. */
    strncpy(buf, SOCK_PATH, PATH_MAX);
    char *sock_dir = dirname(buf);
    if (access(sock_dir, F_OK) == -1) {
        if (mkdir(sock_dir, 0700) == -1)
            ERROR_EXIT("error: failed to create %s directory.\n", sock_dir);
    } else {
        if ((dirptr = opendir(sock_dir)) == NULL)
            ERROR_EXIT("error: %s is not a directory.\n", sock_dir);
        if (exists(SOCK_PATH) && unlink(SOCK_PATH) == -1)
            ERROR_EXIT("failed to unlink %s: %s", SOCK_PATH, strerror(errno));
    }
    DEBUG("sock_dir=%s\n", sock_dir);

    /* Get current working directory. */
    getcwd(cwd, PATH_MAX);
    DEBUG("cwd=%s\n", cwd);

    /* Do the unshare magic. */
    if (unshare(CLONE_NEWNS) == -1)
        ERROR_EXIT("unshare: %s\n", strerror(errno));

    /* Mount stuff from the container to the namespace. */
    /* First, mount what user asked us to mount. */
    if (mount_dirs(dir, dirlen, &user_mounts) < user_mounts.end && !ignore_missing)
        ERROR_EXIT("error: some mounts failed.\n");

    /* Then necessary stuff. */
    struct strarray default_mounts;
    strarray_alloc(&default_mounts, 3);
    strarray_append(&default_mounts, "/usr");
    if (isxbpscommand(argv[optind])) {
        strarray_append(&default_mounts, "/var");
        strarray_append(&default_mounts, "/etc");
    }
    if (mount_dirs(dir, dirlen, &default_mounts) < default_mounts.end)
        ERROR_EXIT("error: some necessary mounts failed.\n");

    /* Bind mount undo binary. */
    if (mount_undo(undo_bin, &undo_mounts, &tounlink) < undo_mounts.end
            && !ignore_missing)
        ERROR_EXIT("error: some undo mounts failed.\n");

    /* Mount sock_dir as tmpfs. It will only be visible in this namespace. */
    if (mount("tmpfs", sock_dir, "tmpfs", 0, "size=4k,mode=0700,uid=0,gid=0") == -1)
        ERROR_EXIT("mount: error mounting tmpfs in %s.\n", sock_dir);

    /* Fork. */
    pid_t ppid_before_fork = getpid();
    pid = fork();
    if (pid == -1)
        ERROR_EXIT("fork: %s\n", strerror(errno));

    forked = true;

    if (pid == 0) {
        /* Catch SIGTERM. */
        struct sigaction sa = {0};
        sa.sa_handler = onterm;
        sigaction(SIGTERM, &sa, NULL);

        /* Ignore SIGINT. */
        signal(SIGINT, SIG_IGN);

        /* Set the child to get SIGTERM when parent thread dies. */
        int r = prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (r == -1)
            ERROR_EXIT("prctl: %s\n", strerror(errno));
        if (getppid() != ppid_before_fork)
            ERROR_EXIT("error: parent has died already.\n");

        /* Create unix socket. */
        sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock_fd == -1)
            ERROR_EXIT("socket: %s.\n", strerror(errno));

        struct sockaddr_un sock_addr = {0};
        sock_addr.sun_family = AF_UNIX;
        strncpy(sock_addr.sun_path, SOCK_PATH, 108);

        if (bind(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) == -1)
            ERROR_EXIT("bind: %s\n", strerror(errno));

        listen(sock_fd, 1);

        while (!term_caught) {
            sock_conn = accept(sock_fd, NULL, 0);
            if (sock_conn == -1)
                continue;
            send_fd(sock_conn, nsfd);
        }
    } else {
        /* Parent process. Drop root rights. */
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
        if (execvp(argv[optind], (char *const *)argv+optind) == -1)
            ERROR_EXIT("execvp(%s): %s\n", argv[optind], strerror(errno));
    }

    exit_code = 0;

end:
    if (nsfd != -1)
        close(nsfd);

    if (sock_fd != -1)
        close(sock_fd);

    if (sock_conn != -1)
        close(sock_conn);

    if (dirptr != NULL)
        closedir(dirptr);

    if (tounlink.end > 0 && (!forked || pid == 0)) {
        for (size_t i = 0; i < tounlink.end; i++) {
            char *path = undo_mounts.list[tounlink.list[i]];
            if (umount(path) == -1)
                DEBUG("umount(%s): %s\n", path, strerror(errno));
            if (unlink(path) == -1)
                ERROR("unlink(%s): %s\n", path, strerror(errno));
            else
                DEBUG("unlink(%s)\n", path);
        }
    }

    return exit_code;
}

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
            "    -u <path>: Add undo bind mount. You can add up to %d paths.\n"
            "    -d <path>: Add /usr subdirectory bind mount.\n"
            "    -U <path>: Path to " VOIDNSUNDO_NAME ". When this option is not present,\n"
            "               " UNDO_BIN_VAR " environment variable is used.\n"
            "    -i:        Don't treat missing source or target for added mounts as error.\n"
            "    -V:        Enable verbose output.\n"
            "    -h:        Print this help.\n"
            "    -v:        Print version.\n",
           USER_LISTS_MAX, USER_LISTS_MAX);
}

size_t mount_dirs(const char *source_prefix,
                  size_t source_prefix_len,
                  struct strarray *targets,
                  struct intarray *created)
{
    char buf[PATH_MAX];
    int successful = 0;
    mode_t mode;
    for (size_t i = 0; i < targets->end; i++) {
        /* Check if it's safe to proceed. */
        if (source_prefix_len + strlen(targets->list[i]) >= PATH_MAX) {
            ERROR("error: path %s%s is too large.\n", source_prefix, targets->list[i]);
            continue;
        }

        /* Should be safe as we just checked that total length of source_prefix
         * and targets->list[i] is no more than PATH_MAX-1. */
        strcpy(buf, source_prefix);
        strcat(buf, targets->list[i]);

        if (!isdir(buf)) {
            ERROR("error: source mount dir %s does not exists.\n", buf);
            continue;
        }

        if (!exists(targets->list[i])) {
            if (created != NULL) {
                mode = getmode(buf);
                if (mode == 0) {
                    ERROR("error: can't get mode for %s.\n", buf);
                    continue;
                }

                if (mkdir(targets->list[i], mode) == -1) {
                    ERROR("error: failed to create mountpotint at %s: %s.\n",
                          targets->list[i], strerror(errno));
                    continue;
                } else
                    intarray_append(created, i);
            } else {
                ERROR("error: mount dir %s does not exists.\n", buf);
                continue;
            }
        }

        if (!isdir(targets->list[i])) {
            ERROR("error: mount point %s is not a directory.\n", targets->list[i]);
            continue;
        }

        if (mount(buf, targets->list[i], NULL, MS_BIND|MS_REC, NULL) == -1)
            ERROR("mount: failed to mount %s: %s\n", targets->list[i], strerror(errno));
        else
            successful++;
    }
    return successful;
}

size_t mount_undo(const char *source,
                  const struct strarray *targets,
                  struct intarray *created)
{
    int successful = 0;
    for (size_t i = 0; i < targets->end; i++) {
        /* If the mount point does not exist, create an empty file, otherwise
         * mount() call will fail. In this case, remember which files we have
         * created to unlink() them before exit. */
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
    char buf[PATH_MAX*2];
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

    /* List of user-specified /usr subdirectories to mount. */
    struct strarray dir_mounts;
    strarray_alloc(&dir_mounts, USER_LISTS_MAX);

    /* List of indexes of items in the undo_mounts array. See comments in
     * mount_undo() function for more info. */
    struct intarray created_undos;
    intarray_alloc(&created_undos, USER_LISTS_MAX);

    /* List of indexes of items in the dir_mounts array. */
    struct intarray created_dirs;
    intarray_alloc(&created_dirs, USER_LISTS_MAX);

    while ((c = getopt(argc, argv, "vhm:r:u:U:iVd:")) != -1) {
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
        case 'd':
            if (!startswith(optarg, "/usr/"))
                ERROR_EXIT("only subdirectories of /usr are allowed for bind mounting this way.\n");
            if (!strarray_append(&dir_mounts, optarg))
                ERROR_EXIT("error: only up to %lu dir mounts allowed.\n",
                           dir_mounts.size);
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
    if (dirlen >= PATH_MAX)
        ERROR_EXIT("error: container's path is too long.\n");

    DEBUG("dir=%s\n", dir);

    /* Get voidnsundo path, if needed. */
    if (undo_mounts.end > 0) {
        if (!undo_bin)
            undo_bin = getenv(UNDO_BIN_VAR);
        if (!undo_bin) {
            ERROR_EXIT("error: environment variable %s not found.\n",
                UNDO_BIN_VAR);
        }

        size_t undo_bin_len = strlen(undo_bin);
        if (undo_bin_len >= PATH_MAX)
            ERROR_EXIT("error: undo binary path is too long.\n");

        /*
         * Check that it exists and it is an executable.
         * These strcpy and strcat calls should be safe, as we already know that
         * both dir and undo_bin are no longer than PATH_MAX-1 and the buf's size
         * is PATH_MAX*2.
         */
        strcpy(buf, dir);
        strcat(buf, undo_bin);
        if (!isexe(buf))
            ERROR_EXIT("error: %s is not an executable.\n", undo_bin);

        DEBUG("undo_bin=%s\n", undo_bin);
    }

    /* Get current namespace's file descriptor. It may be needed later
     * for voidnsundo. */
    nsfd = open("/proc/self/ns/mnt", O_RDONLY);
    if (nsfd == -1)
        ERROR_EXIT("error: failed to acquire mount namespace's fd.%s\n",
                   strerror(errno));

    /* Check socket directory. */
    /* TODO: fix invalid permissions, or just die in that case. */

    /* This should be safe, SOCK_PATH is hardcoded in config.h and it's definitely
     * smaller than buffer. */
    strcpy(buf, SOCK_PATH);

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

    /* Get current working directory. Will need to restore it later in the
     * new mount namespace. */
    getcwd(cwd, PATH_MAX);
    DEBUG("cwd=%s\n", cwd);

    /* Create new mount namespace. */
    if (unshare(CLONE_NEWNS) == -1)
        ERROR_EXIT("unshare: %s\n", strerror(errno));

    /* Mount stuff from the container to the namespace. */
    /* First, mount what user asked us to mount. */
    if (mount_dirs(dir, dirlen, &user_mounts, NULL) < user_mounts.end && !ignore_missing)
        ERROR_EXIT("error: some mounts failed.\n");

    /* Then preserve original /usr at /oldroot if needed. */
    if (dir_mounts.end > 0) {
        mode_t mode = getmode("/usr");
        if (mode == 0)
            ERROR_EXIT("error: failed to get mode of /usr.\n");

        if (mount("tmpfs", OLDROOT, "tmpfs", 0, "size=4k,mode=0700,uid=0,gid=0") == -1)
            ERROR_EXIT("mount: error mounting tmpfs in %s.\n", OLDROOT);

        strcpy(buf, OLDROOT);
        strcat(buf, "/usr");
        if (mkdir(buf, mode) == -1)
            ERROR_EXIT("error: failed to mkdir %s: %s.\n", buf, strerror(errno));

        if (mount("/usr", buf, NULL, MS_BIND|MS_REC, NULL) == -1)
            ERROR_EXIT("error: failed to mount /usr at %s: %s.",
                       buf, strerror(errno));
    }

    /* Then the necessary stuff. */
    struct strarray default_mounts;
    strarray_alloc(&default_mounts, 3);
    strarray_append(&default_mounts, "/usr");
    if (isxbpscommand(argv[optind])) {
        strarray_append(&default_mounts, "/var");
        strarray_append(&default_mounts, "/etc");
    }
    if (mount_dirs(dir, dirlen, &default_mounts, NULL) < default_mounts.end)
        ERROR_EXIT("error: some necessary mounts failed.\n");

    /* Mount /usr subdirectories if needed. */
    if (dir_mounts.end > 0
            && mount_dirs(OLDROOT, strlen(OLDROOT), &dir_mounts, &created_dirs) < dir_mounts.end)
        ERROR_EXIT("error: some dir mounts failed.\n");

    /* Now lets do bind mounts of voidnsundo (if needed). */
    if (mount_undo(undo_bin, &undo_mounts, &created_undos) < undo_mounts.end
            && !ignore_missing)
        ERROR_EXIT("error: some undo mounts failed.\n");

    /* Mount socket directory as tmpfs. It will only be visible in this namespace,
     * and the socket file will also be available from this namespace only.*/
    if (mount("tmpfs", sock_dir, "tmpfs", 0, "size=4k,mode=0700,uid=0,gid=0") == -1)
        ERROR_EXIT("mount: error mounting tmpfs in %s.\n", sock_dir);

    /*
     * Fork. We need it because we need to preserve file descriptor of the
     * original namespace.
     *
     * Linux doesn't allow to bind mount /proc/self/ns/mnt from the original
     * namespace in the child namespace because that would lead to dependency
     * loop. So I came up with another solution.
     *
     * Unix sockets are capable of passing file descriptors. We need to start a
     * server that will listen on a unix socket and pass the namespace's file
     * descriptor to connected clients over this socket. voidnsundo will connect
     * to the socket, receive the file descriptor and perform the setns() system
     * call.
     *
     * We also need to make sure the socket will only be accessible by root.
     * The path to the socket should be hardcoded.
     *
     * So we fork(), start the server in the child process, while the parent
     * drops root privileges and runs the programs it was asked to run.
     */
    pid_t ppid_before_fork = getpid();
    pid = fork();
    if (pid == -1)
        ERROR_EXIT("fork: %s\n", strerror(errno));

    forked = true;

    if (pid == 0) {
        /* This is the child process.
         * Catch SIGTERM: it will be sent here when parent dies. The signal will
         * interrupt the accept() call, so we can clean up and exit immediately.
         */
        struct sigaction sa = {0};
        sa.sa_handler = onterm;
        sigaction(SIGTERM, &sa, NULL);

        /* Ignore SIGINT. Otherwise it will be affected by Ctrl+C in the parent
         * process. */
        signal(SIGINT, SIG_IGN);

        /* Set the child to get SIGTERM when parent thread dies. */
        int r = prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (r == -1)
            ERROR_EXIT("prctl: %s\n", strerror(errno));

        /* Maybe it already has died? */
        if (getppid() != ppid_before_fork)
            ERROR_EXIT("error: parent has died already.\n");

        /* Create unix socket. */
        sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sock_fd == -1)
            ERROR_EXIT("socket: %s.\n", strerror(errno));

        struct sockaddr_un sock_addr = {0};
        sock_addr.sun_family = AF_UNIX;

        /* The size of sun_path is 108 bytes, our SOCK_PATH is definitely
         * smaller. */
        strcpy(sock_addr.sun_path, SOCK_PATH);

        if (bind(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) == -1)
            ERROR_EXIT("bind: %s\n", strerror(errno));

        listen(sock_fd, 1);

        /* Accept incoming connections until SIGTERM. */
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

    if (!forked || pid == 0) {
        /* If we created some empty files to bind the voidnsundo utility,
         * delete them here. */
        if (created_undos.end > 0) {
            for (size_t i = 0; i < created_undos.end; i++) {
                char *path = undo_mounts.list[created_undos.list[i]];
                if (umount(path) == -1)
                    DEBUG("umount(%s): %s\n", path, strerror(errno));
                if (unlink(path) == -1)
                    ERROR("unlink(%s): %s\n", path, strerror(errno));
                else
                    DEBUG("unlink(%s)\n", path);
            }
        }

        /* If we had to create mount tmpfs to /oldroot and do other
         * dirty hacks related to /usr subdirs bind mounting, clean up here. */
        if (dir_mounts.end > 0) {
            for (size_t i = 0; i < dir_mounts.end; i++) {
                char *path = dir_mounts.list[i];
                if (umount(path) == -1)
                    ERROR("umount(%s): %s\n", path, strerror(errno));
            }

            /* If we created some empty dirs to use them as mountpoints for
             * bind mounts, delete them here. */
            if (created_dirs.end > 0) {
                for (size_t i = 0; i < created_dirs.end; i++) {
                    char *path = dir_mounts.list[created_dirs.list[i]];
                    if (rmdir(path) == -1)
                        ERROR("rmdir(%s): %s\n", path, strerror(errno));
                    else
                        DEBUG("rmdir(%s)\n", path);
                }
            }

            strcpy(buf, OLDROOT);
            strcat(buf, "/usr");
            if (umount(buf) == -1)
                ERROR("umount(%s): %s\n", buf, strerror(errno));

            /* This call always fails with EBUSY and I don't know why.
             * We can safely ignore any errors here (I hope) because
             * the mount namespace will be destroyed as soon as there
             * will be no processes attached to it. */
            umount(OLDROOT);
            /*if (umount(OLDROOT) == -1)
                ERROR("umount(%s): %s\n", OLDROOT, strerror(errno));*/
        }
    }

    return exit_code;
}

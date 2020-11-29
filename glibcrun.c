/**
 * Copyright (c) 2020 Evgeny Zinoviev <me@ch1p.io>. 
 * 
 * This program is licensed under the BSD 2-Clause License.
 * https://opensource.org/licenses/BSD-2-Clause
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <linux/limits.h>

const char *var_name = "GLIBCRUN_DIR";

#define ARRAY_SIZE(x)  (sizeof(x) / sizeof((x)[0]))
#define ERROR(f_, ...) fprintf(stderr, (f_), ##__VA_ARGS__)

int main(int argc, char *argv[]) {
    int result;

    /* Get glibc base path */
    const char *dir = getenv(var_name);
    if (!dir) {
        ERROR("error: environment variable %s not found.\n", var_name);
        return 1;
    }

    /* Validate it */
    struct stat st;
    result = stat(dir, &st);
    if (result != 0 || !S_ISDIR(st.st_mode)) {
        ERROR("error: %s is not a directory.\n", dir);
        return 1;
    }

    /* Get shell from current env to reuse it if needed */
    const char *shell = getenv("SHELL");
    if (!shell)
        shell = "/bin/sh";

    /* Do the unshare magic */
    result = unshare(CLONE_NEWNS);
    if (result == -1) {
        ERROR("unshare: %s\n", strerror(errno));
        return 1;
    }

    /* Mount glibc stuff to our private namespace */
    const char *const mountpoints[] = {"/usr", "/var/db/xbps"};
    char buf[PATH_MAX];

    for (int i = 0; i < (int)ARRAY_SIZE(mountpoints); i++) {
        strcpy(buf, dir);
        strcat(buf, mountpoints[i]);
        result = mount(buf, mountpoints[i], NULL, MS_BIND|MS_REC, NULL);
        if (result == -1) {
            ERROR("mount(%s): %s\n", mountpoints[i], strerror(errno));
            return 1;
        }
    }

    /* Drop root */
    uid_t uid = getuid();
    gid_t gid = getgid();

    result = setreuid(uid, uid);
    if (result == -1) {
        ERROR("setreuid: %s\n", strerror(errno));
        return 1;
    }

    result = setregid(gid, gid);
    if (result == -1) {
        ERROR("setregid: %s\n", strerror(errno));
        return 1;
    }

    /* Launch program or shell */
    const char *exec_cmd;
    char *const *exec_args = NULL;
    if (argc < 2)
        exec_cmd = shell;
    else {
        exec_cmd = argv[1];
        exec_args = (char *const *)argv+1;
    }

    result = execvp(exec_cmd, exec_args);
    if (result == -1) {
        ERROR("execvp(%s): %s\n", exec_cmd, strerror(errno));
        return 1;
    }

    return 0;
}

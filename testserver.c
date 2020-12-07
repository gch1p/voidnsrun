#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>
#include "utils.h"

#define ERROR(f_, ...) fprintf(stderr, (f_), ##__VA_ARGS__)
#define UNUSED(x)      (void)(x)
#define ERROR_EXIT(f_, ...) { \
		fprintf(stderr, (f_), ##__VA_ARGS__); \
		return 1; \
	}

volatile sig_atomic_t term_caught = 0;
void onterm(int sig)
{
	printf("sigterm caught\n");
	UNUSED(sig);
	term_caught = 1;
}

int main()
{
	int result;
	int sock_fd, sock_conn;
	int nsfd;

	/* Get current namespace's file descriptor. */
	nsfd = open("/proc/self/ns/mnt", O_RDONLY);
	if (nsfd == -1)
		ERROR_EXIT("error: failed to acquire mount namespace's fd.%s\n",
				   strerror(errno));

	/* Fork. */
	pid_t ppid_before_fork = getpid();
	pid_t pid = fork();
	if (pid == -1)
		ERROR_EXIT("fork: %s\n", strerror(errno));

	if (pid == 0) {
		/* Catch SIGTERM. */
		struct sigaction sa = {0};
		sa.sa_handler = onterm;
		sigaction(SIGTERM, &sa, NULL);

        /* Ignore SIGINT. */
        signal(SIGINT, SIG_IGN);

		/* Set the child to die when parent thread dies. */
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
		strcpy(&sock_addr.sun_path[1], "/tmp/voidnsrun-test.sock");

		if (bind(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) == -1)
			ERROR_EXIT("bind: %s\n", strerror(errno));

		listen(sock_fd, 1);

		while (!term_caught) {
			sock_conn = accept(sock_fd, NULL, 0);
			if (sock_conn == -1) {
				ERROR("accept: %s\n", strerror(errno));
				continue;
			}
			printf("accepted\n");
			send_fd(sock_conn, nsfd);
		}
		printf("exiting\n");
	} else {
		/* This is parent. Launch a program. */
		char *argv[2] = {"/bin/sh", NULL};

		result = execvp(argv[0], (char *const *)argv);
		if (result == -1)
			ERROR_EXIT("execvp: %s\n", strerror(errno));
	}

	return 0;
}
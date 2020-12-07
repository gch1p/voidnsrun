#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include "utils.h"

#define ERROR_EXIT(f_, ...) { \
		fprintf(stderr, (f_), ##__VA_ARGS__); \
		return 1; \
	}

int main()
{
	struct sockaddr_un addr = {0};
	int sock;

	// Create and connect a unix domain socket
	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock == -1)
		ERROR_EXIT("socket: %s\n", strerror(errno));

	addr.sun_family  = AF_UNIX;
	strcpy(&addr.sun_path[1], "/tmp/voidnsrun-test.sock");

	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
		ERROR_EXIT("connect: %s\n", strerror(errno));

	int fd = recv_fd(sock);
	close(sock);

	assert(fd != 0);

	struct stat st;
	if (fstat(fd, &st) == -1)
		ERROR_EXIT("stat: %s\n", strerror(errno));

	printf("st_ino: %lu\n", st.st_ino);

	return 0;
}
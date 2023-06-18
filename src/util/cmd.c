// -*- mode: c; tab-width: 4; indent-tabs-mode: 1; st-rulers: [70] -*-
// vim: ts=8 sw=8 ft=c noet

#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "cmd.h"

int run_cmd(char* argv[])
{
	int status = -1;
	int ret = -1;
	pid_t child = 0;
	if ((child = fork()) == 0) {
		execvp(argv[0],argv);
		exit(1);
	} else if (child < 0) {
		return -1;
	}
	do {
		ret = waitpid(child, &status, 0);
	} while ( ret == -1 && errno == EINTR);

	return status;
}


/* vim: set sw=4 sts=4 : */

/*
 * Copyright (c) 2007-2011, David B. Cortarello
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice
 *     and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice
 *     and the following disclaimer in the documentation and/or other materials
 *     provided with the distribution.
 *   * Neither the name of Kwort nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
*/

#include "config.h"
#ifdef SUDOPATH

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <pwd.h>

#if defined(__FreeBSD__)
#include <libutil.h>
#include <utmpx.h>
#else
#include <pty.h>
#include <utmp.h>
#endif

#include <termios.h>
#include <fcntl.h>

#include "errors.h"

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

#define BUFF_SIZE 1024

static struct termios orig_termios;

/* Check user and password */
int check_password_sudo(const char *username, const char *password)
{
	int status = 0, pid = 0, pip[2];
	char *cmd[10] = { SUDOPATH, "-u", (char *)username, "-k", "-S", "-p", "", "-E", "true", NULL };
	char pass[64];

	if (pipe(pip)) err(1, "pipe()");

	if ((pid = fork()) < 0) err(1, "fork()");
	else if (pid == 0) {
		close(pip[1]);
		status = open("/dev/null", O_WRONLY);
		dup2(status, STDERR_FILENO);
		close(status);
		dup2(pip[0], STDIN_FILENO);
		execv(cmd[0], cmd);
		close(pip[0]);
		err(1, "execv()");
	}
	close(pip[0]);
	sprintf(pass, "%s\n", password);
	write(pip[1], pass, strlen(pass));
	close(pip[1]);
	
	waitpid(pid, &status, 0);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return ERR_SUCCESS;
	else
		if (WIFSIGNALED(status)) {
			fprintf(stderr, "Why I was signaled?: %d\n", WTERMSIG(status));
			return -1;
		}
	return ERR_WRONG_USER_OR_PASSWD;
}


/* Run the given command as the given user */
void run_sudo(char *username, char *password, char *command)
{
	char buf[BUFF_SIZE], *cmd[10] = { SUDOPATH, "-u", (char *)username, "-k", "-S", "-p", "", "-E", command, NULL };
	int fdpty = 0, status = 0, tty = 1, i = 0;
	int pip[2];
	pid_t pid;
	fd_set rfds;
	struct timeval tv;

	char pass[64];

	if (pipe(pip)) err(1, "pipe()");

	/* Creates a new terminal */
	if ((pid = forkpty(&fdpty, NULL, NULL, NULL)) < 0) err(1, "forkpty()");
	else if (pid == 0) {
		setsid();

		close(pip[1]);
		dup2(pip[0], STDIN_FILENO);
		execv(cmd[0], cmd);
		close(pip[0]);

		err(1, "execv()");
		exit(1);
	}

    close(pip[0]);
    sprintf(pass, "%s\n", password);
    write(pip[1], pass, strlen(pass));
    close(pip[1]);

	/* Put the terminal in raw mode */
	if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) {
		if (errno == ENOTTY)
			errno = tty = 0;
		else
			err(1, "tcgetattr()");
	}
	if (tty)
		tty_raw(STDIN_FILENO);

	/*read(fdpty, buf, BUFF_SIZE);*/
	while (!waitpid(pid, &status, WNOHANG)) {

		/* Ok, the program needs some interaction, so this will do it fine */
		tv.tv_sec = 0;
		tv.tv_usec = 10;
		FD_ZERO(&rfds);
		FD_SET(fdpty, &rfds);
		FD_SET(STDIN_FILENO, &rfds);

		if (select(MAX(fdpty, STDIN_FILENO)+1, &rfds, NULL, NULL, &tv) < 0) err(1, "select()");

		if (FD_ISSET(fdpty, &rfds)) {
			status = read(fdpty, buf, BUFF_SIZE);
			write(STDOUT_FILENO, buf, status);
		}
		else if (FD_ISSET(STDIN_FILENO, &rfds)) {
			status = read(STDIN_FILENO, buf, BUFF_SIZE);
			write(fdpty, buf, status);
		}
		usleep(100);
	}

	close(fdpty);

	if (tty)
	    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) < 0) 
			err(1, "tcsetattr()");
}

#endif

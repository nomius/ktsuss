
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
#ifdef SUPATH

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
#include <sys/signal.h>
#else
#include <pty.h>
#include <utmp.h>
#endif
#include <termios.h>

#include "errors.h"

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

#define BUFF_SIZE 1024

static struct termios orig_termios;

/* Finalize the su call */
static void end_su(int fd)
{
	char buf[BUFF_SIZE];
	fd_set rfds;
	struct timeval tv;

	/* If it just ended, let's check if there something in the buffers, blame coreutils su for this ugly hack */
	tv.tv_sec = 0;
	tv.tv_usec = 100;
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	if (select(fd+1, &rfds, NULL, NULL, &tv) < 0)
		err(1, "select()");
	if (FD_ISSET(fd, &rfds))
		read(fd, buf, BUFF_SIZE);
}


/* Finalize the su call */
static int init_su(int *fdpty, const char *username, const char *password, char *cmd[])
{
	int status = 1, i = 0;
	char buf[BUFF_SIZE];
	pid_t pid;
	fd_set rfds;
	struct timeval tv;

	/* Creates a new terminal */
	if ((pid = forkpty(fdpty, NULL, NULL, NULL)) < 0) err(1, "forkpty()");
	else if (pid == 0) {
		setsid();
		signal(SIGHUP, SIG_IGN);
		execv(cmd[0], cmd);
		err(1, "execv()");
		exit(1);
	}

	/* Read the "Password:" prompt */
	for (i = 500; i && status; i--) {
		tv.tv_sec = 0;
		tv.tv_usec = 10;
		FD_ZERO(&rfds);
		FD_SET(*fdpty, &rfds);
		if (select(*fdpty + 1, &rfds, NULL, NULL, &tv) < 0)
			err(1, "select()");
		if (FD_ISSET(*fdpty, &rfds)) {
			read(*fdpty, buf, BUFF_SIZE - 1);
			status = 0;
		}
		usleep(1000);
	}

	/* Check if we got a limit time */
	if (status) {
		kill(pid, SIGKILL);
		err(1, "No prompt given by the su command");
	}

	/* Send the password */
	snprintf(buf, 64, "%s\n", password);
	write(*fdpty, buf, strlen(buf));

	/* Read what's left in the buffers */
	end_su(*fdpty);

	return pid;
}


/* Check user and password */
int check_password_su(const char *username, const char *password)
{
	int fdpty = 0, status = 0, pid = 0;
#if defined(__FreeBSD__)
	char *cmd[6] = { SUPATH, (char *)username, "-m", "-c", "exit", NULL };
#else
	char *cmd[6] = { SUPATH, (char *)username, "-p", "-c", "exit", NULL };
#endif

	pid = init_su(&fdpty, username, password, cmd);

	waitpid(pid, &status, 0);

	end_su(fdpty);
	close(fdpty);

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
void run_su(char *username, char *password, char *command)
{
#if defined(__FreeBSD__)
	char buf[BUFF_SIZE], *cmd[6] = { SUPATH, username, "-m", "-c", command, NULL };
#else
	char buf[BUFF_SIZE], *cmd[6] = { SUPATH, username, "-p", "-c", command, NULL };
#endif
	int fdpty = 0, status = 0, tty = 1;
	pid_t pid = 0;
	fd_set rfds;
	struct timeval tv;

	pid = init_su(&fdpty, username, password, cmd);

	/* Put the terminal in raw mode */
	if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) {
		if (errno == ENOTTY)
			errno = tty = 0;
		else
			err(1, "tcgetattr()");
	}

	if (tty)
		tty_raw(STDIN_FILENO);

	while (1) {
		waitpid(pid, &status, WNOHANG);

		/* Ok, the program needs some interaction, so this will do it fine */
		tv.tv_sec = 0;
		tv.tv_usec = 10;
		FD_ZERO(&rfds);
		FD_SET(fdpty, &rfds);
		FD_SET(STDIN_FILENO, &rfds);

		if (select(MAX(fdpty, STDIN_FILENO)+1, &rfds, NULL, NULL, &tv) < 0) err(1, "select()");

		if (FD_ISSET(fdpty, &rfds)) {
			if ((status = read(fdpty, buf, BUFF_SIZE)) > 0)
				write(STDOUT_FILENO, buf, status);
			else
				break;

		}
		else if (FD_ISSET(STDIN_FILENO, &rfds)) {
			status = read(STDIN_FILENO, buf, BUFF_SIZE);
			write(fdpty, buf, status);
		}
		usleep(100);
	}

	end_su(fdpty);
	close(fdpty);

	if (tty)
	    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) < 0) 
			err(1, "tcsetattr()");
}

#endif

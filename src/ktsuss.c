
/* vim: set sw=4 sts=4 : */

/*
 * Copyright (c) 2007-2011, David B. Cortarello
 * Copyright (c) 2007-2011, Christian Dywan
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <pwd.h>
#include <pty.h>
#include <utmp.h>
#include <termios.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "config.h"
#include "errors.h"

#ifdef SUDOPATH
#include "sudo_backend.h"
#endif

#ifdef SUPATH
#include "su_backend.h"
#endif

static struct termios orig_termios;

/* Print's the help text to the terminal and exits with error */
void say_help(char *str)
{
	printf("Usage: %s [OPTION] <command>\n", str);
	printf("Run a command as another user\n\n");
	printf("OPTIONS:\n");
	printf("\t-v, --version        Gives ktsuss version info\n");
	printf("\t-u, --user USER      Runs the command as the given user\n");
	printf("\t-m, --message MESG   Change default message in ktsuss window\n");
	printf("\t-h, --help           Show this help\n");
	exit(1);
}


/* Print's the about text to the terminal and exits with no error */
void say_about(void)
{
	printf("%s - Copyright (c) 2007-2011 David B. Cortarello\n\n", PACKAGE_STRING);
	printf("Please report comments, suggestions and bugs to:\n\t%s\n\n", PACKAGE_BUGREPORT);
	printf("Check for new versions at:\n\thttp://nomius.blogspot.com\n\n");
	exit(0);
}


/* Creates a dialog with the given text error */
void Werror(int type, char *err_msg, int exit_true, int ret)
{
	GtkWidget *dialog_error;
	if (!err_msg)
		dialog_error = gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Could not run command");
	else
		dialog_error = gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, err_msg);

	gtk_window_set_title(GTK_WINDOW(dialog_error), "ktsuss");
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog_error), KTS_ERRORS[type]);
	gtk_dialog_run(GTK_DIALOG(dialog_error));
	gtk_widget_destroy(dialog_error);

	/* In case exit is needed, let's just exit */
	if (exit_true)
		exit(ret);
}


/* Gets the real full path for the given command */
char *get_real_name(const char *command)
{
	char *real_name = NULL;
	char *path = strdup(getenv("PATH"));
	GString *new_path = g_string_new(getenv("PATH"));
 
	/* temporary set PATH to include the sbin directories */
	new_path = g_string_append(new_path, "/usr/sbin:/usr/local/sbin:/sbin");
	setenv("PATH", new_path->str, 1);

	real_name = g_find_program_in_path(command);

	/* Restore the PATH */
	setenv("PATH", path, 1);

	/* Clean up */
	free(path);
	g_string_free(new_path, TRUE);

	return real_name;
}


/* Set the terminal in raw mode (ttyfd must be a valid terminal file descriptor) */
void tty_raw(int ttyfd)
{
	struct termios raw;

	memcpy(&raw, &orig_termios, sizeof(struct termios));
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 5; raw.c_cc[VTIME] = 8;
    raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;
    raw.c_cc[VMIN] = 2; raw.c_cc[VTIME] = 0;
    raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 8;
    if (tcsetattr(ttyfd, TCSAFLUSH, &raw) < 0) 
		err(1, "tcsetattr()");
}


int main(int argc, char *argv[])
{
	gboolean explicit_username = FALSE;
	gboolean explicit_message = FALSE;
	int error = 0;
	int m = 0, i = 1;
	guint counter = 0;
	gchar *command = NULL;
	gchar *command_run = NULL;
	gchar *username = NULL;
	gchar *password = NULL;

	uid_t whoami;
	struct passwd *pw;

	char err_msg[256];

	gchar *message = NULL;

	char **cmd_argv = NULL;
	GError *cmd_error = NULL;

	GtkWidget *dialog;
	GtkSizeGroup *sizegroup;
	GtkWidget *hbox;
	GtkWidget *image;
	GtkWidget *align;
	GtkWidget *label;
	GtkWidget *user;
	GtkWidget *pass;

	gtk_init(&argc, &argv);

	/* Parse arguments */
	while (i < argc) {
		if (argv[i][0] != '-')
			break;
		if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
			say_help(argv[0]);
		if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-v"))
			say_about();
		if (!strcmp(argv[i], "--user") || !strcmp(argv[i], "-u")) {
			if ((username = argv[i + 1]) == NULL)
				Werror(ERR_MISSING_USER_AND_COMMAND, NULL, 1, 1);
			explicit_username = TRUE;
			i += 1;
		}
		if (!strcmp(argv[i], "--message") || !strcmp(argv[i], "-m")) {
			if ((message = argv[i + 1]) == NULL)
				Werror(ERR_MISSING_MESSAGE_AND_COMMAND, NULL, 1, 1);
			explicit_message = TRUE;
			i += 1;
		}
		i += 1;
	}

	if (argv[i] == NULL)
		Werror(ERR_MISSING_COMMAND, NULL, 1, 1);

	/* handle arguments and spaces in the subcommand correctly */
	if (! g_shell_parse_argv(argv[i], NULL, &cmd_argv, &cmd_error))
		/* Something bad has happened */
		Werror(ERR_INVALID_COMMAND, cmd_error->message, 1, 1);

	/* Get the full path command */
	command = get_real_name(cmd_argv[0]);
	if (command == NULL)
		Werror(ERR_INVALID_COMMAND, cmd_argv[0], 1, 1);

	/* Sanity check */
	whoami = getuid();
	if ((pw = getpwuid(whoami)) == NULL)
		exit(2 + 0 * fprintf(stderr, "Who you think you are? Houdini?\n"));

	if (!explicit_username)
		username = g_strdup("root");

	if (username && !strcmp(pw->pw_name, username)) {
		/* username was me so let's just run it and get the hell out */
		if (execvp(command, &(cmd_argv[0])) == -1) {
			Werror(ERR_PERMISSION_DENIED, NULL, 1, 1);
			exit(1);
		}
		/* We should never get here, but just in case */
		exit(0);
	}

	if (explicit_username && !explicit_message)
		message = g_strdup_printf("Please enter the\npassword for %s:", username);
	else if (!explicit_message)
		message = g_strdup("Please enter the desired\nusername and password:");

	dialog = gtk_dialog_new_with_buttons(command, NULL, GTK_DIALOG_NO_SEPARATOR, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
	gtk_window_set_title(GTK_WINDOW(dialog), cmd_argv[0]);
	gtk_window_set_icon_name(GTK_WINDOW(dialog), "ktsuss");
	gtk_container_set_border_width(GTK_CONTAINER(dialog), 5);
	gtk_container_set_border_width(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), 5);
	gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog)->vbox), 5);
	hbox = gtk_hbox_new(FALSE, 6);
#if GTK_CHECK_VERSION(2, 10, 0)
	image = gtk_image_new_from_stock(GTK_STOCK_DIALOG_AUTHENTICATION, GTK_ICON_SIZE_DIALOG);
#else
	image = gtk_image_new_from_icon_name("ktsuss", GTK_ICON_SIZE_DIALOG);
#endif
	gtk_box_pack_start(GTK_BOX(hbox), image, FALSE, FALSE, 0);
	label = gtk_label_new(message);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), hbox);
	sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	if (!explicit_username) {
		hbox = gtk_hbox_new(FALSE, 6);
		label = gtk_label_new("Username");
		align = gtk_alignment_new(0, 0.5, 0, 0);
		gtk_container_add(GTK_CONTAINER(align), label);
		gtk_size_group_add_widget(sizegroup, align);
		gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);
		user = gtk_entry_new();
		gtk_entry_set_text(GTK_ENTRY(user), username ? username : "root");
		gtk_box_pack_start(GTK_BOX(hbox), user, FALSE, FALSE, 0);
		gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), hbox);
	}
	hbox = gtk_hbox_new(FALSE, 6);
	label = gtk_label_new("Password");
	align = gtk_alignment_new(0, 0.5, 0, 0);
	gtk_container_add(GTK_CONTAINER(align), label);
	gtk_size_group_add_widget(sizegroup, align);
	gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);
	pass = gtk_entry_new_with_max_length(32);
	gtk_entry_set_visibility(GTK_ENTRY(pass), FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), pass, FALSE, FALSE, 0);
	gtk_entry_set_activates_default(GTK_ENTRY(pass), TRUE);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), hbox);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
	gtk_widget_grab_focus(pass);
	gtk_widget_show_all(GTK_DIALOG(dialog)->vbox);

	/* Show the dialog up to 3 times */
	while (counter < 3) {
		if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
			if (!explicit_username)
				username = strdup(gtk_entry_get_text(GTK_ENTRY(user)));
			password = strdup(gtk_entry_get_text(GTK_ENTRY(pass)));

#ifdef SUDOPATH
			if ((error = check_password_sudo(username, password)) == ERR_SUCCESS) {
#else
			if ((error = check_password_su(username, password)) == ERR_SUCCESS) {
#endif
				gtk_widget_destroy(dialog);
				while (gtk_events_pending())
					gtk_main_iteration();
				dialog = NULL;
				/* using argv instead of cmd_argv is fine, because 'su' is going
				 * to implement its only parsing nevertheless */
				command_run = g_strjoinv(" ", &argv[i]);
#ifdef SUDOPATH
				run_sudo(username, password, command_run);
#else
				run_su(username, password, command_run);
#endif
				g_free(command_run);

				counter = 3;
			}
			if (!explicit_username) {
				free(username);
				username = NULL;
			}
			memset(password, '\0', strlen(password));
			free(password);
			if (error != ERR_SUCCESS) {
				snprintf(err_msg, sizeof(err_msg), "Could not run '%s'", command);
				Werror(error, err_msg, 0, 0);
				counter++;
			}
		}
		else
			break;
		if (counter < 3)
			gtk_widget_grab_focus(pass);
	}

	/* Clean up process */
	if (dialog)
		gtk_widget_destroy(dialog);
	if ((explicit_username && !explicit_message) || !explicit_message)
        free(message);
	if (!explicit_username && username)
		g_free(username);
	
	g_strfreev(cmd_argv);
	if (cmd_error)
		g_error_free(cmd_error);

	return 0;
}


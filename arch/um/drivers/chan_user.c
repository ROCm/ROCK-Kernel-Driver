/* 
 * Copyright (C) 2000 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include "kern_util.h"
#include "user_util.h"
#include "chan_user.h"
#include "user.h"
#include "helper.h"
#include "os.h"
#include "choose-mode.h"
#include "mode.h"

int generic_console_write(int fd, const char *buf, int n, void *unused)
{
	struct termios save, new;
	int err;

	if(isatty(fd)){
		CATCH_EINTR(err = tcgetattr(fd, &save));
		if (err)
			goto error;
		new = save;
		/* The terminal becomes a bit less raw, to handle \n also as
		 * "Carriage Return", not only as "New Line". Otherwise, the new
		 * line won't start at the first column.*/
		new.c_oflag |= OPOST;
		CATCH_EINTR(err = tcsetattr(fd, TCSAFLUSH, &new));
		if (err)
			goto error;
	}
	err = generic_write(fd, buf, n, NULL);
	/* Restore raw mode, in any case; we *must* ignore any error apart
	 * EINTR, except for debug.*/
	if(isatty(fd))
		CATCH_EINTR(tcsetattr(fd, TCSAFLUSH, &save));
	return(err);
error:
	return(-errno);
}

static void winch_handler(int sig)
{
}

struct winch_data {
	int pty_fd;
	int pipe_fd;
	int close_me;
};

static int winch_thread(void *arg)
{
	struct winch_data *data = arg;
	sigset_t sigs;
	int pty_fd, pipe_fd;
	int count, err;
	char c = 1;

	os_close_file(data->close_me);
	pty_fd = data->pty_fd;
	pipe_fd = data->pipe_fd;
	count = os_write_file(pipe_fd, &c, sizeof(c));
	if(count != sizeof(c))
		printk("winch_thread : failed to write synchronization "
		       "byte, err = %d\n", -count);

	signal(SIGWINCH, winch_handler);
	sigfillset(&sigs);
	sigdelset(&sigs, SIGWINCH);
	if(sigprocmask(SIG_SETMASK, &sigs, NULL) < 0){
		printk("winch_thread : sigprocmask failed, errno = %d\n", 
		       errno);
		exit(1);
	}

	if(setsid() < 0){
		printk("winch_thread : setsid failed, errno = %d\n", errno);
		exit(1);
	}

	err = os_new_tty_pgrp(pty_fd, os_getpid());
	if(err < 0){
		printk("winch_thread : new_tty_pgrp failed, err = %d\n", -err);
		exit(1);
	}

	count = os_read_file(pipe_fd, &c, sizeof(c));
	if(count != sizeof(c))
		printk("winch_thread : failed to read synchronization byte, "
		       "err = %d\n", -count);

	while(1){
		pause();

		count = os_write_file(pipe_fd, &c, sizeof(c));
		if(count != sizeof(c))
			printk("winch_thread : write failed, err = %d\n",
			       -count);
	}
}

static int winch_tramp(int fd, void *device_data, int *fd_out)
{
	struct winch_data data;
	unsigned long stack;
	int fds[2], pid, n, err;
	char c;

	err = os_pipe(fds, 1, 1);
	if(err < 0){
		printk("winch_tramp : os_pipe failed, err = %d\n", -err);
		return(err);
	}

	data = ((struct winch_data) { .pty_fd 		= fd,
				      .pipe_fd 		= fds[1],
				      .close_me 	= fds[0] } );
	pid = run_helper_thread(winch_thread, &data, 0, &stack, 0);
	if(pid < 0){
		printk("fork of winch_thread failed - errno = %d\n", errno);
		return(pid);
	}

	os_close_file(fds[1]);
	*fd_out = fds[0];
	n = os_read_file(fds[0], &c, sizeof(c));
	if(n != sizeof(c)){
		printk("winch_tramp : failed to read synchronization byte\n");
		printk("read failed, err = %d\n", -n);
		printk("fd %d will not support SIGWINCH\n", fd);
		*fd_out = -1;
	}
	return(pid);
}

void register_winch(int fd, void *device_data)
{
	int pid, thread, thread_fd;
	int count;
	char c = 1;

	if(!isatty(fd))
		return;

	pid = tcgetpgrp(fd);
	if(!CHOOSE_MODE_PROC(is_tracer_winch, is_skas_winch, pid, fd,
			     device_data) && (pid == -1)){
		thread = winch_tramp(fd, device_data, &thread_fd);
		if(fd != -1){
			register_winch_irq(thread_fd, fd, thread, device_data);

			count = os_write_file(thread_fd, &c, sizeof(c));
			if(count != sizeof(c))
				printk("register_winch : failed to write "
				       "synchronization byte, err = %d\n",
					-count);
		}
	}
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */

/* 
 * Copyright (C) 2000, 2001 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include <fcntl.h>
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

void generic_close(int fd, void *unused)
{
	close(fd);
}

int generic_read(int fd, char *c_out, void *unused)
{
	int n;

	n = read(fd, c_out, sizeof(*c_out));
	if(n < 0){
		if(errno == EAGAIN) return(0);
		return(-errno);
	}
	else if(n == 0) return(-EIO);
	return(1);
}

int generic_write(int fd, const char *buf, int n, void *unused)
{
	int count;

	count = write(fd, buf, n);
	if(count < 0) return(-errno);
	return(count);
}

int generic_console_write(int fd, const char *buf, int n, void *unused)
{
	struct termios save, new;
	int err;

	if(isatty(fd)){
		tcgetattr(fd, &save);
		new = save;
		new.c_oflag |= OPOST;
		tcsetattr(fd, TCSAFLUSH, &new);
	}
	err = generic_write(fd, buf, n, NULL);
	if(isatty(fd)) tcsetattr(fd, TCSAFLUSH, &save);
	return(err);
}

int generic_window_size(int fd, void *unused, unsigned short *rows_out,
			unsigned short *cols_out)
{
	struct winsize size;
	int ret = 0;

	if(ioctl(fd, TIOCGWINSZ, &size) == 0){
		ret = ((*rows_out != size.ws_row) || 
		       (*cols_out != size.ws_col));
		*rows_out = size.ws_row;
		*cols_out = size.ws_col;
	}
	return(ret);
}

void generic_free(void *data)
{
	kfree(data);
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
	char c = 1;

	close(data->close_me);
	pty_fd = data->pty_fd;
	pipe_fd = data->pipe_fd;
	if(write(pipe_fd, &c, sizeof(c)) != sizeof(c))
		printk("winch_thread : failed to write synchronization "
		       "byte, errno = %d\n", errno);

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

	if(ioctl(pty_fd, TIOCSCTTY, 0) < 0){
		printk("winch_thread : TIOCSCTTY failed, errno = %d\n", errno);
		exit(1);
	}
	if(tcsetpgrp(pty_fd, os_getpid()) < 0){
		printk("winch_thread : tcsetpgrp failed, errno = %d\n", errno);
		exit(1);
	}

	if(read(pipe_fd, &c, sizeof(c)) != sizeof(c))
		printk("winch_thread : failed to read synchronization byte, "
		       "errno = %d\n", errno);

	while(1){
		pause();

		if(write(pipe_fd, &c, sizeof(c)) != sizeof(c)){
			printk("winch_thread : write failed, errno = %d\n",
			       errno);
		}
	}
}

static int winch_tramp(int fd, void *device_data, int *fd_out)
{
	struct winch_data data;
	unsigned long stack;
	int fds[2], pid, n, err;
	char c;

	err = os_pipe(fds, 1, 1);
	if(err){
		printk("winch_tramp : os_pipe failed, errno = %d\n", -err);
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

	close(fds[1]);
	*fd_out = fds[0];
	n = read(fds[0], &c, sizeof(c));
	if(n != sizeof(c)){
		printk("winch_tramp : failed to read synchronization byte\n");
		printk("read returned %d, errno = %d\n", n, errno);
		printk("fd %d will not support SIGWINCH\n", fd);
		*fd_out = -1;
	}
	return(pid);
}

void register_winch(int fd, void *device_data)
{
	int pid, thread, thread_fd;
	char c = 1;

	if(!isatty(fd)) return;

	pid = tcgetpgrp(fd);
	if(!CHOOSE_MODE(is_tracer_winch(pid, fd, device_data), 0) && 
	   (pid == -1)){
		thread = winch_tramp(fd, device_data, &thread_fd);
		if(fd != -1){
			register_winch_irq(thread_fd, fd, thread, device_data);

			if(write(thread_fd, &c, sizeof(c)) != sizeof(c))
				printk("register_winch : failed to write "
				       "synchronization byte\n");
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

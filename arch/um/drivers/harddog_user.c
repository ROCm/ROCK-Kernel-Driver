/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "user_util.h"
#include "user.h"
#include "helper.h"
#include "mconsole.h"
#include "os.h"
#include "choose-mode.h"
#include "mode.h"

struct dog_data {
	int stdin;
	int stdout;
	int close_me[2];
};

static void pre_exec(void *d)
{
	struct dog_data *data = d;

	dup2(data->stdin, 0);
	dup2(data->stdout, 1);
	dup2(data->stdout, 2);
	close(data->stdin);
	close(data->stdout);
	close(data->close_me[0]);
	close(data->close_me[1]);
}

int start_watchdog(int *in_fd_ret, int *out_fd_ret, char *sock)
{
	struct dog_data data;
	int in_fds[2], out_fds[2], pid, n, err;
	char pid_buf[sizeof("nnnnn\0")], c;
	char *pid_args[] = { "/usr/bin/uml_watchdog", "-pid", pid_buf, NULL };
	char *mconsole_args[] = { "/usr/bin/uml_watchdog", "-mconsole", NULL, 
				  NULL };
	char **args = NULL;

	err = os_pipe(in_fds, 1, 0);
	if(err){
		printk("harddog_open - os_pipe failed, errno = %d\n", -err);
		return(err);
	}

	err = os_pipe(out_fds, 1, 0);
	if(err){
		printk("harddog_open - os_pipe failed, errno = %d\n", -err);
		return(err);
	}

	data.stdin = out_fds[0];
	data.stdout = in_fds[1];
	data.close_me[0] = out_fds[1];
	data.close_me[1] = in_fds[0];

	if(sock != NULL){
		mconsole_args[2] = sock;
		args = mconsole_args;
	}
	else {
		/* XXX The os_getpid() is not SMP correct */
		sprintf(pid_buf, "%d", CHOOSE_MODE(tracing_pid, os_getpid()));
		args = pid_args;
	}

	pid = run_helper(pre_exec, &data, args, NULL);

	close(out_fds[0]);
	close(in_fds[1]);

	if(pid < 0){
		err = -pid;
		printk("harddog_open - run_helper failed, errno = %d\n", err);
		goto out;
	}

	n = read(in_fds[0], &c, sizeof(c));
	if(n == 0){
		printk("harddog_open - EOF on watchdog pipe\n");
		helper_wait(pid);
		err = -EIO;
		goto out;
	}
	else if(n < 0){
		printk("harddog_open - read of watchdog pipe failed, "
		       "errno = %d\n", errno);
		helper_wait(pid);
		err = -errno;
		goto out;
	}
	*in_fd_ret = in_fds[0];
	*out_fd_ret = out_fds[1];
	return(0);
 out:
	close(out_fds[1]);
	close(in_fds[0]);
	return(err);
}

void stop_watchdog(int in_fd, int out_fd)
{
	close(in_fd);
	close(out_fd);
}

int ping_watchdog(int fd)
{
	int n;
	char c = '\n';

	n = write(fd, &c, sizeof(c));
	if(n < sizeof(c)){
		printk("ping_watchdog - write failed, errno = %d\n",
		       errno);
		return(-errno);
	}
	return 1;

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

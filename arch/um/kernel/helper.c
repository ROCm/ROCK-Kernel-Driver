/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include "user.h"
#include "kern_util.h"
#include "os.h"

struct helper_data {
	void (*pre_exec)(void*);
	void *pre_data;
	char **argv;
	int fd;
};

/* Debugging aid, changed only from gdb */
int helper_pause = 0;

static void helper_hup(int sig)
{
}

static int helper_child(void *arg)
{
	struct helper_data *data = arg;
	char **argv = data->argv;

	if(helper_pause){
		signal(SIGHUP, helper_hup);
		pause();
	}
	if(data->pre_exec != NULL)
		(*data->pre_exec)(data->pre_data);
	execvp(argv[0], argv);
	printk("execvp of '%s' failed - errno = %d\n", argv[0], errno);
	write(data->fd, &errno, sizeof(errno));
	os_kill_process(os_getpid(), 0);
	return(0);
}

/* XXX The alloc_stack here breaks if this is called in the tracing thread */

int run_helper(void (*pre_exec)(void *), void *pre_data, char **argv,
	       unsigned long *stack_out)
{
	struct helper_data data;
	unsigned long stack, sp;
	int pid, fds[2], err, n;

	if((stack_out != NULL) && (*stack_out != 0))
		stack = *stack_out;
	else stack = alloc_stack(0, um_in_interrupt());
	if(stack == 0) return(-ENOMEM);

	err = os_pipe(fds, 1, 0);
	if(err){
		printk("run_helper : pipe failed, errno = %d\n", -err);
		return(err);
	}
	if(fcntl(fds[1], F_SETFD, 1) != 0){
		printk("run_helper : setting FD_CLOEXEC failed, errno = %d\n",
		       errno);
		return(-errno);
	}

	sp = stack + page_size() - sizeof(void *);
	data.pre_exec = pre_exec;
	data.pre_data = pre_data;
	data.argv = argv;
	data.fd = fds[1];
	pid = clone(helper_child, (void *) sp, CLONE_VM | SIGCHLD, &data);
	if(pid < 0){
		printk("run_helper : clone failed, errno = %d\n", errno);
		return(-errno);
	}
	close(fds[1]);
	n = read(fds[0], &err, sizeof(err));
	if(n < 0){
		printk("run_helper : read on pipe failed, errno = %d\n", 
		       errno);
		return(-errno);
	}
	else if(n != 0){
		waitpid(pid, NULL, 0);
		pid = -err;
	}

	if(stack_out == NULL) free_stack(stack, 0);
        else *stack_out = stack;
	return(pid);
}

int run_helper_thread(int (*proc)(void *), void *arg, unsigned int flags, 
		      unsigned long *stack_out, int stack_order)
{
	unsigned long stack, sp;
	int pid, status;

	stack = alloc_stack(stack_order, um_in_interrupt());
	if(stack == 0) return(-ENOMEM);

	sp = stack + (page_size() << stack_order) - sizeof(void *);
	pid = clone(proc, (void *) sp, flags | SIGCHLD, arg);
	if(pid < 0){
		printk("run_helper_thread : clone failed, errno = %d\n", 
		       errno);
		return(-errno);
	}
	if(stack_out == NULL){
		pid = waitpid(pid, &status, 0);
		if(pid < 0)
			printk("run_helper_thread - wait failed, errno = %d\n",
			       pid);
		if(!WIFEXITED(status) || (WEXITSTATUS(status) != 0))
			printk("run_helper_thread - thread returned status "
			       "0x%x\n", status);
		free_stack(stack, stack_order);
	}
        else *stack_out = stack;
	return(pid);
}

int helper_wait(int pid, int block)
{
	int ret;

	ret = waitpid(pid, NULL, WNOHANG);
	if(ret < 0){
		printk("helper_wait : waitpid failed, errno = %d\n", errno);
		return(-errno);
	}
	return(ret);
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

/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include "user.h"
#include "kern_util.h"
#include "os.h"
#include "time_user.h"

static int user_thread_tramp(void *arg)
{
	if(ptrace(PTRACE_TRACEME, 0, 0, 0) < 0)
		panic("user_thread_tramp - PTRACE_TRACEME failed, "
		      "errno = %d\n", errno);
	enable_timer();
	os_stop_process(os_getpid());
	return(0);
}

int user_thread(unsigned long stack, int flags)
{
	int pid, status;

	pid = clone(user_thread_tramp, (void *) stack_sp(stack), 
		    flags | CLONE_FILES | SIGCHLD, NULL);
	if(pid < 0){
		printk("user_thread - clone failed, errno = %d\n", errno);
		return(pid);
	}

	if(waitpid(pid, &status, WUNTRACED) < 0){
		printk("user_thread - waitpid failed, errno = %d\n", errno);
		return(-errno);
	}

	if(!WIFSTOPPED(status) || (WSTOPSIG(status) != SIGSTOP)){
		printk("user_thread - trampoline didn't stop, status = %d\n", 
		       status);
		return(-EINVAL);
	}

	return(pid);
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

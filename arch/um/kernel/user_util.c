/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sys/mman.h> 
#include <sys/stat.h>
#include <sys/ptrace.h>
#include <sys/utsname.h>
#include <sys/param.h>
#include <sys/time.h>
#include "asm/types.h"
#include <ctype.h>
#include <signal.h>
#include <wait.h>
#include <errno.h>
#include <stdarg.h>
#include <sched.h>
#include <termios.h>
#include <string.h>
#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "mem_user.h"
#include "init.h"
#include "helper.h"
#include "uml-config.h"

#define COMMAND_LINE_SIZE _POSIX_ARG_MAX

/* Changed in linux_main and setup_arch, which run before SMP is started */
char command_line[COMMAND_LINE_SIZE] = { 0 };

void add_arg(char *cmd_line, char *arg)
{
	if (strlen(cmd_line) + strlen(arg) + 1 > COMMAND_LINE_SIZE) {
		printf("add_arg: Too much command line!\n");
		exit(1);
	}
	if(strlen(cmd_line) > 0) strcat(cmd_line, " ");
	strcat(cmd_line, arg);
}

void stop(void)
{
	while(1) sleep(1000000);
}

void stack_protections(unsigned long address)
{
	int prot = PROT_READ | PROT_WRITE | PROT_EXEC;

        if(mprotect((void *) address, page_size(), prot) < 0)
		panic("protecting stack failed, errno = %d", errno);
}

void task_protections(unsigned long address)
{
	unsigned long guard = address + page_size();
	unsigned long stack = guard + page_size();
	int prot = 0, pages;

#ifdef notdef
	if(mprotect((void *) stack, page_size(), prot) < 0)
		panic("protecting guard page failed, errno = %d", errno);
#endif
	pages = (1 << UML_CONFIG_KERNEL_STACK_ORDER) - 2;
	prot = PROT_READ | PROT_WRITE | PROT_EXEC;
	if(mprotect((void *) stack, pages * page_size(), prot) < 0)
		panic("protecting stack failed, errno = %d", errno);
}

int wait_for_stop(int pid, int sig, int cont_type, void *relay)
{
	sigset_t *relay_signals = relay;
	int status, ret;

	while(1){
		if(((ret = waitpid(pid, &status, WUNTRACED)) < 0) ||
		   !WIFSTOPPED(status) || (WSTOPSIG(status) != sig)){
			if(ret < 0){
				if(errno == EINTR) continue;
				printk("wait failed, errno = %d\n",
				       errno);
			}
			else if(WIFEXITED(status)) 
				printk("process exited with status %d\n", 
				       WEXITSTATUS(status));
			else if(WIFSIGNALED(status))
				printk("process exited with signal %d\n", 
				       WTERMSIG(status));
			else if((WSTOPSIG(status) == SIGVTALRM) ||
				(WSTOPSIG(status) == SIGALRM) ||
				(WSTOPSIG(status) == SIGIO) ||
				(WSTOPSIG(status) == SIGPROF) ||
				(WSTOPSIG(status) == SIGCHLD) ||
				(WSTOPSIG(status) == SIGWINCH) ||
				(WSTOPSIG(status) == SIGINT)){
				ptrace(cont_type, pid, 0, WSTOPSIG(status));
				continue;
			}
			else if((relay_signals != NULL) &&
				sigismember(relay_signals, WSTOPSIG(status))){
				ptrace(cont_type, pid, 0, WSTOPSIG(status));
				continue;
			}
			else printk("process stopped with signal %d\n", 
				    WSTOPSIG(status));
			panic("wait_for_stop failed to wait for %d to stop "
			      "with %d\n", pid, sig);
		}
		return(status);
	}
}

int clone_and_wait(int (*fn)(void *), void *arg, void *sp, int flags)
{
	int pid;

	pid = clone(fn, sp, flags, arg);
 	if(pid < 0) return(-1);
	wait_for_stop(pid, SIGSTOP, PTRACE_CONT, NULL);
	ptrace(PTRACE_CONT, pid, 0, 0);
	return(pid);
}

int raw(int fd, int complain)
{
	struct termios tt;
	int err;

	tcgetattr(fd, &tt);
	cfmakeraw(&tt);
	err = tcsetattr(fd, TCSANOW, &tt);
	if((err < 0) && complain){
		printk("tcsetattr failed, errno = %d\n", errno);
		return(-errno);
	}
	return(0);
}

void setup_machinename(char *machine_out)
{
	struct utsname host;

	uname(&host);
	strcpy(machine_out, host.machine);
}

char host_info[(_UTSNAME_LENGTH + 1) * 4 + _UTSNAME_NODENAME_LENGTH + 1];

void setup_hostinfo(void)
{
	struct utsname host;

	uname(&host);
	sprintf(host_info, "%s %s %s %s %s", host.sysname, host.nodename,
		host.release, host.version, host.machine);
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

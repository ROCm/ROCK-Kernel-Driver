/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdlib.h>
#include <setjmp.h>
#include <sys/time.h>
#include <sys/ptrace.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <asm/ptrace.h>
#include <asm/sigcontext.h>
#include <asm/unistd.h>
#include <asm/page.h>
#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "process.h"
#include "signal_kern.h"
#include "signal_user.h"
#include "sysdep/ptrace.h"
#include "sysdep/sigcontext.h"
#include "irq_user.h"
#include "ptrace_user.h"
#include "time_user.h"
#include "init.h"
#include "os.h"
#include "uml-config.h"
#include "choose-mode.h"
#include "mode.h"
#ifdef UML_CONFIG_MODE_SKAS
#include "skas.h"
#include "skas_ptrace.h"
#endif

void init_new_thread_stack(void *sig_stack, void (*usr1_handler)(int))
{
	int flags = 0, pages;

	if(sig_stack != NULL){
		pages = (1 << UML_CONFIG_KERNEL_STACK_ORDER) - 2;
		set_sigstack(sig_stack, pages * page_size());
		flags = SA_ONSTACK;
	}
	if(usr1_handler) set_handler(SIGUSR1, usr1_handler, flags, -1);
}

void init_new_thread_signals(int altstack)
{
	int flags = altstack ? SA_ONSTACK : 0;

	set_handler(SIGSEGV, (__sighandler_t) sig_handler, flags,
		    SIGUSR1, SIGIO, SIGWINCH, SIGALRM, SIGVTALRM, -1);
	set_handler(SIGTRAP, (__sighandler_t) sig_handler, flags, 
		    SIGUSR1, SIGIO, SIGWINCH, SIGALRM, SIGVTALRM, -1);
	set_handler(SIGFPE, (__sighandler_t) sig_handler, flags, 
		    SIGUSR1, SIGIO, SIGWINCH, SIGALRM, SIGVTALRM, -1);
	set_handler(SIGILL, (__sighandler_t) sig_handler, flags, 
		    SIGUSR1, SIGIO, SIGWINCH, SIGALRM, SIGVTALRM, -1);
	set_handler(SIGBUS, (__sighandler_t) sig_handler, flags, 
		    SIGUSR1, SIGIO, SIGWINCH, SIGALRM, SIGVTALRM, -1);
	set_handler(SIGWINCH, (__sighandler_t) sig_handler, flags, 
		    SIGUSR1, SIGIO, SIGWINCH, SIGALRM, SIGVTALRM, -1);
	set_handler(SIGUSR2, (__sighandler_t) sig_handler, 
		    SA_NOMASK | flags, -1);
	(void) CHOOSE_MODE(signal(SIGCHLD, SIG_IGN), (void *) 0);
	signal(SIGHUP, SIG_IGN);

	init_irq_signals(altstack);
}

struct tramp {
	int (*tramp)(void *);
	void *tramp_data;
	unsigned long temp_stack;
	int flags;
	int pid;
};

/* See above for why sigkill is here */

int sigkill = SIGKILL;

int outer_tramp(void *arg)
{
	struct tramp *t;
	int sig = sigkill;

	t = arg;
	t->pid = clone(t->tramp, (void *) t->temp_stack + page_size()/2,
		       t->flags, t->tramp_data);
	if(t->pid > 0) wait_for_stop(t->pid, SIGSTOP, PTRACE_CONT, NULL);
	kill(os_getpid(), sig);
	_exit(0);
}

int start_fork_tramp(void *thread_arg, unsigned long temp_stack, 
		     int clone_flags, int (*tramp)(void *))
{
	struct tramp arg;
	unsigned long sp;
	int new_pid, status, err;

	/* The trampoline will run on the temporary stack */
	sp = stack_sp(temp_stack);

	clone_flags |= CLONE_FILES | SIGCHLD;

	arg.tramp = tramp;
	arg.tramp_data = thread_arg;
	arg.temp_stack = temp_stack;
	arg.flags = clone_flags;

	/* Start the process and wait for it to kill itself */
	new_pid = clone(outer_tramp, (void *) sp, clone_flags, &arg);
	if(new_pid < 0) return(-errno);
	while((err = waitpid(new_pid, &status, 0) < 0) && (errno == EINTR)) ;
	if(err < 0) panic("Waiting for outer trampoline failed - errno = %d", 
			  errno);
	if(!WIFSIGNALED(status) || (WTERMSIG(status) != SIGKILL))
		panic("outer trampoline didn't exit with SIGKILL");

	return(arg.pid);
}

void suspend_new_thread(int fd)
{
	char c;

	os_stop_process(os_getpid());

	if(read(fd, &c, sizeof(c)) != sizeof(c))
		panic("read failed in suspend_new_thread");
}

static int ptrace_child(void *arg)
{
	int pid = os_getpid();

	if(ptrace(PTRACE_TRACEME, 0, 0, 0) < 0){
		perror("ptrace");
		os_kill_process(pid, 0);
	}
	os_stop_process(pid);
	_exit(os_getpid() == pid);
}

static int start_ptraced_child(void **stack_out)
{
	void *stack;
	unsigned long sp;
	int pid, n, status;
	
	stack = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if(stack == MAP_FAILED)
		panic("check_ptrace : mmap failed, errno = %d", errno);
	sp = (unsigned long) stack + PAGE_SIZE - sizeof(void *);
	pid = clone(ptrace_child, (void *) sp, SIGCHLD, NULL);
	if(pid < 0)
		panic("check_ptrace : clone failed, errno = %d", errno);
	n = waitpid(pid, &status, WUNTRACED);
	if(n < 0)
		panic("check_ptrace : wait failed, errno = %d", errno);
	if(!WIFSTOPPED(status) || (WSTOPSIG(status) != SIGSTOP))
		panic("check_ptrace : expected SIGSTOP, got status = %d",
		      status);

	*stack_out = stack;
	return(pid);
}

static void stop_ptraced_child(int pid, void *stack, int exitcode)
{
	int status, n;

	if(ptrace(PTRACE_CONT, pid, 0, 0) < 0)
		panic("check_ptrace : ptrace failed, errno = %d", errno);
	n = waitpid(pid, &status, 0);
	if(!WIFEXITED(status) || (WEXITSTATUS(status) != exitcode))
		panic("check_ptrace : child exited with status 0x%x", status);

	if(munmap(stack, PAGE_SIZE) < 0)
		panic("check_ptrace : munmap failed, errno = %d", errno);
}

void __init check_ptrace(void)
{
	void *stack;
	int pid, syscall, n, status;

	printk("Checking that ptrace can change system call numbers...");
	pid = start_ptraced_child(&stack);

	while(1){
		if(ptrace(PTRACE_SYSCALL, pid, 0, 0) < 0)
			panic("check_ptrace : ptrace failed, errno = %d", 
			      errno);
		n = waitpid(pid, &status, WUNTRACED);
		if(n < 0)
			panic("check_ptrace : wait failed, errno = %d", errno);
		if(!WIFSTOPPED(status) || (WSTOPSIG(status) != SIGTRAP))
			panic("check_ptrace : expected SIGTRAP, "
			      "got status = %d", status);
		
		syscall = ptrace(PTRACE_PEEKUSER, pid, PT_SYSCALL_NR_OFFSET,
				 0);
		if(syscall == __NR_getpid){
			n = ptrace(PTRACE_POKEUSER, pid, PT_SYSCALL_NR_OFFSET,
				   __NR_getppid);
			if(n < 0)
				panic("check_ptrace : failed to modify system "
				      "call, errno = %d", errno);
			break;
		}
	}
	stop_ptraced_child(pid, stack, 0);
	printk("OK\n");
}

int run_kernel_thread(int (*fn)(void *), void *arg, void **jmp_ptr)
{
	jmp_buf buf;
	int n;

	*jmp_ptr = &buf;
	n = setjmp(buf);
	if(n != 0)
		return(n);
	(*fn)(arg);
	return(0);
}

void forward_pending_sigio(int target)
{
	sigset_t sigs;

	if(sigpending(&sigs)) 
		panic("forward_pending_sigio : sigpending failed");
	if(sigismember(&sigs, SIGIO))
		kill(target, SIGIO);
}

int can_do_skas(void)
{
#ifdef UML_CONFIG_MODE_SKAS
	struct ptrace_faultinfo fi;
	void *stack;
	int pid, n, ret = 1;

	printf("Checking for the skas3 patch in the host...");
	pid = start_ptraced_child(&stack);

	n = ptrace(PTRACE_FAULTINFO, pid, 0, &fi);
	if(n < 0){
		if(errno == EIO)
			printf("not found\n");
		else printf("No (unexpected errno - %d)\n", errno);
		ret = 0;
	}
	else printf("found\n");

	init_registers(pid);
	stop_ptraced_child(pid, stack, 1);

	printf("Checking for /proc/mm...");
	if(access("/proc/mm", W_OK)){
		printf("not found\n");
		ret = 0;
	}
	else printf("found\n");

	return(ret);
#else
	return(0);
#endif
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

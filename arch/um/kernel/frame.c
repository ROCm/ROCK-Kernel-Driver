/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <wait.h>
#include <sched.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <sys/mman.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/sigcontext.h>
#include "sysdep/ptrace.h"
#include "sysdep/sigcontext.h"
#include "frame_user.h"
#include "kern_util.h"
#include "ptrace_user.h"
#include "os.h"

static int capture_stack(int (*child)(void *arg), void *arg, void *sp,
			 unsigned long top, void **data_out)
{
	unsigned long regs[FRAME_SIZE];
	int pid, status, n, len;

	/* Start the child as a thread */
	pid = clone(child, sp, CLONE_VM | SIGCHLD, arg);
	if(pid < 0){
		printf("capture_stack : clone failed - errno = %d\n", errno);
		exit(1);
	}

	/* Wait for it to stop itself and continue it with a SIGUSR1 to force 
	 * it into the signal handler.
	 */
	n = waitpid(pid, &status, WUNTRACED);
	if(n < 0){
		printf("capture_stack : waitpid failed - errno = %d\n", errno);
		exit(1);
	}
	if(!WIFSTOPPED(status) || (WSTOPSIG(status) != SIGSTOP)){
		fprintf(stderr, "capture_stack : Expected SIGSTOP, "
			"got status = 0x%x\n", status);
		exit(1);
	}
	if(ptrace(PTRACE_CONT, pid, 0, SIGUSR1) < 0){
		printf("capture_stack : PTRACE_CONT failed - errno = %d\n", 
		       errno);
		exit(1);
	}

	/* Wait for it to stop itself again and grab its registers again.  
	 * At this point, the handler has stuffed the addresses of
	 * sig, sc, and SA_RESTORER in raw.
	 */
	n = waitpid(pid, &status, WUNTRACED);
	if(n < 0){
		printf("capture_stack : waitpid failed - errno = %d\n", errno);
		exit(1);
	}
	if(!WIFSTOPPED(status) || (WSTOPSIG(status) != SIGSTOP)){
		fprintf(stderr, "capture_stack : Expected SIGSTOP, "
			"got status = 0x%x\n", status);
		exit(1);
	}
	if(ptrace(PTRACE_GETREGS, pid, 0, regs) < 0){
		printf("capture_stack : PTRACE_GETREGS failed - errno = %d\n", 
		       errno);
		exit(1);
	}

	/* It has outlived its usefulness, so continue it so it can exit */
	if(ptrace(PTRACE_CONT, pid, 0, 0) < 0){
		printf("capture_stack : mmap failed - errno = %d\n", errno);
		exit(1);
	}
	if(waitpid(pid, &status, 0) < 0){
		printf("capture_stack : waitpid failed - errno = %d\n", errno);
		exit(1);
	}
	if(!WIFEXITED(status) || (WEXITSTATUS(status) != 0)){
		printf("capture_stack : Expected exit status 0, "
		       "got status = 0x%x\n", status);
		exit(1);
	}

	/* The frame that we want is the top of the signal stack */

	len = top - PT_SP(regs);
	*data_out = malloc(len);
	if(*data_out == NULL){
		printf("capture_stack : malloc failed - errno = %d\n", errno);
		exit(1);
	}
	memcpy(*data_out, (void *) PT_SP(regs), len);

	return(len);
}

static void child_common(void *sp, int size, sighandler_t handler, int flags)
{
	stack_t ss;
	struct sigaction sa;

	if(ptrace(PTRACE_TRACEME, 0, 0, 0) < 0){
		printf("PTRACE_TRACEME failed, errno = %d\n", errno);
	}
	ss.ss_sp = sp;
	ss.ss_flags = 0;
	ss.ss_size = size;
	if(sigaltstack(&ss, NULL) < 0){
		printf("sigaltstack failed - errno = %d\n", errno);
		_exit(1);
	}

	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_ONSTACK | flags;
	if(sigaction(SIGUSR1, &sa, NULL) < 0){
		printf("sigaction failed - errno = %d\n", errno);
		_exit(1);
	}

	os_stop_process(os_getpid());
}

struct sc_frame signal_frame_sc;

struct sc_frame_raw {
	void *stack;
	int size;
	unsigned long sig;
	unsigned long sc;
	unsigned long sr;
	unsigned long sp;
	struct arch_frame_data_raw arch;
};

static struct sc_frame_raw *raw_sc = NULL;

static void sc_handler(int sig, struct sigcontext sc)
{
	raw_sc->sig = (unsigned long) &sig;
	raw_sc->sc = (unsigned long) &sc;
	raw_sc->sr = frame_restorer();
	raw_sc->sp = frame_sp();
	setup_arch_frame_raw(&raw_sc->arch, &sc);
	os_stop_process(os_getpid());
	_exit(0);
}

static int sc_child(void *arg)
{
	raw_sc = arg;
	child_common(raw_sc->stack, raw_sc->size, (sighandler_t) sc_handler, 
		     0);
	return(-1);
}

struct si_frame signal_frame_si;

struct si_frame_raw {
	void *stack;
	int size;
	unsigned long sig;
	unsigned long sip;
	unsigned long si;
	unsigned long sr;
	unsigned long sp;
};

static struct si_frame_raw *raw_si = NULL;

static void si_handler(int sig, siginfo_t *si)
{
	raw_si->sig = (unsigned long) &sig;
	raw_si->sip = (unsigned long) &si;
	raw_si->si = (unsigned long) si;
	raw_si->sr = frame_restorer();
	raw_si->sp = frame_sp();
	os_stop_process(os_getpid());
	_exit(0);
}

static int si_child(void *arg)
{
	raw_si = arg;
	child_common(raw_si->stack, raw_si->size, (sighandler_t) si_handler,
		     SA_SIGINFO);
	return(-1);
}

void capture_signal_stack(void)
{
	struct sc_frame_raw raw_sc;
	struct si_frame_raw raw_si;
	void *stack, *sigstack;
	unsigned long top, sig_top, base;

	stack = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	sigstack = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if((stack == MAP_FAILED) || (sigstack == MAP_FAILED)){
		printf("capture_signal_stack : mmap failed - errno = %d\n", 
		       errno);
		exit(1);
	}

	top = (unsigned long) stack + PAGE_SIZE - sizeof(void *);
	sig_top = (unsigned long) sigstack + PAGE_SIZE;

	raw_sc.stack = sigstack;
	raw_sc.size = PAGE_SIZE;
	signal_frame_sc.len = capture_stack(sc_child, &raw_sc, (void *) top,
					    sig_top, &signal_frame_sc.data);

	/* These are the offsets within signal_frame_sc.data (counting from
	 * the bottom) of sig, sc, SA_RESTORER, and the initial sp.
	 */

	base = sig_top - signal_frame_sc.len;
	signal_frame_sc.sig_index = raw_sc.sig - base;
	signal_frame_sc.sc_index = raw_sc.sc - base;
	signal_frame_sc.sr_index = raw_sc.sr - base;
	if((*((unsigned long *) raw_sc.sr) & PAGE_MASK) == 
	   (unsigned long) sigstack){
		unsigned long *sr = (unsigned long *) raw_sc.sr;
		unsigned long frame = (unsigned long) signal_frame_sc.data;

		signal_frame_sc.sr_relative = 1;
		*sr -= raw_sc.sr;
		*((unsigned long *) (frame + signal_frame_sc.sr_index)) = *sr;
	}
	else signal_frame_sc.sr_relative = 0;
	signal_frame_sc.sp_index = raw_sc.sp - base;
	setup_arch_frame(&raw_sc.arch, &signal_frame_sc.arch);

	/* Repeat for the siginfo variant */

	raw_si.stack = sigstack;
	raw_si.size = PAGE_SIZE;
	signal_frame_si.len = capture_stack(si_child, &raw_si, (void *) top,
					    sig_top, &signal_frame_si.data);
	base = sig_top - signal_frame_si.len;
	signal_frame_si.sig_index = raw_si.sig - base;
	signal_frame_si.sip_index = raw_si.sip - base;
	signal_frame_si.si_index = raw_si.si - base;
	signal_frame_si.sr_index = raw_si.sr - base;
	if((*((unsigned long *) raw_si.sr) & PAGE_MASK) == 
	   (unsigned long) sigstack){
		unsigned long *sr = (unsigned long *) raw_si.sr;
		unsigned long frame = (unsigned long) signal_frame_si.data;

		signal_frame_sc.sr_relative = 1;
		*sr -= raw_si.sr;
		*((unsigned long *) (frame + signal_frame_si.sr_index)) = *sr;
	}
	else signal_frame_si.sr_relative = 0;
	signal_frame_si.sp_index = raw_si.sp - base;

	if((munmap(stack, PAGE_SIZE) < 0) || 
	   (munmap(sigstack, PAGE_SIZE) < 0)){
		printf("capture_signal_stack : munmap failed - errno = %d\n", 
		       errno);
		exit(1);
	}
}

void set_sc_ip_sp(void *sc_ptr, unsigned long ip, unsigned long sp)
{
	struct sigcontext *sc = sc_ptr;

	SC_IP(sc) = ip;
	SC_SP(sc) = sp;
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

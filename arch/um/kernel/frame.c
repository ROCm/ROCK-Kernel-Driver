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
#include <sys/syscall.h>
#include <sys/mman.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/sigcontext.h>
#include "sysdep/ptrace.h"
#include "sysdep/sigcontext.h"
#include "frame_user.h"
#include "kern_util.h"
#include "user_util.h"
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
	CATCH_EINTR(n = waitpid(pid, &status, WUNTRACED));
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
	CATCH_EINTR(n = waitpid(pid, &status, WUNTRACED));
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
		printf("capture_stack : PTRACE_CONT failed - errno = %d\n", 
		       errno);
		exit(1);
	}
	CATCH_EINTR(n = waitpid(pid, &status, 0));
	if(n < 0){
		printf("capture_stack : waitpid failed - errno = %d\n", errno);
		exit(1);
	}
	if(!WIFSIGNALED(status) || (WTERMSIG(status) != 9)){
		printf("capture_stack : Expected exit signal 9, "
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

struct common_raw {
	void *stack;
	int size;
	unsigned long sig;
	unsigned long sr;
	unsigned long sp;	
	struct arch_frame_data_raw arch;
};

#define SA_RESTORER (0x04000000)

typedef unsigned long old_sigset_t;

struct old_sigaction {
	__sighandler_t handler;
	old_sigset_t sa_mask;
	unsigned long sa_flags;
	void (*sa_restorer)(void);
};

static void child_common(struct common_raw *common, sighandler_t handler,
			 int restorer, int flags)
{
	stack_t ss = ((stack_t) { .ss_sp	= common->stack,
				  .ss_flags	= 0,
				  .ss_size	= common->size });
	int err;

	if(ptrace(PTRACE_TRACEME, 0, 0, 0) < 0){
		printf("PTRACE_TRACEME failed, errno = %d\n", errno);
	}
	if(sigaltstack(&ss, NULL) < 0){
		printf("sigaltstack failed - errno = %d\n", errno);
		kill(os_getpid(), SIGKILL);
	}

	if(restorer){
		struct sigaction sa;

		sa.sa_handler = handler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_ONSTACK | flags;
		err = sigaction(SIGUSR1, &sa, NULL);
	}
	else {
		struct old_sigaction sa;

		sa.handler = handler;
		sa.sa_mask = 0;
		sa.sa_flags = (SA_ONSTACK | flags) & ~SA_RESTORER;
		err = syscall(__NR_sigaction, SIGUSR1, &sa, NULL);
	}
	
	if(err < 0){
		printf("sigaction failed - errno = %d\n", errno);
		kill(os_getpid(), SIGKILL);
	}

	os_stop_process(os_getpid());
}

/* Changed only during early boot */
struct sc_frame signal_frame_sc;

struct sc_frame signal_frame_sc_sr;

struct sc_frame_raw {
	struct common_raw common;
	unsigned long sc;
	int restorer;
};

/* Changed only during early boot */
static struct sc_frame_raw *raw_sc = NULL;

static void sc_handler(int sig, struct sigcontext sc)
{
	raw_sc->common.sig = (unsigned long) &sig;
	raw_sc->common.sr = frame_restorer();
	raw_sc->common.sp = frame_sp();
	raw_sc->sc = (unsigned long) &sc;
	setup_arch_frame_raw(&raw_sc->common.arch, &sc + 1, raw_sc->common.sr);

	os_stop_process(os_getpid());
	kill(os_getpid(), SIGKILL);
}

static int sc_child(void *arg)
{
	raw_sc = arg;
	child_common(&raw_sc->common, (sighandler_t) sc_handler, 
		     raw_sc->restorer, 0);
	return(-1);
}

/* Changed only during early boot */
struct si_frame signal_frame_si;

struct si_frame_raw {
	struct common_raw common;
	unsigned long sip;
	unsigned long si;
	unsigned long ucp;
	unsigned long uc;
};

/* Changed only during early boot */
static struct si_frame_raw *raw_si = NULL;

static void si_handler(int sig, siginfo_t *si, struct ucontext *ucontext)
{
	raw_si->common.sig = (unsigned long) &sig;
	raw_si->common.sr = frame_restorer();
	raw_si->common.sp = frame_sp();
	raw_si->sip = (unsigned long) &si;
	raw_si->si = (unsigned long) si;
	raw_si->ucp = (unsigned long) &ucontext;
	raw_si->uc = (unsigned long) ucontext;
	setup_arch_frame_raw(&raw_si->common.arch, 
			     ucontext->uc_mcontext.fpregs, raw_si->common.sr);
	
	os_stop_process(os_getpid());
	kill(os_getpid(), SIGKILL);
}

static int si_child(void *arg)
{
	raw_si = arg;
	child_common(&raw_si->common, (sighandler_t) si_handler, 1, 
 		     SA_SIGINFO);
	return(-1);
}

static int relative_sr(unsigned long sr, int sr_index, void *stack, 
		       void *framep)
{
	unsigned long *srp = (unsigned long *) sr;
	unsigned long frame = (unsigned long) framep;

	if((*srp & PAGE_MASK) == (unsigned long) stack){
		*srp -= sr;
		*((unsigned long *) (frame + sr_index)) = *srp;
		return(1);
	}
	else return(0);
}

static unsigned long capture_stack_common(int (*proc)(void *), void *arg, 
					  struct common_raw *common_in, 
					  void *top, void *sigstack, 
					  int stack_len, 
					  struct frame_common *common_out)
{
	unsigned long sig_top = (unsigned long) sigstack + stack_len, base;

	common_in->stack = (void *) sigstack;
	common_in->size = stack_len;
	common_out->len = capture_stack(proc, arg, top, sig_top, 
					&common_out->data);
	base = sig_top - common_out->len;
	common_out->sig_index = common_in->sig - base;
	common_out->sp_index = common_in->sp - base;
	common_out->sr_index = common_in->sr - base;
	common_out->sr_relative = relative_sr(common_in->sr, 
					      common_out->sr_index, sigstack, 
					      common_out->data);
	return(base);
}

void capture_signal_stack(void)
{
	struct sc_frame_raw raw_sc;
	struct si_frame_raw raw_si;
	void *stack, *sigstack;
	unsigned long top, base;

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

	/* Get the sigcontext, no sigrestorer layout */
	raw_sc.restorer = 0;
	base = capture_stack_common(sc_child, &raw_sc, &raw_sc.common, 
				    (void *) top, sigstack, PAGE_SIZE, 
				    &signal_frame_sc.common);

	signal_frame_sc.sc_index = raw_sc.sc - base;
	setup_arch_frame(&raw_sc.common.arch, &signal_frame_sc.common.arch);

	/* Ditto for the sigcontext, sigrestorer layout */
	raw_sc.restorer = 1;
	base = capture_stack_common(sc_child, &raw_sc, &raw_sc.common, 
				    (void *) top, sigstack, PAGE_SIZE, 
				    &signal_frame_sc_sr.common);
	signal_frame_sc_sr.sc_index = raw_sc.sc - base;
	setup_arch_frame(&raw_sc.common.arch, &signal_frame_sc_sr.common.arch);

	/* And the siginfo layout */

	base = capture_stack_common(si_child, &raw_si, &raw_si.common, 
				    (void *) top, sigstack, PAGE_SIZE, 
				    &signal_frame_si.common);
	signal_frame_si.sip_index = raw_si.sip - base;
	signal_frame_si.si_index = raw_si.si - base;
	signal_frame_si.ucp_index = raw_si.ucp - base;
	signal_frame_si.uc_index = raw_si.uc - base;
	setup_arch_frame(&raw_si.common.arch, &signal_frame_si.common.arch);

	if((munmap(stack, PAGE_SIZE) < 0) || 
	   (munmap(sigstack, PAGE_SIZE) < 0)){
		printf("capture_signal_stack : munmap failed - errno = %d\n", 
		       errno);
		exit(1);
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

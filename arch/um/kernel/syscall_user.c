/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

/* XXX FIXME : Ensure that SIGIO and SIGVTALRM can't happen immediately
 * after setting up syscall stack
 * block SIGVTALRM in any code that's under wait_for_stop
 */

#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <sys/ptrace.h>
#include <asm/unistd.h>
#include "sysdep/ptrace.h"
#include "sigcontext.h"
#include "ptrace_user.h"
#include "task.h"
#include "user_util.h"
#include "kern_util.h"

/* XXX Bogus */
#define ERESTARTSYS	512
#define ERESTARTNOINTR	513
#define ERESTARTNOHAND	514

struct {
	int syscall;
	int pid;
	int result;
	struct timeval start;
	struct timeval end;
} syscall_record[1024];

int syscall_index = 0;

extern int kern_timer_on;

void syscall_handler(int sig, struct uml_pt_regs *regs)
{
	void *sc;
	long result;
	int index, syscall;

	lock_syscall();
	if(syscall_index == 1024) syscall_index = 0;
	index = syscall_index;
	syscall_index++;
	unlock_syscall();

	syscall = regs->syscall;
	sc = regs->sc;
	sc_to_regs(regs, sc, syscall);
	SC_START_SYSCALL(sc);

	syscall_record[index].syscall = syscall;
	syscall_record[index].pid = current_pid();
	syscall_record[index].result = 0xdeadbeef;
	gettimeofday(&syscall_record[index].start, NULL);
	syscall_trace();
	result = execute_syscall(regs);

	/* regs->sc may have changed while the system call ran (there may
	 * have been an interrupt or segfault), so it needs to be refreshed.
	 */
	regs->sc = sc;

	SC_SET_SYSCALL_RETURN(sc, result);
	if((result == -ERESTARTNOHAND) || (result == -ERESTARTSYS) || 
	   (result == -ERESTARTNOINTR))
		do_signal(result);

	syscall_trace();
	syscall_record[index].result = result;
	gettimeofday(&syscall_record[index].end, NULL);
}

int do_syscall(void *task, int pid)
{
	unsigned long proc_regs[FRAME_SIZE];
	struct uml_pt_regs *regs;
	int syscall;

	if(ptrace_getregs(pid, proc_regs) < 0)
		tracer_panic("Couldn't read registers");
	syscall = PT_SYSCALL_NR(proc_regs);

	regs = TASK_REGS(task);
	UPT_SYSCALL_NR(regs) = syscall;

	if(syscall < 1) return(0);

	if((syscall != __NR_sigreturn) &&
	   ((unsigned long *) PT_IP(proc_regs) >= &_stext) && 
	   ((unsigned long *) PT_IP(proc_regs) <= &_etext))
		tracer_panic("I'm tracing myself and I can't get out");

	if(ptrace(PTRACE_POKEUSER, pid, PT_SYSCALL_NR_OFFSET, 
		  __NR_getpid) < 0)
		tracer_panic("do_syscall : Nullifying syscall failed, "
			     "errno = %d", errno);
	return(1);
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

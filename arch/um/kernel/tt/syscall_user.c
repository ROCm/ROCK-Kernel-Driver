/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <unistd.h>
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
#include "syscall_user.h"
#include "tt.h"


void syscall_handler_tt(int sig, union uml_pt_regs *regs)
{
	void *sc;
	long result;
	int index, syscall;

	syscall = UPT_SYSCALL_NR(regs);
	sc = UPT_SC(regs);
	SC_START_SYSCALL(sc);

	index = record_syscall_start(syscall);
	syscall_trace(regs, 0);
	result = execute_syscall(regs);

	/* regs->sc may have changed while the system call ran (there may
	 * have been an interrupt or segfault), so it needs to be refreshed.
	 */
	UPT_SC(regs) = sc;

	SC_SET_SYSCALL_RETURN(sc, result);

	syscall_trace(regs, 1);
	record_syscall_end(index, result);
}

int do_syscall(void *task, int pid, int local_using_sysemu)
{
	unsigned long proc_regs[FRAME_SIZE];
	union uml_pt_regs *regs;
	int syscall;

	if(ptrace_getregs(pid, proc_regs) < 0)
		tracer_panic("Couldn't read registers");
	syscall = PT_SYSCALL_NR(proc_regs);

	regs = TASK_REGS(task);
	UPT_SYSCALL_NR(regs) = syscall;

	if(syscall < 0)
		return(0);

	if((syscall != __NR_sigreturn) &&
	   ((unsigned long *) PT_IP(proc_regs) >= &_stext) && 
	   ((unsigned long *) PT_IP(proc_regs) <= &_etext))
		tracer_panic("I'm tracing myself and I can't get out");

	if(local_using_sysemu)
		return(1);

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

/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <signal.h>
#include <errno.h>
#include <asm/sigcontext.h>
#include "sysdep/ptrace.h"
#include "signal_user.h"
#include "user_util.h"
#include "kern_util.h"
#include "task.h"
#include "sigcontext.h"

void sig_handler_common_skas(int sig, struct sigcontext *sc)
{
	struct uml_pt_regs save_regs, *r;
	struct signal_info *info;
	int save_errno = errno;

	r = (struct uml_pt_regs *) TASK_REGS(get_current());
	save_regs = *r;
	r->is_user = 0;
	r->mode.skas.fault_addr = SC_FAULT_ADDR(sc);
	r->mode.skas.fault_type = SC_FAULT_TYPE(sc);
	r->mode.skas.trap_type = SC_TRAP_TYPE(sc);

	change_sig(SIGUSR1, 1);
	info = &sig_info[sig];
	if(!info->is_irq) unblock_signals();

	(*info->handler)(sig, r);

	*r = save_regs;
	errno = save_errno;
}

extern int missed_ticks[];

void user_signal(int sig, struct uml_pt_regs *regs)
{
	struct signal_info *info;

	if(sig == SIGVTALRM)
		missed_ticks[cpu()]++;
	regs->is_user = 1;
	regs->mode.skas.fault_addr = 0;
	regs->mode.skas.fault_type = 0;
	regs->mode.skas.trap_type = 0;
	info = &sig_info[sig];
	(*info->handler)(sig, regs);

	unblock_signals();
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

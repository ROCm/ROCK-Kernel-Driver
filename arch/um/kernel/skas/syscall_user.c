/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <signal.h>
#include "kern_util.h"
#include "syscall_user.h"
#include "sysdep/ptrace.h"
#include "sysdep/sigcontext.h"

/* XXX Bogus */
#define ERESTARTSYS	512
#define ERESTARTNOINTR	513
#define ERESTARTNOHAND	514

void handle_syscall(union uml_pt_regs *regs)
{
	long result;
	int index;

	index = record_syscall_start(UPT_SYSCALL_NR(regs));

	syscall_trace();
	result = execute_syscall(regs);

	REGS_SET_SYSCALL_RETURN(regs->skas.regs, result);
	if((result == -ERESTARTNOHAND) || (result == -ERESTARTSYS) || 
	   (result == -ERESTARTNOINTR))
		do_signal(result);

	syscall_trace();
	record_syscall_end(index, result);
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

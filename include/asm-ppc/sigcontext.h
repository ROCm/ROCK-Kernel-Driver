/*
 * BK Id: SCCS/s.sigcontext.h 1.5 05/17/01 18:14:25 cort
 */
#ifndef _ASM_PPC_SIGCONTEXT_H
#define _ASM_PPC_SIGCONTEXT_H

#include <asm/ptrace.h>


struct sigcontext_struct {
	unsigned long	_unused[4];
	int		signal;
	unsigned long	handler;
	unsigned long	oldmask;
	struct pt_regs 	*regs;
};

#endif

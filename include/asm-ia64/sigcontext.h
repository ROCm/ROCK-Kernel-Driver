#ifndef _ASM_IA64_SIGCONTEXT_H
#define _ASM_IA64_SIGCONTEXT_H

/*
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <asm/fpu.h>

#define IA64_SC_FLAG_ONSTACK_BIT		1	/* is handler running on signal stack? */
#define IA64_SC_FLAG_IN_SYSCALL_BIT		1	/* did signal interrupt a syscall? */
#define IA64_SC_FLAG_FPH_VALID_BIT		2	/* is state in f[32]-f[127] valid? */

#define IA64_SC_FLAG_ONSTACK		(1 << IA64_SC_FLAG_ONSTACK_BIT)
#define IA64_SC_FLAG_IN_SYSCALL		(1 << IA64_SC_FLAG_IN_SYSCALL_BIT)
#define IA64_SC_FLAG_FPH_VALID		(1 << IA64_SC_FLAG_FPH_VALID_BIT)

# ifndef __ASSEMBLY__

struct sigcontext {
	unsigned long		sc_flags;	/* see manifest constants above */
	unsigned long		sc_nat;		/* bit i == 1 iff scratch reg gr[i] is a NaT */
	stack_t			sc_stack;	/* previously active stack */

	unsigned long		sc_ip;		/* instruction pointer */
	unsigned long		sc_cfm;		/* current frame marker */
	unsigned long		sc_um;		/* user mask bits */
	unsigned long		sc_ar_rsc;	/* register stack configuration register */
	unsigned long		sc_ar_bsp;	/* backing store pointer */
	unsigned long		sc_ar_rnat;	/* RSE NaT collection register */
	unsigned long		sc_ar_ccv;	/* compare and exchange compare value register */
	unsigned long		sc_ar_unat;	/* ar.unat of interrupted context */
	unsigned long		sc_ar_fpsr;	/* floating-point status register */
	unsigned long		sc_ar_pfs;	/* previous function state */
	unsigned long		sc_ar_lc;	/* loop count register */
	unsigned long		sc_pr;		/* predicate registers */
	unsigned long		sc_br[8];	/* branch registers */
	unsigned long		sc_gr[32];	/* general registers (static partition) */
	struct ia64_fpreg	sc_fr[128];	/* floating-point registers */

	/*
	 * The mask must come last so we can increase _NSIG_WORDS
	 * without breaking binary compatibility.
	 */
	sigset_t		sc_mask;	/* signal mask to restore after handler returns */
};

# endif /* __ASSEMBLY__ */
#endif /* _ASM_IA64_SIGCONTEXT_H */

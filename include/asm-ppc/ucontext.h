/*
 * BK Id: SCCS/s.ucontext.h 1.5 05/17/01 18:14:26 cort
 */
#ifndef _ASMPPC_UCONTEXT_H
#define _ASMPPC_UCONTEXT_H

/* Copied from i386. */

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	struct sigcontext_struct uc_mcontext;
	sigset_t	  uc_sigmask;	/* mask last for extensibility */
};

#endif /* !_ASMPPC_UCONTEXT_H */

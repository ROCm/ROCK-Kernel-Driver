/*
 *  include/asm-s390/ucontext.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/ucontext.h"
 */

#ifndef _ASM_S390_UCONTEXT_H
#define _ASM_S390_UCONTEXT_H

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	sigset_t	  uc_sigmask;	/* mask last for extensibility */
	struct sigcontext *sc; /* Added for pthread support */
};



#endif /* !_ASM_S390_UCONTEXT_H */

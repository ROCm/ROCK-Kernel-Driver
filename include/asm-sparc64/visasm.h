/* $Id: visasm.h,v 1.4 1999/04/19 01:25:55 davem Exp $ */
#ifndef _SPARC64_VISASM_H
#define _SPARC64_VISASM_H

/* visasm.h:  FPU saving macros for VIS routines
 *
 * Copyright (C) 1998 Jakub Jelinek (jj@ultra.linux.cz)
 */
 
#include <asm/pstate.h>
#include <asm/ptrace.h>

#define AOFF_task_fpregs	(((ASIZ_task) + (64 - 1)) & ~(64 - 1))
 
/* Clobbers %o5, %g1, %g2, %g3, %g7, %icc, %xcc */

#define VISEntry					\
	rd		%fprs, %o5;			\
	andcc		%o5, (FPRS_FEF|FPRS_DU), %g0;	\
	be,pt		%icc, 297f;			\
	 sethi		%hi(297f), %g7;			\
	ba,pt		%xcc, VISenter;			\
	 or		%g7, %lo(297f), %g7;		\
297:	wr		%g0, FPRS_FEF, %fprs;		\

#define VISExit						\
	wr		%g0, 0, %fprs;

/* Clobbers %o5, %g1, %g2, %g3, %g7, %icc, %xcc.
 * Must preserve %o5 between VISEntryHalf and VISExitHalf */

#define VISEntryHalf					\
	rd		%fprs, %o5;			\
	andcc		%o5, FPRS_FEF, %g0;		\
	be,pt		%icc, 297f;			\
	 sethi		%hi(298f), %g7;			\
	ba,pt		%xcc, VISenterhalf;		\
	 or		%g7, %lo(298f), %g7;		\
	clr		%o5;				\
297:	wr		%o5, FPRS_FEF, %fprs;		\
298:

#define VISExitHalf					\
	wr		%o5, 0, %fprs;

#ifndef __ASSEMBLY__	
extern __inline__ void save_and_clear_fpu(void) {
	__asm__ __volatile__ ("
		rd %%fprs, %%o5
		andcc %%o5, %0, %%g0
		be,pt %%icc, 299f
		 sethi %%hi(298f), %%g7
		ba VISenter	! Note. This cannot be bp, as it may be too far from VISenter.
		 or %%g7, %%lo(298f), %%g7
	298:	wr %%g0, 0, %%fprs
	299:
		" : : "i" (FPRS_FEF|FPRS_DU) :
		"o5", "g1", "g2", "g3", "g7", "cc");
}
#endif

#endif /* _SPARC64_ASI_H */

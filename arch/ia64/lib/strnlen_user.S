/*
 * Returns 0 if exception before NUL or reaching the supplied limit (N),
 * a value greater than N if the string is longer than the limit, else
 * strlen.
 *
 * Inputs:
 *	in0:	address of buffer
 *	in1:	string length limit N
 * Outputs:
 *	r8:	0 in case of fault, strlen(buffer)+1 otherwise
 * 
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <asm/asmmacro.h>

/* If a fault occurs, r8 gets set to -EFAULT and r9 gets cleared.  */
#define EX(x...)				\
	.section __ex_table,"a";		\
	data4 @gprel(99f);			\
	data4 (.Lexit-99f)|1;			\
	.previous				\
99:	x;

	.text
	.psr abi64
	.psr lsb
	.lsb

GLOBAL_ENTRY(__strnlen_user)
	UNW(.prologue)
	alloc r2=ar.pfs,2,0,0,0
	UNW(.save ar.lc, r16)
	mov r16=ar.lc			// preserve ar.lc

	UNW(.body)

	add r3=-1,in1
	;;
	mov ar.lc=r3
	mov r9=0

	// XXX braindead strlen loop---this needs to be optimized
.Loop1:
	EX(ld1 r8=[in0],1)
	add r9=1,r9
	;;
	cmp.eq p6,p0=r8,r0
(p6)	br.dpnt.few .Lexit
	br.cloop.dptk.few .Loop1

	add r9=1,in1			// NUL not found---return N+1
	;;
.Lexit:
	mov r8=r9
	mov ar.lc=r16			// restore ar.lc
	br.ret.sptk.few rp
END(__strnlen_user)

#ifndef _ASM_X86_64_SIGCONTEXT_H
#define _ASM_X86_64_SIGCONTEXT_H

#include <asm/types.h>

/*
 * The first part of "struct _fpstate" is just the normal i387
 * hardware setup, the extra "status" word is used to save the
 * coprocessor status word before entering the handler.
 *
 * Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 *
 * The FPU state data structure has had to grow to accomodate the
 * extended FPU state required by the Streaming SIMD Extensions.
 * There is no documented standard to accomplish this at the moment.
 */
struct _fpreg {
	unsigned short significand[4];
	unsigned short exponent;
};

struct _fpxreg {
	unsigned short significand[4];
	unsigned short exponent;
	unsigned short padding[3];
};

struct _xmmreg {
	__u32	element[4];
};


/* This is FXSAVE layout without 64bit prefix thus 32bit compatible. 
   This means that the IP and DPs are only 32bit and are not useful
   in 64bit space.
   If someone used them we would need to switch to 64bit FXSAVE.   
*/ 
struct _fpstate {
	/* Regular FPU environment */
	__u32 	cw;
	__u32	sw;
	__u32	tag;
	__u32	ipoff;
	__u32	cssel;
	__u32	dataoff;
	__u32	datasel;
	struct _fpreg	_st[8];
	unsigned short	status;
	unsigned short	magic;		/* 0xffff = regular FPU data only */

	/* FXSR FPU environment */
	__u32	_fxsr_env[6];
	__u32	mxcsr;
	__u32	reserved;
	struct _fpxreg	_fxsr_st[8];
	struct _xmmreg	_xmm[8];	/* It's actually 16 */ 
	__u32	padding[56];
};

#define X86_FXSR_MAGIC		0x0000

struct sigcontext { 
	unsigned short gs, __gsh;
	unsigned short fs, __fsh;
	unsigned short es, __esh;
	unsigned short ds, __dsh;
	unsigned long r8;
	unsigned long r9;
	unsigned long r10;
	unsigned long r12;
	unsigned long r13;
	unsigned long r14;
	unsigned long r15;
	unsigned long rdi;
	unsigned long rsi;
	unsigned long rbp;
	unsigned long rbx;
	unsigned long rdx;
	unsigned long rax;
	unsigned long trapno;
	unsigned long err;
	unsigned long rip;
	unsigned short cs, __csh;
	unsigned int __pad0;
	unsigned long eflags;
	unsigned long rsp_at_signal;
	struct _fpstate * fpstate;
	unsigned long oldmask;
	unsigned long cr2;
	unsigned long r11;
	unsigned long rcx;
	unsigned long rsp;
};


#endif

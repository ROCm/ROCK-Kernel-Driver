#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <asm/byteorder.h>

#define add_ssaaaa(sh, sl, ah, al, bh, bl) 				\
  __asm__ ("addcc %r4,%5,%1
	addx %r2,%3,%0"							\
	   : "=r" ((USItype)(sh)),					\
	     "=&r" ((USItype)(sl))					\
	   : "%rJ" ((USItype)(ah)),					\
	     "rI" ((USItype)(bh)),					\
	     "%rJ" ((USItype)(al)),					\
	     "rI" ((USItype)(bl))					\
	   : "cc")
#define sub_ddmmss(sh, sl, ah, al, bh, bl) 				\
  __asm__ ("subcc %r4,%5,%1
	subx %r2,%3,%0"							\
	   : "=r" ((USItype)(sh)),					\
	     "=&r" ((USItype)(sl))					\
	   : "rJ" ((USItype)(ah)),					\
	     "rI" ((USItype)(bh)),					\
	     "rJ" ((USItype)(al)),					\
	     "rI" ((USItype)(bl))					\
	   : "cc")

#define umul_ppmm(w1, w0, u, v) \
  __asm__ ("! Inlined umul_ppmm
	wr	%%g0,%2,%%y	! SPARC has 0-3 delay insn after a wr
	sra	%3,31,%%g2	! Don't move this insn
	and	%2,%%g2,%%g2	! Don't move this insn
	andcc	%%g0,0,%%g1	! Don't move this insn
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,%3,%%g1
	mulscc	%%g1,0,%%g1
	add	%%g1,%%g2,%0
	rd	%%y,%1"							\
	   : "=r" ((USItype)(w1)),					\
	     "=r" ((USItype)(w0))					\
	   : "%rI" ((USItype)(u)),					\
	     "r" ((USItype)(v))						\
	   : "%g1", "%g2", "cc")

/* It's quite necessary to add this much assembler for the sparc.
   The default udiv_qrnnd (in C) is more than 10 times slower!  */
#define udiv_qrnnd(q, r, n1, n0, d) \
  __asm__ ("! Inlined udiv_qrnnd
	mov	32,%%g1
	subcc	%1,%2,%%g0
1:	bcs	5f
	 addxcc %0,%0,%0	! shift n1n0 and a q-bit in lsb
	sub	%1,%2,%1	! this kills msb of n
	addx	%1,%1,%1	! so this can't give carry
	subcc	%%g1,1,%%g1
2:	bne	1b
	 subcc	%1,%2,%%g0
	bcs	3f
	 addxcc %0,%0,%0	! shift n1n0 and a q-bit in lsb
	b	3f
	 sub	%1,%2,%1	! this kills msb of n
4:	sub	%1,%2,%1
5:	addxcc	%1,%1,%1
	bcc	2b
	 subcc	%%g1,1,%%g1
! Got carry from n.  Subtract next step to cancel this carry.
	bne	4b
	 addcc	%0,%0,%0	! shift n1n0 and a 0-bit in lsb
	sub	%1,%2,%1
3:	xnor	%0,0,%0
	! End of inline udiv_qrnnd"					\
	   : "=&r" ((USItype)(q)),					\
	     "=&r" ((USItype)(r))					\
	   : "r" ((USItype)(d)),					\
	     "1" ((USItype)(n1)),					\
	     "0" ((USItype)(n0)) : "%g1", "cc")
#define UDIV_NEEDS_NORMALIZATION 0

#define abort()								\
	return 0

#ifdef __BIG_ENDIAN
#define __BYTE_ORDER __BIG_ENDIAN
#else
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif

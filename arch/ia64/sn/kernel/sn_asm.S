
/*
 * Copyright (c) 2000-2001 Silicon Graphics, Inc.  All rights reserved.
 */

#include <linux/config.h>
#ifdef CONFIG_IA64_SGI_AUTOTEST

// Testing only.
// Routine will cause MCAs
//   zzzmsa(n)
//      n=0 MCA via duplicate TLB dropin
//      n=0 MCA via read of garbage address
//

#define ITIR(key, ps)           ((key<<8) | (ps<<2))
#define TLB_PAGESIZE            28                      // Use 256MB pages for now.

                        .global zzzmca
                        .proc   zzzmca
zzzmca:
                        alloc   loc4       = ar.pfs,2,8,1,0;;
                        cmp.ne  p6,p0=r32,r0;;
                        movl    r2=0x2dead
                        movl    r3=0x3dead
                        movl    r15=0x15dead
                        movl    r16=0x16dead
                        movl    r31=0x31dead
                        movl    loc0=0x34beef
                        movl    loc1=0x35beef
                        movl    loc2=0x36beef
                        movl    loc3=0x37beef
                        movl    out0=0x42beef

                        movl    r20=0x32feed;;
                        mov     ar32=r20
                        movl    r20=0x36feed;;
                        mov     ar36=r20
                        movl    r20=0x65feed;;
                        mov     ar65=r20
                        movl    r20=0x66feed;;
                        mov     ar66=r20

(p6)                    br.cond.sptk    1f

                        rsm      0x2000;;
                        srlz.d;
                        mov      r11      = 1
                        mov      r3       = ITIR(0,TLB_PAGESIZE);;
                        mov      cr.itir  = r3
                        mov      r10      = 0;;
                        itr.d    dtr[r11] = r10;;
                        mov      r11      = 2

                        itr.d    dtr[r11] = r10;;
                        br      9f

1:                      movl    r8=0xfe00000048;;
                        ld8     r9=[r8];;
                        mf
                        mf.a
                        srlz.d

9:                      mov     ar.pfs=loc4
                        br.ret.sptk     rp

                        .endp   zzzmca

                        .global zzzspec
                        .proc   zzzspec
zzzspec:
			mov	r8=r32
			movl	r9=0xe000000000000000
			movl	r10=0x4000;;
			ld8.s	r16=[r8];;
			ld8.s	r17=[r9];;
			add	r8=r8,r10;;
			ld8.s	r18=[r8];;
			add	r8=r8,r10;;
			ld8.s	r19=[r8];;
			add	r8=r8,r10;;
			ld8.s	r20=[r8];;
			mov	r8=r0
			tnat.nz	p6,p0=r16
			tnat.nz	p7,p0=r17
			tnat.nz	p8,p0=r18
			tnat.nz	p9,p0=r19
			tnat.nz	p10,p0=r20;;
		(p6)	dep	r8=-1,r8,0,1;;
		(p7)	dep	r8=-1,r8,1,1;;
		(p8)	dep	r8=-1,r8,2,1;;
		(p9)	dep	r8=-1,r8,3,1;;
		(p10)	dep	r8=-1,r8,4,1;;
                        br.ret.sptk     rp
                        .endp   zzzspec

                        .global zzzspec2
                        .proc   zzzspec2
zzzspec2:
			cmp.eq	p6,p7=r2,r2
			movl	r16=0xc0000a0001000020
			;;
			mf
			;;
			ld8	r9=[r16]
		(p6)	br.spnt	1f
			ld8	r10=[r32]
			;;
		1:	mf.a
			mf

			ld8	r9=[r16];;
			cmp.ne	p6,p7=r9,r16
		(p6)	br.spnt	1f
			ld8	r10=[r32]
			;;
		1:	mf.a
			mf

			ld8	r9=[r33];;
			cmp.ne	p6,p7=r9,r33
		(p6)	br.spnt	1f
			ld8	r10=[r32]
			;;
		1:	mf.a
			mf

			tpa	r23=r32
			add	r20=512,r33
			add	r21=1024,r33;;
			ld8	r9=[r20]
			ld8	r10=[r21];;
			nop.i	0
			{ .mib
			nop.m 0
			cmp.ne	p6,p7=r10,r33
		(p6)	br.spnt	1f
			}
			ld8	r10=[r32]
			;;
		1:	mf.a
			mf
			br.ret.sptk     rp

                        .endp   zzzspec

#endif


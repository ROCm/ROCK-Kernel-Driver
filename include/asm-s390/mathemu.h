/*
 *  arch/s390/kernel/mathemu.h
 *    IEEE floating point emulation.
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#ifndef __MATHEMU__
#define __MATHEMU__

extern int math_emu_b3(__u8 *, struct pt_regs *);
extern int math_emu_ed(__u8 *, struct pt_regs *);
extern void math_emu_ldr(__u8 *);
extern void math_emu_ler(__u8 *);
extern void math_emu_std(__u8 *, struct pt_regs *);
extern void math_emu_ld(__u8 *, struct pt_regs *);
extern void math_emu_ste(__u8 *, struct pt_regs *);
extern void math_emu_le(__u8 *, struct pt_regs *);
extern int math_emu_lfpc(__u8 *, struct pt_regs *);
extern int math_emu_stfpc(__u8 *, struct pt_regs *);
extern int math_emu_srnm(__u8 *, struct pt_regs *);


extern __u64 __adddf3(__u64,__u64);
extern __u64 __subdf3(__u64,__u64);
extern __u64 __muldf3(__u64,__u64);
extern __u64 __divdf3(__u64,__u64);
extern long  __cmpdf2(__u64,__u64);
extern __u64 __negdf2(__u64);
extern __u64 __absdf2(__u64);
extern __u32 __addsf3(__u32,__u32);
extern __u32 __subsf3(__u32,__u32);
extern __u32 __mulsf3(__u32,__u32);
extern __u32 __divsf3(__u32,__u32);
extern __u32 __negsf2(__u32);
extern __u32 __abssf2(__u32);
extern long  __cmpsf2(__u32,__u32);
extern __u32 __truncdfsf2(__u64);
extern __u32 __fixsfsi(__u32);
extern __u32 __fixdfsi(__u64);
extern __u64  __floatsidf(__u32);
extern __u32  __floatsisf(__u32);
extern __u64  __extendsfdf2(__u32);

#endif                                 /* __MATHEMU__                      */


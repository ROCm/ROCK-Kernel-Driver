/*
 *  include/asm-s390/s390-regs-common.h
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *
 *  this file is designed to keep as much compatibility between
 *  gdb's representation of registers & the kernels representation of registers
 *  as possible so as to minimise translation between gdb registers &
 *  kernel registers please keep this matched with gdb & strace 
 */

#ifndef _S390_REGS_COMMON_H
#define _S390_REGS_COMMON_H
#ifndef __ASSEMBLY__
#include <asm/types.h>
#endif

#define REGISTER_SIZE 4
#define NUM_GPRS      16
#define GPR_SIZE      4
#define PSW_MASK_SIZE 4
#define PSW_ADDR_SIZE 4
#define NUM_FPRS      16
#define FPR_SIZE      8
#define FPC_SIZE      4
#define FPC_PAD_SIZE  4 /* gcc insists on aligning the fpregs */
#define NUM_CRS       16
#define CR_SIZE       4
#define NUM_ACRS      16
#define ACR_SIZE      4

#define STACK_FRAME_OVERHEAD    96      /* size of minimum stack frame */

#ifndef __ASSEMBLY__
/* this typedef defines how a Program Status Word looks like */
typedef struct 
{
        __u32   mask;
        __u32   addr;
} psw_t __attribute__ ((aligned(8)));

typedef __u32 gpr_t;

/* 2 __u32's are used for floats instead to compile  with a __STRICT_ANSI__ defined */ 
typedef union
{
#ifdef __KERNEL__ 
	__u64   d; /* mathemu.h gets upset otherwise */
#else
	double  d; /* ansi c dosen't like long longs & make sure that */
	/* alignments are identical for both compiles */ 
#endif
       struct
       {
	       __u32 hi;
	       __u32 lo;
       } fp;
       __u32    f; 
} freg_t;

typedef struct
{
/*
  The compiler appears to like aligning freg_t on an 8 byte boundary
  so I always access fpregs, this was causing fun when I was doing
  coersions.
 */
	__u32   fpc;
	freg_t  fprs[NUM_FPRS];              
} s390_fp_regs;

/*
  gdb structures & the kernel have this much always in common
 */
#define S390_REGS_COMMON       \
psw_t psw;                     \
__u32 gprs[NUM_GPRS];          \
__u32  acrs[NUM_ACRS];         \

typedef struct
{
	S390_REGS_COMMON
} s390_regs_common;


/* Sequence of bytes for breakpoint illegal instruction.  */
#define S390_BREAKPOINT {0x0,0x1}
#define S390_BREAKPOINT_U16 ((__u16)0x0001)
#define S390_SYSCALL_OPCODE ((__u16)0x0a00)
#define S390_SYSCALL_SIZE   2
#define ADDR_BITS_REMOVE(addr) ((addr)&0x7fffffff)
#endif
#endif










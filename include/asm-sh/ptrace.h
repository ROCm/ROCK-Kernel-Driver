#ifndef __ASM_SH_PTRACE_H
#define __ASM_SH_PTRACE_H

/*
 * Copyright (C) 1999, 2000  Niibe Yutaka
 *
 */

/*
 * GCC defines register number like this:
 * -----------------------------
 *	 0 - 15 are integer registers
 *	17 - 22 are control/special registers
 *	24 - 39 fp registers
 *	40 - 47 xd registers
 *	48 -    fpscr register
 * -----------------------------
 *
 * We follows above, except:
 *	16 --- program counter (PC)
 *	22 --- syscall #
 *	23 --- floating point communication register
 */
#define REG_REG0	 0
#define REG_REG15	15

#define REG_PC		16

#define REG_PR		17
#define REG_SR		18
#define REG_GBR      	19
#define REG_MACH	20
#define REG_MACL	21

#define REG_SYSCALL	22

#define REG_FPUL	23

#define REG_FPREG0	24
#define REG_FPREG15	39
#define REG_XDREG0	40
#define REG_XDREG14	47
#define REG_FPSCR	48

#define PTRACE_SETOPTIONS         21

/* options set using PTRACE_SETOPTIONS */
#define PTRACE_O_TRACESYSGOOD     0x00000001

/*
 * This struct defines the way the registers are stored on the
 * kernel stack during a system call or other kernel entry.
 */
struct pt_regs {
	unsigned long regs[16];
	unsigned long pc;
	unsigned long pr;
	unsigned long sr;
	unsigned long gbr;
	unsigned long mach;
	unsigned long macl;
	long syscall_nr;
};

#ifdef __KERNEL__
#define user_mode(regs) (((regs)->sr & 0x40000000)==0)
#define instruction_pointer(regs) ((regs)->pc)
extern void show_regs(struct pt_regs *);

/* User Break Controller */

#if defined(__sh3__)
#define UBC_BARA                0xffffffb0
#define UBC_BAMRA               0xffffffb4
#define UBC_BBRA                0xffffffb8
#define UBC_BASRA               0xffffffe4
#define UBC_BARB                0xffffffa0
#define UBC_BAMRB               0xffffffa4
#define UBC_BBRB                0xffffffa8
#define UBC_BASRB               0xffffffe8
#define UBC_BDRB                0xffffff90
#define UBC_BDMRB               0xffffff94
#define UBC_BRCR                0xffffff98
#elif defined(__SH4__)
#define UBC_BARA		0xff200000
#define UBC_BAMRA		0xff200004
#define UBC_BBRA		0xff200008
#define UBC_BASRA		0xff000014
#define UBC_BARB		0xff20000c
#define UBC_BAMRB		0xff200010
#define UBC_BBRB		0xff200014
#define UBC_BASRB		0xff000018
#define UBC_BDRB		0xff200018
#define UBC_BDMRB		0xff20001c
#define UBC_BRCR		0xff200020
#endif

#define BAMR_ASID		(1 << 2)
#define BAMR_NONE		0
#define BAMR_10			0x1
#define BAMR_12			0x2
#define BAMR_ALL		0x3
#define BAMR_16			0x8
#define BAMR_20			0x9

#define BBR_INST		(1 << 4)
#define BBR_DATA		(2 << 4)
#define BBR_READ		(1 << 2)
#define BBR_WRITE		(2 << 4)
#define BBR_BYTE		0x1
#define BBR_HALF		0x2
#define BBR_LONG		0x3
#define BBR_QUAD		(1 << 6)

#define BRCR_CMFA		(1 << 15)
#define BRCR_CMFB		(1 << 14)
#define BRCR_PCBA		(1 << 10)	/* 1: after execution */
#define BRCR_DBEB		(1 << 7)
#define BRCR_PCBB		(1 << 6)
#define BRCR_SEQ		(1 << 3)
#define BRCR_UBDE		(1 << 0)
#endif

#endif /* __ASM_SH_PTRACE_H */

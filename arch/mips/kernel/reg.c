/*
 * offset.c: Calculate pt_regs and task_struct indices.
 *
 * Copyright (C) 1996 David S. Miller
 * Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003 Ralf Baechle
 */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

#include <asm/ptrace.h>
#include <asm/processor.h>

#define text(t) __asm__("\n@@@" t)
#define _offset(type, member) ((unsigned long) &(((type *)NULL)->member))
#define index(string, ptr, member) \
	__asm__("\n@@@" string "%0" : : "i" (_offset(ptr, member)/sizeof(long)))
#define size(string, size) \
	__asm__("\n@@@" string "%0" : : "i" (sizeof(size)))
#define linefeed text("")

void output_ptreg_defines(void)
{
	text("/* MIPS pt_regs indices. */");
	index("#define EF_R0     ", struct pt_regs, regs[0]);
	index("#define EF_R1     ", struct pt_regs, regs[1]);
	index("#define EF_R2     ", struct pt_regs, regs[2]);
	index("#define EF_R3     ", struct pt_regs, regs[3]);
	index("#define EF_R4     ", struct pt_regs, regs[4]);
	index("#define EF_R5     ", struct pt_regs, regs[5]);
	index("#define EF_R6     ", struct pt_regs, regs[6]);
	index("#define EF_R7     ", struct pt_regs, regs[7]);
	index("#define EF_R8     ", struct pt_regs, regs[8]);
	index("#define EF_R9     ", struct pt_regs, regs[9]);
	index("#define EF_R10    ", struct pt_regs, regs[10]);
	index("#define EF_R11    ", struct pt_regs, regs[11]);
	index("#define EF_R12    ", struct pt_regs, regs[12]);
	index("#define EF_R13    ", struct pt_regs, regs[13]);
	index("#define EF_R14    ", struct pt_regs, regs[14]);
	index("#define EF_R15    ", struct pt_regs, regs[15]);
	index("#define EF_R16    ", struct pt_regs, regs[16]);
	index("#define EF_R17    ", struct pt_regs, regs[17]);
	index("#define EF_R18    ", struct pt_regs, regs[18]);
	index("#define EF_R19    ", struct pt_regs, regs[19]);
	index("#define EF_R20    ", struct pt_regs, regs[20]);
	index("#define EF_R21    ", struct pt_regs, regs[21]);
	index("#define EF_R22    ", struct pt_regs, regs[22]);
	index("#define EF_R23    ", struct pt_regs, regs[23]);
	index("#define EF_R24    ", struct pt_regs, regs[24]);
	index("#define EF_R25    ", struct pt_regs, regs[25]);
	index("#define EF_R26    ", struct pt_regs, regs[26]);
	index("#define EF_R27    ", struct pt_regs, regs[27]);
	index("#define EF_R28    ", struct pt_regs, regs[28]);
	index("#define EF_R29    ", struct pt_regs, regs[29]);
	index("#define EF_R30    ", struct pt_regs, regs[30]);
	index("#define EF_R31    ", struct pt_regs, regs[31]);
	linefeed;
	index("#define EF_LO     ", struct pt_regs, lo);
	index("#define EF_HI     ", struct pt_regs, hi);
	linefeed;
	index("#define EF_EPC    ", struct pt_regs, cp0_epc);
	index("#define EF_BVADDR ", struct pt_regs, cp0_badvaddr);
	index("#define EF_STATUS ", struct pt_regs, cp0_status);
	index("#define EF_CAUSE  ", struct pt_regs, cp0_cause);
	linefeed;
	size("#define EF_SIZE   ", struct pt_regs);
	linefeed;
}

/*    $Id: unaligned.c,v 1.1 2002/07/20 16:27:06 rhirst Exp $
 *
 *    Unaligned memory access handler
 *
 *    Copyright (C) 2001 Randolph Chung <tausq@debian.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/atomic.h>

#include <asm/smp.h>
#include <asm/pdc.h>

/* #define DEBUG_UNALIGNED 1 */

#ifdef DEBUG_UNALIGNED
#define DPRINTF(fmt, args...) do { printk(KERN_DEBUG "%s:%d:%s ", __FILE__, __LINE__, __FUNCTION__ ); printk(KERN_DEBUG fmt, ##args ); } while (0)
#else
#define DPRINTF(fmt, args...)
#endif

#ifdef __LP64__
#define RFMT "%016lx"
#else
#define RFMT "%08lx"
#endif

/* 1111 1100 0000 0000 0001 0011 1100 0000 */
#define OPCODE1(a,b,c)	((a)<<26|(b)<<12|(c)<<6) 
#define OPCODE2(a,b)	((a)<<26|(b)<<1)
#define OPCODE3(a,b)	((a)<<26|(b)<<2)
#define OPCODE4(a)	((a)<<26)
#define OPCODE1_MASK	OPCODE1(0x3f,1,0xf)
#define OPCODE2_MASK 	OPCODE2(0x3f,1)
#define OPCODE3_MASK	OPCODE3(0x3f,1)
#define OPCODE4_MASK    OPCODE4(0x3f)

/* skip LDB (index) */
#define OPCODE_LDH_I	OPCODE1(0x03,0,0x1)
#define OPCODE_LDW_I	OPCODE1(0x03,0,0x2)
#define OPCODE_LDD_I	OPCODE1(0x03,0,0x3)
#define OPCODE_LDDA_I	OPCODE1(0x03,0,0x4)
/* skip LDCD (index) */
#define OPCODE_LDWA_I	OPCODE1(0x03,0,0x6)
/* skip LDCW (index) */
/* skip LDB (short) */
#define OPCODE_LDH_S	OPCODE1(0x03,1,0x1)
#define OPCODE_LDW_S	OPCODE1(0x03,1,0x2)
#define OPCODE_LDD_S	OPCODE1(0x03,1,0x3)
#define OPCODE_LDDA_S	OPCODE1(0x03,1,0x4)
/* skip LDCD (short) */
#define OPCODE_LDWA_S	OPCODE1(0x03,1,0x6)
/* skip LDCW (short) */
/* skip STB */
#define OPCODE_STH	OPCODE1(0x03,1,0x9)
#define OPCODE_STW	OPCODE1(0x03,1,0xa)
#define OPCODE_STD	OPCODE1(0x03,1,0xb)
/* skip STBY */
/* skip STDBY */
#define OPCODE_STWA	OPCODE1(0x03,1,0xe)
#define OPCODE_STDA	OPCODE1(0x03,1,0xf)

#define OPCODE_LDD_L	OPCODE2(0x14,0)
#define OPCODE_FLDD_L	OPCODE2(0x14,1)
#define OPCODE_STD_L	OPCODE2(0x1c,0)
#define OPCODE_FSTD_L	OPCODE2(0x1c,1)

#define OPCODE_LDW_M	OPCODE3(0x17,1)
#define OPCODE_FLDW_L	OPCODE3(0x17,0)
#define OPCODE_FSTW_L	OPCODE3(0x1f,0)
#define OPCODE_STW_M	OPCODE3(0x1f,1)

#define OPCODE_LDH_L    OPCODE4(0x11)
#define OPCODE_LDW_L    OPCODE4(0x12)
#define OPCODE_LDW_L2   OPCODE4(0x13)
#define OPCODE_STH_L    OPCODE4(0x19)
#define OPCODE_STW_L    OPCODE4(0x1A)
#define OPCODE_STW_L2   OPCODE4(0x1B)



void die_if_kernel (char *str, struct pt_regs *regs, long err);

static int emulate_load(struct pt_regs *regs, int len, int toreg)
{
	unsigned long saddr = regs->ior;
	unsigned long val = 0;
	int ret = 0;

	if (regs->isr != regs->sr[7])
	{
		printk(KERN_CRIT "isr verification failed (isr: " RFMT ", sr7: " RFMT "\n",
			regs->isr, regs->sr[7]);
		return 1;
	}

	DPRINTF("load " RFMT ":" RFMT " to r%d for %d bytes\n", 
		regs->isr, regs->ior, toreg, len);

	__asm__ __volatile__  (
"       mfsp %%sr1, %%r20\n"
"       mtsp %6, %%sr1\n"
"	copy %%r0, %0\n"
"0:	ldbs,ma	1(%%sr1,%4), %%r19\n"
"	addi -1, %5, %5\n"
"	cmpib,>= 0, %5, 2f\n"
"	or %%r19, %0, %0\n"
"	b 0b\n"
	
#ifdef __LP64__
	"depd,z %0, 55, 56, %0\n"
#else
	"depw,z %0, 23, 24, %0\n"
#endif
	
"1:	ldi	10, %1\n"
"2:     mtsp %%r20, %%sr1\n"
"	.section __ex_table,\"a\"\n"
#ifdef __LP64__
	".dword 0b, (1b-0b)\n"
#else
	".word 0b, (1b-0b)\n"
#endif
	".previous\n" 
	: "=r" (val), "=r" (ret)
	: "0" (val), "1" (ret), "r" (saddr), "r" (len), "r" (regs->isr)
	: "r19", "r20" );

	DPRINTF("val = 0x" RFMT "\n", val);

	regs->gr[toreg] = val;

	return ret;
}

static int emulate_store(struct pt_regs *regs, int len, int frreg)
{
	int ret = 0;
#ifdef __LP64__
	unsigned long val = regs->gr[frreg] << (64 - (len << 3));
#else
	unsigned long val = regs->gr[frreg] << (32 - (len << 3));
#endif

	if (regs->isr != regs->sr[7])
	{
		printk(KERN_CRIT "isr verification failed (isr: " RFMT ", sr7: " RFMT "\n",
			regs->isr, regs->sr[7]);
		return 1;
	}

	DPRINTF("store r%d (0x" RFMT ") to " RFMT ":" RFMT " for %d bytes\n", frreg, 
		regs->gr[frreg], regs->isr, regs->ior, len);


	__asm__ __volatile__ (
"       mfsp %%sr1, %%r20\n"		/* save sr1 */
"       mtsp %5, %%sr1\n"
#ifdef __LP64__
"0:	extrd,u %2, 7, 8, %%r19\n"
#else
"0:	extrw,u %2, 7, 8, %%r19\n"
#endif
"1:	stb,ma %%r19, 1(%%sr1, %3)\n"
"	addi -1, %4, %4\n"
"	cmpib,>= 0, %4, 3f\n"
	
#ifdef __LP64__
	"depd,z %2, 55, 56, %2\n"
#else
	"depw,z %2, 23, 24, %2\n"
#endif

"	b 0b\n"
"	nop\n"

"2:	ldi 11, %0\n"
"3:     mtsp %%r20, %%sr1\n"
"	.section __ex_table,\"a\"\n"
#ifdef __LP64__
	".dword 1b, (2b-1b)\n"
#else
	".word 1b, (2b-1b)\n"
#endif
	".previous\n" 
	: "=r" (ret)
	: "0" (ret), "r" (val), "r" (regs->ior), "r" (len), "r" (regs->isr)
	: "r19", "r20" );

	return ret;
}


void handle_unaligned(struct pt_regs *regs)
{
	unsigned long unaligned_count = 0;
	unsigned long last_time = 0;
	int ret = -1;
	struct siginfo si;

	/* if the unaligned access is inside the kernel:
	 *   if the access is caused by a syscall, then we fault the calling
	 *     user process
	 *   otherwise we halt the kernel
	 */
	if (!user_mode(regs))
	{
		const struct exception_table_entry *fix;

		/* see if the offending code have its own
		 * exception handler 
		 */ 

		fix = search_exception_table(regs->iaoq[0]);
		if (fix)
		{
			/* lower bits of fix->skip are flags
			 * upper bits are the handler addr
			 */
			if (fix->skip & 1)
				regs->gr[8] = -EFAULT;
			if (fix->skip & 2)
				regs->gr[9] = 0;

			regs->iaoq[0] += ((fix->skip) & ~3);
			regs->iaoq[1] = regs->iaoq[0] + 4;
			regs->gr[0] &= ~PSW_B;

			return;
		}
	}

	/* log a message with pacing */
	if (user_mode(regs))
	{
		if (unaligned_count > 5 && jiffies - last_time > 5*HZ)
		{
			unaligned_count = 0;
			last_time = jiffies;
		}
		if (++unaligned_count < 5)
		{
			char buf[256];
			sprintf(buf, "%s(%d): unaligned access to 0x" RFMT " at ip=0x" RFMT "\n",
				current->comm, current->pid, regs->ior, regs->iaoq[0]);
			printk(KERN_WARNING "%s", buf);
#ifdef DEBUG_UNALIGNED
			show_regs(regs);
#endif		
		}
	}

	/* TODO: make this cleaner... */
	switch (regs->iir & OPCODE1_MASK)
	{
	case OPCODE_LDH_I:
	case OPCODE_LDH_S:
		ret = emulate_load(regs, 2, regs->iir & 0x1f);
		break;

	case OPCODE_LDW_I:
	case OPCODE_LDWA_I:
	case OPCODE_LDW_S:
	case OPCODE_LDWA_S:
		ret = emulate_load(regs, 4, regs->iir&0x1f);
		break;

	case OPCODE_LDD_I:
	case OPCODE_LDDA_I:
	case OPCODE_LDD_S:
	case OPCODE_LDDA_S:
		ret = emulate_load(regs, 8, regs->iir&0x1f);
		break;

	case OPCODE_STH:
		ret = emulate_store(regs, 2, (regs->iir>>16)&0x1f);
		break;

	case OPCODE_STW:
	case OPCODE_STWA:
		ret = emulate_store(regs, 4, (regs->iir>>16)&0x1f);
		break;

	case OPCODE_STD:
	case OPCODE_STDA:
		ret = emulate_store(regs, 8, (regs->iir>>16)&0x1f);
		break;
	}
	switch (regs->iir & OPCODE2_MASK)
	{
	case OPCODE_LDD_L:
	case OPCODE_FLDD_L:
		ret = emulate_load(regs, 8, (regs->iir>>16)&0x1f);
		break;

	case OPCODE_STD_L:
	case OPCODE_FSTD_L:
		ret = emulate_store(regs, 8, (regs->iir>>16)&0x1f);
		break;
	}
	switch (regs->iir & OPCODE3_MASK)
	{
	case OPCODE_LDW_M:
	case OPCODE_FLDW_L:
		ret = emulate_load(regs, 4, (regs->iir>>16)&0x1f);
		break;

	case OPCODE_FSTW_L:
	case OPCODE_STW_M:
		ret = emulate_store(regs, 4, (regs->iir>>16)&0x1f);
		break;
	}
	switch (regs->iir & OPCODE4_MASK)
	{
	case OPCODE_LDH_L:
		ret = emulate_load(regs, 2, (regs->iir>>16)&0x1f);
		break;
	case OPCODE_LDW_L:
	case OPCODE_LDW_L2:
		ret = emulate_load(regs, 4, (regs->iir>>16)&0x1f);
		break;
	case OPCODE_STH_L:
		ret = emulate_store(regs, 2, (regs->iir>>16)&0x1f);
		break;
	case OPCODE_STW_L:
	case OPCODE_STW_L2:
		ret = emulate_store(regs, 4, (regs->iir>>16)&0x1f);
		break;
	}

	if (ret < 0)
		printk(KERN_CRIT "Not-handled unaligned insn 0x%08lx\n", regs->iir);

	DPRINTF("ret = %d\n", ret);

	if (ret)
	{
		printk(KERN_CRIT "Unaligned handler failed, ret = %d\n", ret);
		die_if_kernel("Unaligned data reference", regs, 28);

		/* couldn't handle it ... */
		si.si_signo = SIGBUS;
		si.si_errno = 0;
		si.si_code = BUS_ADRALN;
		si.si_addr = (void *)regs->ior;
		force_sig_info(SIGBUS, &si, current);
		
		return;
	}

	/* else we handled it, advance the PC.... */
	regs->iaoq[0] = regs->iaoq[1];
	regs->iaoq[1] = regs->iaoq[0] + 4;
}

/*
 * NB: check_unaligned() is only used for PCXS processors right
 * now, so we only check for PA1.1 encodings at this point.
 */

int
check_unaligned(struct pt_regs *regs)
{
	unsigned long align_mask;

	/* Get alignment mask */

	align_mask = 0UL;
	switch (regs->iir & OPCODE1_MASK) {

	case OPCODE_LDH_I:
	case OPCODE_LDH_S:
	case OPCODE_STH:
		align_mask = 1UL;
		break;

	case OPCODE_LDW_I:
	case OPCODE_LDWA_I:
	case OPCODE_LDW_S:
	case OPCODE_LDWA_S:
	case OPCODE_STW:
	case OPCODE_STWA:
		align_mask = 3UL;
		break;

	default:
		switch (regs->iir & OPCODE4_MASK) {
		case OPCODE_LDH_L:
		case OPCODE_STH_L:
			align_mask = 1UL;
			break;
		case OPCODE_LDW_L:
		case OPCODE_LDW_L2:
		case OPCODE_STW_L:
		case OPCODE_STW_L2:
			align_mask = 3UL;
			break;
		}
		break;
	}

	return (int)(regs->ior & align_mask);
}

/*
 * align.c - handle alignment exceptions for the Power PC.
 *
 * Copyright (c) 1996 Paul Mackerras <paulus@cs.anu.edu.au>
 * Copyright (c) 1998-1999 TiVo, Inc.
 *   PowerPC 403GCX modifications.
 * Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *   PowerPC 403GCX/405GP modifications.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/cache.h>

struct aligninfo {
	unsigned int len;
	unsigned int flags;
};

#if defined(CONFIG_4xx) || defined(CONFIG_POWER4) || defined(CONFIG_BOOKE)
#define	OPCD(inst)	(((inst) & 0xFC000000) >> 26)
#define	RS(inst)	(((inst) & 0x03E00000) >> 21)
#define	RA(inst)	(((inst) & 0x001F0000) >> 16)
#define	IS_XFORM(code)	((code) == 31)
#endif

#define INVALID	{ 0, 0 }

#define LD	0x001	/* load */
#define ST	0x002	/* store */
#define	SE	0x004	/* sign-extend value */
#define F	0x008	/* to/from fp regs */
#define U	0x010	/* update index register */
#define M	0x020	/* multiple load/store */
#define S	0x040	/* single-precision fp, or byte-swap value */
#define STR_OP	0x080	/* string, length stored in instruction */
#define STR_XER	0x100	/* string, length stored in XER[25-31] */
#define HARD	0x200	/* too hard: stwcx. */

#define DCBZ	0x5f	/* 8xx/82xx dcbz faults when cache not enabled */

/*
 * The PowerPC stores certain bits of the instruction that caused the
 * alignment exception in the DSISR register.  This array maps those
 * bits to information about the operand length and what the
 * instruction would do.
 */
static struct aligninfo aligninfo[128] = {
	{ 4, LD },		/* 00 0 0000: lwz / lwarx */
	INVALID,		/* 00 0 0001 */
	{ 4, ST },		/* 00 0 0010: stw */
	INVALID,		/* 00 0 0011 */
	{ 2, LD },		/* 00 0 0100: lhz */
	{ 2, LD+SE },		/* 00 0 0101: lha */
	{ 2, ST },		/* 00 0 0110: sth */
	{ 4, LD+M },		/* 00 0 0111: lmw */
	{ 4, LD+F+S },		/* 00 0 1000: lfs */
	{ 8, LD+F },		/* 00 0 1001: lfd */
	{ 4, ST+F+S },		/* 00 0 1010: stfs */
	{ 8, ST+F },		/* 00 0 1011: stfd */
	INVALID,		/* 00 0 1100 */
	INVALID,		/* 00 0 1101: ld/ldu/lwa */
	INVALID,		/* 00 0 1110 */
	INVALID,		/* 00 0 1111: std/stdu */
	{ 4, LD+U },		/* 00 1 0000: lwzu */
	INVALID,		/* 00 1 0001 */
	{ 4, ST+U },		/* 00 1 0010: stwu */
	INVALID,		/* 00 1 0011 */
	{ 2, LD+U },		/* 00 1 0100: lhzu */
	{ 2, LD+SE+U },		/* 00 1 0101: lhau */
	{ 2, ST+U },		/* 00 1 0110: sthu */
	{ 4, ST+M },		/* 00 1 0111: stmw */
	{ 4, LD+F+S+U },	/* 00 1 1000: lfsu */
	{ 8, LD+F+U },		/* 00 1 1001: lfdu */
	{ 4, ST+F+S+U },	/* 00 1 1010: stfsu */
	{ 8, ST+F+U },		/* 00 1 1011: stfdu */
	INVALID,		/* 00 1 1100 */
	INVALID,		/* 00 1 1101 */
	INVALID,		/* 00 1 1110 */
	INVALID,		/* 00 1 1111 */
	INVALID,		/* 01 0 0000: ldx */
	INVALID,		/* 01 0 0001 */
	INVALID,		/* 01 0 0010: stdx */
	INVALID,		/* 01 0 0011 */
	INVALID,		/* 01 0 0100 */
	INVALID,		/* 01 0 0101: lwax */
	INVALID,		/* 01 0 0110 */
	INVALID,		/* 01 0 0111 */
	{ 4, LD+STR_XER },	/* 01 0 1000: lswx */
	{ 4, LD+STR_OP },	/* 01 0 1001: lswi */
	{ 4, ST+STR_XER },	/* 01 0 1010: stswx */
	{ 4, ST+STR_OP },	/* 01 0 1011: stswi */
	INVALID,		/* 01 0 1100 */
	INVALID,		/* 01 0 1101 */
	INVALID,		/* 01 0 1110 */
	INVALID,		/* 01 0 1111 */
	INVALID,		/* 01 1 0000: ldux */
	INVALID,		/* 01 1 0001 */
	INVALID,		/* 01 1 0010: stdux */
	INVALID,		/* 01 1 0011 */
	INVALID,		/* 01 1 0100 */
	INVALID,		/* 01 1 0101: lwaux */
	INVALID,		/* 01 1 0110 */
	INVALID,		/* 01 1 0111 */
	INVALID,		/* 01 1 1000 */
	INVALID,		/* 01 1 1001 */
	INVALID,		/* 01 1 1010 */
	INVALID,		/* 01 1 1011 */
	INVALID,		/* 01 1 1100 */
	INVALID,		/* 01 1 1101 */
	INVALID,		/* 01 1 1110 */
	INVALID,		/* 01 1 1111 */
	INVALID,		/* 10 0 0000 */
	INVALID,		/* 10 0 0001 */
	{ 0, ST+HARD },		/* 10 0 0010: stwcx. */
	INVALID,		/* 10 0 0011 */
	INVALID,		/* 10 0 0100 */
	INVALID,		/* 10 0 0101 */
	INVALID,		/* 10 0 0110 */
	INVALID,		/* 10 0 0111 */
	{ 4, LD+S },		/* 10 0 1000: lwbrx */
	INVALID,		/* 10 0 1001 */
	{ 4, ST+S },		/* 10 0 1010: stwbrx */
	INVALID,		/* 10 0 1011 */
	{ 2, LD+S },		/* 10 0 1100: lhbrx */
	INVALID,		/* 10 0 1101 */
	{ 2, ST+S },		/* 10 0 1110: sthbrx */
	INVALID,		/* 10 0 1111 */
	INVALID,		/* 10 1 0000 */
	INVALID,		/* 10 1 0001 */
	INVALID,		/* 10 1 0010 */
	INVALID,		/* 10 1 0011 */
	INVALID,		/* 10 1 0100 */
	INVALID,		/* 10 1 0101 */
	INVALID,		/* 10 1 0110 */
	INVALID,		/* 10 1 0111 */
	INVALID,		/* 10 1 1000 */
	INVALID,		/* 10 1 1001 */
	INVALID,		/* 10 1 1010 */
	INVALID,		/* 10 1 1011 */
	INVALID,		/* 10 1 1100 */
	INVALID,		/* 10 1 1101 */
	INVALID,		/* 10 1 1110 */
	{ 0, ST+HARD },		/* 10 1 1111: dcbz */
	{ 4, LD },		/* 11 0 0000: lwzx */
	INVALID,		/* 11 0 0001 */
	{ 4, ST },		/* 11 0 0010: stwx */
	INVALID,		/* 11 0 0011 */
	{ 2, LD },		/* 11 0 0100: lhzx */
	{ 2, LD+SE },		/* 11 0 0101: lhax */
	{ 2, ST },		/* 11 0 0110: sthx */
	INVALID,		/* 11 0 0111 */
	{ 4, LD+F+S },		/* 11 0 1000: lfsx */
	{ 8, LD+F },		/* 11 0 1001: lfdx */
	{ 4, ST+F+S },		/* 11 0 1010: stfsx */
	{ 8, ST+F },		/* 11 0 1011: stfdx */
	INVALID,		/* 11 0 1100 */
	INVALID,		/* 11 0 1101: lmd */
	INVALID,		/* 11 0 1110 */
	INVALID,		/* 11 0 1111: stmd */
	{ 4, LD+U },		/* 11 1 0000: lwzux */
	INVALID,		/* 11 1 0001 */
	{ 4, ST+U },		/* 11 1 0010: stwux */
	INVALID,		/* 11 1 0011 */
	{ 2, LD+U },		/* 11 1 0100: lhzux */
	{ 2, LD+SE+U },		/* 11 1 0101: lhaux */
	{ 2, ST+U },		/* 11 1 0110: sthux */
	INVALID,		/* 11 1 0111 */
	{ 4, LD+F+S+U },	/* 11 1 1000: lfsux */
	{ 8, LD+F+U },		/* 11 1 1001: lfdux */
	{ 4, ST+F+S+U },	/* 11 1 1010: stfsux */
	{ 8, ST+F+U },		/* 11 1 1011: stfdux */
	INVALID,		/* 11 1 1100 */
	INVALID,		/* 11 1 1101 */
	INVALID,		/* 11 1 1110 */
	INVALID,		/* 11 1 1111 */
};

#define SWAP(a, b)	(t = (a), (a) = (b), (b) = t)

int
fix_alignment(struct pt_regs *regs)
{
	int instr, nb, mb, mr, flags;
#if defined(CONFIG_4xx) || defined(CONFIG_POWER4) || defined(CONFIG_BOOKE)
	int opcode, f1, f2, f3;
#endif
	int i, j, t;
	int reg, areg;
	unsigned char __user *addr;
	union {
		long l;
		float f;
		double d;
		unsigned char v[8];
	} data[32];

	CHECK_FULL_REGS(regs);

#if defined(CONFIG_4xx) || defined(CONFIG_POWER4) || defined(CONFIG_BOOKE)
	/* The 4xx-family & Book-E processors have no DSISR register,
	 * so we emulate it.
	 * The POWER4 has a DSISR register but doesn't set it on
	 * an alignment fault.  -- paulus
	 */

	instr = *((unsigned int *)regs->nip);
	opcode = OPCD(instr);
	reg = RS(instr);
	areg = RA(instr);

	if (!IS_XFORM(opcode)) {
		f1 = 0;
		f2 = (instr & 0x04000000) >> 26;
		f3 = (instr & 0x78000000) >> 27;
	} else {
		f1 = (instr & 0x00000006) >> 1;
		f2 = (instr & 0x00000040) >> 6;
		f3 = (instr & 0x00000780) >> 7;
	}

	instr = ((f1 << 5) | (f2 << 4) | f3);
#else
	reg = (regs->dsisr >> 5) & 0x1f;	/* source/dest register */
	areg = regs->dsisr & 0x1f;		/* register to update */
	instr = (regs->dsisr >> 10) & 0x7f;
#endif

	nb = aligninfo[instr].len;
	if (nb == 0) {
		long *p;
		int i;

		if (instr != DCBZ)
			return 0;	/* too hard or invalid instruction */
		/*
		 * The dcbz (data cache block zero) instruction
		 * gives an alignment fault if used on non-cacheable
		 * memory.  We handle the fault mainly for the
		 * case when we are running with the cache disabled
		 * for debugging.
		 */
		p = (long *) (regs->dar & -L1_CACHE_BYTES);
		for (i = 0; i < L1_CACHE_BYTES / sizeof(long); ++i)
			p[i] = 0;
		return 1;
	}

	flags = aligninfo[instr].flags;

	/* For the 4xx-family & Book-E processors, the 'dar' field of the
	 * pt_regs structure is overloaded and is really from the DEAR.
	 */

	addr = (unsigned char __user *)regs->dar;

	/* Get mb (total number of bytes to load)
	   and mr (number of registers needed) if we're dealing with strings */
	if (flags & M) {
		mr = 32 - reg;
		mb = nb * mr; /* nb = bytes per register */
	} else if (flags & STR_OP) {
		instr = *((unsigned int *)regs->nip);
		mb = (instr >> 11) & 0x1f;
		if (mb == 0)
			mb = 32; /* mb = 32 if bits are 0 */
		mr = mb / 4;
		if ((mb % 4) > 0)
			mr++;
	} else if (flags & STR_XER) {
		mb = regs->xer & 0x7f;
		/* This shouldn't be 128 if XER[25-31] is 0,
		   in that case it's actually a no-op */
		mr = mb / 4;
		if ((mb % 4) > 0)
			mr++;
	} else {
		mb = nb;
		mr = 1;
	}

	/* Verify the address of the operand */
	if (user_mode(regs)) {
		if (verify_area((flags & ST? VERIFY_WRITE: VERIFY_READ), addr, mb))
			return -EFAULT;	/* bad address */
	}

	if (flags & F) {
		preempt_disable();
		if (regs->msr & MSR_FP)
			giveup_fpu(current);
		preempt_enable();
	}

	/* If we read the operand, copy it in */
	if (flags & LD) {
		for (j = 0; j < mr; ++j) {
			if (nb == 2) {
				data[j].v[0] = data[j].v[1] = 0;
				if (__get_user(data[j].v[2], addr)
				    || __get_user(data[j].v[3], addr+1))
					return -EFAULT;
			} else {
				for (i = 0; i < nb; ++i) {
					if ((j*4)+i < mb) {
						if (__get_user(data[j].v[i], addr+(j*4)+i))
							return -EFAULT;
					} else {
						data[j].v[i] = 0;
					}
				}
			}
		}
	}

	/* We already did the main work for the multiple register cases
	   (M, STR_OP and STR_XER), so they get filtered out. As the only
	   possible combinations with multiples are with LD and ST, there
	   is only a loop there. data[0] is used when there is only one
	   register involved. */
	switch (flags & ~(U|M|STR_OP|STR_XER)) {
	case LD+SE:
		if (data[0].v[2] >= 0x80)
			data[0].v[0] = data[0].v[1] = -1;
		/* fall through */
	case LD:
		for (i = 0; i < mr; ++i)
			regs->gpr[reg+i] = data[i].l;
		break;
	case LD+S:
		if (nb == 2) {
			SWAP(data[0].v[2], data[0].v[3]);
		} else {
			SWAP(data[0].v[0], data[0].v[3]);
			SWAP(data[0].v[1], data[0].v[2]);
		}
		regs->gpr[reg] = data[0].l;
		break;
	case ST:
		for (i = 0; i < mr; ++i)
			data[i].l = regs->gpr[reg+i];
		break;
	case ST+S:
		data[0].l = regs->gpr[reg];
		if (nb == 2) {
			SWAP(data[0].v[2], data[0].v[3]);
		} else {
			SWAP(data[0].v[0], data[0].v[3]);
			SWAP(data[0].v[1], data[0].v[2]);
		}
		break;
	case LD+F:
		current->thread.fpr[reg] = data[0].d;
		break;
	case ST+F:
		data[0].d = current->thread.fpr[reg];
		break;
	/* these require some floating point conversions... */
	/* we'd like to use the assignment, but we have to compile
	 * the kernel with -msoft-float so it doesn't use the
	 * fp regs for copying 8-byte objects. */
	case LD+F+S:
		preempt_disable();
		enable_kernel_fp();
		cvt_fd(&data[0].f, &current->thread.fpr[reg], &current->thread.fpscr);
		/* current->thread.fpr[reg] = data[0].f; */
		preempt_enable();
		break;
	case ST+F+S:
		preempt_disable();
		enable_kernel_fp();
		cvt_df(&current->thread.fpr[reg], &data[0].f, &current->thread.fpscr);
		/* data[0].f = current->thread.fpr[reg]; */
		preempt_enable();
		break;
	default:
		printk("align: can't handle flags=%x\n", flags);
		return 0;
	}

	if (flags & ST) {
		for (j = 0; j < mr; ++j) {
			if (nb == 2) {
				if (__put_user(data[j].v[2], addr)
				    || __put_user(data[j].v[3], addr+1))
					return -EFAULT;
			} else {
				for (i = 0; (i < nb) && (((j*4)+i) < mb); ++i)
					if (__put_user(data[j].v[i], addr+(j*4)+i))
						return -EFAULT;
			}
		}
	}

	if (flags & U) {
		regs->gpr[areg] = regs->dar;
	}

	return 1;
}

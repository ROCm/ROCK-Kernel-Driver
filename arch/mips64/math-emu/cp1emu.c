/*
 * MIPS floating point support
 *
 * This program is free software; you can distribute it and/or modify it
 * under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * cp1emu.c: a MIPS coprocessor 1 (fpu) instruction emulator
 * 
 * A complete emulator for MIPS coprocessor 1 instructions.  This is
 * required for #float(switch) or #float(trap), where it catches all
 * COP1 instructions via the "CoProcessor Unusable" exception.  
 *
 * More surprisingly it is also required for #float(ieee), to help out
 * the hardware fpu at the boundaries of the IEEE-754 representation
 * (denormalised values, infinities, underflow, etc).  It is made
 * quite nasty because emulation of some non-COP1 instructions is
 * required, e.g. in branch delay slots.
 * 
 * Notes: 
 *  1) the IEEE754 library (-le) performs the actual arithmetic;
 *  2) if you know that you won't have an fpu, then you'll get much 
 *     better performance by compiling with -msoft-float!  */
 *
 *  Nov 7, 2000
 *  Massive changes to integrate with Linux kernel.
 *
 *  Replace use of kernel data area with use of user stack 
 *  for execution of instructions in branch delay slots.
 *
 *  Replace use of static kernel variables with thread_struct elements.
 *
 * Copyright (C) 1994-2000 Algorithmics Ltd.  All rights reserved.
 * http://www.algor.co.uk
 *
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000  MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/config.h>
#include <linux/mm.h>
#include <linux/signal.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/asm.h>
#include <asm/branch.h>
#include <asm/bootinfo.h>
#include <asm/byteorder.h>
#include <asm/cpu.h>
#include <asm/inst.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/pgtable.h>

#include <asm/fpu_emulator.h>

#include "ieee754.h"

/* Strap kernel emulator for full MIPS IV emulation */

#ifdef __mips
#undef __mips
#endif
#define __mips 4

typedef void *vaddr_t;

/* Function which emulates the instruction in a branch delay slot. */

static int mips_dsemul(struct pt_regs *, mips_instruction, vaddr_t);

/* Function which emulates a floating point instruction. */

static int fpu_emu(struct pt_regs *, struct mips_fpu_soft_struct *,
	 mips_instruction);

#if __mips >= 4 && __mips != 32
static int fpux_emu(struct pt_regs *,
		    struct mips_fpu_soft_struct *, mips_instruction);
#endif

/* Further private data for which no space exists in mips_fpu_soft_struct */

struct mips_fpu_emulator_private fpuemuprivate;

/* Control registers */

#define FPCREG_RID	0	/* $0  = revision id */
#define FPCREG_CSR	31	/* $31 = csr */

/* Convert Mips rounding mode (0..3) to IEEE library modes. */
static const unsigned char ieee_rm[4] = {
	IEEE754_RN, IEEE754_RZ, IEEE754_RU, IEEE754_RD
};

#if __mips >= 4
/* convert condition code register number to csr bit */
static const unsigned int fpucondbit[8] = {
	FPU_CSR_COND0,
	FPU_CSR_COND1,
	FPU_CSR_COND2,
	FPU_CSR_COND3,
	FPU_CSR_COND4,
	FPU_CSR_COND5,
	FPU_CSR_COND6,
	FPU_CSR_COND7
};
#endif



/* 
 * Redundant with logic already in kernel/branch.c,
 * embedded in compute_return_epc.  At some point,
 * a single subroutine should be used across both
 * modules.
 */
static int isBranchInstr(mips_instruction * i)
{
	switch (MIPSInst_OPCODE(*i)) {
	case spec_op:
		switch (MIPSInst_FUNC(*i)) {
		case jalr_op:
		case jr_op:
			return 1;
		}
		break;

	case bcond_op:
		switch (MIPSInst_RT(*i)) {
		case bltz_op:
		case bgez_op:
		case bltzl_op:
		case bgezl_op:
		case bltzal_op:
		case bgezal_op:
		case bltzall_op:
		case bgezall_op:
			return 1;
		}
		break;

	case j_op:
	case jal_op:
	case jalx_op:
	case beq_op:
	case bne_op:
	case blez_op:
	case bgtz_op:
	case beql_op:
	case bnel_op:
	case blezl_op:
	case bgtzl_op:
		return 1;

	case cop0_op:
	case cop1_op:
	case cop2_op:
	case cop1x_op:
		if (MIPSInst_RS(*i) == bc_op)
			return 1;
		break;
	}

	return 0;
}

#define REG_TO_VA (vaddr_t)
#define VA_TO_REG (unsigned long)

static unsigned long
mips_get_word(struct pt_regs *xcp, void *va, int *perr)
{
	unsigned long temp;

	if (!user_mode(xcp)) {
		*perr = 0;
		return (*(unsigned long *) va);
	} else {
		/* Use kernel get_user() macro */
		*perr = (int) get_user(temp, (unsigned long *) va);
		return temp;
	}
}

static unsigned long long
mips_get_dword(struct pt_regs *xcp, void *va, int *perr)
{
	unsigned long long temp;

	if (!user_mode(xcp)) {
		*perr = 0;
		return (*(unsigned long long *) va);
	} else {
		/* Use kernel get_user() macro */
		*perr = (int) get_user(temp, (unsigned long long *) va);
		return temp;
	}
}

static int mips_put_word(struct pt_regs *xcp, void *va, unsigned long val)
{
	if (!user_mode(xcp)) {
		*(unsigned long *) va = val;
		return 0;
	} else {
		/* Use kernel get_user() macro */
		return (int) put_user(val, (unsigned long *) va);
	}
}

static int mips_put_dword(struct pt_regs *xcp, void *va, long long val)
{
	if (!user_mode(xcp)) {
		*(unsigned long long *) va = val;
		return 0;
	} else {
		/* Use kernel get_user() macro */
		return (int) put_user(val, (unsigned long long *) va);
	}
}


/*
 * In the Linux kernel, we support selection of FPR format on the
 * basis of the Status.FR bit.  This does imply that, if a full 32
 * FPRs are desired, there needs to be a flip-flop that can be written
 * to one at that bit position.  In any case, normal MIPS ABI uses
 * only the even FPRs (Status.FR = 0).
 */

#define CP0_STATUS_FR_SUPPORT

/*
 * Emulate the single floating point instruction pointed at by EPC.
 * Two instructions if the instruction is in a branch delay slot.
 */

static int
cop1Emulate(int xcptno, struct pt_regs *xcp,
	    struct mips_fpu_soft_struct *ctx)
{
	mips_instruction ir;
	vaddr_t emulpc;
	vaddr_t contpc;
	unsigned int cond;
	int err = 0;


	ir = mips_get_word(xcp, REG_TO_VA xcp->cp0_epc, &err);
	if (err) {
		fpuemuprivate.stats.errors++;
		return SIGBUS;
	}

	/* XXX NEC Vr54xx bug workaround */
	if ((xcp->cp0_cause & CAUSEF_BD) && !isBranchInstr(&ir))
		xcp->cp0_cause &= ~CAUSEF_BD;

	if (xcp->cp0_cause & CAUSEF_BD) {
		/*
		 * The instruction to be emulated is in a branch delay slot
		 * which means that we have to  emulate the branch instruction
		 * BEFORE we do the cop1 instruction. 
		 *
		 * This branch could be a COP1 branch, but in that case we
		 * would have had a trap for that instruction, and would not
		 * come through this route.
		 *
		 * Linux MIPS branch emulator operates on context, updating the
		 * cp0_epc.
		 */
		emulpc = REG_TO_VA(xcp->cp0_epc + 4);	/* Snapshot emulation target */

		if (__compute_return_epc(xcp)) {
#ifdef CP1DBG
			printk("failed to emulate branch at %p\n",
				    REG_TO_VA(xcp->cp0_epc));
#endif
			return SIGILL;;
		}
		ir = mips_get_word(xcp, emulpc, &err);
		if (err) {
			fpuemuprivate.stats.errors++;
			return SIGBUS;
		}
		contpc = REG_TO_VA xcp->cp0_epc;
	} else {
		emulpc = REG_TO_VA xcp->cp0_epc;
		contpc = REG_TO_VA xcp->cp0_epc + 4;
	}

      emul:
	fpuemuprivate.stats.emulated++;
	switch (MIPSInst_OPCODE(ir)) {
#ifdef CP0_STATUS_FR_SUPPORT
		/* R4000+ 64-bit fpu registers */
#ifndef SINGLE_ONLY_FPU
	case ldc1_op:
		{
			void *va = REG_TO_VA(xcp->regs[MIPSInst_RS(ir)])
			    + MIPSInst_SIMM(ir);
			int ft = MIPSInst_RT(ir);
			if (!(xcp->cp0_status & ST0_FR))
				ft &= ~1;
			ctx->regs[ft] = mips_get_dword(xcp, va, &err);
			fpuemuprivate.stats.loads++;
			if (err) {
				fpuemuprivate.stats.errors++;
				return SIGBUS;
			}
		}
		break;

	case sdc1_op:
		{
			void *va = REG_TO_VA(xcp->regs[MIPSInst_RS(ir)])
			    + MIPSInst_SIMM(ir);
			int ft = MIPSInst_RT(ir);
			if (!(xcp->cp0_status & ST0_FR))
				ft &= ~1;
			fpuemuprivate.stats.stores++;
			if (mips_put_dword(xcp, va, ctx->regs[ft])) {
				fpuemuprivate.stats.errors++;
				return SIGBUS;
			}
		}
		break;
#endif

	case lwc1_op:
		{
			void *va = REG_TO_VA(xcp->regs[MIPSInst_RS(ir)])
			    + MIPSInst_SIMM(ir);
			fpureg_t val;
			int ft = MIPSInst_RT(ir);
			fpuemuprivate.stats.loads++;
			val = mips_get_word(xcp, va, &err);
			if (err) {
				fpuemuprivate.stats.errors++;
				return SIGBUS;
			}
			if (xcp->cp0_status & ST0_FR) {
				/* load whole register */
				ctx->regs[ft] = val;
			} else if (ft & 1) {
				/* load to m.s. 32 bits */
#ifdef SINGLE_ONLY_FPU
				/* illegal register in single-float mode */
				return SIGILL;
#else
				ctx->regs[(ft & ~1)] &= 0xffffffff;
				ctx->regs[(ft & ~1)] |= val << 32;
#endif
			} else {
				/* load to l.s. 32 bits */
				ctx->regs[ft] &= ~0xffffffffLL;
				ctx->regs[ft] |= val;
			}
		}
		break;

	case swc1_op:
		{
			void *va = REG_TO_VA(xcp->regs[MIPSInst_RS(ir)])
			    + MIPSInst_SIMM(ir);
			unsigned int val;
			int ft = MIPSInst_RT(ir);
			fpuemuprivate.stats.stores++;
			if (xcp->cp0_status & ST0_FR) {
				/* store whole register */
				val = ctx->regs[ft];
			} else if (ft & 1) {
#ifdef SINGLE_ONLY_FPU
				/* illegal register in single-float mode */
				return SIGILL;
#else
				/* store from m.s. 32 bits */
				val = ctx->regs[(ft & ~1)] >> 32;
#endif
			} else {
				/* store from l.s. 32 bits */
				val = ctx->regs[ft];
			}
			if (mips_put_word(xcp, va, val)) {
				fpuemuprivate.stats.errors++;
				return SIGBUS;
			}
		}
		break;
#else				/* old 32-bit fpu registers */
	case lwc1_op:
		{
			void *va = REG_TO_VA(xcp->regs[MIPSInst_RS(ir)])
			    + MIPSInst_SIMM(ir);
			ctx->regs[MIPSInst_RT(ir)] =
			    mips_get_word(xcp, va, &err);
			fpuemuprivate.stats.loads++;
			if (err) {
				fpuemuprivate.stats.errors++;
				return SIGBUS;
			}
		}
		break;

	case swc1_op:
		{
			void *va = REG_TO_VA(xcp->regs[MIPSInst_RS(ir)])
			    + MIPSInst_SIMM(ir);
			fpuemuprivate.stats.stores++;
			if (mips_put_word
			    (xcp, va, ctx->regs[MIPSInst_RT(ir)])) {
				fpuemuprivate.stats.errors++;
				return SIGBUS;
			}
		}
		break;
	case ldc1_op:
		{
			void *va = REG_TO_VA(xcp->regs[MIPSInst_RS(ir)])
			    + MIPSInst_SIMM(ir);
			unsigned int rt = MIPSInst_RT(ir) & ~1;
			int errs = 0;
			fpuemuprivate.stats.loads++;
#if (defined(BYTE_ORDER) && BYTE_ORDER == BIG_ENDIAN) || defined(__MIPSEB__)
			ctx->regs[rt + 1] =
			    mips_get_word(xcp, va + 0, &err);
			errs += err;
			ctx->regs[rt + 0] =
			    mips_get_word(xcp, va + 4, &err);
			errs += err;
#else
			ctx->regs[rt + 0] =
			    mips_get_word(xcp, va + 0, &err);
			errs += err;
			ctx->regs[rt + 1] =
			    mips_get_word(xcp, va + 4, &err);
			errs += err;
#endif
			if (err)
				return SIGBUS;
		}
		break;

	case sdc1_op:
		{
			void *va = REG_TO_VA(xcp->regs[MIPSInst_RS(ir)])
			    + MIPSInst_SIMM(ir);
			unsigned int rt = MIPSInst_RT(ir) & ~1;
			fpuemuprivate.stats.stores++;
#if (defined(BYTE_ORDER) && BYTE_ORDER == BIG_ENDIAN) || defined(__MIPSEB__)
			if (mips_put_word(xcp, va + 0, ctx->regs[rt + 1]))
				return SIGBUS;
			if (mips_put_word(xcp, va + 4, ctx->regs[rt + 0]))
				return SIGBUS;
#else
			if (mips_put_word(xcp, va + 0, ctx->regs[rt + 0]))
				return SIGBUS;
			if (mips_put_word(xcp, va + 4, ctx->regs[rt + 1]))
				return SIGBUS;
#endif
		}
		break;
#endif

	case cop1_op:
		switch (MIPSInst_RS(ir)) {

#ifdef CP0_STATUS_FR_SUPPORT
#if __mips64 && !defined(SINGLE_ONLY_FPU)
		case dmfc_op:
			/* copregister fs -> gpr[rt] */
			if (MIPSInst_RT(ir) != 0) {
				int fs = MIPSInst_RD(ir);
				if (!(xcp->cp0_status & ST0_FR))
					fs &= ~1;
				xcp->regs[MIPSInst_RT(ir)] = ctx->regs[fs];
			}
			break;

		case dmtc_op:
			/* copregister fs <- rt */
			{
				fpureg_t value;
				int fs = MIPSInst_RD(ir);
				if (!(xcp->cp0_status & ST0_FR))
					fs &= ~1;
				value =
				    (MIPSInst_RT(ir) ==
				     0) ? 0 : xcp->regs[MIPSInst_RT(ir)];
				ctx->regs[fs] = value;
			}
			break;
#endif

		case mfc_op:
			/* copregister rd -> gpr[rt] */
			if (MIPSInst_RT(ir) != 0) {
				/* default value from l.s. 32 bits */
				int value = ctx->regs[MIPSInst_RD(ir)];
				if (MIPSInst_RD(ir) & 1) {
#ifdef SINGLE_ONLY_FPU
					/* illegal register in single-float mode */
					return SIGILL;
#else
					if (!(xcp->cp0_status & ST0_FR)) {
						/* move from m.s. 32 bits */
						value =
						    ctx->
						    regs[MIPSInst_RD(ir) &
							 ~1] >> 32;
					}
#endif
				}
				xcp->regs[MIPSInst_RT(ir)] = value;
			}
			break;

		case mtc_op:
			/* copregister rd <- rt */
			{
				fpureg_t value;
				if (MIPSInst_RT(ir) == 0)
					value = 0;
				else
					value =
					    (unsigned int) xcp->
					    regs[MIPSInst_RT(ir)];
				if (MIPSInst_RD(ir) & 1) {
#ifdef SINGLE_ONLY_FPU
					/* illegal register in single-float mode */
					return SIGILL;
#else
					if (!(xcp->cp0_status & ST0_FR)) {
						/* move to m.s. 32 bits */
						ctx->
						    regs[
							 (MIPSInst_RD(ir) &
							  ~1)] &=
						    0xffffffff;
						ctx->
						    regs[
							 (MIPSInst_RD(ir) &
							  ~1)] |=
						    value << 32;
						break;
					}
#endif
				}
				/* move to l.s. 32 bits */
				ctx->regs[MIPSInst_RD(ir)] &=
				    ~0xffffffffLL;
				ctx->regs[MIPSInst_RD(ir)] |= value;
			}
			break;
#else

		case mfc_op:
			/* copregister rd -> gpr[rt] */
			if (MIPSInst_RT(ir) != 0) {
				unsigned value =
				    ctx->regs[MIPSInst_RD(ir)];
				xcp->regs[MIPSInst_RT(ir)] = value;
			}
			break;

		case mtc_op:
			/* copregister rd <- rt */
			{
				unsigned value;
				value =
				    (MIPSInst_RT(ir) ==
				     0) ? 0 : xcp->regs[MIPSInst_RT(ir)];
				ctx->regs[MIPSInst_RD(ir)] = value;
			}
			break;
#endif

		case cfc_op:
			/* cop control register rd -> gpr[rt] */
			{
				unsigned value;

				if (MIPSInst_RD(ir) == FPCREG_CSR) {
					value = ctx->sr;
#ifdef CSRTRACE
					printk
					    ("%p gpr[%d]<-csr=%08x\n",
					     REG_TO_VA(xcp->cp0_epc),
					     MIPSInst_RT(ir), value);
#endif
				} else if (MIPSInst_RD(ir) == FPCREG_RID)
					value = 0;
				else
					value = 0;
				if (MIPSInst_RT(ir))
					xcp->regs[MIPSInst_RT(ir)] = value;
			}
			break;

		case ctc_op:
			/* copregister rd <- rt */
			{
				unsigned value;

				if (MIPSInst_RT(ir) == 0)
					value = 0;
				else
					value = xcp->regs[MIPSInst_RT(ir)];

				/* we only have one writable control reg
				 */
				if (MIPSInst_RD(ir) == FPCREG_CSR) {
#ifdef CSRTRACE
					printk
					    ("%p gpr[%d]->csr=%08x\n",
					     REG_TO_VA(xcp->cp0_epc),
					     MIPSInst_RT(ir), value);
#endif
					ctx->sr = value;
					/* copy new rounding mode to ieee library state! */
					ieee754_csr.rm =
					    ieee_rm[value & 0x3];
				}
			}
			break;

		case bc_op:
			if (xcp->cp0_cause & CAUSEF_BD) {
				return SIGILL;
			}
			{
				int likely = 0;

#if __mips >= 4
				cond =
				    ctx->
				    sr & fpucondbit[MIPSInst_RT(ir) >> 2];
#else
				cond = ctx->sr & FPU_CSR_COND;
#endif
				switch (MIPSInst_RT(ir) & 3) {
				case bcfl_op:
					likely = 1;
				case bcf_op:
					cond = !cond;
					break;
				case bctl_op:
					likely = 1;
				case bct_op:
					break;
				default:
					/* thats an illegal instruction */
					return SIGILL;
				}

				xcp->cp0_cause |= CAUSEF_BD;
				if (cond) {
					/* branch taken: emulate dslot instruction */
					xcp->cp0_epc += 4;
					contpc =
					    REG_TO_VA xcp->cp0_epc +
					    (MIPSInst_SIMM(ir) << 2);

					ir =
					    mips_get_word(xcp,
							  REG_TO_VA(xcp->
								    cp0_epc),
							  &err);
					if (err) {
						fpuemuprivate.stats.
						    errors++;
						return SIGBUS;
					}

					switch (MIPSInst_OPCODE(ir)) {
					case lwc1_op:
					case swc1_op:
#if (__mips >= 2 || __mips64) && !defined(SINGLE_ONLY_FPU)
					case ldc1_op:
					case sdc1_op:
#endif
					case cop1_op:
#if __mips >= 4 && __mips != 32
					case cop1x_op:
#endif
						/* its one of ours */
						goto emul;
#if __mips >= 4
					case spec_op:
						if (MIPSInst_FUNC(ir) ==
						    movc_op) goto emul;
						break;
#endif
					}

					/* single step the non-cp1 instruction in the dslot */
					return mips_dsemul(xcp, ir, contpc);
				} else {
					/* branch not taken */
					if (likely)
						/* branch likely nullifies dslot if not taken */
						xcp->cp0_epc += 4;
					/* else continue & execute dslot as normal insn */
				}
			}
			break;

		default:
			if (!(MIPSInst_RS(ir) & 0x10)) {
				return SIGILL;
			}
			/* a real fpu computation instruction */
			{
				int sig;
				if ((sig = fpu_emu(xcp, ctx, ir)))
					return sig;
			}
		}
		break;

#if __mips >= 4 && __mips != 32
	case cop1x_op:
		{
			int sig;
			if ((sig = fpux_emu(xcp, ctx, ir)))
				return sig;
		}
		break;
#endif

#if __mips >= 4
	case spec_op:
		if (MIPSInst_FUNC(ir) != movc_op)
			return SIGILL;
		cond = fpucondbit[MIPSInst_RT(ir) >> 2];
		if (((ctx->sr & cond) != 0) !=
		    ((MIPSInst_RT(ir) & 1) != 0)) return 0;
		xcp->regs[MIPSInst_RD(ir)] = xcp->regs[MIPSInst_RS(ir)];
		break;
#endif

	default:
		return SIGILL;
	}

	/* we did it !! */
	xcp->cp0_epc = VA_TO_REG(contpc);
	xcp->cp0_cause &= ~CAUSEF_BD;
	return 0;
}

/*
 * Emulate the arbritrary instruction ir at xcp->cp0_epc.  Required when
 * we have to emulate the instruction in a COP1 branch delay slot.  Do
 * not change cp0_epc due to the instruction
 *
 * According to the spec:
 * 1) it shouldnt be a branch :-)
 * 2) it can be a COP instruction :-(
 * 3) if we are tring to run a protected memory space we must take
 *    special care on memory access instructions :-(
 */

/*
 * "Trampoline" return routine to catch exception following
 *  execution of delay-slot instruction execution.
 */

int do_dsemulret(struct pt_regs *xcp)
{
#ifdef DSEMUL_TRACE
	printk("desemulret\n");
#endif
	/* Set EPC to return to post-branch instruction */
	xcp->cp0_epc = current->thread.dsemul_epc;
	/*
	 * Clear the state that got us here.
	 */
	current->thread.dsemul_aerpc = (unsigned long) 0;

	return 0;
}


#define AdELOAD 0x8c000001	/* lw $0,1($0) */

static int
mips_dsemul(struct pt_regs *xcp, mips_instruction ir, vaddr_t cpc)
{
	mips_instruction *dsemul_insns;
	mips_instruction forcetrap;
	extern asmlinkage void handle_dsemulret(void);

	if (ir == 0) {		/* a nop is easy */
		xcp->cp0_epc = VA_TO_REG(cpc);
		return 0;
	}
#ifdef DSEMUL_TRACE
	printk("desemul %p %p\n", REG_TO_VA(xcp->cp0_epc), cpc);
#endif

	/* 
	 * The strategy is to push the instruction onto the user stack 
	 * and put a trap after it which we can catch and jump to 
	 * the required address any alternative apart from full 
	 * instruction emulation!!.
	 */
	dsemul_insns = (mips_instruction *) (xcp->regs[29] & ~3);
	dsemul_insns -= 3;	/* Two instructions, plus one for luck ;-) */
	/* Verify that the stack pointer is not competely insane */
	if (verify_area
	    (VERIFY_WRITE, dsemul_insns, sizeof(mips_instruction) * 2))
		return SIGBUS;

	if (mips_put_word(xcp, &dsemul_insns[0], ir)) {
		fpuemuprivate.stats.errors++;
		return (SIGBUS);
	}

	/* 
	 * Algorithmics used a system call instruction, and
	 * borrowed that vector.  MIPS/Linux version is a bit
	 * more heavyweight in the interests of portability and
	 * multiprocessor support.  We flag the thread for special
	 * handling in the unaligned access handler and force an
	 * address error excpetion.
	 */

	/* If one is *really* paranoid, one tests for a bad stack pointer */
	if ((xcp->regs[29] & 0x3) == 0x3)
		forcetrap = AdELOAD - 1;
	else
		forcetrap = AdELOAD;

	if (mips_put_word(xcp, &dsemul_insns[1], forcetrap)) {
		fpuemuprivate.stats.errors++;
		return (SIGBUS);
	}

	/* Set thread state to catch and handle the exception */
	current->thread.dsemul_epc = (unsigned long) cpc;
	current->thread.dsemul_aerpc = (unsigned long) &dsemul_insns[1];
	xcp->cp0_epc = VA_TO_REG & dsemul_insns[0];
	flush_cache_sigtramp((unsigned long) dsemul_insns);

	return SIGILL;		/* force out of emulation loop */
}

/* 
 * Conversion table from MIPS compare ops 48-63
 * cond = ieee754dp_cmp(x,y,IEEE754_UN);
 */
static const unsigned char cmptab[8] = {
	0,					/* cmp_0 (sig) cmp_sf */
	IEEE754_CUN,				/* cmp_un (sig) cmp_ngle */
	IEEE754_CEQ,				/* cmp_eq (sig) cmp_seq */
	IEEE754_CEQ | IEEE754_CUN,		/* cmp_ueq (sig) cmp_ngl  */
	IEEE754_CLT,				/* cmp_olt (sig) cmp_lt */
	IEEE754_CLT | IEEE754_CUN,		/* cmp_ult (sig) cmp_nge */
	IEEE754_CLT | IEEE754_CEQ,		/* cmp_ole (sig) cmp_le */
	IEEE754_CLT | IEEE754_CEQ | IEEE754_CUN, /* cmp_ule (sig) cmp_ngt */
};

#define SIFROMREG(si,x)	((si) = ctx->regs[x])
#define SITOREG(si,x)	(ctx->regs[x] = (int)(si))

#if __mips64 && !defined(SINGLE_ONLY_FPU)
#define DIFROMREG(di,x)	((di) = ctx->regs[x])
#define DITOREG(di,x)	(ctx->regs[x] = (di))
#endif

#define SPFROMREG(sp,x)	((sp).bits = ctx->regs[x])
#define SPTOREG(sp,x)	(ctx->regs[x] = (sp).bits)

#ifdef CP0_STATUS_FR_SUPPORT
#define DPFROMREG(dp,x)	((dp).bits = \
			ctx->regs[(xcp->cp0_status & ST0_FR) ? x : (x & ~1)])
#define DPTOREG(dp,x)	(ctx->regs[(xcp->cp0_status & ST0_FR) ? x : (x & ~1)]\
			= (dp).bits)
#else
/* Beware: MIPS COP1 doubles are always little_word endian in registers */
#define DPFROMREG(dp,x)	\
  ((dp).bits = ((unsigned long long)ctx->regs[(x)+1] << 32) | ctx->regs[x])
#define DPTOREG(dp,x) \
  (ctx->regs[x] = (dp).bits, ctx->regs[(x)+1] = (dp).bits >> 32)
#endif

#if __mips >= 4 && __mips != 32

/*
 * Additional MIPS4 instructions
 */

static ieee754dp fpemu_dp_recip(ieee754dp d)
{
	return ieee754dp_div(ieee754dp_one(0), d);
}

static ieee754dp fpemu_dp_rsqrt(ieee754dp d)
{
	return ieee754dp_div(ieee754dp_one(0), ieee754dp_sqrt(d));
}

static ieee754sp fpemu_sp_recip(ieee754sp s)
{
	return ieee754sp_div(ieee754sp_one(0), s);
}

static ieee754sp fpemu_sp_rsqrt(ieee754sp s)
{
	return ieee754sp_div(ieee754sp_one(0), ieee754sp_sqrt(s));
}


static ieee754dp fpemu_dp_madd(ieee754dp r, ieee754dp s, ieee754dp t)
{
	return ieee754dp_add(ieee754dp_mul(s, t), r);
}

static ieee754dp fpemu_dp_msub(ieee754dp r, ieee754dp s, ieee754dp t)
{
	return ieee754dp_sub(ieee754dp_mul(s, t), r);
}

static ieee754dp fpemu_dp_nmadd(ieee754dp r, ieee754dp s, ieee754dp t)
{
	return ieee754dp_neg(ieee754dp_add(ieee754dp_mul(s, t), r));
}

static ieee754dp fpemu_dp_nmsub(ieee754dp r, ieee754dp s, ieee754dp t)
{
	return ieee754dp_neg(ieee754dp_sub(ieee754dp_mul(s, t), r));
}


static ieee754sp fpemu_sp_madd(ieee754sp r, ieee754sp s, ieee754sp t)
{
	return ieee754sp_add(ieee754sp_mul(s, t), r);
}

static ieee754sp fpemu_sp_msub(ieee754sp r, ieee754sp s, ieee754sp t)
{
	return ieee754sp_sub(ieee754sp_mul(s, t), r);
}

static ieee754sp fpemu_sp_nmadd(ieee754sp r, ieee754sp s, ieee754sp t)
{
	return ieee754sp_neg(ieee754sp_add(ieee754sp_mul(s, t), r));
}

static ieee754sp fpemu_sp_nmsub(ieee754sp r, ieee754sp s, ieee754sp t)
{
	return ieee754sp_neg(ieee754sp_sub(ieee754sp_mul(s, t), r));
}

static int
fpux_emu(struct pt_regs *xcp, struct mips_fpu_soft_struct *ctx,
	 mips_instruction ir)
{
	unsigned rcsr = 0;	/* resulting csr */

	fpuemuprivate.stats.cp1xops++;

	switch (MIPSInst_FMA_FFMT(ir)) {
	case s_fmt:		/* 0 */
		{
			ieee754sp(*handler) (ieee754sp, ieee754sp,
					     ieee754sp);
			ieee754sp fd, fr, fs, ft;

			switch (MIPSInst_FUNC(ir)) {
			case lwxc1_op:
				{
					void *va =
					    REG_TO_VA(xcp->
						      regs[MIPSInst_FR(ir)]
						      +
						      xcp->
						      regs[MIPSInst_FT
							   (ir)]);
					fpureg_t val;
					int err = 0;
					val = mips_get_word(xcp, va, &err);
					if (err) {
						fpuemuprivate.stats.
						    errors++;
						return SIGBUS;
					}
					if (xcp->cp0_status & ST0_FR) {
						/* load whole register */
						ctx->
						    regs[MIPSInst_FD(ir)] =
						    val;
					} else if (MIPSInst_FD(ir) & 1) {
						/* load to m.s. 32 bits */
#if defined(SINGLE_ONLY_FPU)
						/* illegal register in single-float mode */
						return SIGILL;
#else
						ctx->
						    regs[
							 (MIPSInst_FD(ir) &
							  ~1)] &=
						    0xffffffff;
						ctx->
						    regs[
							 (MIPSInst_FD(ir) &
							  ~1)] |=
						    val << 32;
#endif
					} else {
						/* load to l.s. 32 bits */
						ctx->
						    regs[MIPSInst_FD(ir)]
						    &= ~0xffffffffLL;
						ctx->
						    regs[MIPSInst_FD(ir)]
						    |= val;
					}
				}
				break;

			case swxc1_op:
				{
					void *va =
					    REG_TO_VA(xcp->
						      regs[MIPSInst_FR(ir)]
						      +
						      xcp->
						      regs[MIPSInst_FT
							   (ir)]);
					unsigned int val;
					if (xcp->cp0_status & ST0_FR) {
						/* store whole register */
						val =
						    ctx->
						    regs[MIPSInst_FS(ir)];
					} else if (MIPSInst_FS(ir) & 1) {
#if defined(SINGLE_ONLY_FPU)
						/* illegal register in single-float mode */
						return SIGILL;
#else
						/* store from m.s. 32 bits */
						val =
						    ctx->
						    regs[
							 (MIPSInst_FS(ir) &
							  ~1)] >> 32;
#endif
					} else {
						/* store from l.s. 32 bits */
						val =
						    ctx->
						    regs[MIPSInst_FS(ir)];
					}
					if (mips_put_word(xcp, va, val)) {
						fpuemuprivate.stats.
						    errors++;
						return SIGBUS;
					}
				}
				break;

			case madd_s_op:
				handler = fpemu_sp_madd;
				goto scoptop;
			case msub_s_op:
				handler = fpemu_sp_msub;
				goto scoptop;
			case nmadd_s_op:
				handler = fpemu_sp_nmadd;
				goto scoptop;
			case nmsub_s_op:
				handler = fpemu_sp_nmsub;
				goto scoptop;

			      scoptop:
				SPFROMREG(fr, MIPSInst_FR(ir));
				SPFROMREG(fs, MIPSInst_FS(ir));
				SPFROMREG(ft, MIPSInst_FT(ir));
				fd = (*handler) (fr, fs, ft);
				SPTOREG(fd, MIPSInst_FD(ir));

			      copcsr:
				if (ieee754_cxtest(IEEE754_INEXACT))
					rcsr |=
					    FPU_CSR_INE_X | FPU_CSR_INE_S;
				if (ieee754_cxtest(IEEE754_UNDERFLOW))
					rcsr |=
					    FPU_CSR_UDF_X | FPU_CSR_UDF_S;
				if (ieee754_cxtest(IEEE754_OVERFLOW))
					rcsr |=
					    FPU_CSR_OVF_X | FPU_CSR_OVF_S;
				if (ieee754_cxtest
				    (IEEE754_INVALID_OPERATION)) rcsr |=
					    FPU_CSR_INV_X | FPU_CSR_INV_S;

				ctx->sr =
				    (ctx->sr & ~FPU_CSR_ALL_X) | rcsr;
				if ((ctx->sr >> 5) & ctx->
				    sr & FPU_CSR_ALL_E) {
		/*printk ("SIGFPE: fpu csr = %08x\n",ctx->sr); */
					return SIGFPE;
				}

				break;

			default:
				return SIGILL;
			}
		}
		break;

#if !defined(SINGLE_ONLY_FPU)
	case d_fmt:		/* 1 */
		{
			ieee754dp(*handler) (ieee754dp, ieee754dp,
					     ieee754dp);
			ieee754dp fd, fr, fs, ft;

			switch (MIPSInst_FUNC(ir)) {
			case ldxc1_op:
				{
					void *va =
					    REG_TO_VA(xcp->
						      regs[MIPSInst_FR(ir)]
						      +
						      xcp->
						      regs[MIPSInst_FT
							   (ir)]);
					int err = 0;
					ctx->regs[MIPSInst_FD(ir)] =
					    mips_get_dword(xcp, va, &err);
					if (err) {
						fpuemuprivate.stats.
						    errors++;
						return SIGBUS;
					}
				}
				break;

			case sdxc1_op:
				{
					void *va =
					    REG_TO_VA(xcp->
						      regs[MIPSInst_FR(ir)]
						      +
						      xcp->
						      regs[MIPSInst_FT
							   (ir)]);
					if (mips_put_dword
					    (xcp, va,
					     ctx->regs[MIPSInst_FS(ir)])) {
						fpuemuprivate.stats.
						    errors++;
						return SIGBUS;
					}
				}
				break;

			case madd_d_op:
				handler = fpemu_dp_madd;
				goto dcoptop;
			case msub_d_op:
				handler = fpemu_dp_msub;
				goto dcoptop;
			case nmadd_d_op:
				handler = fpemu_dp_nmadd;
				goto dcoptop;
			case nmsub_d_op:
				handler = fpemu_dp_nmsub;
				goto dcoptop;

			      dcoptop:
				DPFROMREG(fr, MIPSInst_FR(ir));
				DPFROMREG(fs, MIPSInst_FS(ir));
				DPFROMREG(ft, MIPSInst_FT(ir));
				fd = (*handler) (fr, fs, ft);
				DPTOREG(fd, MIPSInst_FD(ir));
				goto copcsr;

			default:
				return SIGILL;
			}
		}
		break;
#endif

	case 0x7:		/* 7 */
		{
			if (MIPSInst_FUNC(ir) != pfetch_op) {
				return SIGILL;
			}
			/* ignore prefx operation */
		}
		break;

	default:
		return SIGILL;
	}

	return 0;
}
#endif



/*
 * Emulate a single COP1 arithmetic instruction.
 */
static int
fpu_emu(struct pt_regs *xcp, struct mips_fpu_soft_struct *ctx,
	mips_instruction ir)
{
	int rfmt;		/* resulting format */
	unsigned rcsr = 0;	/* resulting csr */
	unsigned cond;
	union {
		ieee754dp d;
		ieee754sp s;
		int w;
#if __mips64
		long long l;
#endif
	} rv;			/* resulting value */

	fpuemuprivate.stats.cp1ops++;
	switch (rfmt = (MIPSInst_FFMT(ir) & 0xf)) {

	case s_fmt:{		/* 0 */
		ieee754sp(*handler) ();

		switch (MIPSInst_FUNC(ir)) {
			/* binary ops */
		case fadd_op:
			handler = ieee754sp_add;
			goto scopbop;
		case fsub_op:
			handler = ieee754sp_sub;
			goto scopbop;
		case fmul_op:
			handler = ieee754sp_mul;
			goto scopbop;
		case fdiv_op:
			handler = ieee754sp_div;
			goto scopbop;

			/* unary  ops */
#if __mips >= 2 || __mips64
		case fsqrt_op:
			handler = ieee754sp_sqrt;
			goto scopuop;
#endif
#if __mips >= 4 && __mips != 32
		case frsqrt_op:
			handler = fpemu_sp_rsqrt;
			goto scopuop;
		case frecip_op:
			handler = fpemu_sp_recip;
			goto scopuop;
#endif
#if __mips >= 4
		case fmovc_op:
			cond = fpucondbit[MIPSInst_FT(ir) >> 2];
			if (((ctx->sr & cond) != 0) !=
			    ((MIPSInst_FT(ir) & 1) != 0))
				return 0;
			SPFROMREG(rv.s, MIPSInst_FS(ir));
			break;
		case fmovz_op:
			if (xcp->regs[MIPSInst_FT(ir)] != 0)
				return 0;
			SPFROMREG(rv.s, MIPSInst_FS(ir));
			break;
		case fmovn_op:
			if (xcp->regs[MIPSInst_FT(ir)] == 0)
				return 0;
			SPFROMREG(rv.s, MIPSInst_FS(ir));
			break;
#endif
		case fabs_op:
			handler = ieee754sp_abs;
			goto scopuop;
		case fneg_op:
			handler = ieee754sp_neg;
			goto scopuop;
		case fmov_op:
			/* an easy one */
			SPFROMREG(rv.s, MIPSInst_FS(ir));
			break;
			/* binary op on handler */
scopbop:
			{
				ieee754sp fs, ft;

				SPFROMREG(fs, MIPSInst_FS(ir));
				SPFROMREG(ft, MIPSInst_FT(ir));

				rv.s = (*handler) (fs, ft);
				goto copcsr;
			}
scopuop:
			{
				ieee754sp fs;

				SPFROMREG(fs, MIPSInst_FS(ir));
				rv.s = (*handler) (fs);
				goto copcsr;
			}
copcsr:
			if (ieee754_cxtest(IEEE754_INEXACT))
				rcsr |= FPU_CSR_INE_X | FPU_CSR_INE_S;
			if (ieee754_cxtest(IEEE754_UNDERFLOW))
				rcsr |= FPU_CSR_UDF_X | FPU_CSR_UDF_S;
			if (ieee754_cxtest(IEEE754_OVERFLOW))
				rcsr |= FPU_CSR_OVF_X | FPU_CSR_OVF_S;
			if (ieee754_cxtest(IEEE754_ZERO_DIVIDE))
				rcsr |= FPU_CSR_DIV_X | FPU_CSR_DIV_S;
			if (ieee754_cxtest
				(IEEE754_INVALID_OPERATION)) rcsr |=
					    FPU_CSR_INV_X | FPU_CSR_INV_S;
				break;

				/* unary conv ops */
		case fcvts_op:
			return SIGILL;	/* not defined */
		case fcvtd_op:
#if defined(SINGLE_ONLY_FPU)
			return SIGILL;	/* not defined */
#else
			{
				ieee754sp fs;

				SPFROMREG(fs, MIPSInst_FS(ir));
				rv.d = ieee754dp_fsp(fs);
				rfmt = d_fmt;
				goto copcsr;
			}
#endif
		case fcvtw_op:
			{
				ieee754sp fs;

				SPFROMREG(fs, MIPSInst_FS(ir));
				rv.w = ieee754sp_tint(fs);
				rfmt = w_fmt;
				goto copcsr;
			}

#if __mips >= 2 || __mips64
		case fround_op:
		case ftrunc_op:
		case fceil_op:
		case ffloor_op:
			{
				unsigned int oldrm = ieee754_csr.rm;
				ieee754sp fs;

				SPFROMREG(fs, MIPSInst_FS(ir));
				ieee754_csr.rm = ieee_rm[MIPSInst_FUNC(ir) & 0x3];
				rv.w = ieee754sp_tint(fs);
				ieee754_csr.rm = oldrm;
				rfmt = w_fmt;
				goto copcsr;
			}
#endif			/* __mips >= 2 */

#if __mips64 && !defined(SINGLE_ONLY_FPU)
		case fcvtl_op:
			{
				ieee754sp fs;

				SPFROMREG(fs, MIPSInst_FS(ir));
				rv.l = ieee754sp_tlong(fs);
				rfmt = l_fmt;
				goto copcsr;
			}

		case froundl_op:
		case ftruncl_op:
		case fceill_op:
		case ffloorl_op:
			{
				unsigned int oldrm = ieee754_csr.rm;
				ieee754sp fs;

				SPFROMREG(fs, MIPSInst_FS(ir));
				ieee754_csr.rm = ieee_rm[MIPSInst_FUNC(ir) & 0x3];
				rv.l = ieee754sp_tlong(fs);
				ieee754_csr.rm = oldrm;
				rfmt = l_fmt;
				goto copcsr;
			}
#endif /* __mips64 && !fpu(single) */

		default:
			if (MIPSInst_FUNC(ir) >= fcmp_op) {
				unsigned cmpop = MIPSInst_FUNC(ir) - fcmp_op;
				ieee754sp fs, ft;

				SPFROMREG(fs, MIPSInst_FS(ir));
				SPFROMREG(ft, MIPSInst_FT(ir));
				rv.w = ieee754sp_cmp(fs, ft, cmptab[cmpop & 0x7]);
				rfmt = -1;
				if ((cmpop & 0x8) && ieee754_cxtest(IEEE754_INVALID_OPERATION))
					rcsr = FPU_CSR_INV_X | FPU_CSR_INV_S;
				} else {
					return SIGILL;
				}
				break;
			}
			break;
		}

#if !defined(SINGLE_ONLY_FPU)
	case d_fmt: {
		ieee754dp(*handler) ();

		switch (MIPSInst_FUNC(ir)) {
			/* binary ops */
		case fadd_op:
			handler = ieee754dp_add;
			goto dcopbop;
		case fsub_op:
			handler = ieee754dp_sub;
			goto dcopbop;
		case fmul_op:
			handler = ieee754dp_mul;
			goto dcopbop;
		case fdiv_op:
			handler = ieee754dp_div;
			goto dcopbop;

			/* unary  ops */
#if __mips >= 2 || __mips64
		case fsqrt_op:
			handler = ieee754dp_sqrt;
			goto dcopuop;
#endif
#if __mips >= 4 && __mips != 32
		case frsqrt_op:
			handler = fpemu_dp_rsqrt;
			goto dcopuop;
		case frecip_op:
			handler = fpemu_dp_recip;
			goto dcopuop;
#endif
#if __mips >= 4
		case fmovc_op:
			cond = fpucondbit[MIPSInst_FT(ir) >> 2];
			if (((ctx->sr & cond) != 0) != ((MIPSInst_FT(ir) & 1) != 0))
				return 0;
			DPFROMREG(rv.d, MIPSInst_FS(ir));
			break;
		case fmovz_op:
			if (xcp->regs[MIPSInst_FT(ir)] != 0)
				return 0;
			DPFROMREG(rv.d, MIPSInst_FS(ir));
			break;
		case fmovn_op:
			if (xcp->regs[MIPSInst_FT(ir)] == 0)
				return 0;
			DPFROMREG(rv.d, MIPSInst_FS(ir));
			break;
#endif
		case fabs_op:
			handler = ieee754dp_abs;
			goto dcopuop;
		case fneg_op:
			handler = ieee754dp_neg;
			goto dcopuop;
		case fmov_op:
			/* an easy one */
			DPFROMREG(rv.d, MIPSInst_FS(ir));
			break;

			/* binary op on handler */
dcopbop:
			{
				ieee754dp fs, ft;

				DPFROMREG(fs, MIPSInst_FS(ir));
				DPFROMREG(ft, MIPSInst_FT(ir));

				rv.d = (*handler) (fs, ft);
				goto copcsr;
			}
dcopuop:
			{
				ieee754dp fs;

				DPFROMREG(fs, MIPSInst_FS(ir));
				rv.d = (*handler) (fs);
				goto copcsr;
			}

		/* unary conv ops */
		case fcvts_op:
			{
				ieee754dp fs;

				DPFROMREG(fs, MIPSInst_FS(ir));
				rv.s = ieee754sp_fdp(fs);
				rfmt = s_fmt;
				goto copcsr;
			}
		case fcvtd_op:
			return SIGILL;	/* not defined */
		case fcvtw_op:
			{
				ieee754dp fs;

				DPFROMREG(fs, MIPSInst_FS(ir));
				rv.w = ieee754dp_tint(fs);	/* wrong */
				rfmt = w_fmt;
				goto copcsr;
			}

#if __mips >= 2 || __mips64
		case fround_op:
		case ftrunc_op:
		case fceil_op:
		case ffloor_op:
			{
				unsigned int oldrm = ieee754_csr.rm;
				ieee754dp fs;

				DPFROMREG(fs, MIPSInst_FS(ir));
				ieee754_csr.rm = ieee_rm[MIPSInst_FUNC(ir) & 0x3];
				rv.w = ieee754dp_tint(fs);
				ieee754_csr.rm = oldrm;
				rfmt = w_fmt;
				goto copcsr;
			}
#endif

#if __mips64 && !defined(SINGLE_ONLY_FPU)
		case fcvtl_op:
			{
				ieee754dp fs;

				DPFROMREG(fs, MIPSInst_FS(ir));
				rv.l = ieee754dp_tlong(fs);
				rfmt = l_fmt;
				goto copcsr;
			}

		case froundl_op:
		case ftruncl_op:
		case fceill_op:
		case ffloorl_op:
			{
				unsigned int oldrm = ieee754_csr.rm;
				ieee754dp fs;

				DPFROMREG(fs, MIPSInst_FS(ir));
				ieee754_csr.rm = ieee_rm[MIPSInst_FUNC(ir) & 0x3];
				rv.l = ieee754dp_tlong(fs);
				ieee754_csr.rm = oldrm;
				rfmt = l_fmt;
				goto copcsr;
			}
#endif /* __mips >= 3 && !fpu(single) */

		default:
			if (MIPSInst_FUNC(ir) >= fcmp_op) {
				unsigned cmpop = MIPSInst_FUNC(ir) - fcmp_op;
				ieee754dp fs, ft;

				DPFROMREG(fs, MIPSInst_FS(ir));
				DPFROMREG(ft, MIPSInst_FT(ir));
				rv.w = ieee754dp_cmp(fs, ft, cmptab[cmpop & 0x7]);
				rfmt = -1;
				if ((cmpop & 0x8) && ieee754_cxtest (IEEE754_INVALID_OPERATION))
					rcsr = FPU_CSR_INV_X | FPU_CSR_INV_S;
			} else {
				return SIGILL;
			}
			break;
		}
		break;
	}
#endif				/* !defined(SINGLE_ONLY_FPU) */

	case w_fmt: {
		switch (MIPSInst_FUNC(ir)) {
		case fcvts_op:
			/* convert word to single precision real */
			rv.s = ieee754sp_fint(ctx-> regs[MIPSInst_FS(ir)]);
			rfmt = s_fmt;
			goto copcsr;
#if !defined(SINGLE_ONLY_FPU)
		case fcvtd_op:
			/* convert word to double precision real */
			rv.d = ieee754dp_fint(ctx-> regs[MIPSInst_FS(ir)]);
			rfmt = d_fmt;
			goto copcsr;
#endif
		default:
			return SIGILL;
		}
		break;
	}

#if __mips64 && !defined(SINGLE_ONLY_FPU)
	case l_fmt: {
		switch (MIPSInst_FUNC(ir)) {
		case fcvts_op:
			/* convert long to single precision real */
			rv.s = ieee754sp_flong(ctx-> regs[MIPSInst_FS(ir)]);
			rfmt = s_fmt;
			goto copcsr;
		case fcvtd_op:
			/* convert long to double precision real */
			rv.d = ieee754dp_flong(ctx-> regs[MIPSInst_FS(ir)]);
			rfmt = d_fmt;
			goto copcsr;
		default:
			return SIGILL;
		}
		break;
	}
#endif

	default:
		return SIGILL;
	}

	/*
	 * Update the fpu CSR register for this operation.
	 * If an exception is required, generate a tidy SIGFPE exception,
	 * without updating the result register.
	 * Note: cause exception bits do not accumulate, they are rewritten
	 * for each op; only the flag/sticky bits accumulate.
	 */
	ctx->sr = (ctx->sr & ~FPU_CSR_ALL_X) | rcsr;
	if ((ctx->sr >> 5) & ctx->sr & FPU_CSR_ALL_E) {
		/*printk ("SIGFPE: fpu csr = %08x\n",ctx->sr); */
		return SIGFPE;
	}

	/* 
	 * Now we can safely write the result back to the register file.
	 */
	switch (rfmt) {
	case -1: {
#if __mips >= 4
		cond = fpucondbit[MIPSInst_FD(ir) >> 2];
#else
		cond = FPU_CSR_COND;
#endif
		if (rv.w)
			ctx->sr |= cond;
		else
			ctx->sr &= ~cond;
		break;
	}
#if !defined(SINGLE_ONLY_FPU)
	case d_fmt:
		DPTOREG(rv.d, MIPSInst_FD(ir));
		break;
#endif
	case s_fmt:
		SPTOREG(rv.s, MIPSInst_FD(ir));
		break;
	case w_fmt:
		SITOREG(rv.w, MIPSInst_FD(ir));
		break;
#if __mips64 && !defined(SINGLE_ONLY_FPU)
	case l_fmt:
		DITOREG(rv.l, MIPSInst_FD(ir));
		break;
#endif
	default:
		return SIGILL;
	}

	return 0;
}


/*
 * Emulate the floating point instruction at EPC, and continue
 * to run until we hit a non-fp instruction, or a backward
 * branch.  This cuts down dramatically on the per instruction 
 * exception overhead.
 */
int fpu_emulator_cop1Handler(int xcptno, struct pt_regs *xcp)
{
	struct mips_fpu_soft_struct *ctx = &current->thread.fpu.soft;
	unsigned long oldepc, prevepc;
	unsigned int insn;
	int sig = 0;
	int err = 0;

	oldepc = xcp->cp0_epc;
	do {
		if (current->need_resched)
			schedule();

		prevepc = xcp->cp0_epc;
		insn = mips_get_word(xcp, REG_TO_VA(xcp->cp0_epc), &err);
		if (err) {
			fpuemuprivate.stats.errors++;
			return SIGBUS;
		}
		if (insn != 0)
			sig = cop1Emulate(xcptno, xcp, ctx);
		else
			xcp->cp0_epc += 4;	/* skip nops */

		if (mips_cpu.options & MIPS_CPU_FPU)
			break;
	} while (xcp->cp0_epc > prevepc && sig == 0);

	/* SIGILL indicates a non-fpu instruction */
	if (sig == SIGILL && xcp->cp0_epc != oldepc)
		/* but if epc has advanced, then ignore it */
		sig = 0;

	return sig;
}


#ifdef NOTDEF
/*
 * Patch up the hardware fpu state when an f.p. exception occurs.  
 */
static int cop1Patcher(int xcptno, struct pt_regs *xcp)
{
	struct mips_fpu_soft_struct *ctx = &current->thread.fpu.soft;
	unsigned sr;
	int sig;

	/* reenable Cp1, else fpe_save() will get nested exception */
	sr = mips_bissr(ST0_CU1);

	/* get fpu registers and status, then clear pending exceptions */
	fpe_save(ctx);
	fpe_setsr(ctx->sr &= ~FPU_CSR_ALL_X);

	/* get current rounding mode for IEEE library, and emulate insn */
	ieee754_csr.rm = ieee_rm[ctx->sr & 0x3];
	sig = cop1Emulate(xcptno, xcp, ctx);

	/* don't return with f.p. exceptions pending */
	ctx->sr &= ~FPU_CSR_ALL_X;
	fpe_restore(ctx);

	mips_setsr(sr);
	return sig;
}

void _cop1_init(int emulate)
{
	extern int _nofpu;

	if (emulate) {
		/* 
		 * Install cop1 emulator to handle "coprocessor unusable" exception
		 */
		xcption(XCPTCPU, cop1Handler);
		fpuemuactive = 1;	/* tell dbg.c that we are in charge */
		_nofpu = 0;	/* tell setjmp() it "has" an fpu */
	} else {
		/* 
		 * Install cop1 emulator for floating point exceptions only,
		 * i.e. denormalised results, underflow, overflow etc, which
		 * must be emulated in s/w.
		 */
#ifdef 1
		/* r4000 or above use dedicate exception */
		xcption(XCPTFPE, cop1Patcher);
#else
		/* r3000 et al use interrupt */
		extern int _sbd_getfpuintr(void);
		int intno = _sbd_getfpuintr();
		intrupt(intno, cop1Patcher, 0);
		mips_bissr(SR_IM0 << intno);
#endif

#if (#cpu(r4640) || #cpu(r4650)) && !defined(SINGLE_ONLY_FPU)
		/* For R4640/R4650 compiled *without* the -msingle-float flag,
		   then we share responsibility: the h/w handles the single
		   precision operations, and the trap emulator handles the
		   double precision. We set fpuemuactive so that dbg.c first
		   fetches the s/w state before saving the h/w state. */
		fpuemuactive = 1;
		{
			int i;
			/* initialise the unused d.p high order words to be NaN */
			for (i = 0; i < 32; i++)
				current->thread.fpu.soft.regs[i] =
				    0x7ff80bad00000000LL;
		}
#endif				/* (r4640 || r4650) && !fpu(single) */
	}
}
#endif


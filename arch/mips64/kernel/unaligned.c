/*
 * Handle unaligned accesses by emulation.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1998, 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 *
 * This file contains exception handler for address error exception with the
 * special capability to execute faulting instructions in software.  The
 * handler does not try to handle the case when the program counter points
 * to an address not aligned to a word boundary.
 *
 * Putting data to unaligned addresses is a bad practice even on Intel where
 * only the performance is affected.  Much worse is that such code is non-
 * portable.  Due to several programs that die on MIPS due to alignment
 * problems I decided to implement this handler anyway though I originally
 * didn't intend to do this at all for user code.
 *
 * For now I enable fixing of address errors by default to make life easier.
 * I however intend to disable this somewhen in the future when the alignment
 * problems with user programs have been fixed.  For programmers this is the
 * right way to go.
 *
 * Fixing address errors is a per process option.  The option is inherited
 * across fork(2) and execve(2) calls.  If you really want to use the
 * option in your user programs - I discourage the use of the software
 * emulation strongly - use the following code in your userland stuff:
 *
 * #include <sys/sysmips.h>
 *
 * ...
 * sysmips(MIPS_FIXADE, x);
 * ...
 *
 * The argument x is 0 for disabling software emulation, enabled otherwise.
 *
 * Below a little program to play around with this feature.
 *
 * #include <stdio.h>
 * #include <asm/sysmips.h>
 * 
 * struct foo {
 *         unsigned char bar[8];
 * };
 *
 * main(int argc, char *argv[])
 * {
 *         struct foo x = {0, 1, 2, 3, 4, 5, 6, 7};
 *         unsigned int *p = (unsigned int *) (x.bar + 3);
 *         int i;
 *
 *         if (argc > 1)
 *                 sysmips(MIPS_FIXADE, atoi(argv[1]));
 *
 *         printf("*p = %08lx\n", *p);
 *
 *         *p = 0xdeadface;
 *
 *         for(i = 0; i <= 7; i++)
 *         printf("%02x ", x.bar[i]);
 *         printf("\n");
 * }
 *
 * Coprocessor loads are not supported; I think this case is unimportant
 * in the practice.
 *
 * TODO: Handle ndc (attempted store to doubleword in uncached memory)
 *       exception for the R6000.
 *       A store crossing a page boundary might be executed only partially.
 *       Undo the partial store in this case.
 */
#include <linux/mm.h>
#include <linux/signal.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/asm.h>
#include <asm/branch.h>
#include <asm/byteorder.h>
#include <asm/inst.h>
#include <asm/uaccess.h>

#define STR(x)  __STR(x)
#define __STR(x)  #x

/*
 * User code may only access USEG; kernel code may access the
 * entire address space.
 */
#define check_axs(pc,a,s)				\
	if ((long)(~(pc) & ((a) | ((a)+(s)))) < 0)	\
		goto sigbus;

static inline void
emulate_load_store_insn(struct pt_regs *regs,
                        unsigned long addr,
                        unsigned long pc)
{
	union mips_instruction insn;
	unsigned long value, fixup;

	regs->regs[0] = 0;
	/*
	 * This load never faults.
	 */
	__get_user(insn.word, (unsigned int *)pc);

	switch (insn.i_format.opcode) {
	/*
	 * These are instructions that a compiler doesn't generate.  We
	 * can assume therefore that the code is MIPS-aware and
	 * really buggy.  Emulating these instructions would break the
	 * semantics anyway.
	 */
	case ll_op:
	case lld_op:
	case sc_op:
	case scd_op:

	/*
	 * For these instructions the only way to create an address
	 * error is an attempted access to kernel/supervisor address
	 * space.
	 */
	case ldl_op:
	case ldr_op:
	case lwl_op:
	case lwr_op:
	case sdl_op:
	case sdr_op:
	case swl_op:
	case swr_op:
	case lb_op:
	case lbu_op:
	case sb_op:
		goto sigbus;

	/*
	 * The remaining opcodes are the ones that are really of interest.
	 */
	case lh_op:
		check_axs(pc, addr, 2);
		__asm__(
			".set\tnoat\n"
#ifdef __BIG_ENDIAN
			"1:\tlb\t%0,0(%1)\n"
			"2:\tlbu\t$1,1(%1)\n\t"
#endif
#ifdef __LITTLE_ENDIAN
			"1:\tlb\t%0,1(%1)\n"
			"2:\tlbu\t$1,0(%1)\n\t"
#endif
			"sll\t%0,0x8\n\t"
			"or\t%0,$1\n\t"
			".set\tat\n\t"
			".section\t__ex_table,\"a\"\n\t"
			STR(PTR)"\t1b,%2\n\t"
			STR(PTR)"\t2b,%2\n\t"
			".previous"
			:"=&r" (value)
			:"r" (addr), "i" (&&fault)
			:"$1");
		regs->regs[insn.i_format.rt] = value;
		return;

	case lw_op:
		check_axs(pc, addr, 4);
		__asm__(
#ifdef __BIG_ENDIAN
			"1:\tlwl\t%0,(%1)\n"
			"2:\tlwr\t%0,3(%1)\n\t"
#endif
#ifdef __LITTLE_ENDIAN
			"1:\tlwl\t%0,3(%1)\n"
			"2:\tlwr\t%0,(%1)\n\t"
#endif
			".section\t__ex_table,\"a\"\n\t"
			STR(PTR)"\t1b,%2\n\t"
			STR(PTR)"\t2b,%2\n\t"
			".previous"
			:"=&r" (value)
			:"r" (addr), "i" (&&fault));
			regs->regs[insn.i_format.rt] = value;
			return;

	case lhu_op:
		check_axs(pc, addr, 2);
		__asm__(
			".set\tnoat\n"
#ifdef __BIG_ENDIAN
			"1:\tlbu\t%0,0(%1)\n"
			"2:\tlbu\t$1,1(%1)\n\t"
#endif
#ifdef __LITTLE_ENDIAN
			"1:\tlbu\t%0,1(%1)\n"
			"2:\tlbu\t$1,0(%1)\n\t"
#endif
			"sll\t%0,0x8\n\t"
			"or\t%0,$1\n\t"
			".set\tat\n\t"
			".section\t__ex_table,\"a\"\n\t"
			STR(PTR)"\t1b,%2\n\t"
			STR(PTR)"\t2b,%2\n\t"
			".previous"
			:"=&r" (value)
			:"r" (addr), "i" (&&fault)
			:"$1");
		regs->regs[insn.i_format.rt] = value;
		return;

	case lwu_op:
		check_axs(pc, addr, 4);
		__asm__(
#ifdef __BIG_ENDIAN
			"1:\tlwl\t%0,(%1)\n"
			"2:\tlwr\t%0,3(%1)\n\t"
#endif
#ifdef __LITTLE_ENDIAN
			"1:\tlwl\t%0,3(%1)\n"
			"2:\tlwr\t%0,(%1)\n\t"
#endif
			".section\t__ex_table,\"a\"\n\t"
			STR(PTR)"\t1b,%2\n\t"
			STR(PTR)"\t2b,%2\n\t"
			".previous"
			:"=&r" (value)
			:"r" (addr), "i" (&&fault));
		value &= 0xffffffff;
		regs->regs[insn.i_format.rt] = value;
		return;

	case ld_op:
		check_axs(pc, addr, 8);
		__asm__(
			".set\tmips3\n"
#ifdef __BIG_ENDIAN
			"1:\tldl\t%0,(%1)\n"
			"2:\tldr\t%0,7(%1)\n\t"
#endif
#ifdef __LITTLE_ENDIAN
			"1:\tldl\t%0,7(%1)\n"
			"2:\tldr\t%0,(%1)\n\t"
#endif
			".set\tmips0\n\t"
			".section\t__ex_table,\"a\"\n\t"
			STR(PTR)"\t1b,%2\n\t"
			STR(PTR)"\t2b,%2\n\t"
			".previous"
			:"=&r" (value)
			:"r" (addr), "i" (&&fault));
		regs->regs[insn.i_format.rt] = value;
		return;

	case sh_op:
		check_axs(pc, addr, 2);
		value = regs->regs[insn.i_format.rt];
		__asm__(
#ifdef __BIG_ENDIAN
			".set\tnoat\n"
			"1:\tsb\t%0,1(%1)\n\t"
			"srl\t$1,%0,0x8\n"
			"2:\tsb\t$1,0(%1)\n\t"
			".set\tat\n\t"
#endif
#ifdef __LITTLE_ENDIAN
			".set\tnoat\n"
			"1:\tsb\t%0,0(%1)\n\t"
			"srl\t$1,%0,0x8\n"
			"2:\tsb\t$1,1(%1)\n\t"
			".set\tat\n\t"
#endif
			".section\t__ex_table,\"a\"\n\t"
			STR(PTR)"\t1b,%2\n\t"
			STR(PTR)"\t2b,%2\n\t"
			".previous"
			: /* no outputs */
			:"r" (value), "r" (addr), "i" (&&fault)
			:"$1");
		return;

	case sw_op:
		check_axs(pc, addr, 4);
		value = regs->regs[insn.i_format.rt];
		__asm__(
#ifdef __BIG_ENDIAN
			"1:\tswl\t%0,(%1)\n"
			"2:\tswr\t%0,3(%1)\n\t"
#endif
#ifdef __LITTLE_ENDIAN
			"1:\tswl\t%0,3(%1)\n"
			"2:\tswr\t%0,(%1)\n\t"
#endif
			".section\t__ex_table,\"a\"\n\t"
			STR(PTR)"\t1b,%2\n\t"
			STR(PTR)"\t2b,%2\n\t"
			".previous"
			: /* no outputs */
			:"r" (value), "r" (addr), "i" (&&fault));
		return;

	case sd_op:
		check_axs(pc, addr, 8);
		value = regs->regs[insn.i_format.rt];
		__asm__(
			".set\tmips3\n"
#ifdef __BIG_ENDIAN
			"1:\tsdl\t%0,(%1)\n"
			"2:\tsdr\t%0,7(%1)\n\t"
#endif
#ifdef __LITTLE_ENDIAN
			"1:\tsdl\t%0,7(%1)\n"
			"2:\tsdr\t%0,(%1)\n\t"
#endif
			".set\tmips0\n\t"
			".section\t__ex_table,\"a\"\n\t"
			STR(PTR)"\t1b,%2\n\t"
			STR(PTR)"\t2b,%2\n\t"
			".previous"
			: /* no outputs */
			:"r" (value), "r" (addr), "i" (&&fault));
		return;

	case lwc1_op:
	case ldc1_op:
	case swc1_op:
	case sdc1_op:
		/*
		 * I herewith declare: this does not happen.  So send SIGBUS.
		 */
		goto sigbus;

	case lwc2_op:
	case ldc2_op:
	case swc2_op:
	case sdc2_op:
		/*
		 * These are the coprocessor 2 load/stores.  The current
		 * implementations don't use cp2 and cp2 should always be
		 * disabled in c0_status.  So send SIGILL.
                 * (No longer true: The Sony Praystation uses cp2 for
                 * 3D matrix operations.  Dunno if that thingy has a MMU ...)
		 */
	default:
		/*
		 * Pheeee...  We encountered an yet unknown instruction or
		 * cache coherence problem.  Die sucker, die ...
		 */
		goto sigill;
	}
	return;

fault:
	/* Did we have an exception handler installed? */
	fixup = search_exception_table(regs->cp0_epc);
	if (fixup) {
		long new_epc;
		new_epc = fixup_exception(dpf_reg, fixup, regs->cp0_epc);
		printk(KERN_DEBUG "%s: Forwarding exception at [<%lx>] (%lx)\n",
		       current->comm, regs->cp0_epc, new_epc);
		regs->cp0_epc = new_epc;
		return;
	}

	send_sig(SIGSEGV, current, 1);
	return;
sigbus:
	send_sig(SIGBUS, current, 1);
	return;
sigill:
	send_sig(SIGILL, current, 1);
	return;
}

unsigned long unaligned_instructions;

asmlinkage void do_ade(struct pt_regs *regs)
{
	unsigned long pc;

	/*
	 * Did we catch a fault trying to load an instruction?
	 * This also catches attempts to activate MIPS16 code on
	 * CPUs which don't support it.
	 */
	if (regs->cp0_badvaddr == regs->cp0_epc)
		goto sigbus;

	pc = regs->cp0_epc + ((regs->cp0_cause & CAUSEF_BD) ? 4 : 0);
	if (compute_return_epc(regs))
		return;
	if ((current->thread.mflags & MF_FIXADE) == 0)
		goto sigbus;

	emulate_load_store_insn(regs, regs->cp0_badvaddr, pc);
	unaligned_instructions++;

	return;

sigbus:
	force_sig(SIGBUS, current);
}

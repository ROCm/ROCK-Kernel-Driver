/*
 * This program is used to generate definitions needed by
 * assembly language modules.
 *
 * We use the technique used in the OSF Mach kernel code:
 * generate asm statements containing #defines,
 * compile this file to assembler, and then extract the
 * #defines from the assembly-language output.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/hardirq.h>

#include <asm/naca.h>
#include <asm/paca.h>
#include <asm/iSeries/ItLpPaca.h>
#include <asm/iSeries/ItLpQueue.h>
#include <asm/iSeries/HvLpEvent.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/cputable.h>

#define DEFINE(sym, val) \
	asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

int main(void)
{
	/* thread struct on stack */
	DEFINE(THREAD_SHIFT, THREAD_SHIFT);
	DEFINE(THREAD_SIZE, THREAD_SIZE);
	DEFINE(TI_FLAGS, offsetof(struct thread_info, flags));
	DEFINE(TI_PREEMPT, offsetof(struct thread_info, preempt_count));

	/* task_struct->thread */
	DEFINE(THREAD, offsetof(struct task_struct, thread));
	DEFINE(PT_REGS, offsetof(struct thread_struct, regs));
	DEFINE(THREAD_FPEXC_MODE, offsetof(struct thread_struct, fpexc_mode));
	DEFINE(THREAD_FPR0, offsetof(struct thread_struct, fpr[0]));
	DEFINE(THREAD_FPSCR, offsetof(struct thread_struct, fpscr));
	DEFINE(KSP, offsetof(struct thread_struct, ksp));

#ifdef CONFIG_ALTIVEC
	DEFINE(THREAD_VR0, offsetof(struct thread_struct, vr[0]));
	DEFINE(THREAD_VRSAVE, offsetof(struct thread_struct, vrsave));
	DEFINE(THREAD_VSCR, offsetof(struct thread_struct, vscr));
	DEFINE(THREAD_USED_VR, offsetof(struct thread_struct, used_vr));
#endif /* CONFIG_ALTIVEC */
	DEFINE(MM, offsetof(struct task_struct, mm));

	/* naca */
        DEFINE(PACA, offsetof(struct naca_struct, paca));
	DEFINE(DCACHEL1LINESIZE, offsetof(struct systemcfg, dCacheL1LineSize));
        DEFINE(DCACHEL1LOGLINESIZE, offsetof(struct naca_struct, dCacheL1LogLineSize));
        DEFINE(DCACHEL1LINESPERPAGE, offsetof(struct naca_struct, dCacheL1LinesPerPage));
        DEFINE(ICACHEL1LINESIZE, offsetof(struct systemcfg, iCacheL1LineSize));
        DEFINE(ICACHEL1LOGLINESIZE, offsetof(struct naca_struct, iCacheL1LogLineSize));
        DEFINE(ICACHEL1LINESPERPAGE, offsetof(struct naca_struct, iCacheL1LinesPerPage));
	DEFINE(PLATFORM, offsetof(struct systemcfg, platform));

	/* paca */
        DEFINE(PACA_SIZE, sizeof(struct paca_struct));
        DEFINE(PACAPACAINDEX, offsetof(struct paca_struct, xPacaIndex));
        DEFINE(PACAPROCSTART, offsetof(struct paca_struct, xProcStart));
        DEFINE(PACAKSAVE, offsetof(struct paca_struct, xKsave));
	DEFINE(PACACURRENT, offsetof(struct paca_struct, xCurrent));
        DEFINE(PACASAVEDMSR, offsetof(struct paca_struct, xSavedMsr));
        DEFINE(PACASTABREAL, offsetof(struct paca_struct, xStab_data.real));
        DEFINE(PACASTABVIRT, offsetof(struct paca_struct, xStab_data.virt));
	DEFINE(PACASTABRR, offsetof(struct paca_struct, xStab_data.next_round_robin));
        DEFINE(PACAR1, offsetof(struct paca_struct, xR1));
        DEFINE(PACALPQUEUE, offsetof(struct paca_struct, lpQueuePtr));
	DEFINE(PACATOC, offsetof(struct paca_struct, xTOC));
	DEFINE(PACAEXCSP, offsetof(struct paca_struct, exception_sp));
	DEFINE(PACAPROCENABLED, offsetof(struct paca_struct, xProcEnabled));
	DEFINE(PACADEFAULTDECR, offsetof(struct paca_struct, default_decr));
	DEFINE(PACAPROFENABLED, offsetof(struct paca_struct, prof_enabled));
	DEFINE(PACAPROFLEN, offsetof(struct paca_struct, prof_len));
	DEFINE(PACAPROFSHIFT, offsetof(struct paca_struct, prof_shift));
	DEFINE(PACAPROFBUFFER, offsetof(struct paca_struct, prof_buffer));
	DEFINE(PACAPROFSTEXT, offsetof(struct paca_struct, prof_stext));
	DEFINE(PACALPPACA, offsetof(struct paca_struct, xLpPaca));
        DEFINE(LPPACA, offsetof(struct paca_struct, xLpPaca));
        DEFINE(PACAREGSAV, offsetof(struct paca_struct, xRegSav));
        DEFINE(PACAEXC, offsetof(struct paca_struct, exception_stack));
        DEFINE(PACAGUARD, offsetof(struct paca_struct, guard));
        DEFINE(LPPACASRR0, offsetof(struct ItLpPaca, xSavedSrr0));
        DEFINE(LPPACASRR1, offsetof(struct ItLpPaca, xSavedSrr1));
	DEFINE(LPPACAANYINT, offsetof(struct ItLpPaca, xIntDword.xAnyInt));
	DEFINE(LPPACADECRINT, offsetof(struct ItLpPaca, xIntDword.xFields.xDecrInt));
        DEFINE(LPQCUREVENTPTR, offsetof(struct ItLpQueue, xSlicCurEventPtr));
        DEFINE(LPQOVERFLOW, offsetof(struct ItLpQueue, xPlicOverflowIntPending));
        DEFINE(LPEVENTFLAGS, offsetof(struct HvLpEvent, xFlags));
	DEFINE(PROMENTRY, offsetof(struct prom_t, entry));

	/* RTAS */
	DEFINE(RTASBASE, offsetof(struct rtas_t, base));
	DEFINE(RTASENTRY, offsetof(struct rtas_t, entry));
	DEFINE(RTASSIZE, offsetof(struct rtas_t, size));

	/* Interrupt register frame */
	DEFINE(STACK_FRAME_OVERHEAD, STACK_FRAME_OVERHEAD);

	DEFINE(SWITCH_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs));

	/* 288 = # of volatile regs, int & fp, for leaf routines */
	/* which do not stack a frame.  See the PPC64 ABI.       */
	DEFINE(INT_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs) + 288);
	/* Create extra stack space for SRR0 and SRR1 when calling prom/rtas. */
	DEFINE(PROM_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs) + 16);
	DEFINE(RTAS_FRAME_SIZE, STACK_FRAME_OVERHEAD + sizeof(struct pt_regs) + 16);
	DEFINE(GPR0, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[0]));
	DEFINE(GPR1, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[1]));
	DEFINE(GPR2, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[2]));
	DEFINE(GPR3, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[3]));
	DEFINE(GPR4, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[4]));
	DEFINE(GPR5, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[5]));
	DEFINE(GPR6, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[6]));
	DEFINE(GPR7, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[7]));
	DEFINE(GPR8, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[8]));
	DEFINE(GPR9, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[9]));
	DEFINE(GPR20, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[20]));
	DEFINE(GPR21, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[21]));
	DEFINE(GPR22, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[22]));
	DEFINE(GPR23, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, gpr[23]));
	/*
	 * Note: these symbols include _ because they overlap with special
	 * register names
	 */
	DEFINE(_NIP, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, nip));
	DEFINE(_MSR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, msr));
	DEFINE(_CTR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, ctr));
	DEFINE(_LINK, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, link));
	DEFINE(_CCR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, ccr));
	DEFINE(_XER, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, xer));
	DEFINE(_DAR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, dar));
	DEFINE(_DSISR, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, dsisr));
	DEFINE(ORIG_GPR3, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, orig_gpr3));
	DEFINE(RESULT, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, result));
	DEFINE(TRAP, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, trap));
	DEFINE(SOFTE, STACK_FRAME_OVERHEAD+offsetof(struct pt_regs, softe));

	/* These _only_ to be used with {PROM,RTAS}_FRAME_SIZE!!! */
	DEFINE(_SRR0, STACK_FRAME_OVERHEAD+sizeof(struct pt_regs));
	DEFINE(_SRR1, STACK_FRAME_OVERHEAD+sizeof(struct pt_regs)+8);

	DEFINE(CLONE_VM, CLONE_VM);
	DEFINE(CLONE_UNTRACED, CLONE_UNTRACED);

	/* About the CPU features table */
	DEFINE(CPU_SPEC_ENTRY_SIZE, sizeof(struct cpu_spec));
	DEFINE(CPU_SPEC_PVR_MASK, offsetof(struct cpu_spec, pvr_mask));
	DEFINE(CPU_SPEC_PVR_VALUE, offsetof(struct cpu_spec, pvr_value));
	DEFINE(CPU_SPEC_FEATURES, offsetof(struct cpu_spec, cpu_features));
	DEFINE(CPU_SPEC_SETUP, offsetof(struct cpu_spec, cpu_setup));

	return 0;
}

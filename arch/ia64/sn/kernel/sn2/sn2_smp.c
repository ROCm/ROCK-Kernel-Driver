/*
 * SN2 Platform specific SMP Support
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2004 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/bitops.h>

#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/sal.h>
#include <asm/system.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/tlb.h>
#include <asm/numa.h>
#include <asm/hw_irq.h>
#include <asm/current.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/addrs.h>
#include <asm/sn/shub_mmr.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/rw_mmr.h>

void sn2_ptc_deadlock_recovery(unsigned long data0, unsigned long data1);

static spinlock_t sn2_global_ptc_lock __cacheline_aligned = SPIN_LOCK_UNLOCKED;

static unsigned long sn2_ptc_deadlock_count;

static inline unsigned long wait_piowc(void)
{
	volatile unsigned long *piows;
	unsigned long ws;

	piows = pda->pio_write_status_addr;
	do {
		ia64_mfa();
	} while (((ws =
		   *piows) & SH_PIO_WRITE_STATUS_0_PENDING_WRITE_COUNT_MASK) !=
		 SH_PIO_WRITE_STATUS_0_PENDING_WRITE_COUNT_MASK);
	return ws;
}

void sn_tlb_migrate_finish(struct mm_struct *mm)
{
	if (mm == current->mm)
		flush_tlb_mm(mm);
}

/**
 * sn2_global_tlb_purge - globally purge translation cache of virtual address range
 * @start: start of virtual address range
 * @end: end of virtual address range
 * @nbits: specifies number of bytes to purge per instruction (num = 1<<(nbits & 0xfc))
 *
 * Purges the translation caches of all processors of the given virtual address
 * range.
 *
 * Note:
 * 	- cpu_vm_mask is a bit mask that indicates which cpus have loaded the context.
 * 	- cpu_vm_mask is converted into a nodemask of the nodes containing the
 * 	  cpus in cpu_vm_mask.
 *	- if only one bit is set in cpu_vm_mask & it is the current cpu,
 *	  then only the local TLB needs to be flushed. This flushing can be done
 *	  using ptc.l. This is the common case & avoids the global spinlock.
 *	- if multiple cpus have loaded the context, then flushing has to be
 *	  done with ptc.g/MMRs under protection of the global ptc_lock.
 */

void
sn2_global_tlb_purge(unsigned long start, unsigned long end,
		     unsigned long nbits)
{
	int i, cnode, mynasid, cpu, lcpu = 0, nasid, flushed = 0;
	volatile unsigned long *ptc0, *ptc1;
	unsigned long flags = 0, data0, data1;
	struct mm_struct *mm = current->active_mm;
	short nasids[NR_NODES], nix;
	DECLARE_BITMAP(nodes_flushed, NR_NODES);

	bitmap_zero(nodes_flushed, NR_NODES);

	i = 0;

	for_each_cpu_mask(cpu, mm->cpu_vm_mask) {
		cnode = cpu_to_node(cpu);
		__set_bit(cnode, nodes_flushed);
		lcpu = cpu;
		i++;
	}

	preempt_disable();

	if (likely(i == 1 && lcpu == smp_processor_id())) {
		do {
			ia64_ptcl(start, nbits << 2);
			start += (1UL << nbits);
		} while (start < end);
		ia64_srlz_i();
		preempt_enable();
		return;
	}

	if (atomic_read(&mm->mm_users) == 1) {
		flush_tlb_mm(mm);
		preempt_enable();
		return;
	}

	nix = 0;
	for (cnode = find_first_bit(&nodes_flushed, NR_NODES); cnode < NR_NODES;
	     cnode = find_next_bit(&nodes_flushed, NR_NODES, ++cnode))
		nasids[nix++] = cnodeid_to_nasid(cnode);

	data0 = (1UL << SH_PTC_0_A_SHFT) |
	    (nbits << SH_PTC_0_PS_SHFT) |
	    ((ia64_get_rr(start) >> 8) << SH_PTC_0_RID_SHFT) |
	    (1UL << SH_PTC_0_START_SHFT);

	ptc0 = (long *)GLOBAL_MMR_PHYS_ADDR(0, SH_PTC_0);
	ptc1 = (long *)GLOBAL_MMR_PHYS_ADDR(0, SH_PTC_1);

	mynasid = get_nasid();

	spin_lock_irqsave(&sn2_global_ptc_lock, flags);

	do {
		data1 = start | (1UL << SH_PTC_1_START_SHFT);
		for (i = 0; i < nix; i++) {
			nasid = nasids[i];
			if (likely(nasid == mynasid)) {
				ia64_ptcga(start, nbits << 2);
				ia64_srlz_i();
			} else {
				ptc0 = CHANGE_NASID(nasid, ptc0);
				ptc1 = CHANGE_NASID(nasid, ptc1);
				pio_atomic_phys_write_mmrs(ptc0, data0, ptc1,
							   data1);
				flushed = 1;
			}
		}

		if (flushed
		    && (wait_piowc() &
			SH_PIO_WRITE_STATUS_0_WRITE_DEADLOCK_MASK)) {
			sn2_ptc_deadlock_recovery(data0, data1);
		}

		start += (1UL << nbits);

	} while (start < end);

	spin_unlock_irqrestore(&sn2_global_ptc_lock, flags);

	preempt_enable();
}

/*
 * sn2_ptc_deadlock_recovery
 *
 * Recover from PTC deadlocks conditions. Recovery requires stepping thru each 
 * TLB flush transaction.  The recovery sequence is somewhat tricky & is
 * coded in assembly language.
 */
void sn2_ptc_deadlock_recovery(unsigned long data0, unsigned long data1)
{
	extern void sn2_ptc_deadlock_recovery_core(long *, long, long *, long,
						   long *);
	int cnode, mycnode, nasid;
	long *ptc0, *ptc1, *piows;

	sn2_ptc_deadlock_count++;

	ptc0 = (long *)GLOBAL_MMR_PHYS_ADDR(0, SH_PTC_0);
	ptc1 = (long *)GLOBAL_MMR_PHYS_ADDR(0, SH_PTC_1);
	piows = (long *)pda->pio_write_status_addr;

	mycnode = numa_node_id();

	for (cnode = 0; cnode < numnodes; cnode++) {
		if (is_headless_node(cnode) || cnode == mycnode)
			continue;
		nasid = cnodeid_to_nasid(cnode);
		ptc0 = CHANGE_NASID(nasid, ptc0);
		ptc1 = CHANGE_NASID(nasid, ptc1);
		sn2_ptc_deadlock_recovery_core(ptc0, data0, ptc1, data1, piows);
	}
}

/**
 * sn_send_IPI_phys - send an IPI to a Nasid and slice
 * @nasid: nasid to receive the interrupt (may be outside partition)
 * @physid: physical cpuid to receive the interrupt.
 * @vector: command to send
 * @delivery_mode: delivery mechanism
 *
 * Sends an IPI (interprocessor interrupt) to the processor specified by
 * @physid
 *
 * @delivery_mode can be one of the following
 *
 * %IA64_IPI_DM_INT - pend an interrupt
 * %IA64_IPI_DM_PMI - pend a PMI
 * %IA64_IPI_DM_NMI - pend an NMI
 * %IA64_IPI_DM_INIT - pend an INIT interrupt
 */
void sn_send_IPI_phys(int nasid, long physid, int vector, int delivery_mode)
{
	long val;
	unsigned long flags = 0;
	volatile long *p;

	p = (long *)GLOBAL_MMR_PHYS_ADDR(nasid, SH_IPI_INT);
	val = (1UL << SH_IPI_INT_SEND_SHFT) |
	    (physid << SH_IPI_INT_PID_SHFT) |
	    ((long)delivery_mode << SH_IPI_INT_TYPE_SHFT) |
	    ((long)vector << SH_IPI_INT_IDX_SHFT) |
	    (0x000feeUL << SH_IPI_INT_BASE_SHFT);

	mb();
	if (enable_shub_wars_1_1()) {
		spin_lock_irqsave(&sn2_global_ptc_lock, flags);
	}
	pio_phys_write_mmr(p, val);
	if (enable_shub_wars_1_1()) {
		wait_piowc();
		spin_unlock_irqrestore(&sn2_global_ptc_lock, flags);
	}

}

EXPORT_SYMBOL(sn_send_IPI_phys);

/**
 * sn2_send_IPI - send an IPI to a processor
 * @cpuid: target of the IPI
 * @vector: command to send
 * @delivery_mode: delivery mechanism
 * @redirect: redirect the IPI?
 *
 * Sends an IPI (InterProcessor Interrupt) to the processor specified by
 * @cpuid.  @vector specifies the command to send, while @delivery_mode can 
 * be one of the following
 *
 * %IA64_IPI_DM_INT - pend an interrupt
 * %IA64_IPI_DM_PMI - pend a PMI
 * %IA64_IPI_DM_NMI - pend an NMI
 * %IA64_IPI_DM_INIT - pend an INIT interrupt
 */
void sn2_send_IPI(int cpuid, int vector, int delivery_mode, int redirect)
{
	long physid;
	int nasid;

	physid = cpu_physical_id(cpuid);
	nasid = cpuid_to_nasid(cpuid);

	/* the following is used only when starting cpus at boot time */
	if (unlikely(nasid == -1))
		ia64_sn_get_sapic_info(physid, &nasid, NULL, NULL);

	sn_send_IPI_phys(nasid, physid, vector, delivery_mode);
}

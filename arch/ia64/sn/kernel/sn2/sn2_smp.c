/*
 * SN2 Platform specific SMP Support
 *
 * Copyright (C) 2000-2002 Silicon Graphics, Inc. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it 
 * under the terms of version 2 of the GNU General Public License 
 * as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it would be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * 
 * Further, this software is distributed without any warranty that it is 
 * free of the rightful claim of any third person regarding infringement 
 * or the like.  Any license provided herein, whether implied or 
 * otherwise, applies only to this software file.  Patent licenses, if 
 * any, provided herein do not apply to combinations of this program with 
 * other software, or any other product whatsoever.
 * 
 * You should have received a copy of the GNU General Public 
 * License along with this program; if not, write the Free Software 
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 * 
 * Contact information:  Silicon Graphics, Inc., 1600 Amphitheatre Pkwy, 
 * Mountain View, CA  94043, or:
 * 
 * http://www.sgi.com 
 * 
 * For further information regarding this notice, see: 
 * 
 * http://oss.sgi.com/projects/GenInfo/NoticeExplan
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

#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/sal.h>
#include <asm/system.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/hw_irq.h>
#include <asm/current.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/addrs.h>
#include <asm/sn/sn2/shub_mmr.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/rw_mmr.h>

void sn2_ptc_deadlock_recovery(unsigned long data0, unsigned long data1);


static spinlock_t sn2_global_ptc_lock __cacheline_aligned = SPIN_LOCK_UNLOCKED;

static unsigned long sn2_ptc_deadlock_count;


static inline unsigned long
wait_piowc(void)
{
	volatile unsigned long *piows;
	unsigned long	ws;

	piows = pda->pio_write_status_addr;
	do {
		__asm__ __volatile__ ("mf.a" ::: "memory");
	} while (((ws = *piows) & SH_PIO_WRITE_STATUS_0_PENDING_WRITE_COUNT_MASK) != 
			SH_PIO_WRITE_STATUS_0_PENDING_WRITE_COUNT_MASK);
	return ws;
}

#ifdef PTCG_WAR
/*
 * The following structure is used to pass params thru smp_call_function
 * to other cpus for flushing TLB ranges.
 */
typedef struct {
	unsigned long	start;
	unsigned long	end;
	unsigned long	nbits;
	unsigned int	rid;
	atomic_t	unfinished_count;
	char		fill[96];
} ptc_params_t;

#define NUMPTC	512

static ptc_params_t	ptcParamArray[NUMPTC] __attribute__((__aligned__(128)));

/* use separate cache lines on ptcParamsNextByCpu to avoid false sharing */
static ptc_params_t	*ptcParamsNextByCpu[NR_CPUS*16] __attribute__((__aligned__(128)));
static volatile ptc_params_t	*ptcParamsEmpty __cacheline_aligned;

/*REFERENCED*/
static spinlock_t ptcParamsLock __cacheline_aligned = SPIN_LOCK_UNLOCKED;

static int ptcInit = 0;
#ifdef PTCDEBUG
static int ptcParamsAllBusy = 0;		/* debugging/statistics */
static int ptcCountBacklog = 0;
static int ptcBacklog[NUMPTC+1];
static char ptcParamsCounts[NR_CPUS][NUMPTC] __attribute__((__aligned__(128)));
static char ptcParamsResults[NR_CPUS][NUMPTC] __attribute__((__aligned__(128)));
#endif

/*
 * Make smp_send_flush_tlbsmp_send_flush_tlb() a weak reference,
 * so that we get a clean compile with the ia64 patch without the
 * actual SN1 specific code in arch/ia64/kernel/smp.c.
 */
extern void smp_send_flush_tlb (void) __attribute((weak));


/**
 * sn1_ptc_l_range - purge local translation cache
 * @start: start of virtual address range
 * @end: end of virtual address range
 * @nbits: specifies number of bytes to purge per instruction (num = 1<<(nbits & 0xfc))
 *
 * Purges the range specified from the local processor's translation cache
 * (as opposed to the translation registers).  Note that more than the specified
 * range *may* be cleared from the cache by some processors.
 *
 * This is probably not good enough, but I don't want to try to make it better 
 * until I get some statistics on a running system. At a minimum, we should only 
 * send IPIs to 1 processor in each TLB domain & have it issue a ptc.g on it's 
 * own FSB. Also, we only have to serialize per FSB, not globally.
 *
 * More likely, we will have to do some work to reduce the frequency of calls to
 * this routine.
 */
static inline void
sn1_ptc_l_range(unsigned long start, unsigned long end, unsigned long nbits)
{
	do {
		__asm__ __volatile__ ("ptc.l %0,%1" :: "r"(start), "r"(nbits<<2) : "memory");
		start += (1UL << nbits);
	} while (start < end);
	ia64_srlz_d();
}

/**
 * sn1_received_flush_tlb - cpu tlb flush routine
 *
 * Flushes the TLB of a given processor.
 */
void
sn1_received_flush_tlb(void)
{
	unsigned long	start, end, nbits;
	unsigned int	rid, saved_rid;
	int		cpu = smp_processor_id();
	int		result;
	ptc_params_t	*ptcParams;

	ptcParams = ptcParamsNextByCpu[cpu*16];
	if (ptcParams == ptcParamsEmpty)
		return;

	do {
		start = ptcParams->start;
		saved_rid = (unsigned int) ia64_get_rr(start);
		end = ptcParams->end;
		nbits = ptcParams->nbits;
		rid = ptcParams->rid;

		if (saved_rid != rid) {
			ia64_set_rr(start, (unsigned long)rid);
			ia64_srlz_d();
		}

		sn1_ptc_l_range(start, end, nbits);

		if (saved_rid != rid) 
			ia64_set_rr(start, (unsigned long)saved_rid);

		ia64_srlz_i();

		result = atomic_dec(&ptcParams->unfinished_count);
#ifdef PTCDEBUG
		{
		    int i = ptcParams-&ptcParamArray[0];
		    ptcParamsResults[cpu][i] = (char) result;
		    ptcParamsCounts[cpu][i]++;
		}
#endif /* PTCDEBUG */

		if (++ptcParams == &ptcParamArray[NUMPTC])
			ptcParams = &ptcParamArray[0];

	} while (ptcParams != ptcParamsEmpty);

	ptcParamsNextByCpu[cpu*16] = ptcParams;
}

/**
 * sn1_global_tlb_purge - flush a translation cache range on all processors
 * @start: start of virtual address range to flush
 * @end: end of virtual address range
 * @nbits: specifies number of bytes to purge per instruction (num = 1<<(nbits & 0xfc))
 *
 * Flushes the translation cache of all processors from @start to @end.
 */
void
sn1_global_tlb_purge (unsigned long start, unsigned long end, unsigned long nbits)
{
	ptc_params_t	*params;
	ptc_params_t	*next;
	unsigned long	irqflags;
#ifdef PTCDEBUG
	ptc_params_t	*nextnext;
	int		backlog = 0;
#endif

	if (smp_num_cpus == 1) {
		sn1_ptc_l_range(start, end, nbits);
		return;
	}

	if (in_interrupt()) {
		/*
		 *  If at interrupt level and cannot get spinlock, 
		 *  then do something useful by flushing own tlbflush queue
		 *  so as to avoid a possible deadlock.
		 */
		while (!spin_trylock(&ptcParamsLock)) {
			local_irq_save(irqflags);
			sn1_received_flush_tlb();
			local_irq_restore(irqflags);
			udelay(10);	/* take it easier on the bus */	
		}
	} else {
		spin_lock(&ptcParamsLock);
	}

	if (!ptcInit) {
		int cpu;
		ptcInit = 1;
		memset(ptcParamArray, 0, sizeof(ptcParamArray));
		ptcParamsEmpty = &ptcParamArray[0];
		for (cpu=0; cpu<NR_CPUS; cpu++)
			ptcParamsNextByCpu[cpu*16] = &ptcParamArray[0];

#ifdef PTCDEBUG
		memset(ptcBacklog, 0, sizeof(ptcBacklog));
		memset(ptcParamsCounts, 0, sizeof(ptcParamsCounts));
		memset(ptcParamsResults, 0, sizeof(ptcParamsResults));
#endif	/* PTCDEBUG */
	}

	params = (ptc_params_t *) ptcParamsEmpty;
	next = (ptc_params_t *) ptcParamsEmpty + 1;
	if (next == &ptcParamArray[NUMPTC])
		next = &ptcParamArray[0];

#ifdef PTCDEBUG
	nextnext = next + 1;
	if (nextnext == &ptcParamArray[NUMPTC])
		nextnext = &ptcParamArray[0];

	if (ptcCountBacklog) {
		/* quick count of backlog */
		ptc_params_t *ptr;

		/* check the current pointer to the beginning */
		ptr = params;
		while(--ptr >= &ptcParamArray[0]) {
			if (atomic_read(&ptr->unfinished_count) == 0)
				break;
			++backlog;
		}

		if (backlog) {
			/* check the end of the array */
			ptr = &ptcParamArray[NUMPTC];
			while (--ptr > params) {
				if (atomic_read(&ptr->unfinished_count) == 0)
					break;
				++backlog;
			}
		}
		ptcBacklog[backlog]++;
	}
#endif	/* PTCDEBUG */

	/* wait for the next entry to clear...should be rare */
	if (atomic_read(&next->unfinished_count) > 0) {
#ifdef PTCDEBUG
		ptcParamsAllBusy++;

		if (atomic_read(&nextnext->unfinished_count) == 0) {
		    if (atomic_read(&next->unfinished_count) > 0) {
			panic("\nnonzero next zero nextnext %lx %lx\n",
			    (long)next, (long)nextnext);
		    }
		}
#endif

		/* it could be this cpu that is behind */
		local_irq_save(irqflags);
		sn1_received_flush_tlb();
		local_irq_restore(irqflags);

		/* now we know it's not this cpu, so just wait */
		while (atomic_read(&next->unfinished_count) > 0) {
			barrier();
		}
	}

	params->start = start;
	params->end = end;
	params->nbits = nbits;
	params->rid = (unsigned int) ia64_get_rr(start);
	atomic_set(&params->unfinished_count, smp_num_cpus);

	/* The atomic_set above can hit memory *after* the update
	 * to ptcParamsEmpty below, which opens a timing window
	 * that other cpus can squeeze into!
	 */
	mb();

	/* everything is ready to process:
	 *	-- global lock is held
	 *	-- new entry + 1 is free
	 *	-- new entry is set up
	 * so now:
	 *	-- update the global next pointer
	 *	-- unlock the global lock
	 *	-- send IPI to notify other cpus
	 *	-- process the data ourselves
	 */
	ptcParamsEmpty = next;
	spin_unlock(&ptcParamsLock);
	smp_send_flush_tlb();

	local_irq_save(irqflags);
	sn1_received_flush_tlb();
	local_irq_restore(irqflags);

	/* 
	 * Since IPIs are polled event (for now), we need to wait til the
	 * TLB flush has started.
	 * wait for the flush to complete 
	 */ 
	while (atomic_read(&params->unfinished_count) > 0)
		barrier();
}

#endif /* PTCG_WAR */


/**
 * sn2_global_tlb_purge - globally purge translation cache of virtual address range
 * @start: start of virtual address range
 * @end: end of virtual address range
 * @nbits: specifies number of bytes to purge per instruction (num = 1<<(nbits & 0xfc))
 *
 * Purges the translation caches of all processors of the given virtual address
 * range.
 */

void
sn2_global_tlb_purge (unsigned long start, unsigned long end, unsigned long nbits)
{
	int			cnode, mycnode, nasid;
	volatile unsigned	long	*ptc0, *ptc1;
	unsigned long		flags=0, data0, data1;

	/*
	 * Special case 1 cpu & 1 node. Use local purges.
	 */
#ifdef PTCG_WAR
	sn1_global_tlb_purge(start, end, nbits);
	return;
#endif /* PTCG_WAR */
		
	data0 = (1UL<<SH_PTC_0_A_SHFT) |
		(nbits<<SH_PTC_0_PS_SHFT) |
		((ia64_get_rr(start)>>8)<<SH_PTC_0_RID_SHFT) |
		(1UL<<SH_PTC_0_START_SHFT);

	ptc0 = (long*)GLOBAL_MMR_PHYS_ADDR(0, SH_PTC_0);
	ptc1 = (long*)GLOBAL_MMR_PHYS_ADDR(0, SH_PTC_1);

	mycnode = local_nodeid;

	/* 
	 * For now, we dont want to spin uninterruptibly waiting
	 * for the lock. Makes hangs hard to debug.
	 */
	local_irq_save(flags);
	while (!spin_trylock(&sn2_global_ptc_lock)) {
		local_irq_restore(flags);
		udelay(1);
		local_irq_save(flags);
	}

	do {
		data1 = start | (1UL<<SH_PTC_1_START_SHFT);
		for (cnode = 0; cnode < numnodes; cnode++) {
			if (is_headless_node(cnode))
				continue;
			if (cnode == mycnode) {
				asm volatile ("ptc.ga %0,%1;;srlz.i;;" :: "r"(start), "r"(nbits<<2) : "memory");
			} else {
				nasid = cnodeid_to_nasid(cnode);
				ptc0 = CHANGE_NASID(nasid, ptc0);
				ptc1 = CHANGE_NASID(nasid, ptc1);
				pio_atomic_phys_write_mmrs(ptc0, data0, ptc1, data1);
			}
		}

		if (wait_piowc() & SH_PIO_WRITE_STATUS_0_WRITE_DEADLOCK_MASK)
			sn2_ptc_deadlock_recovery(data0, data1);

		start += (1UL << nbits);

	} while (start < end);

	spin_unlock_irqrestore(&sn2_global_ptc_lock, flags);

}

/*
 * sn2_ptc_deadlock_recovery
 *
 * Recover from PTC deadlocks conditions. Recovery requires stepping thru each 
 * TLB flush transaction.  The recovery sequence is somewhat tricky & is
 * coded in assembly language.
 */
void
sn2_ptc_deadlock_recovery(unsigned long data0, unsigned long data1)
{
	extern void sn2_ptc_deadlock_recovery_core(long*, long, long*, long, long*);
	int	cnode, mycnode, nasid;
	long	*ptc0, *ptc1, *piows;

	sn2_ptc_deadlock_count++;

	ptc0 = (long*)GLOBAL_MMR_PHYS_ADDR(0, SH_PTC_0);
	ptc1 = (long*)GLOBAL_MMR_PHYS_ADDR(0, SH_PTC_1);
	piows = (long*)pda->pio_write_status_addr;

	mycnode = local_nodeid;

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
void
sn_send_IPI_phys(long physid, int vector, int delivery_mode)
{
	long		nasid, slice;
	long		val;
	volatile long	*p;

#ifdef BUS_INT_WAR
	if (vector != ap_wakeup_vector && delivery_mode == IA64_IPI_DM_INT) {
		return;
	}
#endif

	nasid = cpu_physical_id_to_nasid(physid);
        slice = cpu_physical_id_to_slice(physid);

	p = (long*)GLOBAL_MMR_PHYS_ADDR(nasid, SH_IPI_INT);
	val =   (1UL<<SH_IPI_INT_SEND_SHFT) | 
		(physid<<SH_IPI_INT_PID_SHFT) | 
	        ((long)delivery_mode<<SH_IPI_INT_TYPE_SHFT) | 
		((long)vector<<SH_IPI_INT_IDX_SHFT) |
		(0x000feeUL<<SH_IPI_INT_BASE_SHFT);

	mb();
	pio_phys_write_mmr(p, val);

#ifndef CONFIG_SHUB_1_0_SPECIFIC
	/* doesn't work on shub 1.0 */
	wait_piowc();
#endif
}

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
void
sn2_send_IPI(int cpuid, int vector, int delivery_mode, int redirect)
{
	long		physid;

	physid = cpu_physical_id(cpuid);

	sn_send_IPI_phys(physid, vector, delivery_mode);
}


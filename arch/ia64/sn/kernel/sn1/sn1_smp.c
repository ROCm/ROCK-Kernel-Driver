/*
 * SN1 Platform specific SMP Support
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

#include <linux/config.h>
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
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/hw_irq.h>
#include <asm/current.h>
#include <asm/delay.h>
#include <asm/sn/sn_cpuid.h>

/*
 * The following structure is used to pass params thru smp_call_function
 * to other cpus for flushing TLB ranges.
 */
typedef struct {
	union {
		struct {
			unsigned long	start;
			unsigned long	end;
			unsigned long	nbits;
			unsigned int	rid;
			atomic_t	unfinished_count;
		} ptc;
		char pad[SMP_CACHE_BYTES];
	};
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

/*
 * The following table/struct is for remembering PTC coherency domains. It
 * is also used to translate sapicid into cpuids. We dont want to start 
 * cpus unless we know their cache domain.
 */
#ifdef PTC_NOTYET
sn_sapicid_info_t	sn_sapicid_info[NR_CPUS];
#endif

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
		start = ptcParams->ptc.start;
		saved_rid = (unsigned int) ia64_get_rr(start);
		end = ptcParams->ptc.end;
		nbits = ptcParams->ptc.nbits;
		rid = ptcParams->ptc.rid;

		if (saved_rid != rid) {
			ia64_set_rr(start, (unsigned long)rid);
			ia64_srlz_d();
		}

		sn1_ptc_l_range(start, end, nbits);

		if (saved_rid != rid) 
			ia64_set_rr(start, (unsigned long)saved_rid);

		ia64_srlz_i();

		result = atomic_dec(&ptcParams->ptc.unfinished_count);
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
			if (atomic_read(&ptr->ptc.unfinished_count) == 0)
				break;
			++backlog;
		}

		if (backlog) {
			/* check the end of the array */
			ptr = &ptcParamArray[NUMPTC];
			while (--ptr > params) {
				if (atomic_read(&ptr->ptc.unfinished_count) == 0)
					break;
				++backlog;
			}
		}
		ptcBacklog[backlog]++;
	}
#endif	/* PTCDEBUG */

	/* wait for the next entry to clear...should be rare */
	if (atomic_read(&next->ptc.unfinished_count) > 0) {
#ifdef PTCDEBUG
		ptcParamsAllBusy++;

		if (atomic_read(&nextnext->ptc.unfinished_count) == 0) {
		    if (atomic_read(&next->ptc.unfinished_count) > 0) {
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
		while (atomic_read(&next->ptc.unfinished_count) > 0) {
			barrier();
		}
	}

	params->ptc.start = start;
	params->ptc.end = end;
	params->ptc.nbits = nbits;
	params->ptc.rid = (unsigned int) ia64_get_rr(start);
	atomic_set(&params->ptc.unfinished_count, smp_num_cpus);

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

	/* Currently we don't think global TLB purges need to be atomic.
	 * All CPUs get sent IPIs, so if they haven't done the purge,
	 * they're busy with interrupts that are at the IPI level, which is
	 * priority 15.  We're asserting that any code at that level
	 * shouldn't be using user TLB entries.  To change this to wait
	 * for all the flushes to complete, enable the following code.
	 */
#if defined(SN1_SYNCHRONOUS_GLOBAL_TLB_PURGE) || defined(BUS_INT_WAR)
	/* this code is not tested */
	/* wait for the flush to complete */
	while (atomic_read(&params->ptc.unfinished_count) > 0)
		barrier();
#endif
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
	long		*p;
	long		nasid, slice;

	static int 	off[4] = {0x1800080, 0x1800088, 0x1a00080, 0x1a00088};

#ifdef BUS_INT_WAR
	if (vector != ap_wakeup_vector) {
		return;
	}
#endif

	nasid = cpu_physical_id_to_nasid(physid);
        slice = cpu_physical_id_to_slice(physid);

	p = (long*)(0xc0000a0000000000LL | (nasid<<33) | off[slice]);

	mb();
	*p = (delivery_mode << 8) | (vector & 0xff);
}


/**
 * sn1_send_IPI - send an IPI to a processor
 * @cpuid: target of the IPI
 * @vector: command to send
 * @delivery_mode: delivery mechanism
 * @redirect: redirect the IPI?
 *
 * Sends an IPI (interprocessor interrupt) to the processor specified by
 * @cpuid.  @delivery_mode can be one of the following
 *
 * %IA64_IPI_DM_INT - pend an interrupt
 * %IA64_IPI_DM_PMI - pend a PMI
 * %IA64_IPI_DM_NMI - pend an NMI
 * %IA64_IPI_DM_INIT - pend an INIT interrupt
 */
void
sn1_send_IPI(int cpuid, int vector, int delivery_mode, int redirect)
{
	long		physid;

	physid = cpu_physical_id(cpuid);

	sn_send_IPI_phys(physid, vector, delivery_mode);
}
#ifdef CONFIG_SMP

#ifdef PTC_NOTYET
static void __init
process_sal_ptc_domain_info(ia64_sal_ptc_domain_info_t *di, int domain)
{
	ia64_sal_ptc_domain_proc_entry_t	*pe;
	int 					i, sapicid, cpuid;

	pe = __va(di->proc_list);
	for (i=0; i<di->proc_count; i++, pe++) {
		sapicid = id_eid_to_sapicid(pe->id, pe->eid);
		cpuid = cpu_logical_id(sapicid);
		sn_sapicid_info[cpuid].domain = domain;
		sn_sapicid_info[cpuid].sapicid = sapicid;
	}
}


static void __init
process_sal_desc_ptc(ia64_sal_desc_ptc_t *ptc)
{
	ia64_sal_ptc_domain_info_t	*di;
	int i;

	di = __va(ptc->domain_info);
	for (i=0; i<ptc->num_domains; i++, di++) {
		process_sal_ptc_domain_info(di, i);	
	}
}
#endif /* PTC_NOTYET */

/**
 * init_sn1_smp_config - setup PTC domains per processor
 */
void __init
init_sn1_smp_config(void)
{
	if (!ia64_ptc_domain_info)  {
		printk("SMP: Can't find PTC domain info. Forcing UP mode\n");
		smp_num_cpus = 1;
		return;
	}

#ifdef PTC_NOTYET
	memset (sn_sapicid_info, -1, sizeof(sn_sapicid_info));
	process_sal_desc_ptc(ia64_ptc_domain_info);
#endif
}

#else /* CONFIG_SMP */

void __init
init_sn1_smp_config(void)
{

#ifdef PTC_NOTYET
	sn_sapicid_info[0].sapicid = hard_smp_processor_id();
#endif
}

#endif /* CONFIG_SMP */

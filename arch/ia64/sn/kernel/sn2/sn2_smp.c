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
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/hw_irq.h>
#include <asm/current.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/addrs.h>
#include <asm/sn/sn2/shub_mmr.h>

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
	int		cnode, nasid;
	volatile long	*ptc0, *ptc1, *piows;
	unsigned long	ws, next, data0, data1;

	piows = (long*)LOCAL_MMR_ADDR(get_slice() ? SH_PIO_WRITE_STATUS_1 : SH_PIO_WRITE_STATUS_0);
	data0 = (1UL<<SH_PTC_0_A_SHFT) |
		(nbits<<SH_PTC_0_PS_SHFT) |
		((ia64_get_rr(start)>>8)<<SH_PTC_0_RID_SHFT) |
		(1UL<<SH_PTC_0_START_SHFT);

	ptc0 = (long*)GLOBAL_MMR_ADDR(0, SH_PTC_0);
	ptc1 = (long*)GLOBAL_MMR_ADDR(0, SH_PTC_1);
	do {
		*(piows+1) = -1;	/* use alias address to clear bits*/
		next = start;
		do {
			for (cnode = 0; cnode < numnodes; cnode++) {
				nasid = cnodeid_to_nasid(cnode);
				ptc0 = CHANGE_NASID(nasid, ptc0);
				ptc1 = CHANGE_NASID(nasid, ptc1);
				data1 = next | (1UL<<SH_PTC_1_START_SHFT);
				*ptc0 = data0;
				*ptc1 = data1;
			}
			next += (1UL << nbits);
		} while (next < end);

		while ((ws = *piows) & SH_PIO_WRITE_STATUS_0_PENDING_WRITE_COUNT_MASK)
			;

	} while (ws & SH_PIO_WRITE_STATUS_0_WRITE_DEADLOCK_MASK);

	
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
	long		*p, val;
	long		physid;
	long		nasid, slice;

	physid = cpu_physical_id(cpuid);
	nasid = cpu_physical_id_to_nasid(physid);
        slice = cpu_physical_id_to_slice(physid);

	p = (long*)GLOBAL_MMR_ADDR(nasid, SH_IPI_INT);
	val =   (1UL<<SH_IPI_INT_SEND_SHFT) | 
		(physid<<SH_IPI_INT_PID_SHFT) | 
	        ((long)delivery_mode<<SH_IPI_INT_TYPE_SHFT) | 
		((long)vector<<SH_IPI_INT_IDX_SHFT) |
		(0x000feeUL<<SH_IPI_INT_BASE_SHFT);

#if defined(BRINGUP)
	{
	static int count=0;
	if (count++ < 10) printk("ZZ sendIPI 0x%x->0x%x, vec %d, nasid 0x%lx, slice %ld, adr 0x%lx, val 0x%lx\n",
		smp_processor_id(), cpuid, vector, nasid, slice, (long)p, val);
	}
#endif
	mb();
	*p = val;
	
}

/**
 * init_sn2_smp_config - initialize SN2 smp configuration
 *
 * currently a NOP.
 */
void __init
init_sn2_smp_config(void)
{

}

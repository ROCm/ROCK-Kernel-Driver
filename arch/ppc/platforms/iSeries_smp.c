/*
 * SMP support for iSeries/LPAR.
 *
 * Copyright (C) 2001 IBM Corp.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#define __KERNEL_SYSCALLS__
#include <linux/unistd.h>
#include <linux/init.h>
#include <linux/spinlock.h>

#include <asm/ptrace.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/residual.h>
#include <asm/time.h>

#include <asm/iSeries/LparData.h>
#include <asm/iSeries/HvCall.h>
extern u64 get_tb64(void);
extern u64 next_jiffy_update_tb[];

static unsigned long iSeries_smp_message[NR_CPUS];

void iSeries_smp_message_recv( struct pt_regs * regs )
{
	int cpu = smp_processor_id();
	int msg;

	if ( num_online_cpus() < 2 )
		return;

	for ( msg = 0; msg < 4; ++msg )
		if ( test_and_clear_bit( msg, &iSeries_smp_message[cpu] ) )
			smp_message_recv( msg, regs );

}

static void smp_iSeries_message_pass(int target, int msg, unsigned long data, int wait)
{
	int i;
	for (i = 0; i < NR_CPUS; ++i) {
		if (!cpu_online(i))
			continue;

		if ( (target == MSG_ALL) ||
			(target == i) ||
			((target == MSG_ALL_BUT_SELF) && (i != smp_processor_id())) ) {
			set_bit( msg, &iSeries_smp_message[i] );
			HvCall_sendIPI(&(xPaca[i]));
		}
	}
}

static int smp_iSeries_probe(void)
{
	unsigned i;
	unsigned np;
	struct ItLpPaca * lpPaca;

	np = 0;
	for (i=0; i<maxPacas; ++i) {
		lpPaca = xPaca[i].xLpPacaPtr;
		if ( lpPaca->xDynProcStatus < 2 )
			++np;
	}

	smp_tb_synchronized = 1;
	return np;
}

extern unsigned long decr_overclock;
static void smp_iSeries_kick_cpu(int nr)
{
	struct ItLpPaca * lpPaca;
	// Verify we have a Paca for processor nr
	if ( ( nr <= 0 ) ||
		( nr >= maxPacas ) )
		return;
	// Verify that our partition has a processor nr
	lpPaca = xPaca[nr].xLpPacaPtr;
	if ( lpPaca->xDynProcStatus >= 2 )
		return;
	xPaca[nr].default_decr = tb_ticks_per_jiffy / decr_overclock;
	// The processor is currently spinning, waiting
	// for the xProcStart field to become non-zero
	// After we set xProcStart, the processor will
	// continue on to secondary_start in iSeries_head.S
	xPaca[nr].xProcStart = 1;
}

static void smp_iSeries_setup_cpu(int nr)
{
	set_dec( xPaca[nr].default_decr );
}

static void smp_iSeries_space_timers(unsigned nr)
{
	unsigned offset,i;
	
	offset = tb_ticks_per_jiffy / nr;
	for ( i=1; i<nr; ++i ) {
		next_jiffy_update_tb[i] = next_jiffy_update_tb[i-1] + offset;
	}
}

struct smp_ops_t iSeries_smp_ops = {
   smp_iSeries_message_pass,
   smp_iSeries_probe,
   smp_iSeries_kick_cpu,
   smp_iSeries_setup_cpu,
   smp_iSeries_space_timers,
   .give_timebase = smp_generic_give_timebase,
   .take_timebase = smp_generic_take_timebase,
};


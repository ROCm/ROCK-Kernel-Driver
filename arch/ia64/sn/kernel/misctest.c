/* 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2002 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/timex.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/sn/intr.h>
#include <asm/hw_irq.h>
#include <asm/sn/leds.h>

extern	int autotest_enabled;
long	mcatest=0, debug0, debug1, debug2, debug3;

#define HDELAY(t)	(IS_RUNNING_ON_SIMULATOR() ? udelay(1) : udelay(t))

/* 
 * mcatest
 *	mactest contains a decimal number (RPTT) where
 *		R - flag, if non zero, run forever
 *
 *		P - identifies when to run the test
 *		    0 execute test at cpu 0 early init
 *		    1 execute test at cpu 0 idle
 *		    2 execute test at last (highest numbered) cpu idle
 *		    3 execute test on all cpus at idle
 *
 *		TT- identifies test to run
 *		    01 = MCA via dup TLB dropin
 *		    02 = MCA via garbage address
 *		    03 = lfetch via garbage address
 *		    05 = INIT self
 *		    06 = INIT other cpu
 *		    07 = INIT non-existent cpu
 *		    10 = IPI stress test. Target cpu 0
 *		    11 = IPI stress test. Target all cpus
 *		    12 = TLB stress test
 *		    13 = Park cpu (spinloop)
 *		    14 = One shot TLB test with tlb spinlock
 *		    15 = One shot TLB test
 *		    16 = One shot TLB test sync'ed with RTC
 *		    20 = set led to the cpuid & spin.
 *		    21 = Try mixed cache/uncached refs & see what happens
 *		    22 = Call SAL reboot
 *		    23 = Call PAL halt
 */
static int __init set_mcatest(char *str)
{
	int	val;
	get_option(&str, &val);
	mcatest = val;
	return 1;
}
__setup("mcatest=", set_mcatest);

static int __init set_debug0(char *str)
{
	int	val;
	get_option(&str, &val);
	debug0 = val;
	return 1;
}
__setup("debug0=", set_debug0);

static int __init set_debug1(char *str)
{
	int	val;
	get_option(&str, &val);
	debug1 = val;
	return 1;
}
__setup("debug1=", set_debug1);

static int __init set_debug2(char *str)
{
	int	val;
	get_option(&str, &val);
	debug2 = val;
	return 1;
}
__setup("debug2=", set_debug2);

static int __init set_debug3(char *str)
{
	int	val;
	get_option(&str, &val);
	debug3 = val;
	return 1;
}
__setup("debug3=", set_debug3);

static volatile int	go;

static void
do_sync(int pos) {
	if (pos != 3)
		return;
	else if (smp_processor_id() == 0)
		go = 1;
	else
		while (!go);
}

static void
sgi_mcatest_bkpt(void)
{
}


/*
 * Optional test
 *	pos - 0 called from early init
 *	pos - called when cpu about to go idle (fully initialized
 */
void
sgi_mcatest(int pos)
{
	long	spos, test, repeat;
	int	cpu, curcpu, i, n;

	//if (IS_RUNNING_ON_SIMULATOR()) mcatest=1323;
	repeat = mcatest/1000;
	spos = (mcatest/100)%10;
	test = mcatest % 100;
	curcpu = smp_processor_id();

	if ( mcatest == 0 || !((pos == 0 && spos == 0) ||
		(pos == 1 && spos == 3) ||
	        (pos == 1 && spos == 1 && curcpu == 0) ||
	        (pos == 1 && spos == 2 && curcpu == smp_num_cpus-1)))
		return;
	     
again:
	if (test == 1 || test == 2 || test == 3) {
		void zzzmca(int);
		printk("CPU %d: About to cause unexpected MCA\n", curcpu);
		HDELAY(100000);
		sgi_mcatest_bkpt();
		do_sync(spos);

		zzzmca(test-1);
	
		HDELAY(100000);
	}

	if (test == 4) {
		long    result, adrs[] = {0xe0021000009821e0UL, 0xc0003f3000000000UL, 0xc0000081101c0000UL, 0xc00000180e021004UL,  0xc00000180e022004UL, 0xc00000180e023004UL };
		long    size[] = {1,2,4,8};
		int     r, i, j, k;

		for (k=0; k<2; k++) {
			for (i=0; i<6; i++) {
				for (j=0; j<4; j++) {
					printk("Probing 0x%lx, size %ld\n", adrs[i], size[j]);
					result = -1;
					r = ia64_sn_probe_io_slot (adrs[i], size[j], &result);
					printk("    status %d, val 0x%lx\n", r, result);
					udelay(100000);
				}
			}
		}

	}

	if (test == 5) {
		cpu =  curcpu;
		printk("CPU %d: About to send INIT to self (cpu %d)\n", curcpu, cpu);
		HDELAY(100000);
		sgi_mcatest_bkpt();
		do_sync(spos);

		platform_send_ipi(cpu, 0, IA64_IPI_DM_INIT, 0);
		
		HDELAY(100000);
		printk("CPU %d: Returned from INIT\n", curcpu);
	}

	if (test == 6) {
		cpu =  curcpu ^ 1;
		printk("CPU %d: About to send INIT to other cpu (cpu %d)\n", curcpu, cpu);
		HDELAY(100000);
		sgi_mcatest_bkpt();
		do_sync(spos);

		platform_send_ipi(cpu, 0, IA64_IPI_DM_INIT, 0);

		HDELAY(100000);
		printk("CPU %d: Done\n", curcpu);
	}

	if (test == 7) {
		printk("CPU %d: About to send INIT to non-existent cpu\n", curcpu);
		HDELAY(100000);
		sgi_mcatest_bkpt();
		do_sync(spos);

		sn_send_IPI_phys(0xffff, 0, IA64_IPI_DM_INIT);

		HDELAY(100000);
		printk("CPU %d: Done\n", curcpu);
	}

	if (test == 10) {
		n = IS_RUNNING_ON_SIMULATOR() ? 10 : 10000000;
		cpu = 0;
		printk("CPU %d: IPI stress test. Target cpu 0\n", curcpu);
		HDELAY(100000);
		sgi_mcatest_bkpt();
		do_sync(spos);

		for (i=0; i<n; i++)
			platform_send_ipi(cpu, IA64_IPI_RESCHEDULE, IA64_IPI_DM_INT, 0);

		HDELAY(100000);
		printk("CPU %d: Done\n", curcpu);
	}

	if (test == 11) {
		n = IS_RUNNING_ON_SIMULATOR() ? 100 : 10000000;
		printk("CPU %d: IPI stress test. Target all cpus\n", curcpu);
		HDELAY(100000);
		sgi_mcatest_bkpt();
		do_sync(spos);

		for (i=0; i<n; i++)
			for (cpu=0; cpu<smp_num_cpus; cpu++)
				if (smp_num_cpus > 2 && cpu != curcpu)
					platform_send_ipi(cpu, IA64_IPI_RESCHEDULE, IA64_IPI_DM_INT, 0);

		HDELAY(100000);
		printk("CPU %d: Done\n", curcpu);
	}

	if (test == 12) {
		long adr = 0xe002200000000000UL;
		n = IS_RUNNING_ON_SIMULATOR() ? 1000 : 100000;
		printk("CPU %d: TLB flush stress test\n", curcpu);
		HDELAY(100000);
		sgi_mcatest_bkpt();
		do_sync(spos);

		for (i=0; i<n; i++)
			platform_global_tlb_purge(adr, adr+25*PAGE_SIZE, 14);

		HDELAY(100000);
		printk("CPU %d: Done\n", curcpu);
	}
	
	if (test == 13) {
		printk("CPU %d: Park cpu in spinloop\n", curcpu);
		while(1);
	}
	if (test == 14 || test == 15 || test == 16 || test == 17) {
		long adr = 0xe002200000000000UL;
		static int inited=0;
		if (inited == 0) {
			if (debug0 == 0) debug0 = 1;
			repeat = 1;
			do_sync(spos);
			if (curcpu >= smp_num_cpus-2) {
				printk("Parking cpu %d\n", curcpu);
				local_irq_disable();
				while(1);
			} else {
				printk("Waiting cpu %d\n", curcpu);
				HDELAY(1000000);
			}
			HDELAY(1000000);
			inited = 1;
		}
		if (test == 16 || test == 17) {
			unsigned long t, shift, mask;
			mask =  (smp_num_cpus > 16) ? 0x1f : 0xf;
			shift = 25-debug1;
			do {
				t = get_cycles();
				if (IS_RUNNING_ON_SIMULATOR())
					t = (t>>8);
				else
					t = (t>>shift);
				t = t & mask;
			} while (t == curcpu);
			do {
				t = get_cycles();
				if (IS_RUNNING_ON_SIMULATOR())
					t = (t>>8);
				else
					t = (t>>shift);
				t = t & mask;
			} while (t != curcpu);
		}
		if(debug3) printk("CPU %d: One TLB start\n", curcpu);
		if (test != 17) platform_global_tlb_purge(adr, adr+PAGE_SIZE*debug0, 14);
		if(debug3) printk("CPU %d: One TLB flush done\n", curcpu);
	}
	if (test == 20) {
		local_irq_disable();
		set_led_bits(smp_processor_id(), 0xff);
		while(1);
	}
	if (test == 21) {
		extern long ia64_mca_stack[];
		int		i, n;
		volatile long	*p, *up;
		p = (volatile long*)__imva(ia64_mca_stack);
		up = (volatile long*)(__pa(p) | __IA64_UNCACHED_OFFSET);

		if(!IS_RUNNING_ON_SIMULATOR()) printk("ZZZ get data in cache\n");
		for (n=0, i=0; i<100; i++)
			n += *(p+i);
		if(!IS_RUNNING_ON_SIMULATOR()) printk("ZZZ Make uncached refs to same data\n");
		for (n=0, i=0; i<100; i++)
			n += *(up+i);
		if(!IS_RUNNING_ON_SIMULATOR()) printk("ZZZ dirty the data via cached refs\n");
		for (n=0, i=0; i<100; i++)
			*(p+i) = i;
		if(!IS_RUNNING_ON_SIMULATOR()) printk("ZZZ Make uncached refs to same data\n");
		for (n=0, i=0; i<100; i++)
			n += *(up+i);
		if(!IS_RUNNING_ON_SIMULATOR()) printk("ZZZ Flushing cache\n");
		for (n=0, i=0; i<100; i++)
			ia64_fc((void*)(p+i));
		printk("ZZZ done\n");
	}
	if (test == 21) {
		int i;
		volatile long tb, t[10];
		for (i=0; i<10; i++) {
			tb = debug3+ia64_get_itc();
			sgi_mcatest_bkpt();
			t[i] = ia64_get_itc() - tb;
		}
		for (i=0; i<10; i++) {
			printk("ZZZ NULL  0x%lx\n", t[i]);
		}
		for (i=0; i<10; i++) {
			tb = debug3+ia64_get_itc();
			ia64_pal_call_static(PAL_MC_DRAIN, 0, 0, 0, 0);
			t[i] = ia64_get_itc() - tb;
		}
		for (i=0; i<10; i++) {
			printk("ZZZ DRAIN 0x%lx\n", t[i]);
		}
	}
	if (test == 22) {
		extern void machine_restart(char*);
		printk("ZZZ machine_restart\n");
		machine_restart(0);
	}
	if (test == 23) {
		printk("ZZZ ia64_pal_halt_light\n");
		ia64_pal_halt_light();
	}
	if (repeat)
		goto again;

}

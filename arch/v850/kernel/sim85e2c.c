/*
 * arch/v850/kernel/sim85e2c.c -- Machine-specific stuff for
 *	V850E2 RTL simulator
 *
 *  Copyright (C) 2002  NEC Corporation
 *  Copyright (C) 2002  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/bootmem.h>
#include <linux/irq.h>

#include <asm/atomic.h>
#include <asm/page.h>
#include <asm/machdep.h>

#include "mach.h"

extern void memcons_setup (void);


void __init mach_early_init (void)
{
	extern int panic_timeout;

	/* Don't stop the simulator at `halt' instructions.  */
	NOTHAL = 1;

	/* The sim85e2c simulator tracks `undefined' values, so to make
	   debugging easier, we begin by zeroing out all otherwise
	   undefined registers.  This is not strictly necessary.

	   The registers we zero are:
	       Every GPR except:
	           stack-pointer (r3)
		   task-pointer (r16)
		   our return addr (r31)
	       Every system register (SPR) that we know about except for
	       the PSW (SPR 5), which we zero except for the
	       disable-interrupts bit.
	*/

	/* GPRs */
	asm volatile ("             mov r0, r1 ; mov r0, r2              ");
	asm volatile ("mov r0, r4 ; mov r0, r5 ; mov r0, r6 ; mov r0, r7 ");
	asm volatile ("mov r0, r8 ; mov r0, r9 ; mov r0, r10; mov r0, r11");
	asm volatile ("mov r0, r12; mov r0, r13; mov r0, r14; mov r0, r15");
	asm volatile ("             mov r0, r17; mov r0, r18; mov r0, r19");
	asm volatile ("mov r0, r20; mov r0, r21; mov r0, r22; mov r0, r23");
	asm volatile ("mov r0, r24; mov r0, r25; mov r0, r26; mov r0, r27");
	asm volatile ("mov r0, r28; mov r0, r29; mov r0, r30");

	/* SPRs */
	asm volatile ("ldsr r0, 0;  ldsr r0, 1;  ldsr r0, 2;  ldsr r0, 3");
	asm volatile ("ldsr r0, 4");
	asm volatile ("addi 0x20, r0, r1; ldsr r1, 5"); /* PSW */
	asm volatile ("ldsr r0, 16; ldsr r0, 17; ldsr r0, 18; ldsr r0, 19");
	asm volatile ("ldsr r0, 20");

	/* Turn on the caches.  */
	NA85E2C_CACHE_BTSC
		|= (NA85E2C_CACHE_BTSC_ICM | NA85E2C_CACHE_BTSC_DCM0);
	NA85E2C_BUSM_BHC = 0xFFFF;

	/* Ensure that the simulator halts on a panic, instead of going
	   into an infinite loop inside the panic function.  */
	panic_timeout = -1;
}

void __init mach_setup (char **cmdline)
{
	memcons_setup ();
}

void mach_get_physical_ram (unsigned long *ram_start, unsigned long *ram_len)
{
	/* There are 3 possible areas we can use:
	     IRAM (1MB) is fast for instruction fetches, but slow for data
	     DRAM (1020KB) is fast for data, but slow for instructions
	     ERAM is cached, so should be fast for both insns and data,
	          _but_ currently only supports write-through caching, so
		  writes are slow.
	   Since there's really no area that's good for general kernel
	   use, we use DRAM -- it won't be good for user programs
	   (which will be loaded into kernel allocated memory), but
	   currently we're more concerned with testing the kernel.  */
	*ram_start = DRAM_ADDR;
	*ram_len = R0_RAM_ADDR - DRAM_ADDR;
}

void __init mach_sched_init (struct irqaction *timer_action)
{
	/* The simulator actually cycles through all interrupts
	   periodically.  We just pay attention to IRQ0, which gives us
	   1/64 the rate of the periodic interrupts.  */
	setup_irq (0, timer_action);
}

void mach_gettimeofday (struct timespec *tv)
{
	tv->tv_sec = 0;
	tv->tv_nsec = 0;
}

/* Interrupts */

struct nb85e_intc_irq_init irq_inits[] = {
	{ "IRQ", 0, NUM_MACH_IRQS, 1, 7 },
	{ 0 }
};
struct hw_interrupt_type hw_itypes[1];

/* Initialize interrupts.  */
void __init mach_init_irqs (void)
{
	nb85e_intc_init_irq_types (irq_inits, hw_itypes);
}


void machine_halt (void) __attribute__ ((noreturn));
void machine_halt (void)
{
	SIMFIN = 0;		/* Halt immediately.  */
	for (;;) {}
}

void machine_restart (char *__unused)
{
	machine_halt ();
}

void machine_power_off (void)
{
	machine_halt ();
}

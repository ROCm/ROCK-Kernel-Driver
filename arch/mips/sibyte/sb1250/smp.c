/*
 * Copyright (C) 2001 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>

#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_int.h>

extern irqreturn_t smp_call_function_interrupt(int irq, void *dev,
	struct pt_regs *regs);
extern void smp_tune_scheduling(void);

/*
 * These are routines for dealing with the sb1250 smp capabilities
 * independent of board/firmware
 */

static u64 mailbox_set_regs[] = {
	KSEG1 + A_IMR_CPU0_BASE + R_IMR_MAILBOX_SET_CPU,
	KSEG1 + A_IMR_CPU1_BASE + R_IMR_MAILBOX_SET_CPU
};

static u64 mailbox_clear_regs[] = {
	KSEG1 + A_IMR_CPU0_BASE + R_IMR_MAILBOX_CLR_CPU,
	KSEG1 + A_IMR_CPU1_BASE + R_IMR_MAILBOX_CLR_CPU
};

static u64 mailbox_regs[] = {
	KSEG1 + A_IMR_CPU0_BASE + R_IMR_MAILBOX_CPU,
	KSEG1 + A_IMR_CPU1_BASE + R_IMR_MAILBOX_CPU
};


/*
 * Simple enough; everything is set up, so just poke the appropriate mailbox
 * register, and we should be set
 */
void core_send_ipi(int cpu, unsigned int action)
{
	__raw_writeq((((u64)action)<< 48), mailbox_set_regs[cpu]);
}


void sb1250_smp_finish(void)
{
	extern void sb1_sanitize_tlb(void);

	sb1_sanitize_tlb();
	sb1250_time_init();
}

void sb1250_mailbox_interrupt(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	unsigned int action;

	kstat_this_cpu.irqs[K_INT_MBOX_0]++;
	/* Load the mailbox register to figure out what we're supposed to do */
	action = (__raw_readq(mailbox_regs[cpu]) >> 48) & 0xffff;

	/* Clear the mailbox to clear the interrupt */
	__raw_writeq(((u64)action)<<48, mailbox_clear_regs[cpu]);

	/*
	 * Nothing to do for SMP_RESCHEDULE_YOURSELF; returning from the
	 * interrupt will do the reschedule for us
	 */

	if (action & SMP_CALL_FUNCTION) {
		smp_call_function_interrupt(0, NULL, regs);
	}
}

extern atomic_t cpus_booted;
extern void prom_setup_smp(void);
extern int prom_boot_secondary(int cpu, unsigned long sp, unsigned long gp);

void __init smp_boot_cpus(void)
{
	int cur_cpu = 0;
	int cpu;

	prom_setup_smp();
	init_new_context(current, &init_mm);
	current_thread_info()->cpu = 0;
	cpu_data[0].udelay_val = loops_per_jiffy;
	cpu_data[0].asid_cache = ASID_FIRST_VERSION;
	cpus_clear(cpu_online_map);
	cpu_set(0, cpu_online_map);
	atomic_set(&cpus_booted, 1);  /* Master CPU is already booted... */
	smp_tune_scheduling();

	/*
	 * This loop attempts to compensate for "holes" in the CPU
	 * numbering.  It's overkill, but general.
	 */
	for (cpu = 1; cpu < num_online_cpus(); ) {
		struct task_struct *idle;
		struct pt_regs regs;
		int retval;
		printk("Starting CPU %d... ", cpu);

		/* Spawn a new process normally.  Grab a pointer to
		   its task struct so we can mess with it */
		idle = copy_process(CLONE_VM | CLONE_IDLETASK, 0, &regs, 0,
		                    NULL, NULL);
		if (IS_ERR(idle))
			panic("failed fork for CPU %d", cpu);

		/*
		 * We remove it from the pidhash and the runqueue
		 * once we got the process:
		 */
		init_idle(idle, cpu);

		unhash_process(idle);

		do {
			/* Iterate until we find a CPU that comes up */
			cur_cpu++;
			retval = prom_boot_secondary(cur_cpu,
					    (unsigned long)idle + THREAD_SIZE - 32,
					    (unsigned long)idle);
		} while (!retval && (cur_cpu < NR_CPUS));
		if (retval) {
			cpu++;
		} else {
			panic("CPU discovery disaster");
		}
	}

	/* Wait for everyone to come up */
	while (atomic_read(&cpus_booted) != num_online_cpus());
	smp_threads_ready = 1;
}

/*
 * Copyright (C) 2000, 2001 Broadcom Corporation
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

#include <linux/sched.h>
#include <linux/smp.h>

#include <asm/mipsregs.h>

#include "cfe_api.h"
#include "cfe_error.h"

extern void asmlinkage smp_bootstrap(void);

/* Boot all other cpus in the system, initialize them, and
   bring them into the boot fn */
int prom_boot_secondary(int cpu, unsigned long sp, unsigned long gp)
{
	int retval;
	
	retval = cfe_cpu_start(cpu, &smp_bootstrap, sp, gp, 0);
	if (retval != 0) {
		printk("cfe_start_cpu(%i) returned %i\n" , cpu, retval);
		return 0;
	} else {
		return 1;
	}
}

void prom_init_secondary(void)
{
	/* Set up kseg0 to be cachable coherent */
	clear_c0_config(CONF_CM_CMASK);
	set_c0_config(0x5);

	/* Enable interrupts for lines 0-4 */
	clear_c0_status(0xe000);
	set_c0_status(0x1f01);
}

/*
 * Set up state, return the total number of cpus in the system, including
 * the master
 */
void prom_setup_smp(void)
{
	int i;

	/* Use CFE to find out how many CPUs are available */
	for (i=1; i<NR_CPUS; i++) {
		if (cfe_cpu_stop(i) == 0) {
			CPUMASK_SETB(cpu_online_map, i);
		}
	}
	printk("Detected %i available CPU(s)\n", num_online_cpus());
}

void prom_smp_finish(void)
{
	extern void sb1250_smp_finish(void);
	sb1250_smp_finish();
}

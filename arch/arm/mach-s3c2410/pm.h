/* linux/arch/arm/mach-s3c2410/pm.h
 *
 * Copyright (c) 2004 Simtec Electronics
 *	Written by Ben Dooks, <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* s3c2410_pm_init
 *
 * called from board at initialisation time to setup the power
 * management
*/

extern __init int s3c2410_pm_init(void);

/* configuration for the IRQ mask over sleep */
extern unsigned long s3c_irqwake_intmask;
extern unsigned long s3c_irqwake_eintmask;

/* IRQ masks for IRQs allowed to go to sleep (see irq.c) */
extern unsigned long s3c_irqwake_intallow;
extern unsigned long s3c_irqwake_eintallow;

/* Flags for PM Control */

extern unsigned long s3c_pm_flags;

/* from sleep.S */

extern void s3c2410_cpu_suspend(unsigned long *saveblk);
extern void s3c2410_cpu_resume(void);

extern unsigned long s3c2410_sleep_save_phys;

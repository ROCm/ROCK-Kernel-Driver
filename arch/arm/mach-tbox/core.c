/*
 *  linux/arch/arm/mm/mm-tbox.c
 *
 *  Copyright (C) 1998, 1999, 2000 Phil Blundell
 *  Copyright (C) 1998-1999 Russell King
 *
 *  Extra MM routines for the Tbox architecture
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched.h>

#include <asm/elf.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>

extern unsigned long soft_irq_mask;

static void tbox_mask_irq(unsigned int irq)
{
	__raw_writel(0, INTCONT + (irq << 2));
	soft_irq_mask &= ~(1<<irq);
}

static void tbox_unmask_irq(unsigned int irq)
{
	soft_irq_mask |= (1<<irq);
	__raw_writel(1, INTCONT + (irq << 2));
}
 
static void tbox_init_irq(void)
{
	unsigned int i;

	/* Disable all interrupts initially. */
	for (i = 0; i < NR_IRQS; i++) {
		if (i <= 10 || (i >= 12 && i <= 13)) {
			irq_desc[i].valid	= 1;
			irq_desc[i].probe_ok	= 0;
			irq_desc[i].mask_ack	= tbox_mask_irq;
			irq_desc[i].mask	= tbox_mask_irq;
			irq_desc[i].unmask	= tbox_unmask_irq;
			tbox_mask_irq(i);
		} else {
			irq_desc[i].valid	= 0;
			irq_desc[i].probe_ok	= 0;
		}
	}
}

static struct map_desc tbox_io_desc[] __initdata = {
	/* See hardware.h for details */
	{ IO_BASE,	IO_START,	0x00100000, MT_DEVICE }
};

static void __init tbox_map_io(void)
{
	iotable_init(tbox_io_desc, ARRAY_SIZE(tbox_io_desc));
}

static irqreturn_t
tbox_timer_interrupt (int irq, void *dev_id, struct pt_regs *regs)
{
	/* Clear irq */
	__raw_writel(1, FPGA1CONT + 0xc); 
	__raw_writel(0, FPGA1CONT + 0xc);

	do_timer(regs);

	return IRQ_HANDLED;
}

static struct irqaction tbox_timer_irq = {
	.name		= "TBOX Timer Tick",
	.flags		= SA_INTERRUPT,
	.handler	= tbox_timer_interrupt
};

void __init tbox_init_time(void)
{
	setup_irq(IRQ_TIMER, &timer_irq);
}

MACHINE_START(TBOX, "unknown-TBOX")
	MAINTAINER("Philip Blundell")
	BOOT_MEM(0x80000000, 0x00400000, 0xe0000000)
	MAPIO(tbox_map_io)
	INITIRQ(tbox_init_irq)
	INITIME(tbox_init_time)
MACHINE_END


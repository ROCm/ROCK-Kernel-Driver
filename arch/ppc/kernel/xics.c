/*
 * arch/ppc/kernel/xics.c
 *
 * Copyright 2000 IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/threads.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/prom.h>
#include <asm/io.h>
#include "i8259.h"
#include "xics.h"

void xics_enable_irq(u_int irq);
void xics_disable_irq(u_int irq);
void xics_mask_and_ack_irq(u_int irq);
void xics_end_irq(u_int irq);

struct hw_interrupt_type xics_pic = {
	" XICS     ",
	NULL,
	NULL,
	xics_enable_irq,
	xics_disable_irq,
	xics_mask_and_ack_irq,
	xics_end_irq
};

struct hw_interrupt_type xics_8259_pic = {
	" XICS/8259",
	NULL,
	NULL,
	NULL,
	NULL,
	xics_mask_and_ack_irq,
	NULL
};

#define XICS_IPI		2
#define XICS_IRQ_8259_CASCADE	0x2c
#define XICS_IRQ_OFFSET		16
#define XICS_IRQ_SPURIOUS	0

#define DEFAULT_SERVER		0
#define	DEFAULT_PRIORITY	0

struct xics_ipl {
	union {
		u32	word;
		u8	bytes[4];
	} xirr_poll;
	union {
		u32 word;
		u8	bytes[4];
	} xirr;
	u32	dummy;
	union {
		u32	word;
		u8	bytes[4];
	} qirr;
};

struct xics_info {
	volatile struct xics_ipl *	per_cpu[NR_CPUS];
};

struct xics_info	xics_info;

#define xirr_info(n_cpu)	(xics_info.per_cpu[n_cpu]->xirr.word)
#define cppr_info(n_cpu)	(xics_info.per_cpu[n_cpu]->xirr.bytes[0])
#define poll_info(n_cpu)	(xics_info.per_cpu[n_cpu]->xirr_poll.word)
#define qirr_info(n_cpu)	(xics_info.per_cpu[n_cpu]->qirr.bytes[0])

void
xics_enable_irq(
	u_int	irq
	)
{
	int	status;
	int	call_status;

	irq -= XICS_IRQ_OFFSET;
	if (irq == XICS_IPI)
		return;
	call_status = call_rtas("ibm,set-xive", 3, 1, (ulong*)&status,
				irq, DEFAULT_SERVER, DEFAULT_PRIORITY);
	if( call_status != 0 ) {
		printk("xics_enable_irq: irq=%x: call_rtas failed; retn=%x, status=%x\n",
		       irq, call_status, status);
		return;
	}
}

void
xics_disable_irq(
	u_int	irq
	)
{
	int	status;
	int	call_status;

	irq -= XICS_IRQ_OFFSET;
	call_status = call_rtas("ibm,int-off", 1, 1, (ulong*)&status, irq);
	if( call_status != 0 ) {
		printk("xics_disable_irq: irq=%x: call_rtas failed, retn=%x\n",
		       irq, call_status);
		return;
	}
}

void
xics_end_irq(
	u_int	irq
	)
{
	int cpu = smp_processor_id();

	cppr_info(cpu) = 0; /* actually the value overwritten by ack */
	xirr_info(cpu) = (0xff<<24) | (irq-XICS_IRQ_OFFSET);
}

void
xics_mask_and_ack_irq(
	u_int	irq
	)
{
	int cpu = smp_processor_id();

	if( irq < XICS_IRQ_OFFSET ) {
		i8259_pic.ack(irq);
		xirr_info(cpu) = (0xff<<24) | XICS_IRQ_8259_CASCADE;
	}
	else {
		cppr_info(cpu) = 0xff;
	}
}

int
xics_get_irq(struct pt_regs *regs)
{
	u_int	cpu = smp_processor_id();
	u_int	vec;
	int irq;
  
	vec = xirr_info(cpu);
	/*  (vec >> 24) == old priority */
	vec &= 0x00ffffff;
	/* for sanity, this had better be < NR_IRQS - 16 */
	if( vec == XICS_IRQ_8259_CASCADE )
		irq = i8259_irq(cpu);
	else if( vec == XICS_IRQ_SPURIOUS )
		irq = -1;
	else
		irq = vec + XICS_IRQ_OFFSET;
	return irq;
}

#ifdef CONFIG_SMP
void xics_ipi_action(int irq, void *dev_id, struct pt_regs *regs)
{
	qirr_info(smp_processor_id()) = 0xff;
	smp_message_recv(MSG_RESCHEDULE, regs);
}

void xics_cause_IPI(int cpu)
{
	qirr_info(cpu) = 0;
}

void xics_setup_cpu(void)
{
	int cpu = smp_processor_id();

	cppr_info(cpu) = 0xff;
}
#endif /* CONFIG_SMP */

void
xics_init_IRQ( void )
{
	int i;
	extern unsigned long smp_chrp_cpu_nr;

#ifdef CONFIG_SMP
	for (i = 0; i < smp_chrp_cpu_nr; ++i)
		xics_info.per_cpu[i] =
			ioremap(0xfe000000 + smp_hw_index[i] * 0x1000, 0x20);
#else
	xics_info.per_cpu[0] = ioremap(0xfe000000, 0x20);
#endif /* CONFIG_SMP */
	xics_8259_pic.enable = i8259_pic.enable;
	xics_8259_pic.disable = i8259_pic.disable;
	for (i = 0; i < 16; ++i)
		irq_desc[i].handler = &xics_8259_pic;
	for (; i < NR_IRQS; ++i)
		irq_desc[i].handler = &xics_pic;

	cppr_info(0) = 0xff;
	if (request_irq(XICS_IRQ_8259_CASCADE + XICS_IRQ_OFFSET, no_action,
			0, "8259 cascade", 0))
		printk(KERN_ERR "xics_init_IRQ: couldn't get 8259 cascade\n");
	i8259_init();

#ifdef CONFIG_SMP
	request_irq(XICS_IPI + XICS_IRQ_OFFSET, xics_ipi_action, 0, "IPI", 0);
#endif
}

/* linux/arch/arm/mach-s3c2410/irq.c
 *
 * Copyright (c) 2003 Simtec Electronics
 * Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */


#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/ptrace.h>
#include <linux/sysdev.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <asm/mach/irq.h>

#include <asm/arch/regs-irq.h>
#include <asm/arch/regs-lcd.h>

#if 0
#include <asm/debug-ll.h>
#endif

#define irqdbf(x...)
#define irqdbf2(x...)

static void
s3c_irq_mask(unsigned int irqno)
{
	unsigned long mask;

	irqno -= IRQ_EINT0;

	mask = __raw_readl(S3C2410_INTMSK);
	mask |= 1UL << irqno;
	__raw_writel(mask, S3C2410_INTMSK);
}

static inline void
s3c_irq_ack(unsigned int irqno)
{
	unsigned long bitval = 1UL << (irqno - IRQ_EINT0);

	__raw_writel(bitval, S3C2410_SRCPND);
	__raw_writel(bitval, S3C2410_INTPND);
}

static inline void
s3c_irq_maskack(unsigned int irqno)
{
	unsigned long bitval = 1UL << (irqno - IRQ_EINT0);
	unsigned long mask;

	mask = __raw_readl(S3C2410_INTMSK);
	__raw_writel(mask|bitval, S3C2410_INTMSK);

	__raw_writel(bitval, S3C2410_SRCPND);
	__raw_writel(bitval, S3C2410_INTPND);
}


static void
s3c_irq_unmask(unsigned int irqno)
{
	unsigned long mask;

	if (irqno != IRQ_TIMER4 && irqno != IRQ_EINT8t23)
		irqdbf2("s3c_irq_unmask %d\n", irqno);

	irqno -= IRQ_EINT0;

	mask = __raw_readl(S3C2410_INTMSK);
	mask &= ~(1UL << irqno);
	__raw_writel(mask, S3C2410_INTMSK);
}

static struct irqchip s3c_irq_level_chip = {
	.ack	   = s3c_irq_maskack,
	.mask	   = s3c_irq_mask,
	.unmask	   = s3c_irq_unmask
};

static struct irqchip s3c_irq_chip = {
	.ack	   = s3c_irq_ack,
	.mask	   = s3c_irq_mask,
	.unmask	   = s3c_irq_unmask
};

/* S3C2410_EINTMASK
 * S3C2410_EINTPEND
 */

#define EXTINT_OFF (IRQ_EINT4 - 4)

static void
s3c_irqext_mask(unsigned int irqno)
{
	unsigned long mask;

	irqno -= EXTINT_OFF;

	mask = __raw_readl(S3C2410_EINTMASK);
	mask |= ( 1UL << irqno);
	__raw_writel(mask, S3C2410_EINTMASK);

	if (irqno <= (IRQ_EINT7 - EXTINT_OFF)) {
		/* check to see if all need masking */

		if ((mask & (0xf << 4)) == (0xf << 4)) {
			/* all masked, mask the parent */
			s3c_irq_mask(IRQ_EINT4t7);
		}
	} else {
		/* todo: the same check as above for the rest of the irq regs...*/

	}
}

static void
s3c_irqext_ack(unsigned int irqno)
{
	unsigned long req;
	unsigned long bit;
	unsigned long mask;

	bit = 1UL << (irqno - EXTINT_OFF);


	mask = __raw_readl(S3C2410_EINTMASK);

	__raw_writel(bit, S3C2410_EINTPEND);

	req = __raw_readl(S3C2410_EINTPEND);
	req &= ~mask;

	/* not sure if we should be acking the parent irq... */

	if (irqno <= IRQ_EINT7 ) {
		if ((req & 0xf0) == 0)
			s3c_irq_ack(IRQ_EINT4t7);
	} else {
		if ((req >> 8) == 0)
			s3c_irq_ack(IRQ_EINT8t23);
	}
}

static void
s3c_irqext_unmask(unsigned int irqno)
{
	unsigned long mask;

	irqno -= EXTINT_OFF;

	mask = __raw_readl(S3C2410_EINTMASK);
	mask &= ~( 1UL << irqno);
	__raw_writel(mask, S3C2410_EINTMASK);

	s3c_irq_unmask((irqno <= (IRQ_EINT7 - EXTINT_OFF)) ? IRQ_EINT4t7 : IRQ_EINT8t23);
}

/* todo - put type handler in here */

static int
s3c_irqext_type(unsigned int irq, unsigned int type)
{
	irqdbf("s3c_irqext_type: called for irq %d, type %d\n", irq, type);

	return 0;
}

static struct irqchip s3c_irqext_chip = {
	.mask	    = s3c_irqext_mask,
	.unmask	    = s3c_irqext_unmask,
	.ack	    = s3c_irqext_ack,
	.type	    = s3c_irqext_type
};

/* mask values for the parent registers for each of the interrupt types */

#define INTMSK_UART0	 (1UL << (IRQ_UART0 - IRQ_EINT0))
#define INTMSK_UART1	 (1UL << (IRQ_UART1 - IRQ_EINT0))
#define INTMSK_UART2	 (1UL << (IRQ_UART2 - IRQ_EINT0))
#define INTMSK_ADCPARENT (1UL << (IRQ_ADCPARENT - IRQ_EINT0))
#define INTMSK_LCD	 (1UL << (IRQ_LCD - IRQ_EINT0))

static inline void
s3c_irqsub_mask(unsigned int irqno, unsigned int parentbit,
		int subcheck)
{
	unsigned long mask;
	unsigned long submask;

	submask = __raw_readl(S3C2410_INTSUBMSK);
	mask = __raw_readl(S3C2410_INTMSK);

	submask |= (1UL << (irqno - IRQ_S3CUART_RX0));

	/* check to see if we need to mask the parent IRQ */

	if ((submask  & subcheck) == subcheck) {
		__raw_writel(mask | parentbit, S3C2410_INTMSK);
	}

	/* write back masks */
	__raw_writel(submask, S3C2410_INTSUBMSK);

}

static inline void
s3c_irqsub_unmask(unsigned int irqno, unsigned int parentbit)
{
	unsigned long mask;
	unsigned long submask;

	submask = __raw_readl(S3C2410_INTSUBMSK);
	mask = __raw_readl(S3C2410_INTMSK);

	submask &= ~(1UL << (irqno - IRQ_S3CUART_RX0));
	mask &= ~parentbit;

	/* write back masks */
	__raw_writel(submask, S3C2410_INTSUBMSK);
	__raw_writel(mask, S3C2410_INTMSK);
}


static inline void
s3c_irqsub_maskack(unsigned int irqno, unsigned int parentmask, unsigned int group)
{
	unsigned int bit = 1UL << (irqno - IRQ_S3CUART_RX0);

	s3c_irqsub_mask(irqno, parentmask, group);

	__raw_writel(bit, S3C2410_SUBSRCPND);

	/* only ack parent if we've got all the irqs (seems we must
	 * ack, all and hope that the irq system retriggers ok when
	 * the interrupt goes off again)
	 */

	if (1) {
		__raw_writel(parentmask, S3C2410_SRCPND);
		__raw_writel(parentmask, S3C2410_INTPND);
	}
}


/* UART0 */

static void
s3c_irq_uart0_mask(unsigned int irqno)
{
	s3c_irqsub_mask(irqno, INTMSK_UART0, 7);
}

static void
s3c_irq_uart0_unmask(unsigned int irqno)
{
	s3c_irqsub_unmask(irqno, INTMSK_UART0);
}

static void
s3c_irq_uart0_ack(unsigned int irqno)
{
	s3c_irqsub_maskack(irqno, INTMSK_UART0, 7);
}

static struct irqchip s3c_irq_uart0 = {
	.mask	    = s3c_irq_uart0_mask,
	.unmask	    = s3c_irq_uart0_unmask,
	.ack	    = s3c_irq_uart0_ack,
};

/* UART1 */

static void
s3c_irq_uart1_mask(unsigned int irqno)
{
	s3c_irqsub_mask(irqno, INTMSK_UART1, 7 << 3);
}

static void
s3c_irq_uart1_unmask(unsigned int irqno)
{
	s3c_irqsub_unmask(irqno, INTMSK_UART1);
}

static void
s3c_irq_uart1_ack(unsigned int irqno)
{
	s3c_irqsub_maskack(irqno, INTMSK_UART1, 7 << 3);
}

static struct irqchip s3c_irq_uart1 = {
	.mask	    = s3c_irq_uart1_mask,
	.unmask	    = s3c_irq_uart1_unmask,
	.ack	    = s3c_irq_uart1_ack,
};

/* UART2 */

static void
s3c_irq_uart2_mask(unsigned int irqno)
{
	s3c_irqsub_mask(irqno, INTMSK_UART2, 7 << 6);
}

static void
s3c_irq_uart2_unmask(unsigned int irqno)
{
	s3c_irqsub_unmask(irqno, INTMSK_UART2);
}

static void
s3c_irq_uart2_ack(unsigned int irqno)
{
	s3c_irqsub_maskack(irqno, INTMSK_UART2, 7 << 6);
}

static struct irqchip s3c_irq_uart2 = {
	.mask	    = s3c_irq_uart2_mask,
	.unmask	    = s3c_irq_uart2_unmask,
	.ack	    = s3c_irq_uart2_ack,
};

/* ADC and Touchscreen */

static void
s3c_irq_adc_mask(unsigned int irqno)
{
	s3c_irqsub_mask(irqno, INTMSK_ADCPARENT, 3 << 9);
}

static void
s3c_irq_adc_unmask(unsigned int irqno)
{
	s3c_irqsub_unmask(irqno, INTMSK_ADCPARENT);
}

static void
s3c_irq_adc_ack(unsigned int irqno)
{
	s3c_irqsub_maskack(irqno, INTMSK_ADCPARENT, 3 << 9);
}

static struct irqchip s3c_irq_adc = {
	.mask	    = s3c_irq_adc_mask,
	.unmask	    = s3c_irq_adc_unmask,
	.ack	    = s3c_irq_adc_ack,
};

#if 0
/* LCD (todo) */

static void
s3c_irq_lcd_mask(unsigned int irqno)
{

}

static void
s3c_irq_lcd_unmask(unsigned int irqno)
{

}

static void
s3c_irq_lcd_ack(unsigned int irqno)
{

}

static struct irqchip s3c_irq_lcd = {
	.mask	    = s3c_irq_lcd_mask,
	.unmask	    = s3c_irq_lcd_unmask,
	.ack	    = s3c_irq_lcd_ack,
};
#endif

/* irq demux */


static void s3c_irq_demux_uart(unsigned int start,
			       struct pt_regs *regs)
{
	unsigned int subsrc, submsk;
	unsigned int offset = start - IRQ_S3CUART_RX0;
	struct irqdesc *desc;

	/* read the current pending interrupts, and the mask
	 * for what it is available */

	subsrc = __raw_readl(S3C2410_SUBSRCPND);
	submsk = __raw_readl(S3C2410_INTSUBMSK);

	irqdbf2("s3c_irq_demux_uart: start=%d (%d), subsrc=0x%08x,0x%08x\n",
		start, offset, subsrc, submsk);

	subsrc &= ~submsk;
	subsrc >>= offset;
	subsrc &= 7;

	if (subsrc != 0) {
		desc = irq_desc + start;

		if (subsrc & 1)
			desc->handle(start, desc, regs);

		desc++;

		if (subsrc & 2)
			desc->handle(start+1, desc, regs);

		desc++;

		if (subsrc & 4)
			desc->handle(start+2, desc, regs);
	}
}

/* uart demux entry points */

static void
s3c_irq_demux_uart0(unsigned int irq,
		    struct irqdesc *desc,
		    struct pt_regs *regs)
{
	irq = irq;
	s3c_irq_demux_uart(IRQ_S3CUART_RX0, regs);
}

static void
s3c_irq_demux_uart1(unsigned int irq,
		    struct irqdesc *desc,
		    struct pt_regs *regs)
{
	irq = irq;
	s3c_irq_demux_uart(IRQ_S3CUART_RX1, regs);
}

static void
s3c_irq_demux_uart2(unsigned int irq,
		    struct irqdesc *desc,
		    struct pt_regs *regs)
{
	irq = irq;
	s3c_irq_demux_uart(IRQ_S3CUART_RX2, regs);
}



void __init s3c2410_init_irq(void)
{
	unsigned long pend;
	int irqno;
	int i;

	irqdbf("s3c2410_init_irq: clearing interrupt status flags\n");

	/* first, clear all interrupts pending... */

	for (i = 0; i < 4; i++) {
		pend = __raw_readl(S3C2410_EINTPEND);
		if (pend == 0)
			break;
		__raw_writel(pend, S3C2410_EINTPEND);
		printk("irq: clearing pending ext status %08x\n", (int)pend);
	}

	for (i = 0; i < 4; i++) {
		pend = __raw_readl(S3C2410_INTPND);
		if (pend == 0)
			break;
		__raw_writel(pend, S3C2410_SRCPND);
		__raw_writel(pend, S3C2410_INTPND);
		printk("irq: clearing pending status %08x\n", (int)pend);
	}

	for (i = 0; i < 4; i++) {
		pend = __raw_readl(S3C2410_SUBSRCPND);

		if (pend == 0)
			break;

		printk("irq: clearing subpending status %08x\n", (int)pend);
		__raw_writel(pend, S3C2410_SUBSRCPND);
	}

	/* register the main interrupts */

	irqdbf("s3c2410_init_irq: registering s3c2410 interrupt handlers\n");

	for (irqno = IRQ_EINT0; irqno < IRQ_ADCPARENT; irqno++) {
		/* set all the s3c2410 internal irqs */

		switch (irqno) {

		case IRQ_EINT4t7:
		case IRQ_EINT8t23:
			/* these are already dealt with, so should never
			 * appear */
			break;

			/* deal with the special IRQs (cascaded) */

		case IRQ_UART0:
		case IRQ_UART1:
		case IRQ_UART2:
		case IRQ_LCD:
		case IRQ_ADCPARENT:
			set_irq_chip(irqno, &s3c_irq_level_chip);
			set_irq_handler(irqno, do_level_IRQ);
			break;

		case IRQ_RESERVED6:
		case IRQ_RESERVED24:
			/* no IRQ here */
			break;

		default:
			//irqdbf("registering irq %d (s3c irq)\n", irqno);
			set_irq_chip(irqno, &s3c_irq_chip);
			set_irq_handler(irqno, do_edge_IRQ);
			set_irq_flags(irqno, IRQF_VALID);
		}
	}

	/* setup the cascade irq handlers */

	set_irq_chained_handler(IRQ_UART0, s3c_irq_demux_uart0);
	set_irq_chained_handler(IRQ_UART1, s3c_irq_demux_uart1);
	set_irq_chained_handler(IRQ_UART2, s3c_irq_demux_uart2);
	//set_irq_chained_handler(IRQ_LCD, s3c_irq_demux_);
	//set_irq_chained_handler(IRQ_ADCPARENT, s3c_irq_demux_);


	/* external interrupts */

	for (irqno = IRQ_EINT4; irqno <= IRQ_EINT23; irqno++) {
		irqdbf("registering irq %d (extended s3c irq)\n", irqno);
		set_irq_chip(irqno, &s3c_irqext_chip);
		set_irq_handler(irqno, do_edge_IRQ);
		set_irq_flags(irqno, IRQF_VALID);
	}

	/* register the uart interrupts */

	irqdbf("s3c2410: registering external interrupts\n");

	for (irqno = IRQ_S3CUART_RX0; irqno <= IRQ_S3CUART_ERR0; irqno++) {
		irqdbf("registering irq %d (s3c uart0 irq)\n", irqno);
		set_irq_chip(irqno, &s3c_irq_uart0);
		set_irq_handler(irqno, do_level_IRQ);
		set_irq_flags(irqno, IRQF_VALID);
	}

	for (irqno = IRQ_S3CUART_RX1; irqno <= IRQ_S3CUART_ERR1; irqno++) {
		irqdbf("registering irq %d (s3c uart1 irq)\n", irqno);
		set_irq_chip(irqno, &s3c_irq_uart1);
		set_irq_handler(irqno, do_level_IRQ);
		set_irq_flags(irqno, IRQF_VALID);
	}

	for (irqno = IRQ_S3CUART_RX2; irqno <= IRQ_S3CUART_ERR2; irqno++) {
		irqdbf("registering irq %d (s3c uart2 irq)\n", irqno);
		set_irq_chip(irqno, &s3c_irq_uart2);
		set_irq_handler(irqno, do_level_IRQ);
		set_irq_flags(irqno, IRQF_VALID);
	}

	for (irqno = IRQ_TC; irqno <= IRQ_ADC; irqno++) {
		irqdbf("registering irq %d (s3c adc irq)\n", irqno);
		set_irq_chip(irqno, &s3c_irq_adc);
		set_irq_handler(irqno, do_edge_IRQ);
		set_irq_flags(irqno, IRQF_VALID);
	}

	irqdbf("s3c2410: registered interrupt handlers\n");
}

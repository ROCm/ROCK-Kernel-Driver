/*
 * linux/arch/arm/mach-sa1100/sa1111.c
 *
 * SA1111 support
 *
 * Original code by John Dorsey
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file contains all generic SA1111 support, except for DMA which is
 * provided separately in dma-sa1111.c.
 *
 * All initialization functions provided here are intended to be called
 * from machine specific code with proper arguments when required.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/errno.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/arch/irq.h>

#include "sa1111.h"

/*
 * SA1111  Interrupt support
 */

void sa1111_IRQ_demux( int irq, void *dev_id, struct pt_regs *regs )
{
	int i;
	unsigned long stat0, stat1;

	for(;;) {
		stat0 = INTSTATCLR0, stat1 = INTSTATCLR1;
		if( !stat0 && !stat1 ) break;
		if( stat0 )
			for( i = 0; i < 32; i++ )
				if( stat0 & (1<<i) )
					do_IRQ( SA1111_IRQ(i), regs );

		if( stat1 )
			for( i = 32; i < 55; i++ )
				if( stat1 & (1<<(i-32)) )
					do_IRQ( SA1111_IRQ(i), regs );
	}
}

static struct irqaction sa1111_irq = {
	name:		"SA1111",
	handler:	sa1111_IRQ_demux,
	flags:		SA_INTERRUPT
};

static void sa1111_mask_and_ack_lowirq(unsigned int irq)
{
	unsigned int mask = 1 << (irq - SA1111_IRQ(0));

	// broken hardware: interrupt events are lost if they occur
	// while the interrupts are disabled.
	//INTEN0 &= ~mask;
	INTSTATCLR0 = mask;
}

static void sa1111_mask_and_ack_highirq(unsigned int irq)
{
	unsigned int mask = 1 << (irq - SA1111_IRQ(32));

	//INTEN1 &= ~mask;
	INTSTATCLR1 = mask;
}

static void sa1111_mask_lowirq(unsigned int irq)
{
	//INTEN0 &= ~(1 << (irq - SA1111_IRQ(0)));
}

static void sa1111_mask_highirq(unsigned int irq)
{
	//INTEN1 &= ~(1 << (irq - SA1111_IRQ(32)));
}

static void sa1111_unmask_lowirq(unsigned int irq)
{
	INTEN0 |= 1 << (irq - SA1111_IRQ(0));
}

static void sa1111_unmask_highirq(unsigned int irq)
{
	INTEN1 |= 1 << ((irq - SA1111_IRQ(32)));
}

void __init sa1111_init_irq(int irq_nr)
{
	int irq;

	/* disable all IRQs */
	INTEN0 = 0;
	INTEN1 = 0;

	/*
	 * detect on rising edge.  Note: Feb 2001 Errata for SA1111
	 * specifies that S0ReadyInt and S1ReadyInt should be '1'.
	 */
	INTPOL0 = 0;
	INTPOL1 = 1 << (S0_READY_NINT - SA1111_IRQ(32)) |
		  1 << (S1_READY_NINT - SA1111_IRQ(32));

	/* clear all IRQs */
	INTSTATCLR0 = -1;
	INTSTATCLR1 = -1;

	for (irq = SA1111_IRQ(0); irq <= SA1111_IRQ(26); irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 0;
		irq_desc[irq].mask_ack	= sa1111_mask_and_ack_lowirq;
		irq_desc[irq].mask	= sa1111_mask_lowirq;
		irq_desc[irq].unmask	= sa1111_unmask_lowirq;
	}
	for (irq = SA1111_IRQ(32); irq <= SA1111_IRQ(54); irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 0;
		irq_desc[irq].mask_ack	= sa1111_mask_and_ack_highirq;
		irq_desc[irq].mask	= sa1111_mask_highirq;
		irq_desc[irq].unmask	= sa1111_unmask_highirq;
	}

	/* Register SA1111 interrupt */
	if (irq_nr >= 0)
		setup_arm_irq(irq_nr, &sa1111_irq);
}

/*
 * Probe for a SA1111 chip.
 */

int __init sa1111_probe(void)
{
	unsigned long id = SBI_SKID;
	int ret = -ENODEV;

	if ((id & SKID_ID_MASK) == SKID_SA1111_ID) {
		printk(KERN_INFO "SA-1111 Microprocessor Companion Chip: "
			"silicon revision %lx, metal revision %lx\n",
			(id & SKID_SIREV_MASK)>>4, (id & SKID_MTREV_MASK));
		ret = 0;
	} else {
		printk(KERN_DEBUG "SA-1111 not detected: ID = %08lx\n", id);
	}

	return ret;
}

/*
 * Bring the SA1111 out of reset.  This requires a set procedure:
 *  1. nRESET asserted (by hardware)
 *  2. CLK turned on from SA1110
 *  3. nRESET deasserted
 *  4. VCO turned on, PLL_BYPASS turned off
 *  5. Wait lock time, then assert RCLKEn
 *  7. PCR set to allow clocking of individual functions
 *
 * Until we've done this, the only registers we can access are:
 *   SBI_SKCR
 *   SBI_SMCR
 *   SBI_SKID
 */
void sa1111_wake(void)
{
	/*
	 * First, set up the 3.6864MHz clock on GPIO 27 for the SA-1111:
	 * (SA-1110 Developer's Manual, section 9.1.2.1)
	 */
	GAFR |= GPIO_32_768kHz;
	GPDR |= GPIO_32_768kHz;
	TUCR = TUCR_3_6864MHz;

	/*
	 * Turn VCO on, and disable PLL Bypass.
	 */
	SBI_SKCR &= ~SKCR_VCO_OFF;
	SBI_SKCR |= SKCR_PLL_BYPASS | SKCR_OE_EN;

	/*
	 * Wait lock time.  SA1111 manual _doesn't_
	 * specify a figure for this!  We choose 100us.
	 */
	udelay(100);

	/*
	 * Enable RCLK.  We also ensure that RDYEN is set.
	 */
	SBI_SKCR |= SKCR_RCLKEN | SKCR_RDYEN;

	/*
	 * Wait 14 RCLK cycles for the chip to finish coming out
	 * of reset. (RCLK=24MHz).  This is 590ns.
	 */
	udelay(1);

	/*
	 * Ensure all clocks are initially off.
	 */
	SKPCR = 0;
}

void sa1111_doze(void)
{
	if (SKPCR & SKPCR_UCLKEN) {
		printk("SA1111 doze mode refused\n");
		return;
	}
	SBI_SKCR &= ~SKCR_RCLKEN;
}

/*
 * Configure the SA1111 shared memory controller.
 */
void sa1111_configure_smc(int sdram, unsigned int drac, unsigned int cas_latency)
{
	unsigned int smcr = SMCR_DTIM | SMCR_MBGE | FInsrt(drac, SMCR_DRAC);

	if (cas_latency == 3)
		smcr |= SMCR_CLAT;

	SBI_SMCR = smcr;
}

/*
 * Disable the memory bus request/grant signals on the SA1110 to
 * ensure that we don't receive spurious memory requests.  We set
 * the MBGNT signal false to ensure the SA1111 doesn't own the
 * SDRAM bus.
 */
void __init sa1110_mb_disable(void)
{
	PGSR &= ~GPIO_MBGNT;
	GPCR = GPIO_MBGNT;
	GPDR = (GPDR & ~GPIO_MBREQ) | GPIO_MBGNT;

	GAFR &= ~(GPIO_MBGNT | GPIO_MBREQ);

}

/*
 * If the system is going to use the SA-1111 DMA engines, set up
 * the memory bus request/grant pins.
 */
void __init sa1110_mb_enable(void)
{
	PGSR &= ~GPIO_MBGNT;
	GPCR = GPIO_MBGNT;
	GPDR = (GPDR & ~GPIO_MBREQ) | GPIO_MBGNT;

	GAFR |= (GPIO_MBGNT | GPIO_MBREQ);
	TUCR |= TUCR_MR;
}

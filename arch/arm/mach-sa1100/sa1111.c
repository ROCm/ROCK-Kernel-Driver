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
 * This file contains all generic SA1111 support.
 *
 * All initialization functions provided here are intended to be called
 * from machine specific code with proper arguments when required.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/slab.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>

#include <asm/hardware/sa1111.h>

#include "sa1111.h"

struct sa1111_device *sa1111;

EXPORT_SYMBOL(sa1111);

/*
 * SA1111 interrupt support.  Since clearing an IRQ while there are
 * active IRQs causes the interrupt output to pulse, the upper levels
 * will call us again if there are more interrupts to process.
 */
static void
sa1111_irq_handler(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs)
{
	unsigned int stat0, stat1, i;

	desc->chip->ack(irq);

	stat0 = INTSTATCLR0;
	stat1 = INTSTATCLR1;

	if (stat0 == 0 && stat1 == 0) {
		do_bad_IRQ(irq, desc, regs);
		return;
	}

	for (i = IRQ_SA1111_START; stat0; i++, stat0 >>= 1)
		if (stat0 & 1)
			do_edge_IRQ(i, irq_desc + i, regs);

	for (i = IRQ_SA1111_START + 32; stat1; i++, stat1 >>= 1)
		if (stat1 & 1)
			do_edge_IRQ(i, irq_desc + i, regs);

	/* For level-based interrupts */
	desc->chip->unmask(irq);
}

#define SA1111_IRQMASK_LO(x)	(1 << (x - IRQ_SA1111_START))
#define SA1111_IRQMASK_HI(x)	(1 << (x - IRQ_SA1111_START - 32))

static void sa1111_ack_lowirq(unsigned int irq)
{
	INTSTATCLR0 = SA1111_IRQMASK_LO(irq);
}

static void sa1111_mask_lowirq(unsigned int irq)
{
	INTEN0 &= ~SA1111_IRQMASK_LO(irq);
}

static void sa1111_unmask_lowirq(unsigned int irq)
{
	INTEN0 |= SA1111_IRQMASK_LO(irq);
}

/*
 * Attempt to re-trigger the interrupt.  The SA1111 contains a register
 * (INTSET) which claims to do this.  However, in practice no amount of
 * manipulation of INTEN and INTSET guarantees that the interrupt will
 * be triggered.  In fact, its very difficult, if not impossible to get
 * INTSET to re-trigger the interrupt.
 */
static void sa1111_rerun_lowirq(unsigned int irq)
{
	unsigned int mask = SA1111_IRQMASK_LO(irq);
	int i;

	for (i = 0; i < 8; i++) {
		INTPOL0 ^= mask;
		INTPOL0 ^= mask;
		if (INTSTATCLR1 & mask)
			break;
	}

	if (i == 8)
		printk(KERN_ERR "Danger Will Robinson: failed to "
			"re-trigger IRQ%d\n", irq);
}

static int sa1111_type_lowirq(unsigned int irq, unsigned int flags)
{
	unsigned int mask = SA1111_IRQMASK_LO(irq);

	if (flags == IRQT_PROBE)
		return 0;

	if ((!(flags & __IRQT_RISEDGE) ^ !(flags & __IRQT_FALEDGE)) == 0)
		return -EINVAL;

	if (flags & __IRQT_RISEDGE)
		INTPOL0 &= ~mask;
	else
		INTPOL0 |= mask;

	return 0;
}

static struct irqchip sa1111_low_chip = {
	ack:		sa1111_ack_lowirq,
	mask:		sa1111_mask_lowirq,
	unmask:		sa1111_unmask_lowirq,
	rerun:		sa1111_rerun_lowirq,
	type:		sa1111_type_lowirq,
};

static void sa1111_ack_highirq(unsigned int irq)
{
	INTSTATCLR1 = SA1111_IRQMASK_HI(irq);
}

static void sa1111_mask_highirq(unsigned int irq)
{
	INTEN1 &= ~SA1111_IRQMASK_HI(irq);
}

static void sa1111_unmask_highirq(unsigned int irq)
{
	INTEN1 |= SA1111_IRQMASK_HI(irq);
}

/*
 * Attempt to re-trigger the interrupt.  The SA1111 contains a register
 * (INTSET) which claims to do this.  However, in practice no amount of
 * manipulation of INTEN and INTSET guarantees that the interrupt will
 * be triggered.  In fact, its very difficult, if not impossible to get
 * INTSET to re-trigger the interrupt.
 */
static void sa1111_rerun_highirq(unsigned int irq)
{
	unsigned int mask = SA1111_IRQMASK_HI(irq);
	int i;

	for (i = 0; i < 8; i++) {
		INTPOL1 ^= mask;
		INTPOL1 ^= mask;
		if (INTSTATCLR1 & mask)
			break;
	}

	if (i == 8)
		printk(KERN_ERR "Danger Will Robinson: failed to "
			"re-trigger IRQ%d\n", irq);
}

static int sa1111_type_highirq(unsigned int irq, unsigned int flags)
{
	unsigned int mask = SA1111_IRQMASK_HI(irq);

	if (flags == IRQT_PROBE)
		return 0;

	if ((!(flags & __IRQT_RISEDGE) ^ !(flags & __IRQT_FALEDGE)) == 0)
		return -EINVAL;

	if (flags & __IRQT_RISEDGE)
		INTPOL1 &= ~mask;
	else
		INTPOL1 |= mask;

	return 0;
}

static struct irqchip sa1111_high_chip = {
	ack:		sa1111_ack_highirq,
	mask:		sa1111_mask_highirq,
	unmask:		sa1111_unmask_highirq,
	rerun:		sa1111_rerun_highirq,
	type:		sa1111_type_highirq,
};

static void __init sa1111_init_irq(int irq_nr)
{
	unsigned int irq;

	request_mem_region(_INTTEST0, 512, "irqs");

	/* disable all IRQs */
	INTEN0 = 0;
	INTEN1 = 0;

	/*
	 * detect on rising edge.  Note: Feb 2001 Errata for SA1111
	 * specifies that S0ReadyInt and S1ReadyInt should be '1'.
	 */
	INTPOL0 = 0;
	INTPOL1 = SA1111_IRQMASK_HI(S0_READY_NINT) |
		  SA1111_IRQMASK_HI(S1_READY_NINT);

	/* clear all IRQs */
	INTSTATCLR0 = ~0;
	INTSTATCLR1 = ~0;

	for (irq = IRQ_GPAIN0; irq <= SSPROR; irq++) {
		set_irq_chip(irq, &sa1111_low_chip);
		set_irq_handler(irq, do_edge_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	for (irq = AUDXMTDMADONEA; irq <= S1_BVD1_STSCHG; irq++) {
		set_irq_chip(irq, &sa1111_high_chip);
		set_irq_handler(irq, do_edge_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	/*
	 * Register SA1111 interrupt
	 */
	set_irq_type(irq_nr, IRQT_RISING);
	set_irq_chained_handler(irq_nr, sa1111_irq_handler);
}

static int sa1111_suspend(struct device *dev, u32 state, u32 level)
{
	return 0;
}

static int sa1111_resume(struct device *dev, u32 level)
{
	return 0;
}

static struct device_driver sa1111_device_driver = {
	suspend:	sa1111_suspend,
	resume:		sa1111_resume,
};

/**
 *	sa1111_probe - probe for a single SA1111 chip.
 *	@phys_addr: physical address of device.
 *
 *	Probe for a SA1111 chip.  This must be called
 *	before any other SA1111-specific code.
 *
 *	Returns:
 *	%-ENODEV	device not found.
 *	%-EBUSY		physical address already marked in-use.
 *	%0		successful.
 */
static int __init
sa1111_probe(struct device *parent, unsigned long phys_addr)
{
	struct sa1111_device *sa;
	unsigned long id;
	int ret = -ENODEV;

	sa = kmalloc(sizeof(struct sa1111_device), GFP_KERNEL);
	if (!sa)
		return -ENOMEM;

	memset(sa, 0, sizeof(struct sa1111_device));

	sa->resource.name  = "SA1111";
	sa->resource.start = phys_addr;
	sa->resource.end   = phys_addr + 0x2000;

	if (request_resource(&iomem_resource, &sa->resource)) {
		ret = -EBUSY;
		goto out;
	}

	/* eventually ioremap... */
	sa->base = (void *)0xf4000000;
	if (!sa->base) {
		ret = -ENOMEM;
		goto release;
	}

	/*
	 * Probe for the chip.  Only touch the SBI registers.
	 */
	id = readl(sa->base + SA1111_SKID);
	if ((id & SKID_ID_MASK) != SKID_SA1111_ID) {
		printk(KERN_DEBUG "SA1111 not detected: ID = %08lx\n", id);
		ret = -ENODEV;
		goto unmap;
	}

	/*
	 * We found the chip.
	 */
	strcpy(sa->dev.name, "SA1111");
	sprintf(sa->dev.bus_id, "%8.8lx", phys_addr);
	sa->dev.parent = parent;
	sa->dev.driver = &sa1111_device_driver;

	ret = device_register(&sa->dev);
	if (ret)
		printk("sa1111 device_register failed: %d\n", ret);

	printk(KERN_INFO "SA1111 Microprocessor Companion Chip: "
		"silicon revision %lx, metal revision %lx\n",
		(id & SKID_SIREV_MASK)>>4, (id & SKID_MTREV_MASK));

	sa1111 = sa;

	return 0;

 unmap:
//	iounmap(sa->base);
 release:
	release_resource(&sa->resource);
 out:
	kfree(sa);
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
	struct sa1111_device *sa = sa1111;
	unsigned long flags, r;

	local_irq_save(flags);

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
	r = readl(sa->base + SA1111_SKCR);
	r &= ~SKCR_VCO_OFF;
	writel(r, sa->base + SA1111_SKCR);
	r |= SKCR_PLL_BYPASS | SKCR_OE_EN;
	writel(r, sa->base + SA1111_SKCR);

	/*
	 * Wait lock time.  SA1111 manual _doesn't_
	 * specify a figure for this!  We choose 100us.
	 */
	udelay(100);

	/*
	 * Enable RCLK.  We also ensure that RDYEN is set.
	 */
	r |= SKCR_RCLKEN | SKCR_RDYEN;
	writel(r, sa->base + SA1111_SKCR);

	/*
	 * Wait 14 RCLK cycles for the chip to finish coming out
	 * of reset. (RCLK=24MHz).  This is 590ns.
	 */
	udelay(1);

	/*
	 * Ensure all clocks are initially off.
	 */
	writel(0, sa->base + SA1111_SKPCR);

	local_irq_restore(flags);
}

void sa1111_doze(void)
{
	struct sa1111_device *sa = sa1111;
	unsigned long flags;

	local_irq_save(flags);

	if (readl(sa->base + SA1111_SKPCR) & SKPCR_UCLKEN) {
		local_irq_restore(flags);
		printk("SA1111 doze mode refused\n");
		return;
	}

	writel(readl(sa->base + SA1111_SKCR) & ~SKCR_RCLKEN, sa->base + SA1111_SKCR);
	local_irq_restore(flags);
}

/*
 * Configure the SA1111 shared memory controller.
 */
void sa1111_configure_smc(int sdram, unsigned int drac, unsigned int cas_latency)
{
	struct sa1111_device *sa = sa1111;
	unsigned int smcr = SMCR_DTIM | SMCR_MBGE | FInsrt(drac, SMCR_DRAC);

	if (cas_latency == 3)
		smcr |= SMCR_CLAT;

	writel(smcr, sa->base + SA1111_SMCR);
}

/* According to the "Intel StrongARM SA-1111 Microprocessor Companion
 * Chip Specification Update" (June 2000), erratum #7, there is a
 * significant bug in Serial Audio Controller DMA. If the SAC is
 * accessing a region of memory above 1MB relative to the bank base,
 * it is important that address bit 10 _NOT_ be asserted. Depending
 * on the configuration of the RAM, bit 10 may correspond to one
 * of several different (processor-relative) address bits.
 *
 * This routine only identifies whether or not a given DMA address
 * is susceptible to the bug.
 */
int sa1111_check_dma_bug(dma_addr_t addr)
{
	unsigned int physaddr=SA1111_DMA_ADDR((unsigned int)addr);

	/* Section 4.6 of the "Intel StrongARM SA-1111 Development Module
	 * User's Guide" mentions that jumpers R51 and R52 control the
	 * target of SA-1111 DMA (either SDRAM bank 0 on Assabet, or
	 * SDRAM bank 1 on Neponset). The default configuration selects
	 * Assabet, so any address in bank 1 is necessarily invalid.
	 */
	if ((machine_is_assabet() || machine_is_pfs168()) && addr >= 0xc8000000)
	  	return -1;

	/* The bug only applies to buffers located more than one megabyte
	 * above the start of the target bank:
	 */
	if (physaddr<(1<<20))
	  	return 0;

	switch (FExtr(SBI_SMCR, SMCR_DRAC)) {
	case 01: /* 10 row + bank address bits, A<20> must not be set */
	  	if (physaddr & (1<<20))
		  	return -1;
		break;
	case 02: /* 11 row + bank address bits, A<23> must not be set */
	  	if (physaddr & (1<<23))
		  	return -1;
		break;
	case 03: /* 12 row + bank address bits, A<24> must not be set */
	  	if (physaddr & (1<<24))
		  	return -1;
		break;
	case 04: /* 13 row + bank address bits, A<25> must not be set */
	  	if (physaddr & (1<<25))
		  	return -1;
		break;
	case 05: /* 14 row + bank address bits, A<20> must not be set */
	  	if (physaddr & (1<<20))
		  	return -1;
		break;
	case 06: /* 15 row + bank address bits, A<20> must not be set */
	  	if (physaddr & (1<<20))
		  	return -1;
		break;
	default:
	  	printk(KERN_ERR "%s(): invalid SMCR DRAC value 0%lo\n",
		       __FUNCTION__, FExtr(SBI_SMCR, SMCR_DRAC));
		return -1;
	}

	return 0;
}

EXPORT_SYMBOL(sa1111_check_dma_bug);

int sa1111_init(struct device *parent, unsigned long phys, unsigned int irq)
{
	int ret;

	ret = sa1111_probe(parent, phys);
	if (ret < 0)
		return ret;

	/*
	 * We found it.  Wake the chip up.
	 */
	sa1111_wake();

	/*
	 * The SDRAM configuration of the SA1110 and the SA1111 must
	 * match.  This is very important to ensure that SA1111 accesses
	 * don't corrupt the SDRAM.  Note that this ungates the SA1111's
	 * MBGNT signal, so we must have called sa1110_mb_disable()
	 * beforehand.
	 */
	sa1111_configure_smc(1,
			     FExtr(MDCNFG, MDCNFG_SA1110_DRAC0),
			     FExtr(MDCNFG, MDCNFG_SA1110_TDL0));

	/*
	 * We only need to turn on DCLK whenever we want to use the
	 * DMA.  It can otherwise be held firmly in the off position.
	 */
	SKPCR |= SKPCR_DCLKEN;

	/*
	 * Enable the SA1110 memory bus request and grant signals.
	 */
	sa1110_mb_enable();

	/*
	 * Initialise SA1111 IRQs
	 */
	sa1111_init_irq(irq);

	return 0;
}

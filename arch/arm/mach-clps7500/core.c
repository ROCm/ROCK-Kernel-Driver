/*
 *  linux/arch/arm/mm/mm-cl7500.c
 *
 *  Copyright (C) 1998 Russell King
 *  Copyright (C) 1999 Nexus Electronics Ltd
 *
 * Extra MM routines for CL7500 architecture
 */
#include <linux/types.h>
#include <linux/init.h>

#include <asm/mach/map.h>

#include <asm/hardware.h>
#include <asm/hardware/iomd.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/proc/domain.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

static void cl7500_mask_irq_ack_a(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << irq;
	val = iomd_readb(IOMD_IRQMASKA);
	iomd_writeb(val & ~mask, IOMD_IRQMASKA);
	iomd_writeb(mask, IOMD_IRQCLRA);
}

static void cl7500_mask_irq_a(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << irq;
	val = iomd_readb(IOMD_IRQMASKA);
	iomd_writeb(val & ~mask, IOMD_IRQMASKA);
}

static void cl7500_unmask_irq_a(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << irq;
	val = iomd_readb(IOMD_IRQMASKA);
	iomd_writeb(val | mask, IOMD_IRQMASKA);
}

static void cl7500_mask_irq_b(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_IRQMASKB);
	iomd_writeb(val & ~mask, IOMD_IRQMASKB);
}

static void cl7500_unmask_irq_b(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_IRQMASKB);
	iomd_writeb(val | mask, IOMD_IRQMASKB);
}

static void cl7500_mask_irq_c(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_IRQMASKC);
	iomd_writeb(val & ~mask, IOMD_IRQMASKC);
}

static void cl7500_unmask_irq_c(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_IRQMASKC);
	iomd_writeb(val | mask, IOMD_IRQMASKC);
}


static void cl7500_mask_irq_d(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_IRQMASKD);
	iomd_writeb(val & ~mask, IOMD_IRQMASKD);
}

static void cl7500_unmask_irq_d(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_IRQMASKD);
	iomd_writeb(val | mask, IOMD_IRQMASKD);
}

static void cl7500_mask_irq_dma(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_DMAMASK);
	iomd_writeb(val & ~mask, IOMD_DMAMASK);
}

static void cl7500_unmask_irq_dma(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_DMAMASK);
	iomd_writeb(val | mask, IOMD_DMAMASK);
}

static void cl7500_mask_irq_fiq(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_FIQMASK);
	iomd_writeb(val & ~mask, IOMD_FIQMASK);
}

static void cl7500_unmask_irq_fiq(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_FIQMASK);
	iomd_writeb(val | mask, IOMD_FIQMASK);
}

static void no_action(int cpl, void *dev_id, struct pt_regs *regs)
{
}

static struct irqaction irq_isa = { no_action, 0, 0, "isa", NULL, NULL };

static void __init clps7500_init_irq(void)
{
	int irq;

	iomd_writeb(0, IOMD_IRQMASKA);
	iomd_writeb(0, IOMD_IRQMASKB);
	iomd_writeb(0, IOMD_FIQMASK);
	iomd_writeb(0, IOMD_DMAMASK);

	for (irq = 0; irq < NR_IRQS; irq++) {
		switch (irq) {
		case 0 ... 6:
			irq_desc[irq].probe_ok = 1;
		case 7:
			irq_desc[irq].valid    = 1;
			irq_desc[irq].mask_ack = cl7500_mask_irq_ack_a;
			irq_desc[irq].mask     = cl7500_mask_irq_a;
			irq_desc[irq].unmask   = cl7500_unmask_irq_a;
			break;

		case 9 ... 15:
			irq_desc[irq].probe_ok = 1;
		case 8:
			irq_desc[irq].valid    = 1;
			irq_desc[irq].mask_ack = cl7500_mask_irq_b;
			irq_desc[irq].mask     = cl7500_mask_irq_b;
			irq_desc[irq].unmask   = cl7500_unmask_irq_b;
			break;

		case 16 ... 22:
			irq_desc[irq].valid    = 1;
			irq_desc[irq].mask_ack = cl7500_mask_irq_dma;
			irq_desc[irq].mask     = cl7500_mask_irq_dma;
			irq_desc[irq].unmask   = cl7500_unmask_irq_dma;
			break;

		case 24 ... 31:
			irq_desc[irq].valid    = 1;
			irq_desc[irq].mask_ack = cl7500_mask_irq_c;
			irq_desc[irq].mask     = cl7500_mask_irq_c;
			irq_desc[irq].unmask   = cl7500_unmask_irq_c;
			break;

		case 40 ... 47:
			irq_desc[irq].valid    = 1;
			irq_desc[irq].mask_ack = cl7500_mask_irq_d;
			irq_desc[irq].mask     = cl7500_mask_irq_d;
			irq_desc[irq].unmask   = cl7500_unmask_irq_d;
			break;

		case 48 ... 55:
			irq_desc[irq].valid      = 1;
			irq_desc[irq].probe_ok   = 1;
			irq_desc[irq].mask_ack   = no_action;
			irq_desc[irq].mask       = no_action;
			irq_desc[irq].unmask     = no_action;
			break;

		case 64 ... 72:
			irq_desc[irq].valid    = 1;
			irq_desc[irq].mask_ack = cl7500_mask_irq_fiq;
			irq_desc[irq].mask     = cl7500_mask_irq_fiq;
			irq_desc[irq].unmask   = cl7500_unmask_irq_fiq;
			break;
		}
	}

	setup_arm_irq(IRQ_ISA, &irq_isa);
}

static struct map_desc cl7500_io_desc[] __initdata = {
	{ IO_BASE,	IO_START,	IO_SIZE	 , DOMAIN_IO, 0, 1 },	/* IO space	*/
	{ ISA_BASE,	ISA_START,	ISA_SIZE , DOMAIN_IO, 0, 1 },	/* ISA space	*/
	{ FLASH_BASE,	FLASH_START,	FLASH_SIZE, DOMAIN_IO, 0, 1 },	/* Flash	*/
	{ LED_BASE,	LED_START,	LED_SIZE , DOMAIN_IO, 0, 1 },	/* LED		*/
	LAST_DESC
};

static void __init clps7500_map_io(void)
{
	iotable_init(cl7500_io_desc);
}

MACHINE_START(CLPS7500, "CL-PS7500")
	MAINTAINER("Philip Blundell")
	BOOT_MEM(0x10000000, 0x03000000, 0xe0000000)
	MAPIO(clps7500_map_io)
	INITIRQ(clps7500_init_irq)
MACHINE_END


/*
 * linux/arch/arm/mach-sa1100/graphicsmaster.c
 *
 * Pieces specific to the GraphicsMaster board
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>

#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/irq.h>

#include <asm/mach/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include <asm/arch/irq.h>

#include "generic.h"
#include "sa1111.h"

static int __init graphicsmaster_init(void)
{
	int ret;

	if (!machine_is_graphicsmaster())
		return -ENODEV;

	/*
	 * Ensure that the memory bus request/grant signals are setup,
	 * and the grant is held in its inactive state
	 */
	sa1110_mb_disable();

	/*
	 * Probe for SA1111.
	 */
	ret = sa1111_probe();
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
	 * Enable PWM control for LCD
	 */
	SKPCR |= SKPCR_PWMCLKEN;
	SKPWM0 = 0x7F;				// VEE
	SKPEN0 = 1;
	SKPWM1 = 0x01;				// Backlight
	SKPEN1 = 1;

	/*
	 * We only need to turn on DCLK whenever we want to use the
	 * DMA.  It can otherwise be held firmly in the off position.
	 */
	SKPCR |= SKPCR_DCLKEN;

	/*
	 * Enable the SA1110 memory bus request and grant signals.
	 */
	sa1110_mb_enable();

	sa1111_init_irq(ADS_EXT_IRQ(0));

	return 0;
}

__initcall(graphicsmaster_init);

/*
 * Handlers for GraphicsMaster's external IRQ logic
 */

static void ADS_IRQ_demux( int irq, void *dev_id, struct pt_regs *regs )
{
	int i;

	while( (irq = ADS_INT_ST1 | (ADS_INT_ST2 << 8)) ){
		for( i = 0; i < 16; i++ )
			if( irq & (1<<i) ) {
				do_IRQ( ADS_EXT_IRQ(i), regs );
			}
	}
}

static struct irqaction ADS_ext_irq = {
	name:		"ADS_ext_IRQ",
	handler:	ADS_IRQ_demux,
	flags:		SA_INTERRUPT
};

static void ADS_mask_and_ack_irq0(unsigned int irq)
{
	int mask = (1 << (irq - ADS_EXT_IRQ(0)));
	ADS_INT_EN1 &= ~mask;
	ADS_INT_ST1 = mask;
}

static void ADS_mask_irq0(unsigned int irq)
{
	ADS_INT_ST1 = (1 << (irq - ADS_EXT_IRQ(0)));
}

static void ADS_unmask_irq0(unsigned int irq)
{
	ADS_INT_EN1 |= (1 << (irq - ADS_EXT_IRQ(0)));
}

static void ADS_mask_and_ack_irq1(unsigned int irq)
{
	int mask = (1 << (irq - ADS_EXT_IRQ(8)));
	ADS_INT_EN2 &= ~mask;
	ADS_INT_ST2 = mask;
}

static void ADS_mask_irq1(unsigned int irq)
{
	ADS_INT_ST2 = (1 << (irq - ADS_EXT_IRQ(8)));
}

static void ADS_unmask_irq1(unsigned int irq)
{
	ADS_INT_EN2 |= (1 << (irq - ADS_EXT_IRQ(8)));
}

static void __init graphicsmaster_init_irq(void)
{
	int irq;

	/* First the standard SA1100 IRQs */
	sa1100_init_irq();

	/* disable all IRQs */
	ADS_INT_EN1 = 0;
	ADS_INT_EN2 = 0;
	/* clear all IRQs */
	ADS_INT_ST1 = 0xff;
	ADS_INT_ST2 = 0xff;

	for (irq = ADS_EXT_IRQ(0); irq <= ADS_EXT_IRQ(7); irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
		irq_desc[irq].mask_ack	= ADS_mask_and_ack_irq0;
		irq_desc[irq].mask	= ADS_mask_irq0;
		irq_desc[irq].unmask	= ADS_unmask_irq0;
	}
	for (irq = ADS_EXT_IRQ(8); irq <= ADS_EXT_IRQ(15); irq++) {
		irq_desc[irq].valid	= 1;
		irq_desc[irq].probe_ok	= 1;
		irq_desc[irq].mask_ack	= ADS_mask_and_ack_irq1;
		irq_desc[irq].mask	= ADS_mask_irq1;
		irq_desc[irq].unmask	= ADS_unmask_irq1;
	}
	GPDR &= ~GPIO_GPIO0;
	set_GPIO_IRQ_edge(GPIO_GPIO0, GPIO_FALLING_EDGE);
	setup_arm_irq( IRQ_GPIO0, &ADS_ext_irq );
}


/*
 * Initialization fixup
 */

static void __init
fixup_graphicsmaster(struct machine_desc *desc, struct param_struct *params,
		     char **cmdline, struct meminfo *mi)
{
	SET_BANK( 0, 0xc0000000, 16*1024*1024 );
	mi->nr_banks = 1;
	SET_BANK( 1, 0xc8000000, 16*1024*1024 );
	mi->nr_banks = 2;

	ROOT_DEV = MKDEV(RAMDISK_MAJOR,0);
	setup_ramdisk( 1, 0, 0, 8192 );
	setup_initrd( __phys_to_virt(0xc0800000), 4*1024*1024 );
}

static struct map_desc graphicsmaster_io_desc[] __initdata = {
 /* virtual     physical    length      domain     r  w  c  b */
  { 0xe8000000, 0x08000000, 0x02000000, DOMAIN_IO, 1, 1, 0, 0 }, /* Flash bank 1 */
  { 0xf0000000, 0x10000000, 0x00400000, DOMAIN_IO, 1, 1, 0, 0 }, /* CPLD */
  { 0xf1000000, 0x40000000, 0x00400000, DOMAIN_IO, 1, 1, 0, 0 }, /* CAN */
  { 0xf4000000, 0x18000000, 0x00800000, DOMAIN_IO, 1, 1, 0, 0 }, /* SA-1111 */
  LAST_DESC
};

static void __init graphicsmaster_map_io(void)
{
	sa1100_map_io();
	iotable_init(graphicsmaster_io_desc);

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
	sa1100_register_uart(2, 2);
}

MACHINE_START(GRAPHICSMASTER, "ADS GraphicsMaster")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	FIXUP(fixup_graphicsmaster)
	MAPIO(graphicsmaster_map_io)
	INITIRQ(graphicsmaster_init_irq)
MACHINE_END

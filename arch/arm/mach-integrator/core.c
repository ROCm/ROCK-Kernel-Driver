/*
 *  linux/arch/arm/mach-integrator/arch.c
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
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
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>

/* 
 * All IO addresses are mapped onto VA 0xFFFx.xxxx, where x.xxxx
 * is the (PA >> 12).
 *
 * Setup a VA for the Integrator interrupt controller (for header #0,
 * just for now).
 */
#define VA_IC_BASE	IO_ADDRESS(INTEGRATOR_IC_BASE) 
#define VA_CMIC_BASE	IO_ADDRESS(INTEGRATOR_HDR_BASE) + INTEGRATOR_HDR_IC_OFFSET

/*
 * Logical      Physical
 * e8000000	40000000	PCI memory		PHYS_PCI_MEM_BASE	(max 512M)
 * ec000000	61000000	PCI config space	PHYS_PCI_CONFIG_BASE	(max 16M)
 * ed000000	62000000	PCI V3 regs		PHYS_PCI_V3_BASE	(max 64k)
 * ee000000	60000000	PCI IO			PHYS_PCI_IO_BASE	(max 16M)
 * ef000000			Cache flush
 * f1000000	10000000	Core module registers
 * f1100000	11000000	System controller registers
 * f1200000	12000000	EBI registers
 * f1300000	13000000	Counter/Timer
 * f1400000	14000000	Interrupt controller
 * f1500000	15000000	RTC
 * f1600000	16000000	UART 0
 * f1700000	17000000	UART 1
 * f1a00000	1a000000	Debug LEDs
 * f1b00000	1b000000	GPIO
 */
 
static struct map_desc integrator_io_desc[] __initdata = {
 { IO_ADDRESS(INTEGRATOR_HDR_BASE),   INTEGRATOR_HDR_BASE,   SZ_4K,  MT_DEVICE },
 { IO_ADDRESS(INTEGRATOR_SC_BASE),    INTEGRATOR_SC_BASE,    SZ_4K,  MT_DEVICE },
 { IO_ADDRESS(INTEGRATOR_EBI_BASE),   INTEGRATOR_EBI_BASE,   SZ_4K,  MT_DEVICE },
 { IO_ADDRESS(INTEGRATOR_CT_BASE),    INTEGRATOR_CT_BASE,    SZ_4K,  MT_DEVICE },
 { IO_ADDRESS(INTEGRATOR_IC_BASE),    INTEGRATOR_IC_BASE,    SZ_4K,  MT_DEVICE },
 { IO_ADDRESS(INTEGRATOR_RTC_BASE),   INTEGRATOR_RTC_BASE,   SZ_4K,  MT_DEVICE },
 { IO_ADDRESS(INTEGRATOR_UART0_BASE), INTEGRATOR_UART0_BASE, SZ_4K,  MT_DEVICE },
 { IO_ADDRESS(INTEGRATOR_UART1_BASE), INTEGRATOR_UART1_BASE, SZ_4K,  MT_DEVICE },
 { IO_ADDRESS(INTEGRATOR_DBG_BASE),   INTEGRATOR_DBG_BASE,   SZ_4K,  MT_DEVICE },
 { IO_ADDRESS(INTEGRATOR_GPIO_BASE),  INTEGRATOR_GPIO_BASE,  SZ_4K,  MT_DEVICE },
 { PCI_MEMORY_VADDR,                  PHYS_PCI_MEM_BASE,     SZ_16M, MT_DEVICE },
 { PCI_CONFIG_VADDR,                  PHYS_PCI_CONFIG_BASE,  SZ_16M, MT_DEVICE },
 { PCI_V3_VADDR,                      PHYS_PCI_V3_BASE,      SZ_64K, MT_DEVICE },
 { PCI_IO_VADDR,                      PHYS_PCI_IO_BASE,      SZ_64K, MT_DEVICE }
};

static void __init integrator_map_io(void)
{
	iotable_init(integrator_io_desc, ARRAY_SIZE(integrator_io_desc));
}

#define ALLPCI ( (1 << IRQ_PCIINT0) | (1 << IRQ_PCIINT1) | (1 << IRQ_PCIINT2) | (1 << IRQ_PCIINT3) ) 

static void sc_mask_irq(unsigned int irq)
{
	writel(1 << irq, VA_IC_BASE + IRQ_ENABLE_CLEAR);
}

static void sc_unmask_irq(unsigned int irq)
{
	writel(1 << irq, VA_IC_BASE + IRQ_ENABLE_SET);
}

static struct irqchip sc_chip = {
	.ack	= sc_mask_irq,
	.mask	= sc_mask_irq,
	.unmask = sc_unmask_irq,
};
 
static void __init integrator_init_irq(void)
{
	unsigned int i;

	/* Disable all interrupts initially. */
	/* Do the core module ones */
	writel(-1, VA_CMIC_BASE + IRQ_ENABLE_CLEAR);

	/* do the header card stuff next */
	writel(-1, VA_IC_BASE + IRQ_ENABLE_CLEAR);
	writel(-1, VA_IC_BASE + FIQ_ENABLE_CLEAR);

	for (i = 0; i < NR_IRQS; i++) {
		if (((1 << i) && INTEGRATOR_SC_VALID_INT) != 0) {
			set_irq_chip(i, &sc_chip);
			set_irq_handler(i, do_level_IRQ);
			set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
		}
	}
}

MACHINE_START(INTEGRATOR, "ARM-Integrator")
	MAINTAINER("ARM Ltd/Deep Blue Solutions Ltd")
	BOOT_MEM(0x00000000, 0x16000000, 0xf1600000)
	BOOT_PARAMS(0x00000100)
	MAPIO(integrator_map_io)
	INITIRQ(integrator_init_irq)
MACHINE_END

/*
 *  linux/arch/arm/mach-versatile/core.c
 *
 *  Copyright (C) 1999 - 2003 ARM Limited
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
#include <linux/init.h>
#include <linux/device.h>
#include <linux/sysdev.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/leds.h>
#include <asm/mach-types.h>
#include <asm/hardware/amba.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>
#ifdef CONFIG_MMC
#include <asm/mach/mmc.h>
#endif

/*
 * All IO addresses are mapped onto VA 0xFFFx.xxxx, where x.xxxx
 * is the (PA >> 12).
 *
 * Setup a VA for the Versatile Vectored Interrupt Controller.
 */
#define VA_VIC_BASE		 IO_ADDRESS(VERSATILE_VIC_BASE)
#define VA_SIC_BASE		 IO_ADDRESS(VERSATILE_SIC_BASE)

static void vic_mask_irq(unsigned int irq)
{
	irq -= IRQ_VIC_START;
	writel(1 << irq, VA_VIC_BASE + VIC_IRQ_ENABLE_CLEAR);
}

static void vic_unmask_irq(unsigned int irq)
{
	irq -= IRQ_VIC_START;
	writel(1 << irq, VA_VIC_BASE + VIC_IRQ_ENABLE);
}

static struct irqchip vic_chip = {
	.ack	= vic_mask_irq,
	.mask	= vic_mask_irq,
	.unmask	= vic_unmask_irq,
};

static void sic_mask_irq(unsigned int irq)
{
	irq -= IRQ_SIC_START;
	writel(1 << irq, VA_SIC_BASE + SIC_IRQ_ENABLE_CLEAR);
}

static void sic_unmask_irq(unsigned int irq)
{
	irq -= IRQ_SIC_START;
	writel(1 << irq, VA_SIC_BASE + SIC_IRQ_ENABLE_SET);
}

static struct irqchip sic_chip = {
	.ack	= sic_mask_irq,
	.mask	= sic_mask_irq,
	.unmask	= sic_unmask_irq,
};

static void
sic_handle_irq(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs)
{
	unsigned long status = readl(VA_SIC_BASE + SIC_IRQ_STATUS);

	if (status == 0) {
		do_bad_IRQ(irq, desc, regs);
		return;
	}

	do {
		irq = ffs(status) - 1;
		status &= ~(1 << irq);

		irq += IRQ_SIC_START;

		desc = irq_desc + irq;
		desc->handle(irq, desc, regs);
	} while (status);
}

#if 1
#define IRQ_MMCI0A	IRQ_VICSOURCE22
#define IRQ_MMCI1A	IRQ_VICSOURCE23
#define IRQ_AACI	IRQ_VICSOURCE24
#define IRQ_ETH		IRQ_VICSOURCE25
#define PIC_MASK	0xFFD00000
#else
#define IRQ_MMCI0A	IRQ_SIC_MMCI0A
#define IRQ_MMCI1A	IRQ_SIC_MMCI1A
#define IRQ_AACI	IRQ_SIC_AACI
#define IRQ_ETH		IRQ_SIC_ETH
#define PIC_MASK	0
#endif

static void __init versatile_init_irq(void)
{
	unsigned int i, value;

	/* Disable all interrupts initially. */

	writel(0, VA_VIC_BASE + VIC_INT_SELECT);
	writel(0, VA_VIC_BASE + VIC_IRQ_ENABLE);
	writel(~0, VA_VIC_BASE + VIC_IRQ_ENABLE_CLEAR);
	writel(0, VA_VIC_BASE + VIC_IRQ_STATUS);
	writel(0, VA_VIC_BASE + VIC_ITCR);
	writel(~0, VA_VIC_BASE + VIC_IRQ_SOFT_CLEAR);

	/*
	 * Make sure we clear all existing interrupts
	 */
	writel(0, VA_VIC_BASE + VIC_VECT_ADDR);
	for (i = 0; i < 19; i++) {
		value = readl(VA_VIC_BASE + VIC_VECT_ADDR);
		writel(value, VA_VIC_BASE + VIC_VECT_ADDR);
	}

	for (i = 0; i < 16; i++) {
		value = readl(VA_VIC_BASE + VIC_VECT_CNTL0 + (i * 4));
		writel(value | VICVectCntl_Enable | i, VA_VIC_BASE + VIC_VECT_CNTL0 + (i * 4));
	}

	writel(32, VA_VIC_BASE + VIC_DEF_VECT_ADDR);

	for (i = IRQ_VIC_START; i <= IRQ_VIC_END; i++) {
		if (i != IRQ_VICSOURCE31) {
			set_irq_chip(i, &vic_chip);
			set_irq_handler(i, do_level_IRQ);
			set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
		}
	}

	set_irq_handler(IRQ_VICSOURCE31, sic_handle_irq);
	vic_unmask_irq(IRQ_VICSOURCE31);

	/* Do second interrupt controller */
	writel(~0, VA_SIC_BASE + SIC_IRQ_ENABLE_CLEAR);

	for (i = IRQ_SIC_START; i <= IRQ_SIC_END; i++) {
		if ((PIC_MASK & (1 << (i - IRQ_SIC_START))) == 0) {
			set_irq_chip(i, &sic_chip);
			set_irq_handler(i, do_level_IRQ);
			set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
		}
	}

	/*
	 * Interrupts on secondary controller from 0 to 8 are routed to
	 * source 31 on PIC.
	 * Interrupts from 21 to 31 are routed directly to the VIC on
	 * the corresponding number on primary controller. This is controlled
	 * by setting PIC_ENABLEx.
	 */
	writel(PIC_MASK, VA_SIC_BASE + SIC_INT_PIC_ENABLE);
}

static struct map_desc versatile_io_desc[] __initdata = {
 { IO_ADDRESS(VERSATILE_SYS_BASE),   VERSATILE_SYS_BASE,   SZ_4K,      MT_DEVICE },
 { IO_ADDRESS(VERSATILE_SIC_BASE),   VERSATILE_SIC_BASE,   SZ_4K,      MT_DEVICE },
 { IO_ADDRESS(VERSATILE_VIC_BASE),   VERSATILE_VIC_BASE,   SZ_4K,      MT_DEVICE },
 { IO_ADDRESS(VERSATILE_SCTL_BASE),  VERSATILE_SCTL_BASE,  SZ_4K * 9,  MT_DEVICE },
#ifdef CONFIG_DEBUG_LL
 { IO_ADDRESS(VERSATILE_UART0_BASE), VERSATILE_UART0_BASE, SZ_4K,      MT_DEVICE },
#endif
#ifdef FIXME
 { PCI_MEMORY_VADDR,		     PHYS_PCI_MEM_BASE,    SZ_16M,     MT_DEVICE },
 { PCI_CONFIG_VADDR,		     PHYS_PCI_CONFIG_BASE, SZ_16M,     MT_DEVICE },
 { PCI_V3_VADDR,		     PHYS_PCI_V3_BASE,     SZ_512K,    MT_DEVICE },
 { PCI_IO_VADDR,		     PHYS_PCI_IO_BASE,     SZ_64K,     MT_DEVICE },
#endif
};

static void __init versatile_map_io(void)
{
	iotable_init(versatile_io_desc, ARRAY_SIZE(versatile_io_desc));
}

#define VERSATILE_REFCOUNTER	(IO_ADDRESS(VERSATILE_SYS_BASE) + VERSATILE_SYS_24MHz_OFFSET)

/*
 * This is the VersatilePB sched_clock implementation.  This has
 * a resolution of 41.7ns, and a maximum value of about 179s.
 */
unsigned long long sched_clock(void)
{
	unsigned long long v;

	v = (unsigned long long)readl(VERSATILE_REFCOUNTER) * 125;
	do_div(v, 3);

	return v;
}


#define VERSATILE_FLASHCTRL    (IO_ADDRESS(VERSATILE_SYS_BASE) + VERSATILE_SYS_FLASH_OFFSET)

static int versatile_flash_init(void)
{
	u32 val;

	val = __raw_readl(VERSATILE_FLASHCTRL);
	val &= ~VERSATILE_FLASHPROG_FLVPPEN;
	__raw_writel(val, VERSATILE_FLASHCTRL);

	return 0;
}

static void versatile_flash_exit(void)
{
	u32 val;

	val = __raw_readl(VERSATILE_FLASHCTRL);
	val &= ~VERSATILE_FLASHPROG_FLVPPEN;
	__raw_writel(val, VERSATILE_FLASHCTRL);
}

static void versatile_flash_set_vpp(int on)
{
	u32 val;

	val = __raw_readl(VERSATILE_FLASHCTRL);
	if (on)
		val |= VERSATILE_FLASHPROG_FLVPPEN;
	else
		val &= ~VERSATILE_FLASHPROG_FLVPPEN;
	__raw_writel(val, VERSATILE_FLASHCTRL);
}

static struct flash_platform_data versatile_flash_data = {
	.map_name		= "cfi_probe",
	.width			= 4,
	.init			= versatile_flash_init,
	.exit			= versatile_flash_exit,
	.set_vpp		= versatile_flash_set_vpp,
};

static struct resource versatile_flash_resource = {
	.start			= VERSATILE_FLASH_BASE,
	.end			= VERSATILE_FLASH_BASE + VERSATILE_FLASH_SIZE,
	.flags			= IORESOURCE_MEM,
};

static struct platform_device versatile_flash_device = {
	.name			= "armflash",
	.id			= 0,
	.dev			= {
		.platform_data	= &versatile_flash_data,
	},
	.num_resources		= 1,
	.resource		= &versatile_flash_resource,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start		= VERSATILE_ETH_BASE,
		.end		= VERSATILE_ETH_BASE + SZ_64K - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_ETH,
		.end		= IRQ_ETH,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

#define VERSATILE_SYSMCI	(IO_ADDRESS(VERSATILE_SYS_BASE) + VERSATILE_SYS_MCI_OFFSET)

#ifdef CONFIG_MMC
static unsigned int mmc_status(struct device *dev)
{
	struct amba_device *adev = container_of(dev, struct amba_device, dev);
	u32 mask;

	if (adev->res.start == VERSATILE_MMCI0_BASE)
		mask = 1;
	else
		mask = 2;

	return readl(VERSATILE_SYSMCI) & mask;
}

static struct mmc_platform_data mmc0_plat_data = {
	.mclk		= 33000000,
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= mmc_status,
};

static struct mmc_platform_data mmc1_plat_data = {
	.mclk		= 33000000,
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.status		= mmc_status,
};
#endif

#define AMBA_DEVICE(name,busid,base,plat)			\
static struct amba_device name##_device = {			\
	.dev		= {					\
		.coherent_dma_mask = ~0,			\
		.bus_id	= busid,				\
		.platform_data = plat,				\
	},							\
	.res		= {					\
		.start	= VERSATILE_##base##_BASE,		\
		.end	= (VERSATILE_##base##_BASE) + SZ_4K - 1,\
		.flags	= IORESOURCE_MEM,			\
	},							\
	.dma_mask	= ~0,					\
	.irq		= base##_IRQ,				\
	/* .dma		= base##_DMA,*/				\
}

#define AACI_IRQ	{ IRQ_AACI, NO_IRQ }
#define AACI_DMA	{ 0x80, 0x81 }
#define MMCI0_IRQ	{ IRQ_MMCI0A,IRQ_SIC_MMCI0B }
#define MMCI0_DMA	{ 0x84, 0 }
#define KMI0_IRQ	{ IRQ_SIC_KMI0, NO_IRQ }
#define KMI0_DMA	{ 0, 0 }
#define KMI1_IRQ	{ IRQ_SIC_KMI1, NO_IRQ }
#define KMI1_DMA	{ 0, 0 }
#define UART3_IRQ	{ IRQ_SIC_UART3, NO_IRQ }
#define UART3_DMA	{ 0x86, 0x87 }
#define SCI1_IRQ	{ IRQ_SIC_SCI3, NO_IRQ }
#define SCI1_DMA	{ 0x88, 0x89 }
#define MMCI1_IRQ	{ IRQ_MMCI1A, IRQ_SIC_MMCI1B }
#define MMCI1_DMA	{ 0x85, 0 }

/*
 * These devices are connected directly to the multi-layer AHB switch
 */
#define SMC_IRQ		{ NO_IRQ, NO_IRQ }
#define SMC_DMA		{ 0, 0 }
#define MPMC_IRQ	{ NO_IRQ, NO_IRQ }
#define MPMC_DMA	{ 0, 0 }
#define CLCD_IRQ	{ IRQ_CLCDINT, NO_IRQ }
#define CLCD_DMA	{ 0, 0 }
#define DMAC_IRQ	{ IRQ_DMAINT, NO_IRQ }
#define DMAC_DMA	{ 0, 0 }

/*
 * These devices are connected via the core APB bridge
 */
#define SCTL_IRQ	{ NO_IRQ, NO_IRQ }
#define SCTL_DMA	{ 0, 0 }
#define WATCHDOG_IRQ	{ IRQ_WDOGINT, NO_IRQ }
#define WATCHDOG_DMA	{ 0, 0 }
#define GPIO0_IRQ	{ IRQ_GPIOINT0, NO_IRQ }
#define GPIO0_DMA	{ 0, 0 }
#define GPIO1_IRQ	{ IRQ_GPIOINT1, NO_IRQ }
#define GPIO1_DMA	{ 0, 0 }
#define GPIO2_IRQ	{ IRQ_GPIOINT2, NO_IRQ }
#define GPIO2_DMA	{ 0, 0 }
#define GPIO3_IRQ	{ IRQ_GPIOINT3, NO_IRQ }
#define GPIO3_DMA	{ 0, 0 }
#define RTC_IRQ		{ IRQ_RTCINT, NO_IRQ }
#define RTC_DMA		{ 0, 0 }

/*
 * These devices are connected via the DMA APB bridge
 */
#define SCI_IRQ		{ IRQ_SCIINT, NO_IRQ }
#define SCI_DMA		{ 7, 6 }
#define UART0_IRQ	{ IRQ_UARTINT0, NO_IRQ }
#define UART0_DMA	{ 15, 14 }
#define UART1_IRQ	{ IRQ_UARTINT1, NO_IRQ }
#define UART1_DMA	{ 13, 12 }
#define UART2_IRQ	{ IRQ_UARTINT2, NO_IRQ }
#define UART2_DMA	{ 11, 10 }
#define SSP_IRQ		{ IRQ_SSPINT, NO_IRQ }
#define SSP_DMA		{ 9, 8 }

/* FPGA Primecells */
AMBA_DEVICE(aaci,  "fpga:04", AACI,     NULL);
#ifdef CONFIG_MMC
AMBA_DEVICE(mmc0,  "fpga:05", MMCI0,    &mmc0_plat_data);
#endif
AMBA_DEVICE(kmi0,  "fpga:06", KMI0,     NULL);
AMBA_DEVICE(kmi1,  "fpga:07", KMI1,     NULL);
AMBA_DEVICE(uart3, "fpga:09", UART3,    NULL);
AMBA_DEVICE(sci1,  "fpga:0a", SCI1,     NULL);
#ifdef CONFIG_MMC
AMBA_DEVICE(mmc1,  "fpga:0b", MMCI1,    &mmc1_plat_data);
#endif

/* DevChip Primecells */
AMBA_DEVICE(smc,   "dev:00",  SMC,      NULL);
AMBA_DEVICE(mpmc,  "dev:10",  MPMC,     NULL);
AMBA_DEVICE(clcd,  "dev:20",  CLCD,     NULL);
AMBA_DEVICE(dmac,  "dev:30",  DMAC,     NULL);
AMBA_DEVICE(sctl,  "dev:e0",  SCTL,     NULL);
AMBA_DEVICE(wdog,  "dev:e1",  WATCHDOG, NULL);
AMBA_DEVICE(gpio0, "dev:e4",  GPIO0,    NULL);
AMBA_DEVICE(gpio1, "dev:e5",  GPIO1,    NULL);
AMBA_DEVICE(gpio2, "dev:e6",  GPIO2,    NULL);
AMBA_DEVICE(gpio3, "dev:e7",  GPIO3,    NULL);
AMBA_DEVICE(rtc,   "dev:e8",  RTC,      NULL);
AMBA_DEVICE(sci0,  "dev:f0",  SCI,      NULL);
AMBA_DEVICE(uart0, "dev:f1",  UART0,    NULL);
AMBA_DEVICE(uart1, "dev:f2",  UART1,    NULL);
AMBA_DEVICE(uart2, "dev:f3",  UART2,    NULL);
AMBA_DEVICE(ssp0,  "dev:f4",  SSP,      NULL);

static struct amba_device *amba_devs[] __initdata = {
	&dmac_device,
	&uart0_device,
	&uart1_device,
	&uart2_device,
	&uart3_device,
	&smc_device,
	&mpmc_device,
	&clcd_device,
	&sctl_device,
	&wdog_device,
	&gpio0_device,
	&gpio1_device,
	&gpio2_device,
	&gpio3_device,
	&rtc_device,
	&sci0_device,
	&ssp0_device,
	&aaci_device,
#ifdef CONFIG_MMC
	&mmc0_device,
#endif
	&kmi0_device,
	&kmi1_device,
	&sci1_device,
#ifdef CONFIG_MMC
	&mmc1_device,
#endif
};

#define VA_LEDS_BASE (IO_ADDRESS(VERSATILE_SYS_BASE) + VERSATILE_SYS_LED_OFFSET)

static void versatile_leds_event(led_event_t ledevt)
{
	unsigned long flags;
	u32 val;

	local_irq_save(flags);
	val = readl(VA_LEDS_BASE);

	switch (ledevt) {
	case led_idle_start:
		val = val & ~VERSATILE_SYS_LED0;
		break;

	case led_idle_end:
		val = val | VERSATILE_SYS_LED0;
		break;

	case led_timer:
		val = val ^ VERSATILE_SYS_LED1;
		break;

	case led_halted:
		val = 0;
		break;

	default:
		break;
	}

	writel(val, VA_LEDS_BASE);
	local_irq_restore(flags);
}

static void __init versatile_init(void)
{
	int i;

	platform_add_device(&versatile_flash_device);
	platform_add_device(&smc91x_device);

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++) {
		struct amba_device *d = amba_devs[i];
		amba_device_register(d, &iomem_resource);
	}

	leds_event = versatile_leds_event;
}

MACHINE_START(VERSATILE_PB, "ARM-Versatile PB")
	MAINTAINER("ARM Ltd/Deep Blue Solutions Ltd")
	BOOT_MEM(0x00000000, 0x101f1000, 0xf11f1000)
	BOOT_PARAMS(0x00000100)
	MAPIO(versatile_map_io)
	INITIRQ(versatile_init_irq)
	INIT_MACHINE(versatile_init)
MACHINE_END

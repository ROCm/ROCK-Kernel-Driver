/*
 * linux/arch/arm/mach-iop3xx/iop331-setup.c
 *
 * Author: Dave Jiang (dave.jiang@intel.com)
 * Copyright (C) 2004 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/config.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_core.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/mach/map.h>
#include <asm/setup.h>
#include <asm/system.h>
#include <asm/memory.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#define IOP331_UART_XTAL 33334000

/*
 * Standard IO mapping for all IOP331 based systems
 */
static struct map_desc iop331_std_desc[] __initdata = {
 /* virtual     physical      length      type */

 /* mem mapped registers */
 { IOP331_VIRT_MEM_BASE,  IOP331_PHYS_MEM_BASE,   0x00002000,  MT_DEVICE },

 /* PCI IO space */
 { 0xfe000000,  0x90000000,   0x00020000,  MT_DEVICE }
};

static struct uart_port iop331_serial_ports[] = {
	{
		.membase	= (char*)(IQ80331_UART0_VIRT),
		.mapbase	= (IQ80331_UART0_PHYS),
		.irq		= IRQ_IOP331_UART0,
		.flags		= UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IOP331_UART_XTAL,
		.line		= 0,
		.type		= PORT_XSCALE,
		.fifosize	= 32
	} , {
		.membase	= (char*)(IQ80331_UART1_VIRT),
		.mapbase	= (IQ80331_UART1_PHYS),
		.irq		= IRQ_IOP331_UART1,
		.flags		= UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IOP331_UART_XTAL,
		.line		= 1,
		.type		= PORT_XSCALE,
		.fifosize	= 32
	}
};

void __init iop331_map_io(void)
{
	iotable_init(iop331_std_desc, ARRAY_SIZE(iop331_std_desc));
	early_serial_setup(&iop331_serial_ports[0]);
	early_serial_setup(&iop331_serial_ports[1]);
}

#ifdef CONFIG_ARCH_IQ80331
extern void iop331_init_irq(void);
extern struct sys_timer iop331_timer;
extern void iq80331_map_io(void);
#endif

#if defined(CONFIG_ARCH_IQ80331)
MACHINE_START(IQ80331, "Intel IQ80331")
    MAINTAINER("Intel Corp.")
    BOOT_MEM(PHYS_OFFSET, 0xfefff000, 0xfffff000) // virtual, physical
    //BOOT_MEM(PHYS_OFFSET, IQ80331_UART0_VIRT, IQ80331_UART0_PHYS)
    MAPIO(iq80331_map_io)
    INITIRQ(iop331_init_irq)
    .timer		= &iop331_timer,
    BOOT_PARAMS(0x0100)
MACHINE_END
#else
#error No machine descriptor defined for this IOP3XX implementation
#endif



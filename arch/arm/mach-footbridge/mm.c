/*
 *  linux/arch/arm/mach-footbridge/mm.c
 *
 *  Copyright (C) 1998-2000 Russell King, Dave Gilbert.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Extra MM routines for the EBSA285 architecture
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
 
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/hardware/dec21285.h>
#include <asm/mach-types.h>

#include <asm/mach/map.h>

/*
 * Common mapping for all systems.  Note that the outbound write flush is
 * commented out since there is a "No Fix" problem with it.  Not mapping
 * it means that we have extra bullet protection on our feet.
 */
static struct map_desc fb_common_io_desc[] __initdata = {
 { ARMCSR_BASE,	 DC21285_ARMCSR_BASE,	    ARMCSR_SIZE,  MT_DEVICE },
 { XBUS_BASE,    0x40000000,		    XBUS_SIZE,    MT_DEVICE }
};

/*
 * The mapping when the footbridge is in host mode.  We don't map any of
 * this when we are in add-in mode.
 */
static struct map_desc ebsa285_host_io_desc[] __initdata = {
#if defined(CONFIG_ARCH_FOOTBRIDGE) && defined(CONFIG_FOOTBRIDGE_HOST)
 { PCIMEM_BASE,  DC21285_PCI_MEM,	    PCIMEM_SIZE,  MT_DEVICE },
 { PCICFG0_BASE, DC21285_PCI_TYPE_0_CONFIG, PCICFG0_SIZE, MT_DEVICE },
 { PCICFG1_BASE, DC21285_PCI_TYPE_1_CONFIG, PCICFG1_SIZE, MT_DEVICE },
 { PCIIACK_BASE, DC21285_PCI_IACK,	    PCIIACK_SIZE, MT_DEVICE },
 { PCIO_BASE,    DC21285_PCI_IO,	    PCIO_SIZE,	  MT_DEVICE }
#endif
};

/*
 * The CO-ebsa285 mapping.
 */
static struct map_desc co285_io_desc[] __initdata = {
#ifdef CONFIG_ARCH_CO285
 { PCIO_BASE,	 DC21285_PCI_IO,	    PCIO_SIZE,    MT_DEVICE },
 { PCIMEM_BASE,	 DC21285_PCI_MEM,	    PCIMEM_SIZE,  MT_DEVICE }
#endif
};

void __init footbridge_map_io(void)
{
	/*
	 * Set up the common mapping first; we need this to
	 * determine whether we're in host mode or not.
	 */
	iotable_init(fb_common_io_desc, ARRAY_SIZE(fb_common_io_desc));

	/*
	 * Now, work out what we've got to map in addition on this
	 * platform.
	 */
	if (machine_is_co285())
		iotable_init(co285_io_desc, ARRAY_SIZE(co285_io_desc));
	if (footbridge_cfn_mode())
		iotable_init(ebsa285_host_io_desc, ARRAY_SIZE(ebsa285_host_io_desc));
}

#ifdef CONFIG_FOOTBRIDGE_ADDIN

/*
 * These two functions convert virtual addresses to PCI addresses and PCI
 * addresses to virtual addresses.  Note that it is only legal to use these
 * on memory obtained via get_zeroed_page or kmalloc.
 */
unsigned long __virt_to_bus(unsigned long res)
{
	WARN_ON(res < PAGE_OFFSET || res >= (unsigned long)high_memory);

	return (res - PAGE_OFFSET) + (*CSR_PCISDRAMBASE & 0xfffffff0);
}

unsigned long __bus_to_virt(unsigned long res)
{
	res -= (*CSR_PCISDRAMBASE & 0xfffffff0);
	res += PAGE_OFFSET;

	WARN_ON(res < PAGE_OFFSET || res >= (unsigned long)high_memory);

	return res;
}

#endif

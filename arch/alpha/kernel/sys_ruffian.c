/*
 *	linux/arch/alpha/kernel/sys_ruffian.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998, 1999, 2000 Richard Henderson
 *
 * Code supporting the RUFFIAN.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_cia.h>

#include "proto.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"


static void __init
ruffian_init_irq(void)
{
	/* Invert 6&7 for i82371 */
	*(vulp)PYXIS_INT_HILO  = 0x000000c0UL; mb();
	*(vulp)PYXIS_INT_CNFG  = 0x00002064UL; mb();	 /* all clear */

	outb(0x11,0xA0);
	outb(0x08,0xA1);
	outb(0x02,0xA1);
	outb(0x01,0xA1);
	outb(0xFF,0xA1);
	
	outb(0x11,0x20);
	outb(0x00,0x21);
	outb(0x04,0x21);
	outb(0x01,0x21);
	outb(0xFF,0x21);
	
	/* Finish writing the 82C59A PIC Operation Control Words */
	outb(0x20,0xA0);
	outb(0x20,0x20);
	
	init_i8259a_irqs();

	/* Not interested in the bogus interrupts (0,3,6),
	   NMI (1), HALT (2), flash (5), or 21142 (8).  */
	init_pyxis_irqs(0x16f0000);

	common_init_isa_dma();
}

static void __init
ruffian_init_rtc(void)
{
	/* Ruffian does not have the RTC connected to the CPU timer
	   interrupt.  Instead, it uses the PIT connected to IRQ 0.  */

	/* Setup interval timer.  */
	outb(0x34, 0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb(LATCH & 0xff, 0x40);	/* LSB */
	outb(LATCH >> 8, 0x40);		/* MSB */

	outb(0xb6, 0x43);		/* pit counter 2: speaker */
	outb(0x31, 0x42);
	outb(0x13, 0x42);

	setup_irq(0, &timer_irqaction);
}

static void
ruffian_kill_arch (int mode)
{
#if 0
	/* This only causes re-entry to ARCSBIOS */
	/* Perhaps this works for other PYXIS as well?  */
	*(vuip) PYXIS_RESET = 0x0000dead;
	mb();
#endif
}

static int __init
ruffian_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	/* We don't know anything about the PCI routing, so leave
	   the IRQ unchanged.  */
	return dev->irq;
}


#ifdef BUILDING_FOR_MILO
/*
 * The DeskStation Ruffian motherboard firmware does not place
 * the memory size in the PALimpure area.  Therefore, we use
 * the Bank Configuration Registers in PYXIS to obtain the size.
 */
static unsigned long __init
ruffian_get_bank_size(unsigned long offset)
{
	unsigned long bank_addr, bank, ret = 0;
  
	/* Valid offsets are: 0x800, 0x840 and 0x880
	   since Ruffian only uses three banks.  */
	bank_addr = (unsigned long)PYXIS_MCR + offset;
	bank = *(vulp)bank_addr;
    
	/* Check BANK_ENABLE */
	if (bank & 0x01) {
		static unsigned long size[] __initdata = {
			0x40000000UL, /* 0x00,   1G */ 
			0x20000000UL, /* 0x02, 512M */
			0x10000000UL, /* 0x04, 256M */
			0x08000000UL, /* 0x06, 128M */
			0x04000000UL, /* 0x08,  64M */
			0x02000000UL, /* 0x0a,  32M */
			0x01000000UL, /* 0x0c,  16M */
			0x00800000UL, /* 0x0e,   8M */
			0x80000000UL, /* 0x10,   2G */
		};

		bank = (bank & 0x1e) >> 1;
		if (bank < sizeof(size)/sizeof(*size))
			ret = size[bank];
	}

	return ret;
}
#endif /* BUILDING_FOR_MILO */

/*
 * The System Vector
 */

struct alpha_machine_vector ruffian_mv __initmv = {
	vector_name:		"Ruffian",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_PYXIS_IO,
	DO_CIA_BUS,
	machine_check:		cia_machine_check,
	max_dma_address:	ALPHA_RUFFIAN_MAX_DMA_ADDRESS,
	min_io_address:		DEFAULT_IO_BASE,
	min_mem_address:	DEFAULT_MEM_BASE,

	nr_irqs:		48,
	device_interrupt:	pyxis_device_interrupt,

	init_arch:		pyxis_init_arch,
	init_irq:		ruffian_init_irq,
	init_rtc:		ruffian_init_rtc,
	init_pci:		cia_init_pci,
	kill_arch:		ruffian_kill_arch,
	pci_map_irq:		ruffian_map_irq,
	pci_swizzle:		common_swizzle,
};
ALIAS_MV(ruffian)

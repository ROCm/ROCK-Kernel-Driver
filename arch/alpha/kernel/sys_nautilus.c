/*
 *	linux/arch/alpha/kernel/sys_nautilus.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1998 Richard Henderson
 *	Copyright (C) 1999 Alpha Processor, Inc.,
 *		(David Daniel, Stig Telfer, Soohoon Lee)
 *
 * Code supporting NAUTILUS systems.
 *
 *
 * NAUTILUS has the following I/O features:
 *
 * a) Driven by AMD 751 aka IRONGATE (northbridge):
 *     4 PCI slots
 *     1 AGP slot
 *
 * b) Driven by ALI M1543C (southbridge)
 *     2 ISA slots
 *     2 IDE connectors
 *     1 dual drive capable FDD controller
 *     2 serial ports
 *     1 ECP/EPP/SP parallel port
 *     2 USB ports
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/reboot.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pci.h>
#include <asm/pgtable.h>
#include <asm/core_irongate.h>
#include <asm/hwrpb.h>
#include <asm/tlbflush.h>

#include "proto.h"
#include "err_impl.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"


static void __init
nautilus_init_irq(void)
{
	if (alpha_using_srm) {
		alpha_mv.device_interrupt = srm_device_interrupt;
	}

	init_i8259a_irqs();
	common_init_isa_dma();
}

static int __init
nautilus_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	/* Preserve the IRQ set up by the console.  */

	u8 irq;
	pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
	return irq;
}

void
nautilus_kill_arch(int mode)
{
	switch (mode) {
	case LINUX_REBOOT_CMD_RESTART:
		if (! alpha_using_srm) {
			u8 t8;
			pcibios_read_config_byte(0, 0x38, 0x43, &t8);
			pcibios_write_config_byte(0, 0x38, 0x43, t8 | 0x80);
			outb(1, 0x92);
			outb(0, 0x92);
			/* NOTREACHED */
		}
		break;

	case LINUX_REBOOT_CMD_POWER_OFF:
		{
			u32 pmuport;
			pcibios_read_config_dword(0, 0x88, 0x10, &pmuport);
			pmuport &= 0xfffe;
			outl(0xffff, pmuport); /* clear pending events */
			outw(0x2000, pmuport+4); /* power off */
			/* NOTREACHED */
		}
		break;
	}
}

/* Perform analysis of a machine check that arrived from the system (NMI) */

static void
naut_sys_machine_check(unsigned long vector, unsigned long la_ptr,
		       struct pt_regs *regs)
{
	printk("xtime %lx\n", CURRENT_TIME);
	printk("PC %lx RA %lx\n", regs->pc, regs->r26);
	irongate_pci_clr_err();
}

/* Machine checks can come from two sources - those on the CPU and those
   in the system.  They are analysed separately but all starts here.  */

void
nautilus_machine_check(unsigned long vector, unsigned long la_ptr,
		       struct pt_regs *regs)
{
	char *mchk_class;

	/* Now for some analysis.  Machine checks fall into two classes --
	   those picked up by the system, and those picked up by the CPU.
	   Add to that the two levels of severity - correctable or not.  */

	if (vector == SCB_Q_SYSMCHK
	    && ((IRONGATE0->dramms & 0x300) == 0x300)) {
		unsigned long nmi_ctl;

		/* Clear ALI NMI */
		nmi_ctl = inb(0x61);
		nmi_ctl |= 0x0c;
		outb(nmi_ctl, 0x61);
		nmi_ctl &= ~0x0c;
		outb(nmi_ctl, 0x61);

		/* Write again clears error bits.  */
		IRONGATE0->stat_cmd = IRONGATE0->stat_cmd & ~0x100;
		mb();
		IRONGATE0->stat_cmd;

		/* Write again clears error bits.  */
		IRONGATE0->dramms = IRONGATE0->dramms;
		mb();
		IRONGATE0->dramms;

		draina();
		wrmces(0x7);
		mb();
		return;
	}

	if (vector == SCB_Q_SYSERR)
		mchk_class = "Correctable";
	else if (vector == SCB_Q_SYSMCHK)
		mchk_class = "Fatal";
	else {
		ev6_machine_check(vector, la_ptr, regs);
		return;
	}

	printk(KERN_CRIT "NAUTILUS Machine check 0x%lx "
			 "[%s  System Machine Check (NMI)]\n",
	       vector, mchk_class);

	naut_sys_machine_check(vector, la_ptr, regs);

	/* Tell the PALcode to clear the machine check */
	draina();
	wrmces(0x7);
	mb();
}


/*
 * The System Vectors
 */

struct alpha_machine_vector nautilus_mv __initmv = {
	.vector_name		= "Nautilus",
	DO_EV6_MMU,
	DO_DEFAULT_RTC,
	DO_IRONGATE_IO,
	DO_IRONGATE_BUS,
	.machine_check		= nautilus_machine_check,
	.max_isa_dma_address	= ALPHA_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= DEFAULT_IO_BASE,
	.min_mem_address	= IRONGATE_DEFAULT_MEM_BASE,

	.nr_irqs		= 16,
	.device_interrupt	= isa_device_interrupt,

	.init_arch		= irongate_init_arch,
	.init_irq		= nautilus_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= common_init_pci,
	.kill_arch		= nautilus_kill_arch,
	.pci_map_irq		= nautilus_map_irq,
	.pci_swizzle		= common_swizzle,
};
ALIAS_MV(nautilus)

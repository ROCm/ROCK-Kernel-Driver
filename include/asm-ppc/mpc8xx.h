/*
 * BK Id: SCCS/s.mpc8xx.h 1.15 11/01/01 12:48:53 trini
 */

/* This is the single file included by all MPC8xx build options.
 * Since there are many different boards and no standard configuration,
 * we have a unique include file for each.  Rather than change every
 * file that has to include MPC8xx configuration, they all include
 * this one and the configuration switching is done here.
 */
#ifdef __KERNEL__
#ifndef __CONFIG_8xx_DEFS
#define __CONFIG_8xx_DEFS

#include <linux/config.h>

#ifdef CONFIG_8xx

#ifdef CONFIG_MBX
#include <asm/mbx.h>
#endif

#ifdef CONFIG_FADS
#include <asm/fads.h>
#endif

#ifdef CONFIG_RPXLITE
#include <asm/rpxlite.h>
#endif

#ifdef CONFIG_BSEIP
#include <asm/bseip.h>
#endif

#ifdef CONFIG_RPXCLASSIC
#include <asm/rpxclassic.h>
#endif

#if defined(CONFIG_TQM8xxL)
#include <asm/tqm8xx.h>
#endif

#if defined(CONFIG_SPD823TS)
#include <asm/spd8xx.h>
#endif

#if defined(CONFIG_IVMS8) || defined(CONFIG_IVML24)
#include <asm/ivms8.h>
#endif

/* I need this to get pt_regs.......
*/
#include <asm/ptrace.h>

/* Currently, all 8xx boards that support a processor to PCI/ISA bridge
 * use the same memory map.
 */
#if 0
#if defined(CONFIG_PCI) && defined(PCI_ISA_IO_ADDR)
#define	_IO_BASE PCI_ISA_IO_ADDR
#define	_ISA_MEM_BASE PCI_ISA_MEM_ADDR
#define PCI_DRAM_OFFSET 0x80000000
#else
#define _IO_BASE        0
#define _ISA_MEM_BASE   0
#define PCI_DRAM_OFFSET 0
#endif
#else
#if !defined(_IO_BASE)  /* defined in board specific header */
#define _IO_BASE        0
#endif
#define _ISA_MEM_BASE   0
#define PCI_DRAM_OFFSET 0
#endif

#ifndef __ASSEMBLY__
extern unsigned long isa_io_base;
extern unsigned long isa_mem_base;
extern unsigned long pci_dram_offset;

/* The "residual" data board information structure the boot loader
 * hands to us.
 */
extern unsigned char __res[];

extern int request_8xxirq(unsigned int irq,
		       void (*handler)(int, void *, struct pt_regs *),
		       unsigned long flags, 
		       const char *device,
		       void *dev_id);
#endif /* !__ASSEMBLY__ */
#endif /* CONFIG_8xx */
#endif
#endif /* __KERNEL__ */

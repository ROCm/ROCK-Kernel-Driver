/* This is the single file included by all MPC8260 build options.
 * Since there are many different boards and no standard configuration,
 * we have a unique include file for each.  Rather than change every
 * file that has to include MPC8260 configuration, they all include
 * this one and the configuration switching is done here.
 */
#ifdef __KERNEL__
#ifndef __CONFIG_8260_DEFS
#define __CONFIG_8260_DEFS

#include <linux/config.h>
#include <platforms/mpc82xx.h>

/* Make sure the memory translation stuff is there if PCI not used.
 */
#ifndef _IO_BASE
#define _IO_BASE        0
#endif

#ifndef _ISA_MEM_BASE
#define _ISA_MEM_BASE   0
#endif

#ifndef PCI_DRAM_OFFSET
#define PCI_DRAM_OFFSET 0
#endif

/* Map 256MB I/O region
 */
#ifndef IO_PHYS_ADDR
#define IO_PHYS_ADDR	0xe0000000
#endif
#ifndef IO_VIRT_ADDR
#define IO_VIRT_ADDR	IO_PHYS_ADDR
#endif

/* The "residual" data board information structure the boot loader
 * hands to us.
 */
extern unsigned char __res[];

#endif /* !__CONFIG_8260_DEFS */
#endif /* __KERNEL__ */

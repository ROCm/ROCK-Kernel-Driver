
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

#ifdef CONFIG_8260

#ifdef CONFIG_EST8260
#include <asm/est8260.h>
#endif

/* I don't yet have the ISA or PCI stuff done....no 8260 with
 * such thing.....
 */
#define _IO_BASE        0
#define _ISA_MEM_BASE   0
#define PCI_DRAM_OFFSET 0

/* The "residual" data board information structure the boot loader
 * hands to us.
 */
extern unsigned char __res[];

/* I need this to get pt_regs.......
*/
#include <asm/ptrace.h>

extern int request_8xxirq(unsigned int irq,
		       void (*handler)(int, void *, struct pt_regs *),
		       unsigned long flags, 
		       const char *device,
		       void *dev_id);

#endif /* CONFIG_8260 */
#endif
#endif /* __KERNEL__ */

/*
 *  linux/include/asm-arm/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the i386 architecture specific IDE code.
 */

#ifndef __ASMARM_IDE_H
#define __ASMARM_IDE_H

#ifdef __KERNEL__

#ifndef MAX_HWIFS
#define MAX_HWIFS	4
#endif

#include <asm/irq.h>
#include <asm/mach-types.h>

/* JMA 18.05.03 these will never be needed, but the kernel needs them to compile */
#define __ide_mm_insw(port,addr,len)    readsw(port,addr,len)
#define __ide_mm_insl(port,addr,len)    readsl(port,addr,len)
#define __ide_mm_outsw(port,addr,len)   writesw(port,addr,len)
#define __ide_mm_outsl(port,addr,len)   writesl(port,addr,len)

/*
 * Set up a hw structure for a specified data port, control port and IRQ.
 * This should follow whatever the default interface uses.
 */
static __inline__ void
ide_init_hwif_ports(hw_regs_t *hw, int data_port, int ctrl_port, int *irq)
{
        ide_ioreg_t reg = (ide_ioreg_t) data_port;
        int i;

        for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
                hw->io_ports[i] = reg;
                reg += 1;
        }
        hw->io_ports[IDE_CONTROL_OFFSET] = (ide_ioreg_t) ctrl_port;
        if (irq)
                *irq = 0;
}

/*
 * This registers the standard ports for this architecture with the IDE
 * driver.
 */
static __inline__ void ide_init_default_hwifs(void)
{
        if (machine_is_a5k()) {
                hw_regs_t hw;

                memset(&hw, 0, sizeof(hw));

                ide_init_hwif_ports(&hw, 0x1f0, 0x3f6, NULL);
                hw.irq = IRQ_HARDDISK;
                ide_register_hw(&hw,NULL);
        }
}


/*
 * We always use the new IDE port registering,
 * so these are fixed here.
 */
#define ide_default_io_base(i)		((ide_ioreg_t)0)
#define ide_default_irq(b)		(0)

#endif /* __KERNEL__ */

#endif /* __ASMARM_IDE_H */

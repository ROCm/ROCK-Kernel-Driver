/* include/asm-arm/arch-lh7a40x/ide.h
 *
 *  Copyright (C) 2004 Logic Product Development
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

#ifndef __ASM_ARCH_IDE_H
#define __ASM_ARCH_IDE_H

#if defined (CONFIG_MACH_LPD7A400) || defined (CONFIG_MACH_LPD7A404)

/*  This implementation of ide.h only applies to the LPD CardEngines.
 *  Thankfully, there is less to do for the KEV.
 */

#include <linux/config.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/arch/registers.h>

#define IDE_REG_LINE	(1<<12)	/* A12 drives !REG  */
#define IDE_ALT_LINE	(1<<11)	/* Unused A11 allows non-overlapping regions */
#define IDE_CONTROLREG_OFFSET	(0xe)

void lpd7a40x_hwif_ioops (struct hwif_s* hwif);

static __inline__ void ide_init_hwif_ports (hw_regs_t *hw, int data_port,
					    int ctrl_port, int *irq)
{
	ide_ioreg_t reg;
        int i;
        int regincr = 1;

        memset (hw, 0, sizeof (*hw));

        reg = (ide_ioreg_t) data_port;

        for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
                hw->io_ports[i] = reg;
                reg += regincr;
        }

        hw->io_ports[IDE_CONTROL_OFFSET] = (ide_ioreg_t) ctrl_port;

        if (irq)
                *irq = IDE_NO_IRQ;
}

static __inline__  void ide_init_default_hwifs (void)
{
	hw_regs_t hw;
	struct hwif_s* hwif;

	ide_init_hwif_ports (&hw,
			     CF_VIRT + IDE_REG_LINE,
			     CF_VIRT + IDE_REG_LINE + IDE_ALT_LINE
			     + IDE_CONTROLREG_OFFSET,
			     NULL);

	ide_register_hw (&hw, &hwif);
	lpd7a40x_hwif_ioops (hwif); /* Override IO routines */
}

#else

static __inline__ void ide_init_hwif_ports (hw_regs_t *hw, int data_port,
					    int ctrl_port, int *irq) {}
static __inline__ void ide_init_default_hwifs (void) {}

#endif

#endif

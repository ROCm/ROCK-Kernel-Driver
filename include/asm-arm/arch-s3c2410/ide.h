/* linux/include/asm-arm/arch-s3c2410/ide.h
 *
 *  Copyright (C) 1997 Russell King
 *  Copyright (C) 2003 Simtec Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Modifications:
 *   29-07-1998	RMK	Major re-work of IDE architecture specific code
 *   16-05-2003 BJD	Changed to work with BAST IDE ports
 *   04-09-2003 BJD	Modifications for V2.6
 */

#ifndef __ASM_ARCH_IDE_H
#define __ASM_ARCH_IDE_H

#include <asm/irq.h>

/*
 * Set up a hw structure for a specified data port, control port and IRQ.
 * This should follow whatever the default interface uses.
 */

static inline void ide_init_hwif_ports(hw_regs_t *hw, unsigned long data_port,
				       unsigned long ctrl_port, int *irq)
{
	unsigned long reg = data_port;
	int i;

	memset(hw, 0, sizeof(*hw));

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}
	hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	if (irq)
		*irq = 0;
}

/* we initialise our ide devices from the main ide core, due to problems
 * with doing it in this function
*/

#define ide_init_default_hwifs() do { } while(0)

#endif /* __ASM_ARCH_IDE_H */

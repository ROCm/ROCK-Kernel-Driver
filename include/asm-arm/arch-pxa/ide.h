/*
 * linux/include/asm-arm/arch-pxa/ide.h
 *
 * Author:	George Davis
 * Created:	Jan 10, 2002
 * Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Originally based upon linux/include/asm-arm/arch-sa1100/ide.h
 *
 */

#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>


/*
 * Set up a hw structure for a specified data port, control port and IRQ.
 * This should follow whatever the default interface uses.
 */
static inline void ide_init_hwif_ports(hw_regs_t *hw, unsigned long data_port,
				       unsigned long ctrl_port, int *irq)
{
	unsigned long reg = data_port;
	int i;
	int regincr = 1;

	memset(hw, 0, sizeof(*hw));

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += regincr;
	}

	hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;

	if (irq)
		*irq = 0;
}


/*
 * Register the standard ports for this architecture with the IDE driver.
 */
static __inline__ void
ide_init_default_hwifs(void)
{
	/* Nothing to declare... */
}

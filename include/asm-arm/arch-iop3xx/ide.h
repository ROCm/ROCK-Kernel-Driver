/*
 * include/asm-arm/arch-iop3xx/ide.h
 *
 * Generic IDE functions for IOP310 systems
 *
 * Author: Deepak Saxena <dsaxena@mvista.com>
 *
 * Copyright 2001 MontaVista Software Inc.
 *
 * 09/26/2001 - Sharon Baartmans
 * 	Fixed so it actually works.
 */

#ifndef _ASM_ARCH_IDE_H_
#define _ASM_ARCH_IDE_H_

/*
 * Set up a hw structure for a specified data port, control port and IRQ.
 * This should follow whatever the default interface uses.
 */
static __inline__ void
ide_init_hwif_ports(hw_regs_t *hw, int data_port, int ctrl_port, int *irq)
{
	ide_ioreg_t reg;
	int i;
	int regincr = 1;

	memset(hw, 0, sizeof(*hw));

	reg = (ide_ioreg_t)data_port;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += regincr;
	}

	hw->io_ports[IDE_CONTROL_OFFSET] = (ide_ioreg_t) ctrl_port;

	if (irq) *irq = 0;
}

/*
 * This registers the standard ports for this architecture with the IDE
 * driver.
 */
static __inline__ void ide_init_default_hwifs(void)
{
	/* There are no standard ports */
}

#endif

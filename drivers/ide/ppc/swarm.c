/*
 * Copyright (C) 2001 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*  Derived loosely from ide-pmac.c, so:
 *  
 *  Copyright (C) 1998 Paul Mackerras.
 *  Copyright (C) 1995-1998 Mark Lord
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/sibyte/sb1250_int.h>
#include <asm/sibyte/swarm_ide.h>

void __init swarm_ide_probe(void)
{
	int i;
	ide_hwif_t *hwif;
	/* 
	 * Find the first untaken slot in hwifs 
	 */
	for (i = 0; i < MAX_HWIFS; i++) {
		if (!ide_hwifs[i].io_ports[IDE_DATA_OFFSET]) {
			break;
		}
	}
	if (i == MAX_HWIFS) {
		printk("No space for SWARM onboard IDE driver in ide_hwifs[].  Not enabled.\n");
		return;
	}

	/* Set up our stuff */
	hwif = &ide_hwifs[i];
	hwif->hw.io_ports[IDE_DATA_OFFSET]    = SWARM_IDE_REG(0x1f0);
	hwif->hw.io_ports[IDE_ERROR_OFFSET]   = SWARM_IDE_REG(0x1f1);
	hwif->hw.io_ports[IDE_NSECTOR_OFFSET] = SWARM_IDE_REG(0x1f2);
	hwif->hw.io_ports[IDE_SECTOR_OFFSET]  = SWARM_IDE_REG(0x1f3);
	hwif->hw.io_ports[IDE_LCYL_OFFSET]    = SWARM_IDE_REG(0x1f4);
	hwif->hw.io_ports[IDE_HCYL_OFFSET]    = SWARM_IDE_REG(0x1f5);
	hwif->hw.io_ports[IDE_SELECT_OFFSET]  = SWARM_IDE_REG(0x1f6);
	hwif->hw.io_ports[IDE_STATUS_OFFSET]  = SWARM_IDE_REG(0x1f7);
	hwif->hw.io_ports[IDE_CONTROL_OFFSET] = SWARM_IDE_REG(0x3f6);
	hwif->hw.io_ports[IDE_IRQ_OFFSET]     = SWARM_IDE_REG(0x3f7);
//	hwif->hw->ack_intr                     = swarm_ide_ack_intr;
	hwif->hw.irq                          = SWARM_IDE_INT;
	hwif->ideproc                         = swarm_ideproc;

	memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->io_ports));
	hwif->irq = hwif->hw.irq;
	printk("SWARM onboard IDE configured as device %i\n", i);
}


/*
 *  linux/drivers/ide/pci/adma100.c -- basic support for Pacific Digital ADMA-100 boards
 *
 *     Created 09 Apr 2002 by Mark Lord
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/io.h>

void __init ide_init_adma100 (ide_hwif_t *hwif)
{
	unsigned long  phy_admctl = pci_resource_start(hwif->pci_dev, 4) + 0x80 + (hwif->channel * 0x20);
	void *v_admctl;

	hwif->autodma = 0;		// not compatible with normal IDE DMA transfers
	hwif->dma_base = 0;		// disable DMA completely
	hwif->io_ports[IDE_CONTROL_OFFSET] += 4;	// chip needs offset of 6 instead of 2
	v_admctl = ioremap_nocache(phy_admctl, 1024);	// map config regs, so we can turn on drive IRQs
	*((unsigned short *)v_admctl) &= 3;		// enable aIEN; preserve PIO mode
	iounmap(v_admctl);				// all done; unmap config regs
}

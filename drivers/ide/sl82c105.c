/*
 * linux/drivers/ide/sl82c105.c
 *
 * SL82C105/Winbond 553 IDE driver
 *
 * Maintainer unknown.
 *
 * Drive tuning added from Rebel.com's kernel sources
 *  -- Russell King (15/11/98) linux@arm.linux.org.uk
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/dma.h>

#include "ide_modes.h"

extern char *ide_xfer_verbose (byte xfer_rate);

#ifdef CONFIG_ARCH_NETWINDER
/*
 * Convert a PIO mode and cycle time to the required on/off
 * times for the interface.  This has protection against run-away
 * timings.
 */
static unsigned int get_timing_sl82c105(ide_pio_data_t *p)
{
	unsigned int cmd_on;
	unsigned int cmd_off;

	cmd_on = (ide_pio_timings[p->pio_mode].active_time + 29) / 30;
	cmd_off = (p->cycle_time - 30 * cmd_on + 29) / 30;

	if (cmd_on > 32)
		cmd_on = 32;
	if (cmd_on == 0)
		cmd_on = 1;

	if (cmd_off > 32)
		cmd_off = 32;
	if (cmd_off == 0)
		cmd_off = 1;

	return (cmd_on - 1) << 8 | (cmd_off - 1) | (p->use_iordy ? 0x40 : 0x00);
}

/*
 * We only deal with PIO mode here - DMA mode 'using_dma' is not
 * initialised at the point that this function is called.
 */
static void tune_sl82c105(ide_drive_t *drive, byte pio)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = hwif->pci_dev;
	ide_pio_data_t p;
	unsigned short drv_ctrl = 0x909;
	unsigned int xfer_mode, reg;

	reg = (hwif->channel ? 0x4c : 0x44) + (drive->select.b.unit ? 4 : 0);

	pio = ide_get_best_pio_mode(drive, pio, 5, &p);

	switch (pio) {
	default:
	case 0:		xfer_mode = XFER_PIO_0;		break;
	case 1:		xfer_mode = XFER_PIO_1;		break;
	case 2:		xfer_mode = XFER_PIO_2;		break;
	case 3:		xfer_mode = XFER_PIO_3;		break;
	case 4:		xfer_mode = XFER_PIO_4;		break;
	}

	if (ide_config_drive_speed(drive, xfer_mode) == 0)
		drv_ctrl = get_timing_sl82c105(&p);

	pci_write_config_word(dev, reg, drv_ctrl);
	pci_read_config_word(dev, reg, &drv_ctrl);

	printk("%s: selected %s (%dns) (%04X)\n", drive->name,
	       ide_xfer_verbose(xfer_mode), p.cycle_time, drv_ctrl);
}
#endif

void __init ide_dmacapable_sl82c105(ide_hwif_t *hwif, unsigned long dmabase)
{
	unsigned char rev;

	pci_read_config_byte(hwif->pci_dev, PCI_REVISION_ID, &rev);

	if (rev <= 5) {
		hwif->autodma = 0;
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		printk("    %s: revision %d, Bus-Master DMA disabled\n",
		       hwif->name, rev);
	}
	ide_setup_dma(hwif, dmabase, 8);
}

void __init ide_init_sl82c105(ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;

#ifdef CONFIG_ARCH_NETWINDER
	unsigned char ctrl_stat;

	pci_read_config_byte(dev, 0x40, &ctrl_stat);
	pci_write_config_byte(dev, 0x40, ctrl_stat | 0x33);

	hwif->tuneproc = tune_sl82c105;
#else
	unsigned short t16;
	unsigned int t32;
	pci_read_config_word(dev, PCI_COMMAND, &t16);
	printk("SL82C105 command word: %x\n",t16);
        t16 |= PCI_COMMAND_IO;
        pci_write_config_word(dev, PCI_COMMAND, t16);
	/* IDE timing */
	pci_read_config_dword(dev, 0x44, &t32);
	printk("IDE timing: %08x, resetting to PIO0 timing\n",t32);
	pci_write_config_dword(dev, 0x44, 0x03e4);
#ifndef CONFIG_MBX
	pci_read_config_dword(dev, 0x40, &t32);
	printk("IDE control/status register: %08x\n",t32);
	pci_write_config_dword(dev, 0x40, 0x10ff08a1);
#endif /* CONFIG_MBX */
#endif
}

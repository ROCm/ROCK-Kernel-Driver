/*
 *  linux/drivers/ide/dtc2278.c		Version 0.02	Feb 10, 1996
 *
 *  Copyright (C) 1996  Linus Torvalds & author (see below)
 */

#undef REALLY_SLOW_IO           /* most systems can safely undef this */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#include "ide_modes.h"

/*
 * Changing this #undef to #define may solve start up problems in some systems.
 */
#undef ALWAYS_SET_DTC2278_PIO_MODE

/*
 * From: andy@cercle.cts.com (Dyan Wile)
 *
 * Below is a patch for DTC-2278 - alike software-programmable controllers
 * The code enables the secondary IDE controller and the PIO4 (3?) timings on
 * the primary (EIDE). You may probably have to enable the 32-bit support to
 * get the full speed. You better get the disk interrupts disabled ( hdparm -u0
 * /dev/hd.. ) for the drives connected to the EIDE interface. (I get my
 * filesystem  corrupted with -u1, but under heavy disk load only :-)
 *
 * This card is now forced to use the "serialize" feature,
 * and irq-unmasking is disallowed.  If io_32bit is enabled,
 * it must be done for BOTH drives on each interface.
 *
 * This code was written for the DTC2278E, but might work with any of these:
 *
 * DTC2278S has only a single IDE interface.
 * DTC2278D has two IDE interfaces and is otherwise identical to the S version.
 * DTC2278E also has serial ports and a printer port
 * DTC2278EB: has onboard BIOS, and "works like a charm" -- Kent Bradford <kent@theory.caltech.edu>
 *
 * There may be a fourth controller type. The S and D versions use the
 * Winbond chip, and I think the E version does also.
 *
 */

static void sub22 (char b, char c)
{
	int i;

	for(i = 0; i < 3; ++i) {
		inb(0x3f6);
		outb_p(b,0xb0);
		inb(0x3f6);
		outb_p(c,0xb4);
		inb(0x3f6);
		if(inb(0xb4) == c) {
			outb_p(7,0xb0);
			inb(0x3f6);
			return;	/* success */
		}
	}
}

static void tune_dtc2278 (ide_drive_t *drive, byte pio)
{
	unsigned long flags;

	pio = ide_get_best_pio_mode(drive, pio, 4, NULL);

	if (pio >= 3) {
		save_flags(flags);	/* all CPUs */
		cli();			/* all CPUs */
		/*
		 * This enables PIO mode4 (3?) on the first interface
		 */
		sub22(1,0xc3);
		sub22(0,0xa0);
		restore_flags(flags);	/* all CPUs */
	} else {
		/* we don't know how to set it back again.. */
	}

	/*
	 * 32bit I/O has to be enabled for *both* drives at the same time.
	 */
	drive->io_32bit = 1;
	HWIF(drive)->drives[!drive->select.b.unit].io_32bit = 1;
}

void __init init_dtc2278 (void)
{
	unsigned long flags;

	__save_flags(flags);	/* local CPU only */
	__cli();		/* local CPU only */
	/*
	 * This enables the second interface
	 */
	outb_p(4,0xb0);
	inb(0x3f6);
	outb_p(0x20,0xb4);
	inb(0x3f6);
#ifdef ALWAYS_SET_DTC2278_PIO_MODE
	/*
	 * This enables PIO mode4 (3?) on the first interface
	 * and may solve start-up problems for some people.
	 */
	sub22(1,0xc3);
	sub22(0,0xa0);
#endif
	__restore_flags(flags);	/* local CPU only */

	ide_hwifs[0].serialized = 1;
	ide_hwifs[1].serialized = 1;
	ide_hwifs[0].chipset = ide_dtc2278;
	ide_hwifs[1].chipset = ide_dtc2278;
	ide_hwifs[0].tuneproc = &tune_dtc2278;
	ide_hwifs[0].drives[0].no_unmask = 1;
	ide_hwifs[0].drives[1].no_unmask = 1;
	ide_hwifs[1].drives[0].no_unmask = 1;
	ide_hwifs[1].drives[1].no_unmask = 1;
	ide_hwifs[0].mate = &ide_hwifs[1];
	ide_hwifs[1].mate = &ide_hwifs[0];
	ide_hwifs[1].channel = 1;
}

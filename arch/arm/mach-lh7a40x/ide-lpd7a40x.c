/* arch/arm/mach-lh7a40x/ide-lpd7a40x.c
 *
 *  Copyright (C) 2004 Logic Product Development
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */


#include <linux/config.h>
#include <linux/ide.h>

#include <asm/io.h>

#define IOBARRIER_READ		readl (IOBARRIER_VIRT)

static u8 lpd7a40x_ide_inb (unsigned long port)
{
	u16 v = (u16) readw (port & ~0x1);
	IOBARRIER_READ;
	if (port & 0x1)
		v >>= 8;
	return v & 0xff;
}

static u16 lpd7a40x_ide_inw (unsigned long port)
{
	u16 v = (u16) readw (port);
	IOBARRIER_READ;
	return v;
}

static void lpd7a40x_ide_insw (unsigned long port, void *addr, u32 count)
{
	while (count--) {
		*((u16*) addr)++ = (u16) readw (port);
		IOBARRIER_READ;
	}
}

static u32 lpd7a40x_ide_inl (unsigned long port)
{
	u32 v = (u16) readw (port);
	IOBARRIER_READ;
	v |= (u16) readw (port + 2);
	IOBARRIER_READ;

	return v;
}

static void lpd7a40x_ide_insl (unsigned long port, void *addr, u32 count)
{
	while (count--) {
		*((u16*) addr)++ = (u16) readw (port);
		IOBARRIER_READ;
		*((u16*) addr)++ = (u16) readw (port + 2);
		IOBARRIER_READ;
	}
}

/* lpd7a40x_ide_outb -- this function is complicated by the fact that
 * the user wants to be able to do byte IO and the hardware cannot.
 * In order to write the high byte, we need to write a short.  So, we
 * read before writing in order to maintain the register values that
 * shouldn't change.  This isn't a good idea for the data IO registers
 * since reading from them will not return the current value.  We
 * expect that this function handles the control register adequately.
*/

static void lpd7a40x_ide_outb (u8 valueUser, unsigned long port)
{
	/* Block writes to SELECT register.  Draconian, but the only
	 * way to cope with this hardware configuration without
	 * modifying the SELECT_DRIVE call in the ide driver. */
	if ((port & 0xf) == 0x6)
		return;

	if (port & 0x1) {	/* Perform read before write.  Only
				 * the COMMAND register needs
				 * this.  */
		u16 value = (u16) readw (port & ~0x1);
		IOBARRIER_READ;
		value = (value & 0x00ff) | (valueUser << 8);
		writew (value, port & ~0x1);
		IOBARRIER_READ;
	}
	else {			/* Allow low-byte writes which seem to
				 * be OK. */
		writeb (valueUser, port);
		IOBARRIER_READ;
	}
}

static void lpd7a40x_ide_outbsync (ide_drive_t *drive, u8 value,
				   unsigned long port)
{
	lpd7a40x_ide_outb (value, port);
}

static void lpd7a40x_ide_outw (u16 value, unsigned long port)
{
	writew (value, port);
	IOBARRIER_READ;
}

static void lpd7a40x_ide_outsw (unsigned long port, void *addr, u32 count)
{
	while (count-- > 0) {
		writew (*((u16*) addr)++, port);
		IOBARRIER_READ;
	}
}

static void lpd7a40x_ide_outl (u32 value, unsigned long port)
{
	writel (value, port);
	IOBARRIER_READ;
}

static void lpd7a40x_ide_outsl (unsigned long port, void *addr, u32 count)
{
	while (count-- > 0) {
		writel (*((u32*) addr)++, port);
		IOBARRIER_READ;
	}
}

void lpd7a40x_SELECT_DRIVE (ide_drive_t *drive)
{
	unsigned jifStart = jiffies;
#define WAIT_TIME	(30*HZ/1000)

	/* Check for readiness. */
	while ((HWIF(drive)->INB(IDE_STATUS_REG) & 0x40) == 0)
		if (jifStart <= jiffies + WAIT_TIME)
			return;

	/* Only allow one drive.
	   For more information, see Documentation/arm/Sharp-LH/ */
	if (drive->select.all & (1<<4))
		return;

	/* OUTW so that the IDLE_IMMEDIATE (and not NOP) command is sent. */
	HWIF(drive)->OUTW(drive->select.all | 0xe100, IDE_SELECT_REG);
}

void lpd7a40x_hwif_ioops (ide_hwif_t *hwif)
{
	hwif->mmio      = 2;	/* Just for show */
	hwif->irq	= IDE_NO_IRQ;	/* Stop this probing */

	hwif->OUTB	= lpd7a40x_ide_outb;
	hwif->OUTBSYNC	= lpd7a40x_ide_outbsync;
	hwif->OUTW	= lpd7a40x_ide_outw;
	hwif->OUTL	= lpd7a40x_ide_outl;
	hwif->OUTSW	= lpd7a40x_ide_outsw;
	hwif->OUTSL	= lpd7a40x_ide_outsl;
	hwif->INB	= lpd7a40x_ide_inb;
	hwif->INW	= lpd7a40x_ide_inw;
	hwif->INL	= lpd7a40x_ide_inl;
	hwif->INSW	= lpd7a40x_ide_insw;
	hwif->INSL	= lpd7a40x_ide_insl;
	hwif->selectproc = lpd7a40x_SELECT_DRIVE;
}

/*
 * Low-level hardware access stuff for Cobalt Microserver 27 board.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997 by Ralf Baechle
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <asm/cachectl.h>
#include <asm/cobalt.h>
#include <asm/segment.h>
#include <asm/vector.h>

void
dummy(void)
{
	panic("What the hell is this called for?");
}

static unsigned char cobalt_read_cmos(unsigned long reg)
{
	unsigned char retval;

	VIA_PORT_WRITE(0x70, reg);
	retval = VIA_PORT_READ(0x71);
	VIA_DELAY();

	return retval;
}

static void cobalt_write_cmos(unsigned char val, unsigned long reg)
{
	VIA_PORT_WRITE(0x70, reg);
	VIA_PORT_WRITE(0x71, val); 
}

struct feature cobalt_feature = {
	/*
	 * How to access the floppy controller's ports
	 */
	(void *)dummy, (void *)dummy,
	/*
	 * How to access the floppy DMA functions.
	 */
	(void *)dummy, (void *)dummy, (void *)dummy, (void *)dummy,
	(void *)dummy, (void *)dummy, (void *)dummy, (void *)dummy,
	(void *)dummy, (void *)dummy, (void *)dummy,
	/*
	 * How to access the RTC functions.
	 */
	cobalt_read_cmos,
	cobalt_write_cmos
};

/*
 *	ide-default		-	Driver for unbound ide devices
 *
 *	This provides a clean way to bind a device to default operations
 *	by having an actual driver class that rather than special casing
 *	"no driver" all over the IDE code
 *
 *	Copyright (C) 2003, Red Hat <alan@redhat.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/slab.h>
#include <linux/cdrom.h>
#include <linux/ide.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/unaligned.h>
#include <asm/bitops.h>

#define IDEDEFAULT_VERSION	"0.9.newide"
/*
 *	Driver initialization.
 */

static int idedefault_attach(ide_drive_t *drive);

/*
 *	IDE subdriver functions, registered with ide.c
 *
 *	idedefault *must* support DMA because it will be
 *	attached before the other drivers are loaded and
 *	we don't want to lose the DMA status at probe
 *	time.
 */

ide_driver_t idedefault_driver = {
	.name		=	"ide-default",
	.version	=	IDEDEFAULT_VERSION,
	.attach		=	idedefault_attach,
	.supports_dma	=	1,
	.drives		=	LIST_HEAD_INIT(idedefault_driver.drives)
};

static int idedefault_attach (ide_drive_t *drive)
{
	if (ide_register_subdriver(drive,
			&idedefault_driver, IDE_SUBDRIVER_VERSION)) {
		printk(KERN_ERR "ide-default: %s: Failed to register the "
			"driver with ide.c\n", drive->name);
		return 1;
	}
	return 0;
}

MODULE_DESCRIPTION("IDE Default Driver");

MODULE_LICENSE("GPL");

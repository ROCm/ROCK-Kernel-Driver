/**** vi:set ts=8 sts=8 sw=8:************************************************
 *
 *  Copyright (C) 2002 Marcin Dalecki <martin@dalecki.de>
 *
 *  Copyright (c) 1999-2000  Andre Hedrick <andre@linux-ide.org>
 *  Copyright (c) 1995-1998  Mark Lord
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/*
 *  Just the black and white list handling for BM-DMA operation.
 */

#include <linux/config.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

#ifdef CONFIG_IDEDMA_NEW_DRIVE_LISTINGS

struct drive_list_entry {
	char * id_model;
	char * id_firmware;
};

struct drive_list_entry drive_whitelist[] = {
	{ "Micropolis 2112A", NULL },
	{ "CONNER CTMA 4000", NULL },
	{ "CONNER CTT8000-A", NULL },
	{ "ST34342A", NULL },
	{ NULL, NULL }
};

struct drive_list_entry drive_blacklist[] = {

	{ "WDC AC11000H", NULL },
	{ "WDC AC22100H", NULL },
	{ "WDC AC32500H", NULL },
	{ "WDC AC33100H", NULL },
	{ "WDC AC31600H", NULL },
	{ "WDC AC32100H", "24.09P07" },
	{ "WDC AC23200L", "21.10N21" },
	{ "Compaq CRD-8241B", NULL },
	{ "CRD-8400B", NULL },
	{ "CRD-8480B", NULL },
	{ "CRD-8480C", NULL },
	{ "CRD-8482B", NULL },
	{ "CRD-84", NULL },
	{ "SanDisk SDP3B", NULL },
	{ "SanDisk SDP3B-64", NULL },
	{ "SANYO CD-ROM CRD", NULL },
	{ "HITACHI CDR-8", NULL },
	{ "HITACHI CDR-8335", NULL },
	{ "HITACHI CDR-8435", NULL },
	{ "Toshiba CD-ROM XM-6202B", NULL },
	{ "CD-532E-A", NULL },
	{ "E-IDE CD-ROM CR-840", NULL },
	{ "CD-ROM Drive/F5A", NULL },
	{ "RICOH CD-R/RW MP7083A", NULL },
	{ "WPI CDD-820", NULL },
	{ "SAMSUNG CD-ROM SC-148C", NULL },
	{ "SAMSUNG CD-ROM SC-148F", NULL },
	{ "SAMSUNG CD-ROM SC", NULL },
	{ "SanDisk SDP3B-64", NULL },
	{ "SAMSUNG CD-ROM SN-124", NULL },
	{ "PLEXTOR CD-R PX-W8432T", NULL },
	{ "ATAPI CD-ROM DRIVE 40X MAXIMUM", NULL },
	{ "_NEC DV5800A", NULL },
	{ NULL,	NULL }

};

static int in_drive_list(struct hd_driveid *id, struct drive_list_entry * drive_table)
{
	for ( ; drive_table->id_model ; drive_table++)
		if ((!strcmp(drive_table->id_model, id->model)) &&
		    ((drive_table->id_firmware && !strstr(drive_table->id_firmware, id->fw_rev)) ||
		     (!drive_table->id_firmware)))
			return 1;
	return 0;
}

#else

/*
 * good_dma_drives() lists the model names (from "hdparm -i")
 * of drives which do not support mode2 DMA but which are
 * known to work fine with this interface under Linux.
 */
const char *good_dma_drives[] = {"Micropolis 2112A",
				 "CONNER CTMA 4000",
				 "CONNER CTT8000-A",
				 "ST34342A",	/* for Sun Ultra */
				 NULL};

/*
 * bad_dma_drives() lists the model names (from "hdparm -i")
 * of drives which supposedly support (U)DMA but which are
 * known to corrupt data with this interface under Linux.
 *
 * This is an empirical list. Its generated from bug reports. That means
 * while it reflects actual problem distributions it doesn't answer whether
 * the drive or the controller, or cabling, or software, or some combination
 * thereof is the fault. If you don't happen to agree with the kernel's
 * opinion of your drive - use hdparm to turn DMA on.
 */
const char *bad_dma_drives[] = {"WDC AC11000H",
				"WDC AC22100H",
				"WDC AC32100H",
				"WDC AC32500H",
				"WDC AC33100H",
				"WDC AC31600H",
				NULL};

#endif

/*
 *  For both Blacklisted and Whitelisted drives.
 *  This is setup to be called as an extern for future support
 *  to other special driver code.
 */
int check_drive_lists(struct ata_device *drive, int good_bad)
{
	struct hd_driveid *id = drive->id;

#ifdef CONFIG_IDEDMA_NEW_DRIVE_LISTINGS
	if (good_bad) {
		return in_drive_list(id, drive_whitelist);
	} else {
		int blacklist = in_drive_list(id, drive_blacklist);
		if (blacklist)
			printk("%s: Disabling (U)DMA for %s\n", drive->name, id->model);
		return(blacklist);
	}
#else
	const char **list;

	if (good_bad) {
		/* Consult the list of known "good" drives */
		list = good_dma_drives;
		while (*list) {
			if (!strcmp(*list++, id->model))
				return 1;
		}
	} else {
		/* Consult the list of known "bad" drives */
		list = bad_dma_drives;
		while (*list) {
			if (!strcmp(*list++, id->model)) {
				printk("%s: Disabling (U)DMA for %s\n",
					drive->name, id->model);
				return 1;
			}
		}
	}
#endif
	return 0;
}

void udma_print(struct ata_device *drive)
{
#ifdef CONFIG_ARCH_ACORN
	printk(", DMA");
#else
	struct hd_driveid *id = drive->id;
	char *str = NULL;

	if ((id->field_valid & 4) && (eighty_ninty_three(drive)) &&
	    (id->dma_ultra & (id->dma_ultra >> 14) & 3)) {
		if ((id->dma_ultra >> 15) & 1)
			str = ", UDMA(mode 7)";	/* UDMA BIOS-enabled! */
		else
			str = ", UDMA(133)";	/* UDMA BIOS-enabled! */
	} else if ((id->field_valid & 4) && (eighty_ninty_three(drive)) &&
		  (id->dma_ultra & (id->dma_ultra >> 11) & 7)) {
		if ((id->dma_ultra >> 13) & 1) {
			str = ", UDMA(100)";	/* UDMA BIOS-enabled! */
		} else if ((id->dma_ultra >> 12) & 1) {
			str = ", UDMA(66)";	/* UDMA BIOS-enabled! */
		} else {
			str = ", UDMA(44)";	/* UDMA BIOS-enabled! */
		}
	} else if ((id->field_valid & 4) &&
		   (id->dma_ultra & (id->dma_ultra >> 8) & 7)) {
		if ((id->dma_ultra >> 10) & 1) {
			str = ", UDMA(33)";	/* UDMA BIOS-enabled! */
		} else if ((id->dma_ultra >> 9) & 1) {
			str = ", UDMA(25)";	/* UDMA BIOS-enabled! */
		} else {
			str = ", UDMA(16)";	/* UDMA BIOS-enabled! */
		}
	} else if (id->field_valid & 4)
		str = ", (U)DMA";	/* Can be BIOS-enabled! */
	else
		str = ", DMA";

	printk(str);
#endif
}

/*
 * Drive back/white list handling for UDMA capability:
 */

int udma_black_list(struct ata_device *drive)
{
	return check_drive_lists(drive, 0);
}

int udma_white_list(struct ata_device *drive)
{
	return check_drive_lists(drive, 1);
}

EXPORT_SYMBOL(udma_print);
EXPORT_SYMBOL(udma_black_list);
EXPORT_SYMBOL(udma_white_list);

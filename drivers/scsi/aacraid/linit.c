/*
 *	Adaptec AAC series RAID controller driver
 *	(c) Copyright 2001 Red Hat Inc.	<alan@redhat.com>
 *
 * based on the old aacraid driver that is..
 * Adaptec aacraid device driver for Linux.
 *
 * Copyright (c) 2000 Adaptec, Inc. (aacraid@adaptec.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Module Name:
 *   linit.c
 *
 * Abstract: Linux Driver entry module for Adaptec RAID Array Controller
 */

#define AAC_DRIVER_VERSION		0x01010005
#define AAC_DRIVER_BUILD_DATE		__DATE__ " " __TIME__
#define AAC_DRIVER_NAME			"aacraid"

#include <linux/compat.h>
#include <linux/blkdev.h>
#include <linux/completion.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/ioctl.h>
#include <asm/semaphore.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsicam.h>
#include <scsi/scsi_eh.h>
#ifdef CONFIG_COMPAT
  /* Cast the function, since sys_ioctl does not match */
# define aac_ioctl32(x,y) register_ioctl32_conversion((x), \
    (int(*)(unsigned int,unsigned int,unsigned long,struct file*))(y))
#endif

#include "aacraid.h"


MODULE_AUTHOR("Red Hat Inc and Adaptec");
MODULE_DESCRIPTION("Dell PERC2, 2/Si, 3/Si, 3/Di, "
		   "Adaptec Advanced Raid Products, "
		   "and HP NetRAID-4M SCSI driver");
MODULE_LICENSE("GPL");
/*
 * Following bears malice and forethought regarding
 * MODULE_VERSION, MODULE_INFO and __MODULE_INFO definitions
 * because you can not get from here to there without this knowledge.
 * This could break in the future ...
 */
#ifdef MODULE
# if ((AAC_DRIVER_VERSION>>24)&0xFF) >= 10
#  define AAC_DRIVER_VERSION_DIGIT1 ((((AAC_DRIVER_VERSION>>24)&0xFF)/10)%10)+'0',
# else
#  define AAC_DRIVER_VERSION_DIGIT1
# endif
# define AAC_DRIVER_VERSION_DIGIT2 (((AAC_DRIVER_VERSION>>24)&0xFF)%10)+'0', '.',
# if ((AAC_DRIVER_VERSION>>16)&0xFF) >= 10
#  define AAC_DRIVER_VERSION_DIGIT3 ((((AAC_DRIVER_VERSION>>16)&0xFF)/10)%10)+'0',
# else
#  define AAC_DRIVER_VERSION_DIGIT3
# endif
# define AAC_DRIVER_VERSION_DIGIT4 (((AAC_DRIVER_VERSION>>16)&0xFF)%10)+'0', '.',
# if (AAC_DRIVER_VERSION&0xFF) >= 10
#  define AAC_DRIVER_VERSION_DIGIT5 (((AAC_DRIVER_VERSION&0xFF)/10)%10)+'0',
# else
#  define AAC_DRIVER_VERSION_DIGIT5
# endif
# define AAC_DRIVER_VERSION_DIGIT6 ((AAC_DRIVER_VERSION&0xFF)%10)+'0',
# if (defined(AAC_DRIVER_BUILD))
#  define AAC_DRIVER_BUILD_DIGIT '-', \
	((AAC_DRIVER_BUILD/1000)%10)+'0', \
	((AAC_DRIVER_BUILD/100)%10)+'0', \
	((AAC_DRIVER_BUILD/10)%10)+'0', \
	(AAC_DRIVER_BUILD%10)+'0',
# else
#  define AAC_DRIVER_BUILD_DIGIT
# endif
# define AAC_DRIVER_SIGNATURE '\0', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', \
	'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x', \
	'x', 'x', '\0'
  static const char __module_cat(version,__LINE__)[]
    __attribute_used__
    __attribute__((section(".modinfo"),unused)) = {
    'v', 'e', 'r', 's', 'i', 'o', 'n', '=',
    AAC_DRIVER_VERSION_DIGIT1 AAC_DRIVER_VERSION_DIGIT2
    AAC_DRIVER_VERSION_DIGIT3 AAC_DRIVER_VERSION_DIGIT4
    AAC_DRIVER_VERSION_DIGIT5 AAC_DRIVER_VERSION_DIGIT6
    AAC_DRIVER_BUILD_DIGIT AAC_DRIVER_SIGNATURE };
#endif

struct aac_dev *aac_devices[MAXIMUM_NUM_ADAPTERS];
static unsigned aac_count;
static int aac_cfg_major = -1;
unsigned long aac_driver_version = AAC_DRIVER_VERSION | 0x00000400;

/*
 * Because of the way Linux names scsi devices, the order in this table has
 * become important.  Check for on-board Raid first, add-in cards second.
 *
 * Note: The last field is used to index into aac_drivers below.
 */
static struct pci_device_id aac_pci_tbl[] = {
	{ 0x1028, 0x0001, 0x1028, 0x0001, 0, 0, 0 }, /* PERC 2/Si (Iguana/PERC2Si) */
	{ 0x1028, 0x0002, 0x1028, 0x0002, 0, 0, 1 }, /* PERC 3/Di (Opal/PERC3Di) */
	{ 0x1028, 0x0003, 0x1028, 0x0003, 0, 0, 2 }, /* PERC 3/Si (SlimFast/PERC3Si */
	{ 0x1028, 0x0004, 0x1028, 0x00d0, 0, 0, 3 }, /* PERC 3/Di (Iguana FlipChip/PERC3DiF */
	{ 0x1028, 0x0002, 0x1028, 0x00d1, 0, 0, 4 }, /* PERC 3/Di (Viper/PERC3DiV) */
	{ 0x1028, 0x0002, 0x1028, 0x00d9, 0, 0, 5 }, /* PERC 3/Di (Lexus/PERC3DiL) */
	{ 0x1028, 0x000a, 0x1028, 0x0106, 0, 0, 6 }, /* PERC 3/Di (Jaguar/PERC3DiJ) */
	{ 0x1028, 0x000a, 0x1028, 0x011b, 0, 0, 7 }, /* PERC 3/Di (Dagger/PERC3DiD) */
	{ 0x1028, 0x000a, 0x1028, 0x0121, 0, 0, 8 }, /* PERC 3/Di (Boxster/PERC3DiB) */
	{ 0x9005, 0x0283, 0x9005, 0x0283, 0, 0, 9 }, /* catapult */
	{ 0x9005, 0x0284, 0x9005, 0x0284, 0, 0, 10 }, /* tomcat */
	{ 0x9005, 0x0285, 0x9005, 0x0286, 0, 0, 11 }, /* Adaptec 2120S (Crusader) */
	{ 0x9005, 0x0285, 0x9005, 0x0285, 0, 0, 12 }, /* Adaptec 2200S (Vulcan) */
	{ 0x9005, 0x0285, 0x9005, 0x0287, 0, 0, 13 }, /* Adaptec 2200S (Vulcan-2m) */
	{ 0x9005, 0x0285, 0x17aa, 0x0286, 0, 0, 14 }, /* Legend S220 (Legend Crusader) */
	{ 0x9005, 0x0285, 0x17aa, 0x0287, 0, 0, 15 }, /* Legend S230 (Legend Vulcan) */

	{ 0x9005, 0x0285, 0x9005, 0x0288, 0, 0, 16 }, /* Adaptec 3230S (Harrier) */
	{ 0x9005, 0x0285, 0x9005, 0x0289, 0, 0, 17 }, /* Adaptec 3240S (Tornado) */
	{ 0x9005, 0x0285, 0x9005, 0x028a, 0, 0, 18 }, /* ASR-2020ZCR SCSI PCI-X ZCR (Skyhawk) */
	{ 0x9005, 0x0285, 0x9005, 0x028b, 0, 0, 19 }, /* ASR-2025ZCR SCSI SO-DIMM PCI-X ZCR (Terminator) */
	{ 0x9005, 0x0286, 0x9005, 0x028c, 0, 0, 20 }, /* ASR-2230S + ASR-2230SLP PCI-X (Lancer) */
	{ 0x9005, 0x0286, 0x9005, 0x028d, 0, 0, 21 }, /* ASR-2130S (Lancer) */
	{ 0x9005, 0x0286, 0x9005, 0x0800, 0, 0, 22 }, /* Jupiter Platform */
	{ 0x9005, 0x0285, 0x9005, 0x028e, 0, 0, 23 }, /* ASR-2020SA SATA PCI-X ZCR (Skyhawk) */
	{ 0x9005, 0x0285, 0x9005, 0x028f, 0, 0, 24 }, /* ASR-2025SA SATA SO-DIMM PCI-X ZCR (Terminator) */
	{ 0x9005, 0x0285, 0x9005, 0x0290, 0, 0, 25 }, /* AAR-2410SA PCI SATA 4ch (Jaguar II) */
	{ 0x9005, 0x0285, 0x1028, 0x0291, 0, 0, 26 }, /* CERC SATA RAID 2 PCI SATA 6ch (DellCorsair) */
	{ 0x9005, 0x0285, 0x9005, 0x0292, 0, 0, 27 }, /* AAR-2810SA PCI SATA 8ch (Corsair-8) */
	{ 0x9005, 0x0285, 0x9005, 0x0293, 0, 0, 28 }, /* AAR-21610SA PCI SATA 16ch (Corsair-16) */
	{ 0x9005, 0x0285, 0x9005, 0x0294, 0, 0, 29 }, /* ESD SO-DIMM PCI-X SATA ZCR (Prowler) */
	{ 0x9005, 0x0285, 0x0E11, 0x0295, 0, 0, 30 }, /* AAR-2610SA PCI SATA 6ch */
	{ 0x9005, 0x0285, 0x9005, 0x0296, 0, 0, 31 }, /* ASR-2240S */
	{ 0x9005, 0x0285, 0x9005, 0x0296, 0, 0, 32 }, /* ASR-4005SAS */
	{ 0x9005, 0x0285, 0x9005, 0x0296, 0, 0, 33 }, /* ASR-4000SAS */
	{ 0x9005, 0x0285, 0x9005, 0x0296, 0, 0, 34 }, /* ASR-4800SAS */
	{ 0x9005, 0x0285, 0x9005, 0x0296, 0, 0, 35 }, /* ASR-4805SAS */

	{ 0x9005, 0x0285, 0x1028, 0x0287, 0, 0, 36 }, /* Perc 320/DC*/
	{ 0x1011, 0x0046, 0x9005, 0x0365, 0, 0, 37 }, /* Adaptec 5400S (Mustang)*/
	{ 0x1011, 0x0046, 0x9005, 0x0364, 0, 0, 38 }, /* Adaptec 5400S (Mustang)*/
	{ 0x1011, 0x0046, 0x9005, 0x1364, 0, 0, 39 }, /* Dell PERC2/QC */
	{ 0x1011, 0x0046, 0x103c, 0x10c2, 0, 0, 40 }, /* HP NetRAID-4M */

	{ 0x9005, 0x0285, 0x1028, PCI_ANY_ID, 0, 0, 41 }, /* Dell Catchall */
	{ 0x9005, 0x0285, 0x17aa, PCI_ANY_ID, 0, 0, 42 }, /* Legend Catchall */
	{ 0x9005, 0x0285, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 43 }, /* Adaptec Catch All */
	{ 0x9005, 0x0286, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 44 }, /* Adaptec Rocket Catch All */
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, aac_pci_tbl);

/*
 * dmb - For now we add the number of channels to this structure.  
 * In the future we should add a fib that reports the number of channels
 * for the card.  At that time we can remove the channels from here
 */
static struct aac_driver_ident aac_drivers[] = {
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 2/Si (Iguana/PERC2Si) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 3/Di (Opal/PERC3Di) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 3/Si (SlimFast/PERC3Si */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 3/Di (Iguana FlipChip/PERC3DiF */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 3/Di (Viper/PERC3DiV) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 3/Di (Lexus/PERC3DiL) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 1, AAC_QUIRK_31BIT }, /* PERC 3/Di (Jaguar/PERC3DiJ) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 3/Di (Dagger/PERC3DiD) */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 3/Di (Boxster/PERC3DiB) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "catapult        ", 2, AAC_QUIRK_31BIT }, /* catapult */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "tomcat          ", 2, AAC_QUIRK_31BIT }, /* tomcat */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 2120S   ", 1, AAC_QUIRK_31BIT }, /* Adaptec 2120S (Crusader) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 2200S   ", 2, AAC_QUIRK_31BIT }, /* Adaptec 2200S (Vulcan) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 2200S   ", 2, AAC_QUIRK_31BIT }, /* Adaptec 2200S (Vulcan-2m) */
	{ aac_rx_init, "aacraid",  "Legend  ", "Legend S220     ", 1, AAC_QUIRK_31BIT }, /* Legend S220 (Legend Crusader) */
	{ aac_rx_init, "aacraid",  "Legend  ", "Legend S230     ", 2, AAC_QUIRK_31BIT }, /* Legend S230 (Legend Vulcan) */

	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 3230S   ", 2 }, /* Adaptec 3230S (Harrier) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 3240S   ", 2 }, /* Adaptec 3240S (Tornado) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2020ZCR     ", 2 }, /* ASR-2020ZCR SCSI PCI-X ZCR (Skyhawk) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2025ZCR     ", 2 }, /* ASR-2025ZCR SCSI SO-DIMM PCI-X ZCR (Terminator) */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "ASR-2230S PCI-X ", 2 }, /* ASR-2230S + ASR-2230SLP PCI-X (Lancer) */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "ASR-2130S PCI-X ", 1 }, /* ASR-2130S (Lancer) */
	{ aac_rkt_init, "aacraid",  "ADAPTEC ", "Callisto        ", 2 }, /* Jupiter Platform */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2020SA       ", 1 }, /* ASR-2020SA SATA PCI-X ZCR (Skyhawk) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2025SA       ", 1 }, /* ASR-2025SA SATA SO-DIMM PCI-X ZCR (Terminator) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "AAR-2410SA SATA ", 1 }, /* AAR-2410SA PCI SATA 4ch (Jaguar II) */
	{ aac_rx_init, "aacraid",  "DELL    ", "CERC SR2        ", 1 }, /* CERC SATA RAID 2 PCI SATA 6ch (DellCorsair) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "AAR-2810SA SATA ", 1 }, /* AAR-2810SA PCI SATA 8ch (Corsair-8) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "AAR-21610SA SATA", 1 }, /* AAR-21610SA PCI SATA 16ch (Corsair-16) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "SO-DIMM SATA ZCR", 1 }, /* ESD SO-DIMM PCI-X SATA ZCR (Prowler) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "AAR-2610SA      ", 1 }, /* SATA 6Ch (Bearcat) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2240S       ", 1 }, /* ASR-2240S */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-4005SAS     ", 1 }, /* ASR-4005SAS */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-4000SAS     ", 1 }, /* ASR-4000SAS */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-4800SAS     ", 1 }, /* ASR-4800SAS */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-4805SAS     ", 1 }, /* ASR-4805SAS */

	{ aac_rx_init, "percraid", "DELL    ", "PERC 320/DC     ", 2, AAC_QUIRK_31BIT }, /* Perc 320/DC*/
	{ aac_sa_init, "aacraid",  "ADAPTEC ", "Adaptec 5400S   ", 4 }, /* Adaptec 5400S (Mustang)*/
	{ aac_sa_init, "aacraid",  "ADAPTEC ", "AAC-364         ", 4 }, /* Adaptec 5400S (Mustang)*/
	{ aac_sa_init, "percraid", "DELL    ", "PERCRAID        ", 4, AAC_QUIRK_31BIT }, /* Dell PERC2/QC */
	{ aac_sa_init, "hpnraid",  "HP      ", "NetRAID         ", 4 }, /* HP NetRAID-4M */

	{ aac_rx_init, "aacraid",  "DELL    ", "RAID            ", 2, AAC_QUIRK_31BIT }, /* Dell Catchall */
	{ aac_rx_init, "aacraid",  "Legend  ", "RAID            ", 2, AAC_QUIRK_31BIT }, /* Legend Catchall */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "RAID            ", 2, AAC_QUIRK_31BIT }, /* Adaptec Catch All */
	{ aac_rkt_init, "aacraid", "ADAPTEC ", "RAID            ", 2 } /* Adaptec Rocket Catch All */
};

#ifdef CONFIG_COMPAT
/* Promote 32 bit apps that call get_next_adapter_fib_ioctl to 64 bit version */
static int aac_get_next_adapter_fib_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct fib_ioctl * f;

	f = compat_alloc_user_space(sizeof(*f));
	if (!access_ok(VERIFY_WRITE, f, sizeof(*f)))
		return -EFAULT;

	clear_user(f, sizeof(*f));
	if(copy_from_user((void *)&f, (void *)arg,
			sizeof(struct fib_ioctl) - sizeof(u32)))
		return -EFAULT;
	return sys_ioctl(fd, cmd, (unsigned long)f);
}
#define sys_ioctl NULL	/* register_ioctl32_conversion defaults to this when NULL passed in as a handler */
#endif

/**
 *	aac_queuecommand	-	queue a SCSI command
 *	@cmd:		SCSI command to queue
 *	@done:		Function to call on command completion
 *
 *	Queues a command for execution by the associated Host Adapter.
 *
 *	TODO: unify with aac_scsi_cmd().
 */ 

static int aac_queuecommand(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *))
{
	cmd->scsi_done = done;
	return (aac_scsi_cmd(cmd) ? FAILED : 0);
} 

/**
 *	aac_info		-	Returns the host adapter name
 *	@shost:		Scsi host to report on
 *
 *	Returns a static string describing the device in question
 */

const char *aac_info(struct Scsi_Host *shost)
{
	struct aac_dev *dev = (struct aac_dev *)shost->hostdata;
	return aac_drivers[dev->cardtype].name;
}

/**
 *	aac_get_driver_ident
 * 	@devtype: index into lookup table
 *
 * 	Returns a pointer to the entry in the driver lookup table.
 */

struct aac_driver_ident* aac_get_driver_ident(int devtype)
{
	return &aac_drivers[devtype];
}

/**
 *	aac_biosparm	-	return BIOS parameters for disk
 *	@sdev: The scsi device corresponding to the disk
 *	@bdev: The block device corresponding to the disk
 *	@capacity: The sector capacity of the disk
 *	@geom: geometry block to fill in
 *
 *	Return the Heads/Sectors/Cylinders BIOS Disk Parameters for Disk.  
 *	The default disk geometry is 64 heads, 32 sectors, and the appropriate 
 *	number of cylinders so as not to exceed drive capacity.  In order for 
 *	disks equal to or larger than 1 GB to be addressable by the BIOS
 *	without exceeding the BIOS limitation of 1024 cylinders, Extended 
 *	Translation should be enabled.   With Extended Translation enabled, 
 *	drives between 1 GB inclusive and 2 GB exclusive are given a disk 
 *	geometry of 128 heads and 32 sectors, and drives above 2 GB inclusive 
 *	are given a disk geometry of 255 heads and 63 sectors.  However, if 
 *	the BIOS detects that the Extended Translation setting does not match 
 *	the geometry in the partition table, then the translation inferred 
 *	from the partition table will be used by the BIOS, and a warning may 
 *	be displayed.
 */
 
static int aac_biosparm(
	struct scsi_device *sdev, struct block_device *bdev, sector_t capacity,
	int *geom)
{
	struct diskparm *param = (struct diskparm *)geom;
	unsigned char *buf;

	dprintk((KERN_DEBUG "aac_biosparm.\n"));

	/*
	 *	Assuming extended translation is enabled - #REVISIT#
	 */
	if (capacity >= 2 * 1024 * 1024) { /* 1 GB in 512 byte sectors */
		if(capacity >= 4 * 1024 * 1024) { /* 2 GB in 512 byte sectors */
			param->heads = 255;
			param->sectors = 63;
		} else {
			param->heads = 128;
			param->sectors = 32;
		}
	} else {
		param->heads = 64;
		param->sectors = 32;
	}

	param->cylinders = cap_to_cyls(capacity, param->heads * param->sectors);

	/*
	 *	Read the first 1024 bytes from the disk device
	 */

	buf = scsi_bios_ptable(bdev);
	/* 
	 *	If the boot sector partition table is valid, search for a partition 
	 *	table entry whose end_head matches one of the standard geometry 
	 *	translations ( 64/32, 128/32, 255/63 ).
	 */
	 
	if(*(unsigned short *)(buf + 0x40) == cpu_to_le16(0xaa55))
	{
		struct partition *first = (struct partition * )buf;
		struct partition *entry = first;
		int saved_cylinders = param->cylinders;
		int num;
		unsigned char end_head, end_sec;

		for(num = 0; num < 4; num++) {
			end_head = entry->end_head;
			end_sec = entry->end_sector & 0x3f;

			if(end_head == 63) {
				param->heads = 64;
				param->sectors = 32;
				break;
			} else if(end_head == 127) {
				param->heads = 128;
				param->sectors = 32;
				break;
			} else if(end_head == 254) {
				param->heads = 255;
				param->sectors = 63;
				break;
			}
			entry++;
		}

		if (num == 4) {
			end_head = first->end_head;
			end_sec = first->end_sector & 0x3f;
		}

		param->cylinders = cap_to_cyls(capacity, param->heads * param->sectors);
		if (num < 4 && end_sec == param->sectors) {
			if (param->cylinders != saved_cylinders)
				dprintk((KERN_DEBUG "Adopting geometry: heads=%d, sectors=%d from partition table %d.\n",
					param->heads, param->sectors, num));
		} else if (end_head > 0 || end_sec > 0) {
			dprintk((KERN_DEBUG "Strange geometry: heads=%d, sectors=%d in partition table %d.\n",
				end_head + 1, end_sec, num));
			dprintk((KERN_DEBUG "Using geometry: heads=%d, sectors=%d.\n",
					param->heads, param->sectors));
		}
	}
	kfree(buf);
	return 0;
}

/**
 *	aac_slave_configure		-	compute queue depths
 *	@sdev:	SCSI device we are considering
 *
 *	Selects queue depths for each target device based on the host adapter's
 *	total capacity and the queue depth supported by the target device.
 *	A queue depth of one automatically disables tagged queueing.
 */

static int aac_slave_configure(struct scsi_device *sdev)
{
	if (sdev->tagged_supported) {
		struct scsi_device * dev;
		struct Scsi_Host * host = sdev->host;
		unsigned num_lsu = 0;
		unsigned num_one = 0;
		unsigned depth;

		__shost_for_each_device(dev, host) {
			if (dev->tagged_supported && (dev->type == 0))
				++num_lsu;
			else
				++num_one;
		}
		if (num_lsu == 0)
			++num_lsu;
		depth = (host->can_queue - num_one) / num_lsu;
		if (depth > 256)
			depth = 256;
		else if (depth < 2)
			depth = 2;
		scsi_adjust_queue_depth(sdev, MSG_ORDERED_TAG, depth);
	} else
		scsi_adjust_queue_depth(sdev, 0, 1);
	return 0;
}

/**
 *	aac_ioctl 	-	Handle SCSI ioctls
 *	@sdev: scsi device to operate upon
 *	@cmd: ioctl command to use issue
 *	@arg: ioctl data pointer
 *
 *	Issue an ioctl on an aacraid device. Returns a standard unix error code or
 *	zero for success
 */
 
/*------------------------------------------------------------------------------
	aac_ioctl()

		Handle SCSI ioctls
 *----------------------------------------------------------------------------*/
static int aac_ioctl(struct scsi_device *sdev, int cmd, void * arg)
{
	struct aac_dev *dev = (struct aac_dev *)sdev->host->hostdata;
	return aac_do_ioctl(dev, cmd, arg);
}

/**
 *	aac_eh_abort	-	Abort command if possible.
 *	@cmd:	SCSI command block to abort
 *
 *	Called when the midlayer wishes to abort a command. We don't support
 *	this facility, and our firmware looks after life for us. We just
 *	report this as failing
 */
static int aac_eh_abort(struct scsi_cmnd *cmd)
{
	return FAILED;
}

/**
 *	aac_eh_reset	- Reset command handling
 *	@scsi_cmd:	SCSI command block causing the reset
 *
 *	Issue a reset of a SCSI host. If things get this bad then arguably we should
 *	go take a look at what the host adapter is doing and see if something really
 *	broke (as can occur at least on my Dell QC card if a drive keeps failing spinup)
 */
#ifdef __arm__
//DEBUG
#define AAC_DEBUG_INSTRUMENT_RESET
#endif
static int aac_eh_reset(struct scsi_cmnd* cmd)
{
#if (!defined(AAC_DEBUG_INSTRUMENT_RESET) && defined(__arm__))
	return FAILED;
#else
	struct scsi_device * dev = cmd->device;
	struct Scsi_Host * host = dev->host;
	struct scsi_cmnd * command;
	int count;
	struct aac_dev * aac;
	unsigned long flags;

	printk(KERN_ERR "%s: Host adapter reset request. SCSI hang ?\n", AAC_DRIVER_NAME);
	if (nblank(dprintk(x))) {
		int active = 0;

		active = active;
		dprintk((KERN_ERR "%s: Outstanding commands on (%d,%d,%d,%d):\n", AAC_DRIVER_NAME, host->host_no, dev->channel, dev->id, dev->lun));
		spin_lock_irqsave(&dev->list_lock, flags);
		list_for_each_entry(command, &dev->cmd_list, list)
		{
			if (command->state != SCSI_STATE_FINISHED)
			dprintk((KERN_ERR "%4d %c%c %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			  active++,
			  (command->serial_number) ? 'A' : 'C',
			  (cmd == command) ? '*' : ' ',
			  command->cmnd[0], command->cmnd[1], command->cmnd[2],
			  command->cmnd[3], command->cmnd[4], command->cmnd[5],
			  command->cmnd[6], command->cmnd[7], command->cmnd[8],
			  command->cmnd[9]));
		}
		spin_unlock_irqrestore(&dev->list_lock, flags);
	}

	aac = (struct aac_dev *)host->hostdata;
	if ((count = aac_adapter_check_health(aac))) {
		/* Fake up an AIF:
		 *	aac_aifcmd.command = AifCmdEventNotify = 1
		 *	aac_aifcmd.seqnum = 0xFFFFFFFF
		 *	aac_aifcmd.data[0] = AifEnExpEvent = 23
		 *	aac_aifcmd.data[1] = AifExeFirmwarePanic = 3
		 *	aac.aifcmd.data[2] = AifHighPriority = 3
		 *	aac.aifcmd.data[3] = count
		 */
		struct list_head *entry;
		u32 time_now = jiffies/HZ;
		unsigned long flagv;
			
		spin_lock_irqsave(&aac->fib_lock, flagv);
		entry = aac->fib_list.next;

		/*
		 * For each Context that is on the 
		 * fibctxList, make a copy of the
		 * fib, and then set the event to wake up the
		 * thread that is waiting for it.
		 */
		while (entry != &aac->fib_list) {
			/*
			 * Extract the fibctx
			 */
			struct aac_fib_context *fibctx = list_entry(entry, struct aac_fib_context, next);
			struct hw_fib * hw_fib;
			struct fib * fib;
			/*
			 * Check if the queue is getting
			 * backlogged
			 */
			if (fibctx->count > 20) {
				/*
				 * It's *not* jiffies folks,
				 * but jiffies / HZ, so do not
				 * panic ...
				 */
				u32 time_last = fibctx->jiffies;
				/*
				 * Has it been > 2 minutes 
				 * since the last read off
				 * the queue?
				 */
				if ((time_now - time_last) > 120) {
					entry = entry->next;
					aac_close_fib_context(aac, fibctx);
					continue;
				}
			}
			/*
			 * Warning: no sleep allowed while
			 * holding spinlock
			 */
			hw_fib = kmalloc(sizeof(struct hw_fib), GFP_ATOMIC);
			fib = kmalloc(sizeof(struct fib), GFP_ATOMIC);
			if (fib && hw_fib) {
				struct aac_aifcmd * aif;
				memset(hw_fib, 0, sizeof(struct hw_fib));
				fib_init(fib);
				memset(fib, 0, sizeof(struct fib));
				fib->type = FSAFS_NTC_FIB_CONTEXT;
				fib->size = sizeof (struct fib);
				fib->hw_fib = hw_fib;
				fib->data = hw_fib->data;
				fib->dev = aac;
				aif = (struct aac_aifcmd *)hw_fib->data;
				aif->command = AifCmdEventNotify;
			 	aif->seqnum = 0xFFFFFFFF;
			 	aif->data[0] = AifEnExpEvent;
				aif->data[1] = AifExeFirmwarePanic;
			 	aif->data[2] = AifHighPriority;
				aif->data[3] = count;

				/*
				 * Put the FIB onto the
				 * fibctx's fibs
				 */
				list_add_tail(&fib->fiblink, &fibctx->fib_list);
				fibctx->count++;
				/* 
				 * Set the event to wake up the
				 * thread that will waiting.
				 */
				up(&fibctx->wait_sem);
			} else {
				printk(KERN_WARNING "aifd: didn't allocate NewFib.\n");
				if(fib)
					kfree(fib);
				if(hw_fib)
					kfree(hw_fib);
			}
			entry = entry->next;
		}
		spin_unlock_irqrestore(&aac->fib_lock, flagv);

		printk(((count < 0)
		    ? KERN_ERR "%s: Host adapter appears dead %d\n"
		    : KERN_ERR "%s: Host adapter BLINK LED 0x%x\n"),
		  AAC_DRIVER_NAME, count);
		/*
		 *	If a positive health, means in a known DEAD PANIC
		 * state and unlikely to recover reliably
		 */
		return (count < 0) ? -ENODEV : SUCCESS;
	}
	/*
	 * Wait for all commands to complete to this specific
	 * target (block maximum 60 seconds).
	 */
	for (count = 60; count; --count) {
		int active = 0;
		__shost_for_each_device(dev, host) {
			spin_lock_irqsave(&dev->list_lock, flags);
			list_for_each_entry(command, &dev->cmd_list, list) {
				if (command->serial_number) {
					active++;
					break;
				}
			}
			spin_unlock_irqrestore(&dev->list_lock, flags);
			if (active)
				break;
		}
		/*
		 * We can exit if all the commands are complete
		 */
		if (active == 0)
			return SUCCESS;
		spin_unlock_irq(host->host_lock);
		scsi_sleep(HZ);
		spin_lock_irq(host->host_lock);
	}
	printk(KERN_ERR "%s: SCSI bus appears hung\n", AAC_DRIVER_NAME);
	return -ETIMEDOUT;
#endif
}

/**
 *	aac_procinfo	-	Implement /proc/scsi/<drivername>/<n>
 *	@proc_buffer: memory buffer for I/O
 *	@start_ptr: pointer to first valid data
 *	@offset: offset into file
 *	@bytes_available: space left
 *	@host_no: scsi host ident
 *	@write: direction of I/O
 *
 *	Used to export driver statistics and other infos to the world outside 
 *	the kernel using the proc file system. Also provides an interface to
 *	feed the driver with information.
 *
 *		For reads
 *			- if offset > 0 return 0
 *			- if offset == 0 write data to proc_buffer and set the start_ptr to
 *			beginning of proc_buffer, return the number of characters written.
 *		For writes
 *			- writes currently not supported, return 0
 *
 *	Bugs:	Only offset zero is handled
 */

static int aac_procinfo(
	struct Scsi_Host * host,
	char *proc_buffer, char **start_ptr,off_t offset,
	int bytes_available,
	int write)
{
	struct aac_dev * dev = (struct aac_dev *)NULL;
	int index, ret, tmp;

#ifdef AAC_LM_SENSOR
	if(offset > 0)
#else
	if(write || offset > 0)
#endif
		return 0;
	*start_ptr = proc_buffer;
#ifdef AAC_LM_SENSOR
	ret = 0;
	if (!write)
#endif
#	if (defined(AAC_DRIVER_BUILD))
		ret = sprintf(proc_buffer,
		  "Adaptec Raid Controller %d.%d-%d[%d]\n",
		  AAC_DRIVER_VERSION >> 24,
		  (AAC_DRIVER_VERSION >> 16) & 0xFF,
		  AAC_DRIVER_VERSION & 0xFF,
		  AAC_DRIVER_BUILD);
#	else
		ret = sprintf(proc_buffer,
		  "Adaptec Raid Controller %d.%d-%d %s\n",
		  AAC_DRIVER_VERSION >> 24,
		  (AAC_DRIVER_VERSION >> 16) & 0xFF,
		  AAC_DRIVER_VERSION & 0xFF,
		  AAC_DRIVER_BUILD_DATE);
#	endif
	for (index = 0; index < aac_count; ++index) {
		if (((dev = aac_devices[index]) != (struct aac_dev *)NULL)
		 && (dev->scsi_host_ptr == host)
		) {
			break;
		}
	}
	if ((index >= aac_count) || (dev == (struct aac_dev *)NULL)) {
		return ret;
	}
#ifdef AAC_LM_SENSOR
	if (write) {
		s32 temp[6];
		static char temperature[] = "temperature=";
		if (strnicmp (proc_buffer, temperature, sizeof(temperature) - 1))
			return bytes_available;
		for (index = 0;
		  index < (sizeof(temp)/sizeof(temp[0]));
		  ++index)
			temp[index] = 0x80000000;
		ret = sizeof(temperature) - 1;
		for (index = 0;
		  index < (sizeof(temp)/sizeof(temp[0]));
		  ++index) {
			int sign, mult, c;
			if (ret >= bytes_available)
				break;
			c = proc_buffer[ret];
			if (c == '\n') {
				++ret;
				break;
			}
			if (c == ',') {
				++ret;
				continue;
			}
			sign = 1;
			mult = 0;
			tmp = 0;
			if (c == '-') {
				sign = -1;
				++ret;
			}
			for (;
			  (ret < bytes_available) && ((c = proc_buffer[ret]));
			  ++ret) {
				if (('0' <= c) && (c <= '9')) {
					tmp *= 10;
					tmp += c - '0';
					mult *= 10;
				} else if ((c == '.') && (mult == 0))
					mult = 1;
				else
					break;
			}
			if ((ret < bytes_available)
			 && ((c == ',') || (c == '\n')))
				++ret;
			if (!mult)
				mult = 1;
			if (sign < 0)
				tmp = -tmp;
			temp[index] = ((tmp << 8) + (mult >> 1)) / mult;
			if (c == '\n')
				break;
		}
		ret = index;
		if (nblank(dprintk(x))) {
			for (index = 0; index < ret; ++index) {
				int sign;
				tmp = temp[index];
				sign = tmp < 0;
				if (sign)
					tmp = -tmp;
				dprintk((KERN_DEBUG "%s%s%d.%08doC",
				  (index ? "," : ""),
				  (sign ? "-" : ""),
				  tmp >> 8, (tmp % 256) * 390625));
			}
		}
		/* Send temperature message to Firmware */
		(void)aac_adapter_sync_cmd(dev, RCV_TEMP_READINGS,
		  ret, temp[0], temp[1], temp[2], temp[3], temp[4], temp[5],
		  NULL, NULL, NULL, NULL, NULL);
		return bytes_available;
	}
#endif
	ret += sprintf(proc_buffer + ret, "Vendor: %s Model: %s\n",
	  aac_drivers[dev->cardtype].vname, aac_drivers[dev->cardtype].model);
	tmp = 0;
	if (nblank(dprintk(x))) {
		ret += sprintf(proc_buffer + ret, "dprintk");
		tmp = '+';
	}
	if (tmp)
		ret += sprintf(proc_buffer + ret, " flags\n");
	tmp = dev->adapter_info.kernelrev;
	ret += sprintf(proc_buffer + ret, "kernel: %d.%d-%d[%d]\n", 
	  tmp >> 24, (tmp >> 16) & 0xff, tmp & 0xff,
	  dev->adapter_info.kernelbuild);
	tmp = dev->adapter_info.monitorrev;
	ret += sprintf(proc_buffer + ret, "monitor: %d.%d-%d[%d]\n", 
	  tmp >> 24, (tmp >> 16) & 0xff, tmp & 0xff,
	  dev->adapter_info.monitorbuild);
	tmp = dev->adapter_info.biosrev;
	ret += sprintf(proc_buffer + ret, "bios: %d.%d-%d[%d]\n", 
	  tmp >> 24, (tmp >> 16) & 0xff, tmp & 0xff,
	  dev->adapter_info.biosbuild);
	if (dev->adapter_info.serial[0] != 0xBAD0)
		ret += sprintf(proc_buffer + ret, "serial: %x\n",
		  dev->adapter_info.serial[0]);
	return ret;
}

/**
 *	aac_cfg_open		-	open a configuration file
 *	@inode: inode being opened
 *	@file: file handle attached
 *
 *	Called when the configuration device is opened. Does the needed
 *	set up on the handle and then returns
 *
 *	Bugs: This needs extending to check a given adapter is present
 *	so we can support hot plugging, and to ref count adapters.
 */

static int aac_cfg_open(struct inode *inode, struct file *file)
{
	unsigned minor_number = iminor(inode);

	if(minor_number >= aac_count)
		return -ENODEV;
	file->private_data = aac_devices[minor_number];
	if (file->private_data == NULL)
		return -ENODEV;
	return 0;
}

/**
 *	aac_cfg_ioctl		-	AAC configuration request
 *	@inode: inode of device
 *	@file: file handle
 *	@cmd: ioctl command code
 *	@arg: argument
 *
 *	Handles a configuration ioctl. Currently this involves wrapping it
 *	up and feeding it into the nasty windowsalike glue layer.
 *
 *	Bugs: Needs locking against parallel ioctls lower down
 *	Bugs: Needs to handle hot plugging
 */
 
static int aac_cfg_ioctl(struct inode *inode,  struct file *file,
		unsigned int cmd, unsigned long arg)
{
	return aac_do_ioctl(file->private_data, cmd, (void *)arg);
}

static struct file_operations aac_cfg_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= aac_cfg_ioctl,
	.open		= aac_cfg_open,
};

static struct scsi_host_template aac_driver_template = {
	.module				= THIS_MODULE,
	.name           		= "AAC",
	.proc_name			= AAC_DRIVER_NAME,
	.info           		= aac_info,
	.ioctl          		= aac_ioctl,
	.queuecommand   		= aac_queuecommand,
	.bios_param     		= aac_biosparm,	
	.proc_info      		= aac_procinfo,
	.slave_configure		= aac_slave_configure,
	.eh_abort_handler		= aac_eh_abort,
	.eh_host_reset_handler		= aac_eh_reset,
	.can_queue      		= AAC_NUM_IO_FIB,	
	.this_id        		= MAXIMUM_NUM_CONTAINERS,
	.sg_tablesize   		= 16,
	.max_sectors    		= 128,
#if (AAC_NUM_IO_FIB > 256)
	.cmd_per_lun			= 256,
#else
	.cmd_per_lun			= AAC_NUM_IO_FIB,
#endif
	.use_clustering			= ENABLE_CLUSTERING,
#ifdef SCSI_HAS_VARY_IO
	.vary_io			= 1,
#endif
};


static int __devinit aac_probe_one(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	unsigned index = id->driver_data;
	struct Scsi_Host *shost;
	struct aac_dev *aac;
	int error = -ENODEV;
	int unique_id = 0;

	for (; (unique_id < aac_count) && aac_devices[unique_id]; ++unique_id)
		continue;
	if (unique_id >= MAXIMUM_NUM_ADAPTERS)
		goto out;

	if (pci_enable_device(pdev))
		goto out;

	if (pci_set_dma_mask(pdev, 0xFFFFFFFFULL) || 
			pci_set_consistent_dma_mask(pdev, 0xFFFFFFFFULL))
		goto out;
	/*
	 * If the quirk31 bit is set, the adapter needs adapter
	 * to driver communication memory to be allocated below 2gig
	 */
	if (aac_drivers[index].quirks & AAC_QUIRK_31BIT) 
		if (pci_set_dma_mask(pdev, 0x7FFFFFFFULL) ||
				pci_set_consistent_dma_mask(pdev, 0x7FFFFFFFULL))
			goto out;
	
	pci_set_master(pdev);

	/* Increment the host adapter count */
	if (unique_id >= aac_count)
		aac_count = unique_id + 1;

	shost = scsi_host_alloc(&aac_driver_template, sizeof(struct aac_dev));
	if (!shost)
		goto out_disable_pdev;

	shost->irq = pdev->irq;
	shost->base = pci_resource_start(pdev, 0);
	scsi_set_device(shost, &pdev->dev);
	shost->unique_id = unique_id;

	aac = (struct aac_dev *)shost->hostdata;
	aac->scsi_host_ptr = shost;	
	aac->pdev = pdev;
	aac->name = aac_driver_template.name;
	aac->id = shost->unique_id;
	aac->cardtype =  index;

	dprintk((KERN_INFO
	  "allocate fibs: kmalloc(%d * (%d + %d), GFP_KERNEL)\n",
	  sizeof(struct fib), shost->can_queue, AAC_NUM_MGT_FIB));
	aac->fibs = kmalloc(sizeof(struct fib) * (shost->can_queue + AAC_NUM_MGT_FIB), GFP_KERNEL);
	if (!aac->fibs)
		goto out_free_host;
	spin_lock_init(&aac->fib_lock);

	/*
	 *	Map in the registers from the adapter.
	 */
	dprintk((KERN_INFO "ioremap(%lx,%d)\n", aac->scsi_host_ptr->base,
	  AAC_MIN_FOOTPRINT_SIZE));
	if ((aac->regs.sa = (struct sa_registers *)ioremap(
	  (unsigned long)aac->scsi_host_ptr->base, AAC_MIN_FOOTPRINT_SIZE))
	  == NULL) {	
		printk(KERN_WARNING "%s: unable to map adapter.\n",
		  AAC_DRIVER_NAME);
		goto out_free_fibs;
	}
	if ((*aac_drivers[index].init)(aac))
		goto out_unmap;

	/*
	 * If we had set a smaller DMA mask earlier, set it to 4gig
	 * now since the adapter can dma data to at least a 4gig
	 * address space.
	 */
	if (aac_drivers[index].quirks & AAC_QUIRK_31BIT)
		if (pci_set_dma_mask(pdev, 0xFFFFFFFFULL))
			goto out_unmap;

	aac_get_adapter_info(aac);

	/*
	 * max channel will be the physical channels plus 1 virtual channel
	 * all containers are on the virtual channel 0
	 * physical channels are address by their actual physical number+1
	 */
	if (aac->nondasd_support == 1)
		shost->max_channel = aac_drivers[index].channels+1;
	else
		shost->max_channel = 1;

	aac_get_config_status(aac);
	aac_get_containers(aac);
	aac_devices[unique_id] = aac;

	shost->max_id = aac->maximum_num_containers;
	if (shost->max_id < MAXIMUM_NUM_CONTAINERS)
		shost->max_id = MAXIMUM_NUM_CONTAINERS;
	else
		shost->this_id = shost->max_id;
	/*
	 * dmb - we may need to move the setting of these parms somewhere else once
	 * we get a fib that can report the actual numbers
	 */
	shost->max_lun = AAC_MAX_LUN;

	error = scsi_add_host(shost, &pdev->dev);
	if (error)
		goto out_deinit;

	pci_set_drvdata(pdev, shost);
	scsi_scan_host(shost);

	return 0;

 out_deinit:
	kill_proc(aac->thread_pid, SIGKILL, 0);
	wait_for_completion(&aac->aif_completion);

	aac_send_shutdown(aac);
	fib_map_free(aac);
	pci_free_consistent(aac->pdev, aac->comm_size, aac->comm_addr, aac->comm_phys);
	kfree(aac->queues);
	if (aac->fsa_dev)
		kfree(aac->fsa_dev);
	free_irq(pdev->irq, aac);
 out_unmap:
	iounmap((void * )aac->regs.sa);
 out_free_fibs:
	kfree(aac->fibs);
 out_free_host:
	scsi_host_put(shost);
 out_disable_pdev:
	pci_disable_device(pdev);
	aac_devices[unique_id] = NULL;
	if (unique_id == (aac_count - 1))
		aac_count--;
 out:
	return error;
}

static void __devexit aac_remove_one(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct aac_dev *aac = (struct aac_dev *)shost->hostdata;
	int index;

	scsi_remove_host(shost);

	kill_proc(aac->thread_pid, SIGKILL, 0);
	wait_for_completion(&aac->aif_completion);

	aac_send_shutdown(aac);
	fib_map_free(aac);
	pci_free_consistent(aac->pdev, aac->comm_size, aac->comm_addr,
			aac->comm_phys);
	kfree(aac->queues);

	free_irq(pdev->irq, aac);
	iounmap((void * )aac->regs.sa);
	
	kfree(aac->fibs);
	kfree(aac->fsa_dev);
	
	scsi_host_put(shost);
	pci_disable_device(pdev);

	for (index = 0; index < aac_count; ++index) {
		if (aac_devices[index] == aac) {
			aac_devices[index] = NULL;
			if (index == (aac_count - 1))
				--aac_count;
			break;
		}
	}
}

static struct pci_driver aac_pci_driver = {
	.name		= AAC_DRIVER_NAME,
	.id_table	= aac_pci_tbl,
	.probe		= aac_probe_one,
	.remove		= __devexit_p(aac_remove_one),
};

static int __init aac_init(void)
{
	int error;
	
#	if (defined(AAC_DRIVER_BUILD))
		printk(KERN_INFO "Red Hat/Adaptec %s driver (%d.%d-%d[%d])\n",
		  AAC_DRIVER_NAME,
		  AAC_DRIVER_VERSION >> 24,
		  (AAC_DRIVER_VERSION >> 16) & 0xFF,
		  AAC_DRIVER_VERSION & 0xFF,
		  AAC_DRIVER_BUILD);
#	else
		printk(KERN_INFO "Red Hat/Adaptec %s driver (%d.%d-%d %s)\n",
		  AAC_DRIVER_NAME,
		  AAC_DRIVER_VERSION >> 24,
		  (AAC_DRIVER_VERSION >> 16) & 0xFF,
		  AAC_DRIVER_VERSION & 0xFF,
		  AAC_DRIVER_BUILD_DATE);
#	endif

	error = pci_module_init(&aac_pci_driver);
	if (error)
		return error;
	if (!aac_count)
		return -ENODEV;

	aac_cfg_major = register_chrdev( 0, "aac", &aac_cfg_fops);
	if (aac_cfg_major < 0) {
		printk(KERN_WARNING
		       "aacraid: unable to register \"aac\" device.\n");
	}
#ifdef CONFIG_COMPAT
	aac_ioctl32(FSACTL_MINIPORT_REV_CHECK, sys_ioctl);
	aac_ioctl32(FSACTL_SENDFIB, sys_ioctl);
	aac_ioctl32(FSACTL_OPEN_GET_ADAPTER_FIB, sys_ioctl);
	aac_ioctl32(FSACTL_GET_NEXT_ADAPTER_FIB,
	  aac_get_next_adapter_fib_ioctl);
	aac_ioctl32(FSACTL_CLOSE_GET_ADAPTER_FIB, sys_ioctl);
	aac_ioctl32(FSACTL_SEND_RAW_SRB, sys_ioctl);
	aac_ioctl32(FSACTL_GET_PCI_INFO, sys_ioctl);
	aac_ioctl32(FSACTL_QUERY_DISK, sys_ioctl);
	aac_ioctl32(FSACTL_DELETE_DISK, sys_ioctl);
	aac_ioctl32(FSACTL_FORCE_DELETE_DISK, sys_ioctl);
	aac_ioctl32(FSACTL_GET_CONTAINERS, sys_ioctl);
#endif

	return 0;
}

static void __exit aac_exit(void)
{
#ifdef CONFIG_COMPAT
	unregister_ioctl32_conversion(FSACTL_MINIPORT_REV_CHECK);
	unregister_ioctl32_conversion(FSACTL_SENDFIB);
	unregister_ioctl32_conversion(FSACTL_OPEN_GET_ADAPTER_FIB);
	unregister_ioctl32_conversion(FSACTL_GET_NEXT_ADAPTER_FIB);
	unregister_ioctl32_conversion(FSACTL_CLOSE_GET_ADAPTER_FIB);
	unregister_ioctl32_conversion(FSACTL_SEND_RAW_SRB);
	unregister_ioctl32_conversion(FSACTL_GET_PCI_INFO);
	unregister_ioctl32_conversion(FSACTL_QUERY_DISK);
	unregister_ioctl32_conversion(FSACTL_DELETE_DISK);
	unregister_ioctl32_conversion(FSACTL_FORCE_DELETE_DISK);
	unregister_ioctl32_conversion(FSACTL_GET_CONTAINERS);
#endif
	unregister_chrdev(aac_cfg_major, "aac");
	pci_unregister_driver(&aac_pci_driver);
}

module_init(aac_init);
module_exit(aac_exit);

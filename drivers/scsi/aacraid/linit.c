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

#define AAC_DRIVER_VERSION		"1.1.2-lk1"
#define AAC_DRIVER_BUILD_DATE		__DATE__
#define AAC_DRIVERNAME			"aacraid"

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
#include <asm/semaphore.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsicam.h>
#include <scsi/scsi_eh.h>

#include "aacraid.h"


MODULE_AUTHOR("Red Hat Inc and Adaptec");
MODULE_DESCRIPTION("Dell PERC2, 2/Si, 3/Si, 3/Di, "
		   "Adaptec Advanced Raid Products, "
		   "and HP NetRAID-4M SCSI driver");
MODULE_LICENSE("GPL");


int nondasd = -1;
module_param(nondasd, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(nondasd, "Control scanning of hba for nondasd devices. 0=off, 1=on");

int paemode = -1;
module_param(paemode, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(paemode, "Control whether dma addressing is using PAE. 0=off, 1=on");

struct aac_dev *aac_devices[MAXIMUM_NUM_ADAPTERS];
static unsigned aac_count;
static int aac_cfg_major = -1;

/*
 * Because of the way Linux names scsi devices, the order in this table has
 * become important.  Check for on-board Raid first, add-in cards second.
 *
 * Note: The last field is used to index into aac_drivers below.
 */
static struct pci_device_id aac_pci_tbl[] = {
	{ 0x1028, 0x0001, 0x1028, 0x0001, 0, 0, 0 }, /* PERC 2/Si */
	{ 0x1028, 0x0002, 0x1028, 0x0002, 0, 0, 1 }, /* PERC 3/Di */
	{ 0x1028, 0x0003, 0x1028, 0x0003, 0, 0, 2 }, /* PERC 3/Si */
	{ 0x1028, 0x0004, 0x1028, 0x00d0, 0, 0, 3 }, /* PERC 3/Si */
	{ 0x1028, 0x0002, 0x1028, 0x00d1, 0, 0, 4 }, /* PERC 3/Di */
	{ 0x1028, 0x0002, 0x1028, 0x00d9, 0, 0, 5 }, /* PERC 3/Di */
	{ 0x1028, 0x000a, 0x1028, 0x0106, 0, 0, 6 }, /* PERC 3/Di */
	{ 0x1028, 0x000a, 0x1028, 0x011b, 0, 0, 7 }, /* PERC 3/Di */
	{ 0x1028, 0x000a, 0x1028, 0x0121, 0, 0, 8 }, /* PERC 3/Di */
	{ 0x9005, 0x0283, 0x9005, 0x0283, 0, 0, 9 }, /* catapult*/
	{ 0x9005, 0x0284, 0x9005, 0x0284, 0, 0, 10 }, /* tomcat*/
	{ 0x9005, 0x0285, 0x9005, 0x0286, 0, 0, 11 }, /* Adaptec 2120S (Crusader)*/
	{ 0x9005, 0x0285, 0x9005, 0x0285, 0, 0, 12 }, /* Adaptec 2200S (Vulcan)*/
	{ 0x9005, 0x0285, 0x9005, 0x0287, 0, 0, 13 }, /* Adaptec 2200S (Vulcan-2m)*/
	{ 0x9005, 0x0285, 0x17aa, 0x0286, 0, 0, 14 }, /* Legend S220*/
	{ 0x9005, 0x0285, 0x17aa, 0x0287, 0, 0, 15 }, /* Legend S230*/

	{ 0x9005, 0x0285, 0x9005, 0x0288, 0, 0, 16 }, /* Adaptec 3230S (Harrier)*/
	{ 0x9005, 0x0285, 0x9005, 0x0289, 0, 0, 17 }, /* Adaptec 3240S (Tornado)*/
	{ 0x9005, 0x0285, 0x9005, 0x028a, 0, 0, 18 }, /* ASR-2020 ZCR PCI-X U320 */
	{ 0x9005, 0x0285, 0x9005, 0x028b, 0, 0, 19 }, /* ASR-2025 ZCR DIMM U320 */
	{ 0x9005, 0x0285, 0x9005, 0x0290, 0, 0, 20 }, /* AAR-2410SA PCI SATA 4ch (Jaguar II)*/

	{ 0x9005, 0x0285, 0x1028, 0x0287, 0, 0, 21 }, /* Perc 320/DC*/
	{ 0x1011, 0x0046, 0x9005, 0x0365, 0, 0, 22 }, /* Adaptec 5400S (Mustang)*/
	{ 0x1011, 0x0046, 0x9005, 0x0364, 0, 0, 23 }, /* Adaptec 5400S (Mustang)*/
	{ 0x1011, 0x0046, 0x9005, 0x1364, 0, 0, 24 }, /* Dell PERC2 "Quad Channel" */
	{ 0x1011, 0x0046, 0x103c, 0x10c2, 0, 0, 25 }, /* HP NetRAID-4M */

	{ 0x9005, 0x0285, 0x1028, 0x0291, 0, 0, 26 }, /* CERC SATA RAID 2 PCI SATA 6ch (DellCorsair) */
	{ 0x9005, 0x0285, 0x9005, 0x0292, 0, 0, 27 }, /* AAR-2810SA PCI SATA 8ch (Corsair-8) */
	{ 0x9005, 0x0285, 0x9005, 0x0293, 0, 0, 28 }, /* AAR-21610SA PCI SATA 16ch (Corsair-16) */
	{ 0x9005, 0x0285, 0x9005, 0x0294, 0, 0, 29 }, /* ESD SO-DIMM PCI-X SATA ZCR (Prowler) */
	{ 0x9005, 0x0285, 0x0E11, 0x0295, 0, 0, 30 }, /* SATA 6Ch (Bearcat) */

	{ 0x9005, 0x0286, 0x9005, 0x028c, 0, 0, 31 }, /* ASR-2230S + ASR-2230SLP PCI-X (Lancer) */
	{ 0x9005, 0x0285, 0x9005, 0x028e, 0, 0, 32 }, /* ASR-2020SA      (ZCR PCI-X SATA) */
	{ 0x9005, 0x0285, 0x9005, 0x028f, 0, 0, 33 }, /* ASR-2025SA      (ZCR DIMM SATA) */
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, aac_pci_tbl);

/*
 * dmb - For now we add the number of channels to this structure.  
 * In the future we should add a fib that reports the number of channels
 * for the card.  At that time we can remove the channels from here
 */
static struct aac_driver_ident aac_drivers[] = {
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 2/Si */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 3/Di */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 3/Si */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 3/Si */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 3/Di */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 3/Di */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 3/Di */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 3/Di */
	{ aac_rx_init, "percraid", "DELL    ", "PERCRAID        ", 2, AAC_QUIRK_31BIT }, /* PERC 3/Di */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "catapult        ", 2, AAC_QUIRK_31BIT }, /* catapult*/
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "tomcat          ", 2, AAC_QUIRK_31BIT }, /* tomcat*/
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 2120S   ", 1, AAC_QUIRK_31BIT }, /* Adaptec 2120S (Crusader)*/
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 2200S   ", 2, AAC_QUIRK_31BIT }, /* Adaptec 2200S (Vulcan)*/
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 2200S   ", 2, AAC_QUIRK_31BIT }, /* Adaptec 2200S (Vulcan-2m)*/
	{ aac_rx_init, "aacraid",  "Legend  ", "Legend S220     ", 1, AAC_QUIRK_31BIT }, /* Legend S220*/
	{ aac_rx_init, "aacraid",  "Legend  ", "Legend S230     ", 2, AAC_QUIRK_31BIT }, /* Legend S230*/

	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 3230S   ", 2 }, /* Adaptec 3230S (Harrier)*/
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "Adaptec 3240S   ", 2 }, /* Adaptec 3240S (Tornado)*/
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2020ZCR     ", 2 }, /* ASR-2020 ZCR PCI-X U320 */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2025ZCR     ", 2 }, /* ASR-2025 ZCR DIMM U320 */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "AAR-2410SA SATA ", 2 }, /* AAR-2410SA PCI SATA 4ch (Jaguar II)*/

	{ aac_rx_init, "percraid", "DELL    ", "PERC 320/DC     ", 2, AAC_QUIRK_31BIT }, /* Perc 320/DC*/
	{ aac_sa_init, "aacraid",  "ADAPTEC ", "Adaptec 5400S   ", 4 }, /* Adaptec 5400S (Mustang)*/
	{ aac_sa_init, "aacraid",  "ADAPTEC ", "AAC-364         ", 4 }, /* Adaptec 5400S (Mustang)*/
	{ aac_sa_init, "percraid", "DELL    ", "PERCRAID        ", 4, AAC_QUIRK_31BIT }, /* Dell PERC2 "Quad Channel" */
	{ aac_sa_init, "hpnraid",  "HP      ", "NetRAID         ", 4 },  /* HP NetRAID-4M */

	{ aac_rx_init, "aacraid",  "DELL    ", "CERC SR2        ", 1 }, /* CERC SATA RAID 2 PCI SATA 6ch (DellCorsair) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "AAR-2810SA SATA ", 1 }, /* AAR-2810SA PCI SATA 8ch (Corsair-8) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "AAR-21610SA SATA", 1 }, /* AAR-21610SA PCI SATA 16ch (Corsair-16) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "SO-DIMM SATA ZCR", 1 }, /* ESD SO-DIMM PCI-X SATA ZCR (Prowler) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "SATA 6Channel   ", 1 }, /* SATA 6Ch (Bearcat) */

	{ aac_rkt_init,"aacraid",  "ADAPTEC ", "ASR-2230S PCI-X ", 2 }, /* ASR-2230S + ASR-2230SLP PCI-X (Lancer) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2020SA      ", 1 }, /* ASR-2020SA      (ZCR PCI-X SATA) */
	{ aac_rx_init, "aacraid",  "ADAPTEC ", "ASR-2025SA      ", 1 }, /* ASR-2025SA      (ZCR DIMM SATA) */
};

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
 *	@bdev: the block device corresponding to the disk
 *	@capacity: the sector capacity of the disk
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
 
static int aac_biosparm(struct scsi_device *sdev, struct block_device *bdev,
			sector_t capacity, int *geom)
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
	 *	Read the first 1024 bytes from the disk device, if the boot
	 *	sector partition table is valid, search for a partition table
	 *	entry whose end_head matches one of the standard geometry 
	 *	translations ( 64/32, 128/32, 255/63 ).
	 */
	buf = scsi_bios_ptable(bdev);
	if(*(unsigned short *)(buf + 0x40) == cpu_to_le16(0xaa55)) {
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
 *	aac_queuedepth		-	compute queue depths
 *	@sdev:	SCSI device we are considering
 *
 *	Selects queue depths for each target device based on the host adapter's
 *	total capacity and the queue depth supported by the target device.
 *	A queue depth of one automatically disables tagged queueing.
 */

static int aac_slave_configure(struct scsi_device *sdev)
{
	if (sdev->tagged_supported)
		scsi_adjust_queue_depth(sdev, MSG_ORDERED_TAG, 128);
	else
		scsi_adjust_queue_depth(sdev, 0, 1);
	return 0;
}

static int aac_ioctl(struct scsi_device *sdev, int cmd, void * arg)
{
	struct aac_dev *dev = (struct aac_dev *)sdev->host->hostdata;
	return aac_do_ioctl(dev, cmd, arg);
}

/*
 * XXX: does aac really need no error handling??
 */
static int aac_eh_abort(struct scsi_cmnd *cmd)
{
	return FAILED;
}

/*
 *	aac_eh_reset	- Reset command handling
 *	@scsi_cmd:	SCSI command block causing the reset
 *
 */
static int aac_eh_reset(struct scsi_cmnd* cmd)
{
	struct scsi_device * dev = cmd->device;
	struct Scsi_Host * host = dev->host;
	struct scsi_cmnd * command;
	int count;
	unsigned long flags;

	printk(KERN_ERR "%s: Host adapter reset request. SCSI hang ?\n", 
					AAC_DRIVERNAME);


	if (aac_adapter_check_health((struct aac_dev *)host->hostdata)) {
		printk(KERN_ERR "%s: Host adapter appears dead\n", 
				AAC_DRIVERNAME);
		return -ENODEV;
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

			/*
			 * We can exit If all the commands are complete
			 */
			if (active == 0)
				return SUCCESS;
		}
		spin_unlock_irq(host->host_lock);
		scsi_sleep(HZ);
		spin_lock_irq(host->host_lock);
	}
	printk(KERN_ERR "%s: SCSI bus appears hung\n", AAC_DRIVERNAME);
	return -ETIMEDOUT;
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
	unsigned minor = iminor(inode);

	if (minor >= aac_count)
		return -ENODEV;
	file->private_data = aac_devices[minor];
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
	.proc_name			= "aacraid",
	.info           		= aac_info,
	.ioctl          		= aac_ioctl,
	.queuecommand   		= aac_queuecommand,
	.bios_param     		= aac_biosparm,	
	.slave_configure		= aac_slave_configure,
	.eh_abort_handler		= aac_eh_abort,
	.eh_host_reset_handler		= aac_eh_reset,
	.can_queue      		= AAC_NUM_IO_FIB,	
	.this_id        		= 16,
	.sg_tablesize   		= 16,
	.max_sectors    		= 128,
	.cmd_per_lun    		= AAC_NUM_IO_FIB, 
	.use_clustering			= ENABLE_CLUSTERING,
};


static int __devinit aac_probe_one(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	unsigned index = id->driver_data;
	struct Scsi_Host *shost;
	struct fsa_scsi_hba *fsa_dev_ptr;
	struct aac_dev *aac;
	int container;
	int error = -ENODEV;

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
	aac_count++;

	shost = scsi_host_alloc(&aac_driver_template, sizeof(struct aac_dev));
	if (!shost)
		goto out_disable_pdev;

	shost->irq = pdev->irq;
	shost->base = pci_resource_start(pdev, 0);
	shost->unique_id = aac_count - 1;

	aac = (struct aac_dev *)shost->hostdata;
	aac->scsi_host_ptr = shost;	
	aac->pdev = pdev;
	aac->name = aac_driver_template.name;
	aac->id = shost->unique_id;
	aac->cardtype =  index;

	aac->fibs = kmalloc(sizeof(struct fib) * AAC_NUM_FIB, GFP_KERNEL);
	if (!aac->fibs)
		goto out_free_host;
	spin_lock_init(&aac->fib_lock);

	/* Initialize the ordinal number of the device to -1 */
	fsa_dev_ptr = &aac->fsa_dev;
	for (container = 0; container < MAXIMUM_NUM_CONTAINERS; container++)
		fsa_dev_ptr->devname[container][0] = '\0';

	if ((*aac_drivers[index].init)(aac , shost->unique_id))
		goto out_free_fibs;

	/*
	 * If we had set a smaller DMA mask earlier, set it to 4gig
	 * now since the adapter can dma data to at least a 4gig
	 * address space.
	 */
	if (aac_drivers[index].quirks & AAC_QUIRK_31BIT)
		if (pci_set_dma_mask(pdev, 0xFFFFFFFFULL))
			goto out_free_fibs;

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

	aac_get_containers(aac);
	aac_devices[aac_count-1] = aac;

	/*
	 * dmb - we may need to move the setting of these parms somewhere else once
	 * we get a fib that can report the actual numbers
	 */
	shost->max_id = AAC_MAX_TARGET;
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
	free_irq(pdev->irq, aac);
	iounmap((void * )aac->regs.sa);
 out_free_fibs:
	kfree(aac->fibs);
 out_free_host:
	scsi_host_put(shost);
 out_disable_pdev:
	pci_disable_device(pdev);
	aac_count--;
 out:
	return error;
}

static void __devexit aac_remove_one(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct aac_dev *aac = (struct aac_dev *)shost->hostdata;

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
	
	scsi_host_put(shost);
	pci_disable_device(pdev);

	/*
	 * We don't decrement aac_count here because adapters can be unplugged
	 * in a different order than they were detected.  If we're ever going
	 * to overflow MAXIMUM_NUM_ADAPTERS we'll have to consider using a
	 * bintmap of free aac_devices slots.
	 */
#if 0
	aac_count--;
#endif
}

static struct pci_driver aac_pci_driver = {
	.name		= AAC_DRIVERNAME,
	.id_table	= aac_pci_tbl,
	.probe		= aac_probe_one,
	.remove		= __devexit_p(aac_remove_one),
};

static int __init aac_init(void)
{
	int error;
	
	printk(KERN_INFO "Red Hat/Adaptec aacraid driver (%s %s)\n",
			AAC_DRIVER_VERSION, AAC_DRIVER_BUILD_DATE);

	error = pci_module_init(&aac_pci_driver);
	if (error)
		return error;

	aac_cfg_major = register_chrdev( 0, "aac", &aac_cfg_fops);
	if (aac_cfg_major < 0) {
		printk(KERN_WARNING
		       "aacraid: unable to register \"aac\" device.\n");
	}

	return 0;
}

static void __exit aac_exit(void)
{
	unregister_chrdev(aac_cfg_major, "aac");
	pci_unregister_driver(&aac_pci_driver);
}

module_init(aac_init);
module_exit(aac_exit);

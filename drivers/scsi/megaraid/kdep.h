/*
 *
 *			Linux MegaRAID device driver
 *
 * Copyright (c) 2003-2004  LSI Logic Corporation.
 *
 *	   This program is free software; you can redistribute it and/or
 *	   modify it under the terms of the GNU General Public License
 *	   as published by the Free Software Foundation; either version
 *	   2 of the License, or (at your option) any later version.
 *
 * FILE		: kdep.h
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/pci.h>
#include <linux/list.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <linux/moduleparam.h>
#include <linux/blkdev.h>
#else
#include <linux/reboot.h>
#include <linux/blk.h>
#include "../sd.h"
#endif
#include "../scsi.h"
#include "../hosts.h"
#include <scsi/scsicam.h>

#ifndef _KDEP_H_
#define _KDEP_H_

// conversion from scsi command
#define SCP2HOST(scp)			(scp)->device->host	// to host
#define SCP2HOSTDATA(scp)		SCP2HOST(scp)->hostdata	// to soft state
#define SCP2CHANNEL(scp)		(scp)->device->channel	// to channel
#define SCP2TARGET(scp)			(scp)->device->id	// to target
#define SCP2LUN(scp)			(scp)->device->lun	// to LUN

// generic macro to convert scsi command and host to controller's soft state
#define SCSIHOST2ADAP(host)	(((caddr_t *)(host->hostdata))[0])
#define SCP2ADAPTER(scp)	(adapter_t *)SCSIHOST2ADAP(SCP2HOST(scp))

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

#define scsi_host_alloc			scsi_register
#define scsi_host_put			scsi_unregister
#define scsi_set_device(host, x)	scsi_set_pci_device(host,	\
						(SCSIHOST2ADAP(host))->pdev)

#ifdef SCSI_HAS_HOST_LOCK
#define scsi_assign_lock(host, lockp)	(host)->host_lock = lockp
#else
#define scsi_assign_lock(host, lockp)	lockp = &io_request_lock
#endif

#define scsi_add_host(x,y)		(0)
#define scsi_remove_host(x)		do {} while(0)
#define scsi_scan_host(x)		do {} while(0)

typedef void irqreturn_t;
#define	IRQ_RETVAL(x)


#define module_param(x,y,z)	MODULE_PARM(x, "i")
#define MODULE_VERSION(x)

#endif	// LINUX_VERSION_CODE

#endif	// _KDEP_H_

/* vim: set ts=8 sw=8 tw=78 ai si: */

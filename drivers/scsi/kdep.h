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
 * Version	: v2.20.0 (Apr 14 2004)
 *
 * This file maintains the backward compatibility in lk 2.6 driver to lk 2.4
 * drivers.
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
#include <linux/blkdev.h>
#else
#include <linux/reboot.h>
#include <linux/blk.h>
#include "sd.h"
#endif
#include "scsi.h"
#include "hosts.h"
#include <scsi/scsicam.h>

#ifndef _KDEP_H_
#define _KDEP_H_

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)

#define mraid_scsi_host_alloc		scsi_host_alloc
#define mraid_scsi_host_dealloc		scsi_host_put
#define mraid_scsi_set_pdev(host, pdev)	scsi_set_device(host, &(pdev)->dev)

// conversion from scsi command
#define SCP2HOST(scp)			(scp)->device->host	// to host
#define SCP2HOSTDATA(scp)		SCP2HOST(scp)->hostdata	// to soft state
#define SCP2CHANNEL(scp)		(scp)->device->channel	// to channel
#define SCP2TARGET(scp)			(scp)->device->id	// to target
#define SCP2LUN(scp)			(scp)->device->lun	// to LUN

#else	// lk 2.4

#define mraid_scsi_host_alloc		scsi_register
#define mraid_scsi_host_dealloc		scsi_unregister
#define mraid_scsi_set_pdev(host, pdev)	scsi_set_pci_device(host, pdev)

#define scsi_add_host(x,y)		(0)
#define scsi_remove_host(x)		do {} while(0)
#define scsi_scan_host(x)		do {} while(0)

// conversion from scsi command
#define SCP2HOST(scp)			(scp)->host		// to host
#define SCP2HOSTDATA(scp)		SCP2HOST(scp)->hostdata	// to soft state
#define SCP2CHANNEL(scp)		(scp)->channel		// to channel
#define SCP2TARGET(scp)			(scp)->target		// to target
#define SCP2LUN(scp)			(scp)->lun		// to LUN

typedef void irqreturn_t;
#define	IRQ_RETVAL(x)

#endif

// generic macro to convert scsi command and host to controller's soft state
#define SCSIHOST2ADAP(host)	(adapter_t *)(((caddr_t *)(host->hostdata))[0])
#define SCP2ADAPTER(scp)	SCSIHOST2ADAP(SCP2HOST(scp))

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define mraid_set_host_lock(host, plock) scsi_assign_lock(host, plock)
#elif defined SCSI_HAS_HOST_LOCK
#define mraid_set_host_lock(host, plock) (host)->lock = plock
#else
	adapter->host_lock = &io_request_lock;
#endif

#endif	// _KDEP_H_

/* vim: set ts=8 sw=8 tw=78 ai si: */

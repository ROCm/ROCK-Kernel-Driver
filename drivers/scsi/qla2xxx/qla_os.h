/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003-2004 QLogic Corporation
 * (www.qlogic.com)
 *
 * Portions (C) Arjan van de Ven <arjanv@redhat.com> for Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/

#ifndef __QLA_OS_H
#define __QLA_OS_H

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/mempool.h>
#include <linux/vmalloc.h>
#include <linux/smp_lock.h>
#include <linux/bio.h>
#include <linux/moduleparam.h>
#include <linux/capability.h>
#include <linux/list.h>

#include <asm/system.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/segment.h>
#include <asm/byteorder.h>
#include <asm/pgtable.h>

#include <linux/ioctl.h>
#include <asm/uaccess.h>

#include "scsi.h"
#include <scsi/scsi_host.h>

#include <scsi/scsicam.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>

//TODO Fix this!!!
/*
* String arrays
*/
#define LINESIZE    256
#define MAXARGS      26

/***********************************************************************
* We use the struct scsi_pointer structure that's included with each 
* command SCSI_Cmnd as a scratchpad. 
*
* SCp is defined as follows:
*  - SCp.ptr  -- > pointer to the SRB
*  - SCp.this_residual  -- > HBA completion status for ioctl code. 
*
* Cmnd->host_scribble --> Used to hold the hba actived handle (1..255).
***********************************************************************/
#define	CMD_SP(Cmnd)		((Cmnd)->SCp.ptr)
#define CMD_COMPL_STATUS(Cmnd)  ((Cmnd)->SCp.this_residual)
/* Additional fields used by ioctl passthru */
#define CMD_RESID_LEN(Cmnd)	((Cmnd)->SCp.buffers_residual)
#define CMD_SCSI_STATUS(Cmnd)	((Cmnd)->SCp.Status)
#define CMD_ACTUAL_SNSLEN(Cmnd)	((Cmnd)->SCp.Message)
#define CMD_ENTRY_STATUS(Cmnd)	((Cmnd)->SCp.have_data_in)

#endif

/* ------------------------------------------------------------
 * ibmvscsi.h
 * (C) Copyright IBM Corporation 1994, 2003
 * Authors: Colin DeVilbiss (devilbis@us.ibm.com)
 *          Santiago Leon (santil@us.ibm.com)
 *          Dave Boutcher (sleddog@us.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 * ------------------------------------------------------------
 * Emulation of a SCSI host adapter for Virtual I/O devices
 *
 * This driver allows the Linux SCSI peripheral drivers to directly
 * access devices in the hosting partition, either on an iSeries
 * hypervisor system or a converged hypervisor system.
 */
#ifndef IBMVSCSI_H
#define IBMVSCSI_H
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/version.h>   
#include <linux/string.h>    
#include <linux/errno.h>     
#include <linux/init.h>
#include <linux/module.h>    
#include <linux/blkdev.h>    
#include <linux/interrupt.h> 
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi.h>
#include <linux/dma-mapping.h>
#include "viosrp.h"

/**
 * Work out the number of scatter/gather buffers we support
 */
static const struct SRP_CMD *fake_srp_cmd = NULL;
enum {
	IBMVSCSI_MAX_REQUESTS = 50,
	MAX_INDIRECT_BUFS = (sizeof(fake_srp_cmd->additional_data) - sizeof(struct indirect_descriptor)) / sizeof(struct memory_descriptor)
};

/* ------------------------------------------------------------
 * Data Structures
 */
/* an RPA command/response transport queue */
struct crq_queue {
	struct VIOSRP_CRQ *msgs;
	int size, cur;
	dma_addr_t msg_token;
	spinlock_t lock;
};

/* a unit of work for the hosting partition */
struct srp_event_struct {
	union VIOSRP_IU *evt;		/* the actual SRP IU to send */
	struct scsi_cmnd  *cmnd;		/* data to use for callback */
	struct list_head list;		/* queued or sent list for active events*/
	void (*done)(struct srp_event_struct *);	/* run done(this) when it comes back */
	struct VIOSRP_CRQ crq;		/* points to *evt for DMA */
	struct ibmvscsi_host_data *hostdata;
	char in_use;
	/* for the queue case only: */
	struct SRP_CMD cmd;
	void (*cmnd_done)(struct scsi_cmnd *);	/* special _done_ passed with scsi cmd */
};

/* a pool of event structs for use */
struct event_pool {
	struct srp_event_struct *events;
	u32 size;
	spinlock_t lock;
	union VIOSRP_IU *iu_storage;
	dma_addr_t iu_token;
};

/* all driver data associated with a host adapter */
struct ibmvscsi_host_data {
	atomic_t request_limit;
	struct device *dev;
	struct event_pool pool;
	struct crq_queue queue;
	struct work_struct srp_task;
	spinlock_t lock; /* lock for queues */
	struct list_head sent;
	struct Scsi_Host *host;
};


int ibmvscsi_register_driver(void);
void ibmvscsi_unregister_driver(void);

/* routines for managing a command/response queue */
int ibmvscsi_init_crq_queue(struct crq_queue *queue, struct ibmvscsi_host_data *hostdata);
void ibmvscsi_release_crq_queue(struct crq_queue *queue, struct ibmvscsi_host_data *hostdata);
void ibmvscsi_handle_crq(struct VIOSRP_CRQ *crq, struct ibmvscsi_host_data *hostdata);
int ibmvscsi_send_crq(struct ibmvscsi_host_data *hostdata, u64 word1, u64 word2);

/* Probe/remove routines */
struct ibmvscsi_host_data *ibmvscsi_probe(struct device *dev);
int ibmvscsi_remove(struct ibmvscsi_host_data *hostdata);

void ibmvscsi_task(unsigned long data);

#endif /* IBMVSCSI_H */

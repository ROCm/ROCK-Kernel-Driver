/* ------------------------------------------------------------
 * ibmvscsi.c
 * (C) Copyright IBM Corporation 1994, 2004
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
 * hypervisor system or a pSeries Power5 system.
 *
 * One of the capabilities provided on these systems is the ability
 * to DMA between partitions.  The architecture states that for VSCSI,
 * the server side is allowed to DMA to and from the client.  The client
 * is never trusted to DMA to or from the server directly.
 *
 * Messages are sent between partitions on a "Command/Response Queue" 
 * (CRQ), which is just a buffer of 16 byte entries in the receiver's 
 * Senders cannot access the buffer directly, but send messages by
 * making a hypervisor call and passing in the 16 bytes.  The hypervisor
 * puts the message in the next 16 byte space in round-robbin fashion,
 * turns on the high order bit of the message (the valid bit), and 
 * generates an interrupt to the receiver (if interrupts are turned on.) 
 * The receiver just turns off the valid bit when they have copied out
 * the message.
 *
 * The VSCSI client builds a SCSI Remote Protocol (SRP) Information Unit
 * (IU) (as defined in the T10 standard available at www.t10.org), gets 
 * a DMA address for the message, and sends it to the server as the
 * payload of a CRQ message.  The server DMAs the SRP IU and processes it,
 * including doing any additional data transfers.  When it is done, it
 * DMAs the SRP response back to the same address as the request came from,
 * and sends a CRQ message back to inform the client that the request has
 * completed.
 *
 * Note that some of the underlying infrastructure is different between
 * machines conforming to the "RS/6000 Platform Architecture" (RPA) and
 * the older iSeries hypervisor models.  To support both, some low level
 * routines have been broken out into rpa_vscsi.c and iSeries_vscsi.c.
 * The Makefile should pick one, not two, not zero, of these.
 *
 * TODO: This is currently pretty tied to the IBM i/pSeries hypervisor
 * interfaces.  It would be really nice to abstract this above an RDMA
 * layer.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <asm/vio.h>
#include "ibmvscsi.h"

MODULE_DESCRIPTION("IBM Virtual SCSI");
MODULE_AUTHOR("Colin DeVilbiss");
MODULE_LICENSE("GPL");

/* data structures */
struct srp_event_struct; /* a unit of work for the hosting partition */
 
/* ------------------------------------------------------------
 * Data Structures
 */

/* ------------------------------------------------------------
 * Routines for the event pool and event structs
 */
/**
 * initialize_event_pool: - Allocates and initializes the event pool for a host
 * @pool:	event_pool to be initialized
 * @size:	Number of events in pool
 * @hostdata:	ibmvscsi_host_data who owns the event pool
 *
 * Returns zero on success.
*/
static int initialize_event_pool(struct event_pool *pool, int size, struct ibmvscsi_host_data *hostdata)
{
	int i;

	pool->size = size;
	pool->lock = SPIN_LOCK_UNLOCKED;
	pool->events = kmalloc(pool->size * sizeof(*pool->events), GFP_KERNEL);
	if(!pool->events)
		return -ENOMEM;
	memset(pool->events, 0x00, pool->size * sizeof(*pool->events));

	pool->iu_storage = dma_alloc_consistent(hostdata->dmadev, pool->size * sizeof(*pool->iu_storage), &pool->iu_token);
	if(!pool->iu_storage) {
		kfree(pool->events);
		return -ENOMEM;
	}

	for(i = 0; i < pool->size; ++i) {
		struct srp_event_struct *evt = &pool->events[i];
		evt->crq.valid = 0x80;
		evt->crq.IU_length = sizeof(*evt->evt);
		evt->crq.IU_data_ptr = pool->iu_token + sizeof(*evt->evt) * i;
		evt->evt = pool->iu_storage + i;
		evt->hostdata = hostdata;
	}

	return 0;
}

/**
 * release_event_pool: - Frees memory of an event pool of a host
 * @pool:	event_pool to be released
 * @hostdata:	ibmvscsi_host_data who owns the even pool
 *
 * Returns zero on success.
*/
static void release_event_pool(struct event_pool *pool, struct ibmvscsi_host_data *hostdata)
{
	int i, in_use = 0;
	for(i = 0; i < pool->size; ++i)
		if(pool->events[i].in_use)
			++in_use;
	if(in_use)
		printk(KERN_WARNING "releasing event pool with %d events still in use?\n", in_use);
	kfree(pool->events);
	dma_free_consistent(hostdata->dmadev, pool->size * sizeof(*pool->iu_storage), pool->iu_storage, pool->iu_token);
}

/**
 * ibmvscsi_valid_event_struct: - Determines if event is valid.
 * @pool:	event_pool that contains the event
 * @evt:	srp_event_struct to be checked for validity
 *
 * Returns zero if event is invalid, one otherwise.
*/
int ibmvscsi_valid_event_struct(struct event_pool *pool, struct srp_event_struct *evt)
{
	int index = evt - pool->events;
	if(index < 0 || index >= pool->size) /* outside of bounds */
		return 0;
	if(evt != pool->events + index) /* unaligned */
		return 0;
	return 1;
}

/**
 * ibmvscsi_free-event_struct: - Changes status of event to "free"
 * @pool:	event_pool that contains the event
 * @evt:	srp_event_struct to be modified
 *
*/
void ibmvscsi_free_event_struct(struct event_pool *pool, struct srp_event_struct *evt)
{
	if(!ibmvscsi_valid_event_struct(pool, evt)) {
		printk(KERN_ERR "ibmvscsi: YIKES! tried to free invalid event_struct %p (not in pool %p)\n", evt, pool->events);
		return;
	}
	if(!evt->in_use) {
		printk(KERN_ERR "ibmvscsi: YIKES! tried to free event_struct %p which is not in use!\n", evt);
		return;
	}
	evt->in_use = 0;
}

/**
 * ibmvscsi_get_event_struct: - Gets the next free event in pool
 * @pool:	event_pool that contains the events to be searched
 *
 * Returns the next event in "free" state, and NULL if none are free.
*/
struct srp_event_struct *ibmvscsi_get_event_struct(struct event_pool *pool)
{
	struct srp_event_struct *cur, *last = pool->events + pool->size;
	unsigned long flags;	

	spin_lock_irqsave(&pool->lock, flags);
	for (cur = pool->events; cur < last; ++cur)
		if (!cur->in_use) {
			cur->in_use = 1;
			break;
		}
	spin_unlock_irqrestore(&pool->lock, flags);

	if(cur >= last) {
		printk(KERN_ERR "ibmvscsi: found no event struct in pool!\n");
		return NULL;
	}

	return cur;
}

/**
 * evt_struct_for: - Initializes the next free event
 * @pool:	event_pool that contains events to be searched
 * @evt:	VIOSRP_IU that the event will point to
 * @data:	data that the event will point to
 * @done:	Callback function when event is processed
 *
 * Returns the initialized event, and NULL if there are no free events
*/
static struct srp_event_struct *evt_struct_for(struct event_pool *pool, union VIOSRP_IU *evt, Scsi_Cmnd *cmnd, void (*done)(struct srp_event_struct *))
{
	struct srp_event_struct *evt_struct = ibmvscsi_get_event_struct(pool);
	if(!evt_struct)
		return NULL;

	*evt_struct->evt = *evt;
	evt_struct->evt->srp.generic.tag = (u64)(unsigned long)evt_struct;

	evt_struct->cmnd = cmnd;
	evt_struct->done = done;
	return evt_struct;
}

/* ------------------------------------------------------------
 * Routines for receiving SCSI responses from the hosting partition
 */
/**
 * unmap_direct_data: - Unmap address pointed by SRP_CMD
 * @cmd:	SRP_CMD whose additional_data member will be unmapped
 * @dma_dev:	dma device for which the memory is mapped
 *
*/
static void unmap_direct_data(struct SRP_CMD *cmd, struct dma_dev *dmadev)
{
	struct memory_descriptor *data = (struct memory_descriptor *)cmd->additional_data;
	dma_unmap_single(dmadev, data->virtual_address, data->length, PCI_DMA_BIDIRECTIONAL);
}

/**
 * unmap_direct_data: - Unmap array of address pointed by SRP_CMD
 * @cmd:	SRP_CMD whose additional_data member will be unmapped
 * @dma_dev:	dma device for which the memory is mapped
 *
*/
static void unmap_indirect_data(struct SRP_CMD *cmd, struct dma_dev *dmadev)
{
	struct indirect_descriptor *indirect = (struct indirect_descriptor *)cmd->additional_data;
	int i, num_mapped = indirect->head.length / sizeof(indirect->list[0]);
	for(i = 0; i < num_mapped; ++i) {
		struct memory_descriptor *data = &indirect->list[i];
		dma_unmap_single(dmadev, data->virtual_address, data->length, PCI_DMA_BIDIRECTIONAL);
	}
}

/**
 * unmap_direct_data: - Unmap data pointed in SRP_CMD based on the format
 * @cmd:	SRP_CMD whose additional_data member will be unmapped
 * @dmadev:	dma device for which the memory is mapped
 *
*/
static void unmap_cmd_data(struct SRP_CMD *cmd, struct dma_dev *dmadev)
{
	if(cmd->data_out_format == SRP_NO_BUFFER && cmd->data_in_format == SRP_NO_BUFFER)
		return;
	else if(cmd->data_out_format == SRP_DIRECT_BUFFER || cmd->data_in_format == SRP_DIRECT_BUFFER)
		unmap_direct_data(cmd, dmadev);
	else
		unmap_indirect_data(cmd, dmadev);
}

/* ------------------------------------------------------------
 * Routines for direct interpartition interaction
 */

/**
 * ibmvscsi_send_srp_event: - Transforms event to u64 array and calls send_crq()
 * @evt_struct:	evt_struct to be sent
 * @hostdata:	ibmvscsi_host_data of host
 * @pending:    This came off the pending queue.  If can't be sent, put it back
 *              at the head.
 *
 * Returns the value returned from ibmvscsi_send_crq(). (Zero for success)
*/
static int ibmvscsi_send_srp_event(struct srp_event_struct *evt_struct, struct ibmvscsi_host_data *hostdata, int pending)
{
	u64 *crq_as_u64 = (u64*)&evt_struct->crq;

	/* If we have exhausted our request limit, just queue this request */
	if (atomic_dec_return(&hostdata->request_limit) < 0) {
		atomic_inc(&hostdata->request_limit);
		spin_lock(&hostdata->lock);
		if (pending) {
			list_add(&evt_struct->list, &hostdata->queued);
		} else {
			list_add_tail(&evt_struct->list, &hostdata->queued);
		}
		spin_unlock(&hostdata->lock);
		return 0;
	} else {
		/* Add this to the sent list.  We need to do this before we actually send 
		 * in case it comes back REALLY fast
		 */
		spin_lock(&hostdata->lock);
		list_add_tail(&evt_struct->list, &hostdata->sent);
		spin_unlock(&hostdata->lock);
		
		if (ibmvscsi_send_crq(hostdata, crq_as_u64[0], crq_as_u64[1]) != 0) {
			spin_lock(&hostdata->lock);
			list_del(&evt_struct->list);
			spin_unlock(&hostdata->lock);

			Scsi_Cmnd *cmnd = evt_struct->cmnd;
			printk(KERN_ERR "ibmvscsi: failed to send event struct\n");
			unmap_cmd_data(&evt_struct->evt->srp.cmd, hostdata->dmadev);
			ibmvscsi_free_event_struct(&hostdata->pool, evt_struct);
			cmnd->result = DID_ERROR << 16;
			cmnd->host_scribble = NULL;
			evt_struct->cmnd_done(cmnd);
			return -1;
		}
	}
	return 0;
}

/**
 * ibmvscsi_send_pending: - Send events from the pending queue
 * @hostdata:	ibmvscsi_host_data of host
 *
*/
static void ibmvscsi_send_pending(struct ibmvscsi_host_data *hostdata) {
	struct srp_event_struct *evt_struct;
	do {
		spin_lock(&hostdata->lock);
		if (list_empty(&hostdata->queued)) {
			spin_unlock(&hostdata->lock);
			return;
		}
		
		evt_struct = list_entry(hostdata->queued.next,
					struct srp_event_struct,
					list);

		list_del(hostdata->queued.next);

		spin_unlock(&hostdata->lock);
		
		ibmvscsi_send_srp_event(evt_struct, hostdata, 1);
	} while (atomic_read(&hostdata->request_limit));
}

/**
 * ibmvscsi_handle_crq: - Handles and frees received events in the CRQ
 * @crq:	Command/Response queue
 * @hostdata:	ibmvscsi_host_data of host
 *
*/
void ibmvscsi_handle_crq(struct VIOSRP_CRQ *crq, struct ibmvscsi_host_data *hostdata)
{
	struct srp_event_struct *evt_struct = (struct srp_event_struct *)crq->IU_data_ptr;
	switch(crq->valid) {
	case 0xC0: /* initialization */
		switch(crq->format) {
		case 0x01: /* Initialization message */
			printk(KERN_INFO "ibmvscsi: partner just initialized\n");
			/* Send back a response */
			ibmvscsi_send_crq(hostdata, 0xC002000000000000, 0);
			break;
		case 0x02: /* Initialization response */
			printk(KERN_INFO "ibmvscsi: partner initialization complete\n");
			break;
		default:
			printk(KERN_ERR "BORK! unknown type\n");
		}
		return;
	case 0xFF: /* Hypervisor telling us the connection is closed */
		printk(KERN_INFO "ibmvscsi: partner closed\n");
		return;
	case 0x80: /* real payload */
		break;
	default:
		printk(KERN_ERR "ibmvscsi: got an invalid message type 0x%02x\n", crq->valid);
		return;
	}

	/* The only kind of payload CRQs we should get are responses to things we send.
	 * Make sure this response is to something we actually sent
	 */
	if(!ibmvscsi_valid_event_struct(&hostdata->pool, evt_struct)) {
		printk(KERN_ERR "BORK! returned correlation_token 0x%p is invalid!\n", (void*)crq->IU_data_ptr);
		return;
	}

	if(crq->format == VIOSRP_SRP_FORMAT)
		atomic_add(evt_struct->evt->srp.rsp.request_limit_delta, &hostdata->request_limit);

	if(evt_struct->done)
		evt_struct->done(evt_struct);
	else
		printk(KERN_ERR "BORK! returned done() is NULL; not running it!\n");

}

/**
 * ibmvscsi_task: - Process srps asynchronously
 * @data:	ibmvscsi_host_data of host
 *
*/
static void ibmvscsi_task(unsigned long data) 
{
	struct ibmvscsi_host_data *hostdata = (struct ibmvscsi_host_data *)data;
	struct VIOSRP_CRQ *crq;
	int done = 0;

	while (!done)
	{
		/* Pull all the valid messages off the CRQ */
		while((crq = crq_queue_next_crq(&hostdata->queue)) != NULL) {
			ibmvscsi_handle_crq(crq, hostdata);
			crq->valid = 0x00;
		}

		ibmvscsi_enable_interrupts((void*)hostdata->dmadev);
		if ((crq = crq_queue_next_crq(&hostdata->queue)) != NULL) {
			ibmvscsi_disable_interrupts((void*)hostdata->dmadev);
			ibmvscsi_handle_crq(crq, hostdata);
			crq->valid = 0x00;
		} else {
			done = 1;
		}
	}
}


/**
 * handle_cmd_rsp: -  Handle responses fom commands
 * @evt_struct:	srp_event_struct to be handled
 *
 * Used as a callback by when sending scsi cmds (by scsi_cmd_to_event_struct). 
 * Gets called by ibmvscsi_handle_crq()
*/
static void handle_cmd_rsp(struct srp_event_struct *evt_struct)
{
	struct SRP_RSP *rsp = &evt_struct->evt->srp.rsp;
	Scsi_Cmnd *cmnd = (Scsi_Cmnd *) evt_struct->cmnd;
	
	if (cmnd) {
		cmnd->result |= rsp->status;
		if(status_byte(cmnd->result) == CHECK_CONDITION)
			memcpy(cmnd->sense_buffer, rsp->sense_and_response_data, rsp->sense_data_list_length);
		unmap_cmd_data(&evt_struct->cmd, evt_struct->hostdata->dmadev);

		if(rsp->dounder)
			cmnd->resid = rsp->data_out_residual_count;
		else if(rsp->diunder)
			cmnd->resid = rsp->data_in_residual_count;
	}
	
	if (evt_struct->cmnd_done) {
		evt_struct->cmnd_done(cmnd);
	}
	
	if (cmnd) {
		cmnd->host_scribble = NULL;
	}

	ibmvscsi_send_pending(evt_struct->hostdata);
	
	ibmvscsi_free_event_struct(&evt_struct->hostdata->pool, evt_struct);
}


/* ------------------------------------------------------------
 * Routines for queuing individual SCSI commands to the hosting partition
 */
/**
 * map_sg_data: - Maps dma for a scatterlist and initializes decriptor fields
 * @cmd:	Scsi_Cmnd with the scatterlist
 * @srp_cmd:	SRP_CMD that contains the memory descriptor
 * @dmadev:	dma device for which to map dma memory
 *
 * Called by map_data_for_srp_cmd() when building srp cmd from scsi cmd.
 * Returns 1 on success.
*/
static int map_sg_data(Scsi_Cmnd *cmd, struct SRP_CMD *srp_cmd, struct dma_dev *dmadev)
{
	
	int i, sg_mapped;
	u64 total_length = 0;
	struct scatterlist *sg = cmd->request_buffer;
	struct memory_descriptor *data = (struct memory_descriptor *)srp_cmd->additional_data;
	struct indirect_descriptor *indirect = (struct indirect_descriptor *)data;
	sg_mapped = dma_map_sg(dmadev, sg, cmd->use_sg, PCI_DMA_BIDIRECTIONAL);

	/* special case; we can use a single direct descriptor */
	if(sg_mapped == 1)
	{
		if(cmd->sc_data_direction == SCSI_DATA_WRITE)
			srp_cmd->data_out_format = SRP_DIRECT_BUFFER;
		else
			srp_cmd->data_in_format = SRP_DIRECT_BUFFER;
		data->virtual_address = sg[0].dma_address;
		data->length = sg[0].dma_length;
		data->memory_handle = 0 /* viopath_sourceinst(viopath_hostLp) */;
		return 1;
	}

	if(sg_mapped > MAX_INDIRECT_BUFS) {
		printk(KERN_ERR "can't handle more than %d mapped sg entries, got %d\n", MAX_INDIRECT_BUFS, sg_mapped);
		return 0;
	}

	if(cmd->sc_data_direction == SCSI_DATA_WRITE) {
		srp_cmd->data_out_format = SRP_INDIRECT_BUFFER;
		srp_cmd->data_out_count = sg_mapped;
	}
	else {
		srp_cmd->data_in_format = SRP_INDIRECT_BUFFER;
		srp_cmd->data_in_count = sg_mapped;
	}
	indirect->head.virtual_address = 0; 
	indirect->head.length = sg_mapped * sizeof(indirect->list[0]);
	indirect->head.memory_handle = 0;
	for(i = 0; i < sg_mapped; ++i) {
		struct memory_descriptor *descr = &indirect->list[i];
		struct scatterlist *sg_entry = &sg[i];
		descr->virtual_address = sg_entry->dma_address;
		descr->length = sg_entry->dma_length;
		descr->memory_handle = 0 /* viopath_sourceinst(viopath_hostLp) */;
		total_length += sg_entry->dma_length;
	}
	indirect->total_length = total_length;

	return 1;
}

/**
 * map_sg_data: - Maps memory and initializes memory decriptor fields
 * @cmd:	Scsi_Cmnd with the memory to be mapped
 * @srp_cmd:	SRP_CMD that contains the memory descriptor
 * @dmadev:	dma device for which to map dma memory
 *
 * Called by map_data_for_srp_cmd() when building srp cmd from scsi cmd.
 * Returns 1 on success.
*/
static int map_single_data(Scsi_Cmnd *cmd, struct SRP_CMD *srp_cmd, struct dma_dev *dmadev)
{
	struct memory_descriptor *data = (struct memory_descriptor *)srp_cmd->additional_data;

	data->virtual_address = (u64)(unsigned long)dma_map_single(
		dmadev, cmd->request_buffer, cmd->request_bufflen,
		PCI_DMA_BIDIRECTIONAL);
	if(data->virtual_address == 0xFFFFFFFF) {
		printk(KERN_ERR "Unable to map request_buffer for command!\n");
		return 0;
	}
	data->length = cmd->request_bufflen;
	data->memory_handle = 0 /* viopath_sourceinst(viopath_hostLp) */;

	if(cmd->sc_data_direction == SCSI_DATA_WRITE)
		srp_cmd->data_out_format = SRP_DIRECT_BUFFER;
	else
		srp_cmd->data_in_format = SRP_DIRECT_BUFFER;

	return 1;
}

/**
 * map_data_for_srp_cmd: - Calls functions to map data for srp cmds
 * @cmd:	Scsi_Cmnd with the memory to be mapped
 * @srp_cmd:	SRP_CMD that contains the memory descriptor
 * @dmadev:	dma device for which to map dma memory
 *
 * Called by scsi_cmd_to_srp_cmd() when converting scsi cmds to srp cmds 
 * Returns 1 on success.
*/
static int map_data_for_srp_cmd(Scsi_Cmnd *cmd, struct SRP_CMD *srp_cmd, struct dma_dev *dmadev)
{
	switch(cmd->sc_data_direction) {
	case SCSI_DATA_READ:
	case SCSI_DATA_WRITE:
		break;
	case SCSI_DATA_NONE:
		return 1;
	case SCSI_DATA_UNKNOWN:
		printk(KERN_ERR "Can't map SCSI_DATA_UNKNOWN to read/write\n");
		return 0;
	default:
		printk(KERN_ERR "Unknown data direction 0x%02x; can't map!\n", cmd->sc_data_direction);
		return 0;
	}

	if(!cmd->request_buffer)
		return 1;
	if(cmd->use_sg)
		return map_sg_data(cmd, srp_cmd, dmadev);
	return map_single_data(cmd, srp_cmd, dmadev);
}

/**
 * lun_from_dev: - Returns the lun of the scsi device
 * @dev:	Scsi_Device
 *
*/
static inline u16 lun_from_dev(Scsi_Device *dev)
{
	return (0x2 << 14) | (dev->id << 8) | (dev->channel << 5) | dev->lun;
}

/**
 * scsi_cmd_to_srp_cmd: - Initializes srp cmd with data from scsi cmd
 * @cmd:	source Scsi_Cmnd
 * @srp_cmd:	target SRP_CMD
 * @hostdata:	ibmvscsi_host_data of host
 *
 * Returns 1 on success.
*/
static int scsi_cmd_to_srp_cmd(Scsi_Cmnd *cmd, struct SRP_CMD *srp_cmd, struct ibmvscsi_host_data *hostdata)
{
	u16 lun = lun_from_dev(cmd->device);
	memset(srp_cmd, 0x00, sizeof(*srp_cmd));

	srp_cmd->type = SRP_CMD_TYPE;
	memcpy(srp_cmd->cdb, cmd->cmnd, sizeof(cmd->cmnd));
	srp_cmd->lun = ((u64)lun) << 48;

	return map_data_for_srp_cmd(cmd, srp_cmd, hostdata->dmadev);
}

/**
 * scsi_cmd_to_event_struct: - Initializes a srp_event_struct with data form scsi cmd
 * @cmd:	Source Scsi_Cmnd
 * @done:	Callback function to be called when cmd is completed
 * @hostdata:	ibmvscsi_host_data of host
 *
 * Returns the srp_event_struct to be used or NULL if not successful.
*/
static struct srp_event_struct *scsi_cmd_to_event_struct(Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd*), struct ibmvscsi_host_data *hostdata)
{
	struct SRP_CMD srp_cmd;
	struct srp_event_struct *evt_struct;

	if(!scsi_cmd_to_srp_cmd(cmd, &srp_cmd, hostdata)) {
		printk(KERN_ERR "ibmvscsi: couldn't convert cmd to SRP_CMD\n");
		return NULL;
	}

	evt_struct = evt_struct_for(&hostdata->pool, (union VIOSRP_IU *)&srp_cmd, (void*)cmd, handle_cmd_rsp);
	if(!evt_struct) {
		printk(KERN_ERR "ibmvscsi: evt_struct_for() returned NULL\n");
		return NULL;
	}

	cmd->host_scribble = (unsigned char *)evt_struct;
	evt_struct->cmd = srp_cmd;
	evt_struct->cmnd_done = done;
	evt_struct->crq.timeout = cmd->timeout;
	return evt_struct;
}

/**
 * ibmvscsi_queue: - The queuecommand function of the scsi template 
 * @cmd:	Scsi_Cmnd to be executed
 * @done:	Callback function to be called when cmd is completed
 *
 * Always returns zero
*/
static int ibmvscsi_queue(Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *))
{
	struct ibmvscsi_host_data *hostdata = (struct ibmvscsi_host_data *)&cmd->device->host->hostdata;
	struct srp_event_struct *evt_struct = scsi_cmd_to_event_struct(cmd, done, hostdata);

	if (!evt_struct) {
		printk(KERN_ERR "ibmvscsi: unable to convert Scsi_Cmnd to LpEvent\n");
		cmd->result = DID_ERROR << 16;
		done(cmd);
		return 0;
	}

	evt_struct->crq.format = VIOSRP_SRP_FORMAT;

	return ibmvscsi_send_srp_event(evt_struct, hostdata, 0);
}

/* ------------------------------------------------------------
 * Routines for driver initialization
 */
static void error_cleanup(struct ibmvscsi_host_data *hostdata) {
	struct Scsi_Host *host = hostdata->host;
	release_event_pool(&hostdata->pool, hostdata);
	release_crq_queue(&hostdata->queue, hostdata);
	memset(hostdata, 0xff, sizeof(*hostdata));
	scsi_host_put(host);
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
/**
 * Initiate a scan of our devices asynchronously.
 */
static void ibmvscsi_scan_task(unsigned long data) 
{
	struct ibmvscsi_host_data *hostdata = (struct ibmvscsi_host_data *)data;
	
	scsi_scan_host(hostdata->host);
}
#endif

/**
 * report_luns_rsp: - Handle response to our report_luns request
 * @evt_struct:	srp_event_struct with the response
 *
 * Used as a "done" callback by when sending report_luns_login. Gets called
 * by ibmvscsi_handle_crq()
*/
static void report_luns_rsp(Scsi_Cmnd* cmnd)
{
	struct ibmvscsi_host_data *hostdata = (struct ibmvscsi_host_data *)&cmnd->device->host->hostdata;
	struct srp_event_struct *evt_struct = 	(struct srp_event_struct *)cmnd->host_scribble;

	u64 *report_luns_data = (u64*)evt_struct->cmnd->request_buffer;
	u32 max_lun_index;
	int i;
	
	max_lun_index = ((report_luns_data[0] >> 32) / sizeof(u64))+ 1;
	printk(KERN_INFO "ibmvscsi: %d devices attached\n", max_lun_index - 1);
	for(i = 1; i < max_lun_index; ++i) {
		u16 lun_level = report_luns_data[i] >> 48;
		u8 target = (lun_level >> 8) & 0x3F;
		u8 bus = (lun_level >> 5) & 0x7;
		u8 lun = (lun_level >> 0) & 0x1F;
		if(hostdata->host->max_id < target)
			hostdata->host->max_id = target;
		if(hostdata->host->max_lun < lun)
			hostdata->host->max_lun = lun;
		if(hostdata->host->max_channel < bus)
			hostdata->host->max_channel = bus;
	}
	++hostdata->host->max_id;
	++hostdata->host->max_lun;

	/* Free the report luns stuff.  I am not proud of this, but
	 * this takes advantage of the fact that cmnd is the first
	 * area of the internal_report_luns area below.
	 */
	kfree(evt_struct->cmnd);

	/* We now know enough to register our adapter */
#if LINUX_VERSION_CODE >= 0x020600
	if (!scsi_add_host(hostdata->host, &hostdata->dmadev->dev)) {
		SCHEDULE_BOTTOM_HALF(&hostdata->scan_task);
	} else {
		/* Couldn't add host.  bail */
		printk(KERN_ERR"ibmvscsi: Unable to add host\n");
		error_cleanup(hostdata);
	}
#else
	up(&hostdata->waitsem);
#endif
	
}

/**
 * set_host_limits: - Get host limits from server and set the values in host
 * @host:	Scsi_Host to set the host limits
 *
*/
static void send_report_luns(struct ibmvscsi_host_data *hostdata)
{
	struct srp_event_struct *evt;
/**
 * struct Internal_Report_Luns
 * contains all the things we need to manufacture a report
 * luns request and send it down through our infrastructure
 */
	struct {
		Scsi_Cmnd cmnd;
		Scsi_Device dev;
		u64 report_luns_data[64];
	} *internal_report_luns = kmalloc(sizeof(*internal_report_luns), GFP_ATOMIC);
	
	struct report_luns_cdb {
		u8 opcode;
		u8 reserved1[1];
		u8 report;
		u8 reserved2[3];
		u32 length PACKED;
		u8 reserved3[1];
		u8 control;
	} *cdb; 

	if (internal_report_luns == NULL) {
		printk(KERN_ERR"couldn't allocate report_luns buffer\n");
		return;
	}

	memset(internal_report_luns,0x00,sizeof(*internal_report_luns));
	/* Note this means id/channel/lun of 0, which is what we want */
	internal_report_luns->cmnd.use_sg = 0;
	internal_report_luns->cmnd.request_buffer = (void *)internal_report_luns->report_luns_data;
	internal_report_luns->cmnd.request_bufflen = sizeof(internal_report_luns->report_luns_data);
	internal_report_luns->cmnd.sc_data_direction = SCSI_DATA_READ;
	internal_report_luns->cmnd.device = &internal_report_luns->dev;

	internal_report_luns->dev.host = hostdata->host;

	cdb = (struct report_luns_cdb *)&internal_report_luns->cmnd.cmnd;

	cdb->opcode = 0xA0; /* REPORT_LUNS */
	cdb->length = sizeof(internal_report_luns->report_luns_data);

	evt = scsi_cmd_to_event_struct(&internal_report_luns->cmnd, report_luns_rsp, hostdata);
	if(!evt) {
		printk(KERN_ERR "couldn't allocate evt struct for host limits\n");
		kfree(internal_report_luns);
		error_cleanup(hostdata);
		return;
	}

	evt->evt->srp.cmd.lun = 0;

	if(ibmvscsi_send_srp_event(evt, hostdata, 0) != 0) {
		printk(KERN_ERR "ibmvscsi: failed to send event struct for host limits\n");
		unmap_cmd_data(&evt->evt->srp.cmd, hostdata->dmadev);
		kfree(internal_report_luns);
		error_cleanup(hostdata);
		return;
	}
}

/**
 * login_rsp: - Handle response to login request
 * @evt_struct:	srp_event_struct with the response
 *
 * Used as a "done" callback by when sending srp_login. Gets called
 * by ibmvscsi_handle_crq()
*/
static void login_rsp(struct srp_event_struct *evt_struct)
{
	struct ibmvscsi_host_data *hostdata = evt_struct->hostdata;
	switch(evt_struct->evt->srp.generic.type) {
	case SRP_LOGIN_RSP_TYPE: /* it worked! */
		break;
	case SRP_LOGIN_REJ_TYPE: /* refused! */
		printk(KERN_INFO "BORK! SRP_LOGIN_REQ rejected\n");
		error_cleanup(hostdata);
		return;
	default:
		printk(KERN_ERR "Invalid SRP_LOGIN_REQ response typecode 0x%02x!\n", evt_struct->evt->srp.generic.type);
		error_cleanup(hostdata);
		return;
	}
	
	printk(KERN_INFO "ibmvscsi: SRP_LOGIN succeeded, sending REPORT_LUNS\n");
	
	/* Now we know what the real request-limit is */
	atomic_set(&hostdata->request_limit, evt_struct->evt->srp.login_rsp.request_limit_delta);
	
	ibmvscsi_free_event_struct(&evt_struct->hostdata->pool, evt_struct);

	send_report_luns(hostdata);
	return;
}


/**
 * send_srp_login: - Sends the srp login
 * @hostdata:	ibmvscsi_host_data of host
 * 
 * Returns zero if successful.
*/
static int send_srp_login(struct ibmvscsi_host_data *hostdata)
{
	struct SRP_LOGIN_REQ req = {
		.type = SRP_LOGIN_REQ_TYPE,
		.max_requested_initiator_to_target_iulen = sizeof(union SRP_IU),
		.required_buffer_formats = 0x0002 /* direct and indirect */
	};
	struct srp_event_struct *evt_struct =
		evt_struct_for(&hostdata->pool, (union VIOSRP_IU *)&req, NULL, login_rsp);

	if(!evt_struct) {
		printk(KERN_ERR "BORK! couldn't allocate an event for SRP_LOGIN_REQ!\n");
		return -1;
	}

	/* Start out with a request limit of 1, since this is negotiated in
	 * the login request we are just sending
	 */
	atomic_set(&hostdata->request_limit, 1);
	evt_struct->crq.format = VIOSRP_SRP_FORMAT;
	
	printk(KERN_INFO "Sending SRP login: req 0x%p\n", &req);
	return ibmvscsi_send_srp_event(evt_struct, hostdata, 0);
};

/**
 * ibmvscsi_info: - The info function in the scsi template.
 * @host:	Host to display information
 *
 * Returns string with information
*/
static const char *ibmvscsi_info(struct Scsi_Host *host)
{
	return "SCSI host adapter emulator for RPA/iSeries Virtual I/O";
}

/**
 * ibmvscsi_bios: - The bios_param function in the scsi template.
 * @disk:	Disk to fill data
 * @dev:	kdev_t of the device
 * @param:	Array of ints to be filled by function
 *
 * Returns zero.
*/
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,0)
static int ibmvscsi_bios(Scsi_Disk *disk, kdev_t dev, int *parm)
{
	off_t capacity = disk->capacity;
#else
static int ibmvscsi_bios(struct scsi_device *sdev, struct block_device *bdev, sector_t capacity, int parm[3])
{
#endif
	parm[0] = 255;
	parm[1] = 63;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	parm[2] = capacity / (parm[0] * parm[1]);
#else
	sector_div(capacity, 255*63);
#endif
	parm[2] = capacity;
	return 0;
}

/**
 * ibmvscsi_abort: Abort the command.  We can only handle commands not sent
 * yet.
 *
 * Note that we could send an abort task_management command over to the 
 * server.  That gets horribly confusing, because we then have to block
 * here until the response comes back, and various done() routines may
 * get called in the mean time. yuck.
 */
int ibmvscsi_abort(Scsi_Cmnd *cmd)
{
	struct ibmvscsi_host_data *hostdata = (struct ibmvscsi_host_data *)&cmd->device->host->hostdata;
	struct srp_event_struct *tmp_evt;
	struct list_head	*pos, *next;

	/* First see if this command is in our pending list */
	spin_lock(&hostdata->lock);
	if(!list_empty(&hostdata->queued)) {
		list_for_each_safe(pos, next, &hostdata->queued) {
			tmp_evt = list_entry(pos, struct srp_event_struct, list);
			
			if (tmp_evt->cmnd == cmd) {
				cmd->result = (DID_ABORT << 16);
				list_del(&tmp_evt->list);
				spin_unlock(&hostdata->lock);
				return TRUE;
			}
		}
	}
	spin_unlock(&hostdata->lock);
	return FALSE;
}

/* ------------------------------------------------------------
 * SCSI driver registration
 */
static Scsi_Host_Template driver_template = {
	.name = "ibmvscsi",
	.proc_name = "ibmvscsi",
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	.use_new_eh_code = 1,
	.detect = ibmvscsi_detect,
	.release = ibmvscsi_release,
#endif
	.info = ibmvscsi_info,
	.queuecommand = ibmvscsi_queue,
	.bios_param = ibmvscsi_bios,
	.eh_abort_handler = ibmvscsi_abort,
	.can_queue = 10,
	.this_id = -1,
	.sg_tablesize = SG_TABLESIZE,
	.cmd_per_lun = 1,
	.use_clustering = DISABLE_CLUSTERING,
	.emulated = 1
};

/**
 * Called by vio bus code for each adapter
 */
struct ibmvscsi_host_data *ibmvscsi_probe_generic(struct dma_dev *dev, const dma_device_id *id)
{
	struct ibmvscsi_host_data *hostdata;
	struct Scsi_Host *host;

	host = scsi_host_alloc(&driver_template, sizeof(*hostdata));
	if(!host) {
		printk(KERN_ERR "ibmvscsi: couldn't allocate host data\n");
		goto scsi_host_alloc_failed;
	}
	
	hostdata = (struct ibmvscsi_host_data *)host->hostdata;
	memset(hostdata, 0x00, sizeof(*hostdata));
	INIT_LIST_HEAD(&hostdata->queued);
	INIT_LIST_HEAD(&hostdata->sent);
	init_MUTEX_LOCKED(&hostdata->waitsem);
	hostdata->host = host;
	hostdata->dmadev = dev;
	INIT_BOTTOM_HALF(&hostdata->srp_task, (void *)ibmvscsi_task, (unsigned long)hostdata);
#if LINUX_VERSION_CODE >= 0x020600
	INIT_BOTTOM_HALF(&hostdata->scan_task, (void *)ibmvscsi_scan_task, (unsigned long)hostdata);
	hostdata->srp_workqueue = create_workqueue("ibmvscsi");
#endif

	if(initialize_crq_queue(&hostdata->queue, hostdata) != 0) {
		printk(KERN_ERR "ibmvscsi: couldn't initialize crq\n");
		goto init_crq_failed;
	}
	if(ibmvscsi_send_crq(hostdata, 0xC001000000000000, 0) != 0){
		printk(KERN_ERR "ibmvscsi: couldn't send init cmd\n");
		goto init_crq_failed;
	}
	if(initialize_event_pool(&hostdata->pool, IBMVSCSI_MAX_REQUESTS, hostdata) != 0) {
		printk(KERN_ERR "ibmvscsi: couldn't initialize event pool\n");
		goto init_pool_failed;
	}

	/* Now kick off the interaction with the server.  This involves two 
	 * asynchronous steps (SRP_LOGIN followed by REPORT_LUNS so we know how
	 * many devices/busses etc. we have
	 */
	if (send_srp_login(hostdata) == 0) {
#if LINUX_VERSION_CODE < 0x020600
	    /* For the 2.4 kernel we have to wait until we have figured
	     * out busses etc before we return, because as soon as we
	     * return a scan is going to start
	     */
	    down(&hostdata->waitsem);
#endif
	    return hostdata;
	}
	
	printk(KERN_ERR "ibmvscsi: couldn't SRP_LOGIN to remote host\n");

	release_event_pool(&hostdata->pool, hostdata);
init_pool_failed:
	release_crq_queue(&hostdata->queue, hostdata);
init_crq_failed:
	scsi_host_put(host);
scsi_host_alloc_failed:
	return NULL;
}

int ibmvscsi_remove_generic(struct ibmvscsi_host_data *hostdata)
{
	/* send an SRP_I_LOGOUT */
	printk("ibmvscsi: release called\n");
	
	release_event_pool(&hostdata->pool, hostdata);
	release_crq_queue(&hostdata->queue, hostdata);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	hostdata->srp_workqueue = create_workqueue("ibmvscsi");

	/* Note that in 2.4 this is taken care of by scsi_module.c */
	scsi_host_put(hostdata->host);
#endif
	return 0;
}

/* Use old model if we are on 2.4 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#include "scsi_module.c"
#else
module_init(ibmvscsi_module_init);
module_exit(ibmvscsi_module_exit);
#endif

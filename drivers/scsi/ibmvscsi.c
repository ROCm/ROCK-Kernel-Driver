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
#include <asm/vio.h>
#include "ibmvscsi.h"

MODULE_DESCRIPTION("IBM Virtual SCSI");
MODULE_AUTHOR("Colin DeVilbiss");
MODULE_LICENSE("GPL");

/* data structures */
struct srp_event_struct; /* a unit of work for the hosting partition */
typedef void (*srp_callback)(struct srp_event_struct *);
 
/* global variables */
int ibmvscsi_host_count = 0;

/* Used by iSeries_vscsi which only supports a single adapter instance */
struct ibmvscsi_host_data *single_host_data = NULL; 

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#include "sd.h"
static int ibmvscsi_bios(Disk *disk, kdev_t dev, int *parm);
#else
static int ibmvscsi_bios(struct scsi_device *sdev, struct block_device *bdev, sector_t capacity, int parm[3]);
#endif

/* ------------------------------------------------------------
 * Data Structures
 */
/* a unit of work for the hosting partition */
struct srp_event_struct {
	union VIOSRP_IU *evt;		/* the actual SRP IU to send */
	void *data;			/* data to use for callback */
	srp_callback done;		/* run done(this) when it comes back */
	struct VIOSRP_CRQ crq;		/* points to *evt for DMA */
	struct ibmvscsi_host_data *hostdata;
	char in_use;
	/* for the queue case only: */
	struct SRP_CMD cmd;
	void (*cmnd_done)(Scsi_Cmnd*);	/* special _done_ passed with scsi cmd */
};

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

	pool->iu_storage = dma_alloc_consistent(hostdata->dma_dev, pool->size * sizeof(*pool->iu_storage), &pool->iu_token);
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
	dma_free_consistent(hostdata->dma_dev, pool->size * sizeof(*pool->iu_storage), pool->iu_storage, pool->iu_token);
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
static struct srp_event_struct *evt_struct_for(struct event_pool *pool, union VIOSRP_IU *evt, void *data, srp_callback done)
{
	struct srp_event_struct *evt_struct = ibmvscsi_get_event_struct(pool);
	if(!evt_struct)
		return NULL;

	*evt_struct->evt = *evt;
	evt_struct->evt->srp.generic.tag = (u64)(unsigned long)evt_struct;

	evt_struct->data = data;
	evt_struct->done = done;
	return evt_struct;
}


/* ------------------------------------------------------------
 * Routines for direct interpartition interaction
 */

/**
 * ibmvscsi_send_srp_event: - Transforms event to u64 array and calls send_crq()
 * @evt_struct:	evt_struct to be sent
 * @hostdata:	ibmvscsi_host_data of host
 *
 * Returns the value returned from ibmvscsi_send_crq(). (Zero for success)
*/
static int ibmvscsi_send_srp_event(struct srp_event_struct *evt_struct, struct ibmvscsi_host_data *hostdata)
{
	u64 *crq_as_u64 = (u64*)&evt_struct->crq;

	if(hostdata->request_limit <= 0) {
		printk(KERN_ERR "ibmvscsi: request_limit is 0 or lower (%d); won't send\n", hostdata->request_limit);
		return -1;
	}
	--hostdata->request_limit;
	return ibmvscsi_send_crq(hostdata, crq_as_u64[0], crq_as_u64[1]);
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
	case 0xC0: // initialization
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
		hostdata->request_limit += evt_struct->evt->srp.rsp.request_limit_delta;

	if(evt_struct->done)
		evt_struct->done(evt_struct);
	else
		printk(KERN_ERR "BORK! returned done() is NULL; not running it!\n");

	ibmvscsi_free_event_struct(&hostdata->pool, evt_struct);
}

/**
 * ibmvscsi_task: - Function of tasklet in response to interrupts
 * @data:	ibmvscsi_host_data of host
 *
*/
void ibmvscsi_task(unsigned long data) 
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

		/* Re-enable interrupts */
		vio_enable_interrupts((void*)hostdata->dma_dev);

		/* Pull off any messages that have showed up while we re-enabled
		 * interrupts
		 */
		if ((crq = crq_queue_next_crq(&hostdata->queue)) != NULL) {
			vio_disable_interrupts((void*)hostdata->dma_dev);
			ibmvscsi_handle_crq(crq, hostdata);
			crq->valid = 0x00;
		} else {
			done = 1;
		}
	}
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
static void unmap_direct_data(struct SRP_CMD *cmd, struct dma_dev *dma_dev)
{
	struct memory_descriptor *data = (struct memory_descriptor *)cmd->additional_data;
	dma_unmap_single(dma_dev, data->virtual_address, data->length, PCI_DMA_BIDIRECTIONAL);
}

/**
 * unmap_direct_data: - Unmap array of address pointed by SRP_CMD
 * @cmd:	SRP_CMD whose additional_data member will be unmapped
 * @dma_dev:	dma device for which the memory is mapped
 *
*/
static void unmap_indirect_data(struct SRP_CMD *cmd, struct dma_dev *dma_dev)
{
	struct indirect_descriptor *indirect = (struct indirect_descriptor *)cmd->additional_data;
	int i, num_mapped = indirect->head.length / sizeof(indirect->list[0]);
	for(i = 0; i < num_mapped; ++i) {
		struct memory_descriptor *data = &indirect->list[i];
		dma_unmap_single(dma_dev, data->virtual_address, data->length, PCI_DMA_BIDIRECTIONAL);
	}
}

/**
 * unmap_direct_data: - Unmap data pointed in SRP_CMD based on the format
 * @cmd:	SRP_CMD whose additional_data member will be unmapped
 * @dma_dev:	dma device for which the memory is mapped
 *
*/
static void unmap_cmd_data(struct SRP_CMD *cmd, struct dma_dev *dma_dev)
{
	if(cmd->data_out_format == SRP_NO_BUFFER && cmd->data_in_format == SRP_NO_BUFFER)
		return;
	else if(cmd->data_out_format == SRP_DIRECT_BUFFER || cmd->data_in_format == SRP_DIRECT_BUFFER)
		unmap_direct_data(cmd, dma_dev);
	else
		unmap_indirect_data(cmd, dma_dev);
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
	Scsi_Cmnd *cmnd = (Scsi_Cmnd *) evt_struct->data;

	cmnd->result |= rsp->status;
	if(status_byte(cmnd->result) == CHECK_CONDITION)
		memcpy(cmnd->sense_buffer, rsp->sense_and_response_data, rsp->sense_data_list_length);
	unmap_cmd_data(&evt_struct->cmd, evt_struct->hostdata->dma_dev);

	if(rsp->dounder)
		cmnd->resid = rsp->data_out_residual_count;
	else if(rsp->diunder)
		cmnd->resid = rsp->data_in_residual_count;

	evt_struct->cmnd_done(cmnd);
	cmnd->host_scribble = NULL;
}


/* ------------------------------------------------------------
 * Routines for queuing individual SCSI commands to the hosting partition
 */
/**
 * map_sg_data: - Maps dma for a scatterlist and initializes decriptor fields
 * @cmd:	Scsi_Cmnd with the scatterlist
 * @srp_cmd:	SRP_CMD that contains the memory descriptor
 * @dma_dev:	dma device for which to map dma memory
 *
 * Called by map_data_for_srp_cmd() when building srp cmd from scsi cmd.
 * Returns 1 on success.
*/
static int map_sg_data(Scsi_Cmnd *cmd, struct SRP_CMD *srp_cmd, struct dma_dev *dma_dev)
{
	
	int i, sg_mapped;
	u64 total_length = 0;
	struct scatterlist *sg = cmd->request_buffer;
	struct memory_descriptor *data = (struct memory_descriptor *)srp_cmd->additional_data;
	struct indirect_descriptor *indirect = (struct indirect_descriptor *)data;
	sg_mapped = dma_map_sg(dma_dev, sg, cmd->use_sg, PCI_DMA_BIDIRECTIONAL);

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
 * @dma_dev:	dma device for which to map dma memory
 *
 * Called by map_data_for_srp_cmd() when building srp cmd from scsi cmd.
 * Returns 1 on success.
*/
static int map_single_data(Scsi_Cmnd *cmd, struct SRP_CMD *srp_cmd, struct dma_dev *dma_dev)
{
	struct memory_descriptor *data = (struct memory_descriptor *)srp_cmd->additional_data;

	data->virtual_address = (u64)(unsigned long)dma_map_single(
		dma_dev, cmd->request_buffer, cmd->request_bufflen,
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
 * @dma_dev:	dma device for which to map dma memory
 *
 * Called by scsi_cmd_to_srp_cmd() when converting scsi cmds to srp cmds 
 * Returns 1 on success.
*/
static int map_data_for_srp_cmd(Scsi_Cmnd *cmd, struct SRP_CMD *srp_cmd, struct dma_dev *dma_dev)
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
		return map_sg_data(cmd, srp_cmd, dma_dev);
	return map_single_data(cmd, srp_cmd, dma_dev);
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

	return map_data_for_srp_cmd(cmd, srp_cmd, hostdata->dma_dev);
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
	struct ibmvscsi_host_data *hostdata = *(struct ibmvscsi_host_data **)&cmd->device->host->hostdata;
	struct srp_event_struct *evt_struct = scsi_cmd_to_event_struct(cmd, done, hostdata);

	if (!evt_struct) {
		printk(KERN_ERR "ibmvscsi: unable to convert Scsi_Cmnd to LpEvent\n");
		cmd->result = DID_ERROR << 16;
		done(cmd);
		return 0;
	}

	evt_struct->crq.format = VIOSRP_SRP_FORMAT;
	if(ibmvscsi_send_srp_event(evt_struct, hostdata) != 0) {
		printk(KERN_ERR "ibmvscsi: failed to send event struct\n");
		unmap_cmd_data(&evt_struct->evt->srp.cmd, hostdata->dma_dev);
		ibmvscsi_free_event_struct(&hostdata->pool, evt_struct);
		cmd->result = DID_ERROR << 16;
		done(cmd);
		return 0;
	}

	return 0;
}

/* ------------------------------------------------------------
 * Routines for driver initialization
 */

/**
 * unblock_sem: - Unblocks semaphore in event
 * @evt_struct:	srp_event_struct with the semaphore
 *
 * Used as a "done" callback by when sending srp_events. Gets called
 * by ibmvscsi_handle_crq()
*/
static void unblock_sem(struct srp_event_struct *evt_struct)
{
	up((struct semaphore*)evt_struct->data);
}


/**
 * do_srp_login: - Does the srp login
 * @hostdata:	ibmvscsi_host_data of host
 * 
 * Returns zero if successful.
*/
static int do_srp_login(struct ibmvscsi_host_data *hostdata)
{
	DECLARE_MUTEX_LOCKED(sem);
	struct SRP_LOGIN_REQ req = {
		.type = SRP_LOGIN_REQ_TYPE,
		.max_requested_initiator_to_target_iulen = sizeof(union SRP_IU),
		.required_buffer_formats = 0x0002 /* direct and indirect */
	};
	struct srp_event_struct *evt_struct =
		evt_struct_for(&hostdata->pool, (union VIOSRP_IU *)&req, (void *)&sem, unblock_sem);

	if(!evt_struct) {
		printk(KERN_ERR "BORK! couldn't allocate an event for SRP_LOGIN_REQ!\n");
		return -1;
	}

	/* Start out with a request limit of 1, since this is negotiated in
	 * the login request we are just sending
	 */
	hostdata->request_limit = 1;
	evt_struct->crq.format = VIOSRP_SRP_FORMAT;
	if(ibmvscsi_send_srp_event(evt_struct, hostdata) != 0) {
		printk(KERN_ERR "BORK! couldn't send srp_event_struct!\n");
		ibmvscsi_free_event_struct(&hostdata->pool, evt_struct);
		return -1;
	}
	printk(KERN_INFO "Sent SRP login, sem %p:, req 0x%p\n", &sem, &req);

	/* Since we are going to block here, unlocking the I/O lock in 2.4
	 * is a very good thing.  This semaphore gets up-ed when the
	 * login response comes back.
	 */
	down(&sem);

	switch(evt_struct->evt->srp.generic.type) {
	case SRP_LOGIN_RSP_TYPE: /* it worked! */
		printk(KERN_INFO "SRP_LOGIN_RSP received!\n");
		break;
	case SRP_LOGIN_REJ_TYPE: /* refused! */
		printk(KERN_INFO "BORK! SRP_LOGIN_REQ rejected\n");
		return -1;
	default:
		printk(KERN_ERR "Invalid SRP_LOGIN_REQ response typecode 0x%02x!\n", evt_struct->evt->srp.generic.type);
		return -1;
	}
	
	/* Now we know what the real request-limit is */
	hostdata->request_limit = evt_struct->evt->srp.login_rsp.request_limit_delta;

	return 0;
}

/**
 * unblock_cmd: - Unblocks semaphore in scsi command
 * @evt_struct:	Scsi_Cmd with the semaphore
 *
 * Used as a "done" callback by when receiving scsi commands. Gets called
 * by ibmvscsi_handle_crq()
*/
static void unblock_cmd(Scsi_Cmnd *cmd)
{
	up((struct semaphore *)cmd->host_scribble);
}

/**
 * set_host_limits: - Get host limits from server and set the values in host
 * @host:	Scsi_Host to set the host limits
 *
*/
static void set_host_limits(struct Scsi_Host *host)
{
	DECLARE_MUTEX_LOCKED(sem);
	struct ibmvscsi_host_data *hostdata = *(struct ibmvscsi_host_data **)&host->hostdata;
	u64 report_luns_data[64] = { 0, };
	Scsi_Device dev = {
		.id = 0,
		.channel = 0,
		.lun = 0
	};
	Scsi_Cmnd cmd = {
		.use_sg = 0,
		.request_buffer = (void *)report_luns_data,
		.request_bufflen = sizeof(report_luns_data),
		.sc_data_direction = SCSI_DATA_READ,
		.device = &dev
	};
	struct report_luns_cdb {
		u8 opcode;
		u8 reserved1[1];
		u8 report;
		u8 reserved2[3];
		u32 length PACKED;
		u8 reserved3[1];
		u8 control;
	} *cdb = (struct report_luns_cdb *)&cmd.cmnd;
	struct srp_event_struct *evt;
	u32 max_lun_index;
	int i;

	cdb->opcode = 0xA0; /* REPORT_LUNS */
	cdb->length = sizeof(report_luns_data);

	evt = scsi_cmd_to_event_struct(&cmd, unblock_cmd, hostdata);
	if(!evt) {
		printk(KERN_ERR "couldn't allocate evt struct for host limits\n");
		host->max_id = 4;
		host->max_lun = 8;
		host->max_channel = 3;
		return;
	}

	evt->evt->srp.cmd.lun = 0;
	cmd.host_scribble = (unsigned char *)&sem;

	if(ibmvscsi_send_srp_event(evt, hostdata) != 0) {
		printk(KERN_ERR "ibmvscsi: failed to send event struct for host limits\n");
		unmap_cmd_data(&evt->evt->srp.cmd, hostdata->dma_dev);
		host->max_id = 4;
		host->max_lun = 8;
		host->max_channel = 3;
		return;
	}

	down(&sem);

	max_lun_index = ((report_luns_data[0] >> 32) / sizeof(u64))+ 1;
	printk(KERN_INFO "ibmvscsi: %d devices attached\n", max_lun_index - 1);
	for(i = 1; i < max_lun_index; ++i) {
		u16 lun_level = report_luns_data[i] >> 48;
		u8 target = (lun_level >> 8) & 0x3F;
		u8 bus = (lun_level >> 5) & 0x7;
		u8 lun = (lun_level >> 0) & 0x1F;
		if(host->max_id < target)
			host->max_id = target;
		if(host->max_lun < lun)
			host->max_lun = lun;
		if(host->max_channel < bus)
			host->max_channel = bus;
	}
	++host->max_id;
	++host->max_lun;
}

/**
 * add_host: - Initializes structures related to a new host found in server
 * @dma_dev:	dma_dev device found
 * @template:	Scsi_Host_Template of this driver
 *
*/
int add_host(struct dma_dev *dma_dev, Scsi_Host_Template *template)
{
	struct ibmvscsi_host_data *hostdata;
	struct Scsi_Host *host;

	hostdata = kmalloc(sizeof(*hostdata), GFP_KERNEL);
	if(!hostdata) {
		printk(KERN_ERR "ibmvscsi: couldn't kmalloc hostdata\n");
		goto kmalloc_failed;
	}
	memset(hostdata, 0x00, sizeof(*hostdata));
	hostdata->dma_dev = dma_dev;
	single_host_data = hostdata;
	tasklet_init(&hostdata->tasklet, ibmvscsi_task, (unsigned long)hostdata);

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
	if(do_srp_login(hostdata) != 0) {
		printk(KERN_ERR "ibmvscsi: couldn't SRP_LOGIN to remote host\n");
		goto srp_login_failed;
	}
	host = scsi_register(template, sizeof(hostdata));
	if(!host) {
		printk(KERN_ERR "ibmvscsi: couldn't scsi_register\n");
		goto scsi_register_failed;
	}

	*(struct ibmvscsi_host_data **)&host->hostdata = hostdata;
	++ibmvscsi_host_count;

	set_host_limits(host);
	return 0;

scsi_register_failed:
srp_login_failed:
	release_event_pool(&hostdata->pool, hostdata);
init_pool_failed:
	release_crq_queue(&hostdata->queue, hostdata);
init_crq_failed:
	kfree(hostdata);
kmalloc_failed:
	return -1;
}

/* ------------------------------------------------------------
 * Routines to complete Linux SCSI Host support
 */
/**
 * ibmvscsi_release: - The release function in the scsi template. Frees all resources.
 * @host:	Host to be released
 *
 * Always returns zero
*/
static int ibmvscsi_release(struct Scsi_Host *host)
{
	struct ibmvscsi_host_data *hostdata = *(struct ibmvscsi_host_data **)&host->hostdata;
	/* send an SRP_I_LOGOUT */
	printk(KERN_INFO "ibmvscsi: release called\n");

	release_event_pool(&hostdata->pool, hostdata);
	release_crq_queue(&hostdata->queue, hostdata);

	kfree(hostdata);

	scsi_unregister(host);

	close_path();
	
	return 0;
}

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
 * ibmvscsi_ioctl: - The ioctl function in the scsi template.
 * @dev:	Scsi dev to be controlled
 * @cmd:	ioctl command
 * @arg:	Arguments of ioctl
 *
 * Returns 0 on success, -errno on failure
*/
static int ibmvscsi_ioctl(Scsi_Device *dev, int cmd, void *arg)
{
	// fail
	return -EINVAL;
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
static int ibmvscsi_bios(Disk *disk, kdev_t dev, int *parm)
{
	off_t capacity = disk->capacity;
#else
static int ibmvscsi_bios(struct scsi_device *sdev, struct block_device *bdev, sector_t capacity, int parm[3])
{
#endif
	parm[0] = 255;
	parm[1] = 63;
	sector_div(capacity, 255*63);
	parm[2] = capacity;
	return 0;
}

/**
 * ibmvscsi_abort: Abort the command
 * just send this over to the server, but wait asynchronously for the response
 */
int ibmvscsi_abort(Scsi_Cmnd *cmd)
{
	struct srp_event_struct *cmd_evt = (struct srp_event_struct *)cmd->host_scribble;
	struct ibmvscsi_host_data *hostdata = *(struct ibmvscsi_host_data **)&cmd->device->host->hostdata;
	union VIOSRP_IU iu;
	struct SRP_TSK_MGMT *tsk_mgmt = &iu.srp.tsk_mgmt;
	struct SRP_RSP *rsp;
	struct srp_event_struct *evt;
	DECLARE_MUTEX_LOCKED(sem);

	if(!cmd_evt)
		return FAILED;

	memset(&iu, 0x00, sizeof(iu));
	tsk_mgmt->type = SRP_TSK_MGMT_TYPE;
	tsk_mgmt->lun = cmd_evt->evt->srp.cmd.lun;
	tsk_mgmt->task_mgmt_flags = 0x01; /* ABORT TASK */
	tsk_mgmt->managed_task_tag = cmd_evt->evt->srp.cmd.tag;
	printk("ibmvscsi: abort for lun 0x%lx, tag 0x%lx\n", tsk_mgmt->lun, tsk_mgmt->managed_task_tag);
	evt = evt_struct_for(&hostdata->pool, &iu, &sem, unblock_sem);
	if(!evt) {
		printk("ibmvscsi: failed to allocate abort() event struct\n");
		return FAILED;
	}

	if(ibmvscsi_send_srp_event(evt, hostdata) != 0) {
		printk("ibmvscsi: failed to send abort() event struct\n");
		ibmvscsi_free_event_struct(&hostdata->pool, evt);
		return FAILED;
	}

	down(&sem);

	if(evt->evt->srp.generic.type != SRP_RSP_TYPE) {
		printk("ibmvscsi: bad TSK_MGMT response type 0x%02x\n", evt->evt->srp.generic.type);
		return FAILED;
	}

	rsp = &evt->evt->srp.rsp;
	if(!rsp->rspvalid || (rsp->response_data_list_length != 4)) {
		printk("ibmvscsi: bad TSK_MGMT response--rsp invalid or invalid length\n");
		return FAILED;
	}

	if(rsp->sense_and_response_data[3] != 0x00) {
		printk("ibmvscsi: ABORT failed: response data rsp_code 0x%02x\n", rsp->sense_and_response_data[3]);
		return FAILED;
	}

	return SUCCESS;
}

int ibmvscsi_device_reset(Scsi_Cmnd *cmd)
{
	struct ibmvscsi_host_data *hostdata = *(struct ibmvscsi_host_data **)&cmd->device->host->hostdata;
	union VIOSRP_IU iu;
	struct SRP_TSK_MGMT *tsk_mgmt = &iu.srp.tsk_mgmt;
	struct SRP_RSP *rsp;
	struct srp_event_struct *evt;
	DECLARE_MUTEX_LOCKED(sem);

	memset(&iu, 0x00, sizeof(iu));
	tsk_mgmt->lun = ((u64)lun_from_dev(cmd->device)) << 48;
	tsk_mgmt->task_mgmt_flags = 0x08; // LUN_RESET
	printk("ibmvscsi: device reset for lun 0x%lx\n", tsk_mgmt->lun);
	evt = evt_struct_for(&hostdata->pool, &iu, &sem, unblock_sem);
	if(!evt) {
		printk("ibmvscsi: failed to allocate device_reset() event struct\n");
		return FAILED;
	}

	if(ibmvscsi_send_srp_event(evt, hostdata) != 0) {
		printk("ibmvscsi: failed to send device_reset() event struct\n");
		ibmvscsi_free_event_struct(&hostdata->pool, evt);
		return FAILED;
	}

	down(&sem);

	if(evt->evt->srp.generic.type != SRP_RSP_TYPE) {
		printk("ibmvscsi: bad TSK_MGMT response type 0x%02x\n", evt->evt->srp.generic.type);
		return FAILED;
	}

	rsp = &evt->evt->srp.rsp;
	if(!rsp->rspvalid || (rsp->response_data_list_length != 4)) {
		printk("ibmvscsi: bad TSK_MGMT response--rsp invalid or invalid length\n");
		return FAILED;
	}

	if(rsp->sense_and_response_data[3] != 0x00) {
		printk("ibmvscsi: LUN RESET failed: response data rsp_code 0x%02x\n", rsp->sense_and_response_data[3]);
		return FAILED;
	}

	return SUCCESS;
}

/* ------------------------------------------------------------
 * SCSI driver registration
 */
static Scsi_Host_Template driver_template = {
	.name = "ibmvscsi",
	.proc_name = "ibmvscsi",
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
	.use_new_eh_code = 1,
#endif
	.detect = ibmvscsi_detect,
	.release = ibmvscsi_release,
	.info = ibmvscsi_info,
	.ioctl = ibmvscsi_ioctl,
	.queuecommand = ibmvscsi_queue,
	.bios_param = ibmvscsi_bios,
	.eh_abort_handler = ibmvscsi_abort,
	.eh_device_reset_handler = ibmvscsi_device_reset,
	.can_queue = 10,
	.this_id = -1,
	.sg_tablesize = SG_TABLESIZE,
	.cmd_per_lun = 1,
	.use_clustering = DISABLE_CLUSTERING,
	.emulated = 1
};

#include "scsi_module.c"

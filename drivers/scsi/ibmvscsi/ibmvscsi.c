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
 * This driver supports the SCSI adapter implemented by the IBM
 * Power5 firmware.  That SCSI adapter is not a physical adapter,
 * but allows Linux SCSI peripheral drivers to directly
 * access devices in another logical partition on the physical system.
 *
 * The virtual adapter(s) are present in the open firmware device
 * tree just like real adapters.
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
 * routines have been broken out into rpa_vscsi.c and iseries_vscsi.c.
 * The Makefile should pick one, not two, not zero, of these.
 *
 * TODO: This is currently pretty tied to the IBM i/pSeries hypervisor
 * interfaces.  It would be really nice to abstract this above an RDMA
 * layer.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/dma-mapping.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include "ibmvscsi.h"

/* The values below are somewhat arbitrary default values, but 
 * OS/400 will use 3 busses (disks, CDs, tapes, I think.)
 * Note that there are 3 bits of channel value, 6 bits of id, and
 * 5 bits of LUN.
 */
static int max_id = 64;
static int max_channel = 3;
static int init_timeout = 5;

MODULE_DESCRIPTION("IBM Virtual SCSI");
MODULE_AUTHOR("Dave Boutcher");
MODULE_LICENSE("GPL");

module_param_named(max_id, max_id, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(max_id, "Largest ID value for each channel");
module_param_named(max_channel, max_channel, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(max_channel, "Largest channel value");
module_param_named(init_timeout, init_timeout, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(init_timeout, "Initialization timeout in seconds");

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
static int initialize_event_pool(struct event_pool *pool,
				 int size, struct ibmvscsi_host_data *hostdata)
{
	int i;

	pool->size = size;
	pool->events = kmalloc(pool->size * sizeof(*pool->events), GFP_KERNEL);
	if (!pool->events)
		return -ENOMEM;
	memset(pool->events, 0x00, pool->size * sizeof(*pool->events));

	pool->iu_storage =
	    dma_alloc_coherent(hostdata->dev,
			       pool->size * sizeof(*pool->iu_storage),
			       &pool->iu_token, 0);
	if (!pool->iu_storage) {
		kfree(pool->events);
		return -ENOMEM;
	}

	for (i = 0; i < pool->size; ++i) {
		struct srp_event_struct *evt = &pool->events[i];
		memset(&evt->crq, 0x00, sizeof(evt->crq));
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
static void release_event_pool(struct event_pool *pool,
			       struct ibmvscsi_host_data *hostdata)
{
	int i, in_use = 0;
	for (i = 0; i < pool->size; ++i)
		if (pool->events[i].in_use)
			++in_use;
	if (in_use)
		printk(KERN_WARNING
		       "ibmvscsi: releasing event pool with %d "
		       "events still in use?\n",
		       in_use);
	kfree(pool->events);
	dma_free_coherent(hostdata->dev,
			  pool->size * sizeof(*pool->iu_storage),
			  pool->iu_storage, pool->iu_token);
}

/**
 * ibmvscsi_valid_event_struct: - Determines if event is valid.
 * @pool:	event_pool that contains the event
 * @evt:	srp_event_struct to be checked for validity
 *
 * Returns zero if event is invalid, one otherwise.
*/
int ibmvscsi_valid_event_struct(struct event_pool *pool,
				struct srp_event_struct *evt)
{
	int index = evt - pool->events;
	if (index < 0 || index >= pool->size)	/* outside of bounds */
		return 0;
	if (evt != pool->events + index)	/* unaligned */
		return 0;
	return 1;
}

/**
 * ibmvscsi_free-event_struct: - Changes status of event to "free"
 * @pool:	event_pool that contains the event
 * @evt:	srp_event_struct to be modified
 *
*/
static void ibmvscsi_free_event_struct(struct event_pool *pool,
				       struct srp_event_struct *evt)
{
	if (!ibmvscsi_valid_event_struct(pool, evt)) {
		printk(KERN_ERR
		       "ibmvscsi: Freeing invalid event_struct %p "
		       "(not in pool %p)\n",
		       evt, pool->events);
		return;
	}
	if (!evt->in_use) {
		printk(KERN_ERR
		       "ibmvscsi: Freeing event_struct %p "
		       "which is not in use!\n",
		       evt);
		return;
	}
	evt->in_use = 0;
}

/**
 * ibmvscsi_get_event_struct: - Gets the next free event in pool
 * @pool:	event_pool that contains the events to be searched
 *
 * Returns the next event in "free" state, and NULL if none are free.
 * Note that no synchronization is done here, we assume the host_lock
 * will syncrhonze things.
*/
static
struct srp_event_struct *ibmvscsi_get_event_struct(struct event_pool *pool)
{
	struct srp_event_struct *cur, *last = pool->events + pool->size;

	for (cur = pool->events; cur < last; ++cur)
		if (!cur->in_use) {
			cur->in_use = 1;
			break;
		}

	if (cur >= last) {
		printk(KERN_ERR "ibmvscsi: found no event struct in pool!\n");
		return NULL;
	}

	return cur;
}

/**
 * evt_struct_for: - Initializes the next free event
 * @pool:	event_pool that contains events to be searched
 * @evt:	VIOSRP_IU that the event will point to
 * @cmnd:	The scsi cmnd object for this event.  Can be NULL
 * @done:	Callback function when event is processed
 *
 * Returns the initialized event, and NULL if there are no free events
 */
static
struct srp_event_struct *evt_struct_for(struct event_pool *pool,
					union VIOSRP_IU *evt,
					struct scsi_cmnd *cmnd,
					void (*done) (struct srp_event_struct
						      *))
{
	struct srp_event_struct *evt_struct = ibmvscsi_get_event_struct(pool);
	if (!evt_struct)
		return NULL;

	*evt_struct->evt = *evt;
	evt_struct->evt->srp.generic.tag = (u64) (unsigned long)evt_struct;

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
 * @dev:	device for which the memory is mapped
 *
*/
static void unmap_direct_data(struct SRP_CMD *cmd, struct device *dev)
{
	struct memory_descriptor *data =
	    (struct memory_descriptor *)cmd->additional_data;
	dma_unmap_single(dev, data->virtual_address, data->length,
			 DMA_BIDIRECTIONAL);
}

/**
 * unmap_direct_data: - Unmap array of address pointed by SRP_CMD
 * @cmd:	SRP_CMD whose additional_data member will be unmapped
 * @dev:	device for which the memory is mapped
 *
*/
static void unmap_indirect_data(struct SRP_CMD *cmd, struct device *dev)
{
	struct indirect_descriptor *indirect =
	    (struct indirect_descriptor *)cmd->additional_data;
	int i, num_mapped = indirect->head.length / sizeof(indirect->list[0]);
	for (i = 0; i < num_mapped; ++i) {
		struct memory_descriptor *data = &indirect->list[i];
		dma_unmap_single(dev,
				 data->virtual_address,
				 data->length, DMA_BIDIRECTIONAL);
	}
}

/**
 * unmap_direct_data: - Unmap data pointed in SRP_CMD based on the format
 * @cmd:	SRP_CMD whose additional_data member will be unmapped
 * @dev:	device for which the memory is mapped
 *
*/
static void unmap_cmd_data(struct SRP_CMD *cmd, struct device *dev)
{
	if ((cmd->data_out_format == SRP_NO_BUFFER) &&
	    (cmd->data_in_format == SRP_NO_BUFFER))
		return;
	else if ((cmd->data_out_format == SRP_DIRECT_BUFFER) ||
		 (cmd->data_in_format == SRP_DIRECT_BUFFER))
		unmap_direct_data(cmd, dev);
	else
		unmap_indirect_data(cmd, dev);
}

/**
 * map_sg_data: - Maps dma for a scatterlist and initializes decriptor fields
 * @cmd:	Scsi_Cmnd with the scatterlist
 * @srp_cmd:	SRP_CMD that contains the memory descriptor
 * @dev:	device for which to map dma memory
 *
 * Called by map_data_for_srp_cmd() when building srp cmd from scsi cmd.
 * Returns 1 on success.
*/
static int map_sg_data(struct scsi_cmnd *cmd,
		       struct SRP_CMD *srp_cmd, struct device *dev)
{

	int i, sg_mapped;
	u64 total_length = 0;
	struct scatterlist *sg = cmd->request_buffer;
	struct memory_descriptor *data =
	    (struct memory_descriptor *)srp_cmd->additional_data;
	struct indirect_descriptor *indirect =
	    (struct indirect_descriptor *)data;
	sg_mapped = dma_map_sg(dev, sg, cmd->use_sg, DMA_BIDIRECTIONAL);

	/* special case; we can use a single direct descriptor */
	if (sg_mapped == 1) {
		if (cmd->sc_data_direction == DMA_TO_DEVICE)
			srp_cmd->data_out_format = SRP_DIRECT_BUFFER;
		else
			srp_cmd->data_in_format = SRP_DIRECT_BUFFER;
		data->virtual_address = sg_dma_address(&sg[0]);
		data->length = sg_dma_len(&sg[0]);
		data->memory_handle = 0;
		return 1;
	}

	if (sg_mapped > MAX_INDIRECT_BUFS) {
		printk(KERN_ERR
		       "ibmvscsi: More than %d mapped sg entries, got %d\n",
		       MAX_INDIRECT_BUFS, sg_mapped);
		return 0;
	}

	if (cmd->sc_data_direction == DMA_TO_DEVICE) {
		srp_cmd->data_out_format = SRP_INDIRECT_BUFFER;
		srp_cmd->data_out_count = sg_mapped;
	} else {
		srp_cmd->data_in_format = SRP_INDIRECT_BUFFER;
		srp_cmd->data_in_count = sg_mapped;
	}
	indirect->head.virtual_address = 0;
	indirect->head.length = sg_mapped * sizeof(indirect->list[0]);
	indirect->head.memory_handle = 0;
	for (i = 0; i < sg_mapped; ++i) {
		struct memory_descriptor *descr = &indirect->list[i];
		struct scatterlist *sg_entry = &sg[i];
		descr->virtual_address = sg_dma_address(sg_entry);
		descr->length = sg_dma_len(sg_entry);
		descr->memory_handle = 0;
		total_length += sg_dma_len(sg_entry);
	}
	indirect->total_length = total_length;

	return 1;
}

/**
 * map_sg_data: - Maps memory and initializes memory decriptor fields
 * @cmd:	struct scsi_cmnd with the memory to be mapped
 * @srp_cmd:	SRP_CMD that contains the memory descriptor
 * @dev:	device for which to map dma memory
 *
 * Called by map_data_for_srp_cmd() when building srp cmd from scsi cmd.
 * Returns 1 on success.
*/
static int map_single_data(struct scsi_cmnd *cmd,
			   struct SRP_CMD *srp_cmd, struct device *dev)
{
	struct memory_descriptor *data =
	    (struct memory_descriptor *)srp_cmd->additional_data;

	data->virtual_address =
	    (u64) (unsigned long)dma_map_single(dev, cmd->request_buffer,
						cmd->request_bufflen,
						DMA_BIDIRECTIONAL);
	/* FIXME: one day this should return something other than 
	 * 0xFFFFFFFF in case of error. currently arch/ppc64 returns
	 * ((dma_addr_t)-1) in some cases
	 */
	if (data->virtual_address == 0xFFFFFFFF) {
		printk(KERN_ERR
		       "ibmvscsi: Unable to map request_buffer for command!\n");
		return 0;
	}
	data->length = cmd->request_bufflen;
	data->memory_handle = 0 /* viopath_sourceinst(viopath_hostLp) */ ;

	if (cmd->sc_data_direction == DMA_TO_DEVICE)
		srp_cmd->data_out_format = SRP_DIRECT_BUFFER;
	else
		srp_cmd->data_in_format = SRP_DIRECT_BUFFER;

	return 1;
}

/**
 * map_data_for_srp_cmd: - Calls functions to map data for srp cmds
 * @cmd:	struct scsi_cmnd with the memory to be mapped
 * @srp_cmd:	SRP_CMD that contains the memory descriptor
 * @dev:	dma device for which to map dma memory
 *
 * Called by scsi_cmd_to_srp_cmd() when converting scsi cmds to srp cmds 
 * Returns 1 on success.
*/
static int map_data_for_srp_cmd(struct scsi_cmnd *cmd,
				struct SRP_CMD *srp_cmd, struct device *dev)
{
	switch (cmd->sc_data_direction) {
	case DMA_FROM_DEVICE:
	case DMA_TO_DEVICE:
		break;
	case DMA_NONE:
		return 1;
	case DMA_BIDIRECTIONAL:
		printk(KERN_ERR
		       "ibmvscsi: Can't map DMA_BIDIRECTIONAL to read/write\n");
		return 0;
	default:
		printk(KERN_ERR
		       "ibmvscsi: Unknown data direction 0x%02x; can't map!\n",
		       cmd->sc_data_direction);
		return 0;
	}

	if (!cmd->request_buffer)
		return 1;
	if (cmd->use_sg)
		return map_sg_data(cmd, srp_cmd, dev);
	return map_single_data(cmd, srp_cmd, dev);
}

/* ------------------------------------------------------------
 * Routines for sending and receiving SRPs
 */
/**
 * ibmvscsi_send_srp_event: - Transforms event to u64 array and calls send_crq()
 * @evt_struct:	evt_struct to be sent
 * @hostdata:	ibmvscsi_host_data of host
 *
 * Returns the value returned from ibmvscsi_send_crq(). (Zero for success)
 * Note that this routine assumes that host_lock is held for synchronization
*/
static int ibmvscsi_send_srp_event(struct srp_event_struct *evt_struct,
				   struct ibmvscsi_host_data *hostdata)
{
	struct scsi_cmnd *cmnd;
	u64 *crq_as_u64 = (u64 *) & evt_struct->crq;

	/* If we have exhausted our request limit, just queue this request.
	 * Note that there are rare cases involving driver generated requests 
	 * (such as task management requests) that the mid layer may think we
	 * can handle more requests (can_queue) when we actually can't
	 */
	if (atomic_dec_if_positive(&hostdata->request_limit) < 0) {
		printk("ibmvscsi: Warning, request_limit exceeded\n");
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	/* Add this to the sent list.  We need to do this 
	 * before we actually send 
	 * in case it comes back REALLY fast
	 */
	list_add_tail(&evt_struct->list, &hostdata->sent);

	if (ibmvscsi_send_crq(hostdata, crq_as_u64[0], crq_as_u64[1]) != 0) {
		list_del(&evt_struct->list);

		cmnd = evt_struct->cmnd;
		printk(KERN_ERR "ibmvscsi: failed to send event struct\n");
		unmap_cmd_data(&evt_struct->evt->srp.cmd, hostdata->dev);
		ibmvscsi_free_event_struct(&hostdata->pool, evt_struct);
		cmnd->result = DID_ERROR << 16;
		evt_struct->cmnd_done(cmnd);
	}

	return 0;
}

/**
 * handle_cmd_rsp: -  Handle responses from commands
 * @evt_struct:	srp_event_struct to be handled
 *
 * Used as a callback by when sending scsi cmds (by scsi_cmd_to_event_struct). 
 * Gets called by ibmvscsi_handle_crq()
*/
static void handle_cmd_rsp(struct srp_event_struct *evt_struct)
{
	struct SRP_RSP *rsp = &evt_struct->evt->srp.rsp;
	struct scsi_cmnd *cmnd = (struct scsi_cmnd *)evt_struct->cmnd;

	if (cmnd) {
		cmnd->result |= rsp->status;
		if (((cmnd->result >> 1) & 0x1f) == CHECK_CONDITION)
			memcpy(cmnd->sense_buffer,
			       rsp->sense_and_response_data,
			       rsp->sense_data_list_length);
		unmap_cmd_data(&evt_struct->cmd, evt_struct->hostdata->dev);

		if (rsp->dounder)
			cmnd->resid = rsp->data_out_residual_count;
		else if (rsp->diunder)
			cmnd->resid = rsp->data_in_residual_count;
	}

	if (evt_struct->cmnd_done) {
		evt_struct->cmnd_done(cmnd);
	}
}

/* ------------------------------------------------------------
 * Routines for queuing individual SCSI commands to the hosting partition
 */

/**
 * lun_from_dev: - Returns the lun of the scsi device
 * @dev:	struct scsi_device
 *
*/
static inline u16 lun_from_dev(struct scsi_device *dev)
{
	return (0x2 << 14) | (dev->id << 8) | (dev->channel << 5) | dev->lun;
}

/**
 * scsi_cmd_to_srp_cmd: - Initializes srp cmd with data from scsi cmd
 * @cmd:	source struct scsi_cmnd
 * @srp_cmd:	target SRP_CMD
 * @hostdata:	ibmvscsi_host_data of host
 *
 * Returns 1 on success.
*/
static int scsi_cmd_to_srp_cmd(struct scsi_cmnd *cmd,
			       struct SRP_CMD *srp_cmd,
			       struct ibmvscsi_host_data *hostdata)
{
	u16 lun = lun_from_dev(cmd->device);
	memset(srp_cmd, 0x00, sizeof(*srp_cmd));

	srp_cmd->type = SRP_CMD_TYPE;
	memcpy(srp_cmd->cdb, cmd->cmnd, sizeof(cmd->cmnd));
	srp_cmd->lun = ((u64) lun) << 48;

	return map_data_for_srp_cmd(cmd, srp_cmd, hostdata->dev);
}

/**
 * scsi_cmd_to_event_struct: - Initializes a srp_event_struct 
 *                             with data from scsi cmd
 * @cmd:	Source struct scsi_cmnd
 * @done:	Callback function to be called when cmd is completed
 * @hostdata:	ibmvscsi_host_data of host
 *
 * Returns the srp_event_struct to be used or NULL if not successful.
*/
static struct srp_event_struct *scsi_cmd_to_event_struct(struct scsi_cmnd *cmd,
							 void (*done) (struct
								       scsi_cmnd
								       *),
							 struct
							 ibmvscsi_host_data
							 *hostdata)
{
	struct SRP_CMD srp_cmd;
	struct srp_event_struct *evt_struct;

	if (!scsi_cmd_to_srp_cmd(cmd, &srp_cmd, hostdata)) {
		printk(KERN_ERR "ibmvscsi: couldn't convert cmd to SRP_CMD\n");
		return NULL;
	}

	evt_struct = evt_struct_for(&hostdata->pool,
				    (union VIOSRP_IU *)&srp_cmd,
				    (void *)cmd, handle_cmd_rsp);
	if (!evt_struct) {
		printk(KERN_ERR "ibmvscsi: evt_struct_for() returned NULL\n");
		return NULL;
	}

	evt_struct->cmd = srp_cmd;
	evt_struct->cmnd_done = done;
	evt_struct->crq.timeout = cmd->timeout;
	return evt_struct;
}

/**
 * ibmvscsi_queue: - The queuecommand function of the scsi template 
 * @cmd:	struct scsi_cmnd to be executed
 * @done:	Callback function to be called when cmd is completed
 *
 * Always returns zero
*/
static
int ibmvscsi_queue(struct scsi_cmnd *cmd, void (*done) (struct scsi_cmnd *))
{
	struct ibmvscsi_host_data *hostdata =
	    (struct ibmvscsi_host_data *)&cmd->device->host->hostdata;
	struct srp_event_struct *evt_struct =
	    scsi_cmd_to_event_struct(cmd, done, hostdata);

	if (!evt_struct) {
		printk(KERN_ERR
		       "ibmvscsi: can't convert struct scsi_cmnd to event\n");
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	evt_struct->crq.format = VIOSRP_SRP_FORMAT;

	return ibmvscsi_send_srp_event(evt_struct, hostdata);
}

/* ------------------------------------------------------------
 * Routines for driver initialization
 */
/**
 * login_rsp: - Handle response to SRP login request
 * @evt_struct:	srp_event_struct with the response
 *
 * Used as a "done" callback by when sending srp_login. Gets called
 * by ibmvscsi_handle_crq()
*/
static void login_rsp(struct srp_event_struct *evt_struct)
{
	struct ibmvscsi_host_data *hostdata = evt_struct->hostdata;
	switch (evt_struct->evt->srp.generic.type) {
	case SRP_LOGIN_RSP_TYPE:	/* it worked! */
		break;
	case SRP_LOGIN_REJ_TYPE:	/* refused! */
		printk(KERN_INFO "ibmvscsi: SRP_LOGIN_REQ rejected\n");
		/* Login failed.  */
		atomic_set(&hostdata->request_limit, -1);
		return;
	default:
		printk(KERN_ERR
		       "ibmvscsi: Invalid login response typecode 0x%02x!\n",
		       evt_struct->evt->srp.generic.type);
		/* Login failed.  */
		atomic_set(&hostdata->request_limit, -1);
		return;
	}

	printk(KERN_INFO "ibmvscsi: SRP_LOGIN succeeded\n");

	/* Now we know what the real request-limit is */
	atomic_set(&hostdata->request_limit,
		   evt_struct->evt->srp.login_rsp.request_limit_delta);

	hostdata->host->can_queue =
	    evt_struct->evt->srp.login_rsp.request_limit_delta;

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
	int rc;
	unsigned long flags;

	struct SRP_LOGIN_REQ req = {
		.type = SRP_LOGIN_REQ_TYPE,
		.max_requested_initiator_to_target_iulen = sizeof(union SRP_IU),
		.required_buffer_formats = 0x0002	/* direct and indirect */
	};
	struct srp_event_struct *evt_struct = evt_struct_for(&hostdata->pool,
							     (union VIOSRP_IU *)
							     &req,
							     NULL,
							     login_rsp);

	if (!evt_struct) {
		printk(KERN_ERR
		       "ibmvscsi: couldn't allocate an event for login req!\n");
		return FAILED;
	}

	/* Start out with a request limit of 1, since this is negotiated in
	 * the login request we are just sending
	 */
	atomic_set(&hostdata->request_limit, 1);
	evt_struct->crq.format = VIOSRP_SRP_FORMAT;

	spin_lock_irqsave(hostdata->host->host_lock, flags);
	rc = ibmvscsi_send_srp_event(evt_struct, hostdata);
	spin_unlock_irqrestore(hostdata->host->host_lock, flags);
	return rc;
};

/**
 * sync_completion: Signal that a synchronous command has completed
 * Note that after returning from this call, the evt_struct is freed.
 * the caller waiting on this completion shouldn't touch the evt_struct
 * again.
 */
static void sync_completion(struct srp_event_struct *evt_struct)
{
	complete(&evt_struct->comp);
}

/**
 * ibmvscsi_abort: Abort a command...from scsi host template
 * send this over to the server and wait synchronously for the response
 */
static int ibmvscsi_abort(struct scsi_cmnd *cmd)
{
	struct ibmvscsi_host_data *hostdata =
	    (struct ibmvscsi_host_data *)cmd->device->host->hostdata;
	union VIOSRP_IU iu;
	struct SRP_TSK_MGMT *tsk_mgmt = &iu.srp.tsk_mgmt;
	struct SRP_RSP *rsp;
	struct srp_event_struct *evt;
	struct srp_event_struct *tmp_evt, *found_evt;
	u16 lun = lun_from_dev(cmd->device);

	/* First, find this command in our sent list so we can figure
	 * out the correct tag
	 */
	found_evt = NULL;
	list_for_each_entry(tmp_evt, &hostdata->sent, list) {
		if (tmp_evt->cmnd == cmd) {
			found_evt = tmp_evt;
			break;
		}
	}

	/* Set up an abort SRP command */
	memset(&iu, 0x00, sizeof(iu));
	tsk_mgmt->type = SRP_TSK_MGMT_TYPE;
	tsk_mgmt->lun = ((u64) lun) << 48;
	tsk_mgmt->task_mgmt_flags = 0x01;	/* ABORT TASK */
	tsk_mgmt->managed_task_tag = (u64) (unsigned long)found_evt;

	printk(KERN_INFO "ibmvscsi: aborting command. lun 0x%lx, tag 0x%lx\n",
	       tsk_mgmt->lun, tsk_mgmt->managed_task_tag);

	evt = evt_struct_for(&hostdata->pool, &iu, NULL, sync_completion);
	if (!evt) {
		printk(KERN_ERR "ibmvscsi: failed to allocate abort() event\n");
		return FAILED;
	}
	evt->crq.format = VIOSRP_SRP_FORMAT;

	init_completion(&evt->comp);
	if (ibmvscsi_send_srp_event(evt, hostdata) != 0) {
		printk(KERN_ERR "ibmvscsi: failed to send abort() event\n");
		ibmvscsi_free_event_struct(&hostdata->pool, evt);
		return FAILED;
	}
	
	spin_unlock(hostdata->host->host_lock);
	wait_for_completion(&evt->comp);
	spin_lock(hostdata->host->host_lock);

	/* Because we dropped the spinlock above, it's possible
	 * The event is no longer in our list.  Make sure it didn't
	 * complete while we were aborting
	 */
	list_for_each_entry(tmp_evt, &hostdata->sent, list) {
		if (tmp_evt->cmnd == cmd) {
			found_evt = tmp_evt;
			break;
		}
	}
	if (found_evt == NULL)
		return SUCCESS;

	cmd->result = (DID_ABORT << 16);
	list_del(&found_evt->list);
	unmap_cmd_data(&found_evt->cmd, found_evt->hostdata->dev);
	ibmvscsi_free_event_struct(&found_evt->hostdata->pool, found_evt);
	atomic_inc(&hostdata->request_limit);
	printk(KERN_INFO
	       "ibmvscsi: successfully aborted task tag 0x%lx\n",
	       tsk_mgmt->managed_task_tag);
	return SUCCESS;
}

/**
 * purge_requests: Our virtual adapter just shut down.  purge any sent requests 
 * @hostdata:    the adapter
 */
static void purge_requests(struct ibmvscsi_host_data *hostdata)
{
	struct srp_event_struct *tmp_evt, *pos;
	unsigned long flags;

	spin_lock_irqsave(hostdata->host->host_lock, flags);
	list_for_each_entry_safe(tmp_evt, pos, &hostdata->sent, list) {
		tmp_evt->cmnd->result = (DID_ERROR << 16);
		list_del(&tmp_evt->list);
		unmap_cmd_data(&tmp_evt->cmd, tmp_evt->hostdata->dev);
		ibmvscsi_free_event_struct(&tmp_evt->hostdata->pool, tmp_evt);
		if (tmp_evt->cmnd_done) {
			spin_unlock_irqrestore(hostdata->host->host_lock,
					       flags);
			tmp_evt->cmnd_done(tmp_evt->cmnd);
			spin_lock_irqsave(hostdata->host->host_lock, flags);
		}
	}
	spin_unlock_irqrestore(hostdata->host->host_lock, flags);
}

/**
 * ibmvscsi_handle_crq: - Handles and frees received events in the CRQ
 * @crq:	Command/Response queue
 * @hostdata:	ibmvscsi_host_data of host
 *
*/
void ibmvscsi_handle_crq(struct VIOSRP_CRQ *crq,
			 struct ibmvscsi_host_data *hostdata)
{
	unsigned long flags;
	struct srp_event_struct *evt_struct =
	    (struct srp_event_struct *)crq->IU_data_ptr;
	switch (crq->valid) {
	case 0xC0:		/* initialization */
		switch (crq->format) {
		case 0x01:	/* Initialization message */
			printk(KERN_INFO "ibmvscsi: partner initialized\n");
			/* Send back a response */
			if (ibmvscsi_send_crq(hostdata, 
					      0xC002000000000000, 0) == 0) {
				/* Now login */
				send_srp_login(hostdata);
			} else {
				printk(KERN_ERR 
				       "ibmvscsi: Unable to send init rsp\n");
			}
				       
			break;
		case 0x02:	/* Initialization response */
			printk(KERN_INFO
			       "ibmvscsi: partner initialization complete\n");

			/* Now login */
			send_srp_login(hostdata);
			break;
		default:
			printk(KERN_ERR "ibmvscsi: unknown crq message type\n");
		}
		return;
	case 0xFF:		/* Hypervisor telling us the connection is closed */
		printk(KERN_INFO "ibmvscsi: Virtual adapter failed!\n");

		atomic_set(&hostdata->request_limit, -1);
		purge_requests(hostdata);
		return;
	case 0x80:		/* real payload */
		break;
	default:
		printk(KERN_ERR
		       "ibmvscsi: got an invalid message type 0x%02x\n",
		       crq->valid);
		return;
	}

	/* The only kind of payload CRQs we should get are responses to 
	 * things we send. Make sure this response is to something we 
	 * actually sent
	 */
	if (!ibmvscsi_valid_event_struct(&hostdata->pool, evt_struct)) {
		printk(KERN_ERR
		       "ibmvscsi: returned correlation_token 0x%p is invalid!\n",
		       (void *)crq->IU_data_ptr);
		return;
	}

	if (crq->format == VIOSRP_SRP_FORMAT)
		atomic_add(evt_struct->evt->srp.rsp.request_limit_delta,
			   &hostdata->request_limit);

	if (evt_struct->done)
		evt_struct->done(evt_struct);
	else
		printk(KERN_ERR
		       "ibmvscsi: returned done() is NULL; not running it!\n");

	/*
	 * Lock the host_lock before messing with these structures, since we
	 * are running in a task context
	 */
	spin_lock_irqsave(evt_struct->hostdata->host->host_lock, flags);
	list_del(&evt_struct->list);
	ibmvscsi_free_event_struct(&evt_struct->hostdata->pool, evt_struct);
	spin_unlock_irqrestore(evt_struct->hostdata->host->host_lock, flags);
}

/**
 * ibmvscsi_get_host_config: Send the command to the server to get host
 * configuration data.  The data is opaque to us.
 */
int ibmvscsi_do_host_config(struct ibmvscsi_host_data *hostdata, 
			    unsigned char *buffer, int length) {
	struct VIOSRP_HOST_CONFIG host_config;
	struct srp_event_struct *evt_struct;
	int rc;
	
	memset(&host_config, 0x00, sizeof(host_config));
	host_config.common.type = VIOSRP_HOST_CONFIG_TYPE;
	host_config.common.length = length;
	host_config.buffer = dma_map_single(hostdata->dev, buffer, length,
					    DMA_BIDIRECTIONAL);
	
	evt_struct = evt_struct_for(&hostdata->pool, 
				    (union VIOSRP_IU *)&host_config, 
				    NULL, 
				    sync_completion);
	
	if (!evt_struct) {
		printk(KERN_ERR 
		       "ibmvscsi: could't allocate event for HOST_CONFIG!\n");
		rc = -1;
	} else {
		evt_struct->crq.format = VIOSRP_MAD_FORMAT;
		init_completion(&evt_struct->comp);
		rc =  ibmvscsi_send_srp_event(evt_struct, hostdata);
		if (rc == 0) {
			wait_for_completion(&evt_struct->comp);
		}
	}
	
	dma_unmap_single(hostdata->dev, host_config.buffer, length, 
			 DMA_BIDIRECTIONAL);

	return rc ? rc : host_config.common.status;
}

/* ------------------------------------------------------------
 * SCSI driver registration
 */
static struct scsi_host_template driver_template = {
	.module = THIS_MODULE,
	.name = "SCSI host adapter emulator for RPA/iSeries Virtual I/O",
	.proc_name = "ibmvscsi",
	.queuecommand = ibmvscsi_queue,
	.eh_abort_handler = ibmvscsi_abort,
	.can_queue = 1,		/* Updated after SRP_LOGIN */
	.this_id = -1,
	.sg_tablesize = MAX_INDIRECT_BUFS,
	.cmd_per_lun = 1,
	.use_clustering = DISABLE_CLUSTERING,
	.emulated = 1
};

/**
 * Called by bus code for each adapter
 */
struct ibmvscsi_host_data *ibmvscsi_probe(struct device *dev)
{
	struct ibmvscsi_host_data *hostdata;
	struct Scsi_Host *host;
	unsigned long wait_switch = 0;

	host = scsi_host_alloc(&driver_template, sizeof(*hostdata));
	if (!host) {
		printk(KERN_ERR "ibmvscsi: couldn't allocate host data\n");
		goto scsi_host_alloc_failed;
	}

	hostdata = (struct ibmvscsi_host_data *)host->hostdata;
	memset(hostdata, 0x00, sizeof(*hostdata));
	INIT_LIST_HEAD(&hostdata->sent);
	hostdata->host = host;
	hostdata->dev = dev;
	atomic_set(&hostdata->request_limit, -1);

	if (ibmvscsi_init_crq_queue(&hostdata->queue, hostdata) != 0) {
		printk(KERN_ERR "ibmvscsi: couldn't initialize crq\n");
		goto init_crq_failed;
	}
	if (initialize_event_pool(&hostdata->pool,
				  IBMVSCSI_MAX_REQUESTS, hostdata) != 0) {
		printk(KERN_ERR "ibmvscsi: couldn't initialize event pool\n");
		goto init_pool_failed;
	}

	host->max_lun = 8;
	host->max_id = max_id;
	host->max_channel = max_channel;

	if (scsi_add_host(hostdata->host, hostdata->dev))
		goto add_host_failed;
		
	/* Try to send an initialization message.  Note that this is allowed
	 * to fail if the other end is not acive.  In that case we don't
	 * want to scan
	 */
	if (ibmvscsi_send_crq(hostdata, 0xC001000000000000, 0) == 0) {
		/*
		 * Wait around max init_timeout secs for the adapter to finish
		 * initializing. When we are done initializing, we will have a 
		 * valid request_limit.  We don't want Linux scanning before 
		 * we are ready.
		 */
		for (wait_switch = jiffies + (init_timeout * HZ);
		     time_before(jiffies,wait_switch) &&
			     atomic_read(&hostdata->request_limit) < 0;) {
			
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(5);
		}

		/* if we now have a valid request_limit, initiate a scan */
		if (atomic_read(&hostdata->request_limit) > 0) 
			scsi_scan_host(host);
	}
	
	return hostdata;

      add_host_failed:
	release_event_pool(&hostdata->pool, hostdata);
      init_pool_failed:
	ibmvscsi_release_crq_queue(&hostdata->queue, hostdata);
      init_crq_failed:
	scsi_host_put(host);
      scsi_host_alloc_failed:
	return NULL;
}

void ibmvscsi_remove(struct ibmvscsi_host_data *hostdata)
{
	release_event_pool(&hostdata->pool, hostdata);
	ibmvscsi_release_crq_queue(&hostdata->queue, hostdata);

	scsi_remove_host(hostdata->host);
	scsi_host_put(hostdata->host);
	return;
}

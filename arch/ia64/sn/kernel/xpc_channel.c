/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2004 Silicon Graphics, Inc.  All Rights Reserved.
 */


/*
 * Cross Partition Communication (XPC) channel support.
 *
 *	This is the part of XPC that manages the channels and
 *	sends/receives messages across them to/from other partitions.
 *
 */


#ifndef	SN_PROM
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <asm/sn/sn_sal.h>
#else /* ! SN_PROM */
#include "main/main.h"
#include "libc/libc.h"
#endif /* ! SN_PROM */
#include "xpc.h"


/*
 * Set up the initial values for the XPartition Communication channels.
 */
static void
xpc_initialize_channels(xpc_partition_t *part, partid_t partid)
{
	int ch_number;
	xpc_channel_t *ch;


	for (ch_number = 0; ch_number < part->nchannels; ch_number++) {
		ch = &part->channels[ch_number];

		ch->partid = partid;
		ch->number = ch_number;
		ch->flags = XPC_C_DISCONNECTED;

		ch->local_GP = &part->local_GPs[ch_number];
		ch->local_openclose_args =
					&part->local_openclose_args[ch_number];

		atomic_set(&ch->kthreads_assigned, 0);
		atomic_set(&ch->kthreads_idle, 0);
		atomic_set(&ch->kthreads_active, 0);

		atomic_set(&ch->references, 0);
		atomic_set(&ch->n_to_notify, 0);

		spin_lock_init(&ch->lock);
		sema_init(&ch->msg_to_pull_sema, 1);	/* mutex */

		atomic_set(&ch->n_on_msg_allocate_wq, 0);
		init_waitqueue_head(&ch->msg_allocate_wq);
		init_waitqueue_head(&ch->idle_wq);
	}
}


/*
 * Setup the infrastructure necessary to support XPartition Communication
 * between the specified remote partition and the local one.
 */
xpc_t
xpc_setup_infrastructure(xpc_partition_t *part)
{
	int ret;
	partid_t partid = XPC_PARTID(part);


	/*
	 * Zero out MOST of the entry for this partition. Only the fields
	 * following `references' will be zeroed. The others must remain
	 * `viable' across partition ups and downs, since they may be
	 * referenced during this memset() operation.
	 */
	memset((u8 *)&part->references + sizeof(part->references), 0,
		(u64)(part + 1) - (u64)((u8 *)&part->references +
						sizeof(part->references)));

	/*
	 * Allocate all of the channel structures as a contiguous chunk of
	 * memory.
	 */
	part->channels = kmalloc(sizeof(xpc_channel_t) * XPC_NCHANNELS,
								GFP_KERNEL);
	if (part->channels == NULL) {
		DPRINTK_ALWAYS(xpc_chan, (XPC_DBG_C_SETUP | XPC_DBG_C_ERROR),
			KERN_ERR "XPC: can't get memory for channels\n");
		return xpcNoMemory;
	}
	memset(part->channels, 0, sizeof(xpc_channel_t) * XPC_NCHANNELS);

	part->nchannels = XPC_NCHANNELS;


	/* allocate all the required GET/PUT values */

	part->local_GPs = kmalloc(XPC_GP_SIZE, GFP_KERNEL);
	if (part->local_GPs == NULL) {
		kfree(part->channels);
		DPRINTK_ALWAYS(xpc_chan, (XPC_DBG_C_SETUP | XPC_DBG_C_ERROR),
			KERN_ERR "XPC: can't get memory for local get/put "
			"values\n");
		return xpcNoMemory;
	}
	XP_ASSERT(L1_CACHE_ALIGNED(part->local_GPs));
	memset(part->local_GPs, 0, XPC_GP_SIZE);

	part->remote_GPs = kmalloc(XPC_GP_SIZE, GFP_KERNEL);
	if (part->remote_GPs == NULL) {
		kfree(part->channels);
		kfree(part->local_GPs);
		DPRINTK_ALWAYS(xpc_chan, (XPC_DBG_C_SETUP | XPC_DBG_C_ERROR),
			KERN_ERR "XPC: can't get memory for remote get/put "
			"values\n");
		return xpcNoMemory;
	}
	XP_ASSERT(L1_CACHE_ALIGNED(part->remote_GPs));
	memset(part->remote_GPs, 0, XPC_GP_SIZE);


	/* allocate all the required open and close args */

	part->local_openclose_args = kmalloc(XPC_OPENCLOSE_ARGS_SIZE,
								GFP_KERNEL);
	if (part->local_openclose_args == NULL) {
		kfree(part->channels);
		kfree(part->local_GPs);
		kfree(part->remote_GPs);
		DPRINTK_ALWAYS(xpc_chan, (XPC_DBG_C_SETUP | XPC_DBG_C_ERROR),
			KERN_ERR "XPC: can't get memory for local connect "
			"args\n");
		return xpcNoMemory;
	}
	XP_ASSERT(L1_CACHE_ALIGNED(part->local_openclose_args));
	memset(part->local_openclose_args, 0, XPC_OPENCLOSE_ARGS_SIZE);

	part->remote_openclose_args = kmalloc(XPC_OPENCLOSE_ARGS_SIZE,
								GFP_KERNEL);
	if (part->remote_openclose_args == NULL) {
		kfree(part->channels);
		kfree(part->local_GPs);
		kfree(part->remote_GPs);
		kfree(part->local_openclose_args);
		DPRINTK_ALWAYS(xpc_chan, (XPC_DBG_C_SETUP | XPC_DBG_C_ERROR),
			KERN_ERR "XPC: can't get memory for remote connect "
			"args\n");
		return xpcNoMemory;
	}
	XP_ASSERT(L1_CACHE_ALIGNED(part->remote_openclose_args));
	memset(part->remote_openclose_args, 0, XPC_OPENCLOSE_ARGS_SIZE);


	xpc_initialize_channels(part, partid);

	atomic_set(&part->nchannels_active, 0);


	/* local_IPI_amo were set to 0 by an earlier memset() */

	/* Initialize this partitions AMO_t structure */
	part->local_IPI_amo_va = xpc_IPI_init(partid);

	spin_lock_init(&part->IPI_lock);

	atomic_set(&part->channel_mgr_requests, 1);
	init_waitqueue_head(&part->channel_mgr_wq);

	sprintf(part->IPI_owner, "xpc%02d", partid);
	ret = XPC_REQUEST_IRQ(SGI_XPC_NOTIFY, xpc_notify_IRQ_handler, SA_SHIRQ,
				part->IPI_owner, (void *) (u64) partid);
	if (ret != 0) {
		kfree(part->channels);
		kfree(part->local_GPs);
		kfree(part->remote_GPs);
		kfree(part->local_openclose_args);
		kfree(part->remote_openclose_args);
		DPRINTK_ALWAYS(xpc_chan, (XPC_DBG_C_SETUP | XPC_DBG_C_ERROR),
			KERN_ERR "XPC: can't register NOTIFY IRQ handler, "
			"errno=%d\n", -ret);
		return xpcLackOfResources;
	}

	/* Setup a timer to check for dropped IPIs */
	XPC_INIT_TIMER(&part->dropped_IPI_timer, xpc_dropped_IPI_check, part,
			XPC_TICKS + XPC_P_DROPPED_IPI_WAIT);

	/*
	 * With the setting of the partition setup_state to XPC_P_SETUP, we're
	 * declaring that this partition is ready to go.
	 */
	part->setup_state = XPC_P_SETUP;


	/*
	 * Setup the per partition specific variables required by the
	 * remote partition to establish channel connections with us.
	 *
	 * The setting of the magic # indicates that these per partition
	 * specific variables are ready to be used.
	 */
	xpc_vars_part[partid].GPs_pa = __pa(part->local_GPs);
	xpc_vars_part[partid].openclose_args_pa =
					__pa(part->local_openclose_args);
	xpc_vars_part[partid].IPI_amo_pa = __pa(part->local_IPI_amo_va);
	xpc_vars_part[partid].IPI_cpuid = cpu_physical_id(smp_processor_id());
	xpc_vars_part[partid].nchannels = part->nchannels;
	xpc_vars_part[partid].magic = XPC_VP_MAGIC1;

	return xpcSuccess;
}


/*
 * Create a wrapper that hides the underlying mechanism for pulling a cacheline
 * (or multiple cachelines) from a remote partition.
 *
 * src must be a cacheline aligned physical address on the remote partition.
 * dst must be a cacheline aligned virtual address on this partition.
 * cnt must be an cacheline sized
 */
static __inline__ xpc_t
xpc_pull_remote_cachelines(xpc_partition_t *part, void *dst, const void *src,
				size_t cnt)
{
	bte_result_t bte_ret;


	XP_ASSERT(L1_CACHE_ALIGNED(src));
	XP_ASSERT(L1_CACHE_ALIGNED(dst));
	XP_ASSERT(L1_CACHE_ALIGNED(cnt));

	if (part->act_state == XPC_P_DEACTIVATING) {
		return part->reason;
	}

	bte_ret = xp_bte_copy((u64) src, (u64) ia64_tpa((__u64) dst),
				(u64) cnt, (BTE_NORMAL | BTE_WACQUIRE), NULL);
	if (bte_ret == BTE_SUCCESS) {
		return xpcSuccess;
	}

	DPRINTK(xpc_chan, (XPC_DBG_C_IPI | XPC_DBG_C_ERROR),
		"xp_bte_copy() from partition %d failed, ret=%d\n",
		XPC_PARTID(part), bte_ret);

	return xpc_map_bte_errors(bte_ret);
}


/*
 * Pull the remote per partititon specific variables from the specified
 * partition.
 */
xpc_t
xpc_pull_remote_vars_part(xpc_partition_t *part)
{
	u8 buffer[L1_CACHE_BYTES * 2];
	xpc_vars_part_t *pulled_entry_cacheline =
			(xpc_vars_part_t *) L1_CACHE_ALIGN((u64) buffer);
	xpc_vars_part_t *pulled_entry;
	u64 remote_entry_cacheline_pa, remote_entry_pa;
	partid_t partid = XPC_PARTID(part);
	xpc_t ret;


	/* pull the cacheline that contains the variables we're interested in */

	XP_ASSERT(L1_CACHE_ALIGNED(part->remote_vars_part_pa));
	XP_ASSERT(sizeof(xpc_vars_part_t) == L1_CACHE_BYTES / 2);

	remote_entry_pa = part->remote_vars_part_pa +
				sn_local_partid() * sizeof(xpc_vars_part_t);

	remote_entry_cacheline_pa = (remote_entry_pa & ~(L1_CACHE_BYTES - 1));

	pulled_entry = (xpc_vars_part_t *) ((u64) pulled_entry_cacheline +
				(remote_entry_pa & (L1_CACHE_BYTES - 1)));
	
	ret = xpc_pull_remote_cachelines(part, pulled_entry_cacheline,
					(void *) remote_entry_cacheline_pa,
					L1_CACHE_BYTES);
	if (ret != xpcSuccess) {
		DPRINTK(xpc_chan, XPC_DBG_C_SETUP,
			"failed to pull XPC vars_part from partition %d, "
			"ret=%d\n", partid, ret);
		return ret;
	}


	/* see if they've been set up yet */

	if (pulled_entry->magic != XPC_VP_MAGIC1 &&
				pulled_entry->magic != XPC_VP_MAGIC2) {

		if (pulled_entry->magic != 0) {
			DPRINTK(xpc_chan, XPC_DBG_C_SETUP,
				"partition %d's XPC vars_part for partition %d"
				" has bad magic value (=0x%lx)\n", partid,
				sn_local_partid(), pulled_entry->magic);
			return xpcBadMagic;
		}

		/* they've not been initialized yet */
		return xpcRetry;
	}

	if (xpc_vars_part[partid].magic == XPC_VP_MAGIC1) {

		/* validate the variables */

		if (pulled_entry->GPs_pa == 0 ||
				pulled_entry->openclose_args_pa == 0 ||
					pulled_entry->IPI_amo_pa == 0) {

			DPRINTK_ALWAYS(xpc_chan, (XPC_DBG_C_SETUP |
							XPC_DBG_C_ERROR),
				KERN_ERR "XPC: partition %d's XPC vars_part "
				"for partition %d are not valid\n", partid,
				sn_local_partid());
			return xpcInvalidAddress;
		}

		/* the variables we imported look to be valid */

		part->remote_GPs_pa = pulled_entry->GPs_pa;
		part->remote_openclose_args_pa =
					pulled_entry->openclose_args_pa;
		part->remote_IPI_amo_va =
				      (AMO_t *) __va(pulled_entry->IPI_amo_pa);
		part->remote_IPI_cpuid = pulled_entry->IPI_cpuid;

		if (part->nchannels > pulled_entry->nchannels) {
			part->nchannels = pulled_entry->nchannels;
		}

		/* let the other side know that we've pulled their variables */

		xpc_vars_part[partid].magic = XPC_VP_MAGIC2;
	}

	if (pulled_entry->magic == XPC_VP_MAGIC1) {
		return xpcRetry;
	}

	return xpcSuccess;
}


/* 
 * Check to see if there is any channel activity to/from the specified
 * partition.
 */
__inline__ void
xpc_check_for_channel_activity(xpc_partition_t *part)
{       
	u64 IPI_amo;
	unsigned long irq_flags;


	IPI_amo = xpc_IPI_receive(part->local_IPI_amo_va);
	if (IPI_amo == 0) {
		return;
	}

	spin_lock_irqsave(&part->IPI_lock, irq_flags);
	part->local_IPI_amo |= IPI_amo;
	spin_unlock_irqrestore(&part->IPI_lock, irq_flags);

	DPRINTK(xpc_chan, XPC_DBG_C_IPI,
		"received IPI from partid=%d, IPI_amo=0x%lx\n",
		XPC_PARTID(part), IPI_amo);

	XPC_PROCESS_CHANNEL_ACTIVITY(part);
}


/*
 * Get the IPI flags and pull the openclose args and/or remote GPs as needed.
 */
static u64
xpc_get_IPI_flags(xpc_partition_t *part)
{
	unsigned long irq_flags;
	u64 IPI_amo;
	xpc_t ret;


	/*
	 * See if there are any IPI flags to be handled.
	 */

	spin_lock_irqsave(&part->IPI_lock, irq_flags);
	if ((IPI_amo = part->local_IPI_amo) != 0) {
		part->local_IPI_amo = 0;
	}
	spin_unlock_irqrestore(&part->IPI_lock, irq_flags);


	if (XPC_ANY_OPENCLOSE_IPI_FLAGS_SET(IPI_amo)) {
		ret = xpc_pull_remote_cachelines(part,
					part->remote_openclose_args,
					(void *) part->remote_openclose_args_pa,
					XPC_OPENCLOSE_ARGS_SIZE);
		if (ret != xpcSuccess) {
			XPC_DEACTIVATE_PARTITION(part, ret);

			DPRINTK(xpc_chan, XPC_DBG_C_IPI,
				"failed to pull openclose args from partition "
				"%d, ret=%d\n", XPC_PARTID(part), ret);

			/* don't bother processing IPIs anymore */
			IPI_amo = 0;
		}
	}

	if (XPC_ANY_MSG_IPI_FLAGS_SET(IPI_amo)) {
		ret = xpc_pull_remote_cachelines(part, part->remote_GPs,
						(void *) part->remote_GPs_pa,
						XPC_GP_SIZE);
		if (ret != xpcSuccess) {
			XPC_DEACTIVATE_PARTITION(part, ret);

			DPRINTK(xpc_chan, XPC_DBG_C_IPI,
				"failed to pull GPs from partition %d, "
				"ret=%d\n", XPC_PARTID(part), ret);

			/* don't bother processing IPIs anymore */
			IPI_amo = 0;
		}
	}

	return IPI_amo;
}


/*
 * Allocate the local message queue and the notify queue.
 */
static xpc_t
xpc_allocate_local_msgqueue(xpc_channel_t *ch)
{
	unsigned long irq_flags;
	int nentries;
	size_t nbytes;


	// >>> may want to check for ch->flags & XPC_C_DISCONNECTING between
	// >>> iterations of the for-loop, bail if set?

	// >>> should we impose a minumum #of entries? like 4 or 8?
	for (nentries = ch->local_nentries; nentries > 0; nentries--) {

		nbytes = nentries * ch->msg_size;
		ch->local_msgqueue = kmalloc(nbytes, GFP_KERNEL | GFP_DMA);
		if (ch->local_msgqueue == NULL) {
			continue;
		}
		XP_ASSERT(L1_CACHE_ALIGNED(ch->local_msgqueue));
		memset(ch->local_msgqueue, 0, nbytes);

		nbytes = nentries * sizeof(xpc_notify_t);
		ch->notify_queue = kmalloc(nbytes, GFP_KERNEL | GFP_DMA);
		if (ch->notify_queue == NULL) {
			kfree(ch->local_msgqueue);
			continue;
		}

		// >>> do these really need to be cache aligned ?
		XP_ASSERT(L1_CACHE_ALIGNED(ch->notify_queue));
		memset(ch->notify_queue, 0, nbytes);

		spin_lock_irqsave(&ch->lock, irq_flags);
		if (nentries < ch->local_nentries) {
			DPRINTK(xpc_chan, XPC_DBG_C_CONNECT,
				"nentries=%d local_nentries=%d, partid=%d, "
				"channel=%d\n", nentries, ch->local_nentries,
				ch->partid, ch->number);

			ch->local_nentries = nentries;
		}
		spin_unlock_irqrestore(&ch->lock, irq_flags);
		return xpcSuccess;
	}

	DPRINTK(xpc_chan, XPC_DBG_C_CONNECT,
		"can't get memory for local message queue and notify queue, "
		"partid=%d, channel=%d\n", ch->partid, ch->number);
	return xpcNoMemory;
}


/*
 * Allocate the cached remote message queue.
 */
static xpc_t
xpc_allocate_remote_msgqueue(xpc_channel_t *ch)
{
	unsigned long irq_flags;
	int nentries;
	size_t nbytes;


	XP_ASSERT(ch->remote_nentries > 0);

	// >>> may want to check for ch->flags & XPC_C_DISCONNECTING between
	// >>> iterations of the for-loop, bail if set?

	// >>> should we impose a minumum #of entries? like 4 or 8?
	for (nentries = ch->remote_nentries; nentries > 0; nentries--) {

		nbytes = nentries * ch->msg_size;
		ch->remote_msgqueue = kmalloc(nbytes, GFP_KERNEL | GFP_DMA);
		if (ch->remote_msgqueue == NULL) {
			continue;
		}

		XP_ASSERT(L1_CACHE_ALIGNED(ch->remote_msgqueue));
		memset(ch->remote_msgqueue, 0, nbytes);

		spin_lock_irqsave(&ch->lock, irq_flags);
		if (nentries < ch->remote_nentries) {
			DPRINTK(xpc_chan, XPC_DBG_C_CONNECT,
				"nentries=%d remote_nentries=%d, partid=%d, "
				"channel=%d\n", nentries, ch->remote_nentries,
				 ch->partid, ch->number);

			ch->remote_nentries = nentries;
		}
		spin_unlock_irqrestore(&ch->lock, irq_flags);
		return xpcSuccess;
	}

	DPRINTK(xpc_chan, XPC_DBG_C_CONNECT,
		"can't get memory for cached remote message queue, partid=%d, "
		"channel=%d\n", ch->partid, ch->number);
	return xpcNoMemory;
}


/*
 * Allocate message queues and other stuff associated with a channel.
 *
 * Note: Assumes all of the channel sizes are filled in.
 */
static xpc_t
xpc_allocate_msgqueues(xpc_channel_t *ch)
{
	unsigned long irq_flags;
	int i;
	xpc_t ret;


	XP_ASSERT(!(ch->flags & XPC_C_SETUP));

	if ((ret = xpc_allocate_local_msgqueue(ch)) != xpcSuccess) {
		return ret;
	}

	if ((ret = xpc_allocate_remote_msgqueue(ch)) != xpcSuccess) {
		kfree(ch->local_msgqueue);
		ch->local_msgqueue = NULL;
		kfree(ch->notify_queue);
		ch->notify_queue = NULL;
		return ret;
	}

	for (i = 0; i < ch->local_nentries; i++) {
		/* use a semaphore as an event wait queue */
		sema_init(&ch->notify_queue[i].sema, 0);
	}

	sema_init(&ch->teardown_sema, 0);	/* event wait */

	spin_lock_irqsave(&ch->lock, irq_flags);
	ch->flags |= XPC_C_SETUP;
	spin_unlock_irqrestore(&ch->lock, irq_flags);

	return xpcSuccess;
}


/*
 * Process a connect message from a remote partition.
 *
 * Note: xpc_process_connect() is expecting to be called with the
 * spin_lock_irqsave held and will leave it locked upon return.
 */
static void
xpc_process_connect(xpc_channel_t *ch, unsigned long *irq_flags)
{
	xpc_t ret;


	XP_ASSERT(spin_is_locked(&ch->lock));

	if (!(ch->flags & XPC_C_OPENREQUEST) ||
				!(ch->flags & XPC_C_ROPENREQUEST)) {
		/* nothing more to do for now */
		return;
	}
	XP_ASSERT(ch->flags & XPC_C_CONNECTING);

	if (!(ch->flags & XPC_C_SETUP)) {
		spin_unlock_irqrestore(&ch->lock, *irq_flags);
		ret = xpc_allocate_msgqueues(ch);
		spin_lock_irqsave(&ch->lock, *irq_flags);

		if (ret != xpcSuccess) {
			XPC_DISCONNECT_CHANNEL(ch, ret, irq_flags);
		}
		if (ch->flags & (XPC_C_CONNECTED | XPC_C_DISCONNECTING)) {
			return;
		}

		XP_ASSERT(ch->flags & XPC_C_SETUP);
		XP_ASSERT(ch->local_msgqueue != NULL);
		XP_ASSERT(ch->remote_msgqueue != NULL);
	}

	if (!(ch->flags & XPC_C_OPENREPLY)) {
		ch->flags |= XPC_C_OPENREPLY;
		XPC_IPI_SEND_OPENREPLY(ch, irq_flags);
	}

	if (!(ch->flags & XPC_C_ROPENREPLY)) {
		return;
	}

	XP_ASSERT(ch->remote_msgqueue_pa != 0);

	ch->flags = (XPC_C_CONNECTED | XPC_C_SETUP);	/* clear all else */

	DPRINTK_ALWAYS(xpc_chan, (XPC_DBG_C_CONNECT | XPC_DBG_C_CONSOLE),
		KERN_INFO "XPC: channel %d to partition %d connected\n",
		ch->number, ch->partid);

	spin_unlock_irqrestore(&ch->lock, *irq_flags);
	XPC_CONNECTED_CALLOUT(ch);
	spin_lock_irqsave(&ch->lock, *irq_flags);
}


/*
 * Free up message queues and other stuff that were allocated for the specified
 * channel.
 *
 * Note: ch->reason and ch->reason_line are left set for debugging purposes,
 * they're cleared when XPC_C_DISCONNECTED is cleared.
 */
static void
xpc_free_msgqueues(xpc_channel_t *ch)
{
	XP_ASSERT(spin_is_locked(&ch->lock));
	XP_ASSERT(atomic_read(&ch->n_to_notify) == 0);

	ch->remote_msgqueue_pa = 0;
	ch->func = NULL;
	ch->key = NULL;
	ch->msg_size = 0;
	ch->local_nentries = 0;
	ch->remote_nentries = 0;
	ch->kthreads_assigned_limit = 0;
	ch->kthreads_idle_limit = 0;

	ch->local_GP->get = 0;
	ch->local_GP->put = 0;
	ch->remote_GP.get = 0;
	ch->remote_GP.put = 0;
	ch->w_local_GP.get = 0;
	ch->w_local_GP.put = 0;
	ch->w_remote_GP.get = 0;
	ch->w_remote_GP.put = 0;
	ch->next_msg_to_pull = 0;

	if (ch->flags & XPC_C_SETUP) {
		ch->flags &= ~XPC_C_SETUP;

		DPRINTK(xpc_chan, XPC_DBG_C_TEARDOWN,
			"ch->flags=0x%x, partid=%d, channel=%d\n", ch->flags,
			ch->partid, ch->number);

		kfree(ch->local_msgqueue);
		ch->local_msgqueue = NULL;
		kfree(ch->remote_msgqueue);
		ch->remote_msgqueue = NULL;
		kfree(ch->notify_queue);
		ch->notify_queue = NULL;

		/* in case someone is waiting for the teardown to complete */
		up(&ch->teardown_sema);
	}
}


/*
 * spin_lock_irqsave() is expected to be held on entry.
 */
static void
xpc_process_disconnect(xpc_channel_t *ch, unsigned long *irq_flags)
{
	xpc_partition_t *part = &xpc_partitions[ch->partid];
	u32 ch_flags = ch->flags;


	XP_ASSERT(spin_is_locked(&ch->lock));

	if (!(ch->flags & XPC_C_DISCONNECTING)) {
		return;
	}

	XP_ASSERT(ch->flags & XPC_C_CLOSEREQUEST);

	/* make sure all activity has settled down first */

	if (atomic_read(&ch->references) > 0) {
		return;
	}
	XP_ASSERT(atomic_read(&ch->kthreads_assigned) == 0);

	/* it's now safe to free the channel's message queues */

	xpc_free_msgqueues(ch);
	XP_ASSERT(!(ch->flags & XPC_C_SETUP));

	if (part->act_state != XPC_P_DEACTIVATING) {

		/* as long as the other side is up do the full protocol */

		if (!(ch->flags & XPC_C_RCLOSEREQUEST)) {
			return;
		}

		if (!(ch->flags & XPC_C_CLOSEREPLY)) {
			ch->flags |= XPC_C_CLOSEREPLY;
			XPC_IPI_SEND_CLOSEREPLY(ch, irq_flags);
		}

		if (!(ch->flags & XPC_C_RCLOSEREPLY)) {
			return;
		}
	}

	/* both sides are disconnected now */

	ch->flags = XPC_C_DISCONNECTED;	/* clear all flags, but this one */

	atomic_dec(&part->nchannels_active);

	if (ch_flags & XPC_C_WASCONNECTED) {
		DPRINTK_ALWAYS(xpc_chan, (XPC_DBG_C_DISCONNECT |
							XPC_DBG_C_CONSOLE),
			KERN_INFO "XPC: channel %d to partition %d "
			"disconnected, reason=%s\n", ch->number, ch->partid,
			xpc_get_ascii_reason_code(ch->reason));
	}

	XPC_DISCONNECTED_CALLOUT(ch, ch_flags, irq_flags);
}


/*
 * Process a change in the channel's remote connection state.
 */
static void
xpc_process_openclose_IPI(xpc_partition_t *part, int ch_number, u8 IPI_flags)
{
	unsigned long irq_flags;
	xpc_openclose_args_t *args = &part->remote_openclose_args[ch_number];
	xpc_channel_t *ch = &part->channels[ch_number];
	xpc_t reason;



	spin_lock_irqsave(&ch->lock, irq_flags);


	if (IPI_flags & XPC_IPI_CLOSEREQUEST) {

		DPRINTK(xpc_chan, XPC_DBG_C_IPI,
			"XPC_IPI_CLOSEREQUEST (reason=%d) received from partid"
			"=%d, channel=%d\n", args->reason,
			ch->partid, ch->number);

		/*
		 * If RCLOSEREQUEST is set, we're probably waiting for
		 * RCLOSEREPLY. We should find it and a ROPENREQUEST packed
		 * with this RCLOSEQREUQEST in the IPI_flags.
		 */

		if (ch->flags & XPC_C_RCLOSEREQUEST) {
			XP_ASSERT(ch->flags & XPC_C_DISCONNECTING);
			XP_ASSERT(ch->flags & XPC_C_CLOSEREQUEST);
			XP_ASSERT(ch->flags & XPC_C_CLOSEREPLY);
			XP_ASSERT(!(ch->flags & XPC_C_RCLOSEREPLY));

			XP_ASSERT(IPI_flags & XPC_IPI_CLOSEREPLY);
			IPI_flags &= ~XPC_IPI_CLOSEREPLY;
			ch->flags |= XPC_C_RCLOSEREPLY;

			/* both sides have finished disconnecting */
			xpc_process_disconnect(ch, &irq_flags);
		}

		if (ch->flags & XPC_C_DISCONNECTED) {
			// >>> explain this section

			if (!(IPI_flags & XPC_IPI_OPENREQUEST)) {
				XP_ASSERT(part->act_state ==
							XPC_P_DEACTIVATING);
				spin_unlock_irqrestore(&ch->lock, irq_flags);
				return;
			}

			XPC_SET_REASON(ch, 0, 0);
			ch->flags &= ~XPC_C_DISCONNECTED;

			atomic_inc(&part->nchannels_active);
			ch->flags |= (XPC_C_CONNECTING | XPC_C_ROPENREQUEST);
		}

		IPI_flags &= ~(XPC_IPI_OPENREQUEST | XPC_IPI_OPENREPLY);

		/*
		 * The meaningful CLOSEREQUEST connection state fields are:
		 *      reason = reason connection is to be closed
		 */

		ch->flags |= XPC_C_RCLOSEREQUEST;

		if (!(ch->flags & XPC_C_DISCONNECTING)) {
			reason = args->reason;
			if (reason <= xpcSuccess || reason > xpcUnknownReason) {
				reason = xpcUnknownReason;
			} else if (reason == xpcUnregistering) {
				reason = xpcOtherUnregistering;
			}

			XPC_DISCONNECT_CHANNEL(ch, reason, &irq_flags);
		} else {
			xpc_process_disconnect(ch, &irq_flags);
		}
	}


	if (IPI_flags & XPC_IPI_CLOSEREPLY) {

		DPRINTK(xpc_chan, XPC_DBG_C_IPI,
			"XPC_IPI_CLOSEREPLY received from partid=%d, channel="
			"%d\n", ch->partid, ch->number);

		if (ch->flags & XPC_C_DISCONNECTED) {
			XP_ASSERT(part->act_state == XPC_P_DEACTIVATING);
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			return;
		}

		XP_ASSERT(ch->flags & XPC_C_CLOSEREQUEST);
		XP_ASSERT(ch->flags & XPC_C_RCLOSEREQUEST);

		ch->flags |= XPC_C_RCLOSEREPLY;

		if (ch->flags & XPC_C_CLOSEREPLY) {
			/* both sides have finished disconnecting */
			xpc_process_disconnect(ch, &irq_flags);
		}
	}


	if (IPI_flags & XPC_IPI_OPENREQUEST) {

		DPRINTK(xpc_chan, XPC_DBG_C_IPI,
			"XPC_IPI_OPENREQUEST (msg_size=%d, local_nentries=%d) "
			"received from partid=%d, channel=%d\n",
			args->msg_size, args->local_nentries,
			ch->partid, ch->number);

		if ((ch->flags & XPC_C_DISCONNECTING) ||
					part->act_state == XPC_P_DEACTIVATING) {
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			return;
		}
		XP_ASSERT(ch->flags & (XPC_C_DISCONNECTED | XPC_C_OPENREQUEST));
		XP_ASSERT(!(ch->flags & (XPC_C_ROPENREQUEST | XPC_C_ROPENREPLY |
					XPC_C_OPENREPLY | XPC_C_CONNECTED)));

		/*
		 * The meaningful OPENREQUEST connection state fields are:
		 *      msg_size = size of channel's messages in bytes
		 *      local_nentries = remote partition's local_nentries
		 */
		XP_ASSERT(args->msg_size != 0);
		XP_ASSERT(args->local_nentries != 0);

		ch->flags |= (XPC_C_ROPENREQUEST | XPC_C_CONNECTING);
		ch->remote_nentries = args->local_nentries;


		if (ch->flags & XPC_C_OPENREQUEST) {
			if (args->msg_size != ch->msg_size) {
				XPC_DISCONNECT_CHANNEL(ch, xpcUnequalMsgSizes,
								&irq_flags);
				spin_unlock_irqrestore(&ch->lock, irq_flags);
				return;
			}
		} else {
			ch->msg_size = args->msg_size;

			XPC_SET_REASON(ch, 0, 0);
			ch->flags &= ~XPC_C_DISCONNECTED;

			atomic_inc(&part->nchannels_active);
		}

		xpc_process_connect(ch, &irq_flags);
	}


	if (IPI_flags & XPC_IPI_OPENREPLY) {

		DPRINTK(xpc_chan, XPC_DBG_C_IPI,
			"XPC_IPI_OPENREPLY (local_msgqueue_pa=0x%lx, "
			"local_nentries=%d, remote_nentries=%d) received from "
			"partid=%d, channel=%d\n", args->local_msgqueue_pa,
			args->local_nentries, args->remote_nentries,
			ch->partid, ch->number);

		if (ch->flags & (XPC_C_DISCONNECTING | XPC_C_DISCONNECTED)) {
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			return;
		}
		XP_ASSERT(ch->flags & XPC_C_OPENREQUEST);
		XP_ASSERT(ch->flags & XPC_C_ROPENREQUEST);
		XP_ASSERT(!(ch->flags & XPC_C_CONNECTED));

		/*
		 * The meaningful OPENREPLY connection state fields are:
		 *      local_msgqueue_pa = physical address of remote
		 *			    partition's local_msgqueue
		 *      local_nentries = remote partition's local_nentries
		 *      remote_nentries = remote partition's remote_nentries
		 */
		XP_ASSERT(args->local_msgqueue_pa != 0);
		XP_ASSERT(args->local_nentries != 0);
		XP_ASSERT(args->remote_nentries != 0);

		ch->flags |= XPC_C_ROPENREPLY;
		ch->remote_msgqueue_pa = args->local_msgqueue_pa;

		if (args->local_nentries < ch->remote_nentries) {
			DPRINTK(xpc_chan, XPC_DBG_C_IPI,
				"XPC_IPI_OPENREPLY: new remote_nentries=%d, old"
				" remote_nentries=%d, partid=%d, channel=%d\n",
				args->local_nentries, ch->remote_nentries,
				ch->partid, ch->number);

			ch->remote_nentries = args->local_nentries;
		}
		if (args->remote_nentries < ch->local_nentries) {
			DPRINTK(xpc_chan, XPC_DBG_C_IPI,
				"XPC_IPI_OPENREPLY: new local_nentries=%d, old "
				"local_nentries=%d, partid=%d, channel=%d\n",
				args->remote_nentries, ch->local_nentries,
				ch->partid, ch->number);

			ch->local_nentries = args->remote_nentries;
		}

		xpc_process_connect(ch, &irq_flags);
	}

	spin_unlock_irqrestore(&ch->lock, irq_flags);
}


/*
 * Attempt to establish a channel connection to a remote partition.
 */
static xpc_t
xpc_connect_channel(xpc_channel_t *ch)
{
	unsigned long irq_flags;
	xpc_registration_t *registration = &xpc_registrations[ch->number];


	if (down_interruptible(&registration->sema) != 0) {
		return xpcInterrupted;
	}

	if (!XPC_CHANNEL_REGISTERED(ch->number)) {
		up(&registration->sema);
		return xpcUnregistered;
	}

	spin_lock_irqsave(&ch->lock, irq_flags);

	XP_ASSERT(!(ch->flags & XPC_C_CONNECTED));
	XP_ASSERT(!(ch->flags & XPC_C_OPENREQUEST));

	if (ch->flags & XPC_C_DISCONNECTING) {
		spin_unlock_irqrestore(&ch->lock, irq_flags);
		up(&registration->sema);
		return ch->reason;
	}


	/* add info from the channel connect registration to the channel */

	ch->kthreads_assigned_limit = registration->assigned_limit;
	ch->kthreads_idle_limit = registration->idle_limit;
	XP_ASSERT(atomic_read(&ch->kthreads_assigned) == 0);
	XP_ASSERT(atomic_read(&ch->kthreads_idle) == 0);
	XP_ASSERT(atomic_read(&ch->kthreads_active) == 0);

	ch->func = registration->func;
	XP_ASSERT(registration->func != NULL);
	ch->key = registration->key;

	ch->local_nentries = registration->nentries;

	if (ch->flags & XPC_C_ROPENREQUEST) {
		if (registration->msg_size != ch->msg_size) { 
			/* the local and remote sides aren't the same */

			/*
			 * Because XPC_DISCONNECT_CHANNEL() can block we're
			 * forced to up the registration sema before we unlock
			 * the channel lock. But that's okay here because we're
			 * done with the part that required the registration
			 * sema. XPC_DISCONNECT_CHANNEL() requires that the
			 * channel lock be locked and will unlock and relock
			 * the channel lock as needed.
			 */
			up(&registration->sema);
			XPC_DISCONNECT_CHANNEL(ch, xpcUnequalMsgSizes,
								&irq_flags);
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			return xpcUnequalMsgSizes;
		}
	} else {
		ch->msg_size = registration->msg_size;

		XPC_SET_REASON(ch, 0, 0);
		ch->flags &= ~XPC_C_DISCONNECTED;

		atomic_inc(&xpc_partitions[ch->partid].nchannels_active);
	}

	up(&registration->sema);


	/* initiate the connection */

	ch->flags |= (XPC_C_OPENREQUEST | XPC_C_CONNECTING);
	XPC_IPI_SEND_OPENREQUEST(ch, &irq_flags);

	xpc_process_connect(ch, &irq_flags);

	spin_unlock_irqrestore(&ch->lock, irq_flags);

	return xpcSuccess;
}


#define XPC_DBG_C_NOTIFY ((reason == xpcMsgDelivered) ? \
					XPC_DBG_C_IPI : XPC_DBG_C_DISCONNECT)


/*
 * Notify those who wanted to be notified upon delivery of their message.
 */
static void
xpc_notify_senders(xpc_channel_t *ch, xpc_t reason, s64 put)
{
	xpc_notify_t *notify;
	u8 notify_type;
	s64 get = ch->w_remote_GP.get - 1;


	while (++get < put && atomic_read(&ch->n_to_notify) > 0) {

		notify = &ch->notify_queue[get % ch->local_nentries];

		/*
		 * See if the notify entry indicates it was associated with
		 * a message who's sender wants to be notified. It is possible
		 * that it is, but someone else is doing or has done the
		 * notification.
		 */
		notify_type = notify->type;
		if (notify_type == 0 ||
				cmpxchg(&notify->type, notify_type, 0) !=
								notify_type) {
			continue;
		}

		XP_ASSERT(notify_type == XPC_N_CALL);

		atomic_dec(&ch->n_to_notify);

		DPRINTK(xpc_chan, XPC_DBG_C_NOTIFY,
			"XPC_CALL_NOTIFY_FUNC() called, notify=0x%p, "
			"msg_number=%ld, partid=%d, channel=%d\n",
			(void *) notify, get, ch->partid, ch->number);

		XPC_CALL_NOTIFY_FUNC(ch, notify, reason);

		DPRINTK(xpc_chan, XPC_DBG_C_NOTIFY,
			"XPC_CALL_NOTIFY_FUNC() returned, notify=0x%p, "
			"msg_number=%ld, partid=%d, channel=%d\n",
			(void *) notify, get, ch->partid, ch->number);
	}
}


#undef XPC_DBG_C_NOTIFY


/*
 *
 */
static void
xpc_process_msg_IPI(xpc_partition_t *part, int ch_number)
{
	xpc_channel_t *ch = &part->channels[ch_number];
	int msgs_sent;


	ch->remote_GP = part->remote_GPs[ch_number];


	/* See what, if anything, has changed for each connected channel */

	XPC_MSGQUEUE_REF(ch);

	if (ch->w_remote_GP.get == ch->remote_GP.get &&
				ch->w_remote_GP.put == ch->remote_GP.put) {
		/* nothing changed since GPs were last pulled */
		XPC_MSGQUEUE_DEREF(ch);
		return;
	}

	if (!(ch->flags & XPC_C_CONNECTED)){
		XPC_MSGQUEUE_DEREF(ch);
		return;
	}


	/*
	 * First check to see if messages recently sent by us have been
	 * received by the other side. (The remote GET value will have
	 * changed since we last looked at it.)
	 */

	if (ch->w_remote_GP.get != ch->remote_GP.get) {

		/*
		 * We need to notify any senders that want to be notified
		 * that their sent messages have been received by their
		 * intended recipients. We need to do this before updating
		 * w_remote_GP.get so that we don't allocate the same message
		 * queue entries prematurely (see xpc_allocate_msg()).
		 */
		if (atomic_read(&ch->n_to_notify) > 0) {
			/*
			 * Notify senders that messages sent have been
			 * received and delivered by the other side.
			 */
			xpc_notify_senders(ch, xpcMsgDelivered,
							ch->remote_GP.get);
		}

		/*
		 * Clear msg->flags in previously sent messages, so that
		 * they're ready for xpc_allocate_msg().
		 */
		XPC_CLEAR_LOCAL_MSGQUEUE_FLAGS(ch);

		ch->w_remote_GP.get = ch->remote_GP.get;

		DPRINTK(xpc_chan, (XPC_DBG_C_IPI | XPC_DBG_C_GP),
			"w_remote_GP.get changed to %ld, partid=%d, "
			"channel=%d\n", ch->w_remote_GP.get,
			ch->partid, ch->number);

		/*
		 * If anyone was waiting for message queue entries to become
		 * available, wake them up.
		 */
		if (atomic_read(&ch->n_on_msg_allocate_wq) > 0) {
			wake_up(&ch->msg_allocate_wq);
		}
	}


	/*
	 * Now check for newly sent messages by the other side. (The remote
	 * PUT value will have changed since we last looked at it.)
	 */

	if (ch->w_remote_GP.put != ch->remote_GP.put) {
		/*
		 * Clear msg->flags in previously received messages, so that
		 * they're ready for xpc_get_deliverable_msg().
		 */
		XPC_CLEAR_REMOTE_MSGQUEUE_FLAGS(ch);

		ch->w_remote_GP.put = ch->remote_GP.put;

		DPRINTK(xpc_chan, (XPC_DBG_C_IPI | XPC_DBG_C_GP),
			"w_remote_GP.put changed to %ld, partid=%d, "
			"channel=%d\n", ch->w_remote_GP.put,
			ch->partid, ch->number);

		msgs_sent = ch->w_remote_GP.put - ch->w_local_GP.get;
		if (msgs_sent > 0) {
			DPRINTK(xpc_chan, XPC_DBG_C_IPI,
				"msgs waiting to be copied and delivered=%d, "
				"partid=%d, channel=%d\n", msgs_sent,
				ch->partid, ch->number);

			XPC_INITIATE_MSG_DELIVERY(ch, msgs_sent);
		}
	}

	XPC_MSGQUEUE_DEREF(ch);
}


void
xpc_process_channel_activity(xpc_partition_t *part)
{
	unsigned long irq_flags;
	u64 IPI_amo, IPI_flags;
	xpc_channel_t *ch;
	int ch_number;


	IPI_amo = xpc_get_IPI_flags(part);

	/*
	 * Initiate channel connections for registered channels.
	 *
	 * For each connected channel that has pending messages activate idle
	 * kthreads and/or create new kthreads as needed.
	 */

	for (ch_number = 0; ch_number < part->nchannels; ch_number++) {
		ch = &part->channels[ch_number];


		/*
		 * Process any open or close related IPI flags, and then deal
		 * with connecting or disconnecting the channel as required.
		 */

		IPI_flags = XPC_GET_IPI_FLAGS(IPI_amo, ch_number);

		if (XPC_ANY_OPENCLOSE_IPI_FLAGS_SET(IPI_flags)) {
			xpc_process_openclose_IPI(part, ch_number, IPI_flags);
		}


		if (ch->flags & XPC_C_DISCONNECTING) {
			spin_lock_irqsave(&ch->lock, irq_flags);
			xpc_process_disconnect(ch, &irq_flags);
			spin_unlock_irqrestore(&ch->lock, irq_flags);
			continue;
		}

		if (part->act_state == XPC_P_DEACTIVATING) {
			continue;
		}

		if (!(ch->flags & XPC_C_CONNECTED)) {
			if (!(ch->flags & XPC_C_OPENREQUEST)) {
				XP_ASSERT(!(ch->flags & XPC_C_SETUP));
				(void) xpc_connect_channel(ch);
			} else {
				spin_lock_irqsave(&ch->lock, irq_flags);
				xpc_process_connect(ch, &irq_flags);
				spin_unlock_irqrestore(&ch->lock, irq_flags);
			}
			continue;
		}


		/*
		 * Process any message related IPI flags, this may involve the
		 * activation of kthreads to deliver any pending messages sent
		 * from the other partition.
		 */

		if (XPC_ANY_MSG_IPI_FLAGS_SET(IPI_flags)) {
			xpc_process_msg_IPI(part, ch_number);
		}
	}
}


/*
 * XPC's heartbeat code calls this function to inform XPC that a partition has
 * gone down.  XPC responds by tearing down the XPartition Communication
 * infrastructure used for the just downed partition.
 *
 * XPC's heartbeat code will never call this function and xpc_partition_up()
 * at the same time. Nor will it ever make multiple calls to either function
 * at the same time.
 */
void
xpc_partition_down(xpc_partition_t *part, xpc_t reason)
{
	unsigned long irq_flags;
	int ch_number;
	xpc_channel_t *ch;


	DPRINTK(xpc_chan, XPC_DBG_C_TEARDOWN,
		"deactivating partition %d, reason=%d\n", XPC_PARTID(part),
		reason);

	if (!XPC_PART_REF(part)) {
		/* infrastructure for this partition isn't currently set up */
		return;
	}


	/* disconnect all channels associated with the downed partition */

	for (ch_number = 0; ch_number < part->nchannels; ch_number++) {
		ch = &part->channels[ch_number];


		XPC_MSGQUEUE_REF(ch);
		spin_lock_irqsave(&ch->lock, irq_flags);

		XPC_DISCONNECT_CHANNEL(ch, reason, &irq_flags);

		spin_unlock_irqrestore(&ch->lock, irq_flags);
		XPC_MSGQUEUE_DEREF(ch);
	}

	XPC_PROCESS_PARTITION_DOWN(part);

	XPC_PART_DEREF(part);
}


/*
 * Teardown the infrastructure necessary to support XPartition Communication
 * between the specified remote partition and the local one.
 */
void
xpc_teardown_infrastructure(xpc_partition_t *part)
{
	partid_t partid = XPC_PARTID(part);


	/*
	 * We start off by making this partition inaccessible to local
	 * processes by marking it as no longer setup. Then we make it
	 * inaccessible to remote processes by clearing the XPC per partition
	 * specific variable's magic # (which indicates that these variables
	 * are no longer valid) and by ignoring all XPC notify IPIs sent to
	 * this partition.
	 */

	XP_ASSERT(atomic_read(&part->nchannels_active) == 0);
	XP_ASSERT(part->setup_state == XPC_P_SETUP);
	part->setup_state = XPC_P_WTEARDOWN;

	xpc_vars_part[partid].magic = 0;


	XPC_FREE_IRQ(SGI_XPC_NOTIFY, (void *) (u64) partid);


	/*
	 * Before proceding with the teardown we have to wait until all
	 * existing references cease.
	 */
	wait_event(part->teardown_wq, (atomic_read(&part->references) == 0));


	/* now we can begin tearing down the infrastructure */

	part->setup_state = XPC_P_TORNDOWN;

	/* in case we've still got outstanding timers registered... */
	XPC_DEL_TIMER(&part->dropped_IPI_timer);

	kfree(part->remote_openclose_args);
	part->remote_openclose_args = NULL;
	kfree(part->local_openclose_args);
	part->local_openclose_args = NULL;
	kfree(part->remote_GPs);
	part->remote_GPs = NULL;
	kfree(part->local_GPs);
	part->local_GPs = NULL;
	kfree(part->channels);
	part->channels = NULL;
	part->local_IPI_amo_va = NULL;
}


/*
 * Called by XP at the time of channel connection registration to cause
 * XPC to establish connections to all currently active partitions.
 */
void
xpc_initiate_connect(int ch_number)
{
	partid_t partid;
	xpc_partition_t *part;
	xpc_channel_t *ch;


	XP_ASSERT(ch_number >= 0 && ch_number < XPC_NCHANNELS);
	
	for (partid = 1; partid < MAX_PARTITIONS; partid++) {
		part = &xpc_partitions[partid];

		if (XPC_PART_REF(part)) {
			ch = &part->channels[ch_number];

			if (!(ch->flags & XPC_C_DISCONNECTING)) {
				XP_ASSERT(!(ch->flags & XPC_C_OPENREQUEST));
				XP_ASSERT(!(ch->flags & XPC_C_CONNECTED));
				XP_ASSERT(!(ch->flags & XPC_C_SETUP));

				/*
				 * Initiate the establishment of a connection
				 * on the newly registered channel to the
				 * remote partition.
				 */
				XPC_PROCESS_CHANNEL_ACTIVITY(part);
			}

			XPC_PART_DEREF(part);
		}
	}
}


void
xpc_connected_callout(xpc_channel_t *ch)
{
	unsigned long irq_flags;


	/* let the registerer know that a connection has been established */

	DPRINTK(xpc_chan, XPC_DBG_C_CONNECT,
		"XPC_CALL_CHANNEL_FUNC() called, reason=xpcConnected, "
		"partid=%d, channel=%d\n", ch->partid, ch->number);

	XPC_CALL_CHANNEL_FUNC(ch, xpcConnected,
					(void *) (u64) ch->local_nentries);

	DPRINTK(xpc_chan, XPC_DBG_C_CONNECT,
		"XPC_CALL_CHANNEL_FUNC() returned, reason=xpcConnected, "
		"partid=%d, channel=%d\n", ch->partid, ch->number);

	spin_lock_irqsave(&ch->lock, irq_flags);
	ch->flags |= XPC_C_CONNECTCALLOUT;
	spin_unlock_irqrestore(&ch->lock, irq_flags);
}


/*
 * Called by XP at the time of channel connection unregistration to cause
 * XPC to teardown all current connections for the specified channel.
 *
 * Before returning xpc_initiate_disconnect() will wait until all connections
 * on the specified channel have been closed/torndown. So the caller can be
 * assured that they will not be receiving any more callouts from XPC to the
 * function they registered via xpc_connect().
 *
 * Arguments:
 *
 *	ch_number - channel # to unregister.
 */
void
xpc_initiate_disconnect(int ch_number)
{
	unsigned long irq_flags;
	partid_t partid;
	xpc_partition_t *part;
	xpc_channel_t *ch;


	XP_ASSERT(ch_number >= 0 && ch_number < XPC_NCHANNELS);

	/* initiate the channel disconnect for every active partition */
	for (partid = 1; partid < MAX_PARTITIONS; partid++) {
		part = &xpc_partitions[partid];

		if (XPC_PART_REF(part)) {
			ch = &part->channels[ch_number];
			XPC_MSGQUEUE_REF(ch);

			spin_lock_irqsave(&ch->lock, irq_flags);

			XPC_DISCONNECT_CHANNEL(ch, xpcUnregistering,
								&irq_flags);

			spin_unlock_irqrestore(&ch->lock, irq_flags);

			XPC_MSGQUEUE_DEREF(ch);
			XPC_PART_DEREF(part);
		}
	}

	XPC_DISCONNECT_WAIT(ch_number);
}


/*
 * To disconnect a channel, and reflect it back to all who may be waiting.
 *
 * >>> An OPEN is not allowed until XPC_C_DISCONNECTING is cleared by
 * >>> xpc_free_msgqueues().
 *
 * THE CHANNEL IS TO BE LOCKED BY THE CALLER AND WILL REMAIN LOCKED UPON RETURN.
 */
void
xpc_disconnect_channel(const int line, xpc_channel_t *ch, xpc_t reason,
			unsigned long *irq_flags)
{
	u32 flags;


	XP_ASSERT(spin_is_locked(&ch->lock));

	if (ch->flags & (XPC_C_DISCONNECTING | XPC_C_DISCONNECTED)) {
		return;
	}
	XP_ASSERT(ch->flags & (XPC_C_CONNECTING | XPC_C_CONNECTED));

	DPRINTK(xpc_chan, XPC_DBG_C_DISCONNECT,
		"reason=%d, line=%d, partid=%d, channel=%d\n", reason, line,
		ch->partid, ch->number);

	XPC_SET_REASON(ch, reason, line);

	flags = ch->flags;
	/* some of these may not have been set */
	ch->flags &= ~(XPC_C_OPENREQUEST | XPC_C_OPENREPLY |
			XPC_C_ROPENREQUEST | XPC_C_ROPENREPLY |
			XPC_C_CONNECTING | XPC_C_CONNECTED);

	ch->flags |= (XPC_C_CLOSEREQUEST | XPC_C_DISCONNECTING);
	XPC_IPI_SEND_CLOSEREQUEST(ch, irq_flags);

	if (flags & XPC_C_CONNECTED) {
		ch->flags |= XPC_C_WASCONNECTED;
	}

	if (atomic_read(&ch->kthreads_idle) > 0) {
		/* wake all idle kthreads so they can exit */
		wake_up_all(&ch->idle_wq);
	}

	spin_unlock_irqrestore(&ch->lock, *irq_flags);


	/* wake those waiting to allocate an entry from the local msg queue */

	if (atomic_read(&ch->n_on_msg_allocate_wq) > 0) {
		wake_up(&ch->msg_allocate_wq);
	}

	/* wake those waiting for notify completion */

	if (atomic_read(&ch->n_to_notify) > 0) {
		xpc_notify_senders(ch, reason, ch->w_local_GP.put);
	}

	spin_lock_irqsave(&ch->lock, *irq_flags);
}


void
xpc_disconnected_callout(xpc_channel_t *ch)
{
	/*
	 * Let the channel's registerer know that the channel is now
	 * disconnected. We don't want to do this if the registerer was never
	 * informed of a connection being made, unless the disconnect was for
	 * abnormal reasons.
	 */

	DPRINTK(xpc_chan, XPC_DBG_C_DISCONNECT,
		"XPC_CALL_CHANNEL_FUNC() called, reason=%d, partid=%d, "
		"channel=%d\n", ch->reason, ch->partid, ch->number);

	XPC_CALL_CHANNEL_FUNC(ch, ch->reason, NULL);

	DPRINTK(xpc_chan, XPC_DBG_C_DISCONNECT,
		"XPC_CALL_CHANNEL_FUNC() returned, reason=%d, partid=%d, "
		"channel=%d\n", ch->reason, ch->partid, ch->number);
}


/*
 * Wait for a message entry to become available for the specified channel,
 * but don't wait any longer than 1 jiffy.
 */
static xpc_t
xpc_allocate_msg_wait(xpc_channel_t *ch)
{
	xpc_t ret;


	if (ch->flags & XPC_C_DISCONNECTING) {
		XP_ASSERT(ch->reason != xpcInterrupted);  // >>> Is this true?
		return ch->reason;
	}

	// >>> It may be more cost effective if the normal kthreads simply call
	// >>> interruptible_sleep_on() and then a separate timer function is
	// >>> registered to wakeup at some interval and see if there are any
	// >>> available. If yes, then it wakes up the sleepers; if no, it
	// >>> fakes an IPI to ourselves and resets itself to wakeup later.
	// >>> xpc_IPI_handler() would also be able to wake sleepers up if it
	// >>> sees that there are available messages.

	// >>> In IRIX, XPC_ACQ_TO was set to 40 microseconds, which
	// >>> would mean that the first timeout was 80 microseconds.
	// >>> Dimitri was seeing differences in performance between
	// >>> 20, 40 and 80 microsecond values for the timeout.
	// >>> Linux timers are in jiffies which are in milliseconds,
	// >>> which is of course a much coarser ganularity.
	// >>> For now we're hardcoding the timeout to 1 jiffy.

	atomic_inc(&ch->n_on_msg_allocate_wq);
	ret = interruptible_sleep_on_timeout(&ch->msg_allocate_wq, 1);
	atomic_dec(&ch->n_on_msg_allocate_wq);

	if (ch->flags & XPC_C_DISCONNECTING) {
		ret = ch->reason;
		XP_ASSERT(ch->reason != xpcInterrupted);  // >>> Is this true?
	} else if (ret == 0) {
		ret = xpcTimeout;
	} else {
		ret = xpcInterrupted;
	}

	return ret;
}


/*
 * Allocate an entry for a message from the message queue associated with the
 * specified channel.
 */
static xpc_t
xpc_allocate_msg(xpc_channel_t *ch, u32 flags, xpc_msg_t **address_of_msg)
{
	xpc_msg_t *msg;
	xpc_t ret;
	s64 put;


	/* this reference will be dropped in xpc_initiate_send() */
	XPC_MSGQUEUE_REF(ch);

	if (ch->flags & XPC_C_DISCONNECTING) {
		XPC_MSGQUEUE_DEREF(ch);
		return ch->reason;
	}
	if (!(ch->flags & XPC_C_CONNECTED)) {
		XPC_MSGQUEUE_DEREF(ch);
		return xpcNotConnected;
	}


	/*
	 * Get the next available message entry from the local message queue.
	 * If none are available, we'll make sure that we grab the latest
	 * GP values.
	 */
	ret = xpcTimeout;

	while (1) {

		put = ch->w_local_GP.put;
		if (put - ch->w_remote_GP.get < ch->local_nentries) {

			/* There are available message entries. We need to try
			 * to secure one for ourselves. We'll do this by trying
			 * to increment w_local_GP.put as long as someone else
			 * doesn't beat us to it. If they do, we'll have to
			 * try again.
		 	 */
			if (cmpxchg(&ch->w_local_GP.put, put, put + 1) ==
									put) {
				/* we got the entry referenced by put */
				break;
			}
			continue;	/* try again */
		}


		/*
		 * There aren't any available msg entries at this time.
		 *
		 * In waiting for a message entry to become available,
		 * we set a timeout in case the other side is not
		 * sending completion IPIs. This lets us fake an IPI
		 * that will cause the IPI handler to fetch the latest
		 * GP values as if an IPI was sent by the other side.
		 */
		if (ret == xpcTimeout) {
			XPC_IPI_SEND_LOCAL_MSGREQUEST(ch);
		}

		// >>> Note that Dimitri said that he saw a noticeable
		// >>> speedup because of the forced fetch of the GPs,
		// >>> in my new scheme we do eliminate the speediness
		// >>> of the forced approach as in now we have to wait
		// >>> for a kthread to be scheduled, whereas before we
		// >>> were the thread that did that fetch. We could
		// >>> explore creating a kthread (maybe the one that
		// >>> creates new kthreads when needed) that does a
		// >>> force fetch of the GPs at some time interval
		// >>> whenever we have messages sent that we haven't
		// >>> yet accounted for (the other side hasn't moved
		// >>> the get value). Anyway something to think about.

		if (flags & XPC_NOWAIT) {
			XPC_MSGQUEUE_DEREF(ch);
			return xpcNoWait;
		}

		ret = xpc_allocate_msg_wait(ch);
		if (ret != xpcInterrupted && ret != xpcTimeout) {
			XPC_MSGQUEUE_DEREF(ch);
			return ret;
		}
	}


	/* get the message's address and initialize it */
	msg = (xpc_msg_t *) ((u64) ch->local_msgqueue +
				(put % ch->local_nentries) * ch->msg_size);


	XP_ASSERT(msg->flags == 0);
	msg->number = put;

	DPRINTK(xpc_chan, (XPC_DBG_C_SEND | XPC_DBG_C_GP),
		"w_local_GP.put changed to %ld; msg=0x%p, msg_number=%ld, "
		"partid=%d, channel=%d\n", put + 1, (void *) msg, msg->number,
		ch->partid, ch->number);

	*address_of_msg = msg;

	return xpcSuccess;
}


/*
 * Allocate an entry for a message from the message queue associated with the
 * specified channel. NOTE that this routine can sleep waiting for a message
 * entry to become available. To not sleep, pass in the XPC_NOWAIT flag.
 *
 * Arguments:
 *
 *	partid - ID of partition to which the channel is connected.
 *	ch_number - channel #.
 *	flags - see xpc.h for valid flags.
 *	payload - address of the allocated payload area pointer (filled in on
 * 	          return) in which the user-defined message is constructed.
 */
xpc_t
xpc_allocate(partid_t partid, int ch_number, u32 flags, void **payload)
{
	xpc_partition_t *part = &xpc_partitions[partid];
	xpc_t ret = xpcUnknownReason;
	xpc_msg_t *msg;


	XP_ASSERT(partid > 0 && partid < MAX_PARTITIONS);
	XP_ASSERT(ch_number >= 0 && ch_number < part->nchannels);

	*payload = NULL;

	if (XPC_PART_REF(part)) {
		ret = xpc_allocate_msg(&part->channels[ch_number], flags, &msg);
		XPC_PART_DEREF(part);

		if (msg != NULL) {
			*payload = &msg->payload;
		}
	}

	return ret;
}


/*
 * Now we actually send the messages that are ready to be sent by advancing
 * the local message queue's Put value and then send an IPI to the recipient
 * partition.
 */
static void
xpc_send_msgs(xpc_channel_t *ch, s64 initial_put)
{
	xpc_msg_t *msg;
	s64 put = initial_put + 1;
	int send_IPI = 0;


	while (1) {

		while (1) {
			if (put == ch->w_local_GP.put) {
				break;
			}

			msg = (xpc_msg_t *) ((u64) ch->local_msgqueue +
			       (put % ch->local_nentries) * ch->msg_size);

			if (!(msg->flags & XPC_M_READY)) {
				break;
			}

			put++;
		}

		if (put == initial_put) {
			/* nothing's changed */
			break;
		}

		if (cmpxchg_rel(&ch->local_GP->put, initial_put, put) !=
								initial_put) {
			/* someone else beat us to it */
			XP_ASSERT(ch->local_GP->put > initial_put);
			break;
		}

		/* we just set the new value of local_GP->put */

		DPRINTK(xpc_chan, (XPC_DBG_C_SEND | XPC_DBG_C_GP),
			"local_GP->put changed to %ld, partid=%d, channel=%d\n",
			put, ch->partid, ch->number);

		send_IPI = 1;

		/*
		 * We need to ensure that the message referenced by
		 * local_GP->put is not XPC_M_READY or that local_GP->put
		 * equals w_local_GP.put, so we'll go have a look.
		 */
		initial_put = put;
	}

	if (send_IPI) {
		XPC_IPI_SEND_MSGREQUEST(ch);
	}
}


/*
 * Common code that does the actual sending of the message by advancing the
 * local message queue's Put value and sends an IPI to the partition the
 * message is being sent to.
 */
static xpc_t
xpc_initiate_send(xpc_channel_t *ch, xpc_msg_t *msg, u8 notify_type,
			xpc_notify_func_t func, void *key)
{
	xpc_t ret = xpcSuccess;
	xpc_notify_t *notify = NULL;	// >>> to keep the compiler happy!!!!!
	s64 put, msg_number = msg->number;


	XP_ASSERT(notify_type != XPC_N_CALL || func != NULL);
	XP_ASSERT((((u64) msg - (u64) ch->local_msgqueue) / ch->msg_size) ==
					msg_number % ch->local_nentries);
	XP_ASSERT(!(msg->flags & XPC_M_READY));

	if (ch->flags & XPC_C_DISCONNECTING) {
		/* drop the reference grabbed in xpc_allocate_msg() */
		XPC_MSGQUEUE_DEREF(ch);
		return ch->reason;
	}

	if (notify_type != 0) {
		/*
		 * Tell the remote side to send an ACK interrupt when the
		 * message has been delivered.
		 */
		msg->flags |= XPC_M_INTERRUPT;

		atomic_inc(&ch->n_to_notify);

		notify = &ch->notify_queue[msg_number % ch->local_nentries];
		notify->func = func;
		notify->key = key;
		notify->type = notify_type;

		// >>> is a mb() needed here?

		if (ch->flags & XPC_C_DISCONNECTING) {
			/*
			 * An error occurred between our last error check and
			 * this one. We will try to clear the type field from
			 * the notify entry. If we succeed then
			 * xpc_disconnect_channel() didn't already process
			 * the notify entry.
			 */
			if (cmpxchg(&notify->type, notify_type, 0) ==
								notify_type) {
				atomic_dec(&ch->n_to_notify);
				ret = ch->reason;
			}

			/* drop the reference grabbed in xpc_allocate_msg() */
			XPC_MSGQUEUE_DEREF(ch);
			return ret;
		}
	}

	msg->flags |= XPC_M_READY;

	/*
	 * The preceding store of msg->flags must occur before the following
	 * load of ch->local_GP->put.
	 */
	mb();

	/* see if the message is next in line to be sent, if so send it */

	put = ch->local_GP->put;
	if (put == msg_number) {
		xpc_send_msgs(ch, put);
	}

	/* drop the reference grabbed in xpc_allocate_msg() */
	XPC_MSGQUEUE_DEREF(ch);
	return ret;
}


/*
 * Send a message previously allocated using xpc_allocate() on the specified
 * channel connected to the specified partition.
 *
 * This routine will not wait for the message to be received, nor will
 * notification be given when it does happen. Once this routine has returned
 * the message entry allocated via xpc_allocate() is no longer accessable to
 * the caller.
 *
 * This routine, although called by users, does not call XPC_PART_REF() to
 * ensure that the partition infrastructure is in place. It relies on the
 * fact that we called XPC_MSGQUEUE_REF() in xpc_allocate_msg().
 *
 * Arguments:
 *
 *	partid - ID of partition to which the channel is connected.
 *	ch_number - channel # to send message on.
 *	payload - pointer to the payload area allocated via xpc_allocate().
 */
xpc_t
xpc_send(partid_t partid, int ch_number, void *payload)
{
	xpc_partition_t *part = &xpc_partitions[partid];
	xpc_msg_t *msg = XPC_MSG_ADDRESS(payload);
	xpc_t ret;


	DPRINTK(xpc_chan, XPC_DBG_C_SEND,
		"msg=0x%p, partid=%d, channel=%d\n", (void *) msg, partid,
		ch_number);

	XP_ASSERT(partid > 0 && partid < MAX_PARTITIONS);
	XP_ASSERT(ch_number >= 0 && ch_number < part->nchannels);
	XP_ASSERT(msg != NULL);

	ret = xpc_initiate_send(&part->channels[ch_number], msg, 0, NULL, NULL);

	return ret;
}


/*
 * Send a message previously allocated using xpc_allocate on the specified
 * channel connected to the specified partition.
 *
 * This routine will not wait for the message to be sent. Once this routine
 * has returned the message entry allocated via xpc_allocate() is no longer
 * accessable to the caller.
 *
 * Once the remote end of the channel has received the message, the function
 * passed as an argument to xpc_send_notify() will be called. This allows the
 * sender to free up or re-use any buffers referenced by the message, but does
 * NOT mean the message has been processed at the remote end by a receiver.
 *
 * If this routine returns an error, the caller's function will NOT be called.
 *
 * This routine, although called by users, does not call XPC_PART_REF() to
 * ensure that the partition infrastructure is in place. It relies on the
 * fact that we called XPC_MSGQUEUE_REF() in xpc_allocate_msg().
 *
 * Arguments:
 *
 *	partid - ID of partition to which the channel is connected.
 *	ch_number - channel # to send message on.
 *	payload - pointer to the payload area allocated via xpc_allocate().
 *	func - function to call with asynchronous notification of message
 *		  receipt. THIS FUNCTION MUST BE NON-BLOCKING.
 *	key - user-defined key to be passed to the function when it's called.
 */
xpc_t
xpc_send_notify(partid_t partid, int ch_number, void *payload,
				xpc_notify_func_t func, void *key)
{
	xpc_partition_t *part = &xpc_partitions[partid];
	xpc_msg_t *msg = XPC_MSG_ADDRESS(payload);
	xpc_t ret;


	DPRINTK(xpc_chan, XPC_DBG_C_SEND,
		"msg=0x%p, partid=%d, channel=%d\n", (void *) msg, partid,
		ch_number);

	XP_ASSERT(partid > 0 && partid < MAX_PARTITIONS);
	XP_ASSERT(ch_number >= 0 && ch_number < part->nchannels);
	XP_ASSERT(msg != NULL);
	XP_ASSERT(func != NULL);

	ret = xpc_initiate_send(&part->channels[ch_number], msg, XPC_N_CALL,
								func, key);
	return ret;
}


static __inline__ xpc_msg_t *
xpc_pull_remote_msg(xpc_channel_t *ch, s64 get)
{
	xpc_partition_t *part = &xpc_partitions[ch->partid];
	xpc_msg_t *remote_msg, *msg;
	u32 msg_index, nmsgs;
	u64 msg_offset;
	xpc_t ret;


	if (down_interruptible(&ch->msg_to_pull_sema) != 0) {
		/* we were interrupted by a signal */
		return NULL;
	}

	while (get >= ch->next_msg_to_pull) {

		/* pull as many messages as are ready and able to be pulled */

		msg_index = ch->next_msg_to_pull % ch->remote_nentries;

		XP_ASSERT(ch->next_msg_to_pull < ch->w_remote_GP.put);
		nmsgs =  ch->w_remote_GP.put - ch->next_msg_to_pull;
		if (msg_index + nmsgs > ch->remote_nentries) {
			/* ignore the ones that wrap the msg queue for now */
			nmsgs = ch->remote_nentries - msg_index;
		}

		msg_offset = msg_index * ch->msg_size;
		msg = (xpc_msg_t *) ((u64) ch->remote_msgqueue + msg_offset);
		remote_msg = (xpc_msg_t *) (ch->remote_msgqueue_pa +
								msg_offset);

		if ((ret = xpc_pull_remote_cachelines(part, msg, remote_msg,
				nmsgs * ch->msg_size)) != xpcSuccess) {

			DPRINTK(xpc_chan, XPC_DBG_C_RECEIVE,
				"failed to pull %d msgs starting with msg %ld "
				"from partition %d, channel=%d, ret=%d\n",
				nmsgs, ch->next_msg_to_pull, ch->partid,
				ch->number, ret);

			XPC_DEACTIVATE_PARTITION(part, ret);

			up(&ch->msg_to_pull_sema);
			return NULL;
		}

		mb();	/* >>> this may not be needed, we're not sure */

		ch->next_msg_to_pull += nmsgs;
	}

	up(&ch->msg_to_pull_sema);

	/* return the message we were looking for */
	msg_offset = (get % ch->remote_nentries) * ch->msg_size;
	msg = (xpc_msg_t *) ((u64) ch->remote_msgqueue + msg_offset);

	return msg;
}


/*
 * Get a message to be delivered.
 */
static xpc_msg_t *
xpc_get_deliverable_msg(xpc_channel_t *ch)
{
	xpc_msg_t *msg = NULL;
	s64 get;


	do {
		if (ch->flags & XPC_C_DISCONNECTING) {
			break;
		}

		get = ch->w_local_GP.get;
		if (get == ch->w_remote_GP.put) {
			break;
		}

		/* There are messages waiting to be pulled and delivered.
		 * We need to try to secure one for ourselves. We'll do this
		 * by trying to increment w_local_GP.get and hope that no one
		 * else beats us to it. If they do, we'll we'll simply have
		 * to try again for the next one.
	 	 */

		if (cmpxchg(&ch->w_local_GP.get, get, get + 1) == get) {
			/* we got the entry referenced by get */

			DPRINTK(xpc_chan, (XPC_DBG_C_RECEIVE | XPC_DBG_C_GP),
				"w_local_GP.get changed to %ld, partid=%d, "
				"channel=%d\n", get + 1,
				ch->partid, ch->number);

			/* pull the message from the remote partition */

			msg = xpc_pull_remote_msg(ch, get);

			XP_ASSERT(msg == NULL || msg->number == get);
			XP_ASSERT(msg == NULL || !(msg->flags & XPC_M_DONE));
			XP_ASSERT(msg == NULL || msg->flags & XPC_M_READY);

			break;
		}

	} while (1);

	return msg;
}


/*
 * Deliver a message to its intended recipient.
 */
void
xpc_deliver_msg(xpc_channel_t *ch)
{
	xpc_msg_t *msg;


	if ((msg = xpc_get_deliverable_msg(ch)) != NULL) {

		/*
		 * This ref is taken to protect the payload itself from being
		 * freed before the user is finished with it, which the user
		 * indicates by calling xpc_received().
		 */
		XPC_MSGQUEUE_REF(ch);

		atomic_inc(&ch->kthreads_active);

		DPRINTK(xpc_chan, XPC_DBG_C_RECEIVE,
			"XPC_CALL_CHANNEL_FUNC() called, msg=0x%p, "
			"msg_number=%ld, partid=%d, channel=%d\n",
			(void *) msg, msg->number, ch->partid, ch->number);

		/* deliver the message to its intended recipient */
		XPC_CALL_CHANNEL_FUNC(ch, xpcMsgReceived, &msg->payload);

		DPRINTK(xpc_chan, XPC_DBG_C_RECEIVE,
			"XPC_CALL_CHANNEL_FUNC() returned, msg=0x%p, "
			"msg_number=%ld, partid=%d, channel=%d\n",
			(void *) msg, msg->number, ch->partid, ch->number);

		atomic_dec(&ch->kthreads_active);
	}
}


/*
 * Now we actually acknowledge the messages that have been delivered and ack'd
 * by advancing the cached remote message queue's Get value and if requested
 * send an IPI to the message sender's partition.
 */
static void
xpc_acknowledge_msgs(xpc_channel_t *ch, s64 initial_get, u8 msg_flags)
{
	xpc_msg_t *msg;
	s64 get = initial_get + 1;
	int send_IPI = 0;


	while (1) {

		while (1) {
			if (get == ch->w_local_GP.get) {
				break;
			}

			msg = (xpc_msg_t *) ((u64) ch->remote_msgqueue +
			       (get % ch->remote_nentries) * ch->msg_size);

			if (!(msg->flags & XPC_M_DONE)) {
				break;
			}

			msg_flags |= msg->flags;
			get++;
		}

		if (get == initial_get) {
			/* nothing's changed */
			break;
		}

		if (cmpxchg_rel(&ch->local_GP->get, initial_get, get) !=
								initial_get) {
			/* someone else beat us to it */
			XP_ASSERT(ch->local_GP->get > initial_get);
			break;
		}

		/* we just set the new value of local_GP->get */

		DPRINTK(xpc_chan, (XPC_DBG_C_RECEIVE | XPC_DBG_C_GP),
			"local_GP->get changed to %ld, partid=%d, channel=%d\n",
			get, ch->partid, ch->number);

		send_IPI = (msg_flags & XPC_M_INTERRUPT);

		/*
		 * We need to ensure that the message referenced by
		 * local_GP->get is not XPC_M_DONE or that local_GP->get
		 * equals w_local_GP.get, so we'll go have a look.
		 */
		initial_get = get;
	}

	if (send_IPI) {
		XPC_IPI_SEND_MSGREQUEST(ch);
	}
}


/*
 * Acknowledge receipt of a delivered message.
 *
 * If a message has XPC_M_INTERRUPT set, send an interrupt to the partition
 * that sent the message.
 *
 * This function, although called by users, does not call XPC_PART_REF() to
 * ensure that the partition infrastructure is in place. It relies on the
 * fact that we called XPC_MSGQUEUE_REF() in xpc_deliver_msg().
 *
 * Arguments:
 *
 *	partid - ID of partition to which the channel is connected.
 *	ch_number - channel # message received on.
 *	payload - pointer to the payload area allocated via xpc_allocate().
 */
void
xpc_received(partid_t partid, int ch_number, void *payload)
{
	xpc_partition_t *part = &xpc_partitions[partid];
	xpc_channel_t *ch;
	xpc_msg_t *msg = XPC_MSG_ADDRESS(payload);
	s64 get, msg_number = msg->number;


	XP_ASSERT(partid > 0 && partid < MAX_PARTITIONS);
	XP_ASSERT(ch_number >= 0 && ch_number < part->nchannels);

	ch = &part->channels[ch_number];

	DPRINTK(xpc_chan, XPC_DBG_C_RECEIVE,
		"msg=0x%p, msg_number=%ld, partid=%d, channel=%d\n",
		(void *) msg, msg_number, ch->partid, ch->number);

	XP_ASSERT((((u64) msg - (u64) ch->remote_msgqueue) / ch->msg_size) ==
					msg_number % ch->remote_nentries);
	XP_ASSERT(!(msg->flags & XPC_M_DONE));

	msg->flags |= XPC_M_DONE;

	/*
	 * The preceding store of msg->flags must occur before the following
	 * load of ch->local_GP->get.
	 */
	mb();

	/*
	 * See if this message is next in line to be acknowledged as having
	 * been delivered.
	 */
	get = ch->local_GP->get;
	if (get == msg_number) {
		xpc_acknowledge_msgs(ch, get, msg->flags);
	}

	/* the call to XPC_MSGQUEUE_REF() was done by xpc_deliver_msg()  */
	XPC_MSGQUEUE_DEREF(ch);
}


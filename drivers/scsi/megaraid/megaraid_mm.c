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
 * FILE		: megaraid_mm.c
 * Version	: v2.20.0.0 (June 23 2004)
 *
 * Common management module
 */

#include "megaraid_mm.h"

MODULE_AUTHOR("LSI Logic Corporation");
MODULE_DESCRIPTION("LSI Logic Management Module");
MODULE_LICENSE("GPL");
MODULE_VERSION(LSI_COMMON_MOD_VERSION);

static int dbglevel = CL_ANN;
module_param_named(dlevel, dbglevel, int, 0);
MODULE_PARM_DESC(dlevel, "Debug level (default=0)");

EXPORT_SYMBOL(mraid_mm_register_adp);
EXPORT_SYMBOL(mraid_mm_unregister_adp);

static int majorno;
static uint32_t drvr_ver	= 0x01000000;

static int slots_inuse	= 0;
static mraid_mmadp_t  adparr[MAX_LSI_CMN_ADAPS];

wait_queue_head_t wait_q;

static struct file_operations lsi_fops = {
	.open	= mraid_mm_open,
	.ioctl	= mraid_mm_ioctl,
	.owner	= THIS_MODULE,
};

/**
 * mraid_mm_open - open routine for char node interface
 * @inod	: unused
 * @filep	: unused
 *
 * allow ioctl operations by apps only if they superuser privilege
 */
static int
mraid_mm_open(struct inode *inode, struct file *filep)
{
	/*
	 * Only allow superuser to access private ioctl interface
	 */
	if (!capable(CAP_SYS_ADMIN)) return (-EACCES);

	return 0;
}

/**
 * mraid_mm_ioctl - module entry-point for ioctls
 * @inode	: inode (ignored)
 * @filep	: file operations pointer (ignored)
 * @cmd		: ioctl command
 * @arg		: user ioctl packet
 */
static int
mraid_mm_ioctl(struct inode *inode, struct file *filep, unsigned int cmd,
							unsigned long arg)
{
	uioc_t		*kioc;
	char		signature[EXT_IOCTL_SIGN_SZ]	= {0};
	int		rval;
	mraid_mmadp_t	*adp;
	int		adp_index;
	uint8_t		old_ioctl;
	int		drvrcmd_rval;

	/*
	 * Make sure only USCSICMD are issued through this interface.
	 * MIMD application would still fire different command.
	 */

	if ((_IOC_TYPE(cmd) != MEGAIOC_MAGIC) && (cmd != USCSICMD)) {
		return (-EINVAL);
	}

	/*
	 * Look for signature to see if this is the new or old ioctl format.
	 */
	if (copy_from_user(signature, (char *)arg, EXT_IOCTL_SIGN_SZ)) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid cmm: copy from usr addr failed\n"));
		return (-EFAULT);
	}

	if (memcmp(signature, EXT_IOCTL_SIGN, EXT_IOCTL_SIGN_SZ) == 0)
		old_ioctl = 0;
	else
		old_ioctl = 1;

	/*
	 * At present, we don't support the new ioctl packet
	 */
	if (!old_ioctl )
		return (-EINVAL);

	/*
	 * If it is a driver ioctl (as opposed to fw ioctls), then we can
	 * handle the command locally. rval > 0 means it is not a drvr cmd
	 */
	rval = handle_drvrcmd(arg, old_ioctl, &drvrcmd_rval);

	if (rval < 0)
		return rval;
	else if (rval == 0)
		return drvrcmd_rval;

	if ((rval = mraid_mm_get_adpindex((mimd_t*)arg, &adp_index))) {
		return rval;
	}

	adp = &adparr[adp_index];

	/*
	 * The following call will block till a kioc is available
	 */
	kioc = mraid_mm_alloc_kioc(adp);

	/*
	 * User sent the old mimd_t ioctl packet. Convert it to uioc_t.
	 */
	if ((rval = mimd_to_kioc((mimd_t*)arg, adp, kioc))) {
		mraid_mm_dealloc_kioc(adp, kioc);
		return rval;
	}

	kioc->done = ioctl_done;

	/*
	 * Issue the IOCTL to the low level driver
	 */
	if ((rval = lld_ioctl(adp, kioc))) {
		mraid_mm_dealloc_kioc(adp, kioc);
		return rval;
	}

	/*
	 * Convert the kioc back to user space
	 */
	rval = kioc_to_mimd(kioc, (mimd_t *)arg);

	/*
	 * Return the kioc to free pool
	 */
	mraid_mm_dealloc_kioc(adp, kioc);

	return rval;
}


/**
 * mraid_mm_get_adpindex - Returns adp number from mimd_t user packet
 * @umimd	: User space mimd_t ioctl packet
 * @adp_index	: Contains adp number if success
 */
static int
mraid_mm_get_adpindex(mimd_t *umimd, int *adp_index)
{
	mimd_t		mimd;
	uint32_t	adapno;

	*adp_index = -1;

	if (copy_from_user( &mimd, umimd, sizeof(mimd_t)))
		return (-EFAULT);

	adapno = GETADAP(mimd.ui.fcs.adapno);

	if (adapno >= slots_inuse)
		return (-ENODEV);

	*adp_index = adapno;

	return 0;
}

/*
 * handle_drvrcmd - This routine checks if the opcode is a driver
 * 			  cmd and if it is, handles it.
 * @arg		: packet sent by the user app
 * @old_ioctl	: mimd if 1; uioc otherwise
 */
static int
handle_drvrcmd(unsigned long arg, uint8_t old_ioctl, int *rval)
{
	mimd_t		*umimd;
	mimd_t		kmimd;
	uint8_t		opcode;
	uint8_t		subopcode;

	if (old_ioctl)
		goto old_packet;
	else
		goto new_packet;

new_packet:
	return (-ENOTSUPP);

old_packet:
	*rval = 0;
	umimd = (mimd_t*) arg;

	if (copy_from_user(&kmimd, umimd, sizeof(mimd_t)))
		return (-EFAULT);

	opcode		= kmimd.ui.fcs.opcode;
	subopcode	= kmimd.ui.fcs.subopcode;

	/*
	 * If the opcode is 0x82 and the subopcode is either GET_DRVRVER or
	 * GET_NUMADP, then we can handle. Otherwise we should return 1 to
	 * indicate that we cannot handle this.
	 */
	if (opcode != 0x82)
		return 1;

	switch (subopcode) {

	case MEGAIOC_QDRVRVER:

		if (copy_to_user(kmimd.data, &drvr_ver, sizeof(uint32_t)))
			return (-EFAULT);

		return 0;

	case MEGAIOC_QNADAP:

		*rval = slots_inuse;

		if (copy_to_user(kmimd.data, &slots_inuse, sizeof(uint32_t)))
			return (-EFAULT);

		return 0;

	default:
		/* cannot handle */
		return 1;
	}

	return 0;
}


/**
 * mimd_to_kioc	- Converter from old to new ioctl format
 *
 * @umimd	: user space old MIMD IOCTL
 * @kioc	: kernel space new format IOCTL
 *
 * Routine to convert MIMD interface IOCTL to new interface IOCTL packet. The
 * new packet is in kernel space so that driver can perform operations on it
 * freely.
 */

static int
mimd_to_kioc(mimd_t *umimd, mraid_mmadp_t *adp, uioc_t *kioc)
{
	mbox64_t		*mbox64;
	mraid_passthru_t	*pthru32;
	uint32_t		adapno;
	uint8_t			opcode;
	uint8_t			subopcode;
	mimd_t			mimd;

	if (copy_from_user(&mimd, umimd, sizeof(mimd_t)))
		return (-EFAULT);

	/*
	 * Applications are not allowed to send extd pthru
	 */
	if ((mimd.mbox[0] == MBOXCMD_PASSTHRU64) ||
			(mimd.mbox[0] == MBOXCMD_EXTPTHRU))
		return (-EINVAL);

	opcode		= mimd.ui.fcs.opcode;
	subopcode	= mimd.ui.fcs.subopcode;
	adapno		= GETADAP(mimd.ui.fcs.adapno);

	if (adapno >= slots_inuse)
		return (-ENODEV);

	kioc->adapno	= adapno;
	kioc->mb_type	= MBOX_LEGACY;
	kioc->app_type	= APPTYPE_MIMD;

	switch (opcode) {

	case 0x82:

		if (subopcode == MEGAIOC_QADAPINFO) {

			kioc->opcode	= GET_ADAP_INFO;
			kioc->data_dir	= UIOC_RD;
			kioc->xferlen	= sizeof(mraid_hba_info_t);

			if (mraid_mm_attach_buf(adp, kioc, kioc->xferlen))
				return (-ENOMEM);
		}
		else {
			con_log(CL_ANN, (KERN_WARNING
					"megaraid cmm: Invalid subop\n"));
			return (-EINVAL);
		}

		break;

	case 0x81:

		kioc->opcode	= MBOX_CMD;
		kioc->xferlen	= mimd.ui.fcs.length;

		if (mraid_mm_attach_buf(adp, kioc, kioc->xferlen))
			return (-ENOMEM);

		if (mimd.outlen) kioc->data_dir  = UIOC_RD;
		if (mimd.inlen) kioc->data_dir |= UIOC_WR;

		break;

	case 0x80:

		kioc->opcode	= MBOX_CMD;
		kioc->xferlen	= (mimd.outlen > mimd.inlen) ?
					mimd.outlen : mimd.inlen;

		if (mraid_mm_attach_buf(adp, kioc, kioc->xferlen))
			return (-ENOMEM);

		if (mimd.outlen) kioc->data_dir  = UIOC_RD;
		if (mimd.inlen) kioc->data_dir |= UIOC_WR;

		break;

	default:
		return (-EINVAL);
	}

	/*
	 * If driver command, nothing else to do
	 */
	if (opcode == 0x82)
		return 0;

	/*
	 * This is a mailbox cmd; copy the mailbox from mimd
	 */
	mbox64 = (mbox64_t*)((unsigned long)kioc->cmdbuf);
	memcpy(&(mbox64->mbox32), mimd.mbox, 18);

	mbox64->xferaddr_lo	= mbox64->mbox32.xferaddr;
	mbox64->xferaddr_hi	= 0;
	mbox64->mbox32.xferaddr	= 0xffffffff;

	if (mbox64->mbox32.cmd != MBOXCMD_PASSTHRU) {	// regular DCMD

		kioc->user_data		= (caddr_t)(unsigned long)
						mbox64->xferaddr_lo;
		kioc->user_data_len	= kioc->xferlen;
		mbox64->xferaddr_lo	= (unsigned long)kioc->buf_paddr;

		if (kioc->data_dir & UIOC_WR) {
			if (copy_from_user(kioc->buf_vaddr, kioc->user_data,
							kioc->xferlen)) {
				return (-EFAULT);
			}
		}

		return 0;
	}

	/*
	 * This is a regular 32-bit pthru cmd; mbox points to pthru struct.
	 * Just like in above case, the beginning for memblk is treated as
	 * a mailbox. The passthru will begin at next 1K boundary. And the
	 * data will start 1K after that.
	 */
	mbox64->mbox32.cmd = MBOXCMD_PASSTHRU;

	pthru32			= kioc->pthru32;
	kioc->user_pthru	= (mraid_passthru_t *)(unsigned long)
					mbox64->xferaddr_lo;
	mbox64->xferaddr_lo	= kioc->pthru32_h;

	if (copy_from_user(pthru32, (caddr_t)kioc->user_pthru,
			sizeof(mraid_passthru_t))) {
		return (-EFAULT);
	}

	kioc->user_data		= (caddr_t)(unsigned long)
						pthru32->dataxferaddr;
	pthru32->dataxferaddr	= kioc->buf_paddr;
	kioc->user_data_len	= pthru32->dataxferlen;

	if (kioc->data_dir & UIOC_WR) {
		if (copy_from_user(kioc->buf_vaddr, kioc->user_data,
						pthru32->dataxferlen)) {
			return (-EFAULT);
		}
	}

	return 0;
}

/**
 * mraid_mm_attch_buf - Attach a free dma buffer for required size
 *
 * @adp		: Adapter softstate
 * @kioc	: kioc that the buffer needs to be attached to
 * @xferlen	: required length for buffer
 *
 * First we search for a pool with smallest buffer that is >= @xferlen. If
 * that pool has no free buffer, we will try for the next bigger size. If none
 * is available, we will try to allocate the smallest buffer that is >=
 * @xferlen and attach it the pool.
 */
static int
mraid_mm_attach_buf(mraid_mmadp_t *adp, uioc_t *kioc, int xferlen)
{
	mm_dmapool_t	*pool;
	int		right_pool = -1;
	unsigned long	flags;
	int		i;

	kioc->pool_index	= -1;
	kioc->buf_vaddr		= 0;
	kioc->buf_paddr		= 0;
	kioc->free_buf		= 0;

	/*
	 * We need xferlen amount of memory. See if we can get it from our
	 * dma pools. If we don't get exact size, we will try bigger buffer
	 */

	for (i = 0; i < MAX_DMA_POOLS; i++) {

		pool = &adp->dma_pool_list[i];

		if (xferlen > pool->buf_size)
			continue;

		if (right_pool == -1)
			right_pool = i;

		spin_lock_irqsave(&pool->lock, flags);

		if (!pool->in_use) {

			pool->in_use		= 1;
			kioc->pool_index	= i;
			kioc->buf_vaddr		= pool->vaddr;
			kioc->buf_paddr		= pool->paddr;

			spin_unlock_irqrestore(&pool->lock, flags);
			return 0;
		}
		else {
			spin_unlock_irqrestore(&pool->lock, flags);
			continue;
		}
	}

	/*
	 * If xferlen doesn't match any of our pools, return error
	 */
	if (right_pool == -1)
		return -EINVAL;

	/*
	 * We did not get any buffer from the preallocated pool. Let us try
	 * to allocate one new buffer. NOTE: This is a blocking call.
	 */
	pool = &adp->dma_pool_list[right_pool];

	spin_lock_irqsave(&pool->lock, flags);

	kioc->pool_index	= right_pool;
	kioc->free_buf		= 1;
	kioc->buf_vaddr 	= pci_pool_alloc(pool->handle, GFP_KERNEL,
							&kioc->buf_paddr);
	spin_unlock_irqrestore(&pool->lock, flags);

	if (!kioc->buf_vaddr)
		return -ENOMEM;

	return 0;
}

/**
 * mraid_mm_alloc_kioc - Returns a uioc_t from free list
 * @adp	: Adapter softstate for this module
 *
 * The kioc_semaphore is initialized with number of kioc nodes in the
 * free kioc pool. If the kioc pool is empty, this function blocks till
 * a kioc becomes free.
 */
static uioc_t *
mraid_mm_alloc_kioc(mraid_mmadp_t *adp)
{
	uioc_t			*kioc;
	struct list_head*	head;
	unsigned long		flags;

	down(&adp->kioc_semaphore);

	spin_lock_irqsave(&adp->kioc_pool_lock, flags);

	head = &adp->kioc_pool;

	if (list_empty(head)) {
		up(&adp->kioc_semaphore);
		spin_unlock_irqrestore(&adp->kioc_pool_lock, flags);

		con_log(CL_ANN, ("megaraid cmm: kioc list empty!\n"));
		return NULL;
	}

	kioc = list_entry(head->next, uioc_t, list);
	list_del_init(&kioc->list);

	spin_unlock_irqrestore(&adp->kioc_pool_lock, flags);

	memset((caddr_t)(unsigned long)kioc->cmdbuf, 0, sizeof(mbox64_t));
	memset((caddr_t) kioc->pthru32, 0, sizeof(mraid_passthru_t));

	kioc->buf_vaddr		= 0;
	kioc->buf_paddr		= 0;
	kioc->pool_index	=-1;
	kioc->free_buf		= 0;
	kioc->user_data		= 0;
	kioc->user_data_len	= 0;
	kioc->user_pthru	= 0;

	return kioc;
}

/**
 * mraid_mm_dealloc_kioc - Return kioc to free pool
 *
 * @adp		: Adapter softstate
 * @kioc	: uioc_t node to be returned to free pool
 */
static void
mraid_mm_dealloc_kioc(mraid_mmadp_t *adp, uioc_t *kioc)
{
	mm_dmapool_t	*pool;
	unsigned long	flags;

	pool = &adp->dma_pool_list[kioc->pool_index];

	/* This routine may be called in non-isr context also */
	spin_lock_irqsave(&pool->lock, flags);

	/*
	 * While attaching the dma buffer, if we didn't get the required
	 * buffer from the pool, we would have allocated it at the run time
	 * and set the free_buf flag. We must free that buffer. Otherwise,
	 * just mark that the buffer is not in use
	 */
	if (kioc->free_buf == 1)
		pci_pool_free(pool->handle, kioc->buf_vaddr, kioc->buf_paddr);
	else
		pool->in_use = 0;

	spin_unlock_irqrestore(&pool->lock, flags);

	/* Return the kioc to the free pool */
	spin_lock_irqsave(&adp->kioc_pool_lock, flags);
	list_add(&kioc->list, &adp->kioc_pool);
	spin_unlock_irqrestore(&adp->kioc_pool_lock, flags);

	/* increment the free kioc count */
	up(&adp->kioc_semaphore);

	return;
}

/**
 * lld_ioctl - Routine to issue ioctl to low level drvr
 *
 * @adp		: The adapter entry in adparr
 * @kioc	: The ioctl packet with kernel addresses
 */
static int
lld_ioctl(mraid_mmadp_t *adp, uioc_t *kioc)
{
	int			rval;
	struct timer_list	timer;
	struct timer_list	*tp = NULL;

	kioc->status	= -ENODATA;
	rval		= adp->issue_uioc(adp->drvr_data, kioc, IOCTL_ISSUE);

	if (rval) return rval;

	/*
	 * Start the timer
	 */
	if (adp->timeout > 0) {
		tp		= &timer;
		init_timer(tp);

		tp->function	= lld_timedout;
		tp->data	= (unsigned long)kioc;
		tp->expires	= jiffies + adp->timeout * HZ;

		add_timer(tp);
	}

	/*
	 * Wait till the low level driver completes the ioctl. After this
	 * call, the ioctl either completed successfully or timedout.
	 */
	wait_event(wait_q, (kioc->status != -ENODATA));
	if (tp) {
		del_timer_sync(tp);
	}

	return kioc->status;
}


/**
 * ioctl_done - callback from the low level driver
 *
 * @kioc	: completed ioctl packet
 */
static void
ioctl_done(uioc_t *kioc)
{
	/*
	 * When the kioc returns from driver, make sure it still doesn't
	 * have ENODATA in status. Otherwise, driver will hang on wait_event
	 * forever
	 */
	if (kioc->status == -ENODATA) {
		con_log(CL_ANN, (KERN_WARNING
			"megaraid cmm: lld didn't change status!\n"));

		kioc->status = -EINVAL;
	}

	wake_up(&wait_q);
}


/*
 * lld_timedout	: callback from the expired timer
 *
 * @ptr		: ioctl packet that timed out
 */
static void
lld_timedout(unsigned long ptr)
{
	uioc_t *kioc	= (uioc_t *)ptr;

	kioc->status 	= -ETIME;

	con_log(CL_ANN, (KERN_WARNING "megaraid cmm: ioctl timed out\n"));

	wake_up(&wait_q);
}


/**
 * kioc_to_mimd	: Converter from new back to old format
 *
 * @kioc	: Kernel space IOCTL packet (successfully issued)
 * @mimd	: User space MIMD packet
 */
static int
kioc_to_mimd(uioc_t *kioc, mimd_t *mimd)
{
	mimd_t			kmimd;
	uint8_t			opcode;
	uint8_t			subopcode;

	mbox64_t		*mbox64;
	mraid_passthru_t	*upthru32;
	mraid_passthru_t	*kpthru32;
	mcontroller_t		cinfo;
	mraid_hba_info_t	*hinfo;


	if (copy_from_user( &kmimd, mimd, sizeof(mimd_t)))
		return (-EFAULT);

	opcode		= kmimd.ui.fcs.opcode;
	subopcode	= kmimd.ui.fcs.subopcode;

	if (opcode == 0x82) {
		switch (subopcode) {

		case MEGAIOC_QADAPINFO:

			hinfo = (mraid_hba_info_t*)(unsigned long)
					kioc->buf_vaddr;
			hinfo_to_cinfo( hinfo, &cinfo );

			if (copy_to_user(kmimd.data, &cinfo, sizeof(cinfo)))
				return (-EFAULT);

			return 0;

		default:
			return (-EINVAL);
		}

		return 0;
	}

	mbox64 = (mbox64_t*)(unsigned long)kioc->cmdbuf;

	if (kioc->user_pthru) {

		upthru32 = kioc->user_pthru;
		kpthru32 = kioc->pthru32;

		if (copy_to_user((void*)&(upthru32->scsistatus),
					(void*)&(kpthru32->scsistatus),
					sizeof(uint8_t))) {
			return (-EFAULT);
		}
	}

	if (kioc->user_data) {
		if (copy_to_user(kioc->user_data, kioc->buf_vaddr,
					kioc->user_data_len)) {
			return (-EFAULT);
		}
	}

	if (copy_to_user((void*)&mimd->mbox[17], (void*)&mbox64->mbox32.status,
				sizeof(uint8_t))) {
		return (-EFAULT);
	}

	return 0;
}


/**
 * hinfo_to_cinfo - Convert new format hba info into old format
 *
 * @hinfo	: New format, more comprehensive adapter info
 * @cinfo	: Old format adapter info to support mimd_t apps
 */
static void
hinfo_to_cinfo(mraid_hba_info_t *hinfo, mcontroller_t *cinfo)
{
	if (!hinfo || !cinfo)
		return;

	cinfo->base		= hinfo->baseport;
	cinfo->irq		= hinfo->irq;
	cinfo->numldrv		= hinfo->num_ldrv;
	cinfo->pcibus		= hinfo->pci_bus;
	cinfo->pcidev		= hinfo->pci_slot;
	cinfo->pcifun		= PCI_FUNC(hinfo->pci_dev_fn);
	cinfo->pciid		= hinfo->pci_device_id;
	cinfo->pcivendor	= hinfo->pci_vendor_id;
	cinfo->pcislot		= hinfo->pci_slot;
	cinfo->uid		= hinfo->unique_id;
}


/*
 * mraid_mm_register_adp - Registration routine for low level drvrs
 *
 * @adp	: Adapter objejct
 */
int
mraid_mm_register_adp(mraid_mmadp_t *lld_adp)
{
	mraid_mmadp_t	*adapter;
	mbox64_t	*mbox_list;
	uioc_t		*kioc;
	uint32_t	rval;
	int		i;


	if (lld_adp->drvr_type != DRVRTYPE_MBOX)
		return (-EINVAL);

	adapter	= &adparr[slots_inuse];
	memset(adapter, 0, sizeof(mraid_mmadp_t));

	adapter->unique_id	= lld_adp->unique_id;
	adapter->drvr_type	= lld_adp->drvr_type;
	adapter->drvr_data	= lld_adp->drvr_data;
	adapter->pdev		= lld_adp->pdev;
	adapter->issue_uioc	= lld_adp->issue_uioc;
	adapter->timeout	= lld_adp->timeout;
	adapter->max_kioc	= lld_adp->max_kioc;

	/*
	 * Allocate single blocks of memory for all required kiocs,
	 * mailboxes and passthru structures.
	 */
	adapter->kioc_list	= kmalloc(sizeof(uioc_t) * lld_adp->max_kioc,
						GFP_KERNEL);
	adapter->mbox_list	= kmalloc(sizeof(mbox64_t) * lld_adp->max_kioc,
						GFP_KERNEL);
	adapter->pthru_dma_pool = pci_pool_create("megaraid mm pthru pool",
						adapter->pdev,
						sizeof(mraid_passthru_t),
						16, 0);

	if (!adapter->kioc_list || !adapter->mbox_list ||
			!adapter->pthru_dma_pool) {

		con_log(CL_ANN, (KERN_WARNING
			"megaraid cmm: out of memory, %s %d\n", __FUNCTION__,
			__LINE__));

		rval = (-ENOMEM);

		goto memalloc_error;
	}

	/*
	 * Slice kioc_list and make a kioc_pool with the individiual kiocs
	 */
	INIT_LIST_HEAD(&adapter->kioc_pool);
	spin_lock_init(&adapter->kioc_pool_lock);
	sema_init(&adapter->kioc_semaphore, lld_adp->max_kioc);

	mbox_list	= (mbox64_t *)adapter->mbox_list;

	for (i = 0; i < lld_adp->max_kioc; i++) {

		kioc		= adapter->kioc_list + i;
		kioc->cmdbuf	= (uint64_t)(unsigned long)(mbox_list + i);
		kioc->pthru32	= pci_pool_alloc(adapter->pthru_dma_pool,
						GFP_KERNEL, &kioc->pthru32_h);

		if (!kioc->pthru32) {

			con_log(CL_ANN, (KERN_WARNING
				"megaraid cmm: out of memory, %s %d\n",
					__FUNCTION__, __LINE__));

			rval = (-ENOMEM);

			goto pthru_dma_pool_error;
		}

		list_add_tail(&kioc->list, &adapter->kioc_pool);
	}

	// Setup the dma pools for data buffers
	if ((rval = mraid_mm_setup_dma_pools(adapter)) != 0) {
		goto dma_pool_error;
	}

	slots_inuse++;
	return 0;

dma_pool_error:
	/* Do nothing */

pthru_dma_pool_error:

	for (i = 0; i < lld_adp->max_kioc; i++) {
		kioc = adapter->kioc_list + i;
		if (kioc->pthru32) {
			pci_pool_free(adapter->pthru_dma_pool, kioc->pthru32,
				kioc->pthru32_h);
		}
	}

memalloc_error:

	if (adapter->kioc_list)
		kfree(adapter->kioc_list);

	if (adapter->mbox_list)
		kfree(adapter->mbox_list);

	if (adapter->pthru_dma_pool)
		pci_pool_destroy(adapter->pthru_dma_pool);

	return rval;
}

/**
 * mraid_mm_setup_dma_pools - Set up dma buffer pools per adapter
 *
 * @adp	: Adapter softstate
 *
 * We maintain a pool of dma buffers per each adapter. Each pool has one
 * buffer. E.g, we may have 5 dma pools - one each for 4k, 8k ... 64k buffers.
 * We have just one 4k buffer in 4k pool, one 8k buffer in 8k pool etc. We
 * dont' want to waste too much memory by allocating more buffers per each
 * pool.
 */
static int
mraid_mm_setup_dma_pools(mraid_mmadp_t *adp)
{
	mm_dmapool_t	*pool;
	int		bufsize;
	int		i;

	/*
	 * Create MAX_DMA_POOLS number of pools
	 */
	bufsize = MRAID_MM_INIT_BUFF_SIZE;

	for (i = 0; i < MAX_DMA_POOLS; i++){

		pool = &adp->dma_pool_list[i];

		pool->buf_size = bufsize;
		spin_lock_init(&pool->lock);

		pool->handle = pci_pool_create("megaraid mm data buffer",
						adp->pdev, bufsize, 16, 0);

		if (!pool->handle) {
			goto dma_pool_setup_error;
		}

		pool->vaddr = pci_pool_alloc(pool->handle, GFP_KERNEL,
							&pool->paddr);

		if (!pool->vaddr)
			goto dma_pool_setup_error;

		bufsize = bufsize * 2;
	}

	return 0;

dma_pool_setup_error:

	mraid_mm_teardown_dma_pools(adp);
	return (-ENOMEM);
}


/*
 * mraid_mm_unregister_adp - Unregister routine for low level drivers
 *				  Assume no outstanding ioctls to llds.
 *
 * @unique_id	: UID of the adpater
 */
int
mraid_mm_unregister_adp(uint32_t unique_id)
{
	int i;

	for (i = 0; i < MAX_LSI_CMN_ADAPS; i++) {

		if (adparr[i].unique_id == unique_id) {

			mraid_mm_free_adp_resources(&adparr[i]);

			memset(&adparr[i], 0, sizeof(mraid_mmadp_t));

			con_log(CL_ANN, (
				"megaraid cmm: Unregistered one adapter:%#x\n",
				unique_id));

			return 0;
		}
	}

	return (-ENODEV);
}

/**
 * mraid_mm_free_adp_resources - Free adapter softstate
 *
 * @adp	: Adapter softstate
 */
static void
mraid_mm_free_adp_resources(mraid_mmadp_t *adp)
{
	uioc_t	*kioc;
	int	i;

	INIT_LIST_HEAD(&adp->kioc_pool);

	kfree(adp->kioc_list);

	kfree(adp->mbox_list);

	for (i = 0; i < adp->max_kioc; i++) {

		kioc = adp->kioc_list + i;

		pci_pool_free(adp->pthru_dma_pool, kioc->pthru32,
				kioc->pthru32_h);
	}

	pci_pool_destroy(adp->pthru_dma_pool);

	mraid_mm_teardown_dma_pools(adp);

	return;
}


/**
 * mraid_mm_teardown_dma_pools - Free all per adapter dma buffers
 *
 * @adp	: Adapter softstate
 */
static void
mraid_mm_teardown_dma_pools(mraid_mmadp_t *adp)
{
	int		i;
	mm_dmapool_t	*pool;

	for (i = 0; i < MAX_DMA_POOLS; i++) {

		pool = &adp->dma_pool_list[i];

		if (pool->handle) {

			if (pool->vaddr)
				pci_pool_free(pool->handle, pool->vaddr,
							pool->paddr);

			pci_pool_destroy(pool->handle);
			pool->handle = 0;
		}
	}

	return;
}

/**
 * mraid_mm_init	: Module entry point
 */
static int __init
mraid_mm_init(void)
{
	// Announce the driver version
	con_log(CL_ANN, (KERN_INFO "megaraid cmm: %s %s\n",
		LSI_COMMON_MOD_VERSION, LSI_COMMON_MOD_EXT_VERSION));

	majorno = register_chrdev(0, "megadev", &lsi_fops);

	if (majorno < 0) {
		con_log(CL_ANN, ("megaraid cmm: cannot get major\n"));
		return majorno;
	}

	init_waitqueue_head(&wait_q);
	memset(adparr, 0, sizeof(mraid_mmadp_t) * MAX_LSI_CMN_ADAPS);

	slots_inuse = 0;

	register_ioctl32_conversion(MEGAIOCCMD, mraid_mm_compat_ioctl);

	return 0;
}


/**
 * mraid_mm_compat_ioctl	: 32bit to 64bit ioctl conversion routine
 */
#ifdef LSI_CONFIG_COMPAT
static int
mraid_mm_compat_ioctl(unsigned int fd, unsigned int cmd,
			unsigned long arg, struct file *filep)
{
	struct inode *inode = filep->f_dentry->d_inode;

	return mraid_mm_ioctl(inode, filep, cmd, arg);
}
#endif

/**
 * mraid_mm_exit	: Module exit point
 */
static void __exit
mraid_mm_exit(void)
{
	con_log(CL_DLEVEL1 , ("exiting common mod\n"));

	unregister_chrdev(majorno, "megadev");
	unregister_ioctl32_conversion(MEGAIOCCMD);
}

module_init(mraid_mm_init);
module_exit(mraid_mm_exit);

/* vi: set ts=8 sw=8 tw=78: */

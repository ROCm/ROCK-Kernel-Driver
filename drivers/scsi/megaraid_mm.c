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
 * Version	: v1.0.0.0.04.17.2004 (Apr 14 2004)
 *
 * Common management module
 */

#include "megaraid_mm.h"

MODULE_AUTHOR		("LSI Logic Corporation");
MODULE_DESCRIPTION	("LSI Logic Management Module");
MODULE_LICENSE		("GPL");
MODULE_PARM		(dbglevel, "i");
MODULE_PARM_DESC	(dbglevel, "Debug level (default=0)");

EXPORT_SYMBOL		( mraid_mm_register_adp );
EXPORT_SYMBOL		( mraid_mm_unregister_adp );

static int		majorno;
static int		dbglevel	= CL_DLEVEL1;
spinlock_t		lc_lock		= SPIN_LOCK_UNLOCKED;
static uint32_t		drvr_ver	= 0x01000000;

static int		slots_inuse	= 0;
static mraid_mmadp_t 	adparr[MAX_LSI_CMN_ADAPS];

wait_queue_head_t	wait_q;

static struct file_operations lsi_fops = {

	.ioctl		= megaraid_mm_ioctl,
	.open		= megaraid_mm_open,
	.release	= megaraid_mm_close,
	.owner		= THIS_MODULE,
};

static int
megaraid_mm_ioctl( struct inode* inode, struct file* filep, unsigned int cmd,
							unsigned long arg )
{
	uioc_t*		kioc;
	char		signature[EXT_IOCTL_SIGN_SZ]	= {0};
	int		rval;
	mraid_mmadp_t*	adp;
	int		adp_index;
	uint8_t		old_ioctl;
	int		drvrcmd_rval;

	/*
	 * Make sure only USCSICMD are issued through this interface.
	 * MIMD application would still fire different command.
	 */

	if( (_IOC_TYPE(cmd) != MEGAIOC_MAGIC) && (cmd != USCSICMD) ) {
		return -EINVAL;
	}

	/* 
	 * Look for signature to see if this is the new or old ioctl format. 
	 */
	if( copy_from_user(signature, (char *)arg, EXT_IOCTL_SIGN_SZ) ) {
		con_log(CL_ANN,(KERN_WARNING "cp from usr addr failed\n"));
		return (-EFAULT);
	}	

	if( memcmp(signature, EXT_IOCTL_SIGN, EXT_IOCTL_SIGN_SZ) == 0 ) 
		old_ioctl = MRAID_FALSE;
	else
		old_ioctl = MRAID_TRUE;

	/*
	 * If it is a driver ioctl (as opposed to fw ioctls), then we can
	 * handle the command locally. rval > 0 means it is not a drvr cmd
	 */
	rval = handle_drvrcmd( arg, old_ioctl, &drvrcmd_rval );

	if (rval < 0)	
		return rval;
	else if (rval == 0)
		return drvrcmd_rval;

	/* 
	 * Call the approprite converter to convert to kernel space
	 */
	if (old_ioctl == MRAID_FALSE) {
		/* 
		 * User sent the new uioc_t packet. We don't support it yet.
		 */
		return (-EINVAL);
	}
	else {
		/* 
		 * User sent the old mimd_t ioctl packet. Convert it to
		 * uioc. If there is an error, the mutexes and other resources
		 * would have been released already. So we can just return.
		 */
		if ((rval = mimd_to_kioc((mimd_t*)arg, &adp_index))) {
			return rval;
		}
	}

	adp		= &adparr[adp_index];
	kioc		= &adp->kioc;
	kioc->done	= ioctl_done;

	/* 
	 * Issue the IOCTL to the low level driver
	 */
	if ((rval = lld_ioctl( adp, kioc ))) {
		up( &adp->kioc_mtx );
		return rval;
	}

	/* 
	 * Convert the kioc back to user space 
	 */
	rval = kioc_to_mimd( kioc, (mimd_t*) arg );
	up( &adp->kioc_mtx );

	return rval;
}

/*
 * handle_drvrcmd()	: This routine checks if the opcode is a driver
 * 			  cmd and if it is, handles it.
 *
 * @arg			: packet sent by the user app
 * @old_ioctl		: mimd if MRAID_TRUE; uioc otherwise
 */
static int
handle_drvrcmd( ulong arg, uint8_t old_ioctl, int* rval )
{
	mimd_t*		umimd;
	mimd_t		kmimd;
	uint8_t		opcode;
	uint8_t		subopcode;

	if (old_ioctl == MRAID_TRUE)
		goto old_packet;
	else
		goto new_packet;

new_packet:
	return (-ENOTSUPP);

old_packet:
	*rval = 0;
	umimd = (mimd_t*) arg;

	if (copy_from_user( &kmimd, umimd, sizeof(mimd_t)))
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

	switch( subopcode ) {

	case MEGAIOC_QDRVRVER:

		if (copy_to_user(kmimd.data, &drvr_ver, sizeof(uint32_t)))
			return (-EFAULT);

		return 0;

	case MEGAIOC_QNADAP:

		*rval = slots_inuse;

		if(copy_to_user(kmimd.data, &slots_inuse, sizeof(uint32_t)))
			return (-EFAULT);

		return 0;

	default:
		/* cannot handle */
		return 1;
	}

	return 0;
}

/**
 * mimd_to_kioc	: Converter from old to new ioctl format
 * 
 * @umimd	: user space old MIMD IOCTL
 * @kioc	: kernel space new format IOCTL
 *
 * Routine to convert MIMD interface IOCTL to new interface IOCTL packet. The
 * new packet is in kernel space so that driver can perform operations on it
 * freely.
 */

static int
mimd_to_kioc( mimd_t* umimd, int* adp_index )
{
	uint64_t		temp;
	uint32_t		adapno;
	uint8_t			opcode;
	uint8_t			subopcode;
	uint32_t		bufsz = 0;
	mimd_t			mimd;

	mraid_mmadp_t*		adp;
	mbox64_t*		mbox64;
	mraid_passthru_t*	pthru32;
	uioc_t*			kioc;

	if( copy_from_user( &mimd, umimd, sizeof(mimd_t)) )
		return (-EFAULT);

	/*
	 * Applications are not allowed to send extd pthru
	 */
	if( mimd.mbox[0] == 0xC3 )
		return (-EINVAL);

	opcode		= mimd.ui.fcs.opcode;
	subopcode	= mimd.ui.fcs.subopcode;
	adapno		= GETADAP(mimd.ui.fcs.adapno);

	if( adapno >= slots_inuse ) 
		return (-ENODEV);

	adp = &adparr[ adapno ];

	down( &adp->kioc_mtx );

	kioc		= &adp->kioc;
	kioc->adapno	= adapno;
	*adp_index	= adapno;

	kioc->mb_type	= MBOX_LEGACY;
	kioc->app_type	= APPTYPE_MIMD;

	switch( opcode ) {

	case 0x82:

		if (subopcode == MEGAIOC_QADAPINFO) {

			kioc->opcode	= GET_ADAP_INFO;
			kioc->data_dir	= UIOC_RD;
			kioc->xferlen	= sizeof(mraid_hba_info_t);
			kioc->cmdbuf	= (ulong)adp->memblk;
			bufsz		= kioc->xferlen;
		}
		else {
			con_log( CL_ANN, ("Invalid subop\n"));

			up( &adp->kioc_mtx );
			return (-EINVAL);
		}

		break;

	case 0x81:

		kioc->opcode	= MBOX_CMD;
		kioc->xferlen	= mimd.ui.fcs.length;
		bufsz		= sizeof(mbox64_t);
		kioc->cmdbuf	= (ulong)adp->memblk;

		if( mimd.outlen ) kioc->data_dir  = UIOC_RD;
		if( mimd.inlen  ) kioc->data_dir |= UIOC_WR;

		break;

	case 0x80:

		bufsz		= sizeof(mbox64_t);
		kioc->opcode	= MBOX_CMD;
		kioc->xferlen	= (mimd.outlen > mimd.inlen) ?
					mimd.outlen : mimd.inlen;
		kioc->cmdbuf	= (ulong)adp->memblk;

		if( mimd.outlen ) kioc->data_dir  = UIOC_RD;
		if( mimd.inlen  ) kioc->data_dir |= UIOC_WR;

		break;

	default:
		up( &adp->kioc_mtx );
		return (-EINVAL);
	}

	memset( (void*)((ulong)kioc->cmdbuf), 0, bufsz );
	
	/*
	 * If driver command, nothing else to do
	 */
	if( opcode == 0x82 ) 
		return 0;

	/*
	 * This is a mailbox cmd; copy the mailbox from mimd 
	 */
	mbox64 = (mbox64_t*) ((ulong)kioc->cmdbuf);
	memcpy( &(mbox64->mbox32), mimd.mbox, 18 );
	
	mbox64->xferaddr_lo	= mbox64->mbox32.xferaddr;
	mbox64->xferaddr_hi	= 0;
	mbox64->mbox32.xferaddr	= 0xffffffff;

	if( mbox64->mbox32.cmd != 0x03 ) {	/* Not pthru; regular DCMD */

		/*
		 * We had allocated 28K for memblk. adp->kioc.cmdbuf is 
		 * is pointing to the beginning for that memory block. Since
		 * this is a mailbox command, the beginning of the block is
		 * treated as a mailbox. Now we need the data. We will leave
		 * the first 1k for mailbox and have int_data point to the 
		 * memblk + 1024
		 */
		adp->int_data		= adp->memblk + 1024;
		adp->int_data_dmah	= (ulong)adp->memblk_dmah + 1024;

		temp			= mbox64->xferaddr_lo;
		mbox64->xferaddr_lo	= adp->int_data_dmah;
		adp->int_data_len	= kioc->xferlen;
		adp->int_data_user	= (caddr_t)(ulong)temp;
		
		if( kioc->data_dir & UIOC_WR ) {
			if(copy_from_user(adp->int_data, 
					(void*)(ulong)temp, kioc->xferlen)){
				up( &adp->kioc_mtx );
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
	mbox64->mbox32.cmd = 0x03;

	adp->int_pthru		= adp->memblk + 1024;
	adp->int_pthru_dmah	= adp->memblk_dmah + 1024;

	temp			= mbox64->xferaddr_lo;
	pthru32			= (mraid_passthru_t*)adp->int_pthru;
	mbox64->xferaddr_lo	= adp->int_pthru_dmah;
	adp->int_pthru_len	= sizeof(mraid_passthru_t);
	adp->int_pthru_user	= (caddr_t)(ulong)temp;

	if( copy_from_user( pthru32, (void*)(ulong)temp, 
					sizeof(mraid_passthru_t)) ) {
		up( &adp->kioc_mtx );
		return (-EFAULT);
	}

	adp->int_data			= adp->memblk + 2048;
	adp->int_data_dmah		= adp->memblk_dmah + 2048;

	temp				= pthru32->dataxferaddr;
	pthru32->dataxferaddr		= adp->int_data_dmah;
	adp->int_data_len		= pthru32->dataxferlen;
	adp->int_data_user		= (caddr_t)(ulong)temp;

	if( copy_from_user(adp->int_data, (void*)(ulong)temp,
					adp->int_data_len) ) {
		up( &adp->kioc_mtx );
		return (-EFAULT);
	}

	return 0;
}

/**
 * lld_ioctl	: Routine to issue ioctl to low level drvr
 *
 * @adp		: The adapter entry in adparr
 * @kioc	: The ioctl packet with kernel addresses
 */
static int
lld_ioctl( mraid_mmadp_t* adp, uioc_t* kioc )
{
	int			rval;
	struct timer_list	timer;
	struct timer_list*	tp = NULL;

	if (!adp || !kioc)
		return (-EINVAL);

	kioc->status	= LC_PENDING;
	rval		= adp->issue_uioc( adp->drvr_data, kioc, IOCTL_ISSUE); 

	if( rval )
		return rval;

	/*
	 * Start the timer
	 */
	if (adp->timeout > 0) {
		tp		= &timer;
		init_timer(tp);

		tp->function	= lld_timedout;
		tp->data	= (ulong) kioc;
		tp->expires	= jiffies + adp->timeout * HZ;

		add_timer(tp);
	}

	/*
	 * Wait till the low level driver completes the ioctl. After this
	 * call, the ioctl either completed successfully or timedout.
	 */
	wait_event( wait_q, (kioc->status != LC_PENDING) );
	del_timer_sync( tp );

	return 0;
}

/*
 * ioctl_done	: callback from the low level driver
 *
 * @kioc	: completed ioctl packet
 */
void
ioctl_done( uioc_t* kioc )
{
	/*
	 * When the kioc returns from driver, make sure it still doesn't
	 * have LC_PENDING in status. Otherwise, driver will hang on
	 * wait_event forever
	 */
	if( kioc->status == LC_PENDING ){
		con_log( CL_ANN, ("lld didn't change status!!\n"));
		kioc->status = LC_UNKNOWN;
	}

	wake_up( &wait_q );
}

/*
 * lld_timedout	: callback from the expired timer
 *
 * @ptr		: ioctl packet that timed out
 */
void
lld_timedout( ulong ptr )
{
	uioc_t* kioc	= (uioc_t*)ptr;
	kioc->status 	= LC_ETIME;

	wake_up( &wait_q );
}

/**
 * kioc_to_mimd	: Converter from new back to old format
 *
 * @kioc	: Kernel space IOCTL packet (successfully issued)
 * @mimd	: User space MIMD packet
 */
static int
kioc_to_mimd( uioc_t* kioc, mimd_t* mimd )
{
	mimd_t			kmimd;
	uint8_t			opcode;
	uint8_t			subopcode;

	mbox64_t*		mbox64;
	mraid_passthru_t*	upthru32;
	mraid_passthru_t*	kpthru32;
	mcontroller_t		cinfo;
	mraid_hba_info_t*	hinfo;

	mraid_mmadp_t*		adp = &adparr[ kioc->adapno ];

	if (kioc->status != LC_SUCCESS)
		return (-EFAULT);

	if (copy_from_user( &kmimd, mimd, sizeof(mimd_t)))
		return (-EFAULT);

	opcode		= kmimd.ui.fcs.opcode;
	subopcode	= kmimd.ui.fcs.subopcode;

	if( opcode == 0x82 ) {
		switch( subopcode ) {

		case MEGAIOC_QADAPINFO:

			hinfo = (mraid_hba_info_t*)(ulong)kioc->cmdbuf;
			hinfo_to_cinfo( hinfo, &cinfo );

			if (copy_to_user(kmimd.data, &cinfo, sizeof(cinfo)))
				return (-EFAULT);

			return 0;

		default:
			return (-EINVAL);
		}

		return 0;
	}

	mbox64 = (mbox64_t*) (ulong)kioc->cmdbuf;

	if( adp->int_pthru_len ) {

		upthru32 = (mraid_passthru_t*)adp->int_pthru_user;
		kpthru32 = (mraid_passthru_t*)adp->int_pthru;

		if( copy_to_user( (void*) &(upthru32->scsistatus),
					(void*)&(kpthru32->scsistatus),
					sizeof( uint8_t )) ) {
			return (-EFAULT);
		}
	}

	if( adp->int_data_len ) {
		if( copy_to_user(adp->int_data_user, adp->int_data,
					adp->int_data_len ) ) {
			return (-EFAULT);
		}
	}

	if (copy_to_user((void*)&mimd->mbox[17], (void*)&mbox64->mbox32.status,
				sizeof(uint8_t))) {
		return (-EFAULT);
	}

	adp->int_data		= NULL;
	adp->int_data_len	= 0;
	adp->int_data_dmah	= 0;
	adp->int_data_user	= NULL;
	adp->int_pthru		= NULL;
	adp->int_pthru_len	= 0;
	adp->int_pthru_dmah	= 0;
	adp->int_pthru_user	= NULL;

	return 0;
}

static void
hinfo_to_cinfo( mraid_hba_info_t* hinfo, mcontroller_t* cinfo )
{
	if( !hinfo || !cinfo )
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
 * mraid_mm_register_adp	: Registration routine for low level drvrs
 *
 * @adp				: Adapter objejct
 */
uint32_t
mraid_mm_register_adp( mraid_mmadp_t* adp )
{
	int	i;
	int	cur_slot;

	caddr_t		memblk;
	dma_addr_t	memblk_dmah;

	if (!adp) 
		return LC_EINVAL;

	if (adp->drvr_type != DRVRTYPE_MBOX)
		return LC_ENOTSUPP;

	memblk = pci_alloc_consistent(adp->pdev, MEMBLK_SZ, &memblk_dmah);

	if( !memblk )
		return LC_ENOMEM;

	spin_lock( &lc_lock );

	if (slots_inuse >= MAX_LSI_CMN_ADAPS ) {
		spin_unlock( &lc_lock );
		return LC_EFULL;
	}

	cur_slot = slots_inuse++;
	spin_unlock( &lc_lock );

	/* 
	 * Return error if it is a duplicate unique_id 
	 */
	for (i=0; i < MAX_LSI_CMN_ADAPS; i++ ) {
		if (adparr[i].unique_id == adp->unique_id) {
			return LC_EEXISTS;
		}
	}

	adparr[cur_slot].unique_id	= adp->unique_id;
	adparr[cur_slot].drvr_type	= adp->drvr_type;
	adparr[cur_slot].drvr_data	= adp->drvr_data;
	adparr[cur_slot].pdev		= adp->pdev;
	adparr[cur_slot].issue_uioc	= adp->issue_uioc;
	adparr[cur_slot].timeout	= adp->timeout;
	adparr[cur_slot].memblk		= memblk;
	adparr[cur_slot].memblk_dmah	= memblk_dmah;

	init_MUTEX( &adparr[cur_slot].kioc_mtx );

	return 0;
}

/*
 * mraid_mm_unregister_adp	: Unregister routine for low level drivers
 *
 * @unique_id			: UID of the adpater
 */
uint32_t
mraid_mm_unregister_adp( uint32_t unique_id )
{
	int i;

	spin_lock( &lc_lock );

	for (i = 0; i < MAX_LSI_CMN_ADAPS; i++ ) {
		if (adparr[i].unique_id == unique_id) {

			pci_free_consistent( adparr[i].pdev, MEMBLK_SZ,
				adparr[i].memblk, adparr[i].memblk_dmah );
		
			memset( &adparr[i], 0, sizeof(mraid_mmadp_t) );
			spin_unlock( &lc_lock );

			con_log( CL_ANN, ("Unregistered one lsi adp\n"));
			return 0;
		}
	}

	spin_unlock( &lc_lock );
	return LC_ENOADP;
}

static int
megaraid_mm_open( struct inode *inode, struct file *filep )
{
	return 0;
}

static int
megaraid_mm_close( struct inode *inode, struct file *filep )
{
	return 0;
}

static int __init
megaraid_mm_init(void)
{
	// Announce the driver version
	con_log(CL_ANN, (KERN_INFO "megaraid_mm: %s\n", 
					LSI_COMMON_MOD_VERSION));

	majorno = register_chrdev( 0, "megadev", &lsi_fops );

	if (majorno < 0) {
		con_log( CL_ANN, ("lsi_cmnmod: cannot get major\n"));
		return majorno;
	}
	init_waitqueue_head( &wait_q );
	memset( adparr, 0, sizeof(mraid_mmadp_t) * MAX_LSI_CMN_ADAPS );

	slots_inuse = 0;

	register_ioctl32_conversion( MEGAIOCCMD, megaraid_mm_compat_ioctl );
	
	return 0; 
}

#ifdef LSI_CONFIG_COMPAT
static int
megaraid_mm_compat_ioctl( unsigned int fd, unsigned int cmd,
			ulong arg, struct file* filep )
{
	struct inode *inode = filep->f_dentry->d_inode;

	return megaraid_mm_ioctl(inode, filep, cmd, arg);
}
#endif

static void __exit
megaraid_mm_exit(void)
{
	con_log( CL_ANN, ("exiting common mod \n" ));

	unregister_chrdev( majorno, "megadev" );
	unregister_ioctl32_conversion( MEGAIOCCMD );
}

module_init( megaraid_mm_init);
module_exit( megaraid_mm_exit);

/* vi: set ts=8 sw=8 tw=78: */

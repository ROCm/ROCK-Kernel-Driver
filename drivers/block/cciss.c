/*
 *    Disk Array driver for Compaq SMART2 Controllers
 *    Copyright 2000 Compaq Computer Corporation
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *    Questions/Comments/Bugfixes to arrays@compaq.com
 *
 */

#include <linux/config.h>	/* CONFIG_PROC_FS */
#include <linux/module.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/blkpg.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <linux/init.h> 
#include <linux/hdreg.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/blk.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>

#define CCISS_DRIVER_VERSION(maj,min,submin) ((maj<<16)|(min<<8)|(submin))
#define DRIVER_NAME "Compaq CISS Driver (v 2.4.5)"
#define DRIVER_VERSION CCISS_DRIVER_VERSION(2,4,5)

/* Embedded module documentation macros - see modules.h */
MODULE_AUTHOR("Charles M. White III - Compaq Computer Corporation");
MODULE_DESCRIPTION("Driver for Compaq Smart Array Controller 5300");

#include "cciss_cmd.h"
#include "cciss.h"
#include <linux/cciss_ioctl.h>

/* define the PCI info for the cards we can control */
const struct pci_device_id cciss_pci_device_id[] = {
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_CISS,
			0x0E11, 0x4070, 0, 0, 0},
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_CISSB,
                        0x0E11, 0x4080, 0, 0, 0},
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_CISSB,
                        0x0E11, 0x4082, 0, 0, 0},
	{0,}
};
MODULE_DEVICE_TABLE(pci, cciss_pci_device_id);

#define NR_PRODUCTS (sizeof(products)/sizeof(struct board_type))

/*  board_id = Subsystem Device ID & Vendor ID
 *  product = Marketing Name for the board
 *  access = Address of the struct of function pointers 
 */
static struct board_type products[] = {
	{ 0x40700E11, "Smart Array 5300",	&SA5_access },
	{ 0x40800E11, "Smart Array 5i", &SA5B_access},
	{ 0x40820E11, "Smart Array 532", &SA5B_access},
};

/* How long to wait (in millesconds) for board to go into simple mode */
#define MAX_CONFIG_WAIT 1000 

#define READ_AHEAD 	 128
#define NR_CMDS		 128 /* #commands that can be outstanding */
#define MAX_CTLR 8

#define CCISS_DMA_MASK	0xFFFFFFFF	/* 32 bit DMA */

static ctlr_info_t *hba[MAX_CTLR];

static struct proc_dir_entry *proc_cciss;

static void do_cciss_request(request_queue_t *q);
static int cciss_open(struct inode *inode, struct file *filep);
static int cciss_release(struct inode *inode, struct file *filep);
static int cciss_ioctl(struct inode *inode, struct file *filep, 
		unsigned int cmd, unsigned long arg);

static int revalidate_allvol(kdev_t dev);
static int revalidate_logvol(kdev_t dev, int maxusage);
static int frevalidate_logvol(kdev_t dev);

static void cciss_getgeometry(int cntl_num);

static inline void addQ(CommandList_struct **Qptr, CommandList_struct *c);
static void start_io( ctlr_info_t *h);

#ifdef CONFIG_PROC_FS
static int cciss_proc_get_info(char *buffer, char **start, off_t offset, 
		int length, int *eof, void *data);
static void cciss_procinit(int i);
#else
static int cciss_proc_get_info(char *buffer, char **start, off_t offset, 
		int length, int *eof, void *data) { return 0;}
static void cciss_procinit(int i) {}
#endif /* CONFIG_PROC_FS */

static struct block_device_operations cciss_fops  = {
	open:			cciss_open, 
	release:        	cciss_release,
        ioctl:			cciss_ioctl,
	revalidate:		frevalidate_logvol,
};

/*
 * Report information about this controller.
 */
#ifdef CONFIG_PROC_FS
static int cciss_proc_get_info(char *buffer, char **start, off_t offset, 
		int length, int *eof, void *data)
{
        off_t pos = 0;
        off_t len = 0;
        int size, i, ctlr;
        ctlr_info_t *h = (ctlr_info_t*)data;
        drive_info_struct *drv;

        ctlr = h->ctlr;
        size = sprintf(buffer, "%s:  Compaq %s Controller\n"
                "       Board ID: 0x%08lx\n"
		"       Firmware Version: %c%c%c%c\n"
                "       Memory Address: 0x%08lx\n"
                "       IRQ: %d\n"
                "       Logical drives: %d\n"
                "       Current Q depth: %d\n"
		"       Current # commands on controller %d\n"
                "       Max Q depth since init: %d\n"
		"       Max # commands on controller since init: %d\n"
		"       Max SG entries since init: %d\n\n",
                h->devname,
                h->product_name,
                (unsigned long)h->board_id,
		h->firm_ver[0], h->firm_ver[1], h->firm_ver[2], h->firm_ver[3],
                (unsigned long)h->vaddr,
                (unsigned int)h->intr,
                h->num_luns, 
                h->Qdepth, h->commands_outstanding,
		h->maxQsinceinit, h->max_outstanding, h->maxSG);

        pos += size; len += size;
	for(i=0; i<h->num_luns; i++) {
                drv = &h->drv[i];
                size = sprintf(buffer+len, "cciss/c%dd%d: blksz=%d nr_blocks=%d\n",
                                ctlr, i, drv->block_size, drv->nr_blocks);
                pos += size; len += size;
        }

	size = sprintf(buffer+len, "nr_allocs = %d\nnr_frees = %d\n",
                        h->nr_allocs, h->nr_frees);
        pos += size; len += size;

        *eof = 1;
        *start = buffer+offset;
        len -= offset;
        if (len>length)
                len = length;
        return len;
}

/*
 * Get us a file in /proc/cciss that says something about each controller.
 * Create /proc/cciss if it doesn't exist yet.
 */
static void __init cciss_procinit(int i)
{
        if (proc_cciss == NULL) {
                proc_cciss = proc_mkdir("cciss", proc_root_driver);
                if (!proc_cciss) 
			return;
        }

        create_proc_read_entry(hba[i]->devname, 0, proc_cciss,
        		cciss_proc_get_info, hba[i]);
}
#endif /* CONFIG_PROC_FS */

/* 
 * For operations that cannot sleep, a command block is allocated at init, 
 * and managed by cmd_alloc() and cmd_free() using a simple bitmap to track
 * which ones are free or in use.  For operations that can wait for kmalloc 
 * to possible sleep, this routine can be called with get_from_pool set to 0. 
 * cmd_free() MUST be called with a got_from_pool set to 0 if cmd_alloc was. 
 */ 
static CommandList_struct * cmd_alloc(ctlr_info_t *h, int get_from_pool)
{
	CommandList_struct *c;
	int i; 
	u64bit temp64;
	dma_addr_t cmd_dma_handle, err_dma_handle;

	if (!get_from_pool)
	{
		c = (CommandList_struct *) pci_alloc_consistent(
			h->pdev, sizeof(CommandList_struct), &cmd_dma_handle); 
        	if(c==NULL)
                 	return NULL;
		memset(c, 0, sizeof(CommandList_struct));

		c->err_info = (ErrorInfo_struct *)pci_alloc_consistent(
					h->pdev, sizeof(ErrorInfo_struct), 
					&err_dma_handle);
	
		if (c->err_info == NULL)
		{
			pci_free_consistent(h->pdev, 
				sizeof(CommandList_struct), c, cmd_dma_handle);
			return NULL;
		}
		memset(c->err_info, 0, sizeof(ErrorInfo_struct));
	} else /* get it out of the controllers pool */ 
	{
	     	do {
                	i = find_first_zero_bit(h->cmd_pool_bits, NR_CMDS);
                        if (i == NR_CMDS)
                                return NULL;
                } while(test_and_set_bit(i%32, h->cmd_pool_bits+(i/32)) != 0);
#ifdef CCISS_DEBUG
		printk(KERN_DEBUG "cciss: using command buffer %d\n", i);
#endif
                c = h->cmd_pool + i;
		memset(c, 0, sizeof(CommandList_struct));
		cmd_dma_handle = h->cmd_pool_dhandle 
					+ i*sizeof(CommandList_struct);
		c->err_info = h->errinfo_pool + i;
		memset(c->err_info, 0, sizeof(ErrorInfo_struct));
		err_dma_handle = h->errinfo_pool_dhandle 
					+ i*sizeof(ErrorInfo_struct);
                h->nr_allocs++;
        }

	c->busaddr = (__u32) cmd_dma_handle;
	temp64.val = (__u64) err_dma_handle;	
	c->ErrDesc.Addr.lower = temp64.val32.lower;
	c->ErrDesc.Addr.upper = temp64.val32.upper;
	c->ErrDesc.Len = sizeof(ErrorInfo_struct);
	
	c->ctlr = h->ctlr;
        return c;


}

/* 
 * Frees a command block that was previously allocated with cmd_alloc(). 
 */
static void cmd_free(ctlr_info_t *h, CommandList_struct *c, int got_from_pool)
{
	int i;
	u64bit temp64;

	if( !got_from_pool)
	{ 
		temp64.val32.lower = c->ErrDesc.Addr.lower;
		temp64.val32.upper = c->ErrDesc.Addr.upper;
		pci_free_consistent(h->pdev, sizeof(ErrorInfo_struct), 
			c->err_info, (dma_addr_t) temp64.val);
		pci_free_consistent(h->pdev, sizeof(CommandList_struct), 
			c, (dma_addr_t) c->busaddr);
	} else 
	{
		i = c - h->cmd_pool;
		clear_bit(i%32, h->cmd_pool_bits+(i/32));
                h->nr_frees++;
        }
}

/*  
 * fills in the disk information. 
 */
static void cciss_geninit( int ctlr)
{
	drive_info_struct *drv;
	int i,j;
	
	/* Loop through each real device */ 
	hba[ctlr]->gendisk.nr_real = 0; 
	for(i=0; i< NWD; i++)
	{
		drv = &(hba[ctlr]->drv[i]);
		if( !(drv->nr_blocks))
			continue;
		hba[ctlr]->hd[i << NWD_SHIFT].nr_sects = 
		hba[ctlr]->sizes[i << NWD_SHIFT] = drv->nr_blocks;

		/* for each partition */ 
		for(j=0; j<MAX_PART; j++)
		{
			hba[ctlr]->blocksizes[(i<<NWD_SHIFT) + j] = 1024; 

			hba[ctlr]->hardsizes[ (i<<NWD_SHIFT) + j] = 
				drv->block_size;
		}
		hba[ctlr]->gendisk.nr_real++;
	}
}
/*
 * Open.  Make sure the device is really there.
 */
static int cciss_open(struct inode *inode, struct file *filep)
{
	int ctlr = MAJOR(inode->i_rdev) - MAJOR_NR;
	int dsk  = MINOR(inode->i_rdev) >> NWD_SHIFT;

#ifdef CCISS_DEBUG
	printk(KERN_DEBUG "cciss_open %x (%x:%x)\n", inode->i_rdev, ctlr, dsk);
#endif /* CCISS_DEBUG */ 

	if (ctlr > MAX_CTLR || hba[ctlr] == NULL)
		return -ENXIO;

	if (!suser() && hba[ctlr]->sizes[ MINOR(inode->i_rdev)] == 0)
		return -ENXIO;

	/*
	 * Root is allowed to open raw volume zero even if its not configured
	 * so array config can still work.  I don't think I really like this,
	 * but I'm already using way to many device nodes to claim another one
	 * for "raw controller".
	 */
	if (suser()
		&& (hba[ctlr]->sizes[MINOR(inode->i_rdev)] == 0) 
		&& (MINOR(inode->i_rdev)!= 0))
		return -ENXIO;

	hba[ctlr]->drv[dsk].usage_count++;
	hba[ctlr]->usage_count++;
	MOD_INC_USE_COUNT;
	return 0;
}
/*
 * Close.  Sync first.
 */
static int cciss_release(struct inode *inode, struct file *filep)
{
	int ctlr = MAJOR(inode->i_rdev) - MAJOR_NR;
	int dsk  = MINOR(inode->i_rdev) >> NWD_SHIFT;

#ifdef CCISS_DEBUG
	printk(KERN_DEBUG "cciss_release %x (%x:%x)\n", inode->i_rdev, ctlr, dsk);
#endif /* CCISS_DEBUG */

	/* fsync_dev(inode->i_rdev); */

	hba[ctlr]->drv[dsk].usage_count--;
	hba[ctlr]->usage_count--;
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * ioctl 
 */
static int cciss_ioctl(struct inode *inode, struct file *filep, 
		unsigned int cmd, unsigned long arg)
{
	int ctlr = MAJOR(inode->i_rdev) - MAJOR_NR;
	int dsk  = MINOR(inode->i_rdev) >> NWD_SHIFT;
	int diskinfo[4];
	struct hd_geometry *geo = (struct hd_geometry *)arg;

#ifdef CCISS_DEBUG
	printk(KERN_DEBUG "cciss_ioctl: Called with cmd=%x %lx\n", cmd, arg);
#endif /* CCISS_DEBUG */ 
	
	switch(cmd) {
	case HDIO_GETGEO:
		if (hba[ctlr]->drv[dsk].cylinders) {
			diskinfo[0] = hba[ctlr]->drv[dsk].heads;
			diskinfo[1] = hba[ctlr]->drv[dsk].sectors;
			diskinfo[2] = hba[ctlr]->drv[dsk].cylinders;
		} else {
			diskinfo[0] = 0xff;
			diskinfo[1] = 0x3f;
			diskinfo[2] = hba[ctlr]->drv[dsk].nr_blocks / (0xff*0x3f);		}
		put_user(diskinfo[0], &geo->heads);
		put_user(diskinfo[1], &geo->sectors);
		put_user(diskinfo[2], &geo->cylinders);
		put_user(hba[ctlr]->hd[MINOR(inode->i_rdev)].start_sect, &geo->start);
		return 0;
	case BLKGETSIZE:
		put_user(hba[ctlr]->hd[MINOR(inode->i_rdev)].nr_sects, (long*)arg);
		return 0;
	case BLKGETSIZE64:
		put_user((u64)hba[ctlr]->hd[MINOR(inode->i_rdev)].nr_sects << 9, (u64*)arg);
		return 0;
	case BLKRRPART:
		return revalidate_logvol(inode->i_rdev, 1);
	case BLKFLSBUF:
	case BLKBSZSET:
	case BLKBSZGET:
	case BLKROSET:
	case BLKROGET:
	case BLKRASET:
	case BLKRAGET:
	case BLKPG:
		return( blk_ioctl(inode->i_rdev, cmd, arg));
	case CCISS_GETPCIINFO:
	{
		cciss_pci_info_struct pciinfo;

		if (!arg) return -EINVAL;
		pciinfo.bus = hba[ctlr]->pdev->bus->number;
		pciinfo.dev_fn = hba[ctlr]->pdev->devfn;
		pciinfo.board_id = hba[ctlr]->board_id;
		if (copy_to_user((void *) arg, &pciinfo,  sizeof( cciss_pci_info_struct )))
			return  -EFAULT;
		return(0);
	}	
	case CCISS_GETINTINFO:
	{
		cciss_coalint_struct intinfo;
		ctlr_info_t *c = hba[ctlr];

		if (!arg) return -EINVAL;
		intinfo.delay = readl(&c->cfgtable->HostWrite.CoalIntDelay);
		intinfo.count = readl(&c->cfgtable->HostWrite.CoalIntCount);
		if (copy_to_user((void *) arg, &intinfo, sizeof( cciss_coalint_struct )))
			return -EFAULT;
                return(0);
        }
	case CCISS_SETINTINFO:
        {
                cciss_coalint_struct intinfo;
                ctlr_info_t *c = hba[ctlr];
		unsigned long flags;
		int i;

		if (!arg) return -EINVAL;	
		if (!capable(CAP_SYS_ADMIN)) return -EPERM;
		if (copy_from_user(&intinfo, (void *) arg, sizeof( cciss_coalint_struct)))
			return -EFAULT;
		if ( (intinfo.delay == 0 ) && (intinfo.count == 0))

		{
//			printk("cciss_ioctl: delay and count cannot be 0\n");
			return( -EINVAL);
		}
		spin_lock_irqsave(&io_request_lock, flags);
		/* Can only safely update if no commands outstanding */ 
		if (c->commands_outstanding > 0 )
		{
//			printk("cciss_ioctl: cannot change coalasing "
//				"%d commands outstanding on controller\n", 
//					c->commands_outstanding);
			spin_unlock_irqrestore(&io_request_lock, flags);
			return(-EINVAL);
		}
		/* Update the field, and then ring the doorbell */ 
		writel( intinfo.delay, 
			&(c->cfgtable->HostWrite.CoalIntDelay));
		writel( intinfo.count, 
                        &(c->cfgtable->HostWrite.CoalIntCount));
		writel( CFGTBL_ChangeReq, c->vaddr + SA5_DOORBELL);

		for(i=0;i<MAX_CONFIG_WAIT;i++)
		{
			if (!(readl(c->vaddr + SA5_DOORBELL) 
					& CFGTBL_ChangeReq))
				break;
			/* delay and try again */
			udelay(1000);
		}	
		spin_unlock_irqrestore(&io_request_lock, flags);
		if (i >= MAX_CONFIG_WAIT)
			return( -EFAULT);
                return(0);
        }
	case CCISS_GETNODENAME:
        {
                NodeName_type NodeName;
                ctlr_info_t *c = hba[ctlr];
		int i; 

		if (!arg) return -EINVAL;
		for(i=0;i<16;i++)
			NodeName[i] = readb(&c->cfgtable->ServerName[i]);
                if (copy_to_user((void *) arg, NodeName, sizeof( NodeName_type)))
                	return  -EFAULT;
                return(0);
        }
	case CCISS_SETNODENAME:
	{
		NodeName_type NodeName;
		ctlr_info_t *c = hba[ctlr];
		unsigned long flags;
		int i;

		if (!arg) return -EINVAL;
		if (!capable(CAP_SYS_ADMIN)) return -EPERM;
		
		if (copy_from_user(NodeName, (void *) arg, sizeof( NodeName_type)))
			return -EFAULT;

		spin_lock_irqsave(&io_request_lock, flags);

			/* Update the field, and then ring the doorbell */ 
		for(i=0;i<16;i++)
			writeb( NodeName[i], &c->cfgtable->ServerName[i]);
			
		writel( CFGTBL_ChangeReq, c->vaddr + SA5_DOORBELL);

		for(i=0;i<MAX_CONFIG_WAIT;i++)
		{
			if (!(readl(c->vaddr + SA5_DOORBELL) 
					& CFGTBL_ChangeReq))
				break;
			/* delay and try again */
			udelay(1000);
		}	
		spin_unlock_irqrestore(&io_request_lock, flags);
		if (i >= MAX_CONFIG_WAIT)
			return( -EFAULT);
                return(0);
        }

	case CCISS_GETHEARTBEAT:
        {
                Heartbeat_type heartbeat;
                ctlr_info_t *c = hba[ctlr];

		if (!arg) return -EINVAL;
                heartbeat = readl(&c->cfgtable->HeartBeat);
                if (copy_to_user((void *) arg, &heartbeat, sizeof( Heartbeat_type)))
                	return -EFAULT;
                return(0);
        }
	case CCISS_GETBUSTYPES:
        {
                BusTypes_type BusTypes;
                ctlr_info_t *c = hba[ctlr];

		if (!arg) return -EINVAL;
                BusTypes = readl(&c->cfgtable->BusTypes);
                if (copy_to_user((void *) arg, &BusTypes, sizeof( BusTypes_type) ))
                	return  -EFAULT;
                return(0);
        }
	case CCISS_GETFIRMVER:
        {
		FirmwareVer_type firmware;

		if (!arg) return -EINVAL;
		memcpy(firmware, hba[ctlr]->firm_ver, 4);

                if (copy_to_user((void *) arg, firmware, sizeof( FirmwareVer_type)))
                	return -EFAULT;
                return(0);
        }
        case CCISS_GETDRIVVER:
        {
		DriverVer_type DriverVer = DRIVER_VERSION;

                if (!arg) return -EINVAL;

                if (copy_to_user((void *) arg, &DriverVer, sizeof( DriverVer_type) ))
                	return -EFAULT;
                return(0);
        }

	case CCISS_REVALIDVOLS:
                return( revalidate_allvol(inode->i_rdev));
	
	case CCISS_PASSTHRU:
	{
		IOCTL_Command_struct iocommand;
		ctlr_info_t *h = hba[ctlr];
		CommandList_struct *c;
		char 	*buff = NULL;
		u64bit	temp64;
		unsigned long flags;

		if (!arg) return -EINVAL;
	
		if (!capable(CAP_SYS_RAWIO)) return -EPERM;

		if (copy_from_user(&iocommand, (void *) arg, sizeof( IOCTL_Command_struct) ))
			return -EFAULT;
		if((iocommand.buf_size < 1) && 
				(iocommand.Request.Type.Direction != XFER_NONE))
		{	
			return -EINVAL;
		} 
		/* Check kmalloc limits */
		if(iocommand.buf_size > 128000)
			return -EINVAL;
		if(iocommand.buf_size > 0)
		{
			buff =  kmalloc(iocommand.buf_size, GFP_KERNEL);
			if( buff == NULL) 
				return -EFAULT;
		}
		if (iocommand.Request.Type.Direction == XFER_WRITE)
		{
			/* Copy the data into the buffer we created */ 
			if (copy_from_user(buff, iocommand.buf, iocommand.buf_size))
			{
				kfree(buff);
				return -EFAULT;
			}
		}
		if ((c = cmd_alloc(h , 0)) == NULL)
		{
			kfree(buff);
			return -ENOMEM;
		}
			// Fill in the command type 
		c->cmd_type = CMD_IOCTL_PEND;
			// Fill in Command Header 
		c->Header.ReplyQueue = 0;  // unused in simple mode
		if( iocommand.buf_size > 0) 	// buffer to fill 
		{
			c->Header.SGList = 1;
			c->Header.SGTotal= 1;
		} else	// no buffers to fill  
		{
			c->Header.SGList = 0;
                	c->Header.SGTotal= 0;
		}
		c->Header.LUN = iocommand.LUN_info;
		c->Header.Tag.lower = c->busaddr;  // use the kernel address the cmd block for tag
		
		// Fill in Request block 
		c->Request = iocommand.Request; 
	
		// Fill in the scatter gather information
		if (iocommand.buf_size > 0 ) 
		{
			temp64.val = pci_map_single( h->pdev, buff,
                                        iocommand.buf_size, 
                                PCI_DMA_BIDIRECTIONAL);	
			c->SG[0].Addr.lower = temp64.val32.lower;
			c->SG[0].Addr.upper = temp64.val32.upper;
			c->SG[0].Len = iocommand.buf_size;
			c->SG[0].Ext = 0;  // we are not chaining
		}
		/* Put the request on the tail of the request queue */
		spin_lock_irqsave(&io_request_lock, flags);
		addQ(&h->reqQ, c);
		h->Qdepth++;
		start_io(h);
		spin_unlock_irqrestore(&io_request_lock, flags);

		/* Wait for completion */
		while(c->cmd_type != CMD_IOCTL_DONE)
			schedule_timeout(1);

		/* unlock the buffers from DMA */
		temp64.val32.lower = c->SG[0].Addr.lower;
                temp64.val32.upper = c->SG[0].Addr.upper;
                pci_unmap_single( h->pdev, (dma_addr_t) temp64.val,
                	iocommand.buf_size, PCI_DMA_BIDIRECTIONAL);

		/* Copy the error information out */ 
		iocommand.error_info = *(c->err_info);
		if ( copy_to_user((void *) arg, &iocommand, sizeof( IOCTL_Command_struct) ) )
		{
			kfree(buff);
			cmd_free(h, c, 0);
			return( -EFAULT);	
		} 	

		if (iocommand.Request.Type.Direction == XFER_READ)
                {
                        /* Copy the data out of the buffer we created */
                        if (copy_to_user(iocommand.buf, buff, iocommand.buf_size))
			{
                        	kfree(buff);
				cmd_free(h, c, 0);
				return -EFAULT;
			}
                }
                kfree(buff);
		cmd_free(h, c, 0);
                return(0);
	} 

	default:
		return -EBADRQC;
	}
	
}

/* Borrowed and adapted from sd.c */
static int revalidate_logvol(kdev_t dev, int maxusage)
{
        int ctlr, target;
        struct gendisk *gdev;
        unsigned long flags;
        int max_p;
        int start;
        int i;

        target = MINOR(dev) >> NWD_SHIFT;
        ctlr = MAJOR(dev) - MAJOR_NR;
        gdev = &(hba[ctlr]->gendisk);

        spin_lock_irqsave(&io_request_lock, flags);
        if (hba[ctlr]->drv[target].usage_count > maxusage) {
                spin_unlock_irqrestore(&io_request_lock, flags);
                printk(KERN_WARNING "cciss: Device busy for "
                        "revalidation (usage=%d)\n",
                        hba[ctlr]->drv[target].usage_count);
                return -EBUSY;
        }
        hba[ctlr]->drv[target].usage_count++;
        spin_unlock_irqrestore(&io_request_lock, flags);

        max_p = gdev->max_p;
        start = target << gdev->minor_shift;

        for(i=max_p-1; i>=0; i--) {
                int minor = start+i;
                invalidate_device(MKDEV(MAJOR_NR + ctlr, minor), 1);
                gdev->part[minor].start_sect = 0;
                gdev->part[minor].nr_sects = 0;

                /* reset the blocksize so we can read the partition table */
                blksize_size[MAJOR_NR+ctlr][minor] = 1024;
        }
	/* setup partitions per disk */
	grok_partitions(gdev, target, MAX_PART, 
			hba[ctlr]->drv[target].nr_blocks);
        hba[ctlr]->drv[target].usage_count--;
        return 0;
}

static int frevalidate_logvol(kdev_t dev)
{
#ifdef CCISS_DEBUG
	printk(KERN_DEBUG "cciss: frevalidate has been called\n");
#endif /* CCISS_DEBUG */ 
	return revalidate_logvol(dev, 0);
}

/*
 * revalidate_allvol is for online array config utilities.  After a
 * utility reconfigures the drives in the array, it can use this function
 * (through an ioctl) to make the driver zap any previous disk structs for
 * that controller and get new ones.
 *
 * Right now I'm using the getgeometry() function to do this, but this
 * function should probably be finer grained and allow you to revalidate one
 * particualar logical volume (instead of all of them on a particular
 * controller).
 */
static int revalidate_allvol(kdev_t dev)
{
	int ctlr, i;
	unsigned long flags;

	ctlr = MAJOR(dev) - MAJOR_NR;
        if (MINOR(dev) != 0)
                return -ENXIO;

        spin_lock_irqsave(&io_request_lock, flags);
        if (hba[ctlr]->usage_count > 1) {
                spin_unlock_irqrestore(&io_request_lock, flags);
                printk(KERN_WARNING "cciss: Device busy for volume"
                        " revalidation (usage=%d)\n", hba[ctlr]->usage_count);
                return -EBUSY;
        }
        spin_unlock_irqrestore(&io_request_lock, flags);
        hba[ctlr]->usage_count++;

        /*
         * Set the partition and block size structures for all volumes
         * on this controller to zero.  We will reread all of this data
         */
	memset(hba[ctlr]->hd,         0, sizeof(struct hd_struct) * 256);
        memset(hba[ctlr]->sizes,      0, sizeof(int) * 256);
        memset(hba[ctlr]->blocksizes, 0, sizeof(int) * 256);
        memset(hba[ctlr]->hardsizes,  0, sizeof(int) * 256);
        memset(hba[ctlr]->drv,        0, sizeof(drive_info_struct)
						* CISS_MAX_LUN);
        hba[ctlr]->gendisk.nr_real = 0;

        /*
         * Tell the array controller not to give us any interrupts while
         * we check the new geometry.  Then turn interrupts back on when
         * we're done.
         */
        hba[ctlr]->access.set_intr_mask(hba[ctlr], CCISS_INTR_OFF);
        cciss_getgeometry(ctlr);
        hba[ctlr]->access.set_intr_mask(hba[ctlr], CCISS_INTR_ON);

        cciss_geninit(ctlr);
        for(i=0; i<NWD; i++)
                if (hba[ctlr]->sizes[ i<<NWD_SHIFT ])
                        revalidate_logvol(dev+(i<<NWD_SHIFT), 2);

        hba[ctlr]->usage_count--;
        return 0;
}



/*
 *   Wait polling for a command to complete.
 *   The memory mapped FIFO is polled for the completion.
 *   Used only at init time, interrupts disabled.
 */
static unsigned long pollcomplete(int ctlr)
{
        unsigned long done;
        int i;

        /* Wait (up to 2 seconds) for a command to complete */

        for (i = 200000; i > 0; i--) {
                done = hba[ctlr]->access.command_completed(hba[ctlr]);
                if (done == FIFO_EMPTY) {
                        udelay(10);     /* a short fixed delay */
                } else
                        return (done);
        }
        /* Invalid address to tell caller we ran out of time */
        return 1;
}
/*
 * Send a command to the controller, and wait for it to complete.  
 * Only used at init time. 
 */
static int sendcmd(
	__u8	cmd,
	int	ctlr,
	void	*buff,
	size_t	size,
	unsigned int use_unit_num,
	unsigned int log_unit,
	__u8	page_code )
{
	CommandList_struct *c;
	int i;
	unsigned long complete;
	ctlr_info_t *info_p= hba[ctlr];
	u64bit buff_dma_handle;

	c = cmd_alloc(info_p, 1);
	if (c == NULL)
	{
		printk(KERN_WARNING "cciss: unable to get memory");
		return(IO_ERROR);
	}
	// Fill in Command Header 
	c->Header.ReplyQueue = 0;  // unused in simple mode
	if( buff != NULL) 	// buffer to fill 
	{
		c->Header.SGList = 1;
		c->Header.SGTotal= 1;
	} else	// no buffers to fill  
	{
		c->Header.SGList = 0;
                c->Header.SGTotal= 0;
	}
	c->Header.Tag.lower = c->busaddr;  // use the kernel address the cmd block for tag
	// Fill in Request block 	
	switch(cmd)
	{
		case  CISS_INQUIRY:
			/* If the logical unit number is 0 then, this is going
				to controller so It's a physical command
				mode = 0 target = 0.
				So we have nothing to write. 
				Otherwise 
				mode = 1  target = LUNID
			*/
			if(use_unit_num != 0)
			{
				c->Header.LUN.LogDev.VolId=
                                	hba[ctlr]->drv[log_unit].LunID;
                        	c->Header.LUN.LogDev.Mode = 1;
			}
			/* are we trying to read a vital product page */
			if(page_code != 0)
			{
				c->Request.CDB[1] = 0x01;
				c->Request.CDB[2] = page_code;
			}
			c->Request.CDBLen = 6;
			c->Request.Type.Type =  TYPE_CMD; // It is a command. 
			c->Request.Type.Attribute = ATTR_SIMPLE;  
			c->Request.Type.Direction = XFER_READ; // Read 
			c->Request.Timeout = 0; // Don't time out 
			c->Request.CDB[0] =  CISS_INQUIRY;
			c->Request.CDB[4] = size  & 0xFF;  
		break;
		case CISS_REPORT_LOG:
                        /* Talking to controller so It's a physical command
                                mode = 00 target = 0.
                                So we have nothing to write.
                        */
                        c->Request.CDBLen = 12;
                        c->Request.Type.Type =  TYPE_CMD; // It is a command.
                        c->Request.Type.Attribute = ATTR_SIMPLE; 
                        c->Request.Type.Direction = XFER_READ; // Read
                        c->Request.Timeout = 0; // Don't time out
                        c->Request.CDB[0] = CISS_REPORT_LOG;
                        c->Request.CDB[6] = (size >> 24) & 0xFF;  //MSB
                        c->Request.CDB[7] = (size >> 16) & 0xFF;
                        c->Request.CDB[8] = (size >> 8) & 0xFF;
                        c->Request.CDB[9] = size & 0xFF;
                break;

		case CCISS_READ_CAPACITY:
			c->Header.LUN.LogDev.VolId= 
				hba[ctlr]->drv[log_unit].LunID;
			c->Header.LUN.LogDev.Mode = 1;
			c->Request.CDBLen = 10;
                        c->Request.Type.Type =  TYPE_CMD; // It is a command.
                        c->Request.Type.Attribute = ATTR_SIMPLE; 
                        c->Request.Type.Direction = XFER_READ; // Read
                        c->Request.Timeout = 0; // Don't time out
                        c->Request.CDB[0] = CCISS_READ_CAPACITY;
		break;
		default:
			printk(KERN_WARNING
				"cciss:  Unknown Command 0x%c sent attempted\n",
				  cmd);
			cmd_free(info_p, c, 1);
			return(IO_ERROR);
	};
	// Fill in the scatter gather information
	if (size > 0 ) 
	{
		buff_dma_handle.val = (__u64) pci_map_single( info_p->pdev, 
			buff, size, PCI_DMA_BIDIRECTIONAL);
		c->SG[0].Addr.lower = buff_dma_handle.val32.lower;
		c->SG[0].Addr.upper = buff_dma_handle.val32.upper;
		c->SG[0].Len = size;
		c->SG[0].Ext = 0;  // we are not chaining
	}
	/*
         * Disable interrupt
         */
#ifdef CCISS_DEBUG
	printk(KERN_DEBUG "cciss: turning intr off\n");
#endif /* CCISS_DEBUG */ 
        info_p->access.set_intr_mask(info_p, CCISS_INTR_OFF);
	
	/* Make sure there is room in the command FIFO */
        /* Actually it should be completely empty at this time. */
        for (i = 200000; i > 0; i--) 
	{
		/* if fifo isn't full go */
                if (!(info_p->access.fifo_full(info_p))) 
		{
			
                        break;
                }
                udelay(10);
                printk(KERN_WARNING "cciss cciss%d: SendCmd FIFO full,"
                        " waiting!\n", ctlr);
        }
        /*
         * Send the cmd
         */
        info_p->access.submit_command(info_p, c);
        complete = pollcomplete(ctlr);

#ifdef CCISS_DEBUG
	printk(KERN_DEBUG "cciss: command completed\n");
#endif /* CCISS_DEBUG */

	/* unlock the data buffer from DMA */
	pci_unmap_single(info_p->pdev, (dma_addr_t) buff_dma_handle.val,
                                size, PCI_DMA_BIDIRECTIONAL);
	if (complete != 1) {
		if ( (complete & CISS_ERROR_BIT)
		     && (complete & ~CISS_ERROR_BIT) == c->busaddr)
		     {
			/* if data overrun or underun on Report command 
				ignore it 
			*/
			if (((c->Request.CDB[0] == CISS_REPORT_LOG) ||
			     (c->Request.CDB[0] == CISS_INQUIRY)) &&
				((c->err_info->CommandStatus == 
					CMD_DATA_OVERRUN) || 
				 (c->err_info->CommandStatus == 
					CMD_DATA_UNDERRUN)
			 	))
			{
				complete = c->busaddr;
			} else
			{
				printk(KERN_WARNING "ciss ciss%d: sendcmd"
				" Error %x \n", ctlr, 
					c->err_info->CommandStatus); 
				printk(KERN_WARNING "ciss ciss%d: sendcmd"
				" offensive info\n"
				"  size %x\n   num %x   value %x\n", ctlr,
				  c->err_info->MoreErrInfo.Invalid_Cmd.offense_size,
				  c->err_info->MoreErrInfo.Invalid_Cmd.offense_num,
				  c->err_info->MoreErrInfo.Invalid_Cmd.offense_value);
				cmd_free(info_p,c, 1);
				return(IO_ERROR);
			}
		}
                if (complete != c->busaddr) {
                        printk( KERN_WARNING "cciss cciss%d: SendCmd "
                      "Invalid command list address returned! (%lx)\n",
                                ctlr, complete);
                        cmd_free(info_p, c, 1);
                        return (IO_ERROR);
                }
        } else {
                printk( KERN_WARNING
                        "cciss cciss%d: SendCmd Timeout out, "
                        "No command list address returned!\n",
                        ctlr);
                cmd_free(info_p, c, 1);
                return (IO_ERROR);
        }
	cmd_free(info_p, c, 1);
        return (IO_OK);
} 
/*
 * Map (physical) PCI mem into (virtual) kernel space
 */
static ulong remap_pci_mem(ulong base, ulong size)
{
        ulong page_base        = ((ulong) base) & PAGE_MASK;
        ulong page_offs        = ((ulong) base) - page_base;
        ulong page_remapped    = (ulong) ioremap(page_base, page_offs+size);

        return (ulong) (page_remapped ? (page_remapped + page_offs) : 0UL);
}

/*
 * Enqueuing and dequeuing functions for cmdlists.
 */
static inline void addQ(CommandList_struct **Qptr, CommandList_struct *c)
{
        if (*Qptr == NULL) {
                *Qptr = c;
                c->next = c->prev = c;
        } else {
                c->prev = (*Qptr)->prev;
                c->next = (*Qptr);
                (*Qptr)->prev->next = c;
                (*Qptr)->prev = c;
        }
}

static inline CommandList_struct *removeQ(CommandList_struct **Qptr, 
						CommandList_struct *c)
{
        if (c && c->next != c) {
                if (*Qptr == c) *Qptr = c->next;
                c->prev->next = c->next;
                c->next->prev = c->prev;
        } else {
                *Qptr = NULL;
        }
        return c;
}

/* 
 * Takes jobs of the Q and sends them to the hardware, then puts it on 
 * the Q to wait for completion. 
 */ 
static void start_io( ctlr_info_t *h)
{
	CommandList_struct *c;
	
	while(( c = h->reqQ) != NULL )
	{
		/* can't do anything if fifo is full */
		if ((h->access.fifo_full(h)))
		{
			printk(KERN_WARNING "cciss: fifo full \n");
			return;
		}
		/* Get the frist entry from the Request Q */ 
		removeQ(&(h->reqQ), c);
		h->Qdepth--;
	
		/* Tell the controller execute command */ 
		h->access.submit_command(h, c);
		
		/* Put job onto the completed Q */ 
		addQ (&(h->cmpQ), c); 
	}
}

static inline void complete_buffers( struct buffer_head *bh, int status)
{
	struct buffer_head *xbh;
	
	while(bh)
	{
		xbh = bh->b_reqnext; 
		bh->b_reqnext = NULL; 
		blk_finished_io(bh->b_size >> 9);
		bh->b_end_io(bh, status);
		bh = xbh;
	}
} 
/* checks the status of the job and calls complete buffers to mark all 
 * buffers for the completed job. 
 */ 
static inline void complete_command( CommandList_struct *cmd, int timeout)
{
	int status = 1;
	int i;
	u64bit temp64;
		
	if (timeout)
		status = 0; 
	/* unmap the DMA mapping for all the scatter gather elements */
	for(i=0; i<cmd->Header.SGList; i++)
	{
		temp64.val32.lower = cmd->SG[i].Addr.lower;
		temp64.val32.upper = cmd->SG[i].Addr.upper;
		pci_unmap_single(hba[cmd->ctlr]->pdev,
			temp64.val, cmd->SG[i].Len, 
			(cmd->Request.Type.Direction == XFER_READ) ? 
				PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
	}

	if(cmd->err_info->CommandStatus != 0) 
	{ /* an error has occurred */ 
		switch(cmd->err_info->CommandStatus)
		{
			case CMD_TARGET_STATUS:
				printk(KERN_WARNING "cciss: cmd %p has "
					" completed with errors\n", cmd);
				if( cmd->err_info->ScsiStatus)
                		{
                    			printk(KERN_WARNING "cciss: cmd %p "
					"has SCSI Status = %x\n",
                        			cmd,  
						cmd->err_info->ScsiStatus);
                		}

			break;
			case CMD_DATA_UNDERRUN:
				printk(KERN_WARNING "cciss: cmd %p has"
					" completed with data underrun "
					"reported\n", cmd);
			break;
			case CMD_DATA_OVERRUN:
				printk(KERN_WARNING "cciss: cmd %p has"
					" completed with data overrun "
					"reported\n", cmd);
			break;
			case CMD_INVALID:
				printk(KERN_WARNING "cciss: cmd %p is "
					"reported invalid\n", cmd);
				status = 0;
			break;
			case CMD_PROTOCOL_ERR:
                                printk(KERN_WARNING "cciss: cmd %p has "
					"protocol error \n", cmd);
                                status = 0;
                        break;
			case CMD_HARDWARE_ERR:
                                printk(KERN_WARNING "cciss: cmd %p had " 
                                        " hardware error\n", cmd);
                                status = 0;
                        break;
			case CMD_CONNECTION_LOST:
				printk(KERN_WARNING "cciss: cmd %p had "
					"connection lost\n", cmd);
				status=0;
			break;
			case CMD_ABORTED:
				printk(KERN_WARNING "cciss: cmd %p was "
					"aborted\n", cmd);
				status=0;
			break;
			case CMD_ABORT_FAILED:
				printk(KERN_WARNING "cciss: cmd %p reports "
					"abort failed\n", cmd);
				status=0;
			break;
			case CMD_UNSOLICITED_ABORT:
				printk(KERN_WARNING "cciss: cmd %p aborted "
					"do to an unsolicited abort\n", cmd);
				status=0;
			break;
			case CMD_TIMEOUT:
				printk(KERN_WARNING "cciss: cmd %p timedout\n",
					cmd);
				status=0;
			break;
			default:
				printk(KERN_WARNING "cciss: cmd %p returned "
					"unknown status %x\n", cmd, 
						cmd->err_info->CommandStatus); 
				status=0;
		}
	}
	complete_buffers(cmd->bh, status);
}


static inline int cpq_new_segment(request_queue_t *q, struct request *rq,
                                  int max_segments)
{
        if (rq->nr_segments < MAXSGENTRIES) {
                rq->nr_segments++;
                return 1;
        }
        return 0;
}

static int cpq_back_merge_fn(request_queue_t *q, struct request *rq,
                             struct buffer_head *bh, int max_segments)
{
        if (rq->bhtail->b_data + rq->bhtail->b_size == bh->b_data)
                return 1;
        return cpq_new_segment(q, rq, max_segments);
}

static int cpq_front_merge_fn(request_queue_t *q, struct request *rq,
                             struct buffer_head *bh, int max_segments)
{
        if (bh->b_data + bh->b_size == rq->bh->b_data)
                return 1;
        return cpq_new_segment(q, rq, max_segments);
}

static int cpq_merge_requests_fn(request_queue_t *q, struct request *rq,
                                 struct request *nxt, int max_segments)
{
        int total_segments = rq->nr_segments + nxt->nr_segments;

        if (rq->bhtail->b_data + rq->bhtail->b_size == nxt->bh->b_data)
                total_segments--;

        if (total_segments > MAXSGENTRIES)
                return 0;

        rq->nr_segments = total_segments;
        return 1;
}

/* 
 * Get a request and submit it to the controller. 
 * Currently we do one request at a time.  Ideally we would like to send
 * everything to the controller on the first call, but there is a danger
 * of holding the io_request_lock for to long.  
 */
static void do_cciss_request(request_queue_t *q)
{
	ctlr_info_t *h= q->queuedata; 
	CommandList_struct *c;
	int log_unit, start_blk, seg, sect;
	char *lastdataend;
	struct buffer_head *bh;
	struct list_head *queue_head = &q->queue_head;
	struct request *creq;
	u64bit temp64;
	struct my_sg tmp_sg[MAXSGENTRIES];
	int i;

    // Loop till the queue is empty if or it is plugged
    while (1)
    {
	if (q->plugged || list_empty(queue_head)) {
                start_io(h);
                return;
        }

	creq =	blkdev_entry_next_request(queue_head); 
	if (creq->nr_segments > MAXSGENTRIES)
                BUG();

        if (h->ctlr != MAJOR(creq->rq_dev)-MAJOR_NR )
        {
                printk(KERN_WARNING "doreq cmd for %d, %x at %p\n",
                                h->ctlr, creq->rq_dev, creq);
                blkdev_dequeue_request(creq);
                complete_buffers(creq->bh, 0);
                start_io(h);
                return;
        }

	if (( c = cmd_alloc(h, 1)) == NULL)
	{
		start_io(h);
		return;
	}
	c->cmd_type = CMD_RWREQ;      
	bh = c->bh = creq->bh;
	
	/* fill in the request */ 
	log_unit = MINOR(creq->rq_dev) >> NWD_SHIFT; 
	c->Header.ReplyQueue = 0;  // unused in simple mode
	c->Header.Tag.lower = c->busaddr;  // use the physical address the cmd block for tag
	c->Header.LUN.LogDev.VolId= hba[h->ctlr]->drv[log_unit].LunID;
	c->Header.LUN.LogDev.Mode = 1;
	c->Request.CDBLen = 10; // 12 byte commands not in FW yet;
	c->Request.Type.Type =  TYPE_CMD; // It is a command. 
	c->Request.Type.Attribute = ATTR_SIMPLE; 
	c->Request.Type.Direction = 
		(creq->cmd == READ) ? XFER_READ: XFER_WRITE; 
	c->Request.Timeout = 0; // Don't time out	
	c->Request.CDB[0] = (creq->cmd == READ) ? CCISS_READ : CCISS_WRITE;
	start_blk = hba[h->ctlr]->hd[MINOR(creq->rq_dev)].start_sect + creq->sector;
#ifdef CCISS_DEBUG
	if (bh == NULL)
		panic("cciss: bh== NULL?");
	printk(KERN_DEBUG "ciss: sector =%d nr_sectors=%d\n",(int) creq->sector,
		(int) creq->nr_sectors);	
#endif /* CCISS_DEBUG */
	seg = 0; 
	lastdataend = NULL;
	sect = 0;
	while(bh)
	{
		sect += bh->b_size/512;
		if (bh->b_data == lastdataend)
		{  // tack it on to the last segment 
			tmp_sg[seg-1].len +=bh->b_size;
			lastdataend += bh->b_size;
		} else
		{
			if (seg == MAXSGENTRIES)
				BUG();
			tmp_sg[seg].len = bh->b_size;
			tmp_sg[seg].start_addr = bh->b_data;
			lastdataend = bh->b_data + bh->b_size;
			seg++;
		}
		bh = bh->b_reqnext;
	}
	/* get the DMA records for the setup */ 
	for (i=0; i<seg; i++)
	{
		c->SG[i].Len = tmp_sg[i].len;
		temp64.val = (__u64) pci_map_single( h->pdev,
			tmp_sg[i].start_addr,
			tmp_sg[i].len,
			(c->Request.Type.Direction == XFER_READ) ? 
                                PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
		c->SG[i].Addr.lower = temp64.val32.lower;
                c->SG[i].Addr.upper = temp64.val32.upper;
                c->SG[i].Ext = 0;  // we are not chaining
	}
	/* track how many SG entries we are using */ 
	if( seg > h->maxSG)
		h->maxSG = seg; 

#ifdef CCISS_DEBUG
	printk(KERN_DEBUG "cciss: Submitting %d sectors in %d segments\n", sect, seg);
#endif /* CCISS_DEBUG */

	c->Header.SGList = c->Header.SGTotal = seg;
	c->Request.CDB[1]= 0;
	c->Request.CDB[2]= (start_blk >> 24) & 0xff;	//MSB
	c->Request.CDB[3]= (start_blk >> 16) & 0xff;
	c->Request.CDB[4]= (start_blk >>  8) & 0xff;
	c->Request.CDB[5]= start_blk & 0xff;
	c->Request.CDB[6]= 0; // (sect >> 24) & 0xff; MSB
	c->Request.CDB[7]= (sect >>  8) & 0xff; 
	c->Request.CDB[8]= sect & 0xff; 
	c->Request.CDB[9] = c->Request.CDB[11] = c->Request.CDB[12] = 0;

			
	blkdev_dequeue_request(creq);

	
        /*
         * ehh, we can't really end the request here since it's not
         * even started yet. for now it shouldn't hurt though
         */
#ifdef CCISS_DEBUG
	printk("Done with %p\n", creq);
#endif /* CCISS_DEBUG */ 
	end_that_request_last(creq);

	addQ(&(h->reqQ),c);
	h->Qdepth++;
	if(h->Qdepth > h->maxQsinceinit)
		h->maxQsinceinit = h->Qdepth; 
   }  // while loop
}

static void do_cciss_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	ctlr_info_t *h = dev_id;
	CommandList_struct *c;
	unsigned long flags;
	__u32 a, a1;


	/* Is this interrupt for us? */
	if ( h->access.intr_pending(h) == 0)
		return;

	/*
	 * If there are completed commands in the completion queue,
	 * we had better do something about it.
	 */
	spin_lock_irqsave(&io_request_lock, flags);
	while( h->access.intr_pending(h))
	{
		while((a = h->access.command_completed(h)) != FIFO_EMPTY) 
		{
			a1 = a;
			a &= ~3;
			if ((c = h->cmpQ) == NULL)
			{  
				printk(KERN_WARNING "cciss: Completion of %08lx ignored\n", (unsigned long)a1);
				continue;	
			} 
			while(c->busaddr != a) {
				c = c->next;
				if (c == h->cmpQ) 
					break;
			}
			/*
			 * If we've found the command, take it off the
			 * completion Q and free it
			 */
			 if (c->busaddr == a) {
				removeQ(&h->cmpQ, c);
				if (c->cmd_type == CMD_RWREQ) {
					complete_command(c, 0);
					cmd_free(h, c, 1);
				} else if (c->cmd_type == CMD_IOCTL_PEND) {
					c->cmd_type = CMD_IOCTL_DONE;
				}
				continue;
			}
		}
	}
	/*
	 * See if we can queue up some more IO
	 */
	do_cciss_request(BLK_DEFAULT_QUEUE(MAJOR_NR + h->ctlr));
	spin_unlock_irqrestore(&io_request_lock, flags);
}
/* 
 *  We cannot read the structure directly, for portablity we must use 
 *   the io functions.
 *   This is for debug only. 
 */
#ifdef CCISS_DEBUG
static void print_cfg_table( CfgTable_struct *tb)
{
	int i;
	char temp_name[17];

	printk("Controller Configuration information\n");
	printk("------------------------------------\n");
	for(i=0;i<4;i++)
		temp_name[i] = readb(&(tb->Signature[i]));
	temp_name[4]='\0';
	printk("   Signature = %s\n", temp_name); 
	printk("   Spec Number = %d\n", readl(&(tb->SpecValence)));
	printk("   Transport methods supported = 0x%x\n", 
				readl(&(tb-> TransportSupport)));
	printk("   Transport methods active = 0x%x\n", 
				readl(&(tb->TransportActive)));
	printk("   Requested transport Method = 0x%x\n", 
			readl(&(tb->HostWrite.TransportRequest)));
	printk("   Coalese Interrupt Delay = 0x%x\n", 
			readl(&(tb->HostWrite.CoalIntDelay)));
	printk("   Coalese Interrupt Count = 0x%x\n", 
			readl(&(tb->HostWrite.CoalIntCount)));
	printk("   Max outstanding commands = 0x%d\n", 
			readl(&(tb->CmdsOutMax)));
	printk("   Bus Types = 0x%x\n", readl(&(tb-> BusTypes)));
	for(i=0;i<16;i++)
		temp_name[i] = readb(&(tb->ServerName[i]));
	temp_name[16] = '\0';
	printk("   Server Name = %s\n", temp_name);
	printk("   Heartbeat Counter = 0x%x\n\n\n", 
			readl(&(tb->HeartBeat)));
}
#endif /* CCISS_DEBUG */ 

static int cciss_pci_init(ctlr_info_t *c, struct pci_dev *pdev)
{
	ushort vendor_id, device_id, command;
	unchar cache_line_size, latency_timer;
	unchar irq, revision;
	uint addr[6];
	__u32 board_id;
	int cfg_offset;
	int cfg_base_addr;
	int cfg_base_addr_index;
	int i;

	vendor_id = pdev->vendor;
	device_id = pdev->device;
	irq = pdev->irq;

	for(i=0; i<6; i++)
		addr[i] = pdev->resource[i].start;

	if (pci_enable_device(pdev))
	{
		printk(KERN_ERR "cciss: Unable to Enable PCI device\n");
		return( -1);
	}
	if (pci_set_dma_mask(pdev, CCISS_DMA_MASK ) != 0)
	{
		printk(KERN_ERR "cciss:  Unable to set DMA mask\n");
		return(-1);
	}
	
	(void) pci_read_config_word(pdev, PCI_COMMAND,&command);
	(void) pci_read_config_byte(pdev, PCI_CLASS_REVISION, &revision);
	(void) pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE,
						&cache_line_size);
	(void) pci_read_config_byte(pdev, PCI_LATENCY_TIMER,
						&latency_timer);

	(void) pci_read_config_dword(pdev, PCI_SUBSYSTEM_VENDOR_ID, 
						&board_id);

#ifdef CCISS_DEBUG
	printk("vendor_id = %x\n", vendor_id);
	printk("device_id = %x\n", device_id);
	printk("command = %x\n", command);
	for(i=0; i<6; i++)
		printk("addr[%d] = %x\n", i, addr[i]);
	printk("revision = %x\n", revision);
	printk("irq = %x\n", irq);
	printk("cache_line_size = %x\n", cache_line_size);
	printk("latency_timer = %x\n", latency_timer);
	printk("board_id = %x\n", board_id);
#endif /* CCISS_DEBUG */ 

	c->intr = irq;

	/*
	 * Memory base addr is first addr , the second points to the config
         *   table
	 */

	c->paddr = addr[0] & 0xfffffff0; /* remove the addressing mode bits */
#ifdef CCISS_DEBUG
	printk("address 0 = %x\n", c->paddr);
#endif /* CCISS_DEBUG */ 
	c->vaddr = remap_pci_mem(c->paddr, 200);

	/* get the address index number */
	cfg_base_addr = readl(c->vaddr + SA5_CTCFG_OFFSET);
	/* I am not prepared to deal with a 64 bit address value */
	cfg_base_addr &= 0xffff;
#ifdef CCISS_DEBUG
	printk("cfg base address = %x\n", cfg_base_addr);
#endif /* CCISS_DEBUG */
	cfg_base_addr_index = (cfg_base_addr  - PCI_BASE_ADDRESS_0)/4;
#ifdef CCISS_DEBUG
	printk("cfg base address index = %x\n", cfg_base_addr_index);
#endif /* CCISS_DEBUG */

	cfg_offset = readl(c->vaddr + SA5_CTMEM_OFFSET);
#ifdef CCISS_DEBUG
	printk("cfg offset = %x\n", cfg_offset);
#endif /* CCISS_DEBUG */
	c->cfgtable = (CfgTable_struct *) 
		remap_pci_mem((addr[cfg_base_addr_index] & 0xfffffff0)
				+ cfg_offset, sizeof(CfgTable_struct));
	c->board_id = board_id;

#ifdef CCISS_DEBUG
	print_cfg_table(c->cfgtable); 
#endif /* CCISS_DEBUG */

	for(i=0; i<NR_PRODUCTS; i++) {
		if (board_id == products[i].board_id) {
			c->product_name = products[i].product_name;
			c->access = *(products[i].access);
			break;
		}
	}
	if (i == NR_PRODUCTS) {
		printk(KERN_WARNING "cciss: Sorry, I don't know how"
			" to access the Smart Array controller %08lx\n", 
				(unsigned long)board_id);
		return -1;
	}
	if (  (readb(&c->cfgtable->Signature[0]) != 'C') ||
	      (readb(&c->cfgtable->Signature[1]) != 'I') ||
	      (readb(&c->cfgtable->Signature[2]) != 'S') ||
	      (readb(&c->cfgtable->Signature[3]) != 'S') )
	{
		printk("Does not appear to be a valid CISS config table\n");
		return -1;
	}
#ifdef CCISS_DEBUG
	printk("Trying to put board into Simple mode\n");
#endif /* CCISS_DEBUG */ 
	c->max_commands = readl(&(c->cfgtable->CmdsOutMax));
	/* Update the field, and then ring the doorbell */ 
	writel( CFGTBL_Trans_Simple, 
		&(c->cfgtable->HostWrite.TransportRequest));
	writel( CFGTBL_ChangeReq, c->vaddr + SA5_DOORBELL);

	for(i=0;i<MAX_CONFIG_WAIT;i++)
	{
		if (!(readl(c->vaddr + SA5_DOORBELL) & CFGTBL_ChangeReq))
			break;
		/* delay and try again */
		udelay(1000);
	}	

#ifdef CCISS_DEBUG
	printk(KERN_DEBUG "I counter got to %d %x\n", i, readl(c->vaddr + SA5_DOORBELL));
#endif /* CCISS_DEBUG */
#ifdef CCISS_DEBUG
	print_cfg_table(c->cfgtable);	
#endif /* CCISS_DEBUG */ 

	if (!(readl(&(c->cfgtable->TransportActive)) & CFGTBL_Trans_Simple))
	{
		printk(KERN_WARNING "cciss: unable to get board into"
					" simple mode\n");
		return -1;
	}
	return 0;

}

/* 
 * Gets information about the local volumes attached to the controller. 
 */ 
static void cciss_getgeometry(int cntl_num)
{
	ReportLunData_struct *ld_buff;
	ReadCapdata_struct *size_buff;
	InquiryData_struct *inq_buff;
	int return_code;
	int i;
	int listlength = 0;
	int lunid = 0;
	int block_size;
	int total_size; 

	ld_buff = kmalloc(sizeof(ReportLunData_struct), GFP_KERNEL);
	if (ld_buff == NULL)
	{
		printk(KERN_ERR "cciss: out of memory\n");
		return;
	}
	memset(ld_buff, 0, sizeof(ReportLunData_struct));
	size_buff = kmalloc(sizeof( ReadCapdata_struct), GFP_KERNEL);
        if (size_buff == NULL)
        {
                printk(KERN_ERR "cciss: out of memory\n");
		kfree(ld_buff);
                return;
        }
	inq_buff = kmalloc(sizeof( InquiryData_struct), GFP_KERNEL);
        if (inq_buff == NULL)
        {
                printk(KERN_ERR "cciss: out of memory\n");
                kfree(ld_buff);
		kfree(size_buff);
                return;
        }
	/* Get the firmware version */ 
	return_code = sendcmd(CISS_INQUIRY, cntl_num, inq_buff, 
		sizeof(InquiryData_struct), 0, 0 ,0 );
	if (return_code == IO_OK)
	{
		hba[cntl_num]->firm_ver[0] = inq_buff->data_byte[32];
		hba[cntl_num]->firm_ver[1] = inq_buff->data_byte[33];
		hba[cntl_num]->firm_ver[2] = inq_buff->data_byte[34];
		hba[cntl_num]->firm_ver[3] = inq_buff->data_byte[35];
	} else /* send command failed */
	{
		printk(KERN_WARNING "cciss: unable to determine firmware"
			" version of controller\n");
	}
	/* Get the number of logical volumes */ 
	return_code = sendcmd(CISS_REPORT_LOG, cntl_num, ld_buff, 
			sizeof(ReportLunData_struct), 0, 0, 0 );

	if( return_code == IO_OK)
	{
#ifdef CCISS_DEBUG
		printk("LUN Data\n--------------------------\n");
#endif /* CCISS_DEBUG */ 

		listlength |= (0xff & (unsigned int)(ld_buff->LUNListLength[0])) << 24;
		listlength |= (0xff & (unsigned int)(ld_buff->LUNListLength[1])) << 16;
		listlength |= (0xff & (unsigned int)(ld_buff->LUNListLength[2])) << 8;	
		listlength |= 0xff & (unsigned int)(ld_buff->LUNListLength[3]);
	} else /* reading number of logical volumes failed */
	{
		printk(KERN_WARNING "cciss: report logical volume"
			" command failed\n");
		listlength = 0;
	}
	hba[cntl_num]->num_luns = listlength / 8; // 8 bytes pre entry
	if (hba[cntl_num]->num_luns > CISS_MAX_LUN)
	{
		printk(KERN_ERR "ciss:  only %d number of logical volumes supported\n",
			CISS_MAX_LUN);
		hba[cntl_num]->num_luns = CISS_MAX_LUN;
	}
#ifdef CCISS_DEBUG
	printk(KERN_DEBUG "Length = %x %x %x %x = %d\n", ld_buff->LUNListLength[0],
		ld_buff->LUNListLength[1], ld_buff->LUNListLength[2],
		ld_buff->LUNListLength[3],  hba[cntl_num]->num_luns);
#endif /* CCISS_DEBUG */
	for(i=0; i<  hba[cntl_num]->num_luns ; i++)
	{
	  	lunid = (0xff & (unsigned int)(ld_buff->LUN[i][3])) << 24;
        	lunid |= (0xff & (unsigned int)(ld_buff->LUN[i][2])) << 16;
        	lunid |= (0xff & (unsigned int)(ld_buff->LUN[i][1])) << 8;
        	lunid |= 0xff & (unsigned int)(ld_buff->LUN[i][0]);
		hba[cntl_num]->drv[i].LunID = lunid;

#ifdef CCISS_DEBUG
	  	printk(KERN_DEBUG "LUN[%d]:  %x %x %x %x = %x\n", i, 
		ld_buff->LUN[i][0], ld_buff->LUN[i][1],ld_buff->LUN[i][2], 
		ld_buff->LUN[i][3], hba[cntl_num]->drv[i].LunID);
#endif /* CCISS_DEBUG */

	  	memset(size_buff, 0, sizeof(ReadCapdata_struct));
	  	return_code = sendcmd(CCISS_READ_CAPACITY, cntl_num, size_buff, 
				sizeof( ReadCapdata_struct), 1, i, 0 );
	  	if (return_code == IO_OK)
		{
			total_size = (0xff & 
				(unsigned int)(size_buff->total_size[0])) << 24;
			total_size |= (0xff & 
				(unsigned int)(size_buff->total_size[1])) << 16;
			total_size |= (0xff & 
				(unsigned int)(size_buff->total_size[2])) << 8;
			total_size |= (0xff & (unsigned int)
				(size_buff->total_size[3])); 
			total_size++; // command returns highest block address

			block_size = (0xff & 
				(unsigned int)(size_buff->block_size[0])) << 24;
                	block_size |= (0xff & 
				(unsigned int)(size_buff->block_size[1])) << 16;
                	block_size |= (0xff & 
				(unsigned int)(size_buff->block_size[2])) << 8;
                	block_size |= (0xff & 
				(unsigned int)(size_buff->block_size[3]));
		} else /* read capacity command failed */ 
		{
			printk(KERN_WARNING "cciss: read capacity failed\n");
			total_size = block_size = 0; 
		}	
		printk(KERN_INFO "      blocks= %d block_size= %d\n", 
					total_size, block_size);

		/* Execute the command to read the disk geometry */
		memset(inq_buff, 0, sizeof(InquiryData_struct));
		return_code = sendcmd(CISS_INQUIRY, cntl_num, inq_buff,
                	sizeof(InquiryData_struct), 1, i ,0xC1 );
	  	if (return_code == IO_OK)
		{
			if(inq_buff->data_byte[8] == 0xFF)
			{
			   printk(KERN_WARNING "cciss: reading geometry failed, volume does not support reading geometry\n");

                           hba[cntl_num]->drv[i].block_size = block_size;
                           hba[cntl_num]->drv[i].nr_blocks = total_size;
                           hba[cntl_num]->drv[i].heads = 255;
                           hba[cntl_num]->drv[i].sectors = 32; // Sectors per track
                           hba[cntl_num]->drv[i].cylinders = total_size / 255 / 32;                	} else
			{

		 	   hba[cntl_num]->drv[i].block_size = block_size;
                           hba[cntl_num]->drv[i].nr_blocks = total_size;
                           hba[cntl_num]->drv[i].heads = 
					inq_buff->data_byte[6]; 
                           hba[cntl_num]->drv[i].sectors = 
					inq_buff->data_byte[7]; 
			   hba[cntl_num]->drv[i].cylinders = 
					(inq_buff->data_byte[4] & 0xff) << 8;
			   hba[cntl_num]->drv[i].cylinders += 
                                        inq_buff->data_byte[5];
			}
		}
		else /* Get geometry failed */
		{
			printk(KERN_WARNING "cciss: reading geometry failed, continuing with default geometry\n"); 

			hba[cntl_num]->drv[i].block_size = block_size;
			hba[cntl_num]->drv[i].nr_blocks = total_size;
			hba[cntl_num]->drv[i].heads = 255;
			hba[cntl_num]->drv[i].sectors = 32; // Sectors per track 
			hba[cntl_num]->drv[i].cylinders = total_size / 255 / 32;
		}
		printk(KERN_INFO "      heads= %d, sectors= %d, cylinders= %d\n\n",
			hba[cntl_num]->drv[i].heads, 
			hba[cntl_num]->drv[i].sectors,
			hba[cntl_num]->drv[i].cylinders);

	}
	kfree(ld_buff);
	kfree(size_buff);
}	

/* Function to find the first free pointer into our hba[] array */
/* Returns -1 if no free entries are left.  */
static int alloc_cciss_hba(void)
{
	int i;
	for(i=0; i< MAX_CTLR; i++)
	{
		if (hba[i] == NULL)
		{
			hba[i] = kmalloc(sizeof(ctlr_info_t), GFP_KERNEL);
			if(hba[i]==NULL)
			{
				printk(KERN_ERR "cciss: out of memory.\n");
				return (-1);
			}
			return (i);
		}
	}
	printk(KERN_WARNING "cciss: This driver supports a maximum"
		" of 8 controllers.\n");
	return(-1);
}

static void free_hba(int i)
{
	kfree(hba[i]);
	hba[i]=NULL;
}

/*
 *  This is it.  Find all the controllers and register them.  I really hate
 *  stealing all these major device numbers.
 *  returns the number of block devices registered.
 */
static int __init cciss_init_one(struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	request_queue_t *q;
	int i;
	int j;

	printk(KERN_DEBUG "cciss: Device 0x%x has been found at"
			" bus %d dev %d func %d\n",
		pdev->device, pdev->bus->number, PCI_SLOT(pdev->devfn),
			PCI_FUNC(pdev->devfn));
	i = alloc_cciss_hba();
	if( i < 0 ) 
		return (-1);
	memset(hba[i], 0, sizeof(ctlr_info_t));
	if (cciss_pci_init(hba[i], pdev) != 0)
	{
		free_hba(i);
		return (-1);
	}
	sprintf(hba[i]->devname, "cciss%d", i);
	hba[i]->ctlr = i;
	hba[i]->pdev = pdev;
			
	if( register_blkdev(MAJOR_NR+i, hba[i]->devname, &cciss_fops))
	{
		printk(KERN_ERR "cciss:  Unable to get major number "
			"%d for %s\n", MAJOR_NR+i, hba[i]->devname);
		free_hba(i);
		return(-1);
	}
	/* make sure the board interrupts are off */
	hba[i]->access.set_intr_mask(hba[i], CCISS_INTR_OFF);
	if( request_irq(hba[i]->intr, do_cciss_intr, 
		SA_INTERRUPT|SA_SHIRQ, hba[i]->devname, hba[i]))
	{
		printk(KERN_ERR "ciss: Unable to get irq %d for %s\n",
			hba[i]->intr, hba[i]->devname);
		unregister_blkdev( MAJOR_NR+i, hba[i]->devname);
		free_hba(i);
		return(-1);
	}
	hba[i]->cmd_pool_bits = (__u32*)kmalloc(
        	((NR_CMDS+31)/32)*sizeof(__u32), GFP_KERNEL);
	hba[i]->cmd_pool = (CommandList_struct *)pci_alloc_consistent(
		hba[i]->pdev, NR_CMDS * sizeof(CommandList_struct), 
		&(hba[i]->cmd_pool_dhandle));
	hba[i]->errinfo_pool = (ErrorInfo_struct *)pci_alloc_consistent(
		hba[i]->pdev, NR_CMDS * sizeof( ErrorInfo_struct), 
		&(hba[i]->errinfo_pool_dhandle));
	if((hba[i]->cmd_pool_bits == NULL) 
		|| (hba[i]->cmd_pool == NULL)
		|| (hba[i]->errinfo_pool == NULL))
        {
		if(hba[i]->cmd_pool_bits)
                	kfree(hba[i]->cmd_pool_bits);
                if(hba[i]->cmd_pool)
                	pci_free_consistent(hba[i]->pdev,  
				NR_CMDS * sizeof(CommandList_struct), 
				hba[i]->cmd_pool, hba[i]->cmd_pool_dhandle);	
		if(hba[i]->errinfo_pool)
			pci_free_consistent(hba[i]->pdev,
				NR_CMDS * sizeof( ErrorInfo_struct),
				hba[i]->errinfo_pool, 
				hba[i]->errinfo_pool_dhandle);
                free_irq(hba[i]->intr, hba[i]);
                unregister_blkdev(MAJOR_NR+i, hba[i]->devname);
		free_hba(i);
                printk( KERN_ERR "cciss: out of memory");
		return(-1);
	}

	/* Initialize the pdev driver private data. 
		have it point to hba[i].  */
	pdev->driver_data = hba[i];
	/* command and error info recs zeroed out before 
			they are used */
        memset(hba[i]->cmd_pool_bits, 0, ((NR_CMDS+31)/32)*sizeof(__u32));

#ifdef CCISS_DEBUG	
	printk(KERN_DEBUG "Scanning for drives on controller cciss%d\n",i);
#endif /* CCISS_DEBUG */

	cciss_getgeometry(i);

	/* Turn the interrupts on so we can service requests */
	hba[i]->access.set_intr_mask(hba[i], CCISS_INTR_ON);

	cciss_procinit(i);

	q = BLK_DEFAULT_QUEUE(MAJOR_NR + i);
        q->queuedata = hba[i];
        blk_init_queue(q, do_cciss_request);
        blk_queue_headactive(q, 0);		

	/* fill in the other Kernel structs */
	blksize_size[MAJOR_NR+i] = hba[i]->blocksizes;
        hardsect_size[MAJOR_NR+i] = hba[i]->hardsizes;
        read_ahead[MAJOR_NR+i] = READ_AHEAD;

	/* Set the pointers to queue functions */ 
	q->back_merge_fn = cpq_back_merge_fn;
        q->front_merge_fn = cpq_front_merge_fn;
        q->merge_requests_fn = cpq_merge_requests_fn;


	/* Fill in the gendisk data */ 	
	hba[i]->gendisk.major = MAJOR_NR + i;
	hba[i]->gendisk.major_name = "cciss";
	hba[i]->gendisk.minor_shift = NWD_SHIFT;
	hba[i]->gendisk.max_p = MAX_PART;
	hba[i]->gendisk.part = hba[i]->hd;
	hba[i]->gendisk.sizes = hba[i]->sizes;
	hba[i]->gendisk.nr_real = hba[i]->num_luns;

	/* Get on the disk list */ 
	add_gendisk(&(hba[i]->gendisk));

	cciss_geninit(i);
	for(j=0; j<NWD; j++)
		register_disk(&(hba[i]->gendisk),
			MKDEV(MAJOR_NR+i, j <<4), 
			MAX_PART, &cciss_fops, 
			hba[i]->drv[j].nr_blocks);

	return(1);
}

static void __devexit cciss_remove_one (struct pci_dev *pdev)
{
	ctlr_info_t *tmp_ptr;
	int i;

	if (pdev->driver_data == NULL)
	{
		printk( KERN_ERR "cciss: Unable to remove device \n");
		return;
	}
	tmp_ptr = (ctlr_info_t *) pdev->driver_data;
	i = tmp_ptr->ctlr;
	if (hba[i] == NULL) 
	{
		printk(KERN_ERR "cciss: device appears to "
			"already be removed \n");
		return;
	}
	/* Turn board interrupts off */
	hba[i]->access.set_intr_mask(hba[i], CCISS_INTR_OFF);
	free_irq(hba[i]->intr, hba[i]);
	pdev->driver_data = NULL;
	iounmap((void*)hba[i]->vaddr);
	unregister_blkdev(MAJOR_NR+i, hba[i]->devname);
	remove_proc_entry(hba[i]->devname, proc_cciss);	
	

	/* remove it from the disk list */
	del_gendisk(&(hba[i]->gendisk));

	pci_free_consistent(hba[i]->pdev, NR_CMDS * sizeof(CommandList_struct), 
		hba[i]->cmd_pool, hba[i]->cmd_pool_dhandle);
	pci_free_consistent(hba[i]->pdev, NR_CMDS * sizeof( ErrorInfo_struct),
		hba[i]->errinfo_pool, hba[i]->errinfo_pool_dhandle);
	kfree(hba[i]->cmd_pool_bits);
	free_hba(i);
}	

static struct pci_driver cciss_pci_driver = {
	 name:   "cciss",
	probe:  cciss_init_one,
	remove:  cciss_remove_one,
	id_table:  cciss_pci_device_id, /* id_table */
};

/*
*  This is it.  Register the PCI driver information for the cards we control
*  the OS will call our registered routines when it finds one of our cards. 
*/
int __init cciss_init(void)
{

	printk(KERN_INFO DRIVER_NAME "\n");
	/* Register for out PCI devices */
	if (pci_register_driver(&cciss_pci_driver) > 0 )
		return 0;
	else 
		return -ENODEV;

 }

EXPORT_NO_SYMBOLS;
static int __init init_cciss_module(void)
{

	return ( cciss_init());
}

static void __exit cleanup_cciss_module(void)
{
	int i;

	pci_unregister_driver(&cciss_pci_driver);
	/* double check that all controller entrys have been removed */
	for (i=0; i< MAX_CTLR; i++) 
	{
		if (hba[i] != NULL)
		{
			printk(KERN_WARNING "cciss: had to remove"
					" controller %d\n", i);
			cciss_remove_one(hba[i]->pdev);
		}
	}
	remove_proc_entry("cciss", proc_root_driver);
}

module_init(init_cciss_module);
module_exit(cleanup_cciss_module);

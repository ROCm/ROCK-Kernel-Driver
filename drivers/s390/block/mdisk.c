/*
 *  drivers/s390/block/mdisk.c
 *    VM minidisk device driver.
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 */


#ifndef __KERNEL__
#  define __KERNEL__
#endif

#define __NO_VERSION__
#include <linux/config.h>
#include <linux/version.h>

char kernel_version [] = UTS_RELEASE;

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>              /* printk()                         */
#include <linux/malloc.h>              /* kmalloc()                        */
#include <linux/vmalloc.h>             /* vmalloc()                        */
#include <linux/fs.h>                  /* everything...                    */
#include <linux/errno.h>               /* error codes                      */
#include <linux/timer.h>
#include <linux/types.h>               /* size_t                           */
#include <linux/fcntl.h>               /* O_ACCMODE                        */
#include <linux/hdreg.h>               /* HDIO_GETGEO                      */
#include <linux/init.h>                /* initfunc                         */
#include <linux/interrupt.h>
#include <linux/ctype.h>

#include <asm/system.h>                /* cli(), *_flags                   */
#include <asm/uaccess.h>               /* access_ok                        */
#include <asm/io.h>                    /* virt_to_phys                     */

	 /* Added statement HSM 12/03/99 */
#include <asm/irq.h>

#define MAJOR_NR MDISK_MAJOR /* force definitions on in blk.h */

#include <linux/blk.h>


#include "mdisk.h"        /* local definitions */

/*
 * structure for all device specific information
 */

typedef struct mdisk_Dev {
	u32 vdev;   /* vdev of mindisk */
	u32 size;   /* size in blocks */
	u32 status; /* status of last io operation */
	u32 nr_bhs; /* number of buffer of last io operation */
        u32 blksize; /* blksize from minidisk */
        u32 blkmult; /* multiplier between blksize and 512 HARDSECT */
        u32 blkshift; /* loe2 of multiplier above */
	/*
	 * each device has own iob and bio,
	 * it's possible to run io in parallel
	 * not used yet due to only one CURRENT per MAJOR
	 */
	
	mdisk_rw_io_t* iob;      /* each device has it own iob and bio */
	mdisk_bio_t*   bio;
	 /* Added statement HSM 12/03/99 */
	devstat_t dev_status;    /* Here we hold the I/O status */

	int usage;               /* usage counter */

	struct tq_struct tqueue; /* per device task queue */
} mdisk_Dev;


/*
 * appended to global structures in mdisk_init;
 */

static int mdisk_blksizes[MDISK_DEVS];
static int mdisk_sizes[MDISK_DEVS] = { 0 };
static int mdisk_hardsects[MDISK_DEVS];
static int mdisk_maxsectors[MDISK_DEVS];

/*
 *  structure hold device specific information
 */

static mdisk_Dev mdisk_devices[MDISK_DEVS];
static mdisk_rw_io_t mdisk_iob[MDISK_DEVS] __attribute__ ((aligned(8)));
static mdisk_bio_t mdisk_bio[MDISK_DEVS][256]__attribute__ ((aligned(8)));


/*
 * Parameter parsing
 */
struct {
        long vdev[MDISK_DEVS];
	long size[MDISK_DEVS];
	long offset[MDISK_DEVS];
        long blksize[MDISK_DEVS];
} mdisk_setup_data;

/*
 * Parameter parsing function, called from init/main.c
 * vdev    : virtual device number
 * size    : size in kbyte
 * offset  : offset after which minidisk is available
 * blksize : blocksize minidisk is formated
 * Format is: mdisk=<vdev>:<size>:<offset>:<blksize>,<vdev>:<size>:<offset>...
 * <vdev>:<size>:<offset>:<blksize> can be shortened to <vdev>:<size> with offset=0,blksize=512
 */
int __init mdisk_setup(char *str)
{
	char *cur = str;
	int vdev, size, offset=0,blksize;
	static int i = 0;
	if (!i)
	        memset(&mdisk_setup_data,0,sizeof(mdisk_setup_data));

        while (*cur != 0) {
	        blksize=MDISK_HARDSECT;
		vdev = size = offset = 0;
		if (!isxdigit(*cur)) goto syntax_error;
		vdev = simple_strtoul(cur,&cur,16);
		if (*cur != 0 && *cur != ',') { 
		if (*cur++ != ':') goto syntax_error;
		if (!isxdigit(*cur)) goto syntax_error;
		size = simple_strtoul(cur,&cur,16);
		if (*cur == ':') { /* another colon -> offset specified */
			cur++;
			if (!isxdigit(*cur)) goto syntax_error;
			offset = simple_strtoul(cur,&cur,16);
			if (*cur == ':') { /* another colon -> blksize */
			  cur++;
			  if (!isxdigit(*cur)) goto syntax_error;
			  blksize = simple_strtoul(cur,&cur,16);
			}
		}
		if (*cur != ',' && *cur != 0) goto syntax_error;
		} 
		if (*cur == ',') cur++;
		if (i >= MDISK_DEVS) {
			printk(KERN_WARNING "mnd: too many devices\n");
			return 1;
		}
		mdisk_setup_data.vdev[i] = vdev;
		mdisk_setup_data.size[i] = size;
		mdisk_setup_data.offset[i] = offset;
		mdisk_setup_data.blksize[i] = blksize;

		i++;
	}
	
	return 1;

syntax_error:
        printk(KERN_WARNING "mnd: syntax error in parameter string: %s\n", str);
	return 0;
}

__setup("mdisk=", mdisk_setup);

/*
 * Open and close
 */

static int mdisk_open (struct inode *inode, struct file *filp)
{
	mdisk_Dev *dev; /* device information */
	int num = MINOR(inode->i_rdev);
	
	/* 
	 * size 0 means device not installed
	 */
	if ((num >= MDISK_DEVS) || (mdisk_sizes[num] == 0))
		return -ENODEV;
	MOD_INC_USE_COUNT;
	dev = &mdisk_devices[num];
	dev->usage++;
	return 0;          /* success */
}

static int mdisk_release (struct inode *inode, struct file *filp)
{
	mdisk_Dev *dev = &mdisk_devices[MINOR(inode->i_rdev)];
	
	/*
	 * flush device
	 */
	
	fsync_dev(inode->i_rdev);
	dev->usage--;
	MOD_DEC_USE_COUNT;
	return 0;
}


/*
 * The mdisk() implementation
 */

static int mdisk_ioctl (struct inode *inode, struct file *filp,
                 unsigned int cmd, unsigned long arg)
{
	int err,rc, size=0;
	struct hd_geometry *geo = (struct hd_geometry *)arg;
	mdisk_Dev *dev = mdisk_devices + MINOR(inode->i_rdev);
	
	switch(cmd) {
		
	case BLKGETSIZE:
	        rc = copy_to_user ((long *) arg, &dev->size, sizeof (long));
		printk(KERN_WARNING "mnd: ioctl BLKGETSIZE %d\n",dev->size);
		return rc;
	case BLKFLSBUF: /* flush */
		if (!suser()) return -EACCES; /* only root */
		fsync_dev(inode->i_rdev);
		invalidate_buffers(inode->i_rdev);
		return 0;
		
	case BLKRAGET: /* return the readahead value */
		if (!arg)  return -EINVAL;
		err = access_ok(VERIFY_WRITE, (long *) arg, sizeof(long));
		if (err) return err;
		put_user(read_ahead[MAJOR(inode->i_rdev)],(long *) arg);
		return 0;
		
	case BLKRASET: /* set the readahead value */
		if (!suser()) return -EACCES;
		if (arg > 0xff) return -EINVAL; /* limit it */
		read_ahead[MAJOR(inode->i_rdev)] = arg;
		return 0;
		
	case BLKRRPART: /* re-read partition table: can't do it */
		return -EINVAL;
		
	case HDIO_GETGEO:
		/*
		 * get geometry of device -> linear
		 */
		size = dev->size;
		if (geo==NULL) return -EINVAL;
		err = access_ok(VERIFY_WRITE, geo, sizeof(*geo));
		if (err) return err;
		put_user(1,    &geo->cylinders);
		put_user(1,    &geo->heads);
		put_user(size, &geo->sectors);
		put_user(0,    &geo->start);
		return 0;
	}
	
	return -EINVAL; /* unknown command */
}

/*
 * The file operations
 */

static struct block_device_operations mdisk_fops = {
	ioctl:        mdisk_ioctl,
	open:         mdisk_open,
        release:      mdisk_release,
};

/*
 * The 'low level' IO function
 */


static __inline__ int
dia250(void* iob,int cmd)
{
	int rc;
	
	iob = (void*) virt_to_phys(iob);

	asm volatile ("    lr    2,%1\n"
		      "    lr    3,%2\n"
		      "    .long 0x83230250\n"
		      "    lr    %0,3"
		      : "=d" (rc)
		      : "d" (iob) , "d" (cmd)
		      : "2", "3" );
	return rc;
}
/*
 * Init of minidisk device
 */

static __inline__ int
mdisk_init_io(mdisk_Dev *dev,int blocksize,int offset,int size)
{
	mdisk_init_io_t *iob = (mdisk_init_io_t*) dev->iob;
	int rc;

	memset(iob,0,sizeof(mdisk_init_io_t));
	
	iob->dev_nr = dev->vdev;
	iob->block_size = blocksize;
	iob->offset     = offset;
	iob->start_block= 0;
	iob->end_block  = size;
	
	rc = dia250(iob,INIT_BIO);
	
	/*
	 * clear for following io once
	 */
	
	memset(iob,0,sizeof(mdisk_rw_io_t));
	
	return rc;
}

/*
 * release of minidisk device
 */

static __inline__ int
mdisk_term_io(mdisk_Dev *dev)
{
	mdisk_init_io_t *iob = (mdisk_init_io_t*) dev->iob;
	
	memset(iob,0,sizeof(mdisk_init_io_t));
	
	iob->dev_nr = dev->vdev;
	
	return dia250(iob,TERM_BIO);
}

/*
 * setup and start of minidisk io request
 */

static __inline__ int
mdisk_rw_io_clustered (mdisk_Dev *dev,
                       mdisk_bio_t* bio_array,
                       int length,
                       int req,
                       int sync)
{
	int rc;
	mdisk_rw_io_t *iob = dev->iob;
	
	iob->dev_nr      = dev->vdev;
	iob->key         = 0;
	iob->flags       = sync;
	
	iob->block_count = length;
	iob->interrupt_params = req;
	iob->bio_list     = virt_to_phys(bio_array);
	
	rc = dia250(iob,RW_BIO);
	return rc;
}



/*
 * The device characteristics function
 */

static __inline__ int
dia210(void* devchar)
{
	int rc;

	devchar = (void*) virt_to_phys(devchar);

	asm volatile ("    lr    2,%1\n"
		      "    .long 0x83200210\n"
		      "    ipm   %0\n"
		      "    srl   %0,28"
		      : "=d" (rc)
		      : "d" (devchar)
		      : "2" );
	return rc;
}
/*
 * read the label of a minidisk and extract its characteristics
 */

static __inline__ int
mdisk_read_label (mdisk_Dev *dev, int i)
{
	static mdisk_dev_char_t devchar;
	static long label[1024];
	int block, b;
	int rc;
	mdisk_bio_t *bio;

	devchar.dev_nr = dev -> vdev;
	devchar.rdc_len = sizeof(mdisk_dev_char_t);

	if (dia210(&devchar) == 0) {
		if (devchar.vdev_class == DEV_CLASS_FBA) {
			block = 2;
		}
		else {
			block = 3;
		}
		bio = dev->bio;
		for (b=512;b<4097;b=b*2) {
			rc = mdisk_init_io(dev, b, 0, 64);
			if (rc > 4) {
				continue;
			}
			memset(&bio[0], 0, sizeof(mdisk_bio_t));
			bio[0].type = MDISK_READ_REQ;
			bio[0].block_number = block;
			bio[0].buffer = virt_to_phys(&label);
			dev->nr_bhs = 1;
			if (mdisk_rw_io_clustered(dev,
			                          &bio[0],
			                          1,
			                          (unsigned long) dev,
			                          MDISK_SYNC)
			    == 0 ) {
				if (label[0] != 0xc3d4e2f1) { /* CMS1 */
					printk ( KERN_WARNING "mnd: %4lX "
					         "is not CMS format\n",
					         mdisk_setup_data.vdev[i]);
					rc = mdisk_term_io(dev);
					return 1;
				}
				if (label[13] == 0) {
					printk ( KERN_WARNING "mnd: %4lX "
		   		         "is not reserved\n",
					         mdisk_setup_data.vdev[i]);
					rc = mdisk_term_io(dev);
					return 2;
				}
				mdisk_setup_data.size[i] =
				   (label[7] - 1 - label[13]) *
				   (label[3] >> 9) >> 1;
				mdisk_setup_data.blksize[i] = label[3];
				mdisk_setup_data.offset[i] = label[13] + 1;
				rc = mdisk_term_io(dev);
				return rc;
			}
			rc = mdisk_term_io(dev);
		}
		printk ( KERN_WARNING "mnd: Cannot read label of %4lX "
			 "- is it formatted?\n",
			 mdisk_setup_data.vdev[i]);
		return 3;
	}
	return 4;
}





/*
 * this handles a clustered request in success case
 * all buffers are detach and marked uptodate to the kernel
 * then CURRENT->bh is set to the last processed but not
 * update buffer
 */

static __inline__ void
mdisk_end_request(int nr_bhs)
{
	int i;
	struct buffer_head *bh;
	struct request *req;

	if (nr_bhs > 1) {
		req = CURRENT;
		bh  = req->bh;
		
		for (i=0; i < nr_bhs-1; i++) {
			req->bh = bh->b_reqnext;
			bh->b_reqnext = NULL;
			bh->b_end_io(bh,1);
			bh = req->bh;			
		}
		
		/*
		 * set CURRENT to last processed, not marked buffer
		 */
		req->buffer = bh->b_data;
		req->current_nr_sectors = bh->b_size >> 9;
		CURRENT = req;
	}
	end_request(1);
}



/*
 * Block-driver specific functions
 */

void mdisk_request(request_queue_t *queue)
{
	mdisk_Dev *dev;
	mdisk_bio_t *bio;
	struct buffer_head *bh;
	unsigned int sector, nr, offset;
	int rc,rw,i;

	i = 0;
	while(CURRENT) {
		INIT_REQUEST;
		
		/* Check if the minor number is in range */
		if (DEVICE_NR(CURRENT_DEV) > MDISK_DEVS) {
			static int count = 0;
			if (count++ < 5) /* print the message at most five times */
				printk(KERN_WARNING "mnd: request for minor %d out of range\n",
				       DEVICE_NR(CURRENT_DEV)  )       ;
			end_request(0);
			continue;
		}
		
		/*
		 * Pointer to device structure, from the static array
		 */
		dev = mdisk_devices + DEVICE_NR(CURRENT_DEV);
		
		/*
		 * check, if operation is past end of devices
		 */
		if (CURRENT->nr_sectors + CURRENT->sector > dev->size) {
			static int count = 0;
			if (count++ < 5)
				printk(KERN_WARNING "mnd%c: request past end of device\n",
				       DEVICE_NR(CURRENT_DEV));
			end_request(0);
			continue;
		}
		
		/*
		 * do command (read or write)
		 */
		switch(CURRENT->cmd) {
		case READ:
			rw = MDISK_READ_REQ;
			break;
		case WRITE:
			rw = MDISK_WRITE_REQ;
			break;
		default:
			/* can't happen */
			end_request(0);
			continue;
		}
		
		/*
		 * put the clustered requests in mdisk_bio array
		 * nr_sectors is checked against max_sectors in make_request
		 * nr_sectors and sector are always blocks of 512 
		 * but bh_size depends on the filesystems size
		 */
		sector = CURRENT->sector>>dev->blkshift;
		bh     = CURRENT->bh;
		bio    = dev->bio;
		dev->nr_bhs = 0;
		
		/*
		 * sector is translated to block in minidisk context
		 * 
		 */
		offset = 0;


		
		for (nr = 0,i = 0; 
		     nr < CURRENT->nr_sectors && bh;
		     nr+=dev->blkmult, sector++,i++) {
			memset(&bio[i], 0, sizeof(mdisk_bio_t));
			bio[i].type   = rw;
			bio[i].block_number = sector;
			bio[i].buffer  = virt_to_phys(bh->b_data+offset);
			offset += dev->blksize;
			if (bh->b_size <= offset) {
				offset = 0;
				bh = bh->b_reqnext;
				dev->nr_bhs++;
			}
		}

		if (( rc = mdisk_rw_io_clustered(dev, &bio[0], i,
						 (unsigned long) dev,
#ifdef CONFIG_MDISK_SYNC
						 MDISK_SYNC
#else
						 MDISK_ASYNC
#endif
			)) > 8 ) {
			printk(KERN_WARNING "mnd%c: %s request failed rc %d"
			       " sector %ld nr_sectors %ld \n",
			       DEVICE_NR(CURRENT_DEV),
			       rw == MDISK_READ_REQ ? "read" : "write",
			       rc, CURRENT->sector, CURRENT->nr_sectors);
			end_request(0);
			continue;
		}
		i = 0;		
		/*
		 * Synchron: looping to end of request (INIT_REQUEST has return)
		 * Asynchron: end_request done in bottom half
		 */
#ifdef CONFIG_MDISK_SYNC
		mdisk_end_request(dev->nr_bhs);
#else
		if (rc == 0)
		        mdisk_end_request(dev->nr_bhs);
		else
		return;
#endif	
	}
}


/*
 * mdisk interrupt handler called when read/write request finished
 * queues and marks a bottom half.
 *
 */
void do_mdisk_interrupt(void)
{
        u16 code;
	mdisk_Dev *dev;
	
	code = S390_lowcore.cpu_addr;

	if ((code >> 8) != 0x03) {
	  printk("mnd: wrong sub-interruption code %d",code>>8);
	  return;
	}

	/*
	 * pointer to devives structure given as external interruption
	 * parameter
	 */
	dev = (mdisk_Dev*) S390_lowcore.ext_params;
	dev->status = code & 0x00ff;

	queue_task(&dev->tqueue, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
}

/*
 * the bottom half checks the status of request
 * on success it calls end_request and calls mdisk_request
 * if more transfer to do
 */

static void
do_mdisk_bh(void *data)
{
	mdisk_Dev *dev = (mdisk_Dev*) data;
	unsigned long flags;

	spin_lock_irqsave(&io_request_lock, flags);
	/*
	 * check for status of asynchronous rw
	 */
	if (dev->status != 0x00) {
		printk("mnd: status of async rw %d",dev->status);
		end_request(0);
	} else {
		/*
		 * end request for clustered requests
		 */
	  if (CURRENT)
		mdisk_end_request(dev->nr_bhs);
	}

	/*
	 * if more to do, call mdisk_request
	 */
	if (CURRENT)
		mdisk_request(NULL);
	spin_unlock_irqrestore(&io_request_lock, flags);
}

void /* Added fuction HSM 12/03/99 */
mdisk_handler (int cpu, void *ds, struct pt_regs *regs)
{
	printk (KERN_ERR "mnd: received I/O interrupt... shouldn't happen\n");
}

int __init mdisk_init(void)
{
        int rc,i;
        mdisk_Dev *dev;
	request_queue_t *q;

	/*
	 * register block device
	 */
	if (register_blkdev(MAJOR_NR,"mnd",&mdisk_fops) < 0) {
		printk("mnd: unable to get major %d for mini disk\n"
		       ,MAJOR_NR);
		return MAJOR_NR;
	}
        q = BLK_DEFAULT_QUEUE(MAJOR_NR);
        blk_init_queue(q, mdisk_request);
        blk_queue_headactive(BLK_DEFAULT_QUEUE(major), 0);
	
	/*
	 * setup sizes for available devices
	 */
	read_ahead[MAJOR_NR] = MDISK_RAHEAD;	/* 8 sector (4kB) read-ahead */
	blk_size[MAJOR_NR] = mdisk_sizes;       /* size of reserved mdisk    */
	blksize_size[MAJOR_NR] = mdisk_blksizes;   /* blksize of device      */
	hardsect_size[MAJOR_NR] = mdisk_hardsects;
	max_sectors[MAJOR_NR]   = mdisk_maxsectors;
	blk_init_queue(BLK_DEFAULT_QUEUE(MAJOR_NR), DEVICE_REQUEST);
	
	for (i=0;i<MDISK_DEVS;i++) {
		if (mdisk_setup_data.vdev[i] == 0) {
			continue;
		}
		/* Added block HSM 12/03/99 */
		if ( request_irq(get_irq_by_devno(mdisk_setup_data.vdev[i]),
				 mdisk_handler, 0, "mnd",
				 &(mdisk_devices[i].dev_status)) ){
			printk ( KERN_WARNING "mnd: Cannot acquire I/O irq of"
				 " %4lX for paranoia reasons, skipping\n",
				 mdisk_setup_data.vdev[i]);
			continue;
		}
		/*
		 * open VM minidisk low level device
		 */
		dev = &mdisk_devices[i];
		dev->bio=mdisk_bio[i];
		dev->iob=&mdisk_iob[i];
		dev->vdev = mdisk_setup_data.vdev[i];

		if ( mdisk_setup_data.size[i] == 0 )
		        rc = mdisk_read_label(dev, i);
		dev->size = mdisk_setup_data.size[i] * 2; /* buffer 512 b */
		dev->blksize = mdisk_setup_data.blksize[i]; 
		dev->tqueue.routine = do_mdisk_bh;
		dev->tqueue.data = dev;
		dev->blkmult = dev->blksize/512;
		dev->blkshift = 
		  dev->blkmult==1?0:
		  dev->blkmult==2?1:
		  dev->blkmult==4?2:
		  dev->blkmult==8?3:-1;

		mdisk_sizes[i] = mdisk_setup_data.size[i];
		mdisk_blksizes[i]  = mdisk_setup_data.blksize[i];
		mdisk_hardsects[i] = mdisk_setup_data.blksize[i];

		/*
		 * max sectors for one clustered req  
		 */
		mdisk_maxsectors[i] = MDISK_MAXSECTORS*dev->blkmult;

		rc = mdisk_init_io(dev,
				   mdisk_setup_data.blksize[i],
				   mdisk_setup_data.offset[i],/* offset in vdev*/
				   dev->size>>dev->blkshift  /* size in blocks */
			);
		if (rc > 4) {
			printk("mnd%c: init failed (rc: %d)\n",'a'+i,rc);
			mdisk_sizes[i] = 0;
			continue;
		}
		
		/*
		 * set vdev in device structure for further rw access
		 * vdev and size given by linload
		 */
		printk("mnd%c: register device at major %X with %d blocks %d blksize \n",
		       'a' + i, MAJOR_NR, dev->size>>dev->blkshift,dev->blkmult*512);
	}
	
	/*
	 * enable service-signal external interruptions,
	 * Control Register 0 bit 22 := 1
	 * (besides PSW bit 7 must be set to 1 somewhere for external
	 * interruptions)
	 */
        ctl_set_bit(0, 9);
	
        return 0;
}

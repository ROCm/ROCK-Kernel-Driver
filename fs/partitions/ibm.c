/*
 * File...........: linux/fs/partitions/ibm.c      
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *                  Volker Sameske <sameske@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000

 * History of changes (starts July 2000)
 * 07/10/00 Fixed detection of CMS formatted disks     
 * 02/13/00 VTOC partition support added
 */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <linux/slab.h>
#include <linux/hdreg.h>
#include <linux/ioctl.h>
#include <linux/version.h>
#include <asm/ebcdic.h>
#include <asm/uaccess.h>
#include <asm/dasd.h>

#include "ibm.h"
#include "check.h"
#include <asm/vtoc.h>

typedef enum {
  ibm_partition_lnx1 = 0,
  ibm_partition_vol1 = 1,
  ibm_partition_cms1 = 2,
  ibm_partition_none = 3
} ibm_partition_t;

static char* part_names[] = {   [ibm_partition_lnx1] = "LNX1",
			     [ibm_partition_vol1] = "VOL1",
			     [ibm_partition_cms1] = "CMS1",
			     [ibm_partition_none] = "(nonl)"
};

static int
get_drive_geometry(int kdev,struct hd_geometry *geo) 
{
	int rc = 0;
	mm_segment_t old_fs;
	struct file *filp;
	struct inode *inode;
        /* find out offset of volume label (partn table) */
        filp = (struct file *)kmalloc (sizeof(struct file),GFP_KERNEL);
        if ( filp == NULL ) {
                printk (KERN_WARNING __FILE__ " ibm_partition: kmalloc failed fo
r filp\n");
                return -ENOMEM;
        }
        memset(filp,0,sizeof(struct file));
        filp ->f_mode = 1; /* read only */
        inode = get_empty_inode();
	if ( inode == NULL )
		return -ENOMEM;
        inode -> i_rdev = kdev;
	inode -> i_bdev = bdget(kdev_t_to_nr(kdev));
	rc = blkdev_open(inode,filp);
        if ( rc == 0 ) {
		old_fs=get_fs();
		set_fs(KERNEL_DS);
		rc = inode-> i_bdev -> bd_op->ioctl (inode, filp, HDIO_GETGEO, 
						     (unsigned long)(geo))
			;
		set_fs(old_fs);
	}
	blkdev_put(inode->i_bdev,BDEV_FILE);
	return rc;
}

static int
get_drive_info(int kdev,dasd_information_t *info) 
{
	int rc = 0;
	mm_segment_t old_fs;
	struct file *filp;
	struct inode *inode;
        /* find out offset of volume label (partn table) */
        filp = (struct file *)kmalloc (sizeof(struct file),GFP_KERNEL);
        if ( filp == NULL ) {
                printk (KERN_WARNING __FILE__ " ibm_partition: kmalloc failed fo
r filp\n");
                return -ENOMEM;
        }
        memset(filp,0,sizeof(struct file));
        filp ->f_mode = 1; /* read only */
        inode = get_empty_inode();
	if ( inode == NULL )
		return -ENOMEM;
        inode -> i_rdev = kdev;
	inode -> i_bdev = bdget(kdev_t_to_nr(kdev));
        rc = blkdev_open(inode,filp);
        if ( rc == 0 ) {
		old_fs=get_fs();
		set_fs(KERNEL_DS);
		rc = inode-> i_bdev -> bd_op->ioctl (inode, filp, BIODASDINFO, 
						     (unsigned long)(info));
		set_fs(old_fs);
	}
	blkdev_put(inode->i_bdev,BDEV_FILE);
	return rc;
}

static ibm_partition_t
get_partition_type ( char * type )
{
	int i;
	for ( i = 0; i < 3; i ++) {
		if ( ! strncmp (type,part_names[i],4) ) 
			break;
	}
        return i;
}

/*
 * add the two default partitions
 * - whole dasd
 * - whole dasd without "offset"
 */
static inline void
two_partitions(struct gendisk *hd,
	       int minor,
	       int blocksize,
	       int offset,
	       int size) {

        add_gd_partition( hd, minor, 0,size);
	add_gd_partition( hd, minor + 1, 
			   offset * (blocksize >> 9),
			   size-offset*(blocksize>>9));
}


/*
 * compute the block number from a 
 * cyl-cyl-head-head structure
 */
static inline int
cchh2blk (cchh_t *ptr, struct hd_geometry *geo) {
        return ptr->cc * geo->heads * geo->sectors +
	       ptr->hh * geo->sectors;
}


/*
 * compute the block number from a 
 * cyl-cyl-head-head-block structure
 */
static inline int
cchhb2blk (cchhb_t *ptr, struct hd_geometry *geo) {
        return ptr->cc * geo->heads * geo->sectors +
		ptr->hh * geo->sectors +
		ptr->b;
}

int 
ibm_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sector, int
first_part_minor)
{
	struct buffer_head *bh, *buf;
	ibm_partition_t partition_type;
	char type[5] = {0,};
	char name[7] = {0,};
	struct hd_geometry *geo;
	int blocksize;
	int offset=0, size=0, psize=0, counter=0;
	unsigned int blk;
	format1_label_t f1;
	volume_label_t vlabel;
	dasd_information_t *info;

	if ( first_sector != 0 ) {
		BUG();
	}
	info = (struct dasd_information_t *)kmalloc(sizeof(dasd_information_t),
						    GFP_KERNEL);
	if ( info == NULL )
		return 0;
	if (get_drive_info (dev,info)) 
		return 0;
	geo = (struct hd_geometry *)kmalloc(sizeof(struct hd_geometry),
					    GFP_KERNEL);
	if ( geo == NULL )
		return 0;
	if (get_drive_geometry (dev,geo)) 
		return 0;
	blocksize = hardsect_size[MAJOR(dev)][MINOR(dev)];
	if ( blocksize <= 0 ) {
		return 0;
	}
	
	set_blocksize(dev, blocksize);  /* OUCH !! */
	if ( ( bh = bread( dev, info->label_block, blocksize) ) != NULL ) {
		strncpy ( type,bh -> b_data + 0, 4);
		if ((!info->FBA_layout) && (!strcmp(info->type,"ECKD"))) {
		        
		        strncpy ( name,bh -> b_data + 8, 6);
		} else {
		        strncpy ( name,bh -> b_data + 4, 6);		  
		}
		memcpy (&vlabel, bh->b_data, sizeof(volume_label_t));
        } else {
		return 0;
	}
	EBCASC(type,4);
	EBCASC(name,6);
	
	partition_type = get_partition_type(type);
	printk ( "%4s/%8s:",part_names[partition_type],name);
	switch ( partition_type ) {
	case ibm_partition_cms1:
		if (* (((long *)bh->b_data) + 13) != 0) {
			/* disk is reserved minidisk */
			long *label=(long*)bh->b_data;
			blocksize = label[3];
			offset = label[13];
			size = (label[7]-1)*(blocksize>>9); 
			printk ("(MDSK)");
		} else {
			offset = (info->label_block + 1);
			size = hd -> sizes[MINOR(dev)]<<1;
		}
		two_partitions( hd, MINOR(dev), blocksize, offset, size);
		break;
	case ibm_partition_lnx1: 
	case ibm_partition_none:
		offset = (info->label_block + 1);
		size = hd -> sizes[MINOR(dev)]<<1;
		two_partitions( hd, MINOR(dev), blocksize, offset, size);
		break;
	case ibm_partition_vol1: 
		size = hd -> sizes[MINOR(dev)]<<1;
		add_gd_partition(hd, MINOR(dev), 0, size);
		
		/* get block number and read then first format1 label */
		blk = cchhb2blk(&vlabel.vtoc, geo) + 1;
		if ((buf = bread( dev, blk, blocksize)) != NULL) {
		        memcpy (&f1, buf->b_data, sizeof(format1_label_t));
			bforget(buf);
		}
		
		while (f1.DS1FMTID == _ascebc['1']) {
		        offset = cchh2blk(&f1.DS1EXT1.llimit, geo);
			psize  = cchh2blk(&f1.DS1EXT1.ulimit, geo) - 
				offset + geo->sectors;
			
			counter++;
			add_gd_partition(hd, MINOR(dev) + counter, 
					 offset * (blocksize >> 9),
					 psize * (blocksize >> 9));
			
			blk++;
			if ((buf = bread( dev, blk, blocksize)) != NULL) {
			        memcpy (&f1, buf->b_data, 
					sizeof(format1_label_t));
				bforget(buf);
			}
		}
		break;
	default:
		add_gd_partition( hd, MINOR(dev), 0, 0);
		add_gd_partition( hd, MINOR(dev) + 1, 0, 0);
	}
	
	printk ( "\n" );
	bforget(bh);
	return 1;
}

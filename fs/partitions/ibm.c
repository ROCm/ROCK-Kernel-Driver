/*
 * File...........: linux/fs/partitions/ibm.c      
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000

 * History of changes (starts July 2000)
 * 07/10/00 Fixed detection of CMS formatted disks               

 */

#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <linux/malloc.h>
#include <linux/hdreg.h>
#include <linux/ioctl.h>
#include <linux/version.h>
#include <asm/ebcdic.h>
#include <asm/uaccess.h>
#include <asm/dasd.h>

#include "ibm.h"
#include "check.h"

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
/* We hook in when DASD is a module... */
int (*genhd_dasd_name)(char*,int,int,struct gendisk*) = NULL;
#endif /* LINUX_IS_24 */

typedef enum {
  ibm_partition_none = 0,
  ibm_partition_lnx1 = 1,
  ibm_partition_vol1 = 3,
  ibm_partition_cms1 = 4
} ibm_partition_t;

static ibm_partition_t
get_partition_type ( char * type )
{
        static char lnx[5]="LNX1";
        static char vol[5]="VOL1";
        static char cms[5]="CMS1";
        if ( ! strncmp ( lnx, "LNX1",4 ) ) {
                ASCEBC(lnx,4);
                ASCEBC(vol,4);
                ASCEBC(cms,4);
        }
        if ( ! strncmp (type,lnx,4) ||
             ! strncmp (type,"LNX1",4) )
                return ibm_partition_lnx1;
        if ( ! strncmp (type,vol,4) )
                return ibm_partition_vol1;
        if ( ! strncmp (type,cms,4) )
                return ibm_partition_cms1;
        return ibm_partition_none;
}

int 
ibm_partition(struct gendisk *hd, kdev_t dev, unsigned long first_sector, int
first_part_minor)
{
	struct buffer_head *bh;
	ibm_partition_t partition_type;
	char type[5] = {0,};
	char name[7] = {0,};
	struct hd_geometry geo;
	mm_segment_t old_fs;
	int blocksize;
	struct file *filp = NULL;
	struct inode *inode = NULL;
	int offset, size;

	blocksize = hardsect_size[MAJOR(dev)][MINOR(dev)];
	if ( blocksize <= 0 ) {
		return 0;
	}
	set_blocksize(dev, blocksize);  /* OUCH !! */

	/* find out offset of volume label (partn table) */
	inode = get_empty_inode();
	inode -> i_rdev = dev;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
	inode -> i_bdev = bdget(kdev_t_to_nr(dev));
#endif /* KERNEL_VERSION */
	filp = (struct file *)kmalloc (sizeof(struct file),GFP_KERNEL);
	if (!filp)
		return 0;
	memset(filp,0,sizeof(struct file));
	filp ->f_mode = 1; /* read only */
	blkdev_open(inode,filp);
	old_fs=get_fs();
	set_fs(KERNEL_DS);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
	inode-> i_bdev -> bd_op->ioctl (inode, filp, HDIO_GETGEO, (unsigned long)(&geo));
#else
	filp->f_op->ioctl (inode, filp, HDIO_GETGEO, (unsigned long)(&geo));
#endif /* KERNEL_VERSION */
	set_fs(old_fs);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0))
        blkdev_put(inode->i_bdev,BDEV_FILE);
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
	blkdev_close(inode,filp);
#else
	blkdev_release(inode);
#endif /* LINUX_VERSION_CODE */

	size = hd -> sizes[MINOR(dev)]<<1;
	if ( ( bh = bread( dev, geo.start, blocksize) ) != NULL ) {
		strncpy ( type,bh -> b_data, 4);
		strncpy ( name,bh -> b_data + 4, 6);
        } else {
		return 0;
	}
	if ( (*(char *)bh -> b_data) & 0x80 ) {
		EBCASC(name,6);
	}
	switch ( partition_type = get_partition_type(type) ) {
	case ibm_partition_lnx1: 
		offset = (geo.start + 1);
		printk ( "(LNX1)/%6s:",name);
		break;
	case ibm_partition_vol1:
		offset = 0;
		size = 0;
		printk ( "(VOL1)/%6s:",name);
		break;
	case ibm_partition_cms1:
		printk ( "(CMS1)/%6s:",name);
		if (* (((long *)bh->b_data) + 13) == 0) {
			/* disk holds a CMS filesystem */
			offset = (geo.start + 1);
			printk ("(CMS)");
		} else {
			/* disk is reserved minidisk */
			// mdisk_setup_data.size[i] =
			// (label[7] - 1 - label[13]) *
			// (label[3] >> 9) >> 1;
			long *label=(long*)bh->b_data;
			blocksize = label[3];
			offset = label[13];
			size = (label[7]-1)*(blocksize>>9); 
			printk ("(MDSK)");
		}
		break;
	case ibm_partition_none:
		printk ( "(nonl)/      :");
		offset = (geo.start+1);
		break;
	default:
		offset = 0;
		size = 0;
		
	}
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
	add_gd_partition( hd, MINOR(dev), 0,size);
	add_gd_partition( hd, MINOR(dev) + 1, offset * (blocksize >> 9),
			  size-offset*(blocksize>>9));
#else
	add_partition( hd, MINOR(dev), 0,size,0);
	add_partition( hd, MINOR(dev) + 1, offset * (blocksize >> 9),
			  size-offset*(blocksize>>9) ,0 );
#endif /* LINUX_VERSION */
	printk ( "\n" );
	bforget(bh);
	return 1;
}


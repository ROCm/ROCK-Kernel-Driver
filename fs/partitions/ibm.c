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
#include <linux/module.h>
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
#include <asm/vtoc.h>

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
/* We hook in when DASD is a module... */
int (*genhd_dasd_name)(char*,int,int,struct gendisk*) = NULL;
int (*genhd_dasd_fillgeo)(int,struct hd_geometry *) = NULL;
EXPORT_SYMBOL(genhd_dasd_fillgeo);
EXPORT_SYMBOL(genhd_dasd_name);
#endif /* LINUX_IS_24 */

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
	struct hd_geometry geo;
	int blocksize;
	int offset=0, size=0, psize=0, counter=0;
	unsigned int blk;
	format1_label_t f1;
	volume_label_t vlabel;

	if ( first_sector != 0 ) {
		BUG();
	}
	if ( !genhd_dasd_fillgeo ) {
		return 0;
	}
	genhd_dasd_fillgeo(dev,&geo);
	blocksize = hardsect_size[MAJOR(dev)][MINOR(dev)];
	if ( blocksize <= 0 ) {
		return 0;
	}

	set_blocksize(dev, blocksize);  /* OUCH !! */
	if ( ( bh = bread( dev, geo.start, blocksize) ) != NULL ) {
		strncpy ( type,bh -> b_data + 0, 4);
		strncpy ( name,bh -> b_data + 4, 6);
		memcpy (&vlabel, bh->b_data, sizeof(volume_label_t));
        } else {
		return 0;
	}
	EBCASC(type,4);
	EBCASC(name,6);

	partition_type = get_partition_type(type);
	printk ( "%6s/%6s:",part_names[partition_type],name);
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
			offset = (geo.start + 1);
			size = hd -> sizes[MINOR(dev)]<<1;
		}
		two_partitions( hd, MINOR(dev), blocksize, 
				offset, size);
		break;
	case ibm_partition_lnx1: 
	case ibm_partition_none:
		offset = (geo.start + 1);
		size = hd -> sizes[MINOR(dev)]<<1;
		two_partitions( hd, MINOR(dev), blocksize, 
				offset, size);
		break;
	case ibm_partition_vol1:
		add_gd_partition(hd, MINOR(dev), 0, size);

		/* get block number and read then first format1 label */
		blk = cchhb2blk(&vlabel.vtoc, &geo) + 1;
		if ((buf = bread( dev, blk, blocksize)) != NULL) {
		        memcpy (&f1, buf->b_data, sizeof(format1_label_t));
			bforget(buf);
		}

		while (f1.DS1FMTID == _ascebc['1']) {
		        offset = cchh2blk(&f1.DS1EXT1.llimit, &geo);
			psize  = cchh2blk(&f1.DS1EXT1.ulimit, &geo) - 
				offset + 1;
			
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


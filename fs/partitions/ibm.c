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

        add_gd_partition( hd, minor, 0, size);
	add_gd_partition( hd, minor+1, offset*blocksize, size-offset*blocksize);
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
ibm_partition(struct gendisk *hd, struct block_device *bdev,
		unsigned long first_sector, int first_part_minor)
{
	Sector sect, sect2;
	unsigned char *data;
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
	kdev_t dev = to_kdev_t(bdev->bd_dev);

	if ( first_sector != 0 )
		BUG();

	info = (struct dasd_information_t *)kmalloc(sizeof(dasd_information_t),
						    GFP_KERNEL);
	if ( info == NULL )
		return 0;
	if (ioctl_by_bdev(bdev, BIODASDINFO, (unsigned long)(info)))
		return 0;
	geo = (struct hd_geometry *)kmalloc(sizeof(struct hd_geometry),
					    GFP_KERNEL);
	if ( geo == NULL )
		return 0;
	if (ioctl_by_bdev(bdev, HDIO_GETGEO, (unsigned long)geo);
		return 0;
	blocksize = hardsect_size[MAJOR(dev)][MINOR(dev)];
	if ( blocksize <= 0 ) {
		return 0;
	}
	blocksize >>= 9;
	
	data = read_dev_sector(bdev, inode->label_block*blocksize, &sect);
	if (!data)
		return 0;

	strncpy (type, data, 4);
	if ((!info->FBA_layout) && (!strcmp(info->type,"ECKD"))) {
		strncpy ( name, data + 8, 6);
	} else {
		strncpy ( name, data + 4, 6);
	}
	memcpy (&vlabel, data, sizeof(volume_label_t));

	EBCASC(type,4);
	EBCASC(name,6);
	
	partition_type = get_partition_type(type);
	printk ( "%4s/%8s:",part_names[partition_type],name);
	switch ( partition_type ) {
	case ibm_partition_cms1:
		if (* ((long *)data + 13) != 0) {
			/* disk is reserved minidisk */
			long *label=(long*)data;
			blocksize = label[3]>>9;
			offset = label[13];
			size = (label[7]-1)*blocksize; 
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
		data = read_dev_sector(bdev, blk * blocksize, &sect2);
		if (data) {
		        memcpy (&f1, data, sizeof(format1_label_t));
			put_dev_sector(sect2);
		}
		
		while (f1.DS1FMTID == _ascebc['1']) {
		        offset = cchh2blk(&f1.DS1EXT1.llimit, geo);
			psize  = cchh2blk(&f1.DS1EXT1.ulimit, geo) - 
				offset + geo->sectors;
			
			counter++;
			add_gd_partition(hd, MINOR(dev) + counter, 
					 offset * blocksize,
					 psize * blocksize);
			
			blk++;
			data = read_dev_sector(bdev, blk * blocksize, &sect2);
			if (data) {
			        memcpy (&f1, data, sizeof(format1_label_t));
				put_dev_sector(sect2);
			}
		}
		break;
	default:
		add_gd_partition( hd, MINOR(dev), 0, 0);
		add_gd_partition( hd, MINOR(dev) + 1, 0, 0);
	}
	
	printk ( "\n" );
	put_dev_sector(sect);
	return 1;
}

/*
 * File...........: linux/fs/partitions/ibm.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000
 */

#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>

#include <asm/ebcdic.h>
#include "../../drivers/s390/block/dasd_types.h"

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

void
ibm_partition (struct gendisk *hd, kdev_t dev)
{
	struct buffer_head *bh;
	ibm_partition_t partition_type;
	int di = MINOR(dev) >> PARTN_BITS;
	char type[5] = {0,};
	char name[7] = {0,};
	if ( bh = bread( dev, 
			 dasd_info[di]->sizes.label_block, 
			 get_ptable_blocksize(dev) ) ) {
		strncpy ( type,bh -> b_data, 4);
		strncpy ( name,bh -> b_data + 4, 6);
        } else {
		return;
	}
	if ( (*(char *)bh -> b_data) & 0x80 ) {
		EBCASC(name,6);
	}
	switch ( partition_type = get_partition_type(type) ) {
	case ibm_partition_lnx1:
		printk ( "(LNX1)/%6s:",name);
		add_gd_partition( hd, MINOR(dev) + 1, 
				  (dasd_info [di]->sizes.label_block + 1) <<
				  dasd_info [di]->sizes.s2b_shift,
				  (dasd_info [di]->sizes.blocks -
				   dasd_info [di]->sizes.label_block - 1) <<
				  dasd_info [di]->sizes.s2b_shift );
		break;
	case ibm_partition_vol1:
		printk ( "(VOL1)/%6s:",name);
		break;
	case ibm_partition_cms1:
		printk ( "(CMS1)/%6s:",name);
		if (* (((long *)bh->b_data) + 13) == 0) {
		  /* disk holds a CMS filesystem */
		  add_gd_partition( hd, MINOR(dev) + 1, 
				    (dasd_info [di]->sizes.label_block + 1) <<
				    dasd_info [di]->sizes.s2b_shift,
				    (dasd_info [di]->sizes.blocks -
				     dasd_info [di]->sizes.label_block) <<
				    dasd_info [di]->sizes.s2b_shift );
		  printk ("(CMS)");
		} else {
		  /* disk is reserved minidisk */
		  int offset = (*(((long *)bh->b_data) + 13));
		  int size = (*(((long *)bh->b_data) + 7)) - 1 - 
		    (*(((long *)bh->b_data) + 13)) *
		    ((*(((long *)bh->b_data) + 3)) >> 9);
		  add_gd_partition( hd, MINOR(dev) + 1, 
				    offset << dasd_info [di]->sizes.s2b_shift,
				    size << dasd_info [di]->sizes.s2b_shift );
		  printk ("(MDSK)");
		}
		break;
	case ibm_partition_none:
		printk ( "(nonl)/      :");
/*
		printk ( "%d %d %d ", MINOR(dev) + 1, 
			 (dasd_info [di]->sizes.label_block + 1) <<
			 dasd_info [di]->sizes.s2b_shift,
			 (dasd_info [di]->sizes.blocks -
			  dasd_info [di]->sizes.label_block - 1) <<
			 dasd_info [di]->sizes.s2b_shift );
*/
		add_gd_partition( hd, MINOR(dev) + 1, 
				  (dasd_info [di]->sizes.label_block + 1) <<
				  dasd_info [di]->sizes.s2b_shift,
				  (dasd_info [di]->sizes.blocks -
				   dasd_info [di]->sizes.label_block - 1) <<
				  dasd_info [di]->sizes.s2b_shift );
		break;
	}
	printk ( "\n" );
	bforget(bh);
}


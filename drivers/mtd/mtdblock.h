/*
 * drivers/mtd/mtdblock.h
 *
 * common defines for mtdblock-core and mtdblock-2x
 *
 * $Id: mtdblock.h,v 1.1 2002/11/27 10:33:37 gleixner Exp $
 *
 */

#ifndef __MTD_MTDBLOCK_H__
#define __MTD_MTDBLOCK_H__

#define MAJOR_NR MTD_BLOCK_MAJOR
#define DEVICE_NAME "mtdblock"

struct mtdblk_dev {
	struct mtd_info *mtd; /* Locked */
	int count;
	struct semaphore cache_sem;
	unsigned char *cache_data;
	unsigned long cache_offset;
	unsigned int cache_size;
	enum { STATE_EMPTY, STATE_CLEAN, STATE_DIRTY } cache_state;
}; 

extern int write_cached_data (struct mtdblk_dev *mtdblk);
extern int do_cached_write (struct mtdblk_dev *mtdblk, unsigned long pos, 
			    int len, const char *buf);
extern int do_cached_read (struct mtdblk_dev *mtdblk, unsigned long pos, 
			   int len, char *buf);

extern void __exit cleanup_mtdblock(void);
extern int __init init_mtdblock(void);

#endif

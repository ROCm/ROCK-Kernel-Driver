// -*- mode: cpp; mode: fold -*-
// Description                                                          /*{{{*/
// $Id: mapped.h,v 1.2 2000/03/14 17:13:12 dwmw2 Exp $
/* ######################################################################

   Memory Mapped  MTD Routines
   
   These routines are support routines for memory mapped chips, with
   routines to support common sorts of flash. For devices that are based
   on a memory mapped interface these routines provide everything necessary,
   only a window changing function is required by the low level implementation.
   
   The entry point to setup and register a memory mapped MTD device, 
   mtd_mapped_setup will perform a detection sequence that can determine
   the type size and configuration of many sorts of chip setups. 

   ROMs and RAMs are detected and passed off to very simple routines, Flash
   writing and erasing is handled as well.
   
   ##################################################################### */
									/*}}}*/
#ifndef __MTD_FLASH_H__
#define __MTD_FLASH_H__

#include <linux/types.h>
#include <linux/mtd/mtd.h>

// MTD flags for ordinary flash
struct JEDECTable
{
   u_short jedec;
   char *name;
   u_long size;
   u_long sectorsize;
   u_long capabilities;
};

// JEDEC being 0 is the end of the chip array
struct flash_chip
{
   u_short jedec;
   u_long size;
   u_long sectorsize;
   u_long base;
   u_long capabilities;
   
   // These markers are filled in by the flash_chip_scan function
   u_long start;
   u_long length;
};

struct mapped_mtd_info
{
   struct mtd_info mtd;
   u_long pagesize;        // Size of the memory window
   u_long maxsize;         // Maximum MTD size in pages
   u_char mfr,id;
   char part[100];            // Part Catalogue number if available
   int *lock;
   // Multiple chip support, only used if this is type MTD_FLASH
   u_char interleve;       // Address chip interleve (0 = concatination)
   struct flash_chip chips[5];
  
   // Operations
   unsigned long (*page)(struct mapped_mtd_info *map,unsigned long page);
   int (*jedec_sense)(struct mapped_mtd_info *map);
};

extern struct JEDECTable mtd_JEDEC_table[];

// Automatic configurators
extern int mtd_mapped_setup(struct mapped_mtd_info *map);
extern int mtd_mapped_remove(struct mapped_mtd_info *map);

// Generic functions
extern int flash_jedec(struct mapped_mtd_info *map);
extern int flash_erase(struct mtd_info *map, struct erase_info *instr);
extern int flash_write(struct mtd_info *map, loff_t start, size_t len,
		       size_t *retlen, const u_char *buf);
extern int rom_read(struct mtd_info *map, loff_t start, size_t len,
		    size_t *retlen, u_char *buf);
extern int ram_write(struct mtd_info *map, loff_t start, size_t len,
		     size_t *retlen, const u_char *buf);

// Helpers
extern int page_jump(struct mapped_mtd_info *map,unsigned long start,
		     unsigned long len,unsigned long *buffer,
		     unsigned long *size);
extern void flash_chip_scan(struct mapped_mtd_info *map,unsigned long start,
			    unsigned long len);

#endif /* __MTD_FLASH_H__ */

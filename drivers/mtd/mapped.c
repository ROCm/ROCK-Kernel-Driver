// -*- mode: cpp; mode: fold -*-
// Description                                                          /*{{{*/
// $Id: mapped.c,v 1.8 2000/03/31 14:40:42 dwmw2 Exp $
/* ######################################################################

   Flash MTD Routines

   These routine support IDing and manipulating flash. Currently the 
   older JEDEC ID mechanism and a table is used for determining the
   flash characterisitics, but it is trivial to add support for the
   CFI specification:
     http://www.pentium.com/design/flash/ in the technote section.
   
   ##################################################################### */
									/*}}}*/
#include <linux/mtd/mapped.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <asm/io.h>

struct JEDECTable mtd_JEDEC_table[] = 
  {{0x01AD,"AMD Am29F016",2*1024*1024,64*1024,MTD_CAP_NORFLASH},
   {0x01D5,"AMD Am29F080",1*1024*1024,64*1024,MTD_CAP_NORFLASH},
   {}};

// flash_setup - Setup the mapped_mtd_info structure for normal flash	/*{{{*/
// ---------------------------------------------------------------------
/* There is a set of commands that flash manufactures follow for getting the
   JEDEC id, erasing and writing. So long as your flash device supports 
   getting the JEDEC ID in this (standard?) way it will be supported as flash,
   otherwise it is converted to ROM. Upon completion the structure is 
   registered with the MTD layer */
int mtd_mapped_setup(struct mapped_mtd_info *map)
{
   DEBUG(1, "\n");
   // Must define a page function to use the defaults!
   if (map->page == 0)
      return -1;
   
   if (map->jedec_sense == 0)
      map->jedec_sense = flash_jedec;

   if (map->jedec_sense(map) != 0)
      return -1;

   if (map->mtd.erase == 0 && map->mtd.type == MTD_NORFLASH)
      map->mtd.erase = flash_erase;
   if (map->mtd.write == 0)
   {
      if (map->mtd.type == MTD_NORFLASH)
	 map->mtd.write = flash_write;
      if (map->mtd.type == MTD_RAM)
	 map->mtd.write = ram_write;      
   }   
   if (map->mtd.read == 0)
      map->mtd.read = rom_read;   
   
   return add_mtd_device(&map->mtd);
}
									/*}}}*/
// flash_remove - Remove the flash device from the MTD layer		/*{{{*/
// ---------------------------------------------------------------------
/* Free any memory allocated for the device here */
int mtd_mapped_remove(struct mapped_mtd_info *map)
{
   return del_mtd_device(&map->mtd);
}
									/*}}}*/

// checkparity - Checks a number for odd parity				/*{{{*/
// ---------------------------------------------------------------------
/* Helper for the JEDEC function, JEDEC numbers all have odd parity */
static int checkparity(u_char C)
{
   u_char parity = 0;
   while (C != 0)
   {
      parity ^= C & 1;
      C >>= 1;
   }
   
   return parity == 1;
}
									/*}}}*/
// SetJedec - Set the jedec information for a chip			/*{{{*/
// ---------------------------------------------------------------------
/* We track the configuration of each chip separately in the chip list, 
   each chip can have a different type and configuration to allow for 
   maximum flexability. */
void set_jedec(struct mapped_mtd_info *map,unsigned chip,unsigned char mfr,
	      unsigned char id)
{
   unsigned long longID = (mfr << 8) + id;
   unsigned int I;
   
   map->mtd.type = MTD_NORFLASH;
   map->mfr = mfr;
   map->id = id;

   // Locate the chip in the jedec table
   for (I = 0; mtd_JEDEC_table[I].jedec != 0; I++)
   {      
      if (mtd_JEDEC_table[I].jedec == longID)
	 break;
   }

   if (mtd_JEDEC_table[I].jedec != longID || longID == 0)
   {
      printk("Unknown JEDEC number %x-%x, treating as ROM\n",map->mfr,
	     map->id);
      map->mtd.type = MTD_ROM;
      return;
   }
      
   // Setup the MTD from the JEDEC information
//   map->mtd.size = mtd_JEDEC_table[I].size;
//   map->mtd.erasesize = mtd_JEDEC_table[I].sectorsize;
//   map->mtd.capabilities = mtd_JEDEC_table[I].capabilities;
//   strncpy(map->mtd.part,mtd_JEDEC_table[I].name,sizeof(map->mtd.part)-1);
   
   map->chips[chip].jedec = longID;
   map->chips[chip].size = mtd_JEDEC_table[I].size;
   map->chips[chip].sectorsize = mtd_JEDEC_table[I].sectorsize;
   map->chips[chip].capabilities = mtd_JEDEC_table[I].capabilities;
   map->chips[chip].base = 0; 
}
									/*}}}*/
// isjedec - Check if reading from the memory location gives jedec #s	/*{{{*/
// ---------------------------------------------------------------------
/* This is ment to be called on the flash window once it is in jedec mode */
int isjedec(unsigned long base)
{
   // Test #1, JEDEC numbers are readable from 0x??00/0x??01
   if (readb(base + 0) != readb(base + 0x100) || 
       readb(base + 1) != readb(base + 0x101))
      return 0;
   
   // Test #2 JEDEC numbers exhibit odd parity
   if (checkparity(readb(base + 0)) == 0 || checkparity(readb(base + 1)) == 0)
      return 0;
   return 1;
}
									/*}}}*/
// flash_jedec - JEDEC ID sensor					/*{{{*/
// ---------------------------------------------------------------------
/* The mysterious jedec flash probe sequence writes a specific pattern of
   bytes to the flash. This should be general enough to work with any MTD
   structure that may contain a flash chip, but note that it will corrupt
   address 0x5555 on SRAM cards if the machine dies between the two 
   critical operations. */
int flash_jedec(struct mapped_mtd_info *map)
{
   unsigned I;
   u_char OldVal;
   unsigned long base;
   unsigned long baseaddr = 0;
   unsigned chip = 0;
   unsigned count;
   
   // Who has a page size this small? :>
   if (map->pagesize < 0x555)
      return 1;
   
   base = map->page(map,0);
   
   // Wait for any write/erase operation to settle
   OldVal = readb(base);
   for (I = 0; OldVal != readb(base) && I < 10000; I++)
      OldVal = readb(base);
   
   /* Check for sram by writing to it, the write also happens to be part 
      of the flash reset sequence.. */
   OldVal = readb(base + 0x555);
   writeb(OldVal,base + 0x555);
   writeb(0xF0,base + 0x555);
   if (OldVal != readb(base + 0x555))
   {
      udelay(100);
      
      // Set it back and make sure..
      writeb(OldVal,base + 0x555);
      if (OldVal == readb(base + 0x555))
      {
	 map->mtd.type = MTD_RAM;
	 return 0;
      }
      
      writeb(0xF0,base + 0x555);
   }
   
   // Probe for chips
   while (chip < sizeof(map->chips)/sizeof(map->chips[0]))
   {
      // Already in jedec mode, we might be doing some address wrap around
      if (chip != 0 && isjedec(base) != 0)
      {
	 /* Try to reset this page and check if that resets the first page
	    to confirm */
	 writeb(0xF0,base + 0x555);
	 if (isjedec(base) != 0)
	    break;
	 base = map->page(map,0);
	 if (isjedec(base) == 0)
	    break;
	 base = map->page(map,baseaddr/map->pagesize);
      }
      
      // Send the sequence
      writeb(0xAA,base + 0x555);
      writeb(0x55,base + 0x2AA);
      writeb(0x90,base + 0x555);

      // Check the jedec number
      if (isjedec(base) == 0)
      {
	 /* If this is the first chip it must be rom, otherwise it is the
	    end of the flash region */
	 if (chip == 0)
	 {
	    map->mtd.type = MTD_ROM;
	    return 0;
	 }
	 break;
      }      
      
      // Store the jdec info
      set_jedec(map,chip,readb(base + 0),readb(base + 1));
      map->chips[chip].base = baseaddr;
      
      // Jump to the next chip
      baseaddr += map->chips[chip].size;
      if (baseaddr/map->pagesize > map->maxsize)
	 break;
      base = map->page(map,baseaddr/map->pagesize);
      if (base == 0)
	 return -EIO;
      
      chip++;
   }

   // Reset all of the chips
   map->mtd.size = 0;
   baseaddr = 0;
   map->mtd.flags = 0xFFFF;
   for (I = 0; map->chips[I].jedec != 0; I++)
   {
      // Fill in the various MTD structures
      map->mtd.size += map->chips[I].size;
      if (map->mtd.erasesize < map->chips[I].sectorsize)
	 map->mtd.erasesize = map->chips[I].sectorsize;
      map->mtd.flags &= map->chips[I].capabilities;
      
      base = map->page(map,baseaddr/map->pagesize);
      baseaddr += map->chips[chip].size;
      writeb(0xF0,base + 0);  // Reset      
   }   
   
   /* Generate a part name that includes the number of different chips and
      other configuration information */
   count = 1;
   map->part[0] = 0;
   for (I = 0; map->chips[I].jedec != 0; I++)
   {
      unsigned J;
      if (map->chips[I+1].jedec == map->chips[I].jedec)
      {
	 count++;
	 continue;
      }
      
      // Locate the chip in the jedec table
      for (J = 0; mtd_JEDEC_table[J].jedec != 0; J++)
      {      
	 if (mtd_JEDEC_table[J].jedec == map->chips[I].jedec)
	    break;
      }
      
      if (map->part[0] != 0)
	 strcat(map->part,",");
      
      if (count != 1)
	 sprintf(map->part+strlen(map->part),"%u*[%s]",count,
		 mtd_JEDEC_table[J].name);
      else
	 sprintf(map->part+strlen(map->part),"%s",
		 mtd_JEDEC_table[J].name);
      count = 1;
   }   
   return 0;
}
									/*}}}*/

// flash_failed - Print a console message about why the failure		/*{{{*/
// ---------------------------------------------------------------------
/* Pass the flags value that the flash return before it re-entered read 
   mode. */
static void flash_failed(unsigned char code)
{
   /* Bit 5 being high indicates that there was an internal device
      failure, erasure time limits exceeded or something */
   if ((code & (1 << 5)) != 0)
   {
      printk("mtd: Internal Flash failure\n");
      return;
   }
   printk("mtd: Programming didn't take\n");
}
									/*}}}*/
// flash_erase - Generic erase function					/*{{{*/
// ---------------------------------------------------------------------
/* This uses the erasure function described in the AMD Flash Handbook, 
   it will work for flashes with a fixed sector size only. Flashes with
   a selection of sector sizes (ie the AMD Am29F800B) will need a different
   routine. This routine tries to parallize erasing multiple chips/sectors 
   where possible */
int flash_erase(struct mtd_info *mtd, struct erase_info *instr)
{
   unsigned long Time = 0;
   unsigned long NoTime = 0;
   unsigned long start = instr->addr, len = instr->len;
   unsigned int I;
   struct mapped_mtd_info *map = (struct mapped_mtd_info *)mtd;

   // Verify the arguments..
   if (start + len > map->mtd.size ||
       (start % map->mtd.erasesize) != 0 ||
       (len % map->mtd.erasesize) != 0 ||
       (len/map->mtd.erasesize) == 0)
      return -EINVAL;
   
   flash_chip_scan(map,start,len);

   // Start the erase sequence on each chip
   for (I = 0; map->chips[I].jedec != 0; I++)
   {
      unsigned long off;
      struct flash_chip *chip = map->chips + I;
      unsigned long base;
      unsigned long flags;
      
      if (chip->length == 0)
	 continue;
      
      if (page_jump(map,chip->base + chip->start,0x555,&base,0) != 0)
	 return -EIO;
      
      // Send the erase setup code
      writeb(0xF0,base + 0x555);
      writeb(0xAA,base + 0x555);
      writeb(0x55,base + 0x2AA);
      writeb(0x80,base + 0x555);
      writeb(0xAA,base + 0x555);
      writeb(0x55,base + 0x2AA);

      // Use chip erase if possible
      if (chip->start == 0 && chip->length == chip->size)
      {
	 writeb(0x10,base+0x555);
	 continue;
      }
            
      /* Once we start selecting the erase sectors the delay between each 
         command must not exceed 50us or it will immediately start erasing 
         and ignore the other sectors */
      save_flags(flags);
      cli();
      for (off = 0; off < chip->length; off += chip->sectorsize)
      {
	 if (page_jump(map,chip->base + chip->start + off,1,&base,0) != 0)
	    return -EIO;
	 
	 // Check to make sure we didn't timeout
	 writeb(0x30,base);
	 if ((readb(base) & (1 << 3)) != 0)
	 {
	    printk("mtd: Ack! We timed out the erase timer!\n");
	    return -EIO;
	 }       	 
      }
      restore_flags(flags);
   }   

   /* We could split this into a timer routine and return early, performing
      background erasure.. Maybe later if the need warrents */
   
   /* Poll the flash for erasure completion, specs say this can take as long
      as 480 seconds to do all the sectors (for a 2 meg flash). 
      Erasure time is dependant on chip age, temp and wear.. */
   Time = 0;
   NoTime = 0;
   for (I = 0; map->chips[I].jedec != 0; I++)
   {
      struct flash_chip *chip = map->chips + I;
      unsigned long base;
      unsigned long off = 0;
      if (chip->length == 0)
	 continue;
      
      if (page_jump(map,chip->base + chip->start,1,&base,0) != 0)
	 return -EIO;
      
      while (1)
      {
	 unsigned char Last[4];
	 unsigned long Count = 0;
	 
	 /* During erase bit 7 is held low and bit 6 toggles, we watch this,
	    should it stop toggling or go high then the erase is completed,
  	    or this is not really flash ;> */
	 Last[0] = readb(base);
	 Last[1] = readb(base);
	 Last[2] = readb(base);
	 for (Count = 3; (Last[(Count - 1) % 4] & (1 << 7)) == 0 && 
	      Last[(Count - 1) % 4] != Last[(Count - 2) % 4]; Count++)
	 {
	    if (NoTime == 0)
	       Time += HZ/10 - schedule_timeout(HZ/10);
	    NoTime = 0;
	    
	    Last[Count % 4] = readb(base);
	 
	    // Count time, max of 15s per sector (according to AMD)
	    if (Time > 15*len/mtd->erasesize*HZ)
	    {
	       printk("mtd: Flash Erase Timed out\n");
	       return -EIO;
	    }	    
	 }
	 
	 if (Last[(Count - 1) % 4] == Last[(Count - 2) % 4])
	 {
	    flash_failed(Last[(Count - 3) % 4]);
	    return -EIO;
	 }
	 
	 // Skip to the next chip if we used chip erase
	 if (chip->length == chip->size)
	    off = chip->size;
	 else
	    off += chip->sectorsize;
	 
	 if (off >= chip->length)
	    break;
	 if (page_jump(map,chip->base + chip->start + off,1,&base,0) != 0)
	    return -EIO;	 
	 NoTime = 1;
      }      
   }
       	 
   // Paranoid verify of erasure
   {
      unsigned long base;
      unsigned long buflen;
      while (len > 0)
      {
	 unsigned long step;
	 
	 if (page_jump(map,start,len,&base,&buflen) != 0)
	    return -EIO;
	 start += buflen;
	 len -= buflen;
	 step = buflen/128;
	 for (;buflen != 0; buflen -= step)
	 {
	    if (readb(base+buflen-1) != 0xFF)
	    {
	       printk("mtd: Flash Erase didn't take %lu %lu %lu\n",buflen,len,start);
	       return -EIO;
	    }
	 }	 
      }      
   }   
   
   return 0;
}
#if 1
									/*}}}*/
// flash_write - Generic writing function				/*{{{*/
// ---------------------------------------------------------------------
/* This could do parallel writes on multiple chips but doesnt, memory 
   constraints make that infeasable. This should work with any sort of 
   linear flash that is not interleved */
extern int flash_write(struct mtd_info *mtd, loff_t start, size_t len,
		       size_t *retlen, const u_char *buf)
{
   struct mapped_mtd_info *map = (struct mapped_mtd_info *)mtd;
   unsigned long base;
   unsigned long off;
   DEBUG(1,"\n");
   if (start + len > mtd->size)
      return -EIO;
   
   while (len != 0)
   {
      // Compute the page offset and reposition
      base = map->page(map,(u_long)start/map->pagesize);
      off = (u_long)start %  map->pagesize;

      // Loop over this page
      for (; off != map->pagesize && len != 0; start++, len--, off++,buf++)
      {
	 unsigned char oldbyte = readb(base+off);
	 unsigned char Last[4];
	 unsigned long Count = 0;

	 if (oldbyte == *buf)
	    continue;
	 if (((~oldbyte) & *buf) != 0)
	    printk("mtd: warn: Trying to set a 0 to a 1\n");
	     
	 // Write
	 writeb(0xAA,base + 0x555);
	 writeb(0x55,base + 0x2AA);
	 writeb(0xA0,base + 0x555);
	 writeb(*buf,base + off);
	 Last[0] = readb(base + off);
	 Last[1] = readb(base + off);
	 Last[2] = readb(base + off);
	 
	 /* Wait for the flash to finish the operation. We store the last 4
	    status bytes that have been retrieved so we can determine why
	    it failed. The toggle bits keep toggling when there is a 
	    failure */
	 for (Count = 3; Last[(Count - 1) % 4] != Last[(Count - 2) % 4] &&
	      Count < 10000; Count++)
	    Last[Count % 4] = readb(base + off);
	 if (Last[(Count - 1) % 4] != *buf)
	 {
	    flash_failed(Last[(Count - 3) % 4]);
	    return -EIO;
	 }	 
      }
   }
   *retlen = len;
   return 0;
}
#endif

// ram_write - Generic writing function	for ram				/*{{{*/
// ---------------------------------------------------------------------
/* */
extern int ram_write(struct mtd_info *mtd, loff_t start, size_t len,
		       size_t *retlen, const u_char *buf)
{
   struct mapped_mtd_info *map = (struct mapped_mtd_info *)mtd;
   unsigned long base;
   size_t origlen = len;
   unsigned long buflen;
   DEBUG(1,"\n");
   if (start + len > mtd->size)
      return -EIO;
   
   while (len != 0)
   {
      // Reposition..
      if (page_jump(map,start,len,&base,&buflen) != 0)
	 return -EIO;
      
      // Copy
      memcpy_toio(base,buf,buflen);
      len -= buflen;
      start += buflen;
   }
   *retlen = origlen;
   return 0;
}

// rom_read - Read handler for any sort of device			/*{{{*/
// ---------------------------------------------------------------------
/* This is a generic read function that should work with any device in the
   mapped region. */
extern int rom_read(struct mtd_info *mtd, loff_t start, size_t len,
		    size_t *retlen, u_char *buf)
{
   struct mapped_mtd_info *map = (struct mapped_mtd_info *)mtd;
   size_t origlen = len;
   unsigned long base;
   unsigned long buflen;

   printk("Rom_Read\n");
   if (start + len > mtd->size)
      return -EIO;
   
   while (len != 0)
   {
      // Reposition..
      if (page_jump(map,start,len,&base,&buflen) != 0)
	 return -EIO;
      
      // Copy
      memcpy_fromio(buf,base,buflen);
      len -= buflen;
      start += buflen;
   }
   *retlen = origlen;
   return 0;
}

// page_jump - Move the window and return the buffer			/*{{{*/
// ---------------------------------------------------------------------
/* Unlike the page function this returns a buffer and length adjusted for
   the page dimensions and the reading offset into the page, simplifies
   many of the other routines */
int page_jump(struct mapped_mtd_info *map,unsigned long start,
	      unsigned long len,unsigned long *base,
	      unsigned long *retlen)
{
   DEBUG(1,"Page Jump\n");
   if (start > map->mtd.size || start + len > map->mtd.size)
      return -EINVAL;
      
   *base = map->page(map,start/map->pagesize);
   if (*base == 0)
      return -EIO;

   *base += start % map->pagesize;

   // If retlen is 0 that mean the caller requires len bytes, no quibbling.
   if (retlen == 0)
   {
      if (len > map->pagesize  - (start % map->pagesize))
	 return -EIO;
      return 0;
   }
   
   // Compute the buffer paramaters and return
   if (len > map->pagesize - (start % map->pagesize))
      *retlen = map->pagesize - (start % map->pagesize);
   else
      *retlen = len;
   return 0;
}
									/*}}}*/
// flash_chip_scan - Intersect a region with the flash chip structure	/*{{{*/
// ---------------------------------------------------------------------
/* This is used to enhance the speed of the erase routine,
   when things are being done to multiple chips it is possible to
   parallize the operations, particularly full memory erases of multi
   chip memories benifit */

void flash_chip_scan(struct mapped_mtd_info *map,unsigned long start,
		     unsigned long len)
{
   unsigned int I = 0;

   DEBUG(1,"\n");
   // Zero the records
   for (I = 0; map->chips[I].jedec != 0; I++)
      map->chips[I].start = map->chips[I].length = 0;
   
   // Intesect our region with the chip structures
   for (I = 0; map->chips[I].jedec != 0 && len != 0; I++)
   {
      // Havent found the start yet
      if (start >= map->chips[I].base + map->chips[I].size)
	 continue;

      // Store the portion of this chip that is being effected
      map->chips[I].start = start - map->chips[I].base;
      if (len <= map->chips[I].size - map->chips[I].start)
	 map->chips[I].length = len;
      else
	 map->chips[I].length = map->chips[I].size - map->chips[I].start;
      len -= map->chips[I].length;
      start = map->chips[I].base + map->chips[I].size;
   }
}
									/*}}}*/


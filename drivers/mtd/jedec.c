
/* JEDEC Flash Interface.
 * This is an older type of interface for self programming flash. It is 
 * commonly use in older AMD chips and is obsolete compared with CFI.
 * It is called JEDEC because the JEDEC association distributes the ID codes
 * for the chips.
 *
 * See the AMD flash databook for information on how to operate the interface.
 *
 * This code does not support anything wider than 8 bit flash chips, I am
 * not going to guess how to send commands to them, plus I expect they will
 * all speak CFI..
 *
 * $Id: jedec.c,v 1.1 2000/07/04 07:21:57 jgg Exp $
 */

#include <linux/mtd/jedec.h>

struct mtd_info *jedec_probe(struct map_info *);
int jedec_probe8(struct map_info *map,unsigned long base,
		  struct jedec_private *priv);
int jedec_probe16(struct map_info *map,unsigned long base,
		  struct jedec_private *priv);
int jedec_probe32(struct map_info *map,unsigned long base,
		  struct jedec_private *priv);
static void jedec_flash_chip_scan(struct jedec_private *priv,unsigned long start,
			    unsigned long len);
static int flash_erase(struct mtd_info *mtd, struct erase_info *instr);
static int flash_write(struct mtd_info *mtd, loff_t start, size_t len,
		       size_t *retlen, const u_char *buf);
   
EXPORT_SYMBOL(jedec_probe);

/* Listing of parts and sizes. We need this table to learn the sector
   size of the chip and the total length */
static const struct JEDECTable JEDEC_table[] = 
  {{0x01AD,"AMD Am29F016",2*1024*1024,64*1024,MTD_CAP_NORFLASH},
   {0x01D5,"AMD Am29F080",1*1024*1024,64*1024,MTD_CAP_NORFLASH},
   {0x01A4,"AMD Am29F040",512*1024,64*1024,MTD_CAP_NORFLASH},
   {}};

static void jedec_sync(struct mtd_info *mtd) {};
static int jedec_read(struct mtd_info *mtd, loff_t from, size_t len, 
		      size_t *retlen, u_char *buf);
static int jedec_read_banked(struct mtd_info *mtd, loff_t from, size_t len, 
			     size_t *retlen, u_char *buf);

/* Probe entry point */
   struct jedec_private priv;
   struct mtd_info __MTD;
struct mtd_info *jedec_probe(struct map_info *map)
{
   struct mtd_info *MTD = &__MTD;
   unsigned long Base;
   unsigned long SectorSize;
   unsigned count;
   unsigned I,Uniq;
   char Part[200];
   memset(&priv,0,sizeof(priv));
   
   if (map->bank_size == 0)
      map->bank_size = map->size;

   if (map->size/map->bank_size > MAX_JEDEC_CHIPS)
   {
      printk("mtd: Increase MAX_JEDEC_CHIPS, too many banks.\n");
      return 0;
   }
   
   for (Base = 0; Base < map->size; Base += map->bank_size)
   {
      // Perhaps zero could designate all tests?
      if (map->bus_width == 0)
	 map->bus_width = 8;
      
      if (map->bus_width == 8)
	 jedec_probe8(map,Base,&priv);
      if (map->bus_width == 16)
	 jedec_probe16(map,Base,&priv);
      if (map->bus_width == 32)
	 jedec_probe32(map,Base,&priv);
   }
   
   // Get the biggest sector size
   SectorSize = 0;
   for (I = 0; priv.chips[I].jedec != 0 && I < MAX_JEDEC_CHIPS; I++)
   {
      if (priv.chips[I].sectorsize > SectorSize)
	 SectorSize = priv.chips[I].sectorsize;
   }
   
   // Quickly ensure that the other sector sizes are factors of the largest
   for (I = 0; priv.chips[I].jedec != 0 && I < MAX_JEDEC_CHIPS; I++)
   {
      if ((SectorSize/priv.chips[I].sectorsize)*priv.chips[I].sectorsize != SectorSize)
      {
	 printk("mtd: Failed. Device has incompatible mixed sector sizes\n");
	 return 0;
      }      
   }
   
   /* Generate a part name that includes the number of different chips and
      other configuration information */
   count = 1;
   strncpy(Part,map->name,sizeof(Part)-10);
   Part[sizeof(Part)-11] = 0;
   strcat(Part," ");
   Uniq = 0;
   for (I = 0; priv.chips[I].jedec != 0 && I < MAX_JEDEC_CHIPS; I++)
   {
      const struct JEDECTable *JEDEC;
      
      if (priv.chips[I+1].jedec == priv.chips[I].jedec)
      {
	 count++;
	 continue;
      }
      
      // Locate the chip in the jedec table
      JEDEC = jedec_idtoinf(priv.chips[I].jedec >> 8,priv.chips[I].jedec);
      if (JEDEC == 0)
      {
	 printk("mtd: Internal Error, JEDEC not set\n");
	 return 0;
      }
      
      if (Uniq != 0)
	 strcat(Part,",");
      Uniq++;
      
      if (count != 1)
	 sprintf(Part+strlen(Part),"%x*[%s]",count,JEDEC->name);
      else
	 sprintf(Part+strlen(Part),"%s",JEDEC->name);
      if (strlen(Part) > sizeof(Part)*2/3)
	 break;
      count = 1;
   }   

   /* Determine if the chips are organized in a linear fashion, or if there
      are empty banks. Note, the last bank does not count here, only the
      first banks are important. Holes on non-bank boundaries can not exist
      due to the way the detection algorithm works. */
   if (priv.size < map->bank_size)
      map->bank_size = priv.size;
   priv.is_banked = 0;
   for (I = 0; I != priv.size/map->bank_size - 1; I++)
   {
      if (priv.bank_fill[I] != map->bank_size)
	 priv.is_banked = 1;
      
      /* This even could be eliminated, but new de-optimized read/write
         functions have to be written */
      if (priv.bank_fill[I] != priv.bank_fill[0])
      {
	 printk("mtd: Failed. Cannot handle unsymetric banking\n");
	 return 0;
      }      
   }
   if (priv.is_banked == 1)
      strcat(Part,", banked");

   xprintf("Part: '%s'\n",Part);
   
   memset(MTD,0,sizeof(*MTD));
   strncpy(MTD->name,Part,sizeof(MTD->name));
   MTD->name[sizeof(MTD->name)-1] = 0;
   MTD->type = MTD_NORFLASH;
   MTD->flags = MTD_CAP_NORFLASH;
   MTD->erasesize = SectorSize*(map->bus_width/8);
   MTD->size = priv.size;
   //MTD->module = THIS_MODULE; // ? Maybe this should be the low level module?
   MTD->erase = flash_erase;
   if (priv.is_banked == 1)
      MTD->read = jedec_read_banked;
   else
      MTD->read = jedec_read;
   MTD->write = flash_write;
   MTD->sync = jedec_sync;
   MTD->priv = map;
   map->fldrv_priv = &priv;
   
   return MTD;
}

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


/* Take an array of JEDEC numbers that represent interleved flash chips
   and process them. Check to make sure they are good JEDEC numbers, look
   them up and then add them to the chip list */   
int handle_jedecs(struct map_info *map,__u8 *Mfg,__u8 *Id,unsigned Count,
		  unsigned long base,struct jedec_private *priv)
{
   unsigned I,J;
   unsigned long Size;
   unsigned long SectorSize;
   const struct JEDECTable *JEDEC;

   // Test #2 JEDEC numbers exhibit odd parity
   for (I = 0; I != Count; I++)
   {
      if (checkparity(Mfg[I]) == 0 || checkparity(Id[I]) == 0)
	 return 0;
   }
   
   // Finally, just make sure all the chip sizes are the same
   JEDEC = jedec_idtoinf(Mfg[0],Id[0]);
   
   if (JEDEC == 0)
   {
      printk("mtd: Found JEDEC flash chip, but do not have a table entry for %x:%x\n",Mfg[0],Mfg[1]);
      return 0;
   }
   
   Size = JEDEC->size;
   SectorSize = JEDEC->sectorsize;
   for (I = 0; I != Count; I++)
   {
      JEDEC = jedec_idtoinf(Mfg[0],Id[0]);
      if (JEDEC == 0)
      {
	 printk("mtd: Found JEDEC flash chip, but do not have a table entry for %x:%x\n",Mfg[0],Mfg[1]);
	 return 0;
      }

      if (Size != JEDEC->size || SectorSize != JEDEC->sectorsize)
      {
	 printk("mtd: Failed. Interleved flash does not have matching characteristics\n");
	 return 0;
      }      
   }

   // Load the Chips
   for (I = 0; I != MAX_JEDEC_CHIPS; I++)
   {
      if (priv->chips[I].jedec == 0)
	 break;
   }

   if (I + Count > MAX_JEDEC_CHIPS)
   {
      printk("mtd: Device has too many chips. Increase MAX_JEDEC_CHIPS\n");
      return 0;
   }      
   
   // Add them to the table
   for (J = 0; J != Count; J++)
   {
      unsigned long Bank;
	 
      JEDEC = jedec_idtoinf(Mfg[J],Id[J]);
      priv->chips[I].jedec = (Mfg[J] << 8) | Id[J];
      priv->chips[I].size = JEDEC->size;
      priv->chips[I].sectorsize = JEDEC->sectorsize;
      priv->chips[I].base = base + J;
      priv->chips[I].datashift = J*8;
      priv->chips[I].capabilities = JEDEC->capabilities;
      priv->chips[I].offset = priv->size + J;

      // log2 n :|
      priv->chips[I].addrshift = 0;
      for (Bank = Count; Bank != 1; Bank >>= 1, priv->chips[I].addrshift++);
      
      // Determine how filled this bank is.
      Bank = base & (~(map->bank_size-1));
      if (priv->bank_fill[Bank/map->bank_size] < base + 
	  (JEDEC->size << priv->chips[I].addrshift) - Bank)
	 priv->bank_fill[Bank/map->bank_size] =  base + (JEDEC->size << priv->chips[I].addrshift) - Bank;
      I++;
   }

   priv->size += priv->chips[I-1].size*Count;
	 
   return priv->chips[I-1].size;
}

/* Lookup the chip information from the JEDEC ID table. */
const struct JEDECTable *jedec_idtoinf(__u8 mfr,__u8 id)
{
   __u16 Id = (mfr << 8) | id;
   unsigned long I = 0;
   for (I = 0; JEDEC_table[I].jedec != 0; I++)
      if (JEDEC_table[I].jedec == Id)
	 return JEDEC_table + I;
   return 0;
}

// Look for flash using an 8 bit bus interface
int jedec_probe8(struct map_info *map,unsigned long base,
		  struct jedec_private *priv)
{ 
   return 0;
}

// Look for flash using a 16 bit bus interface (ie 2 8-bit chips)
int jedec_probe16(struct map_info *map,unsigned long base,
		  struct jedec_private *priv)
{
   return 0;
}

// Look for flash using a 32 bit bus interface (ie 4 8-bit chips)
int jedec_probe32(struct map_info *map,unsigned long base,
		  struct jedec_private *priv)
{
   #define flread(x) map->read32(map,base+((x)<<2))
   #define flwrite(v,x) map->write32(map,v,base+((x)<<2))

   const unsigned long AutoSel1 = 0xAAAAAAAA;
   const unsigned long AutoSel2 = 0x55555555;
   const unsigned long AutoSel3 = 0x90909090;
   const unsigned long Reset = 0x90909090;
   __u32 OldVal;
   __u8 Mfg[4];
   __u8 Id[4];
   unsigned I;
   unsigned long Size;

   // Wait for any write/erase operation to settle
   OldVal = flread(base);
   for (I = 0; OldVal != flread(base) && I < 10000; I++)
      OldVal = flread(base);
   
   // Reset the chip
   flwrite(Reset,0x555); 
   
   // Send the sequence
   flwrite(AutoSel1,0x555);
   flwrite(AutoSel2,0x2AA);
   flwrite(AutoSel3,0x555);
   
   // Test #1, JEDEC numbers are readable from 0x??00/0x??01
   if (flread(0) != flread(0x100) || 
       flread(1) != flread(0x101))
   {
      flwrite(Reset,0x555);
      return 0;
   }

   // Split up the JEDEC numbers
   OldVal = flread(0);
   for (I = 0; I != 4; I++)
      Mfg[I] = (OldVal >> (I*8));
   OldVal = flread(1);
   for (I = 0; I != 4; I++)
      Id[I] = (OldVal >> (I*8));
      
   Size = handle_jedecs(map,Mfg,Id,4,base,priv);
   if (Size == 0)
   {
      flwrite(Reset,0x555);
      return 0;
   }
   
   /* Check if there is address wrap around within a single bank, if this
      returns JEDEC numbers then we assume that it is wrap around. Notice
      we call this routine with the JEDEC return still enabled, if two or
      more flashes have a truncated address space the probe test will still
      work */
   if (base + Size+0x555 < map->size &&
       base + Size+0x555 < (base & (~(map->bank_size-1))) + map->bank_size)
   {
      if (flread(base+Size) != flread(base+Size + 0x100) ||
	  flread(base+Size + 1) != flread(base+Size + 0x101))
      {
	 jedec_probe32(map,base+Size,priv);
      }
   }

   // Reset.
   flwrite(0xF0F0F0F0,0x555);
   
   return 1;
   
   #undef flread
   #undef flwrite
}

/* Linear read. */
static int jedec_read(struct mtd_info *mtd, loff_t from, size_t len, 
		      size_t *retlen, u_char *buf)
{
   struct map_info *map = (struct map_info *)mtd->priv;
   
   map->copy_from(map, buf, from, len);
   *retlen = len;
   return 0;   
}

/* Banked read. Take special care to jump past the holes in the bank
   mapping. This version assumes symetry in the holes.. */
static int jedec_read_banked(struct mtd_info *mtd, loff_t from, size_t len, 
			     size_t *retlen, u_char *buf)
{
   struct map_info *map = (struct map_info *)mtd->priv;
   struct jedec_private *priv = (struct jedec_private *)map->fldrv_priv;

   *retlen = 0;
   while (len > 0)
   {
      // Determine what bank and offset into that bank the first byte is
      unsigned long bank = from & (~(priv->bank_fill[0]-1));
      unsigned long offset = from & (priv->bank_fill[0]-1);
      unsigned long get = len;
      if (priv->bank_fill[0] - offset < len)
	 get = priv->bank_fill[0] - offset;

      bank /= priv->bank_fill[0];      
      map->copy_from(map,buf + *retlen,bank*map->bank_size + offset,get);
      
      len -= get;
      *retlen += get;
      from += get;
   }   
   return 0;   
}

/* Pass the flags value that the flash return before it re-entered read 
   mode. */
static void jedec_flash_failed(unsigned char code)
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

/* This uses the erasure function described in the AMD Flash Handbook, 
   it will work for flashes with a fixed sector size only. Flashes with
   a selection of sector sizes (ie the AMD Am29F800B) will need a different
   routine. This routine tries to parallize erasing multiple chips/sectors 
   where possible */
static int flash_erase(struct mtd_info *mtd, struct erase_info *instr)
{
   // Does IO to the currently selected chip
   #define flread(x) map->read8(map,chip->base+((x)<<chip->addrshift))
   #define flwrite(v,x) map->write8(map,v,chip->base+((x)<<chip->addrshift))
   
   unsigned long Time = 0;
   unsigned long NoTime = 0;
   unsigned long start = instr->addr, len = instr->len;
   unsigned int I;
   struct map_info *map = (struct map_info *)mtd->priv;
   struct jedec_private *priv = (struct jedec_private *)map->fldrv_priv;

   // Verify the arguments..
   if (start + len > mtd->size ||
       (start % mtd->erasesize) != 0 ||
       (len % mtd->erasesize) != 0 ||
       (len/mtd->erasesize) == 0)
      return -EINVAL;
   
   jedec_flash_chip_scan(priv,start,len);

   // Start the erase sequence on each chip
   for (I = 0; priv->chips[I].jedec != 0 && I < MAX_JEDEC_CHIPS; I++)
   {
      unsigned long off;
      struct jedec_flash_chip *chip = priv->chips + I;
      
      if (chip->length == 0)
	 continue;
      
      // Send the erase setup code
      xprintf("Erase: ");
      puth(chip->start); putc(' ');
      puth(chip->base); putc(' ');
      puth(chip->length); putc(' ');
      puth(chip->sectorsize); putc('\n');
      
      if (chip->start + chip->length > chip->size)
      {
	 xprintf("DIE\n");
	 return -EIO;
      }     
      
      flwrite(0xF0,chip->start + 0x555);
      flwrite(0xAA,chip->start + 0x555);
      flwrite(0x55,chip->start + 0x2AA);
      flwrite(0x80,chip->start + 0x555);
      flwrite(0xAA,chip->start + 0x555);
      flwrite(0x55,chip->start + 0x2AA);

      // Use chip erase if possible
      if (chip->start == 0 && chip->length == chip->size)
      {
	 flwrite(0x10,0x555);
	 continue;
      }
            
      /* Once we start selecting the erase sectors the delay between each 
         command must not exceed 50us or it will immediately start erasing 
         and ignore the other sectors */
/*    how do you portably turn off interrupts?
      save_flags(flags);
      cli();*/
      for (off = 0; off < chip->length; off += chip->sectorsize)
      {
	 // Check to make sure we didn't timeout
	 flwrite(0x30,chip->start + off);
	 if (off == 0)
	    continue;
	 if ((flread(chip->start + off) & (1 << 3)) != 0)
	 {
	    printk("mtd: Ack! We timed out the erase timer!\n");
	    return -EIO;
	 }       	 
      }
//      restore_flags(flags);
   }   

   /* We could split this into a timer routine and return early, performing
      background erasure.. Maybe later if the need warrents */

   /* Poll the flash for erasure completion, specs say this can take as long
      as 480 seconds to do all the sectors (for a 2 meg flash). 
      Erasure time is dependant on chip age, temp and wear.. */
   
   /* This being a generic routine assumes a 32 bit bus. It does read32s
      and bundles interleved chips into the same grouping. This will work 
      for all bus widths */
   Time = 0;
   NoTime = 0;
   for (I = 0; priv->chips[I].jedec != 0 && I < MAX_JEDEC_CHIPS; I++)
   {
      struct jedec_flash_chip *chip = priv->chips + I;
      unsigned long off = 0;
      unsigned todo[4] = {0,0,0,0};
      unsigned todo_left = 0;
      unsigned J;
      
      if (chip->length == 0)
	 continue;

      /* Find all chips in this data line, realistically this is all 
         or nothing up to the interleve count */
      for (J = 0; priv->chips[J].jedec != 0 && J < MAX_JEDEC_CHIPS; J++)
      {
	 if ((priv->chips[J].base & (~((1<<chip->addrshift)-1))) == 
	     (chip->base & (~((1<<chip->addrshift)-1))))
	 {
	    todo_left++;
	    todo[priv->chips[J].base & ((1<<chip->addrshift)-1)] = 1;
	 }	 
      }

      xprintf("todo: %x %x %x %x\n",(short)todo[0],(short)todo[1],
	      (short)todo[2],(short)todo[3]);
      
      while (1)
      {
	 __u32 Last[4];
	 unsigned long Count = 0;
	 
	 /* During erase bit 7 is held low and bit 6 toggles, we watch this,
	    should it stop toggling or go high then the erase is completed,
  	    or this is not really flash ;> */
	 Last[0] = map->read32(map,(chip->base >> chip->addrshift) + chip->start + off);
	 Last[1] = map->read32(map,(chip->base >> chip->addrshift) + chip->start + off);
	 Last[2] = map->read32(map,(chip->base >> chip->addrshift) + chip->start + off);
	 Count = 3;
	 while (todo_left != 0)
	 {
	    for (J = 0; J != 4; J++)
	    {
	       __u8 Byte1 = (Last[(Count-1)%4] >> (J*8)) & 0xFF;
	       __u8 Byte2 = (Last[(Count-2)%4] >> (J*8)) & 0xFF;
	       __u8 Byte3 = (Last[(Count-3)%4] >> (J*8)) & 0xFF;
	       if (todo[J] == 0)
		  continue;
	       
	       if ((Byte1 & (1 << 7)) == 0 && Byte1 != Byte2)
	       {
//		  printk("Check %x %x %x\n",(short)J,(short)Byte1,(short)Byte2);
		  continue;
	       }
	       
	       if (Byte1 == Byte2)
	       {
		  jedec_flash_failed(Byte3);
		  return -EIO;
	       }
	       
	       todo[J] = 0;
	       todo_left--;
	    }
	    
/*	    if (NoTime == 0)
	       Time += HZ/10 - schedule_timeout(HZ/10);*/
	    NoTime = 0;
	    
	    Last[Count % 4] = map->read32(map,(chip->base >> chip->addrshift) + chip->start + off);
	    Count++;
	    
	    putc('.');
	    
/*	    // Count time, max of 15s per sector (according to AMD)
	    if (Time > 15*len/mtd->erasesize*HZ)
	    {
	       printk("mtd: Flash Erase Timed out\n");
	       return -EIO;
	    }	    */
	 }
	 	 
	 puts("out\n");
	    
	 // Skip to the next chip if we used chip erase
	 if (chip->length == chip->size)
	    off = chip->size;
	 else
	    off += chip->sectorsize;
	 
	 if (off >= chip->length)
	    break;
	 NoTime = 1;
      }
      
      for (J = 0; priv->chips[J].jedec != 0 && J < MAX_JEDEC_CHIPS; J++)
      {
	 if ((priv->chips[J].base & (~((1<<chip->addrshift)-1))) ==
	     (chip->base & (~((1<<chip->addrshift)-1))))
	    priv->chips[J].length = 0;
      }      
   }
       	    
   puts("done\n");
   return 0;
   
   #undef flread
   #undef flwrite
}

/* This is the simple flash writing function. It writes to every byte, in
   sequence. It takes care of how to properly address the flash if
   the flash is interleved. It can only be used if all the chips in the 
   array are identical!*/
static int flash_write(struct mtd_info *mtd, loff_t start, size_t len,
		       size_t *retlen, const u_char *buf)
{
   /* Does IO to the currently selected chip. It takes the bank addressing
      base (which is divisable by the chip size) adds the necesary lower bits
      of addrshift (interleve index) and then adds the control register index. */
   #define flread(x) map->read8(map,base+(off&((1<<chip->addrshift)-1))+((x)<<chip->addrshift))
   #define flwrite(v,x) map->write8(map,v,base+(off&((1<<chip->addrshift)-1))+((x)<<chip->addrshift))
   
   struct map_info *map = (struct map_info *)mtd->priv;
   struct jedec_private *priv = (struct jedec_private *)map->fldrv_priv;
   unsigned long base;
   unsigned long off;
   
   if (start + len > mtd->size)
      return -EIO;
   
   puts("Here");
   
   while (len != 0)
   {
      struct jedec_flash_chip *chip = priv->chips;
      unsigned long bank;
      unsigned long boffset;
	 
      // Compute the base of the flash.
      off = start % (chip->size << chip->addrshift);
      base = start - off;

      // Perform banked addressing translation.
      bank = base & (~(priv->bank_fill[0]-1));
      boffset = base & (priv->bank_fill[0]-1);
      bank = (bank/priv->bank_fill[0])*map->bank_size;
      base = bank + boffset;
      
      xprintf("Flasing %X %X %X\n",base,chip->size,len);
      
      // Loop over this page
      for (; off != (chip->size << chip->addrshift) && len != 0; start++, len--, off++,buf++)
      {
	 unsigned char oldbyte = map->read8(map,base+off);
	 unsigned char Last[4];
	 unsigned long Count = 0;

//	 putc('.');
	 
	 if (oldbyte == *buf)
	    continue;
	 if (((~oldbyte) & *buf) != 0)
	    printk("mtd: warn: Trying to set a 0 to a 1\n");
	     
	 // Write
	 flwrite(0xAA,0x555);
	 flwrite(0x55,0x2AA);
	 flwrite(0xA0,0x555);
	 map->write8(map,*buf,base + off);
	 Last[0] = map->read8(map,base + off);
	 Last[1] = map->read8(map,base + off);
	 Last[2] = map->read8(map,base + off);
	 
	 /* Wait for the flash to finish the operation. We store the last 4
	    status bytes that have been retrieved so we can determine why
	    it failed. The toggle bits keep toggling when there is a 
	    failure */
	 for (Count = 3; Last[(Count - 1) % 4] != Last[(Count - 2) % 4] &&
	      Count < 10000; Count++)
	    Last[Count % 4] = map->read8(map,base + off);
	 if (Last[(Count - 1) % 4] != *buf)
	 {
	    jedec_flash_failed(Last[(Count - 3) % 4]);
	    return -EIO;
	 }	 
      }
   }
   *retlen = len;
   return 0;
}

/* This is used to enhance the speed of the erase routine,
   when things are being done to multiple chips it is possible to
   parallize the operations, particularly full memory erases of multi
   chip memories benifit */
static void jedec_flash_chip_scan(struct jedec_private *priv,unsigned long start,
		     unsigned long len)
{
   unsigned int I;

   // Zero the records
   for (I = 0; priv->chips[I].jedec != 0 && I < MAX_JEDEC_CHIPS; I++)
      priv->chips[I].start = priv->chips[I].length = 0;
   
   // Intersect the region with each chip
   for (I = 0; priv->chips[I].jedec != 0 && I < MAX_JEDEC_CHIPS; I++)
   {
      struct jedec_flash_chip *chip = priv->chips + I;
      unsigned long ByteStart;
      unsigned long ChipEndByte = chip->offset + (chip->size << chip->addrshift);
      
      // End is before this chip or the start is after it
      if (start+len < chip->offset ||
	  ChipEndByte - (1 << chip->addrshift) < start)
	 continue;
      
      if (start < chip->offset)
      {
	 ByteStart = chip->offset;
	 chip->start = 0;
      }      
      else
      {
	 chip->start = (start - chip->offset + (1 << chip->addrshift)-1) >> chip->addrshift;
	 ByteStart = start;
      }

      if (start + len >= ChipEndByte)
	 chip->length = (ChipEndByte - ByteStart) >> chip->addrshift;
      else
	 chip->length = (start + len - ByteStart + (1 << chip->addrshift)-1) >> chip->addrshift;
   }
}
									/*}}}*/

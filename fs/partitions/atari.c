/*
 *  fs/partitions/atari.c
 *
 *  Code extracted from drivers/block/genhd.c
 *
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 */

#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <linux/ctype.h>

#include <asm/byteorder.h>
#include <asm/system.h>

#include "check.h"
#include "atari.h"

/* ++guenther: this should be settable by the user ("make config")?.
 */
#define ICD_PARTS

/* check if a partition entry looks valid -- Atari format is assumed if at
   least one of the primary entries is ok this way */
#define	VALID_PARTITION(pi,hdsiz)					     \
    (((pi)->flg & 1) &&							     \
     isalnum((pi)->id[0]) && isalnum((pi)->id[1]) && isalnum((pi)->id[2]) && \
     be32_to_cpu((pi)->st) <= (hdsiz) &&				     \
     be32_to_cpu((pi)->st) + be32_to_cpu((pi)->siz) <= (hdsiz))

int atari_partition (struct gendisk *hd, kdev_t dev,
		     unsigned long first_sector, int minor)
{
  int m_lim = minor + hd->max_p;
  struct buffer_head *bh;
  struct rootsector *rs;
  struct partition_info *pi;
  u32 extensect;
  u32 hd_size;
#ifdef ICD_PARTS
  int part_fmt = 0; /* 0:unknown, 1:AHDI, 2:ICD/Supra */
#endif

  bh = bread (dev, 0, get_ptable_blocksize(dev));
  if (!bh) {
      if (warn_no_part) printk (" unable to read block 0 (partition table)\n");
      return -1;
  }

  /* Verify this is an Atari rootsector: */
  rs = (struct rootsector *) bh->b_data;
  hd_size = hd->part[minor - 1].nr_sects;
  if (!VALID_PARTITION(&rs->part[0], hd_size) &&
      !VALID_PARTITION(&rs->part[1], hd_size) &&
      !VALID_PARTITION(&rs->part[2], hd_size) &&
      !VALID_PARTITION(&rs->part[3], hd_size)) {
      /* if there's no valid primary partition, assume that no Atari
	 format partition table (there's no reliable magic or the like
	 :-() */
      brelse(bh);
      return 0;
  }

  pi = &rs->part[0];
  printk (" AHDI");
  for (; pi < &rs->part[4] && minor < m_lim; minor++, pi++)
    {
      if (pi->flg & 1)
	/* active partition */
	{
	  if (memcmp (pi->id, "XGM", 3) == 0)
	    /* extension partition */
	    {
	      struct rootsector *xrs;
	      struct buffer_head *xbh;
	      ulong partsect;

#ifdef ICD_PARTS
	      part_fmt = 1;
#endif
	      printk(" XGM<");
	      partsect = extensect = be32_to_cpu(pi->st);
	      while (1)
		{
		  xbh = bread (dev, partsect / 2, get_ptable_blocksize(dev));
		  if (!xbh)
		    {
		      printk (" block %ld read failed\n", partsect);
		      brelse(bh);
		      return 0;
		    }
		  if (partsect & 1)
		    xrs = (struct rootsector *) &xbh->b_data[512];
		  else
		    xrs = (struct rootsector *) &xbh->b_data[0];

		  /* ++roman: sanity check: bit 0 of flg field must be set */
		  if (!(xrs->part[0].flg & 1)) {
		    printk( "\nFirst sub-partition in extended partition is not valid!\n" );
		    break;
		  }

		  add_gd_partition(hd, minor,
				   partsect + be32_to_cpu(xrs->part[0].st),
				   be32_to_cpu(xrs->part[0].siz));

		  if (!(xrs->part[1].flg & 1)) {
		    /* end of linked partition list */
		    brelse( xbh );
		    break;
		  }
		  if (memcmp( xrs->part[1].id, "XGM", 3 ) != 0) {
		    printk( "\nID of extended partition is not XGM!\n" );
		    brelse( xbh );
		    break;
		  }

		  partsect = be32_to_cpu(xrs->part[1].st) + extensect;
		  brelse (xbh);
		  minor++;
		  if (minor >= m_lim) {
		    printk( "\nMaximum number of partitions reached!\n" );
		    break;
		  }
		}
	      printk(" >");
	    }
	  else
	    {
	      /* we don't care about other id's */
	      add_gd_partition (hd, minor, be32_to_cpu(pi->st),
				be32_to_cpu(pi->siz));
	    }
	}
    }
#ifdef ICD_PARTS
  if ( part_fmt!=1 ) /* no extended partitions -> test ICD-format */
  {
    pi = &rs->icdpart[0];
    /* sanity check: no ICD format if first partition invalid */
    if (memcmp (pi->id, "GEM", 3) == 0 ||
        memcmp (pi->id, "BGM", 3) == 0 ||
        memcmp (pi->id, "LNX", 3) == 0 ||
        memcmp (pi->id, "SWP", 3) == 0 ||
        memcmp (pi->id, "RAW", 3) == 0 )
    {
      printk(" ICD<");
      for (; pi < &rs->icdpart[8] && minor < m_lim; minor++, pi++)
      {
        /* accept only GEM,BGM,RAW,LNX,SWP partitions */
        if (pi->flg & 1 && 
            (memcmp (pi->id, "GEM", 3) == 0 ||
             memcmp (pi->id, "BGM", 3) == 0 ||
             memcmp (pi->id, "LNX", 3) == 0 ||
             memcmp (pi->id, "SWP", 3) == 0 ||
             memcmp (pi->id, "RAW", 3) == 0) )
        {
          part_fmt = 2;
	  add_gd_partition (hd, minor, be32_to_cpu(pi->st),
			    be32_to_cpu(pi->siz));
        }
      }
      printk(" >");
    }
  }
#endif
  brelse (bh);

  printk ("\n");

  return 1;
}


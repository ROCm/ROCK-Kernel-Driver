/* Linux driver for NAND Flash Translation Layer      */
/* (c) 1999 Machine Vision Holdings, Inc.             */
/* Author: David Woodhouse <dwmw2@infradead.org>      */
/* $Id: nftl.c,v 1.57 2000/12/01 17:51:54 dwmw2 Exp $ */

/*
  The contents of this file are distributed under the GNU General
  Public License version 2 ("GPL"). The author places no additional
  restrictions of any kind on it. However, local legislation in some
  countries may restrict the use of the algorithms implemented by this
  code in certain circumstances.

  The legal note below refers only to the _use_ of the code in the 
  affected jurisdictions, and does not in any way affect the copying,
  distribution and modification of this code, which are permitted, and
  indeed required, under the terms of the GPL.

  Section 0 of the GPL says:
 "Activities other than copying, distribution and modification are not
  covered by this License; they are outside its scope."

  You may copy, distribute and modify this code to your hearts'
  content - it's just that in some jurisdictions, you may only _use_
  it under the terms of the patent grant below. This puts it in a
  similar situation to the ISDN code, which you may need telco
  approval to use, and indeed any code which has uses that may be
  restricted in law. For example, certain malicious uses of the
  networking stack may be illegal, but that doesn't prevent the
  networking code from being under GPL.

  In fact the ISDN case is worse than this, because modification of
  the code automatically invalidates its approval. Modification,
  unlike usage, _is_ one of the rights which is protected by the
  GPL. Happily, the law in those places where approval is required
  doesn't actually prevent you from modifying the code - it's just
  that you may not be allowed to _use_ it once you've done so - and
  because usage isn't addressed by the GPL, that's just fine.

  dwmw2@infradead.org
  30/10/0

  LEGAL NOTE: The NFTL format is patented by M-Systems.  They have
  granted a licence for its use with their DiskOnChip products:

    "M-Systems grants a royalty-free, non-exclusive license under
    any presently existing M-Systems intellectual property rights
    necessary for the design and development of NFTL-compatible
    drivers, file systems and utilities to use the data formats with, 
    and solely to support, M-Systems' DiskOnChip products"

  A signed copy of this agreement from M-Systems is kept on file by
  Red Hat UK Limited. In the unlikely event that you need access to it,
  please contact dwmw2@redhat.com for assistance.  */

#define PRERELEASE

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/errno.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/blkpg.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif
#include <linux/mtd/mtd.h>
#include <linux/mtd/nftl.h>
#include <linux/mtd/compatmac.h>

/* maximum number of loops while examining next block, to have a
   chance to detect consistency problems (they should never happen
   because of the checks done in the mounting */

#define MAX_LOOPS 10000

/* NFTL block device stuff */
#define MAJOR_NR NFTL_MAJOR
#define DEVICE_REQUEST nftl_request
#define DEVICE_OFF(device)


#include <linux/blk.h>
#include <linux/hdreg.h>

/* Linux-specific block device functions */

/* I _HATE_ the Linux block device setup more than anything else I've ever
 *  encountered, except ...
 */

static int nftl_sizes[256] = {0,};
static int nftl_blocksizes[256] = {0,};

/* .. for the Linux partition table handling. */
struct hd_struct part_table[256] = {{0,0},};

#if LINUX_VERSION_CODE < 0x20328
static void dummy_init (struct gendisk *crap)
{}
#endif

static struct gendisk nftl_gendisk = {
        MAJOR_NR,     /* Major number */      
        "nftl",          /* Major name */
        4,              /* Bits to shift to get real from partition */
        15,             /* Number of partitions per real */
#if LINUX_VERSION_CODE < 0x20328
        MAX_NFTLS,      /* maximum number of real */
        dummy_init,     /* init function */
#endif
        part_table,     /* hd struct */
        nftl_sizes,     /* block sizes */
        0,              /* number */
        NULL,           /* internal use, not presently used */
        NULL            /* next */
};

struct NFTLrecord *NFTLs[MAX_NFTLS] = {NULL};

static void NFTL_setup(struct mtd_info *mtd)
{
	int i;
	struct NFTLrecord *nftl;
	unsigned long temp;
	int firstfree = -1;

	DEBUG(MTD_DEBUG_LEVEL1,"NFTL_setup\n");

	for (i = 0; i < MAX_NFTLS; i++) {
		if (!NFTLs[i] && firstfree == -1)
			firstfree = i;
		else if (NFTLs[i] && NFTLs[i]->mtd == mtd) {
			/* This is a Spare Media Header for an NFTL we've already found */
			DEBUG(MTD_DEBUG_LEVEL1, "MTD already mounted as NFTL\n");
			return;
		}
	}
        if (firstfree == -1) {
		printk(KERN_WARNING "No more NFTL slot available\n");
		return;
        }

	nftl = kmalloc(sizeof(struct NFTLrecord), GFP_KERNEL);
	if (!nftl) {
		printk(KERN_WARNING "Out of memory for NFTL data structures\n");
		return;
	}

	init_MUTEX(&nftl->mutex);

        /* get physical parameters */
	nftl->EraseSize = mtd->erasesize;
        nftl->nb_blocks = mtd->size / mtd->erasesize;
	nftl->mtd = mtd;

        if (NFTL_mount(nftl) < 0) {
		printk(KERN_WARNING "Could not mount NFTL device\n");
		kfree(nftl);
		return;
        }

	/* OK, it's a new one. Set up all the data structures. */
#ifdef PSYCHO_DEBUG
	printk("Found new NFTL nftl%c\n", firstfree + 'a');
#endif

        /* linux stuff */
	nftl->usecount = 0;
	nftl->cylinders = 1024;
	nftl->heads = 16;

	temp = nftl->cylinders * nftl->heads;
	nftl->sectors = nftl->nr_sects / temp;
	if (nftl->nr_sects % temp) {
		nftl->sectors++;
		temp = nftl->cylinders * nftl->sectors;
		nftl->heads = nftl->nr_sects / temp;

		if (nftl->nr_sects % temp) {
			nftl->heads++;
			temp = nftl->heads * nftl->sectors;
			nftl->cylinders = nftl->nr_sects / temp;
		}
	}

	if (nftl->nr_sects != nftl->heads * nftl->cylinders * nftl->sectors) {
		printk(KERN_WARNING "Cannot calculate an NFTL geometry to "
		       "match size of 0x%lx.\n", nftl->nr_sects);
		printk(KERN_WARNING "Using C:%d H:%d S:%d (== 0x%lx sects)\n", 
		       nftl->cylinders, nftl->heads , nftl->sectors, 
		       (long)nftl->cylinders * (long)nftl->heads * (long)nftl->sectors );

		/* Oh no we don't have nftl->nr_sects = nftl->heads * nftl->cylinders * nftl->sectors; */
	}
	NFTLs[firstfree] = nftl;
	/* Finally, set up the block device sizes */
	nftl_sizes[firstfree * 16] = nftl->nr_sects;
	//nftl_blocksizes[firstfree*16] = 512;
	part_table[firstfree * 16].nr_sects = nftl->nr_sects;

	/* partition check ... */
#if LINUX_VERSION_CODE < 0x20328
	resetup_one_dev(&nftl_gendisk, firstfree);
#else
	grok_partitions(&nftl_gendisk, firstfree, 1<<4, nftl->nr_sects);
#endif
}

static void NFTL_unsetup(int i)
{
	struct NFTLrecord *nftl = NFTLs[i];

	DEBUG(MTD_DEBUG_LEVEL1, "NFTL_unsetup %d\n", i);
	
	NFTLs[i] = NULL;
	
	if (nftl->ReplUnitTable)
		kfree(nftl->ReplUnitTable);
	if (nftl->EUNtable)
		kfree(nftl->EUNtable);
		      
	kfree(nftl);
}

/* Search the MTD device for NFTL partitions */
static void NFTL_notify_add(struct mtd_info *mtd)
{
	DEBUG(MTD_DEBUG_LEVEL1, "NFTL_notify_add for %s\n", mtd->name);

	if (mtd) {
		if (!mtd->read_oob) {
			/* If this MTD doesn't have out-of-band data,
			   then there's no point continuing */
			DEBUG(MTD_DEBUG_LEVEL1, "No OOB data, quitting\n");
			return;
		}
		DEBUG(MTD_DEBUG_LEVEL3, "mtd->read = %p, size = %d, erasesize = %d\n", 
		      mtd->read, mtd->size, mtd->erasesize);

                NFTL_setup(mtd);
	}
}

static void NFTL_notify_remove(struct mtd_info *mtd)
{
	int i;

	for (i = 0; i < MAX_NFTLS; i++) {
		if (NFTLs[i] && NFTLs[i]->mtd == mtd)
			NFTL_unsetup(i);
	}
}

#ifdef CONFIG_NFTL_RW

/* Actual NFTL access routines */
/* NFTL_findfreeblock: Find a free Erase Unit on the NFTL partition. This function is used
 *	when the give Virtual Unit Chain
 */
static u16 NFTL_findfreeblock(struct NFTLrecord *nftl, int desperate )
{
	/* For a given Virtual Unit Chain: find or create a free block and
	   add it to the chain */
	/* We're passed the number of the last EUN in the chain, to save us from
	   having to look it up again */
	u16 pot = nftl->LastFreeEUN;
	int silly = -1;

	/* Normally, we force a fold to happen before we run out of free blocks completely */
	if (!desperate && nftl->numfreeEUNs < 2) {
		DEBUG(MTD_DEBUG_LEVEL1, "NFTL_findfreeblock: there are too few free EUNs\n");
		return 0xffff;
	}

	/* Scan for a free block */
	do {
		if (nftl->ReplUnitTable[pot] == BLOCK_FREE) {
			nftl->LastFreeEUN = pot;
			nftl->numfreeEUNs--;
			return pot;
		}

		/* This will probably point to the MediaHdr unit itself,
		   right at the beginning of the partition. But that unit
		   (and the backup unit too) should have the UCI set
		   up so that it's not selected for overwriting */
		if (++pot > nftl->lastEUN)
			pot = le16_to_cpu(nftl->MediaHdr.FirstPhysicalEUN);

		if (!silly--) {
			printk("Argh! No free blocks found! LastFreeEUN = %d, "
			       "FirstEUN = %d\n", nftl->LastFreeEUN, 
			       le16_to_cpu(nftl->MediaHdr.FirstPhysicalEUN));
			return 0xffff;
		}
	} while (pot != nftl->LastFreeEUN);

	return 0xffff;
}

static u16 NFTL_foldchain (struct NFTLrecord *nftl, unsigned thisVUC, unsigned pendingblock )
{
	u16 BlockMap[MAX_SECTORS_PER_UNIT];
	unsigned char BlockLastState[MAX_SECTORS_PER_UNIT];
	unsigned char BlockFreeFound[MAX_SECTORS_PER_UNIT];
	unsigned int thisEUN;
	int block;
	int silly;
	unsigned int targetEUN;
	struct nftl_oob oob;
	int inplace = 1;
        size_t retlen;

	memset(BlockMap, 0xff, sizeof(BlockMap));
	memset(BlockFreeFound, 0, sizeof(BlockFreeFound));

	thisEUN = nftl->EUNtable[thisVUC];

	if (thisEUN == BLOCK_NIL) {
		printk(KERN_WARNING "Trying to fold non-existent "
		       "Virtual Unit Chain %d!\n", thisVUC);
		return BLOCK_NIL;
	}
	
	/* Scan to find the Erase Unit which holds the actual data for each
	   512-byte block within the Chain.
	*/
        silly = MAX_LOOPS;
	targetEUN = BLOCK_NIL;
	while (thisEUN <= nftl->lastEUN ) {
                unsigned int status, foldmark;

		targetEUN = thisEUN;
		for (block = 0; block < nftl->EraseSize / 512; block ++) {
			MTD_READOOB(nftl->mtd,
				    (thisEUN * nftl->EraseSize) + (block * 512),
				    16 , &retlen, (char *)&oob);
			if (block == 2) {
                                foldmark = oob.u.c.FoldMark | oob.u.c.FoldMark1;
                                if (foldmark == FOLD_MARK_IN_PROGRESS) {
                                        DEBUG(MTD_DEBUG_LEVEL1, 
                                              "Write Inhibited on EUN %d\n", thisEUN);
					inplace = 0;
				} else {
					/* There's no other reason not to do inplace,
					   except ones that come later. So we don't need
					   to preserve inplace */
					inplace = 1;
				}
			}
                        status = oob.b.Status | oob.b.Status1;
			BlockLastState[block] = status;

			switch(status) {
			case SECTOR_FREE:
				BlockFreeFound[block] = 1;
				break;

			case SECTOR_USED:
				if (!BlockFreeFound[block])
					BlockMap[block] = thisEUN;
				else
					printk(KERN_WARNING 
					       "SECTOR_USED found after SECTOR_FREE "
					       "in Virtual Unit Chain %d for block %d\n",
					       thisVUC, block);
				break;
			case SECTOR_IGNORE:
			case SECTOR_DELETED:
				break;
			default:
				printk("Unknown status for block %d in EUN %d: %x\n",
				       block, thisEUN, status);
			}
		}

		if (!silly--) {
			printk(KERN_WARNING "Infinite loop in Virtual Unit Chain 0x%x\n",
			       thisVUC);
			return BLOCK_NIL;
		}
		
		thisEUN = nftl->ReplUnitTable[thisEUN];
	}

	if (inplace) {
		/* We're being asked to be a fold-in-place. Check
		   that all blocks are either present or SECTOR_FREE
		   in the target block. If not, we're going to have
		   to fold out-of-place anyway.
		*/
		for (block = 0; block < nftl->EraseSize / 512 ; block++) {
			if (BlockLastState[block] != SECTOR_FREE &&
			    BlockMap[block] != targetEUN) {
				DEBUG(MTD_DEBUG_LEVEL1, "Setting inplace to 0. VUC %d, "
				      "block %d was %x lastEUN, "
				      "and is in EUN %d (%s) %d\n",
				      thisVUC, block, BlockLastState[block],
				      BlockMap[block], 
				      BlockMap[block]== targetEUN ? "==" : "!=",
				      targetEUN);
				inplace = 0;
				break;
			}
		}

		if (pendingblock >= (thisVUC * (nftl->EraseSize / 512)) &&
		    pendingblock < ((thisVUC + 1)* (nftl->EraseSize / 512)) &&
		    BlockLastState[pendingblock - (thisVUC * (nftl->EraseSize / 512))] !=
		    SECTOR_FREE) {
			DEBUG(MTD_DEBUG_LEVEL1, "Pending write not free in EUN %d. "
			      "Folding out of place.\n", targetEUN);
			inplace = 0;
		}
	}
	
	if (!inplace) {
		DEBUG(MTD_DEBUG_LEVEL1, "Cannot fold Virtual Unit Chain %d in place. "
		      "Trying out-of-place\n", thisVUC);
		/* We need to find a targetEUN to fold into. */
		targetEUN = NFTL_findfreeblock(nftl, 1);
		if (targetEUN == BLOCK_NIL) {
			/* Ouch. Now we're screwed. We need to do a 
			   fold-in-place of another chain to make room
			   for this one. We need a better way of selecting
			   which chain to fold, because makefreeblock will 
			   only ask us to fold the same one again.
			*/
			printk(KERN_WARNING
			       "NFTL_findfreeblock(desperate) returns 0xffff.\n");
			return BLOCK_NIL;
		}
	} else {
            /* We put a fold mark in the chain we are folding only if
               we fold in place to help the mount check code. If we do
               not fold in place, it is possible to find the valid
               chain by selecting the longer one */
            oob.u.c.FoldMark = oob.u.c.FoldMark1 = cpu_to_le16(FOLD_MARK_IN_PROGRESS);
            oob.u.c.unused = 0xffffffff;
            MTD_WRITEOOB(nftl->mtd, (nftl->EraseSize * targetEUN) + 2 * 512 + 8, 
                         8, &retlen, (char *)&oob.u);
        }

	/* OK. We now know the location of every block in the Virtual Unit Chain,
	   and the Erase Unit into which we are supposed to be copying.
	   Go for it.
	*/
	DEBUG(MTD_DEBUG_LEVEL1,"Folding chain %d into unit %d\n", thisVUC, targetEUN);
	for (block = 0; block < nftl->EraseSize / 512 ; block++) {
		unsigned char movebuf[512];
		int ret;

		/* If it's in the target EUN already, or if it's pending write, do nothing */
		if (BlockMap[block] == targetEUN ||
		    (pendingblock == (thisVUC * (nftl->EraseSize / 512) + block))) {
			continue;
		}

                /* copy only in non free block (free blocks can only
                   happen in case of media errors or deleted blocks) */
                if (BlockMap[block] == BLOCK_NIL)
                        continue;
                
                ret = MTD_READECC(nftl->mtd, (nftl->EraseSize * BlockMap[block])
                                  + (block * 512), 512, &retlen, movebuf, (char *)&oob); 
                if (ret < 0) {
                    ret = MTD_READECC(nftl->mtd, (nftl->EraseSize * BlockMap[block])
                                      + (block * 512), 512, &retlen,
                                      movebuf, (char *)&oob); 
                    if (ret != -EIO) 
                        printk("Error went away on retry.\n");
                }
                MTD_WRITEECC(nftl->mtd, (nftl->EraseSize * targetEUN) + (block * 512),
                             512, &retlen, movebuf, (char *)&oob);
	}
        
        /* add the header so that it is now a valid chain */
        oob.u.a.VirtUnitNum = oob.u.a.SpareVirtUnitNum
                = cpu_to_le16(thisVUC);
        oob.u.a.ReplUnitNum = oob.u.a.SpareReplUnitNum = 0xffff;
        
        MTD_WRITEOOB(nftl->mtd, (nftl->EraseSize * targetEUN) + 8, 
                     8, &retlen, (char *)&oob.u);

	/* OK. We've moved the whole lot into the new block. Now we have to free the original blocks. */

	/* At this point, we have two different chains for this Virtual Unit, and no way to tell 
	   them apart. If we crash now, we get confused. However, both contain the same data, so we
	   shouldn't actually lose data in this case. It's just that when we load up on a medium which
	   has duplicate chains, we need to free one of the chains because it's not necessary any more.
	*/
	thisEUN = nftl->EUNtable[thisVUC];
	DEBUG(MTD_DEBUG_LEVEL1,"Want to erase\n");

	/* For each block in the old chain (except the targetEUN of course), 
	   free it and make it available for future use */
	while (thisEUN <= nftl->lastEUN && thisEUN != targetEUN) {
		unsigned int EUNtmp;

                EUNtmp = nftl->ReplUnitTable[thisEUN];

                if (NFTL_formatblock(nftl, thisEUN) < 0) {
			/* could not erase : mark block as reserved
			 * FixMe: Update Bad Unit Table on disk
			 */
			nftl->ReplUnitTable[thisEUN] = BLOCK_RESERVED;
                } else {
			/* correctly erased : mark it as free */
			nftl->ReplUnitTable[thisEUN] = BLOCK_FREE;
			nftl->numfreeEUNs++;
                }
                thisEUN = EUNtmp;
	}
	
	/* Make this the new start of chain for thisVUC */
	nftl->ReplUnitTable[targetEUN] = BLOCK_NIL;
	nftl->EUNtable[thisVUC] = targetEUN;

	return targetEUN;
}

u16 NFTL_makefreeblock( struct NFTLrecord *nftl , unsigned pendingblock)
{
	/* This is the part that needs some cleverness applied. 
	   For now, I'm doing the minimum applicable to actually
	   get the thing to work.
	   Wear-levelling and other clever stuff needs to be implemented
	   and we also need to do some assessment of the results when
	   the system loses power half-way through the routine.
	*/
	u16 LongestChain = 0;
	u16 ChainLength = 0, thislen;
	u16 chain, EUN;

	for (chain = 0; chain < nftl->MediaHdr.FormattedSize / nftl->EraseSize; chain++) {
		EUN = nftl->EUNtable[chain];
		thislen = 0;

		while (EUN <= nftl->lastEUN) {
			thislen++;
			//printk("VUC %d reaches len %d with EUN %d\n", chain, thislen, EUN);
			EUN = nftl->ReplUnitTable[EUN] & 0x7fff;
			if (thislen > 0xff00) {
				printk("Endless loop in Virtual Chain %d: Unit %x\n",
				       chain, EUN);
			}
			if (thislen > 0xff10) {
				/* Actually, don't return failure. Just ignore this chain and
				   get on with it. */
				thislen = 0;
				break;
			}
		}

		if (thislen > ChainLength) {
			//printk("New longest chain is %d with length %d\n", chain, thislen);
			ChainLength = thislen;
			LongestChain = chain;
		}
	}

	if (ChainLength < 2) {
		printk(KERN_WARNING "No Virtual Unit Chains available for folding. "
		       "Failing request\n");
		return 0xffff;
	}

	return NFTL_foldchain (nftl, LongestChain, pendingblock);
}

/* NFTL_findwriteunit: Return the unit number into which we can write 
                       for this block. Make it available if it isn't already
*/
static inline u16 NFTL_findwriteunit(struct NFTLrecord *nftl, unsigned block)
{
	u16 lastEUN;
	u16 thisVUC = block / (nftl->EraseSize / 512);
	unsigned int writeEUN;
	unsigned long blockofs = (block * 512) & (nftl->EraseSize -1);
	size_t retlen;
	int silly, silly2 = 3;
	struct nftl_oob oob;

	do {
		/* Scan the media to find a unit in the VUC which has
		   a free space for the block in question.
		*/

		/* This condition catches the 0x[7f]fff cases, as well as 
		   being a sanity check for past-end-of-media access
		*/
		lastEUN = BLOCK_NIL;
		writeEUN = nftl->EUNtable[thisVUC];
                silly = MAX_LOOPS;
		while (writeEUN <= nftl->lastEUN) {
			struct nftl_bci bci;
			size_t retlen;
                        unsigned int status;

			lastEUN = writeEUN;

			MTD_READOOB(nftl->mtd, (writeEUN * nftl->EraseSize) + blockofs,
				    8, &retlen, (char *)&bci);
			
			DEBUG(MTD_DEBUG_LEVEL2, "Status of block %d in EUN %d is %x\n",
			      block , writeEUN, le16_to_cpu(bci.Status));

                        status = bci.Status | bci.Status1;
			switch(status) {
			case SECTOR_FREE:
				return writeEUN;

			case SECTOR_DELETED:
			case SECTOR_USED:
			case SECTOR_IGNORE:
				break;
			default:
				// Invalid block. Don't use it any more. Must implement.
				break;			
			}
			
			if (!silly--) { 
				printk(KERN_WARNING
				       "Infinite loop in Virtual Unit Chain 0x%x\n",
				       thisVUC);
				return 0xffff;
			}

			/* Skip to next block in chain */
			writeEUN = nftl->ReplUnitTable[writeEUN];
		}

		/* OK. We didn't find one in the existing chain, or there 
		   is no existing chain. */

		/* Try to find an already-free block */
		writeEUN = NFTL_findfreeblock(nftl, 0);

		if (writeEUN == BLOCK_NIL) {
			/* That didn't work - there were no free blocks just
			   waiting to be picked up. We're going to have to fold
			   a chain to make room.
			*/

			/* First remember the start of this chain */
			//u16 startEUN = nftl->EUNtable[thisVUC];
			
			//printk("Write to VirtualUnitChain %d, calling makefreeblock()\n", thisVUC);
			writeEUN = NFTL_makefreeblock(nftl, 0xffff);
			
			if (writeEUN == BLOCK_NIL) {
				/* Ouch. This should never happen - we should
				   always be able to make some room somehow. 
				   If we get here, we've allocated more storage 
				   space than actual media, or our makefreeblock
				   routine is missing something.
				*/
				printk(KERN_WARNING "Cannot make free space.\n");
				return BLOCK_NIL;
			}			
			//printk("Restarting scan\n");
			lastEUN = BLOCK_NIL;
			continue;
		}

		/* We've found a free block. Insert it into the chain. */
		
		if (lastEUN != BLOCK_NIL) {
                    thisVUC |= 0x8000; /* It's a replacement block */
		} else {
                    /* The first block in a new chain */
                    nftl->EUNtable[thisVUC] = writeEUN;
		}

		/* set up the actual EUN we're writing into */
		/* Both in our cache... */
		nftl->ReplUnitTable[writeEUN] = BLOCK_NIL;

		/* ... and on the flash itself */
		MTD_READOOB(nftl->mtd, writeEUN * nftl->EraseSize + 8, 8,
			    &retlen, (char *)&oob.u);

		oob.u.a.VirtUnitNum = oob.u.a.SpareVirtUnitNum = cpu_to_le16(thisVUC);

		MTD_WRITEOOB(nftl->mtd, writeEUN * nftl->EraseSize + 8, 8,
                             &retlen, (char *)&oob.u);

                /* we link the new block to the chain only after the
                   block is ready. It avoids the case where the chain
                   could point to a free block */
                if (lastEUN != BLOCK_NIL) {
			/* Both in our cache... */
			nftl->ReplUnitTable[lastEUN] = writeEUN;
			/* ... and on the flash itself */
			MTD_READOOB(nftl->mtd, (lastEUN * nftl->EraseSize) + 8,
				    8, &retlen, (char *)&oob.u);

			oob.u.a.ReplUnitNum = oob.u.a.SpareReplUnitNum
				= cpu_to_le16(writeEUN);

			MTD_WRITEOOB(nftl->mtd, (lastEUN * nftl->EraseSize) + 8,
				     8, &retlen, (char *)&oob.u);
		}

		return writeEUN;

	} while (silly2--);

	printk(KERN_WARNING "Error folding to make room for Virtual Unit Chain 0x%x\n",
	       thisVUC);
	return 0xffff;
}

static int NFTL_writeblock(struct NFTLrecord *nftl, unsigned block, char *buffer)
{
	u16 writeEUN;
	unsigned long blockofs = (block * 512) & (nftl->EraseSize - 1);
	size_t retlen;
	u8 eccbuf[6];

	writeEUN = NFTL_findwriteunit(nftl, block);

	if (writeEUN == BLOCK_NIL) {
		printk(KERN_WARNING
		       "NFTL_writeblock(): Cannot find block to write to\n");
		/* If we _still_ haven't got a block to use, we're screwed */
		return 1;
	}

	MTD_WRITEECC(nftl->mtd, (writeEUN * nftl->EraseSize) + blockofs,
		     512, &retlen, (char *)buffer, (char *)eccbuf);
        /* no need to write SECTOR_USED flags since they are written in mtd_writeecc */

	return 0;
}
#endif /* CONFIG_NFTL_RW */

static int NFTL_readblock(struct NFTLrecord *nftl, unsigned block, char *buffer)
{
	u16 lastgoodEUN;
	u16 thisEUN = nftl->EUNtable[block / (nftl->EraseSize / 512)];
	unsigned long blockofs = (block * 512) & (nftl->EraseSize - 1);
        unsigned int status;
	int silly = MAX_LOOPS;
        size_t retlen;
        struct nftl_bci bci;

	lastgoodEUN = BLOCK_NIL;

        if (thisEUN != BLOCK_NIL) {
		while (thisEUN < nftl->nb_blocks) {
			if (MTD_READOOB(nftl->mtd, (thisEUN * nftl->EraseSize) + blockofs,
					8, &retlen, (char *)&bci) < 0)
				status = SECTOR_IGNORE;
			else
				status = bci.Status | bci.Status1;

			switch (status) {
			case SECTOR_FREE:
				/* no modification of a sector should follow a free sector */
				goto the_end;
			case SECTOR_DELETED:
				lastgoodEUN = BLOCK_NIL;
				break;
			case SECTOR_USED:
				lastgoodEUN = thisEUN;
				break;
			case SECTOR_IGNORE:
				break;
			default:
				printk("Unknown status for block %d in EUN %d: %x\n",
				       block, thisEUN, status);
				break;
			}

			if (!silly--) {
				printk(KERN_WARNING "Infinite loop in Virtual Unit Chain 0x%x\n",
				       block / (nftl->EraseSize / 512));
				return 1;
			}
			thisEUN = nftl->ReplUnitTable[thisEUN];
		}
        }

 the_end:
	if (lastgoodEUN == BLOCK_NIL) {
		/* the requested block is not on the media, return all 0x00 */
		memset(buffer, 0, 512);
	} else {
		loff_t ptr = (lastgoodEUN * nftl->EraseSize) + blockofs;
		size_t retlen;
		u_char eccbuf[6];
		if (MTD_READECC(nftl->mtd, ptr, 512, &retlen, buffer, eccbuf))
			return -EIO;
	}
	return 0;
}

static int nftl_ioctl(struct inode * inode, struct file * file, unsigned int cmd, unsigned long arg)
{
	struct NFTLrecord *nftl;

	nftl = NFTLs[MINOR(inode->i_rdev) / 16];

	if (!nftl) return -EINVAL;

	switch (cmd) {
	case HDIO_GETGEO: {
		struct hd_geometry g;

		g.heads = nftl->heads;
		g.sectors = nftl->sectors;
		g.cylinders = nftl->cylinders;
		g.start = part_table[MINOR(inode->i_rdev)].start_sect;
		return copy_to_user((void *)arg, &g, sizeof g) ? -EFAULT : 0;
	}
	case BLKGETSIZE:   /* Return device size */
		if (!arg) return -EINVAL;
		return put_user(part_table[MINOR(inode->i_rdev)].nr_sects,
                                (long *) arg);
		
	case BLKFLSBUF:
		if (!capable(CAP_SYS_ADMIN)) return -EACCES;
		fsync_dev(inode->i_rdev);
		invalidate_buffers(inode->i_rdev);
		if (nftl->mtd->sync)
			nftl->mtd->sync(nftl->mtd);
		return 0;

	case BLKRRPART:
		if (!capable(CAP_SYS_ADMIN)) return -EACCES;
		if (nftl->usecount > 1) return -EBUSY;
#if LINUX_VERSION_CODE < 0x20328
		resetup_one_dev(&nftl_gendisk, MINOR(inode->i_rdev) / 16);
#else
		grok_partitions(&nftl_gendisk, MINOR(inode->i_rdev) / 16,
				1<<4, nftl->nr_sects);
#endif
		return 0;

#if (LINUX_VERSION_CODE < 0x20303)		
	RO_IOCTLS(inode->i_rdev, arg);  /* ref. linux/blk.h */
#else
	case BLKROSET:
	case BLKROGET:
	case BLKSSZGET:
		return blk_ioctl(inode->i_rdev, cmd, arg);
#endif

	default:
		return -EINVAL;
	}
}

void nftl_request(RQFUNC_ARG)
{
	unsigned int dev, block, nsect;
	struct NFTLrecord *nftl;
	char *buffer;
	struct request *req;
	int res;

	while (1) {
		INIT_REQUEST;	/* blk.h */
		req = CURRENT;
		
		/* We can do this because the generic code knows not to
		   touch the request at the head of the queue */
		spin_unlock_irq(&io_request_lock);

		DEBUG(MTD_DEBUG_LEVEL2, "NFTL_request\n");
		DEBUG(MTD_DEBUG_LEVEL3, "NFTL %s request, from sector 0x%04lx for 0x%04lx sectors\n",
		      (req->cmd == READ) ? "Read " : "Write",
		      req->sector, req->current_nr_sectors);

		dev = MINOR(req->rq_dev);
		block = req->sector;
		nsect = req->current_nr_sectors;
		buffer = req->buffer;
		res = 1; /* succeed */

		if (dev >= MAX_NFTLS * 16) {
			/* there is no such partition */
			printk("nftl: bad minor number: device = %s\n",
			       kdevname(req->rq_dev));
			res = 0; /* fail */
			goto repeat;
		}
		
		nftl = NFTLs[dev / 16];
		DEBUG(MTD_DEBUG_LEVEL3, "Waiting for mutex\n");
		down(&nftl->mutex);
		DEBUG(MTD_DEBUG_LEVEL3, "Got mutex\n");

		if (block + nsect > part_table[dev].nr_sects) {
			/* access past the end of device */
			printk("nftl%c%d: bad access: block = %d, count = %d\n",
			       (MINOR(req->rq_dev)>>6)+'a', dev & 0xf, block, nsect);
			up(&nftl->mutex);
			res = 0; /* fail */
			goto repeat;
		}
		
		block += part_table[dev].start_sect;
		
		if (req->cmd == READ) {
			DEBUG(MTD_DEBUG_LEVEL2, "NFTL read request of 0x%x sectors @ %x "
			      "(req->nr_sectors == %lx)\n", nsect, block, req->nr_sectors);
	
			for ( ; nsect > 0; nsect-- , block++, buffer += 512) {
				/* Read a single sector to req->buffer + (512 * i) */
				if (NFTL_readblock(nftl, block, buffer)) {
					DEBUG(MTD_DEBUG_LEVEL2, "NFTL read request failed\n");
					up(&nftl->mutex);
					res = 0;
					goto repeat;
				}
			}

			DEBUG(MTD_DEBUG_LEVEL2,"NFTL read request completed OK\n");
			up(&nftl->mutex);
			goto repeat;
		} else if (req->cmd == WRITE) {
			DEBUG(MTD_DEBUG_LEVEL2, "NFTL write request of 0x%x sectors @ %x "
			      "(req->nr_sectors == %lx)\n", nsect, block,
			      req->nr_sectors);
#ifdef CONFIG_NFTL_RW
			for ( ; nsect > 0; nsect-- , block++, buffer += 512) {
				/* Read a single sector to req->buffer + (512 * i) */
				if (NFTL_writeblock(nftl, block, buffer)) {
					DEBUG(MTD_DEBUG_LEVEL1,"NFTL write request failed\n");
					up(&nftl->mutex);
					res = 0;
					goto repeat;
				}
			}
			DEBUG(MTD_DEBUG_LEVEL2,"NFTL write request completed OK\n");
#else
			res = 0; /* Writes always fail */
#endif /* CONFIG_NFTL_RW */
			up(&nftl->mutex);
			goto repeat;
		} else {
			DEBUG(MTD_DEBUG_LEVEL0, "NFTL unknown request\n");
			up(&nftl->mutex);
			res = 0;
			goto repeat;
		}
	repeat: 
		DEBUG(MTD_DEBUG_LEVEL3, "end_request(%d)\n", res);
		spin_lock_irq(&io_request_lock);
		end_request(res);
	}
}

static int nftl_open(struct inode *ip, struct file *fp)
{
	int nftlnum = MINOR(ip->i_rdev) / 16;
	struct NFTLrecord *thisNFTL;
	thisNFTL = NFTLs[nftlnum];

	DEBUG(MTD_DEBUG_LEVEL2,"NFTL_open\n");

#ifdef CONFIG_KMOD
	if (!thisNFTL && nftlnum == 0) {
		request_module("docprobe");
		thisNFTL = NFTLs[nftlnum];
	}
#endif
	if (!thisNFTL) {
		DEBUG(MTD_DEBUG_LEVEL2,"ENODEV: thisNFTL = %d, minor = %d, ip = %p, fp = %p\n", 
		      nftlnum, ip->i_rdev, ip, fp);
		return -ENODEV;
	}

#ifndef CONFIG_NFTL_RW
	if (fp->f_mode & FMODE_WRITE)
		return -EROFS;
#endif /* !CONFIG_NFTL_RW */

	thisNFTL->usecount++;
	MOD_INC_USE_COUNT;
	if (!get_mtd_device(thisNFTL->mtd, -1)) {
		MOD_DEC_USE_COUNT;
		return /* -E'SBUGGEREDOFF */ -ENXIO;
	}

	return 0;
}

static int nftl_release(struct inode *inode, struct file *fp)
{
	struct super_block *sb = get_super(inode->i_rdev);
	struct NFTLrecord *thisNFTL;

	thisNFTL = NFTLs[MINOR(inode->i_rdev) / 16];

	DEBUG(MTD_DEBUG_LEVEL2, "NFTL_release\n");

	fsync_dev(inode->i_rdev);
	if (sb)
		invalidate_inodes(sb);
	invalidate_buffers(inode->i_rdev);

	if (thisNFTL->mtd->sync)
		thisNFTL->mtd->sync(thisNFTL->mtd);
	thisNFTL->usecount--;
	MOD_DEC_USE_COUNT;

	put_mtd_device(thisNFTL->mtd);

	return 0;
}
#if LINUX_VERSION_CODE < 0x20326
static struct file_operations nftl_fops = {
	read:		block_read,
	write:		block_write,
	ioctl:		nftl_ioctl,
	open:		nftl_open,
	release:	nftl_release,
	fsync:		block_fsync,
};
#else
static struct block_device_operations nftl_fops = 
{
	open:		nftl_open,
	release:	nftl_release,
	ioctl: 		nftl_ioctl
};
#endif



/****************************************************************************
 *
 * Module stuff
 *
 ****************************************************************************/

#if LINUX_VERSION_CODE < 0x20212 && defined(MODULE)
#define init_nftl init_module
#define cleanup_nftl cleanup_module
#endif

static struct mtd_notifier nftl_notifier = {NFTL_notify_add, NFTL_notify_remove, NULL};

/* static int __init init_nftl(void) */
int __init init_nftl(void)
{
	int i;

	printk(KERN_NOTICE
	       "M-Systems NAND Flash Translation Layer driver. (C) 1999 MVHI\n");
#ifdef PRERELEASE 
	printk(KERN_INFO"$Id: nftl.c,v 1.57 2000/12/01 17:51:54 dwmw2 Exp $\n");
#endif

	if (register_blkdev(MAJOR_NR, "nftl", &nftl_fops)){
		printk("unable to register NFTL block device on major %d\n", MAJOR_NR);
		return -EBUSY;
	} else {
#if LINUX_VERSION_CODE < 0x20320
		blk_dev[MAJOR_NR].request_fn = nftl_request;
#else
		blk_init_queue(BLK_DEFAULT_QUEUE(MAJOR_NR), &nftl_request);
#endif
		/* set block size to 1kB each */
		for (i = 0; i < 256; i++) {
			nftl_blocksizes[i] = 1024;
		}
		blksize_size[MAJOR_NR] = nftl_blocksizes;

		nftl_gendisk.next = gendisk_head;
		gendisk_head = &nftl_gendisk;
	}
	
	register_mtd_user(&nftl_notifier);

	return 0;
}

static void __exit cleanup_nftl(void)
{
	struct gendisk *gd, **gdp;

  	unregister_mtd_user(&nftl_notifier);
  	unregister_blkdev(MAJOR_NR, "nftl");
  	
#if LINUX_VERSION_CODE < 0x20320
  	blk_dev[MAJOR_NR].request_fn = 0;
#else
  	blk_cleanup_queue(BLK_DEFAULT_QUEUE(MAJOR_NR));
#endif	

	/* remove ourself from generic harddisk list
	   FIXME: why can't I found this partition on /proc/partition */
  	for (gdp = &gendisk_head; *gdp; gdp = &((*gdp)->next))
    		if (*gdp == &nftl_gendisk) {
      			gd = *gdp; *gdp = gd->next;
      			break;
    	}
}

module_init(init_nftl);
module_exit(cleanup_nftl);

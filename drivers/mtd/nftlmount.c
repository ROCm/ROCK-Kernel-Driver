/* 
 * NFTL mount code with extensive checks
 *
 * Author: Fabrice Bellard (fabrice.bellard@netgem.com) 
 * Copyright (C) 2000 Netgem S.A.
 *
 * $Id: nftlmount.c,v 1.11 2000/11/17 12:24:09 ollie Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
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
#include <linux/mtd/mtd.h>
#include <linux/mtd/nftl.h>
#include <linux/mtd/compatmac.h>

#define SECTORSIZE 512

/* find_boot_record: Find the NFTL Media Header and its Spare copy which contains the
 *	various device information of the NFTL partition and Bad Unit Table. Update
 *	the ReplUnitTable[] table accroding to the Bad Unit Table. ReplUnitTable[]
 *	is used for management of Erase Unit in other routines in nftl.c and nftlmount.c
 */
static int find_boot_record(struct NFTLrecord *nftl)
{
	struct nftl_uci1 h1;
	struct nftl_oob oob;
	unsigned int block, boot_record_count;
	int retlen;
	u8 buf[SECTORSIZE];
	struct NFTLMediaHeader *mh = &nftl->MediaHdr;

	nftl->MediaUnit = BLOCK_NIL;
	nftl->SpareMediaUnit = BLOCK_NIL;
	boot_record_count = 0;

	/* search for a valid boot record */
	for (block = 0; block < nftl->nb_blocks; block++) {
		unsigned int erase_mark;

		/* read ANAND header. To be safer with BIOS, also use erase mark as discriminant */
		if (MTD_READOOB(nftl->mtd, block * nftl->EraseSize + SECTORSIZE + 8,
				8, &retlen, (char *)&h1) < 0)
			continue;

		erase_mark = le16_to_cpu ((h1.EraseMark | h1.EraseMark1));
		if (erase_mark != ERASE_MARK) 
			continue;

		if (MTD_READECC(nftl->mtd, block * nftl->EraseSize, SECTORSIZE,
				&retlen, buf, (char *)&oob) < 0)
			continue;

		memcpy(mh, buf, sizeof(struct NFTLMediaHeader));
		if (memcmp(mh->DataOrgID, "ANAND", 6) == 0) {
			/* first boot record */
			if (boot_record_count == 0) {
				unsigned int i;
				/* header found : read the bad block table data */
				if (mh->UnitSizeFactor != 0xff) {
					printk("Sorry, we don't support UnitSizeFactor "
					       "of != 1 yet\n");
					goto ReplUnitTable;
				}

				nftl->nb_boot_blocks = le16_to_cpu(mh->FirstPhysicalEUN);
				if ((nftl->nb_boot_blocks + 2) >= nftl->nb_blocks)
					goto ReplUnitTable; /* small consistency check */

				nftl->numvunits = le32_to_cpu(mh->FormattedSize) / nftl->EraseSize;
				if (nftl->numvunits > (nftl->nb_blocks - nftl->nb_boot_blocks - 2))
					goto ReplUnitTable; /* small consistency check */

				/* FixMe: with bad blocks, the total size available is not FormattedSize any
				   more !!! */
				nftl->nr_sects  = nftl->numvunits * (nftl->EraseSize / SECTORSIZE);
				nftl->MediaUnit = block;

				/* read the Bad Erase Unit Table and modify ReplUnitTable[] accordingly */
				for (i = 0; i < nftl->nb_blocks; i++) {
					if ((i & (SECTORSIZE - 1)) == 0) {
						/* read one sector for every SECTORSIZE of blocks */
						if (MTD_READECC(nftl->mtd, block * nftl->EraseSize +
								i + SECTORSIZE, SECTORSIZE,
								&retlen, buf, (char *)&oob) < 0)
							goto ReplUnitTable;
					}
					/* mark the Bad Erase Unit as RESERVED in ReplUnitTable */
					if (buf[i & (SECTORSIZE - 1)] != 0xff)
						nftl->ReplUnitTable[i] = BLOCK_RESERVED;
				}

				boot_record_count++;
			} else if (boot_record_count == 1) {
				nftl->SpareMediaUnit = block;
				boot_record_count++;
				break;
			}
		}
	ReplUnitTable:
	}

	if (boot_record_count == 0) {
		/* no boot record found */
		return -1;
	} else {
		return 0;
	}
}

static int memcmpb(void *a, int c, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		if (c != ((unsigned char *)a)[i])
			return 1;
	}
	return 0;
}

/* check_free_sector: check if a free sector is actually FREE, i.e. All 0xff in data and oob area */
static int check_free_sectors(struct NFTLrecord *nftl, unsigned int address, int len, 
			      int check_oob)
{
	int i, retlen;
	u8 buf[SECTORSIZE];

	for (i = 0; i < len; i += SECTORSIZE) {
		/* we want to read the sector without ECC check here since a free
		   sector does not have ECC syndrome on it yet */
		if (MTD_READ(nftl->mtd, address, SECTORSIZE, &retlen, buf) < 0)
			return -1;
		if (memcmpb(buf, 0xff, SECTORSIZE) != 0)
			return -1;

		if (check_oob) {
			if (MTD_READOOB(nftl->mtd, address, nftl->mtd->oobsize,
					&retlen, buf) < 0)
				return -1;
			if (memcmpb(buf, 0xff, nftl->mtd->oobsize) != 0)
				return -1;
		}
		address += SECTORSIZE;
	}

	return 0;
}

/* NFTL_format: format a Erase Unit by erasing ALL Erase Zones in the Erase Unit and
 *              Update NFTL metadata. Each erase operation is checked with check_free_sectors
 *
 * Return: 0 when succeed, -1 on error.
 *
 *  ToDo: 1. Is it neceressary to check_free_sector after erasing ?? 
 *        2. UnitSizeFactor != 0xFF
 */
int NFTL_formatblock(struct NFTLrecord *nftl, int block)
{
	int retlen;
	unsigned int nb_erases, erase_mark;
	struct nftl_uci1 uci;
	struct erase_info *instr = &nftl->instr;

	/* Read the Unit Control Information #1 for Wear-Leveling */
	if (MTD_READOOB(nftl->mtd, block * nftl->EraseSize + SECTORSIZE + 8,
			8, &retlen, (char *)&uci) < 0)
		goto default_uci1;

	erase_mark = le16_to_cpu ((uci.EraseMark | uci.EraseMark1));
	if (erase_mark != ERASE_MARK) {
	default_uci1:
		uci.EraseMark = cpu_to_le16(ERASE_MARK);
		uci.EraseMark1 = cpu_to_le16(ERASE_MARK);
		uci.WearInfo = cpu_to_le32(0);
	}

	memset(instr, 0, sizeof(struct erase_info));

	/* XXX: use async erase interface, XXX: test return code */
	instr->addr = block * nftl->EraseSize;
	instr->len = nftl->EraseSize;
	MTD_ERASE(nftl->mtd, instr);

	if (instr->state == MTD_ERASE_FAILED) {
		/* could not format, FixMe: We should update the BadUnitTable 
		   both in memory and on disk */
		printk("Error while formatting block %d\n", block);
		return -1;
	} else {
		/* increase and write Wear-Leveling info */
		nb_erases = le32_to_cpu(uci.WearInfo);
		nb_erases++;

		/* wrap (almost impossible with current flashs) or free block */
		if (nb_erases == 0)
			nb_erases = 1;

		/* check the "freeness" of Erase Unit before updating metadata
		 * FixMe:  is this check really necessary ? since we have check the
		 *         return code after the erase operation. */
		if (check_free_sectors(nftl, instr->addr, nftl->EraseSize, 1) != 0)
			return -1;

		uci.WearInfo = le32_to_cpu(nb_erases);
		if (MTD_WRITEOOB(nftl->mtd, block * nftl->EraseSize + SECTORSIZE + 8, 8,
				 &retlen, (char *)&uci) < 0)
			return -1;
		return 0;
	}
}

/* check_sectors_in_chain: Check that each sector of a Virtual Unit Chain is correct.
 *	Mark as 'IGNORE' each incorrect sector. This check is only done if the chain
 *	was being folded when NFTL was interrupted.
 *
 *	The check_free_sectors in this function is neceressary. There is a possible
 *	situation that after writing the Data area, the Block Control Information is
 *	not updated according (due to power failure or something) which leaves the block
 *	in an umconsistent state. So we have to check if a block is really FREE in this
 *	case. */
static void check_sectors_in_chain(struct NFTLrecord *nftl, unsigned int first_block)
{
	unsigned int block, i, status;
	struct nftl_bci bci;
	int sectors_per_block, retlen;

	sectors_per_block = nftl->EraseSize / SECTORSIZE;
	block = first_block;
	for (;;) {
		for (i = 0; i < sectors_per_block; i++) {
			if (MTD_READOOB(nftl->mtd, block * nftl->EraseSize + i * SECTORSIZE,
					8, &retlen, (char *)&bci) < 0)
				status = SECTOR_IGNORE;
			else
				status = bci.Status | bci.Status1;

			switch(status) {
			case SECTOR_FREE:
				/* verify that the sector is really free. If not, mark
				   as ignore */
				if (memcmpb(&bci, 0xff, 8) != 0 ||
				    check_free_sectors(nftl, block * nftl->EraseSize + i * SECTORSIZE, 
						       SECTORSIZE, 0) != 0) {
					printk("Incorrect free sector %d in block %d: "
					       "marking it as ignored\n",
					       i, block);

					/* sector not free actually : mark it as SECTOR_IGNORE  */
					bci.Status = SECTOR_IGNORE;
					bci.Status1 = SECTOR_IGNORE;
					MTD_WRITEOOB(nftl->mtd,
						     block * nftl->EraseSize + i * SECTORSIZE,
						     8, &retlen, (char *)&bci);
				}
				break;
			default:
				break;
			}
		}

		/* proceed to next Erase Unit on the chain */
		block = nftl->ReplUnitTable[block];
		if (!(block == BLOCK_NIL || block < nftl->nb_blocks))
			printk("incorrect ReplUnitTable[] : %d\n", block);
		if (block == BLOCK_NIL || block >= nftl->nb_blocks)
			break;
	}
}

/* calc_chain_lenght: Walk through a Virtual Unit Chain and estimate chain length */
static int calc_chain_length(struct NFTLrecord *nftl, unsigned int first_block)
{
	unsigned int length = 0, block = first_block;

	for (;;) {
		length++;
		/* avoid infinite loops, although this is guaranted not to
		   happen because of the previous checks */
		if (length >= nftl->nb_blocks) {
			printk("nftl: length too long %d !\n", length);
			break;
		}

		block = nftl->ReplUnitTable[block];
		if (!(block == BLOCK_NIL || block < nftl->nb_blocks))
			printk("incorrect ReplUnitTable[] : %d\n", block);
		if (block == BLOCK_NIL || block >= nftl->nb_blocks)
			break;
	}
	return length;
}

/* format_chain: Format an invalid Virtual Unit chain. It frees all the Erase Units in a
 *	Virtual Unit Chain, i.e. all the units are disconnected.
 *
 *	It is not stricly correct to begin from the first block of the chain because
 *	if we stop the code, we may see again a valid chain if there was a first_block
 *	flag in a block inside it. But is it really a problem ?
 *
 * FixMe: Figure out what the last statesment means. What if power failure when we are
 *	in the for (;;) loop formatting blocks ??
 */
static void format_chain(struct NFTLrecord *nftl, unsigned int first_block)
{
	unsigned int block = first_block, block1;

	printk("Formatting chain at block %d\n", first_block);

	for (;;) {
		block1 = nftl->ReplUnitTable[block];

		printk("Formatting block %d\n", block);
		if (NFTL_formatblock(nftl, block) < 0) {
			/* cannot format !!!! Mark it as Bad Unit,
			   FixMe: update the BadUnitTable on disk */
			nftl->ReplUnitTable[block] = BLOCK_RESERVED;
		} else {
			nftl->ReplUnitTable[block] = BLOCK_FREE;
		}

		/* goto next block on the chain */
		block = block1;

		if (!(block == BLOCK_NIL || block < nftl->nb_blocks))
			printk("incorrect ReplUnitTable[] : %d\n", block);
		if (block == BLOCK_NIL || block >= nftl->nb_blocks)
			break;
	}
}

/* check_and_mark_free_block: Verify that a block is free in the NFTL sense (valid erase mark) or
 *	totally free (only 0xff).
 *
 * Definition: Free Erase Unit -- A properly erased/formatted Free Erase Unit should have meet the
 *	following critia:
 *	1. */
static int check_and_mark_free_block(struct NFTLrecord *nftl, int block)
{
	struct nftl_uci1 h1;
	unsigned int erase_mark;
	int i, retlen;
	unsigned char buf[SECTORSIZE];

	/* check erase mark. */
	if (MTD_READOOB(nftl->mtd, block * nftl->EraseSize + SECTORSIZE + 8, 8, 
			&retlen, (char *)&h1) < 0)
		return -1;

	erase_mark = le16_to_cpu ((h1.EraseMark | h1.EraseMark1));
	if (erase_mark != ERASE_MARK) {
		/* if no erase mark, the block must be totally free. This is
		   possible in two cases : empty filsystem or interrupted erase (very unlikely) */
		if (check_free_sectors (nftl, block * nftl->EraseSize, nftl->EraseSize, 1) != 0)
			return -1;

		/* free block : write erase mark */
		h1.EraseMark = cpu_to_le16(ERASE_MARK);
		h1.EraseMark1 = cpu_to_le16(ERASE_MARK);
		h1.WearInfo = cpu_to_le32(0);
		if (MTD_WRITEOOB(nftl->mtd, block * nftl->EraseSize + SECTORSIZE + 8, 8, 
				 &retlen, (char *)&h1) < 0)
			return -1;
	} else {
#if 0
		/* if erase mark present, need to skip it when doing check */
		for (i = 0; i < nftl->EraseSize; i += SECTORSIZE) {
			/* check free sector */
			if (check_free_sectors (nftl, block * nftl->EraseSize + i,
						SECTORSIZE, 0) != 0)
				return -1;

			if (MTD_READOOB(nftl->mtd, block * nftl->EraseSize + i,
					16, &retlen, buf) < 0)
				return -1;
			if (i == SECTORSIZE) {
				/* skip erase mark */
				if (memcmpb(buf, 0xff, 8))
					return -1;
			} else {
				if (memcmpb(buf, 0xff, 16))
					return -1;
			}
		}
#endif
	}

	return 0;
}

/* get_fold_mark: Read fold mark from Unit Control Information #2, we use FOLD_MARK_IN_PROGRESS
 *	to indicate that we are in the progression of a Virtual Unit Chain folding. If the UCI #2
 *	is FOLD_MARK_IN_PROGRESS when mounting the NFTL, the (previous) folding process is interrupted
 *	for some reason. A clean up/check of the VUC is neceressary in this case.
 *
 * WARNING: return 0 if read error
 */
static int get_fold_mark(struct NFTLrecord *nftl, unsigned int block)
{
	struct nftl_uci2 uci;
	int retlen;

	if (MTD_READOOB(nftl->mtd, block * nftl->EraseSize + 2 * SECTORSIZE + 8,
			8, &retlen, (char *)&uci) < 0)
		return 0;

	return le16_to_cpu((uci.FoldMark | uci.FoldMark1));
}

int NFTL_mount(struct NFTLrecord *s)
{
	int i;
	unsigned int first_logical_block, logical_block, rep_block, nb_erases, erase_mark;
	unsigned int block, first_block, is_first_block;
	int chain_length, do_format_chain;
	struct nftl_uci0 h0;
	struct nftl_uci1 h1;
	int retlen;

	/* XXX: will be suppressed */
	s->lastEUN = s->nb_blocks - 1;

	/* memory alloc */
	s->EUNtable = kmalloc(s->nb_blocks * sizeof(u16), GFP_KERNEL);
	s->ReplUnitTable = kmalloc(s->nb_blocks * sizeof(u16), GFP_KERNEL);
	if (!s->EUNtable || !s->ReplUnitTable) {
	fail:
		if (s->EUNtable)
			kfree(s->EUNtable);
		if (s->ReplUnitTable)
			kfree(s->ReplUnitTable);
		return -1;
	}

	/* mark all blocks as potentially containing data */
	for (i = 0; i < s->nb_blocks; i++) { 
		s->ReplUnitTable[i] = BLOCK_NOTEXPLORED;
	}

	/* search for NFTL MediaHeader and Spare NFTL Media Header */
	if (find_boot_record(s) < 0) {
		printk("Could not find valid boot record\n");
		goto fail;
	}

	/* mark the bios blocks (blocks before NFTL MediaHeader) as reserved */
	for (i = 0; i < s->nb_boot_blocks; i++)
		s->ReplUnitTable[i] = BLOCK_RESERVED;

	/* also mark the boot records (NFTL MediaHeader) blocks as reserved */
	if (s->MediaUnit != BLOCK_NIL)
		s->ReplUnitTable[s->MediaUnit] = BLOCK_RESERVED;
	if (s->SpareMediaUnit != BLOCK_NIL)
		s->ReplUnitTable[s->SpareMediaUnit] = BLOCK_RESERVED;

	/* init the logical to physical table */
	for (i = 0; i < s->nb_blocks; i++) {
		s->EUNtable[i] = BLOCK_NIL;
	}

	/* first pass : explore each block chain */
	first_logical_block = 0;
	for (first_block = 0; first_block < s->nb_blocks; first_block++) {
		/* if the block was not already explored, we can look at it */
		if (s->ReplUnitTable[first_block] == BLOCK_NOTEXPLORED) {
			block = first_block;
			chain_length = 0;
			do_format_chain = 0;

			for (;;) {
				/* read the block header. If error, we format the chain */
				if (MTD_READOOB(s->mtd, block * s->EraseSize + 8, 8, 
						&retlen, (char *)&h0) < 0 ||
				    MTD_READOOB(s->mtd, block * s->EraseSize + SECTORSIZE + 8, 8, 
						&retlen, (char *)&h1) < 0) {
					s->ReplUnitTable[block] = BLOCK_NIL;
					do_format_chain = 1;
					break;
				}

				logical_block = le16_to_cpu ((h0.VirtUnitNum | h0.SpareVirtUnitNum));
				rep_block = le16_to_cpu ((h0.ReplUnitNum | h0.SpareReplUnitNum));
				nb_erases = le32_to_cpu (h1.WearInfo);
				erase_mark = le16_to_cpu ((h1.EraseMark | h1.EraseMark1));

				is_first_block = !(logical_block >> 15);
				logical_block = logical_block & 0x7fff;

				/* invalid/free block test */
				if (erase_mark != ERASE_MARK || logical_block >= s->nb_blocks) {
					if (chain_length == 0) {
						/* if not currently in a chain, we can handle it safely */
						if (check_and_mark_free_block(s, block) < 0) {
							/* not really free: format it */
							printk("Formatting block %d\n", block);
							if (NFTL_formatblock(s, block) < 0) {
								/* could not format: reserve the block */
								s->ReplUnitTable[block] = BLOCK_RESERVED;
							} else {
								s->ReplUnitTable[block] = BLOCK_FREE;
							}
						} else {
							/* free block: mark it */
							s->ReplUnitTable[block] = BLOCK_FREE;
						}
						/* directly examine the next block. */
						goto examine_ReplUnitTable;
					} else {
						/* the block was in a chain : this is bad. We
						   must format all the chain */
						printk("Block %d: free but referenced in chain %d\n",
						       block, first_block);
						s->ReplUnitTable[block] = BLOCK_NIL;
						do_format_chain = 1;
						break;
					}
				}

				/* we accept only first blocks here */
				if (chain_length == 0) {
					/* this block is not the first block in chain :
					   ignore it, it will be included in a chain
					   later, or marked as not explored */
					if (!is_first_block)
						goto examine_ReplUnitTable;
					first_logical_block = logical_block;
				} else {
					if (logical_block != first_logical_block) {
						printk("Block %d: incorrect logical block: %d expected: %d\n", 
						       block, logical_block, first_logical_block);
						/* the chain is incorrect : we must format it,
						   but we need to read it completly */
						do_format_chain = 1;
					}
					if (is_first_block) {
						/* we accept that a block is marked as first
						   block while being last block in a chain
						   only if the chain is being folded */
						if (get_fold_mark(s, block) != FOLD_MARK_IN_PROGRESS ||
						    rep_block != 0xffff) {
							printk("Block %d: incorrectly marked as first block in chain\n",
							       block);
							/* the chain is incorrect : we must format it,
							   but we need to read it completly */
							do_format_chain = 1;
						} else {
							printk("Block %d: folding in progress - ignoring first block flag\n",
							       block);
						}
					}
				}
				chain_length++;
				if (rep_block == 0xffff) {
					/* no more blocks after */
					s->ReplUnitTable[block] = BLOCK_NIL;
					break;
				} else if (rep_block >= s->nb_blocks) {
					printk("Block %d: referencing invalid block %d\n", 
					       block, rep_block);
					do_format_chain = 1;
					s->ReplUnitTable[block] = BLOCK_NIL;
					break;
				} else if (s->ReplUnitTable[rep_block] != BLOCK_NOTEXPLORED) {
					/* same problem as previous 'is_first_block' test:
					   we accept that the last block of a chain has
					   the first_block flag set if folding is in
					   progress. We handle here the case where the
					   last block appeared first */
					if (s->ReplUnitTable[rep_block] == BLOCK_NIL &&
					    s->EUNtable[first_logical_block] == rep_block &&
					    get_fold_mark(s, first_block) == FOLD_MARK_IN_PROGRESS) {
						/* EUNtable[] will be set after */
						printk("Block %d: folding in progress - ignoring first block flag\n",
						       rep_block);
						s->ReplUnitTable[block] = rep_block;
						s->EUNtable[first_logical_block] = BLOCK_NIL;
					} else {
						printk("Block %d: referencing block %d already in another chain\n", 
						       block, rep_block);
						/* XXX: should handle correctly fold in progress chains */
						do_format_chain = 1;
						s->ReplUnitTable[block] = BLOCK_NIL;
					}
					break;
				} else {
					/* this is OK */
					s->ReplUnitTable[block] = rep_block;
					block = rep_block;
				}
			}

			/* the chain was completely explored. Now we can decide
			   what to do with it */
			if (do_format_chain) {
				/* invalid chain : format it */
				format_chain(s, first_block);
			} else {
				unsigned int first_block1, chain_to_format, chain_length1;
				int fold_mark;
				
				/* valid chain : get foldmark */
				fold_mark = get_fold_mark(s, first_block);
				if (fold_mark == 0) {
					/* cannot get foldmark : format the chain */
					printk("Could read foldmark at block %d\n", first_block);
					format_chain(s, first_block);
				} else {
					if (fold_mark == FOLD_MARK_IN_PROGRESS)
						check_sectors_in_chain(s, first_block);

					/* now handle the case where we find two chains at the
					   same virtual address : we select the longer one,
					   because the shorter one is the one which was being
					   folded if the folding was not done in place */
					first_block1 = s->EUNtable[first_logical_block];
					if (first_block1 != BLOCK_NIL) {
						/* XXX: what to do if same length ? */
						chain_length1 = calc_chain_length(s, first_block1);
						printk("Two chains at blocks %d (len=%d) and %d (len=%d)\n", 
						       first_block1, chain_length1, first_block, chain_length);
						
						if (chain_length >= chain_length1) {
							chain_to_format = first_block1;
							s->EUNtable[first_logical_block] = first_block;
						} else {
							chain_to_format = first_block;
						}
						format_chain(s, chain_to_format);
					} else {
						s->EUNtable[first_logical_block] = first_block;
					}
				}
			}
		}
	examine_ReplUnitTable:
	}

	/* second pass to format unreferenced blocks  and init free block count */
	s->numfreeEUNs = 0;
	s->LastFreeEUN = BLOCK_NIL;

	for (block = 0; block < s->nb_blocks; block++) {
		if (s->ReplUnitTable[block] == BLOCK_NOTEXPLORED) {
			printk("Unreferenced block %d, formatting it\n", block);
			if (NFTL_formatblock(s, block) < 0)
				s->ReplUnitTable[block] = BLOCK_RESERVED;
			else
				s->ReplUnitTable[block] = BLOCK_FREE;
		}
		if (s->ReplUnitTable[block] == BLOCK_FREE) {
			s->numfreeEUNs++;
			s->LastFreeEUN = block;
		}
	}

	return 0;
}

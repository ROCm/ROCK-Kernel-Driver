/*
 * truncate.c
 *
 * PURPOSE
 *	Truncate handling routines for the OSTA-UDF(tm) filesystem.
 *
 * CONTACTS
 *	E-mail regarding any portion of the Linux UDF file system should be
 *	directed to the development team mailing list (run by majordomo):
 *		linux_udf@hootie.lvld.hp.com
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 *  (C) 1999-2000 Ben Fennema
 *  (C) 1999 Stelias Computing Inc
 *
 * HISTORY
 *
 *  02/24/99 blf  Created.
 *
 */

#include "udfdecl.h"
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/udf_fs.h>

#include "udf_i.h"
#include "udf_sb.h"

static void extent_trunc(struct inode * inode, lb_addr bloc, int extoffset,
	lb_addr eloc, Uint8 etype, Uint32 elen, struct buffer_head **bh, Uint32 nelen)
{
	lb_addr neloc = { 0, 0 };
	int blocks = inode->i_sb->s_blocksize / 512;
	int last_block = (elen + inode->i_sb->s_blocksize - 1) >> inode->i_sb->s_blocksize_bits;
	int first_block = (nelen + inode->i_sb->s_blocksize - 1) >> inode->i_sb->s_blocksize_bits;

	if (nelen)
	{
		neloc = eloc;
		nelen = (etype << 30) | nelen;
	}

	if (elen != nelen)
	{
		udf_write_aext(inode, bloc, &extoffset, neloc, nelen, bh, 0);
		if (last_block - first_block > 0)
		{
			if (etype == EXTENT_RECORDED_ALLOCATED)
			{
				inode->i_blocks -= (blocks * (last_block - first_block));
				mark_inode_dirty(inode);
			}
			if (etype != EXTENT_NOT_RECORDED_NOT_ALLOCATED)
				udf_free_blocks(inode, eloc, first_block, last_block - first_block);
		}
	}
}

void udf_trunc(struct inode * inode)
{
	lb_addr bloc, eloc, neloc = { 0, 0 };
	Uint32 extoffset, elen, offset, nelen = 0, lelen = 0, lenalloc;
	int etype;
	int first_block = inode->i_size >> inode->i_sb->s_blocksize_bits;
	struct buffer_head *bh = NULL;
	int adsize;

	if (UDF_I_ALLOCTYPE(inode) == ICB_FLAG_AD_SHORT)
		adsize = sizeof(short_ad);
	else if (UDF_I_ALLOCTYPE(inode) == ICB_FLAG_AD_LONG)
		adsize = sizeof(long_ad);
	else
		adsize = 0;

	etype = inode_bmap(inode, first_block, &bloc, &extoffset, &eloc, &elen, &offset, &bh);
	offset = (offset << inode->i_sb->s_blocksize_bits) |
		(inode->i_size & (inode->i_sb->s_blocksize - 1));
	if (etype != -1)
	{
		extoffset -= adsize;
		extent_trunc(inode, bloc, extoffset, eloc, etype, elen, &bh, offset);
		extoffset += adsize;

		if (offset)
			lenalloc = extoffset;
		else
			lenalloc = extoffset - adsize;

		if (!memcmp(&UDF_I_LOCATION(inode), &bloc, sizeof(lb_addr)))
			lenalloc -= udf_file_entry_alloc_offset(inode);
		else
			lenalloc -= sizeof(struct AllocExtDesc);

		while ((etype = udf_current_aext(inode, &bloc, &extoffset, &eloc, &elen, &bh, 0)) != -1)
		{
			if (etype == EXTENT_NEXT_EXTENT_ALLOCDECS)
			{
				udf_write_aext(inode, bloc, &extoffset, neloc, nelen, &bh, 0);
				extoffset = 0;
				if (lelen)
				{
					if (!memcmp(&UDF_I_LOCATION(inode), &bloc, sizeof(lb_addr)))
						memset(bh->b_data, 0x00, udf_file_entry_alloc_offset(inode));
					else
						memset(bh->b_data, 0x00, sizeof(struct AllocExtDesc));
					udf_free_blocks(inode, bloc, 0, lelen);
				}
				else
				{
					if (!memcmp(&UDF_I_LOCATION(inode), &bloc, sizeof(lb_addr)))
					{
						UDF_I_LENALLOC(inode) = lenalloc;
						mark_inode_dirty(inode);
					}
					else
					{
						struct AllocExtDesc *aed = (struct AllocExtDesc *)(bh->b_data);
						aed->lengthAllocDescs = cpu_to_le32(lenalloc);
						udf_update_tag(bh->b_data, lenalloc +
							sizeof(struct AllocExtDesc));
						mark_buffer_dirty(bh);
					}
				}

				udf_release_data(bh);
				bh = NULL;

				bloc = eloc;
				if (elen)
					lelen = (elen + inode->i_sb->s_blocksize - 1) >>
						inode->i_sb->s_blocksize_bits;
				else
					lelen = 1;
			}
			else
			{
				extent_trunc(inode, bloc, extoffset, eloc, etype, elen, &bh, 0);
				extoffset += adsize;
			}
		}

		if (lelen)
		{
			if (!memcmp(&UDF_I_LOCATION(inode), &bloc, sizeof(lb_addr)))
				memset(bh->b_data, 0x00, udf_file_entry_alloc_offset(inode));
			else
				memset(bh->b_data, 0x00, sizeof(struct AllocExtDesc));
			udf_free_blocks(inode, bloc, 0, lelen);
		}
		else
		{
			if (!memcmp(&UDF_I_LOCATION(inode), &bloc, sizeof(lb_addr)))
			{
				UDF_I_LENALLOC(inode) = lenalloc;
				mark_inode_dirty(inode);
			}
			else
			{
				struct AllocExtDesc *aed = (struct AllocExtDesc *)(bh->b_data);
				aed->lengthAllocDescs = cpu_to_le32(lenalloc);
				udf_update_tag(bh->b_data, lenalloc +
					sizeof(struct AllocExtDesc));
				mark_buffer_dirty(bh);
			}
		}
	}
	else if (inode->i_size)
	{
		if (offset)
		{
			extoffset -= adsize;
			etype = udf_next_aext(inode, &bloc, &extoffset, &eloc, &elen, &bh, 1);
			if (etype == EXTENT_NOT_RECORDED_NOT_ALLOCATED)
			{
				extoffset -= adsize;
				elen = (EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30) | (elen + offset);
				udf_write_aext(inode, bloc, &extoffset, eloc, elen, &bh, 0);
			}
			else if (etype == EXTENT_NOT_RECORDED_ALLOCATED)
			{
				lb_addr neloc = { 0, 0 };
				extoffset -= adsize;
				nelen = (EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30) |
					((elen + offset + inode->i_sb->s_blocksize - 1) &
					~(inode->i_sb->s_blocksize - 1));
				udf_write_aext(inode, bloc, &extoffset, neloc, nelen, &bh, 1);
				udf_add_aext(inode, &bloc, &extoffset, eloc, (etype << 30) | elen, &bh, 1);
			}
			else
			{
				if (elen & (inode->i_sb->s_blocksize - 1))
				{
					extoffset -= adsize;
					elen = (EXTENT_RECORDED_ALLOCATED << 30) |
						((elen + inode->i_sb->s_blocksize - 1) &
						~(inode->i_sb->s_blocksize - 1));
					udf_write_aext(inode, bloc, &extoffset, eloc, elen, &bh, 1);
				}
				memset(&eloc, 0x00, sizeof(lb_addr));
				elen = (EXTENT_NOT_RECORDED_NOT_ALLOCATED << 30) | offset;
				udf_add_aext(inode, &bloc, &extoffset, eloc, elen, &bh, 1);
			}
		}
	}

	udf_release_data(bh);
}

void udf_truncate(struct inode * inode)
{
	int err;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
			S_ISLNK(inode->i_mode)))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;

	if (UDF_I_ALLOCTYPE(inode) == ICB_FLAG_AD_IN_ICB)
	{
		if (inode->i_sb->s_blocksize < (udf_file_entry_alloc_offset(inode) +
			inode->i_size))
		{
			udf_expand_file_adinicb(inode, inode->i_size, &err);
			if (UDF_I_ALLOCTYPE(inode) == ICB_FLAG_AD_IN_ICB)
			{
				inode->i_size = UDF_I_LENALLOC(inode);
				return;
			}
			else
				udf_trunc(inode);
		}
		else
			UDF_I_LENALLOC(inode) = inode->i_size;
	}
	else
		udf_trunc(inode);

	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
}

/*
 * directory.c
 *
 * PURPOSE
 *	Directory related functions
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
 */

#include "udfdecl.h"

#if defined(__linux__) && defined(__KERNEL__)

#include <linux/fs.h>
#include <linux/string.h>
#include <linux/udf_fs.h>

#else

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>

#endif

#ifdef __KERNEL__

Uint8 * udf_filead_read(struct inode *dir, Uint8 *tmpad, Uint8 ad_size,
	lb_addr fe_loc, int *pos, int *offset, struct buffer_head **bh, int *error)
{
	int loffset = *offset;
	int block;
	Uint8 *ad;
	int remainder;

	*error = 0;

	ad = (Uint8 *)(*bh)->b_data + *offset;
	*offset += ad_size;

	if (!ad)
	{
		udf_release_data(*bh);
		*error = 1;
		return NULL;
	}

	if (*offset == dir->i_sb->s_blocksize)
	{
		udf_release_data(*bh);
		block = udf_get_lb_pblock(dir->i_sb, fe_loc, ++*pos);
		if (!block)
			return NULL;
		if (!(*bh = udf_tread(dir->i_sb, block, dir->i_sb->s_blocksize)))
			return NULL;
	}
	else if (*offset > dir->i_sb->s_blocksize)
	{
		ad = tmpad;

		remainder = dir->i_sb->s_blocksize - loffset;
		memcpy((Uint8 *)ad, (*bh)->b_data + loffset, remainder);

		udf_release_data(*bh);
		block = udf_get_lb_pblock(dir->i_sb, fe_loc, ++*pos);
		if (!block)
			return NULL;
		if (!((*bh) = udf_tread(dir->i_sb, block, dir->i_sb->s_blocksize)))
			return NULL;

		memcpy((Uint8 *)ad + remainder, (*bh)->b_data, ad_size - remainder);
		*offset = ad_size - remainder;
	}
	return ad;
}

struct FileIdentDesc *
udf_fileident_read(struct inode *dir, loff_t *nf_pos,
	struct udf_fileident_bh *fibh,
	struct FileIdentDesc *cfi,
	lb_addr *bloc, Uint32 *extoffset, 
	lb_addr *eloc, Uint32 *elen,
	Uint32 *offset, struct buffer_head **bh)
{
	struct FileIdentDesc *fi;
	int block;

	fibh->soffset = fibh->eoffset;

	if (fibh->eoffset == dir->i_sb->s_blocksize)
	{
		int lextoffset = *extoffset;

		if (udf_next_aext(dir, bloc, extoffset, eloc, elen, bh, 1) !=
			EXTENT_RECORDED_ALLOCATED)
		{
			return NULL;
		}

		block = udf_get_lb_pblock(dir->i_sb, *eloc, *offset);

		(*offset) ++;

		if ((*offset << dir->i_sb->s_blocksize_bits) >= *elen)
			*offset = 0;
		else
			*extoffset = lextoffset;

		udf_release_data(fibh->sbh);
		if (!(fibh->sbh = fibh->ebh = udf_tread(dir->i_sb, block, dir->i_sb->s_blocksize)))
			return NULL;
		fibh->soffset = fibh->eoffset = 0;
	}
	else if (fibh->sbh != fibh->ebh)
	{
		udf_release_data(fibh->sbh);
		fibh->sbh = fibh->ebh;
	}

	fi = udf_get_fileident(fibh->sbh->b_data, dir->i_sb->s_blocksize,
		&(fibh->eoffset));

	if (!fi)
		return NULL;

	*nf_pos += ((fibh->eoffset - fibh->soffset) >> 2);

	if (fibh->eoffset <= dir->i_sb->s_blocksize)
	{
		memcpy((Uint8 *)cfi, (Uint8 *)fi, sizeof(struct FileIdentDesc));
	}
	else if (fibh->eoffset > dir->i_sb->s_blocksize)
	{
		int lextoffset = *extoffset;

		if (udf_next_aext(dir, bloc, extoffset, eloc, elen, bh, 1) !=
			EXTENT_RECORDED_ALLOCATED)
		{
			return NULL;
		}

		block = udf_get_lb_pblock(dir->i_sb, *eloc, *offset);

		(*offset) ++;

		if ((*offset << dir->i_sb->s_blocksize_bits) >= *elen)
			*offset = 0;
		else
			*extoffset = lextoffset;

		fibh->soffset -= dir->i_sb->s_blocksize;
		fibh->eoffset -= dir->i_sb->s_blocksize;

		if (!(fibh->ebh = udf_tread(dir->i_sb, block, dir->i_sb->s_blocksize)))
			return NULL;

		if (sizeof(struct FileIdentDesc) > - fibh->soffset)
		{
			int fi_len;

			memcpy((Uint8 *)cfi, (Uint8 *)fi, - fibh->soffset);
			memcpy((Uint8 *)cfi - fibh->soffset, fibh->ebh->b_data,
				sizeof(struct FileIdentDesc) + fibh->soffset);

			fi_len = (sizeof(struct FileIdentDesc) + cfi->lengthFileIdent +
				le16_to_cpu(cfi->lengthOfImpUse) + 3) & ~3;

			*nf_pos += ((fi_len - (fibh->eoffset - fibh->soffset)) >> 2);
			fibh->eoffset = fibh->soffset + fi_len;
		}
		else
		{
			memcpy((Uint8 *)cfi, (Uint8 *)fi, sizeof(struct FileIdentDesc));
		}
	}
	return fi;
}
#endif

struct FileIdentDesc * 
udf_get_fileident(void * buffer, int bufsize, int * offset)
{
	struct FileIdentDesc *fi;
	int lengthThisIdent;
	Uint8 * ptr;
	int padlen;

	if ( (!buffer) || (!offset) ) {
#ifdef __KERNEL__
		udf_debug("invalidparms\n, buffer=%p, offset=%p\n", buffer, offset);
#endif
		return NULL;
	}

	ptr = buffer;

	if ( (*offset > 0) && (*offset < bufsize) ) {
		ptr += *offset;
	}
	fi=(struct FileIdentDesc *)ptr;
	if (le16_to_cpu(fi->descTag.tagIdent) != TID_FILE_IDENT_DESC)
	{
#ifdef __KERNEL__
		udf_debug("0x%x != TID_FILE_IDENT_DESC\n",
			le16_to_cpu(fi->descTag.tagIdent));
		udf_debug("offset: %u sizeof: %lu bufsize: %u\n",
			*offset, (unsigned long)sizeof(struct FileIdentDesc), bufsize);
#endif
		return NULL;
	}
	if ( (*offset + sizeof(struct FileIdentDesc)) > bufsize )
	{
		lengthThisIdent = sizeof(struct FileIdentDesc);
	}
	else
		lengthThisIdent = sizeof(struct FileIdentDesc) +
			fi->lengthFileIdent + le16_to_cpu(fi->lengthOfImpUse);

	/* we need to figure padding, too! */
	padlen = lengthThisIdent % UDF_NAME_PAD;
	if (padlen)
		lengthThisIdent += (UDF_NAME_PAD - padlen);
	*offset = *offset + lengthThisIdent;

	return fi;
}

extent_ad *
udf_get_fileextent(void * buffer, int bufsize, int * offset)
{
	extent_ad * ext;
	struct FileEntry *fe;
	Uint8 * ptr;

	if ( (!buffer) || (!offset) )
	{
#ifdef __KERNEL__
		printk(KERN_ERR "udf: udf_get_fileextent() invalidparms\n");
#endif
		return NULL;
	}

	fe = (struct FileEntry *)buffer;

	if ( le16_to_cpu(fe->descTag.tagIdent) != TID_FILE_ENTRY )
	{
#ifdef __KERNEL__
		udf_debug("0x%x != TID_FILE_ENTRY\n",
			le16_to_cpu(fe->descTag.tagIdent));
#endif
		return NULL;
	}

	ptr=(Uint8 *)(fe->extendedAttr) + le32_to_cpu(fe->lengthExtendedAttr);

	if ( (*offset > 0) && (*offset < le32_to_cpu(fe->lengthAllocDescs)) )
	{
		ptr += *offset;
	}

	ext = (extent_ad *)ptr;

	*offset = *offset + sizeof(extent_ad);
	return ext;
}

short_ad *
udf_get_fileshortad(void * buffer, int maxoffset, int *offset, int inc)
{
	short_ad * sa;
	Uint8 * ptr;

	if ( (!buffer) || (!offset) )
	{
#ifdef __KERNEL__
		printk(KERN_ERR "udf: udf_get_fileshortad() invalidparms\n");
#endif
		return NULL;
	}

	ptr = (Uint8 *)buffer;

	if ( (*offset > 0) && (*offset < maxoffset) )
		ptr += *offset;
	else
		return NULL;

	if ((sa = (short_ad *)ptr)->extLength == 0)
		return NULL;
	else if (inc)
		(*offset) += sizeof(short_ad);
	return sa;
}

long_ad *
udf_get_filelongad(void * buffer, int maxoffset, int * offset, int inc)
{
	long_ad * la;
	Uint8 * ptr;

	if ( (!buffer) || !(offset) ) 
	{
#ifdef __KERNEL__
		printk(KERN_ERR "udf: udf_get_filelongad() invalidparms\n");
#endif
		return NULL;
	}

	ptr = (Uint8 *)buffer;

	if ( (*offset > 0) && (*offset < maxoffset) )
		ptr += *offset;
	else
		return NULL;

	if ((la = (long_ad *)ptr)->extLength == 0)
		return NULL;
	else if (inc)
		(*offset) += sizeof(long_ad);
	return la;
}

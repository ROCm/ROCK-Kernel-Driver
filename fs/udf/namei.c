/*
 * namei.c
 *
 * PURPOSE
 *      Inode name handling routines for the OSTA-UDF(tm) filesystem.
 *
 * CONTACTS
 *      E-mail regarding any portion of the Linux UDF file system should be
 *      directed to the development team mailing list (run by majordomo):
 *              linux_udf@hootie.lvld.hp.com
 *
 * COPYRIGHT
 *      This file is distributed under the terms of the GNU General Public
 *      License (GPL). Copies of the GPL can be obtained from:
 *              ftp://prep.ai.mit.edu/pub/gnu/GPL
 *      Each contributing author retains all rights to their own work.
 *
 *  (C) 1998-2000 Ben Fennema
 *  (C) 1999-2000 Stelias Computing Inc
 *
 * HISTORY
 *
 *  12/12/98 blf  Created. Split out the lookup code from dir.c
 *  04/19/99 blf  link, mknod, symlink support
 */

#include "udfdecl.h"

#include "udf_i.h"
#include "udf_sb.h"
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/quotaops.h>
#include <linux/udf_fs.h>

static inline int udf_match(int len, const char * const name, struct qstr *qs)
{
	if (len != qs->len)
		return 0;
	return !memcmp(name, qs->name, len);
}

int udf_write_fi(struct FileIdentDesc *cfi, struct FileIdentDesc *sfi,
	struct udf_fileident_bh *fibh,
	Uint8 *impuse, Uint8 *fileident)
{
	Uint16 crclen = fibh->eoffset - fibh->soffset - sizeof(tag);
	Uint16 crc;
	Uint8 checksum = 0;
	int i;
	int offset;
	Uint16 liu = le16_to_cpu(cfi->lengthOfImpUse);
	Uint8 lfi = cfi->lengthFileIdent;
	int padlen = fibh->eoffset - fibh->soffset - liu - lfi -
		sizeof(struct FileIdentDesc);


	offset = fibh->soffset + sizeof(struct FileIdentDesc);

	if (impuse)
	{
		if (offset + liu < 0)
			memcpy((Uint8 *)sfi->impUse, impuse, liu);
		else if (offset >= 0)
			memcpy(fibh->ebh->b_data + offset, impuse, liu);
		else
		{
			memcpy((Uint8 *)sfi->impUse, impuse, -offset);
			memcpy(fibh->ebh->b_data, impuse - offset, liu + offset);
		}
	}

	offset += liu;

	if (fileident)
	{
		if (offset + lfi < 0)
			memcpy((Uint8 *)sfi->fileIdent + liu, fileident, lfi);
		else if (offset >= 0)
			memcpy(fibh->ebh->b_data + offset, fileident, lfi);
		else
		{
			memcpy((Uint8 *)sfi->fileIdent + liu, fileident, -offset);
			memcpy(fibh->ebh->b_data, fileident - offset, lfi + offset);
		}
	}

	offset += lfi;

	if (offset + padlen < 0)
		memset((Uint8 *)sfi->padding + liu + lfi, 0x00, padlen);
	else if (offset >= 0)
		memset(fibh->ebh->b_data + offset, 0x00, padlen);
	else
	{
		memset((Uint8 *)sfi->padding + liu + lfi, 0x00, -offset);
		memset(fibh->ebh->b_data, 0x00, padlen + offset);
	}

	crc = udf_crc((Uint8 *)cfi + sizeof(tag), sizeof(struct FileIdentDesc) -
		sizeof(tag), 0);

	if (fibh->sbh == fibh->ebh)
		crc = udf_crc((Uint8 *)sfi->impUse,
			crclen + sizeof(tag) - sizeof(struct FileIdentDesc), crc);
	else if (sizeof(struct FileIdentDesc) >= -fibh->soffset)
		crc = udf_crc(fibh->ebh->b_data + sizeof(struct FileIdentDesc) + fibh->soffset,
			crclen + sizeof(tag) - sizeof(struct FileIdentDesc), crc);
	else
	{
		crc = udf_crc((Uint8 *)sfi->impUse,
			-fibh->soffset - sizeof(struct FileIdentDesc), crc);
		crc = udf_crc(fibh->ebh->b_data, fibh->eoffset, crc);
	}

	cfi->descTag.descCRC = cpu_to_le32(crc);
	cfi->descTag.descCRCLength = cpu_to_le16(crclen);

	for (i=0; i<16; i++)
		if (i != 4)
			checksum += ((Uint8 *)&cfi->descTag)[i];

	cfi->descTag.tagChecksum = checksum;
	if (sizeof(struct FileIdentDesc) <= -fibh->soffset)
		memcpy((Uint8 *)sfi, (Uint8 *)cfi, sizeof(struct FileIdentDesc));
	else
	{
		memcpy((Uint8 *)sfi, (Uint8 *)cfi, -fibh->soffset);
		memcpy(fibh->ebh->b_data, (Uint8 *)cfi - fibh->soffset,
			sizeof(struct FileIdentDesc) + fibh->soffset);
	}

	if (fibh->sbh != fibh->ebh)
		mark_buffer_dirty(fibh->ebh);
	mark_buffer_dirty(fibh->sbh);
	return 0;
}

static struct FileIdentDesc *
udf_find_entry(struct inode *dir, struct dentry *dentry,
	struct udf_fileident_bh *fibh,
	struct FileIdentDesc *cfi)
{
	struct FileIdentDesc *fi=NULL;
	loff_t f_pos;
	int block, flen;
	char fname[255];
	char *nameptr;
	Uint8 lfi;
	Uint16 liu;
	loff_t size = (udf_ext0_offset(dir) + dir->i_size) >> 2;
	lb_addr bloc, eloc;
	Uint32 extoffset, elen, offset;
	struct buffer_head *bh = NULL;

	if (!dir)
		return NULL;

	f_pos = (udf_ext0_offset(dir) >> 2);

	fibh->soffset = fibh->eoffset = (f_pos & ((dir->i_sb->s_blocksize - 1) >> 2)) << 2;
	if (inode_bmap(dir, f_pos >> (dir->i_sb->s_blocksize_bits - 2),
		&bloc, &extoffset, &eloc, &elen, &offset, &bh) == EXTENT_RECORDED_ALLOCATED)
	{
		block = udf_get_lb_pblock(dir->i_sb, eloc, offset);
		if ((++offset << dir->i_sb->s_blocksize_bits) < elen)
		{
			if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_SHORT)
				extoffset -= sizeof(short_ad);
			else if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_LONG)
				extoffset -= sizeof(long_ad);
		}
		else
			offset = 0;
	}
	else
	{
		udf_release_data(bh);
		return NULL;
	}

	if (!(fibh->sbh = fibh->ebh = udf_tread(dir->i_sb, block, dir->i_sb->s_blocksize)))
	{
		udf_release_data(bh);
		return NULL;
	}

	while ( (f_pos < size) )
	{
		fi = udf_fileident_read(dir, &f_pos, fibh, cfi, &bloc, &extoffset, &eloc, &elen, &offset, &bh);

		if (!fi)
		{
			if (fibh->sbh != fibh->ebh)
				udf_release_data(fibh->ebh);
			udf_release_data(fibh->sbh);
			udf_release_data(bh);
			return NULL;
		}

		liu = le16_to_cpu(cfi->lengthOfImpUse);
		lfi = cfi->lengthFileIdent;

		if (fibh->sbh == fibh->ebh)
		{
			nameptr = fi->fileIdent + liu;
		}
		else
		{
			int poffset;	/* Unpaded ending offset */

			poffset = fibh->soffset + sizeof(struct FileIdentDesc) + liu + lfi;

			if (poffset >= lfi)
				nameptr = (Uint8 *)(fibh->ebh->b_data + poffset - lfi);
			else
			{
				nameptr = fname;
				memcpy(nameptr, fi->fileIdent + liu, lfi - poffset);
				memcpy(nameptr + lfi - poffset, fibh->ebh->b_data, poffset);
			}
		}

		if ( (cfi->fileCharacteristics & FILE_DELETED) != 0 )
		{
			if ( !UDF_QUERY_FLAG(dir->i_sb, UDF_FLAG_UNDELETE) )
				continue;
		}
	    
		if ( (cfi->fileCharacteristics & FILE_HIDDEN) != 0 )
		{
			if ( !UDF_QUERY_FLAG(dir->i_sb, UDF_FLAG_UNHIDE) )
				continue;
		}

		if (!lfi)
			continue;

		if ((flen = udf_get_filename(nameptr, fname, lfi)))
		{
			if (udf_match(flen, fname, &(dentry->d_name)))
			{
				udf_release_data(bh);
				return fi;
			}
		}
	}
	if (fibh->sbh != fibh->ebh)
		udf_release_data(fibh->ebh);
	udf_release_data(fibh->sbh);
	udf_release_data(bh);
	return NULL;
}

/*
 * udf_lookup
 *
 * PURPOSE
 *	Look-up the inode for a given name.
 *
 * DESCRIPTION
 *	Required - lookup_dentry() will return -ENOTDIR if this routine is not
 *	available for a directory. The filesystem is useless if this routine is
 *	not available for at least the filesystem's root directory.
 *
 *	This routine is passed an incomplete dentry - it must be completed by
 *	calling d_add(dentry, inode). If the name does not exist, then the
 *	specified inode must be set to null. An error should only be returned
 *	when the lookup fails for a reason other than the name not existing.
 *	Note that the directory inode semaphore is held during the call.
 *
 *	Refer to lookup_dentry() in fs/namei.c
 *	lookup_dentry() -> lookup() -> real_lookup() -> .
 *
 * PRE-CONDITIONS
 *	dir			Pointer to inode of parent directory.
 *	dentry			Pointer to dentry to complete.
 *
 * POST-CONDITIONS
 *	<return>		Zero on success.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */

static struct dentry *
udf_lookup(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = NULL;
	struct FileIdentDesc cfi, *fi;
	struct udf_fileident_bh fibh;

	if (dentry->d_name.len > UDF_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

#ifdef UDF_RECOVERY
	/* temporary shorthand for specifying files by inode number */
	if (!strncmp(dentry->d_name.name, ".B=", 3) )
	{
		lb_addr lb = { 0, simple_strtoul(dentry->d_name.name+3, NULL, 0) };
		inode = udf_iget(dir->i_sb, lb);
		if (!inode)
			return ERR_PTR(-EACCES);
	}
	else
#endif /* UDF_RECOVERY */

	if ((fi = udf_find_entry(dir, dentry, &fibh, &cfi)))
	{
		if (fibh.sbh != fibh.ebh)
			udf_release_data(fibh.ebh);
		udf_release_data(fibh.sbh);

		inode = udf_iget(dir->i_sb, lelb_to_cpu(cfi.icb.extLocation));
		if ( !inode )
			return ERR_PTR(-EACCES);
	}
	d_add(dentry, inode);
	return NULL;
}

static struct FileIdentDesc *
udf_add_entry(struct inode *dir, struct dentry *dentry,
	struct udf_fileident_bh *fibh,
	struct FileIdentDesc *cfi, int *err)
{
	struct super_block *sb;
	struct FileIdentDesc *fi=NULL;
	struct ustr unifilename;
	char name[UDF_NAME_LEN], fname[UDF_NAME_LEN];
	int namelen;
	loff_t f_pos;
	int flen;
	char *nameptr;
	loff_t size = (udf_ext0_offset(dir) + dir->i_size) >> 2;
	int nfidlen;
	Uint8 lfi;
	Uint16 liu;
	int block;
	lb_addr bloc, eloc;
	Uint32 extoffset, elen, offset;
	struct buffer_head *bh = NULL;

	sb = dir->i_sb;

	if (dentry)
	{
		if ( !(udf_char_to_ustr(&unifilename, dentry->d_name.name, dentry->d_name.len)) )
		{
			*err = -ENAMETOOLONG;
			return NULL;
		}

		if ( !(namelen = udf_UTF8toCS0(name, &unifilename, UDF_NAME_LEN)) )
		{
			*err = -ENAMETOOLONG;
			return NULL;
		}
	}
	else if (dir->i_size != 0)
	{
		/* WTF??? */
		*err = -ENOENT;
		return NULL;
	}
	else /* .. */
		namelen = 0;

	nfidlen = (sizeof(struct FileIdentDesc) + 0 + namelen + 3) & ~3;

	f_pos = (udf_ext0_offset(dir) >> 2);

	fibh->soffset = fibh->eoffset = (f_pos & ((dir->i_sb->s_blocksize - 1) >> 2)) << 2;
	if (inode_bmap(dir, f_pos >> (dir->i_sb->s_blocksize_bits - 2),
		&bloc, &extoffset, &eloc, &elen, &offset, &bh) == EXTENT_RECORDED_ALLOCATED)
	{
		block = udf_get_lb_pblock(dir->i_sb, eloc, offset);
		if ((++offset << dir->i_sb->s_blocksize_bits) < elen)
		{
			if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_SHORT)
				extoffset -= sizeof(short_ad);
			else if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_LONG)
				extoffset -= sizeof(long_ad);
		}
		else
			offset = 0;

		if (!(fibh->sbh = fibh->ebh = udf_tread(dir->i_sb, block, dir->i_sb->s_blocksize)))
		{
			udf_release_data(bh);
			return NULL;
		}
	
		block = UDF_I_LOCATION(dir).logicalBlockNum;
	
		while ( (f_pos < size) )
		{
			fi = udf_fileident_read(dir, &f_pos, fibh, cfi, &bloc, &extoffset, &eloc, &elen, &offset, &bh);
	
			if (!fi)
			{
				if (fibh->sbh != fibh->ebh)
					udf_release_data(fibh->ebh);
				udf_release_data(fibh->sbh);
				udf_release_data(bh);
				return NULL;
			}
	
			liu = le16_to_cpu(cfi->lengthOfImpUse);
			lfi = cfi->lengthFileIdent;
	
			if (fibh->sbh == fibh->ebh)
				nameptr = fi->fileIdent + liu;
			else
			{
				int poffset;	/* Unpaded ending offset */
	
				poffset = fibh->soffset + sizeof(struct FileIdentDesc) + liu + lfi;
	
				if (poffset >= lfi)
					nameptr = (char *)(fibh->ebh->b_data + poffset - lfi);
				else
				{
					nameptr = fname;
					memcpy(nameptr, fi->fileIdent + liu, lfi - poffset);
					memcpy(nameptr + lfi - poffset, fibh->ebh->b_data, poffset);
				}
			}
	
			if ( (cfi->fileCharacteristics & FILE_DELETED) != 0 )
			{
				if (((sizeof(struct FileIdentDesc) + liu + lfi + 3) & ~3) == nfidlen)
				{
					udf_release_data(bh);
					cfi->descTag.tagSerialNum = cpu_to_le16(1);
					cfi->fileVersionNum = cpu_to_le16(1);
					cfi->fileCharacteristics = 0;
					cfi->lengthFileIdent = namelen;
					cfi->lengthOfImpUse = cpu_to_le16(0);
					if (!udf_write_fi(cfi, fi, fibh, NULL, name))
						return fi;
					else
						return NULL;
				}
			}
	
			if (!lfi || !dentry)
				continue;
	
			if ((flen = udf_get_filename(nameptr, fname, lfi)) &&
			    udf_match(flen, fname, &(dentry->d_name))) {
				if (fibh->sbh != fibh->ebh)
					udf_release_data(fibh->ebh);
				udf_release_data(fibh->sbh);
				udf_release_data(bh);
				*err = -EEXIST;
				return NULL;
			}
		}
	}
	else
	{
		block = udf_get_lb_pblock(dir->i_sb, UDF_I_LOCATION(dir), 0);
		if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_IN_ICB)
		{
			fibh->sbh = fibh->ebh = udf_tread(dir->i_sb, block, dir->i_sb->s_blocksize);
			fibh->soffset = fibh->eoffset = udf_file_entry_alloc_offset(dir);
		}
		else
		{
			fibh->sbh = fibh->ebh = NULL;
			fibh->soffset = fibh->eoffset = sb->s_blocksize;
		}
	}

	f_pos += nfidlen;

	if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_IN_ICB &&
		sb->s_blocksize - fibh->eoffset < nfidlen)
	{
		udf_release_data(bh);
		bh = NULL;
		fibh->soffset -= udf_ext0_offset(dir);
		fibh->eoffset -= udf_ext0_offset(dir);
		f_pos -= (udf_ext0_offset(dir) >> 2);
		if (fibh->sbh != fibh->ebh)
			udf_release_data(fibh->ebh);
		udf_release_data(fibh->sbh);
		if (!(fibh->sbh = fibh->ebh = udf_expand_dir_adinicb(dir, &block, err)))
			return NULL;
		bloc = UDF_I_LOCATION(dir);
		eloc.logicalBlockNum = block;
		eloc.partitionReferenceNum = UDF_I_LOCATION(dir).partitionReferenceNum;
		elen = dir->i_sb->s_blocksize;
		extoffset = udf_file_entry_alloc_offset(dir);
		if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_SHORT)
			extoffset += sizeof(short_ad);
		else if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_LONG)
			extoffset += sizeof(long_ad);
	}

	if (sb->s_blocksize - fibh->eoffset >= nfidlen)
	{
		fibh->soffset = fibh->eoffset;
		fibh->eoffset += nfidlen;
		if (fibh->sbh != fibh->ebh)
		{
			udf_release_data(fibh->sbh);
			fibh->sbh = fibh->ebh;
		}

		if (UDF_I_ALLOCTYPE(dir) != ICB_FLAG_AD_IN_ICB)
			block = eloc.logicalBlockNum + ((elen - 1) >>
				dir->i_sb->s_blocksize_bits);
		else
			block = UDF_I_LOCATION(dir).logicalBlockNum;
				
		fi = (struct FileIdentDesc *)(fibh->sbh->b_data + fibh->soffset);
	}
	else
	{
		fibh->soffset = fibh->eoffset - sb->s_blocksize;
		fibh->eoffset += nfidlen - sb->s_blocksize;
		if (fibh->sbh != fibh->ebh)
		{
			udf_release_data(fibh->sbh);
			fibh->sbh = fibh->ebh;
		}

		block = eloc.logicalBlockNum + ((elen - 1) >>
			dir->i_sb->s_blocksize_bits);

		*err = -ENOSPC;
		if (!(fibh->ebh = udf_bread(dir, f_pos >> (dir->i_sb->s_blocksize_bits - 2), 1, err)))
		{
			udf_release_data(bh);
			udf_release_data(fibh->sbh);
			return NULL;
		}

		if (!(fibh->soffset))
		{
			if (udf_next_aext(dir, &bloc, &extoffset, &eloc, &elen, &bh, 1) ==
				EXTENT_RECORDED_ALLOCATED)
			{
				block = eloc.logicalBlockNum + ((elen - 1) >>
					dir->i_sb->s_blocksize_bits);
			}
			else
				block ++;

			udf_release_data(fibh->sbh);
			fibh->sbh = fibh->ebh;
			fi = (struct FileIdentDesc *)(fibh->sbh->b_data);
		}
		else
		{
			fi = (struct FileIdentDesc *)
				(fibh->sbh->b_data + sb->s_blocksize + fibh->soffset);
		}
	}

	memset(cfi, 0, sizeof(struct FileIdentDesc));
	udf_new_tag((char *)cfi, TID_FILE_IDENT_DESC, 2, 1, block, sizeof(tag));
	cfi->fileVersionNum = cpu_to_le16(1);
	cfi->lengthFileIdent = namelen;
	cfi->lengthOfImpUse = cpu_to_le16(0);
	if (!udf_write_fi(cfi, fi, fibh, NULL, name))
	{
		udf_release_data(bh);
		dir->i_size += nfidlen;
		if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_IN_ICB)
			UDF_I_LENALLOC(dir) += nfidlen;
		dir->i_version = ++event;
		mark_inode_dirty(dir);
		return fi;
	}
	else
	{
		udf_release_data(bh);
		if (fibh->sbh != fibh->ebh)
			udf_release_data(fibh->ebh);
		udf_release_data(fibh->sbh);
		return NULL;
	}
}

static int udf_delete_entry(struct FileIdentDesc *fi,
	struct udf_fileident_bh *fibh,
	struct FileIdentDesc *cfi)
{
	cfi->fileCharacteristics |= FILE_DELETED;
	return udf_write_fi(cfi, fi, fibh, NULL, NULL);
}

static int udf_create(struct inode *dir, struct dentry *dentry, int mode)
{
	struct udf_fileident_bh fibh;
	struct inode *inode;
	struct FileIdentDesc cfi, *fi;
	int err;

	inode = udf_new_inode(dir, mode, &err);
	if (!inode)
		return err;

	if (UDF_I_ALLOCTYPE(inode) == ICB_FLAG_AD_IN_ICB)
		inode->i_data.a_ops = &udf_adinicb_aops;
	else
		inode->i_data.a_ops = &udf_aops;
	inode->i_op = &udf_file_inode_operations;
	inode->i_fop = &udf_file_operations;
	inode->i_mode = mode;
	mark_inode_dirty(inode);

	if (!(fi = udf_add_entry(dir, dentry, &fibh, &cfi, &err)))
	{
		inode->i_nlink --;
		mark_inode_dirty(inode);
		iput(inode);
		return err;
	}
	cfi.icb.extLength = cpu_to_le32(inode->i_sb->s_blocksize);
	cfi.icb.extLocation = cpu_to_lelb(UDF_I_LOCATION(inode));
	*(Uint32 *)((struct ADImpUse *)cfi.icb.impUse)->impUse =
		cpu_to_le32(UDF_I_UNIQUE(inode) & 0x00000000FFFFFFFFUL);
	udf_write_fi(&cfi, fi, &fibh, NULL, NULL);
	if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_IN_ICB)
	{
		mark_inode_dirty(dir);
		dir->i_version = ++event;
	}
	if (fibh.sbh != fibh.ebh)
		udf_release_data(fibh.ebh);
	udf_release_data(fibh.sbh);
	d_instantiate(dentry, inode);
	return 0;
}

static int udf_mknod(struct inode * dir, struct dentry * dentry, int mode, int rdev)
{
	struct inode * inode;
	struct udf_fileident_bh fibh;
	int err;
	struct FileIdentDesc cfi, *fi;

	err = -EIO;
	inode = udf_new_inode(dir, mode, &err);
	if (!inode)
		goto out;

	inode->i_uid = current->fsuid;
	init_special_inode(inode, mode, rdev);
	if (!(fi = udf_add_entry(dir, dentry, &fibh, &cfi, &err)))
	{
		inode->i_nlink --;
		mark_inode_dirty(inode);
		iput(inode);
		return err;
	}
	cfi.icb.extLength = cpu_to_le32(inode->i_sb->s_blocksize);
	cfi.icb.extLocation = cpu_to_lelb(UDF_I_LOCATION(inode));
	*(Uint32 *)((struct ADImpUse *)cfi.icb.impUse)->impUse =
		cpu_to_le32(UDF_I_UNIQUE(inode) & 0x00000000FFFFFFFFUL);
	udf_write_fi(&cfi, fi, &fibh, NULL, NULL);
	if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_IN_ICB)
	{
		mark_inode_dirty(dir);
		dir->i_version = ++event;
	}
	mark_inode_dirty(inode);

	if (fibh.sbh != fibh.ebh)
		udf_release_data(fibh.ebh);
	udf_release_data(fibh.sbh);
	d_instantiate(dentry, inode);
	err = 0;
out:
	return err;
}

static int udf_mkdir(struct inode * dir, struct dentry * dentry, int mode)
{
	struct inode * inode;
	struct udf_fileident_bh fibh;
	int err;
	struct FileIdentDesc cfi, *fi;

	err = -EMLINK;
	if (dir->i_nlink >= (256<<sizeof(dir->i_nlink))-1)
		goto out;

	err = -EIO;
	inode = udf_new_inode(dir, S_IFDIR, &err);
	if (!inode)
		goto out;

	inode->i_op = &udf_dir_inode_operations;
	inode->i_fop = &udf_dir_operations;
	inode->i_size = 0;
	if (!(fi = udf_add_entry(inode, NULL, &fibh, &cfi, &err)))
	{
		inode->i_nlink--;
		mark_inode_dirty(inode);
		iput(inode);
		goto out;
	}
	inode->i_nlink = 2;
	cfi.icb.extLength = cpu_to_le32(inode->i_sb->s_blocksize);
	cfi.icb.extLocation = cpu_to_lelb(UDF_I_LOCATION(dir));
	*(Uint32 *)((struct ADImpUse *)cfi.icb.impUse)->impUse =
		cpu_to_le32(UDF_I_UNIQUE(dir) & 0x00000000FFFFFFFFUL);
	cfi.fileCharacteristics = FILE_DIRECTORY | FILE_PARENT;
	udf_write_fi(&cfi, fi, &fibh, NULL, NULL);
	udf_release_data(fibh.sbh);
	inode->i_mode = S_IFDIR | mode;
	if (dir->i_mode & S_ISGID)
		inode->i_mode |= S_ISGID;
	mark_inode_dirty(inode);

	if (!(fi = udf_add_entry(dir, dentry, &fibh, &cfi, &err)))
	{
		inode->i_nlink = 0;
		mark_inode_dirty(inode);
		iput(inode);
		goto out;
	}
	cfi.icb.extLength = cpu_to_le32(inode->i_sb->s_blocksize);
	cfi.icb.extLocation = cpu_to_lelb(UDF_I_LOCATION(inode));
	*(Uint32 *)((struct ADImpUse *)cfi.icb.impUse)->impUse =
		cpu_to_le32(UDF_I_UNIQUE(inode) & 0x00000000FFFFFFFFUL);
	cfi.fileCharacteristics |= FILE_DIRECTORY;
	udf_write_fi(&cfi, fi, &fibh, NULL, NULL);
	dir->i_version = ++event;
	dir->i_nlink++;
	mark_inode_dirty(dir);
	d_instantiate(dentry, inode);
	if (fibh.sbh != fibh.ebh)
		udf_release_data(fibh.ebh);
	udf_release_data(fibh.sbh);
	err = 0;
out:
	return err;
}

static int empty_dir(struct inode *dir)
{
	struct FileIdentDesc *fi, cfi;
	struct udf_fileident_bh fibh;
	loff_t f_pos;
	loff_t size = (udf_ext0_offset(dir) + dir->i_size) >> 2;
	int block;
	lb_addr bloc, eloc;
	Uint32 extoffset, elen, offset;
	struct buffer_head *bh = NULL;

	f_pos = (udf_ext0_offset(dir) >> 2);

	fibh.soffset = fibh.eoffset = (f_pos & ((dir->i_sb->s_blocksize - 1) >> 2)) << 2;
	if (inode_bmap(dir, f_pos >> (dir->i_sb->s_blocksize_bits - 2),
		&bloc, &extoffset, &eloc, &elen, &offset, &bh) == EXTENT_RECORDED_ALLOCATED)
	{
		block = udf_get_lb_pblock(dir->i_sb, eloc, offset);
		if ((++offset << dir->i_sb->s_blocksize_bits) < elen)
		{
			if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_SHORT)
				extoffset -= sizeof(short_ad);
			else if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_LONG)
				extoffset -= sizeof(long_ad);
		}
		else
			offset = 0;
	}
	else
	{
		udf_release_data(bh);
		return 0;
	}

	if (!(fibh.sbh = fibh.ebh = udf_tread(dir->i_sb, block, dir->i_sb->s_blocksize)))
		return 0;

	while ( (f_pos < size) )
	{
		fi = udf_fileident_read(dir, &f_pos, &fibh, &cfi, &bloc, &extoffset, &eloc, &elen, &offset, &bh);

		if (!fi)
		{
			if (fibh.sbh != fibh.ebh)
				udf_release_data(fibh.ebh);
			udf_release_data(fibh.sbh);
			udf_release_data(bh);
			return 0;
		}

		if (cfi.lengthFileIdent && (cfi.fileCharacteristics & FILE_DELETED) == 0)
		{
			udf_release_data(bh);
			return 0;
		}
	}
	if (fibh.sbh != fibh.ebh)
		udf_release_data(fibh.ebh);
	udf_release_data(fibh.sbh);
	udf_release_data(bh);
	return 1;
}

static int udf_rmdir(struct inode * dir, struct dentry * dentry)
{
	int retval;
	struct inode * inode;
	struct udf_fileident_bh fibh;
	struct FileIdentDesc *fi, cfi;

	retval = -ENOENT;
	fi = udf_find_entry(dir, dentry, &fibh, &cfi);
	if (!fi)
		goto out;

	inode = dentry->d_inode;
	DQUOT_INIT(inode);

	retval = -EIO;
	if (udf_get_lb_pblock(dir->i_sb, lelb_to_cpu(cfi.icb.extLocation), 0) != inode->i_ino)
		goto end_rmdir;
	retval = -ENOTEMPTY;
	if (!empty_dir(inode))
		goto end_rmdir;
	retval = udf_delete_entry(fi, &fibh, &cfi);
	dir->i_version = ++event;
	if (retval)
		goto end_rmdir;
	if (inode->i_nlink != 2)
		udf_warning(inode->i_sb, "udf_rmdir",
			"empty directory has nlink != 2 (%d)",
			inode->i_nlink);
	inode->i_version = ++event;
	inode->i_nlink = 0;
	inode->i_size = 0;
	mark_inode_dirty(inode);
	dir->i_nlink --;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	UDF_I_UCTIME(inode) = UDF_I_UCTIME(dir) = UDF_I_UMTIME(dir) = CURRENT_UTIME;
	mark_inode_dirty(dir);

end_rmdir:
	if (fibh.sbh != fibh.ebh)
		udf_release_data(fibh.ebh);
	udf_release_data(fibh.sbh);
out:
	return retval;
}

static int udf_unlink(struct inode * dir, struct dentry * dentry)
{
	int retval;
	struct inode * inode;
	struct udf_fileident_bh fibh;
	struct FileIdentDesc *fi;
	struct FileIdentDesc cfi;

	retval = -ENOENT;
	fi = udf_find_entry(dir, dentry, &fibh, &cfi);
	if (!fi)
		goto out;

	inode = dentry->d_inode;
	DQUOT_INIT(inode);

	retval = -EIO;

	if (udf_get_lb_pblock(dir->i_sb, lelb_to_cpu(cfi.icb.extLocation), 0) !=
		inode->i_ino)
	{
		goto end_unlink;
	}

	if (!inode->i_nlink)
	{
		udf_debug("Deleting nonexistent file (%lu), %d\n",
			inode->i_ino, inode->i_nlink);
		inode->i_nlink = 1;
	}
	retval = udf_delete_entry(fi, &fibh, &cfi);
	if (retval)
		goto end_unlink;
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	UDF_I_UCTIME(dir) = UDF_I_UMTIME(dir) = CURRENT_UTIME;
	mark_inode_dirty(dir);
	inode->i_nlink--;
	mark_inode_dirty(inode);
	inode->i_ctime = dir->i_ctime;
	retval = 0;

end_unlink:
	if (fibh.sbh != fibh.ebh)
		udf_release_data(fibh.ebh);
	udf_release_data(fibh.sbh);
out:
	return retval;
}

static int udf_symlink(struct inode * dir, struct dentry * dentry, const char * symname)
{
	struct inode * inode;
	struct PathComponent *pc;
	char *compstart;
	struct udf_fileident_bh fibh;
	struct buffer_head *bh = NULL;
	int eoffset, elen = 0;
	struct FileIdentDesc *fi;
	struct FileIdentDesc cfi;
	char *ea;
	int err;
	int block;

	if (!(inode = udf_new_inode(dir, S_IFLNK, &err)))
		goto out;

	inode->i_mode = S_IFLNK | S_IRWXUGO;
	inode->i_data.a_ops = &udf_symlink_aops;
	inode->i_op = &page_symlink_inode_operations;

	if (UDF_I_ALLOCTYPE(inode) != ICB_FLAG_AD_IN_ICB)
	{
		struct buffer_head *bh = NULL;
		lb_addr bloc, eloc;
		Uint32 elen, extoffset;

		block = udf_new_block(inode,
			UDF_I_LOCATION(inode).partitionReferenceNum,
			UDF_I_LOCATION(inode).logicalBlockNum, &err);
		if (!block)
			goto out_no_entry;
		bloc = UDF_I_LOCATION(inode);
		eloc.logicalBlockNum = block;
		eloc.partitionReferenceNum = UDF_I_LOCATION(inode).partitionReferenceNum;
		elen = inode->i_sb->s_blocksize;
		extoffset = udf_file_entry_alloc_offset(inode);
		udf_add_aext(inode, &bloc, &extoffset, eloc, elen, &bh, 0);
		udf_release_data(bh);

		inode->i_blocks = inode->i_sb->s_blocksize / 512;
		block = udf_get_pblock(inode->i_sb, block,
			UDF_I_LOCATION(inode).partitionReferenceNum, 0);
	}
	else
		block = udf_get_lb_pblock(inode->i_sb, UDF_I_LOCATION(inode), 0);

	bh = udf_tread(inode->i_sb, block, inode->i_sb->s_blocksize);
	ea = bh->b_data + udf_ext0_offset(inode);

	eoffset = inode->i_sb->s_blocksize - udf_ext0_offset(inode);
	pc = (struct PathComponent *)ea;

	if (*symname == '/')
	{
		do
		{
			symname++;
		} while (*symname == '/');

		pc->componentType = 1;
		pc->lengthComponentIdent = 0;
		pc->componentFileVersionNum = 0;
		pc += sizeof(struct PathComponent);
		elen += sizeof(struct PathComponent);
	}

	err = -ENAMETOOLONG;

	while (*symname)
	{
		if (elen + sizeof(struct PathComponent) > eoffset)
			goto out_no_entry;

		pc = (struct PathComponent *)(ea + elen);

		compstart = (char *)symname;

		do
		{
			symname++;
		} while (*symname && *symname != '/');

		pc->componentType = 5;
		pc->lengthComponentIdent = 0;
		pc->componentFileVersionNum = 0;
		if (pc->componentIdent[0] == '.')
		{
			if (pc->lengthComponentIdent == 1)
				pc->componentType = 4;
			else if (pc->lengthComponentIdent == 2 && pc->componentIdent[1] == '.')
				pc->componentType = 3;
		}

		if (pc->componentType == 5)
		{
			if (elen + sizeof(struct PathComponent) + symname - compstart > eoffset)
				goto out_no_entry;
			else
				pc->lengthComponentIdent = symname - compstart;

			memcpy(pc->componentIdent, compstart, pc->lengthComponentIdent);
		}

		elen += sizeof(struct PathComponent) + pc->lengthComponentIdent;

		if (*symname)
		{
			do
			{
				symname++;
			} while (*symname == '/');
		}
	}

	udf_release_data(bh);
	inode->i_size = elen;
	if (UDF_I_ALLOCTYPE(inode) == ICB_FLAG_AD_IN_ICB)
		UDF_I_LENALLOC(inode) = inode->i_size;
	mark_inode_dirty(inode);

	if (!(fi = udf_add_entry(dir, dentry, &fibh, &cfi, &err)))
		goto out_no_entry;
	cfi.icb.extLength = cpu_to_le32(inode->i_sb->s_blocksize);
	cfi.icb.extLocation = cpu_to_lelb(UDF_I_LOCATION(inode));
	if (UDF_SB_LVIDBH(inode->i_sb))
	{
		struct LogicalVolHeaderDesc *lvhd;
		Uint64 uniqueID;
		lvhd = (struct LogicalVolHeaderDesc *)(UDF_SB_LVID(inode->i_sb)->logicalVolContentsUse);
		uniqueID = le64_to_cpu(lvhd->uniqueID);
		*(Uint32 *)((struct ADImpUse *)cfi.icb.impUse)->impUse =
			cpu_to_le32(uniqueID & 0x00000000FFFFFFFFUL);
		if (!(++uniqueID & 0x00000000FFFFFFFFUL))
			uniqueID += 16;
		lvhd->uniqueID = cpu_to_le64(uniqueID);
		mark_buffer_dirty(UDF_SB_LVIDBH(inode->i_sb));
	}
	udf_write_fi(&cfi, fi, &fibh, NULL, NULL);
	if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_IN_ICB)
	{
		mark_inode_dirty(dir);
		dir->i_version = ++event;
	}
	if (fibh.sbh != fibh.ebh)
		udf_release_data(fibh.ebh);
	udf_release_data(fibh.sbh);
	d_instantiate(dentry, inode);
	err = 0;

out:
	return err;

out_no_entry:
	inode->i_nlink--;
	mark_inode_dirty(inode);
	iput(inode);
	goto out;
}

static int udf_link(struct dentry * old_dentry, struct inode * dir,
	 struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;
	struct udf_fileident_bh fibh;
	int err;
	struct FileIdentDesc cfi, *fi;

	if (S_ISDIR(inode->i_mode))
		return -EPERM;

	if (inode->i_nlink >= (256<<sizeof(inode->i_nlink))-1)
		return -EMLINK;

	if (!(fi = udf_add_entry(dir, dentry, &fibh, &cfi, &err)))
		return err;
	cfi.icb.extLength = cpu_to_le32(inode->i_sb->s_blocksize);
	cfi.icb.extLocation = cpu_to_lelb(UDF_I_LOCATION(inode));
	if (UDF_SB_LVIDBH(inode->i_sb))
	{
		struct LogicalVolHeaderDesc *lvhd;
		Uint64 uniqueID;
		lvhd = (struct LogicalVolHeaderDesc *)(UDF_SB_LVID(inode->i_sb)->logicalVolContentsUse);
		uniqueID = le64_to_cpu(lvhd->uniqueID);
		*(Uint32 *)((struct ADImpUse *)cfi.icb.impUse)->impUse =
			cpu_to_le32(uniqueID & 0x00000000FFFFFFFFUL);
		if (!(++uniqueID & 0x00000000FFFFFFFFUL))
			uniqueID += 16;
		lvhd->uniqueID = cpu_to_le64(uniqueID);
		mark_buffer_dirty(UDF_SB_LVIDBH(inode->i_sb));
	}
	udf_write_fi(&cfi, fi, &fibh, NULL, NULL);
	if (UDF_I_ALLOCTYPE(dir) == ICB_FLAG_AD_IN_ICB)
	{
		mark_inode_dirty(dir);
		dir->i_version = ++event;
	}
	if (fibh.sbh != fibh.ebh)
		udf_release_data(fibh.ebh);
	udf_release_data(fibh.sbh);
	inode->i_nlink ++;
	inode->i_ctime = CURRENT_TIME;
	UDF_I_UCTIME(inode) = CURRENT_UTIME;
	mark_inode_dirty(inode);
	atomic_inc(&inode->i_count);
	d_instantiate(dentry, inode);
	return 0;
}

/* Anybody can rename anything with this: the permission checks are left to the
 * higher-level routines.
 */
static int udf_rename (struct inode * old_dir, struct dentry * old_dentry,
	struct inode * new_dir, struct dentry * new_dentry)
{
	struct inode * old_inode, * new_inode;
	struct udf_fileident_bh ofibh, nfibh;
	struct FileIdentDesc *ofi = NULL, *nfi = NULL, *dir_fi = NULL, ocfi, ncfi;
	struct buffer_head *dir_bh = NULL;
	int retval = -ENOENT;

	old_inode = old_dentry->d_inode;
	ofi = udf_find_entry(old_dir, old_dentry, &ofibh, &ocfi);
	if (!ofi || udf_get_lb_pblock(old_dir->i_sb, lelb_to_cpu(ocfi.icb.extLocation), 0) !=
		old_inode->i_ino)
	{
		goto end_rename;
	}

	new_inode = new_dentry->d_inode;
	nfi = udf_find_entry(new_dir, new_dentry, &nfibh, &ncfi);
	if (nfi)
	{
		if (!new_inode)
		{
			if (nfibh.sbh != nfibh.ebh)
				udf_release_data(nfibh.ebh);
			udf_release_data(nfibh.sbh);
			nfi = NULL;
		}
		else
		{
			DQUOT_INIT(new_inode);
		}
	}
	if (S_ISDIR(old_inode->i_mode))
	{
		Uint32 offset = udf_ext0_offset(old_inode);

		if (new_inode)
		{
			retval = -ENOTEMPTY;
			if (!empty_dir(new_inode))
				goto end_rename;
		}
		retval = -EIO;
		dir_bh = udf_bread(old_inode, 0, 0, &retval);
		if (!dir_bh)
			goto end_rename;
		dir_fi = udf_get_fileident(dir_bh->b_data, old_inode->i_sb->s_blocksize, &offset);
		if (!dir_fi)
			goto end_rename;
		if (udf_get_lb_pblock(old_inode->i_sb, cpu_to_lelb(dir_fi->icb.extLocation), 0) !=
			old_dir->i_ino)
		{
			goto end_rename;
		}
		retval = -EMLINK;
		if (!new_inode && new_dir->i_nlink >= (256<<sizeof(new_dir->i_nlink))-1)
			goto end_rename;
	}
	if (!nfi)
	{
		nfi = udf_add_entry(new_dir, new_dentry, &nfibh, &ncfi, &retval);
		if (!nfi)
			goto end_rename;
	}
	new_dir->i_version = ++event;

	/*
	 * Like most other Unix systems, set the ctime for inodes on a
	 * rename.
	 */
	old_inode->i_ctime = CURRENT_TIME;
	UDF_I_UCTIME(old_inode) = CURRENT_UTIME;
	mark_inode_dirty(old_inode);

	/*
	 * ok, that's it
	 */
	ncfi.fileVersionNum = ocfi.fileVersionNum;
	ncfi.fileCharacteristics = ocfi.fileCharacteristics;
	memcpy(&(ncfi.icb), &(ocfi.icb), sizeof(long_ad));
	udf_write_fi(&ncfi, nfi, &nfibh, NULL, NULL);

	udf_delete_entry(ofi, &ofibh, &ocfi);

	old_dir->i_version = ++event;
	if (new_inode)
	{
		new_inode->i_nlink--;
		new_inode->i_ctime = CURRENT_TIME;
		UDF_I_UCTIME(new_inode) = CURRENT_UTIME;
		mark_inode_dirty(new_inode);
	}
	old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME;
	UDF_I_UCTIME(old_dir) = UDF_I_UMTIME(old_dir) = CURRENT_UTIME;
	mark_inode_dirty(old_dir);

	if (dir_bh)
	{
		dir_fi->icb.extLocation = lelb_to_cpu(UDF_I_LOCATION(new_dir));
		udf_update_tag((char *)dir_fi, sizeof(struct FileIdentDesc) +
			cpu_to_le16(dir_fi->lengthOfImpUse));
		if (UDF_I_ALLOCTYPE(old_inode) == ICB_FLAG_AD_IN_ICB)
		{
			mark_inode_dirty(old_inode);
			old_inode->i_version = ++event;
		}
		else
			mark_buffer_dirty(dir_bh);
		old_dir->i_nlink --;
		mark_inode_dirty(old_dir);
		if (new_inode)
		{
			new_inode->i_nlink --;
			mark_inode_dirty(new_inode);
		}
		else
		{
			new_dir->i_nlink ++;
			mark_inode_dirty(new_dir);
		}
	}

	retval = 0;

end_rename:
	udf_release_data(dir_bh);
	if (ofi)
	{
		if (ofibh.sbh != ofibh.ebh)
			udf_release_data(ofibh.ebh);
		udf_release_data(ofibh.sbh);
	}
	if (nfi)
	{
		if (nfibh.sbh != nfibh.ebh)
			udf_release_data(nfibh.ebh);
		udf_release_data(nfibh.sbh);
	}
	return retval;
}

struct inode_operations udf_dir_inode_operations = {
	lookup:				udf_lookup,
	create:				udf_create,
	link:				udf_link,
	unlink:				udf_unlink,
	symlink:			udf_symlink,
	mkdir:				udf_mkdir,
	rmdir:				udf_rmdir,
	mknod:				udf_mknod,
	rename:				udf_rename,
};

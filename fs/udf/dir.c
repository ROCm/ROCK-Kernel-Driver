/*
 * dir.c
 *
 * PURPOSE
 *  Directory handling routines for the OSTA-UDF(tm) filesystem.
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
 *  (C) 1998-2000 Ben Fennema
 *
 * HISTORY
 *
 *  10/05/98 dgb  Split directory operations into it's own file
 *                Implemented directory reads via do_udf_readdir
 *  10/06/98      Made directory operations work!
 *  11/17/98      Rewrote directory to support ICB_FLAG_AD_LONG
 *  11/25/98 blf  Rewrote directory handling (readdir+lookup) to support reading
 *                across blocks.
 *  12/12/98      Split out the lookup code to namei.c. bulk of directory
 *                code now in directory.c:udf_fileident_read.
 */

#include "udfdecl.h"

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/version.h>
#include "udf_i.h"
#include "udf_sb.h"
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/udf_fs.h>
#endif

/* Prototypes for file operations */
static int udf_readdir(struct file *, void *, filldir_t);
static int do_udf_readdir(struct inode *, struct file *, filldir_t, void *);

/* readdir and lookup functions */

struct file_operations udf_dir_operations = {
	read:				generic_read_dir,
	readdir:			udf_readdir,
	ioctl:				udf_ioctl,
	fsync:				udf_sync_file,
};

/*
 * udf_readdir
 *
 * PURPOSE
 *	Read a directory entry.
 *
 * DESCRIPTION
 *	Optional - sys_getdents() will return -ENOTDIR if this routine is not
 *	available.
 *
 *	Refer to sys_getdents() in fs/readdir.c
 *	sys_getdents() -> .
 *
 * PRE-CONDITIONS
 *	filp			Pointer to directory file.
 *	buf			Pointer to directory entry buffer.
 *	filldir			Pointer to filldir function.
 *
 * POST-CONDITIONS
 *	<return>		>=0 on success.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */

int udf_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct inode *dir = filp->f_dentry->d_inode;
	int result;

	if ( filp->f_pos == 0 ) 
	{
		if (filldir(dirent, ".", 1, filp->f_pos, dir->i_ino, DT_DIR))
			return 0;
	}
 
	result = do_udf_readdir(dir, filp, filldir, dirent);
	UPDATE_ATIME(dir);
 	return result;
}

static int 
do_udf_readdir(struct inode * dir, struct file *filp, filldir_t filldir, void *dirent)
{
	struct udf_fileident_bh fibh;
	struct FileIdentDesc *fi=NULL;
	struct FileIdentDesc cfi;
	int block, iblock;
	loff_t nf_pos = filp->f_pos;
	int flen;
	char fname[255];
	char *nameptr;
	Uint16 liu;
	Uint8 lfi;
	loff_t size = (udf_ext0_offset(dir) + dir->i_size) >> 2;
	struct buffer_head * bh = NULL;
	lb_addr bloc, eloc;
	Uint32 extoffset, elen, offset;

	if (nf_pos >= size)
		return 1;

	if (nf_pos == 0)
		nf_pos = (udf_ext0_offset(dir) >> 2);

	fibh.soffset = fibh.eoffset = (nf_pos & ((dir->i_sb->s_blocksize - 1) >> 2)) << 2;
	if (inode_bmap(dir, nf_pos >> (dir->i_sb->s_blocksize_bits - 2),
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
	{
		udf_release_data(bh);
		return 0;
	}

	while ( nf_pos < size )
	{
		filp->f_pos = nf_pos;

		fi = udf_fileident_read(dir, &nf_pos, &fibh, &cfi, &bloc, &extoffset, &eloc, &elen, &offset, &bh);

		if (!fi)
		{
			if (fibh.sbh != fibh.ebh)
				udf_release_data(fibh.ebh);
			udf_release_data(fibh.sbh);
			udf_release_data(bh);
			return 1;
		}

		liu = le16_to_cpu(cfi.lengthOfImpUse);
		lfi = cfi.lengthFileIdent;

		if (fibh.sbh == fibh.ebh)
			nameptr = fi->fileIdent + liu;
		else
		{
			int poffset;	/* Unpaded ending offset */

			poffset = fibh.soffset + sizeof(struct FileIdentDesc) + liu + lfi;

			if (poffset >= lfi)
				nameptr = (char *)(fibh.ebh->b_data + poffset - lfi);
			else
			{
				nameptr = fname;
				memcpy(nameptr, fi->fileIdent + liu, lfi - poffset);
				memcpy(nameptr + lfi - poffset, fibh.ebh->b_data, poffset);
			}
		}

		if ( (cfi.fileCharacteristics & FILE_DELETED) != 0 )
		{
			if ( !UDF_QUERY_FLAG(dir->i_sb, UDF_FLAG_UNDELETE) )
				continue;
		}
		
		if ( (cfi.fileCharacteristics & FILE_HIDDEN) != 0 )
		{
			if ( !UDF_QUERY_FLAG(dir->i_sb, UDF_FLAG_UNHIDE) )
				continue;
		}

		iblock = udf_get_lb_pblock(dir->i_sb, lelb_to_cpu(cfi.icb.extLocation), 0);
 
 		if (!lfi) /* parent directory */
 		{
			if (filldir(dirent, "..", 2, filp->f_pos, filp->f_dentry->d_parent->d_inode->i_ino, DT_DIR))
			{
				if (fibh.sbh != fibh.ebh)
					udf_release_data(fibh.ebh);
				udf_release_data(fibh.sbh);
				udf_release_data(bh);
 				return 1;
			}
		}
		else
		{
			if ((flen = udf_get_filename(nameptr, fname, lfi)))
			{
				if (filldir(dirent, fname, flen, filp->f_pos, iblock, DT_UNKNOWN))
				{
					if (fibh.sbh != fibh.ebh)
						udf_release_data(fibh.ebh);
					udf_release_data(fibh.sbh);
					udf_release_data(bh);
		 			return 1; /* halt enum */
				}
			}
		}
	} /* end while */

	filp->f_pos = nf_pos;

	if (fibh.sbh != fibh.ebh)
		udf_release_data(fibh.ebh);
	udf_release_data(fibh.sbh);
	udf_release_data(bh);

	if ( filp->f_pos >= size)
		return 1;
	else
		return 0;
}

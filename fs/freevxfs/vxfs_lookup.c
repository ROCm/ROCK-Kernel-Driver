/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ident "$Id: vxfs_lookup.c,v 1.11 2001/04/24 19:28:36 hch Exp hch $"

/*
 * Veritas filesystem driver - lookup and other directory related code.
 */
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>

#include "vxfs.h"
#include "vxfs_dir.h"
#include "vxfs_inode.h"

/*
 * Number of VxFS blocks per page.
 */
#define VXFS_BLOCK_PER_PAGE(sbp)  ((PAGE_CACHE_SIZE / (sbp)->s_blocksize))


static struct dentry *	vxfs_lookup(struct inode *, struct dentry *);
static int		vxfs_readdir(struct file *, void *, filldir_t);

struct inode_operations vxfs_dir_inode_ops = {
	.lookup =		vxfs_lookup,
};

struct file_operations vxfs_dir_operations = {
	.readdir =		vxfs_readdir,
};


static __inline__ void
vxfs_put_page(struct page *page)
{
	kunmap(page);
	page_cache_release(page);
}
 
static __inline__ u_long
dir_pages(struct inode *inode)
{
	return (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
}
 
static __inline__ u_long
dir_blocks(struct inode *ip)
{
	u_long			bsize = ip->i_sb->s_blocksize;
	return (ip->i_size + bsize - 1) & ~(bsize - 1);
}

/**
 * vxfs_get_page - read a page into memory.
 * @ip:		inode to read from
 * @n:		page number
 *
 * Description:
 *   vxfs_get_page reads the @n th page of @ip into the pagecache.
 *
 * Returns:
 *   The wanted page on success, else a NULL pointer.
 */
static struct page *
vxfs_get_page(struct inode *dir, u_long n)
{
	struct address_space *		mapping = dir->i_mapping;
	struct page *			page;

	page = read_cache_page(mapping, n,
			(filler_t*)mapping->a_ops->readpage, NULL);

	if (!IS_ERR(page)) {
		wait_on_page(page);
		kmap(page);
		if (!Page_Uptodate(page))
			goto fail;
		/** if (!PageChecked(page)) **/
			/** vxfs_check_page(page); **/
		if (PageError(page))
			goto fail;
	}
	
	return (page);
		 
fail:
	vxfs_put_page(page);
	return ERR_PTR(-EIO);
}

/*
 * NOTE! unlike strncmp, vxfs_match returns 1 for success, 0 for failure.
 *
 * len <= VXFS_NAME_LEN and de != NULL are guaranteed by caller.
 */
static __inline__ int
vxfs_match(int len, const char * const name, struct vxfs_direct *de)
{
	if (len != de->d_namelen)
		return 0;
	if (!de->d_ino)
		return 0;
	return !memcmp(name, de->d_name, len);
}

static __inline__ struct vxfs_direct *
vxfs_next_entry(struct vxfs_direct *de)
{
	return ((struct vxfs_direct *)((char*)de + de->d_reclen));
}

/**
 * vxfs_find_entry - find a mathing directory entry for a dentry
 * @ip:		directory inode
 * @dp:		dentry for which we want to find a direct
 * @ppp:	gets filled with the page the return value sits in
 *
 * Description:
 *   vxfs_find_entry finds a &struct vxfs_direct for the VFS directory
 *   cache entry @dp.  @ppp will be filled with the page the return
 *   value resides in.
 *
 * Returns:
 *   The wanted direct on success, else a NULL pointer.
 */
static struct vxfs_direct *
vxfs_find_entry(struct inode *ip, struct dentry *dp, struct page **ppp)
{
	const char			*name = dp->d_name.name;
	u_long				npages = dir_pages(ip), n, i;
	u_long				bsize = ip->i_sb->s_blocksize;
	int				namelen = dp->d_name.len;
	loff_t				pos = ip->i_size - 2;
	struct vxfs_direct		*de;
	struct page			*pp;
	
	
	for (n = 0; n < npages; n++) {
		char			*kaddr;
		u_long			max;

		pp = vxfs_get_page(ip, n);
		if (IS_ERR(pp))
			continue;

		kaddr = (char *)page_address(pp);
		max = (pos + bsize - 1) & ~(bsize - 1);

		for (i = 0; i <= (PAGE_CACHE_SIZE / bsize) && i <= max; i++) {
			struct vxfs_dirblk	*blp;
			char			*lim;

			blp = (struct vxfs_dirblk *)(kaddr + (i * bsize));
			de = (struct vxfs_direct *)((char *)blp + (2 * blp->d_nhash) + 4);
			
			/*
			 * The magic number 48 stands for the reclen offset
			 * in the direct plus the size of reclen.
			 */
			lim = (char *)blp + bsize - 48;

			do {
				if ((char *)de > lim)
					break;
				if ((char *)de + de->d_reclen > lim)
					break;
				if (!de->d_reclen)
					break;
				if (vxfs_match(namelen, name, de))
					goto found;
			} while ((de = vxfs_next_entry(de)) != NULL);
		}
		vxfs_put_page(pp);
	}

	return NULL;

found:
	*ppp = pp;
	return (de);
}

/**
 * vxfs_inode_by_name - find inode number for dentry
 * @dip:	directory to search in
 * @dp:		dentry we seach for
 *
 * Description:
 *   vxfs_inode_by_name finds out the inode number of
 *   the path component described by @dp in @dip.
 *
 * Returns:
 *   The wanted inode number on success, else Zero.
 */
static ino_t
vxfs_inode_by_name(struct inode *dip, struct dentry *dp)
{
	struct vxfs_direct		*de;
	struct page			*pp;
	ino_t				ino = 0;

	de = vxfs_find_entry(dip, dp, &pp);
	if (de) {
		ino = de->d_ino;
		kunmap(pp);
		page_cache_release(pp);
	}
	
	return (ino);
}

/**
 * vxfs_lookup - lookup pathname component
 * @dip:	dir in which we lookup
 * @dp:		dentry we lookup
 *
 * Description:
 *   vxfs_lookup tries to lookup the pathname component described
 *   by @dp in @dip.
 *
 * Returns:
 *   A NULL-pointer on success, else an negative error code encoded
 *   in the return pointer.
 */
static struct dentry *
vxfs_lookup(struct inode *dip, struct dentry *dp)
{
	struct inode		*ip = NULL;
	ino_t			ino;
			 
	if (dp->d_name.len > VXFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);
				 
	ino = vxfs_inode_by_name(dip, dp);
	if (ino == 0)
		return NULL;

	ip = iget(dip->i_sb, ino);
	if (!ip)
		return ERR_PTR(-EACCES);
	d_add(dp, ip);
	return NULL;
}

/**
 * vxfs_readdir - read a directory
 * @fp:		the directory to read
 * @retp:	return buffer
 * @filler:	filldir callback
 *
 * Description:
 *   vxfs_readdir fills @retp with directory entries from @fp
 *   using the VFS supplied callback @filler.
 *
 * Returns:
 *   Zero.
 */
static int
vxfs_readdir(struct file *fp, void *retp, filldir_t filler)
{
	struct inode		*ip = fp->f_dentry->d_inode;
	struct super_block	*sbp = ip->i_sb;
	u_long			bsize = sbp->s_blocksize;
	u_long			page, npages, block, nblocks, offset;
	loff_t			pos;
	int			pblocks;

	switch ((long)fp->f_pos) {
	case 0:
		if (filler(retp, ".", 1, fp->f_pos, ip->i_ino, DT_DIR) < 0)
			goto out;
		fp->f_pos++;
		/* fallthrough */
	case 1:
		if (filler(retp, "..", 2, fp->f_pos, VXFS_INO(ip)->vii_dotdot, DT_DIR) < 0)
			goto out;
		fp->f_pos++;
		/* fallthrough */
	}

	if (fp->f_pos >= ip->i_size)
		goto out;
	
	pos = fp->f_pos - 2;
	offset = pos & ~PAGE_CACHE_MASK;
	page = pos >> PAGE_CACHE_SHIFT;
	block = pos & (sbp->s_blocksize - 1);

	npages = dir_pages(ip);
	nblocks = dir_blocks(ip);
	pblocks = VXFS_BLOCK_PER_PAGE(sbp);

	for (; page < npages; page++, offset = 0) {
		char			*kaddr, *lim;
		struct vxfs_dirblk	*dblkp;
		struct vxfs_direct	*de;
		struct page		*pp;

		pp = vxfs_get_page(ip, page);
		if (IS_ERR(pp))
			continue;

		kaddr = (char *)page_address(pp);

		for (; block <= nblocks && block <= pblocks; block++) {
			dblkp = (struct vxfs_dirblk *)
				(kaddr + (block * bsize));
			de = (struct vxfs_direct *)
				((char *)dblkp + (2 * dblkp->d_nhash) + 4);
			
			/*
			 * The magic number 48 stands for the reclen offset
			 * in the direct plus the size of reclen.
			 */
			lim = (char *)dblkp + bsize - 48;

			do {
				int	over;

				if ((char *)de > lim)
					break;
				if ((char *)de + de->d_reclen > lim)
					break;
				if (!de->d_reclen)
					break;
				if (!de->d_ino)
					continue;

				offset = (char *)de - kaddr;

				over = filler(retp, de->d_name, de->d_namelen,
						(page << PAGE_CACHE_SHIFT) | offset,
						de->d_ino, DT_UNKNOWN);
				if (over) {
					vxfs_put_page(pp);
					goto done;
				}
			} while ((de = vxfs_next_entry(de)) != NULL);
		}

		vxfs_put_page(pp);
		block = 0;
	}

done:
	fp->f_pos = ((page << PAGE_CACHE_SHIFT) | offset) + 2;
out:
	fp->f_version = ip->i_version;
	return 0;
}

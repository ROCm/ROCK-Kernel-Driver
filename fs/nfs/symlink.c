/*
 *  linux/fs/nfs/symlink.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  Optimization changes Copyright (C) 1994 Florian La Roche
 *
 *  Jun 7 1999, cache symlink lookups in the page cache.  -DaveM
 *
 *  nfs symlink handling code
 */

#define NFS_NEED_XDR_TYPES
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs2.h>
#include <linux/nfs_fs.h>
#include <linux/pagemap.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/namei.h>

/* Symlink caching in the page cache is even more simplistic
 * and straight-forward than readdir caching.
 *
 * We place the length at the beginning of the page, in host byte order,
 * followed by the string.  The XDR response verification will NUL-terminate
 * it.  In the very end of page we store pointer to struct page in question,
 * simplifying nfs_put_link() (if inode got invalidated we can't find the page
 * to be freed via pagecache lookup).
 */

struct nfs_symlink {
	u32 length;
	char body[PAGE_SIZE - sizeof(u32) - sizeof(struct page *)];
	struct page *page;
} __attribute__((packed));	/* this must be page-sized */

static int nfs_symlink_filler(struct inode *inode, struct page *page)
{
	int error;

	lock_kernel();
	error = NFS_PROTO(inode)->readlink(inode, page);
	unlock_kernel();
	if (error < 0)
		goto error;
	SetPageUptodate(page);
	unlock_page(page);
	return 0;

error:
	SetPageError(page);
	unlock_page(page);
	return -EIO;
}

static int nfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	struct page *page;
	struct nfs_symlink *p;
	void *err = ERR_PTR(nfs_revalidate_inode(NFS_SERVER(inode), inode));
	if (err)
		goto read_failed;
	page = read_cache_page(&inode->i_data, 0,
				(filler_t *)nfs_symlink_filler, inode);
	if (IS_ERR(page)) {
		err = page;
		goto read_failed;
	}
	if (!PageUptodate(page)) {
		err = ERR_PTR(-EIO);
		goto getlink_read_error;
	}
	p = kmap(page);
	if (p->length > sizeof(p->body) - 1)
		goto too_long;
	p->page = page;
	nd_set_link(nd, p->body);
	return 0;

too_long:
	err = ERR_PTR(-ENAMETOOLONG);
	kunmap(page);
getlink_read_error:
	page_cache_release(page);
read_failed:
	nd_set_link(nd, err);
	return 0;
}

static void nfs_put_link(struct dentry *dentry, struct nameidata *nd)
{
	char *s = nd_get_link(nd);
	if (!IS_ERR(s)) {
		struct nfs_symlink *p;
		struct page *page;

		p = container_of(s, struct nfs_symlink, body[0]);
		page = p->page;

		kunmap(page);
		page_cache_release(page);
	}
}

/*
 * symlinks can't do much...
 */
struct inode_operations nfs_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= nfs_follow_link,
	.put_link	= nfs_put_link,
	.getattr	= nfs_getattr,
	.setattr	= nfs_setattr,
};

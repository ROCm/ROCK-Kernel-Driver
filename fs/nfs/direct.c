/*
 * linux/fs/nfs/direct.c
 *
 * Copyright (C) 2001 by Chuck Lever <cel@netapp.com>
 *
 * High-performance uncached I/O for the Linux NFS client
 *
 * There are important applications whose performance or correctness
 * depends on uncached access to file data.  Database clusters
 * (multiple copies of the same instance running on separate hosts) 
 * implement their own cache coherency protocol that subsumes file
 * system cache protocols.  Applications that process datasets 
 * considerably larger than the client's memory do not always benefit 
 * from a local cache.  A streaming video server, for instance, has no 
 * need to cache the contents of a file.
 *
 * When an application requests uncached I/O, all read and write requests
 * are made directly to the server; data stored or fetched via these
 * requests is not cached in the Linux page cache.  The client does not
 * correct unaligned requests from applications.  All requested bytes are
 * held on permanent storage before a direct write system call returns to
 * an application.
 *
 * Solaris implements an uncached I/O facility called directio() that
 * is used for backups and sequential I/O to very large files.  Solaris
 * also supports uncaching whole NFS partitions with "-o forcedirectio,"
 * an undocumented mount option.
 *
 * Designed by Jeff Kimmel, Chuck Lever, and Trond Myklebust.
 *
 * 18 Dec 2001	Initial implementation for 2.4  --cel
 * 08 Jul 2002	Version for 2.4.19, with bug fixes --trondmy
 * 24 Sep 2002	Rewrite to use asynchronous RPCs, port to 2.5  --cel
 *
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/errno.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/sunrpc/clnt.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#define NFSDBG_FACILITY		(NFSDBG_PAGECACHE | NFSDBG_VFS)
#define VERF_SIZE		(2 * sizeof(__u32))


/**
 * nfs_get_user_pages - find and set up page representing user buffer
 * addr: user-space address of target buffer
 * size: total size in bytes of target buffer
 * @pages: returned array of page struct pointers underlying target buffer
 * write: whether or not buffer is target of a write operation
 */
static inline int
nfs_get_user_pages(unsigned long addr, size_t size,
		struct page ***pages, int rw)
{
	int result = -ENOMEM;
	unsigned page_count = (unsigned) size >> PAGE_SHIFT;
	unsigned array_size = (page_count * sizeof(struct page *)) + 2U;

	*pages = (struct page **) kmalloc(array_size, GFP_KERNEL);
	if (*pages) {
		down_read(&current->mm->mmap_sem);
		result = get_user_pages(current, current->mm, addr,
					page_count, (rw == WRITE), 0,
					*pages, NULL);
		up_read(&current->mm->mmap_sem);
		if (result < 0)
			printk(KERN_ERR "%s: get_user_pages result %d\n",
					__FUNCTION__, result);
	}
	return result;
}

/**
 * nfs_free_user_pages - tear down page struct array
 * @pages: array of page struct pointers underlying target buffer
 */
static inline void
nfs_free_user_pages(struct page **pages, unsigned count)
{
	unsigned page = 0;

	while (count--)
		page_cache_release(pages[page++]);

	kfree(pages);
}

/**
 * nfs_iov2pagelist - convert an array of iovecs to a list of page requests
 * @inode: inode of target file
 * @cred: credentials of user who requested I/O
 * @iov: array of vectors that define I/O buffer
 * offset: where in file to begin the read
 * nr_segs: size of iovec array
 * @requests: append new page requests to this list head
 */
static int
nfs_iov2pagelist(int rw, const struct inode *inode,
		const struct rpc_cred *cred,
		const struct iovec *iov, loff_t offset,
		unsigned long nr_segs, struct list_head *requests)
{
	unsigned seg;
	int tot_bytes = 0;
	struct page **pages;

	/* for each iovec in the array... */
	for (seg = 0; seg < nr_segs; seg++) {
		const unsigned long user_addr =
					(unsigned long) iov[seg].iov_base;
		size_t bytes = iov[seg].iov_len;
		unsigned int pg_offset = (user_addr & ~PAGE_MASK);
		int page_count, page = 0;

		page_count = nfs_get_user_pages(user_addr, bytes, &pages, rw);
		if (page_count < 0) {
			nfs_release_list(requests);
			return page_count;
		}

		/* ...build as many page requests as required */
		while (bytes > 0) {
			struct nfs_page *new;
			const unsigned int pg_bytes = (bytes > PAGE_SIZE) ?
							PAGE_SIZE : bytes;

			new = nfs_create_request((struct rpc_cred *) cred,
						 (struct inode *) inode,
						 pages[page],
						 pg_offset, pg_bytes);
			if (IS_ERR(new)) {
				nfs_free_user_pages(pages, page_count);
				nfs_release_list(requests);
				return PTR_ERR(new);
			}
			new->wb_index = offset;
			nfs_list_add_request(new, requests);

			/* after the first page */
			pg_offset = 0;
			offset += PAGE_SIZE;
			tot_bytes += pg_bytes;
			bytes -= pg_bytes;
			page++;
		}

		/* don't release pages here -- I/O completion will do that */
		nfs_free_user_pages(pages, 0);
	}

	return tot_bytes;
}

/**
 * do_nfs_direct_IO - Read or write data without caching
 * @inode: inode of target file
 * @cred: credentials of user who requested I/O
 * @iov: array of vectors that define I/O buffer
 * offset: where in file to begin the read
 * nr_segs: size of iovec array
 *
 * Break the passed-in iovec into a series of page-sized or smaller
 * requests, where each page is mapped for direct user-land I/O.
 *
 * For each of these pages, create an NFS page request and
 * append it to an automatic list of page requests.
 *
 * When all page requests have been queued, start the I/O on the
 * whole list.  The underlying routines coalesce the pages on the
 * list into a bunch of asynchronous "r/wsize" network requests.
 *
 * I/O completion automatically unmaps and releases the pages.
 */
static int
do_nfs_direct_IO(int rw, const struct inode *inode,
		const struct rpc_cred *cred, const struct iovec *iov,
		loff_t offset, unsigned long nr_segs)
{
	LIST_HEAD(requests);
	int result, tot_bytes;

	result = nfs_iov2pagelist(rw, inode, cred, iov, offset, nr_segs,
								&requests);
	if (result < 0)
		return result;
	tot_bytes = result;

	switch (rw) {
	case READ:
		if (IS_SYNC(inode) || (NFS_SERVER(inode)->rsize < PAGE_SIZE)) {
			result = nfs_direct_read_sync(inode, cred, iov, offset, nr_segs);
			break;
		}
		result = nfs_pagein_list(&requests, NFS_SERVER(inode)->rpages);
		nfs_wait_for_reads(&requests);
		break;
	case WRITE:
		if (IS_SYNC(inode) || (NFS_SERVER(inode)->wsize < PAGE_SIZE))
			result = nfs_direct_write_sync(inode, cred, iov, offset, nr_segs);
		else
			result = nfs_flush_list(&requests,
					NFS_SERVER(inode)->wpages, FLUSH_WAIT);

		/* invalidate cache so non-direct readers pick up changes */
		invalidate_inode_pages((struct inode *) inode);
		break;
	default:
		result = -EINVAL;
		break;
	}

	if (result < 0)
		return result;
	return tot_bytes;
}

/**
 * nfs_direct_IO - NFS address space operation for direct I/O
 * rw: direction (read or write)
 * @file: file struct of target file
 * @iov: array of vectors that define I/O buffer
 * offset: offset in file to begin the operation
 * nr_segs: size of iovec array
 *
 * The inode's i_sem is no longer held by the VFS layer before it calls
 * this function to do a write.
 */
int
nfs_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov,
		loff_t offset, unsigned long nr_segs)
{
	/* None of this works yet, so prevent it from compiling. */
#if 0
	int result;
	struct dentry *dentry = file->f_dentry;
	const struct inode *inode = dentry->d_inode->i_mapping->host;
	const struct rpc_cred *cred = nfs_file_cred(file);
#endif

	dfprintk(VFS, "NFS: direct_IO(%s) (%s/%s) off/no(%Lu/%lu)\n",
				((rw == READ) ? "READ" : "WRITE"),
				dentry->d_parent->d_name.name,
				dentry->d_name.name, offset, nr_segs);

	result = do_nfs_direct_IO(rw, inode, cred, iov, offset, nr_segs);

	dfprintk(VFS, "NFS: direct_IO result = %d\n", result);

	return result;
}

/*
 * linux/fs/nfs/direct.c
 *
 * Copyright (C) 2003 by Chuck Lever <cel@netapp.com>
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
 * Designed by Jeff Kimmel, Chuck Lever, and Trond Myklebust, with
 * help from Andrew Morton.
 *
 * 18 Dec 2001	Initial implementation for 2.4  --cel
 * 08 Jul 2002	Version for 2.4.19, with bug fixes --trondmy
 * 08 Jun 2003	Port to 2.5 APIs  --cel
 * 31 Mar 2004	Handle direct I/O without VFS support  --cel
 *
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/smp_lock.h>
#include <linux/file.h>
#include <linux/pagemap.h>

#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/sunrpc/clnt.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#define NFSDBG_FACILITY		NFSDBG_VFS
#define VERF_SIZE		(2 * sizeof(__u32))
#define MAX_DIRECTIO_SIZE	(4096UL << PAGE_SHIFT)


/**
 * nfs_get_user_pages - find and set up pages underlying user's buffer
 * rw: direction (read or write)
 * user_addr: starting address of this segment of user's buffer
 * count: size of this segment
 * @pages: returned array of page struct pointers underlying user's buffer
 */
static inline int
nfs_get_user_pages(int rw, unsigned long user_addr, size_t size,
		struct page ***pages)
{
	int result = -ENOMEM;
	unsigned long page_count;
	size_t array_size;

	/* set an arbitrary limit to prevent arithmetic overflow */
	if (size > MAX_DIRECTIO_SIZE)
		return -EFBIG;

	page_count = (user_addr + size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	page_count -= user_addr >> PAGE_SHIFT;

	array_size = (page_count * sizeof(struct page *));
	*pages = kmalloc(array_size, GFP_KERNEL);
	if (*pages) {
		down_read(&current->mm->mmap_sem);
		result = get_user_pages(current, current->mm, user_addr,
					page_count, (rw == READ), 0,
					*pages, NULL);
		up_read(&current->mm->mmap_sem);
	}
	return result;
}

/**
 * nfs_free_user_pages - tear down page struct array
 * @pages: array of page struct pointers underlying target buffer
 */
static void
nfs_free_user_pages(struct page **pages, int npages, int do_dirty)
{
	int i;
	for (i = 0; i < npages; i++) {
		if (do_dirty)
			set_page_dirty_lock(pages[i]);
		page_cache_release(pages[i]);
	}
	kfree(pages);
}

/**
 * nfs_direct_read_seg - Read in one iov segment.  Generate separate
 *                        read RPCs for each "rsize" bytes.
 * @inode: target inode
 * @file: target file (may be NULL)
 * user_addr: starting address of this segment of user's buffer
 * count: size of this segment
 * file_offset: offset in file to begin the operation
 * @pages: array of addresses of page structs defining user's buffer
 * nr_pages: size of pages array
 */
static int
nfs_direct_read_seg(struct inode *inode, struct file *file,
		unsigned long user_addr, size_t count, loff_t file_offset,
		struct page **pages, int nr_pages)
{
	const unsigned int rsize = NFS_SERVER(inode)->rsize;
	int tot_bytes = 0;
	int curpage = 0;
	struct nfs_read_data	rdata = {
		.inode		= inode,
		.args		= {
			.fh		= NFS_FH(inode),
			.lockowner	= current->files,
		},
		.res		= {
			.fattr		= &rdata.fattr,
		},
	};

	rdata.args.pgbase = user_addr & ~PAGE_MASK;
	rdata.args.offset = file_offset;
        do {
		int result;

		rdata.args.count = count;
                if (rdata.args.count > rsize)
                        rdata.args.count = rsize;
		rdata.args.pages = &pages[curpage];

		dprintk("NFS: direct read: c=%u o=%Ld ua=%lu, pb=%u, cp=%u\n",
			rdata.args.count, (long long) rdata.args.offset,
			user_addr + tot_bytes, rdata.args.pgbase, curpage);

		lock_kernel();
		result = NFS_PROTO(inode)->read(&rdata, file);
		unlock_kernel();

		if (result <= 0) {
			if (tot_bytes > 0)
				break;
			if (result == -EISDIR)
				result = -EINVAL;
			return result;
		}

                tot_bytes += result;
		if (rdata.res.eof)
			break;

                rdata.args.offset += result;
		rdata.args.pgbase += result;
		curpage += rdata.args.pgbase >> PAGE_SHIFT;
		rdata.args.pgbase &= ~PAGE_MASK;
		count -= result;
	} while (count != 0);

	/* XXX: should we zero the rest of the user's buffer if we
	 *      hit eof? */

	return tot_bytes;
}

/**
 * nfs_direct_read - For each iov segment, map the user's buffer
 *                   then generate read RPCs.
 * @inode: target inode
 * @file: target file (may be NULL)
 * @iov: array of vectors that define I/O buffer
 * file_offset: offset in file to begin the operation
 * nr_segs: size of iovec array
 *
 * generic_file_direct_IO has already pushed out any non-direct
 * writes so that this read will see them when we read from the
 * server.
 */
static ssize_t
nfs_direct_read(struct inode *inode, struct file *file,
		const struct iovec *iov, loff_t file_offset,
		unsigned long nr_segs)
{
	ssize_t tot_bytes = 0;
	unsigned long seg = 0;

	while ((seg < nr_segs) && (tot_bytes >= 0)) {
		ssize_t result;
		int page_count;
		struct page **pages;
		const struct iovec *vec = &iov[seg++];
		unsigned long user_addr = (unsigned long) vec->iov_base;
		size_t size = vec->iov_len;

                page_count = nfs_get_user_pages(READ, user_addr, size, &pages);
                if (page_count < 0) {
                        nfs_free_user_pages(pages, 0, 0);
			if (tot_bytes > 0)
				break;
                        return page_count;
                }

		result = nfs_direct_read_seg(inode, file, user_addr, size,
				file_offset, pages, page_count);

		nfs_free_user_pages(pages, page_count, 1);

		if (result <= 0) {
			if (tot_bytes > 0)
				break;
			return result;
		}
		tot_bytes += result;
		file_offset += result;
		if (result < size)
			break;
	}

	return tot_bytes;
}

/**
 * nfs_direct_write_seg - Write out one iov segment.  Generate separate
 *                        write RPCs for each "wsize" bytes, then commit.
 * @inode: target inode
 * @file: target file (may be NULL)
 * user_addr: starting address of this segment of user's buffer
 * count: size of this segment
 * file_offset: offset in file to begin the operation
 * @pages: array of addresses of page structs defining user's buffer
 * nr_pages: size of pages array
 */
static int
nfs_direct_write_seg(struct inode *inode, struct file *file,
		unsigned long user_addr, size_t count, loff_t file_offset,
		struct page **pages, int nr_pages)
{
	const unsigned int wsize = NFS_SERVER(inode)->wsize;
	size_t request;
	int curpage, need_commit, result, tot_bytes;
	struct nfs_writeverf first_verf;
	struct nfs_write_data	wdata = {
		.inode		= inode,
		.args		= {
			.fh		= NFS_FH(inode),
			.lockowner	= current->files,
		},
		.res		= {
			.fattr		= &wdata.fattr,
			.verf		= &wdata.verf,
		},
	};

	wdata.args.stable = NFS_UNSTABLE;
	if (IS_SYNC(inode) || NFS_PROTO(inode)->version == 2 || count <= wsize)
		wdata.args.stable = NFS_FILE_SYNC;

	nfs_begin_data_update(inode);
retry:
	need_commit = 0;
	tot_bytes = 0;
	curpage = 0;
	request = count;
	wdata.args.pgbase = user_addr & ~PAGE_MASK;
	wdata.args.offset = file_offset;
        do {
		wdata.args.count = request;
                if (wdata.args.count > wsize)
                        wdata.args.count = wsize;
		wdata.args.pages = &pages[curpage];

		dprintk("NFS: direct write: c=%u o=%Ld ua=%lu, pb=%u, cp=%u\n",
			wdata.args.count, (long long) wdata.args.offset,
			user_addr + tot_bytes, wdata.args.pgbase, curpage);

		lock_kernel();
		result = NFS_PROTO(inode)->write(&wdata, file);
		unlock_kernel();

		if (result <= 0) {
			if (tot_bytes > 0)
				break;
			goto out;
		}

		if (tot_bytes == 0)
			memcpy(&first_verf.verifier, &wdata.verf.verifier,
								VERF_SIZE);
		if (wdata.verf.committed != NFS_FILE_SYNC) {
			need_commit = 1;
			if (memcmp(&first_verf.verifier,
					&wdata.verf.verifier, VERF_SIZE))
				goto sync_retry;
		}

                tot_bytes += result;
                wdata.args.offset += result;
		wdata.args.pgbase += result;
		curpage += wdata.args.pgbase >> PAGE_SHIFT;
		wdata.args.pgbase &= ~PAGE_MASK;
		request -= result;
	} while (request != 0);

	/*
	 * Commit data written so far, even in the event of an error
	 */
	if (need_commit) {
		wdata.args.count = tot_bytes;
		wdata.args.offset = file_offset;

		lock_kernel();
		result = NFS_PROTO(inode)->commit(&wdata, file);
		unlock_kernel();

		if (result < 0 || memcmp(&first_verf.verifier,
						&wdata.verf.verifier,
						VERF_SIZE) != 0)
			goto sync_retry;
	}
	result = tot_bytes;

out:
	nfs_end_data_update_defer(inode);

	return result;

sync_retry:
	wdata.args.stable = NFS_FILE_SYNC;
	goto retry;
}

/**
 * nfs_direct_write - For each iov segment, map the user's buffer
 *                    then generate write and commit RPCs.
 * @inode: target inode
 * @file: target file (may be NULL)
 * @iov: array of vectors that define I/O buffer
 * file_offset: offset in file to begin the operation
 * nr_segs: size of iovec array
 *
 * Upon return, generic_file_direct_IO invalidates any cached pages
 * that non-direct readers might access, so they will pick up these
 * writes immediately.
 */
static ssize_t
nfs_direct_write(struct inode *inode, struct file *file,
		const struct iovec *iov, loff_t file_offset,
		unsigned long nr_segs)
{
	ssize_t tot_bytes = 0;
	unsigned long seg = 0;

	while ((seg < nr_segs) && (tot_bytes >= 0)) {
		ssize_t result;
		int page_count;
		struct page **pages;
		const struct iovec *vec = &iov[seg++];
		unsigned long user_addr = (unsigned long) vec->iov_base;
		size_t size = vec->iov_len;

                page_count = nfs_get_user_pages(WRITE, user_addr, size, &pages);
                if (page_count < 0) {
                        nfs_free_user_pages(pages, 0, 0);
			if (tot_bytes > 0)
				break;
                        return page_count;
                }

		result = nfs_direct_write_seg(inode, file, user_addr, size,
				file_offset, pages, page_count);
		nfs_free_user_pages(pages, page_count, 0);

		if (result <= 0) {
			if (tot_bytes > 0)
				break;
			return result;
		}
		tot_bytes += result;
		file_offset += result;
		if (result < size)
			break;
	}
	return tot_bytes;
}

/**
 * nfs_direct_IO - NFS address space operation for direct I/O
 * rw: direction (read or write)
 * @iocb: target I/O control block
 * @iov: array of vectors that define I/O buffer
 * file_offset: offset in file to begin the operation
 * nr_segs: size of iovec array
 *
 */
ssize_t
nfs_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov,
		loff_t file_offset, unsigned long nr_segs)
{
	ssize_t result = -EINVAL;
	struct file *file = iocb->ki_filp;
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;

	/*
	 * No support for async yet
	 */
	if (!is_sync_kiocb(iocb))
		return result;

	switch (rw) {
	case READ:
		dprintk("NFS: direct_IO(read) (%s) off/no(%Lu/%lu)\n",
				dentry->d_name.name, file_offset, nr_segs);

		result = nfs_direct_read(inode, file, iov,
						file_offset, nr_segs);
		break;
	case WRITE:
		dprintk("NFS: direct_IO(write) (%s) off/no(%Lu/%lu)\n",
				dentry->d_name.name, file_offset, nr_segs);

		result = nfs_direct_write(inode, file, iov,
						file_offset, nr_segs);
		break;
	default:
		break;
	}
	return result;
}

/**
 * nfs_file_direct_read - file direct read operation for NFS files
 * @iocb: target I/O control block
 * @buf: user's buffer into which to read data
 * count: number of bytes to read
 * pos: byte offset in file where reading starts
 *
 * We use this function for direct reads instead of calling
 * generic_file_aio_read() in order to avoid gfar's check to see if
 * the request starts before the end of the file.  For that check
 * to work, we must generate a GETATTR before each direct read, and
 * even then there is a window between the GETATTR and the subsequent
 * READ where the file size could change.  So our preference is simply
 * to do all reads the application wants, and the server will take
 * care of managing the end of file boundary.
 * 
 * This function also eliminates unnecessarily updating the file's
 * atime locally, as the NFS server sets the file's atime, and this
 * client must read the updated atime from the server back into its
 * cache.
 */
ssize_t
nfs_file_direct_read(struct kiocb *iocb, char *buf, size_t count, loff_t pos)
{
	ssize_t retval = -EINVAL;
	loff_t *ppos = &iocb->ki_pos;
	struct file *file = iocb->ki_filp;
	struct dentry *dentry = file->f_dentry;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = count,
	};

	dprintk("nfs: direct read(%s/%s, %lu@%lu)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		(unsigned long) count, (unsigned long) pos);

	if (!is_sync_kiocb(iocb))
		goto out;
	if (count < 0)
		goto out;
	retval = -EFAULT;
	if (!access_ok(VERIFY_WRITE, iov.iov_base, iov.iov_len))
		goto out;
	retval = 0;
	if (!count)
		goto out;

	if (mapping->nrpages) {
		retval = filemap_fdatawrite(mapping);
		if (retval == 0)
			retval = filemap_fdatawait(mapping);
		if (retval)
			goto out;
	}

	retval = nfs_direct_read(inode, file, &iov, pos, 1);
	if (retval > 0)
		*ppos = pos + retval;

out:
	return retval;
}

/**
 * nfs_file_direct_write - file direct write operation for NFS files
 * @iocb: target I/O control block
 * @buf: user's buffer from which to write data
 * count: number of bytes to write
 * pos: byte offset in file where writing starts
 *
 * We use this function for direct writes instead of calling
 * generic_file_aio_write() in order to avoid taking the inode
 * semaphore and updating the i_size.  The NFS server will set
 * the new i_size and this client must read the updated size
 * back into its cache.  We let the server do generic write
 * parameter checking and report problems.
 *
 * We also avoid an unnecessary invocation of generic_osync_inode(),
 * as it is fairly meaningless to sync the metadata of an NFS file.
 *
 * We eliminate local atime updates, see direct read above.
 *
 * We avoid unnecessary page cache invalidations for normal cached
 * readers of this file.
 *
 * Note that O_APPEND is not supported for NFS direct writes, as there
 * is no atomic O_APPEND write facility in the NFS protocol.
 */
ssize_t
nfs_file_direct_write(struct kiocb *iocb, const char __user *buf, size_t count, loff_t pos)
{
	ssize_t retval = -EINVAL;
	loff_t *ppos = &iocb->ki_pos;
	unsigned long limit = current->rlim[RLIMIT_FSIZE].rlim_cur;
	struct file *file = iocb->ki_filp;
	struct dentry *dentry = file->f_dentry;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = count,
	};

	dfprintk(VFS, "nfs: direct write(%s/%s(%ld), %lu@%lu)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		inode->i_ino, (unsigned long) count, (unsigned long) pos);

	if (!is_sync_kiocb(iocb))
		goto out;
	if (count < 0)
		goto out;
        if (pos < 0)
		goto out;
	retval = -EFAULT;
	if (!access_ok(VERIFY_READ, iov.iov_base, iov.iov_len))
		goto out;
        if (file->f_error) {
                retval = file->f_error;
                file->f_error = 0;
                goto out;
        }
	retval = -EFBIG;
	if (limit != RLIM_INFINITY) {
		if (pos >= limit) {
			send_sig(SIGXFSZ, current, 0);
			goto out;
		}
		if (count > limit - (unsigned long) pos)
			count = limit - (unsigned long) pos;
	}
	retval = 0;
	if (!count)
		goto out;

	if (mapping->nrpages) {
		retval = filemap_fdatawrite(mapping);
		if (retval == 0)
			retval = filemap_fdatawait(mapping);
		if (retval)
			goto out;
	}

	retval = nfs_direct_write(inode, file, &iov, pos, 1);
	if (mapping->nrpages)
		invalidate_inode_pages2(mapping);
	if (retval > 0)
		*ppos = pos + retval;

out:
	return retval;
}

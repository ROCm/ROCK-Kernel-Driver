/*
 * linux/fs/nfs/write.c
 *
 * Writing file data over NFS.
 *
 * We do it like this: When a (user) process wishes to write data to an
 * NFS file, a write request is allocated that contains the RPC task data
 * plus some info on the page to be written, and added to the inode's
 * write chain. If the process writes past the end of the page, an async
 * RPC call to write the page is scheduled immediately; otherwise, the call
 * is delayed for a few seconds.
 *
 * Just like readahead, no async I/O is performed if wsize < PAGE_SIZE.
 *
 * Write requests are kept on the inode's writeback list. Each entry in
 * that list references the page (portion) to be written. When the
 * cache timeout has expired, the RPC task is woken up, and tries to
 * lock the page. As soon as it manages to do so, the request is moved
 * from the writeback list to the writelock list.
 *
 * Note: we must make sure never to confuse the inode passed in the
 * write_page request with the one in page->inode. As far as I understand
 * it, these are different when doing a swap-out.
 *
 * To understand everything that goes on here and in the NFS read code,
 * one should be aware that a page is locked in exactly one of the following
 * cases:
 *
 *  -	A write request is in progress.
 *  -	A user process is in generic_file_write/nfs_update_page
 *  -	A user process is in generic_file_read
 *
 * Also note that because of the way pages are invalidated in
 * nfs_revalidate_inode, the following assertions hold:
 *
 *  -	If a page is dirty, there will be no read requests (a page will
 *	not be re-read unless invalidated by nfs_revalidate_inode).
 *  -	If the page is not uptodate, there will be no pending write
 *	requests, and no process will be in nfs_update_page.
 *
 * FIXME: Interaction with the vmscan routines is not optimal yet.
 * Either vmscan must be made nfs-savvy, or we need a different page
 * reclaim concept that supports something like FS-independent
 * buffer_heads with a b_ops-> field.
 *
 * Copyright (C) 1996, 1997, Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/mpage.h>
#include <linux/writeback.h>

#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs_page.h>
#include <asm/uaccess.h>
#include <linux/smp_lock.h>
#include <linux/mempool.h>

#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

#define MIN_POOL_WRITE		(32)
#define MIN_POOL_COMMIT		(4)

/*
 * Local function declarations
 */
static struct nfs_page * nfs_update_request(struct file*, struct inode *,
					    struct page *,
					    unsigned int, unsigned int);

static kmem_cache_t *nfs_wdata_cachep;
static mempool_t *nfs_wdata_mempool;
static mempool_t *nfs_commit_mempool;

static __inline__ struct nfs_write_data *nfs_writedata_alloc(void)
{
	struct nfs_write_data	*p;
	p = (struct nfs_write_data *)mempool_alloc(nfs_wdata_mempool, SLAB_NOFS);
	if (p) {
		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->pages);
	}
	return p;
}

static __inline__ void nfs_writedata_free(struct nfs_write_data *p)
{
	mempool_free(p, nfs_wdata_mempool);
}

void nfs_writedata_release(struct rpc_task *task)
{
	struct nfs_write_data	*wdata = (struct nfs_write_data *)task->tk_calldata;
	nfs_writedata_free(wdata);
}

static __inline__ struct nfs_write_data *nfs_commit_alloc(void)
{
	struct nfs_write_data	*p;
	p = (struct nfs_write_data *)mempool_alloc(nfs_commit_mempool, SLAB_NOFS);
	if (p) {
		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->pages);
	}
	return p;
}

static __inline__ void nfs_commit_free(struct nfs_write_data *p)
{
	mempool_free(p, nfs_commit_mempool);
}

void nfs_commit_release(struct rpc_task *task)
{
	struct nfs_write_data	*wdata = (struct nfs_write_data *)task->tk_calldata;
	nfs_commit_free(wdata);
}

/* Adjust the file length if we're writing beyond the end */
static void nfs_grow_file(struct page *page, unsigned int offset, unsigned int count)
{
	struct inode *inode = page->mapping->host;
	loff_t end, i_size = i_size_read(inode);
	unsigned long end_index = (i_size - 1) >> PAGE_CACHE_SHIFT;

	if (i_size > 0 && page->index < end_index)
		return;
	end = ((loff_t)page->index << PAGE_CACHE_SHIFT) + ((loff_t)offset+count);
	if (i_size >= end)
		return;
	i_size_write(inode, end);
}

/* We can set the PG_uptodate flag if we see that a write request
 * covers the full page.
 */
static void nfs_mark_uptodate(struct page *page, unsigned int base, unsigned int count)
{
	loff_t end_offs;

	if (PageUptodate(page))
		return;
	if (base != 0)
		return;
	if (count == PAGE_CACHE_SIZE) {
		SetPageUptodate(page);
		return;
	}

	end_offs = i_size_read(page->mapping->host) - 1;
	if (end_offs < 0)
		return;
	/* Is this the last page? */
	if (page->index != (unsigned long)(end_offs >> PAGE_CACHE_SHIFT))
		return;
	/* This is the last page: set PG_uptodate if we cover the entire
	 * extent of the data, then zero the rest of the page.
	 */
	if (count == (unsigned int)(end_offs & (PAGE_CACHE_SIZE - 1)) + 1) {
		memclear_highpage_flush(page, count, PAGE_CACHE_SIZE - count);
		SetPageUptodate(page);
	}
}

/*
 * Write a page synchronously.
 * Offset is the data offset within the page.
 */
static int
nfs_writepage_sync(struct file *file, struct inode *inode, struct page *page,
		   unsigned int offset, unsigned int count)
{
	unsigned int	wsize = NFS_SERVER(inode)->wsize;
	int		result, written = 0;
	int		swapfile = IS_SWAPFILE(inode);
	struct nfs_write_data	wdata = {
		.flags		= swapfile ? NFS_RPC_SWAPFLAGS : 0,
		.cred		= NULL,
		.inode		= inode,
		.args		= {
			.fh		= NFS_FH(inode),
			.pages		= &page,
			.stable		= NFS_FILE_SYNC,
			.pgbase		= offset,
			.count		= wsize,
		},
		.res		= {
			.fattr		= &wdata.fattr,
			.verf		= &wdata.verf,
		},
	};

	dprintk("NFS:      nfs_writepage_sync(%s/%Ld %d@%Ld)\n",
		inode->i_sb->s_id,
		(long long)NFS_FILEID(inode),
		count, (long long)(page_offset(page) + offset));

	nfs_begin_data_update(inode);
	do {
		if (count < wsize && !swapfile)
			wdata.args.count = count;
		wdata.args.offset = page_offset(page) + wdata.args.pgbase;

		result = NFS_PROTO(inode)->write(&wdata, file);

		if (result < 0) {
			/* Must mark the page invalid after I/O error */
			ClearPageUptodate(page);
			goto io_error;
		}
		if (result < wdata.args.count)
			printk(KERN_WARNING "NFS: short write, count=%u, result=%d\n",
					wdata.args.count, result);

		wdata.args.offset += result;
	        wdata.args.pgbase += result;
		written += result;
		count -= result;
	} while (count);
	/* Update file length */
	nfs_grow_file(page, offset, written);
	/* Set the PG_uptodate flag? */
	nfs_mark_uptodate(page, offset, written);

	if (PageError(page))
		ClearPageError(page);

io_error:
	nfs_end_data_update(inode);
	if (wdata.cred)
		put_rpccred(wdata.cred);

	return written ? written : result;
}

static int nfs_writepage_async(struct file *file, struct inode *inode,
		struct page *page, unsigned int offset, unsigned int count)
{
	struct nfs_page	*req;
	int		status;

	req = nfs_update_request(file, inode, page, offset, count);
	status = (IS_ERR(req)) ? PTR_ERR(req) : 0;
	if (status < 0)
		goto out;
	/* Update file length */
	nfs_grow_file(page, offset, count);
	/* Set the PG_uptodate flag? */
	nfs_mark_uptodate(page, offset, count);
	nfs_unlock_request(req);
 out:
	return status;
}

/*
 * Write an mmapped page to the server.
 */
int
nfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	unsigned long end_index;
	unsigned offset = PAGE_CACHE_SIZE;
	loff_t i_size = i_size_read(inode);
	int inode_referenced = 0;
	int err;

	/*
	 * Note: We need to ensure that we have a reference to the inode
	 *       if we are to do asynchronous writes. If not, waiting
	 *       in nfs_wait_on_request() may deadlock with clear_inode().
	 *
	 *       If igrab() fails here, then it is in any case safe to
	 *       call nfs_wb_page(), since there will be no pending writes.
	 */
	if (igrab(inode) != 0)
		inode_referenced = 1;
	end_index = i_size >> PAGE_CACHE_SHIFT;

	/* Ensure we've flushed out any previous writes */
	nfs_wb_page(inode,page);

	/* easy case */
	if (page->index < end_index)
		goto do_it;
	/* things got complicated... */
	offset = i_size & (PAGE_CACHE_SIZE-1);

	/* OK, are we completely out? */
	err = 0; /* potential race with truncate - ignore */
	if (page->index >= end_index+1 || !offset)
		goto out;
do_it:
	lock_kernel();
	if (NFS_SERVER(inode)->wsize >= PAGE_CACHE_SIZE && !IS_SYNC(inode) &&
			inode_referenced) {
		err = nfs_writepage_async(NULL, inode, page, 0, offset);
		if (err >= 0)
			err = 0;
	} else {
		err = nfs_writepage_sync(NULL, inode, page, 0, offset); 
		if (err == offset)
			err = 0;
	}
	unlock_kernel();
out:
	unlock_page(page);
	if (inode_referenced)
		iput(inode);
	return err; 
}

int
nfs_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	int is_sync = !wbc->nonblocking;
	int err;

	err = generic_writepages(mapping, wbc);
	if (err)
		goto out;
	err = nfs_flush_inode(inode, 0, 0, 0);
	if (err < 0)
		goto out;
	if (wbc->sync_mode == WB_SYNC_HOLD)
		goto out;
	if (is_sync && wbc->sync_mode == WB_SYNC_ALL) {
		err = nfs_wb_all(inode);
	} else
		nfs_commit_inode(inode, 0, 0, 0);
out:
	return err;
}

/*
 * Insert a write request into an inode
 */
static inline int
nfs_inode_add_request(struct inode *inode, struct nfs_page *req)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int error;

	error = radix_tree_insert(&nfsi->nfs_page_tree, req->wb_index, req);
	BUG_ON(error == -EEXIST);
	if (error)
		return error;
	if (!nfsi->npages) {
		igrab(inode);
		nfs_begin_data_update(inode);
	}
	nfsi->npages++;
	req->wb_count++;
	return 0;
}

/*
 * Insert a write request into an inode
 */
static inline void
nfs_inode_remove_request(struct nfs_page *req)
{
	struct nfs_inode *nfsi;
	struct inode *inode;

	BUG_ON (!NFS_WBACK_BUSY(req));
	spin_lock(&nfs_wreq_lock);
	inode = req->wb_inode;
	nfsi = NFS_I(inode);
	radix_tree_delete(&nfsi->nfs_page_tree, req->wb_index);
	nfsi->npages--;
	if (!nfsi->npages) {
		spin_unlock(&nfs_wreq_lock);
		nfs_end_data_update(inode);
		iput(inode);
	} else
		spin_unlock(&nfs_wreq_lock);
	nfs_clear_request(req);
	nfs_release_request(req);
}

/*
 * Find a request
 */
static inline struct nfs_page *
_nfs_find_request(struct inode *inode, unsigned long index)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_page *req;

	req = (struct nfs_page*)radix_tree_lookup(&nfsi->nfs_page_tree, index);
	if (req)
		req->wb_count++;
	return req;
}

static struct nfs_page *
nfs_find_request(struct inode *inode, unsigned long index)
{
	struct nfs_page		*req;

	spin_lock(&nfs_wreq_lock);
	req = _nfs_find_request(inode, index);
	spin_unlock(&nfs_wreq_lock);
	return req;
}

/*
 * Add a request to the inode's dirty list.
 */
static inline void
nfs_mark_request_dirty(struct nfs_page *req)
{
	struct inode *inode = req->wb_inode;
	struct nfs_inode *nfsi = NFS_I(inode);

	spin_lock(&nfs_wreq_lock);
	nfs_list_add_request(req, &nfsi->dirty);
	nfsi->ndirty++;
	spin_unlock(&nfs_wreq_lock);
	inc_page_state(nr_dirty);
	mark_inode_dirty(inode);
}

/*
 * Check if a request is dirty
 */
static inline int
nfs_dirty_request(struct nfs_page *req)
{
	struct nfs_inode *nfsi = NFS_I(req->wb_inode);
	return !list_empty(&req->wb_list) && req->wb_list_head == &nfsi->dirty;
}

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
/*
 * Add a request to the inode's commit list.
 */
static inline void
nfs_mark_request_commit(struct nfs_page *req)
{
	struct inode *inode = req->wb_inode;
	struct nfs_inode *nfsi = NFS_I(inode);

	spin_lock(&nfs_wreq_lock);
	nfs_list_add_request(req, &nfsi->commit);
	nfsi->ncommit++;
	spin_unlock(&nfs_wreq_lock);
	inc_page_state(nr_unstable);
	mark_inode_dirty(inode);
}
#endif

/*
 * Wait for a request to complete.
 *
 * Interruptible by signals only if mounted with intr flag.
 */
static int
nfs_wait_on_requests(struct inode *inode, unsigned long idx_start, unsigned int npages)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_page *req;
	unsigned long		idx_end, next;
	unsigned int		res = 0;
	int			error;

	if (npages == 0)
		idx_end = ~0;
	else
		idx_end = idx_start + npages - 1;

	spin_lock(&nfs_wreq_lock);
	next = idx_start;
	while (radix_tree_gang_lookup(&nfsi->nfs_page_tree, (void **)&req, next, 1)) {
		if (req->wb_index > idx_end)
			break;

		next = req->wb_index + 1;
		if (!NFS_WBACK_BUSY(req))
			continue;

		req->wb_count++;
		spin_unlock(&nfs_wreq_lock);
		error = nfs_wait_on_request(req);
		nfs_release_request(req);
		if (error < 0)
			return error;
		spin_lock(&nfs_wreq_lock);
		res++;
	}
	spin_unlock(&nfs_wreq_lock);
	return res;
}

/*
 * nfs_scan_dirty - Scan an inode for dirty requests
 * @inode: NFS inode to scan
 * @dst: destination list
 * @idx_start: lower bound of page->index to scan.
 * @npages: idx_start + npages sets the upper bound to scan.
 *
 * Moves requests from the inode's dirty page list.
 * The requests are *not* checked to ensure that they form a contiguous set.
 */
static int
nfs_scan_dirty(struct inode *inode, struct list_head *dst, unsigned long idx_start, unsigned int npages)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int	res;
	res = nfs_scan_list(&nfsi->dirty, dst, idx_start, npages);
	nfsi->ndirty -= res;
	sub_page_state(nr_dirty,res);
	if ((nfsi->ndirty == 0) != list_empty(&nfsi->dirty))
		printk(KERN_ERR "NFS: desynchronized value of nfs_i.ndirty.\n");
	return res;
}

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
/*
 * nfs_scan_commit - Scan an inode for commit requests
 * @inode: NFS inode to scan
 * @dst: destination list
 * @idx_start: lower bound of page->index to scan.
 * @npages: idx_start + npages sets the upper bound to scan.
 *
 * Moves requests from the inode's 'commit' request list.
 * The requests are *not* checked to ensure that they form a contiguous set.
 */
static int
nfs_scan_commit(struct inode *inode, struct list_head *dst, unsigned long idx_start, unsigned int npages)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int	res;
	res = nfs_scan_list(&nfsi->commit, dst, idx_start, npages);
	nfsi->ncommit -= res;
	if ((nfsi->ncommit == 0) != list_empty(&nfsi->commit))
		printk(KERN_ERR "NFS: desynchronized value of nfs_i.ncommit.\n");
	return res;
}
#endif


/*
 * Try to update any existing write request, or create one if there is none.
 * In order to match, the request's credentials must match those of
 * the calling process.
 *
 * Note: Should always be called with the Page Lock held!
 */
static struct nfs_page *
nfs_update_request(struct file* file, struct inode *inode, struct page *page,
		   unsigned int offset, unsigned int bytes)
{
	struct nfs_page		*req, *new = NULL;
	unsigned long		rqend, end;

	end = offset + bytes;

	for (;;) {
		/* Loop over all inode entries and see if we find
		 * A request for the page we wish to update
		 */
		spin_lock(&nfs_wreq_lock);
		req = _nfs_find_request(inode, page->index);
		if (req) {
			if (!nfs_lock_request_dontget(req)) {
				int error;
				spin_unlock(&nfs_wreq_lock);
				error = nfs_wait_on_request(req);
				nfs_release_request(req);
				if (error < 0)
					return ERR_PTR(error);
				continue;
			}
			spin_unlock(&nfs_wreq_lock);
			if (new)
				nfs_release_request(new);
			break;
		}

		if (new) {
			int error;
			nfs_lock_request_dontget(new);
			error = nfs_inode_add_request(inode, new);
			if (error) {
				spin_unlock(&nfs_wreq_lock);
				nfs_unlock_request(new);
				return ERR_PTR(error);
			}
			spin_unlock(&nfs_wreq_lock);
			nfs_mark_request_dirty(new);
			return new;
		}
		spin_unlock(&nfs_wreq_lock);

		new = nfs_create_request(file, inode, page, offset, bytes);
		if (IS_ERR(new))
			return new;
		if (file) {
			new->wb_file = file;
			get_file(file);
		}
	}

	/* We have a request for our page.
	 * If the creds don't match, or the
	 * page addresses don't match,
	 * tell the caller to wait on the conflicting
	 * request.
	 */
	rqend = req->wb_offset + req->wb_bytes;
	if (req->wb_file != file
	    || req->wb_page != page
	    || !nfs_dirty_request(req)
	    || offset > rqend || end < req->wb_offset) {
		nfs_unlock_request(req);
		return ERR_PTR(-EBUSY);
	}

	/* Okay, the request matches. Update the region */
	if (offset < req->wb_offset) {
		req->wb_offset = offset;
		req->wb_pgbase = offset;
		req->wb_bytes = rqend - req->wb_offset;
	}

	if (end > rqend)
		req->wb_bytes = end - req->wb_offset;

	return req;
}

int
nfs_flush_incompatible(struct file *file, struct page *page)
{
	struct inode	*inode = page->mapping->host;
	struct nfs_page	*req;
	int		status = 0;
	/*
	 * Look for a request corresponding to this page. If there
	 * is one, and it belongs to another file, we flush it out
	 * before we try to copy anything into the page. Do this
	 * due to the lack of an ACCESS-type call in NFSv2.
	 * Also do the same if we find a request from an existing
	 * dropped page.
	 */
	req = nfs_find_request(inode, page->index);
	if (req) {
		if (!NFS_PROTO(inode)->request_compatible(req, file, page))
			status = nfs_wb_page(inode, page);
		nfs_release_request(req);
	}
	return (status < 0) ? status : 0;
}

/*
 * Update and possibly write a cached page of an NFS file.
 *
 * XXX: Keep an eye on generic_file_read to make sure it doesn't do bad
 * things with a page scheduled for an RPC call (e.g. invalidate it).
 */
int
nfs_updatepage(struct file *file, struct page *page, unsigned int offset, unsigned int count)
{
	struct dentry	*dentry = file->f_dentry;
	struct inode	*inode = page->mapping->host;
	struct nfs_page	*req;
	int		status = 0;

	dprintk("NFS:      nfs_updatepage(%s/%s %d@%Ld)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		count, (long long)(page_offset(page) +offset));

	/*
	 * If wsize is smaller than page size, update and write
	 * page synchronously.
	 */
	if (NFS_SERVER(inode)->wsize < PAGE_CACHE_SIZE || IS_SYNC(inode)) {
		status = nfs_writepage_sync(file, inode, page, offset, count);
		if (status > 0) {
			if (offset == 0 && status == PAGE_CACHE_SIZE)
				SetPageUptodate(page);
			return 0;
		}
		return status;
	}

	/* If we're not using byte range locks, and we know the page
	 * is entirely in cache, it may be more efficient to avoid
	 * fragmenting write requests.
	 */
	if (PageUptodate(page) && inode->i_flock == NULL) {
		loff_t end_offs = i_size_read(inode) - 1;
		unsigned long end_index = end_offs >> PAGE_CACHE_SHIFT;

		count += offset;
		offset = 0;
		if (unlikely(end_offs < 0)) {
			/* Do nothing */
		} else if (page->index == end_index) {
			unsigned int pglen;
			pglen = (unsigned int)(end_offs & (PAGE_CACHE_SIZE-1)) + 1;
			if (count < pglen)
				count = pglen;
		} else if (page->index < end_index)
			count = PAGE_CACHE_SIZE;
	}

	/*
	 * Try to find an NFS request corresponding to this page
	 * and update it.
	 * If the existing request cannot be updated, we must flush
	 * it out now.
	 */
	do {
		req = nfs_update_request(file, inode, page, offset, count);
		status = (IS_ERR(req)) ? PTR_ERR(req) : 0;
		if (status != -EBUSY)
			break;
		/* Request could not be updated. Flush it out and try again */
		status = nfs_wb_page(inode, page);
	} while (status >= 0);
	if (status < 0)
		goto done;

	status = 0;

	/* Update file length */
	nfs_grow_file(page, offset, count);
	/* Set the PG_uptodate flag? */
	nfs_mark_uptodate(page, req->wb_pgbase, req->wb_bytes);
	nfs_unlock_request(req);
done:
        dprintk("NFS:      nfs_updatepage returns %d (isize %Ld)\n",
			status, (long long)i_size_read(inode));
	if (status < 0)
		ClearPageUptodate(page);
	return status;
}

/*
 * Set up the argument/result storage required for the RPC call.
 */
static void
nfs_write_rpcsetup(struct list_head *head, struct nfs_write_data *data, int how)
{
	struct rpc_task		*task = &data->task;
	struct inode		*inode;
	struct nfs_page		*req;
	struct page		**pages;
	unsigned int		count;

	/* Set up the RPC argument and reply structs
	 * NB: take care not to mess about with data->commit et al. */

	pages = data->pagevec;
	count = 0;
	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_list_add_request(req, &data->pages);
		set_page_writeback(req->wb_page);
		*pages++ = req->wb_page;
		count += req->wb_bytes;
	}
	req = nfs_list_entry(data->pages.next);
	data->inode = inode = req->wb_inode;
	data->cred = req->wb_cred;

	NFS_PROTO(inode)->write_setup(data, count, how);

	dprintk("NFS: %4d initiated write call (req %s/%Ld, %u bytes @ offset %Lu)\n",
		task->tk_pid,
		inode->i_sb->s_id,
		(long long)NFS_FILEID(inode),
		count,
		(unsigned long long)req_offset(req));
}

/*
 * Create an RPC task for the given write request and kick it.
 * The page must have been locked by the caller.
 *
 * It may happen that the page we're passed is not marked dirty.
 * This is the case if nfs_updatepage detects a conflicting request
 * that has been written but not committed.
 */
static int
nfs_flush_one(struct list_head *head, struct inode *inode, int how)
{
	struct rpc_clnt 	*clnt = NFS_CLIENT(inode);
	struct nfs_write_data	*data;
	sigset_t		oldset;

	data = nfs_writedata_alloc();
	if (!data)
		goto out_bad;

	/* Set up the argument struct */
	nfs_write_rpcsetup(head, data, how);

	rpc_clnt_sigmask(clnt, &oldset);
	lock_kernel();
	rpc_execute(&data->task);
	unlock_kernel();
	rpc_clnt_sigunmask(clnt, &oldset);
	return 0;
 out_bad:
	while (!list_empty(head)) {
		struct nfs_page *req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_mark_request_dirty(req);
		nfs_unlock_request(req);
	}
	return -ENOMEM;
}

int
nfs_flush_list(struct list_head *head, int wpages, int how)
{
	LIST_HEAD(one_request);
	struct nfs_page		*req;
	int			error = 0;
	unsigned int		pages = 0;

	while (!list_empty(head)) {
		pages += nfs_coalesce_requests(head, &one_request, wpages);
		req = nfs_list_entry(one_request.next);
		error = nfs_flush_one(&one_request, req->wb_inode, how);
		if (error < 0)
			break;
	}
	if (error >= 0)
		return pages;

	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_mark_request_dirty(req);
		nfs_unlock_request(req);
	}
	return error;
}


/*
 * This function is called when the WRITE call is complete.
 */
void
nfs_writeback_done(struct rpc_task *task)
{
	struct nfs_write_data	*data = (struct nfs_write_data *) task->tk_calldata;
	struct nfs_writeargs	*argp = &data->args;
	struct nfs_writeres	*resp = &data->res;
	struct nfs_page		*req;
	struct page		*page;

	dprintk("NFS: %4d nfs_writeback_done (status %d)\n",
		task->tk_pid, task->tk_status);

	/* We can't handle that yet but we check for it nevertheless */
	if (resp->count < argp->count && task->tk_status >= 0) {
		static unsigned long    complain;
		if (time_before(complain, jiffies)) {
			printk(KERN_WARNING
			       "NFS: Server wrote less than requested.\n");
			complain = jiffies + 300 * HZ;
		}
		/* Can't do anything about it right now except throw
		 * an error. */
		task->tk_status = -EIO;
	}
#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
	if (data->verf.committed < argp->stable && task->tk_status >= 0) {
		/* We tried a write call, but the server did not
		 * commit data to stable storage even though we
		 * requested it.
		 * Note: There is a known bug in Tru64 < 5.0 in which
		 *	 the server reports NFS_DATA_SYNC, but performs
		 *	 NFS_FILE_SYNC. We therefore implement this checking
		 *	 as a dprintk() in order to avoid filling syslog.
		 */
		static unsigned long    complain;

		if (time_before(complain, jiffies)) {
			dprintk("NFS: faulty NFS server %s:"
				" (committed = %d) != (stable = %d)\n",
				NFS_SERVER(data->inode)->hostname,
				data->verf.committed, argp->stable);
			complain = jiffies + 300 * HZ;
		}
	}
#endif

	/*
	 * Process the nfs_page list
	 */
	while (!list_empty(&data->pages)) {
		req = nfs_list_entry(data->pages.next);
		nfs_list_remove_request(req);
		page = req->wb_page;

		dprintk("NFS: write (%s/%Ld %d@%Ld)",
			req->wb_inode->i_sb->s_id,
			(long long)NFS_FILEID(req->wb_inode),
			req->wb_bytes,
			(long long)req_offset(req));

		if (task->tk_status < 0) {
			ClearPageUptodate(page);
			SetPageError(page);
			if (req->wb_file)
				req->wb_file->f_error = task->tk_status;
			end_page_writeback(page);
			nfs_inode_remove_request(req);
			dprintk(", error = %d\n", task->tk_status);
			goto next;
		}
		end_page_writeback(page);

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
		if (argp->stable != NFS_UNSTABLE || data->verf.committed == NFS_FILE_SYNC) {
			nfs_inode_remove_request(req);
			dprintk(" OK\n");
			goto next;
		}
		memcpy(&req->wb_verf, &data->verf, sizeof(req->wb_verf));
		nfs_mark_request_commit(req);
		dprintk(" marked for commit\n");
#else
		nfs_inode_remove_request(req);
#endif
	next:
		nfs_unlock_request(req);
	}
}


#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
/*
 * Set up the argument/result storage required for the RPC call.
 */
static void
nfs_commit_rpcsetup(struct list_head *head, struct nfs_write_data *data, int how)
{
	struct rpc_task		*task = &data->task;
	struct nfs_page		*first, *last;
	struct inode		*inode;
	loff_t			start, end, len;

	/* Set up the RPC argument and reply structs
	 * NB: take care not to mess about with data->commit et al. */

	list_splice_init(head, &data->pages);
	first = nfs_list_entry(data->pages.next);
	last = nfs_list_entry(data->pages.prev);
	inode = first->wb_inode;

	/*
	 * Determine the offset range of requests in the COMMIT call.
	 * We rely on the fact that data->pages is an ordered list...
	 */
	start = req_offset(first);
	end = req_offset(last) + last->wb_bytes;
	len = end - start;
	/* If 'len' is not a 32-bit quantity, pass '0' in the COMMIT call */
	if (end >= i_size_read(inode) || len < 0 || len > (~((u32)0) >> 1))
		len = 0;

	data->inode	  = inode;
	data->cred	  = first->wb_cred;

	NFS_PROTO(inode)->commit_setup(data, start, len, how);
	
	dprintk("NFS: %4d initiated commit call\n", task->tk_pid);
}

/*
 * Commit dirty pages
 */
int
nfs_commit_list(struct list_head *head, int how)
{
	struct rpc_clnt		*clnt;
	struct nfs_write_data	*data;
	struct nfs_page         *req;
	sigset_t		oldset;

	data = nfs_commit_alloc();

	if (!data)
		goto out_bad;

	/* Set up the argument struct */
	nfs_commit_rpcsetup(head, data, how);
	clnt = NFS_CLIENT(data->inode);

	rpc_clnt_sigmask(clnt, &oldset);
	lock_kernel();
	rpc_execute(&data->task);
	unlock_kernel();
	rpc_clnt_sigunmask(clnt, &oldset);
	return 0;
 out_bad:
	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_mark_request_commit(req);
		nfs_unlock_request(req);
	}
	return -ENOMEM;
}

/*
 * COMMIT call returned
 */
void
nfs_commit_done(struct rpc_task *task)
{
	struct nfs_write_data	*data = (struct nfs_write_data *)task->tk_calldata;
	struct nfs_page		*req;
	int res = 0;

        dprintk("NFS: %4d nfs_commit_done (status %d)\n",
                                task->tk_pid, task->tk_status);

	while (!list_empty(&data->pages)) {
		req = nfs_list_entry(data->pages.next);
		nfs_list_remove_request(req);

		dprintk("NFS: commit (%s/%Ld %d@%Ld)",
			req->wb_inode->i_sb->s_id,
			(long long)NFS_FILEID(req->wb_inode),
			req->wb_bytes,
			(long long)req_offset(req));
		if (task->tk_status < 0) {
			if (req->wb_file)
				req->wb_file->f_error = task->tk_status;
			nfs_inode_remove_request(req);
			dprintk(", error = %d\n", task->tk_status);
			goto next;
		}

		/* Okay, COMMIT succeeded, apparently. Check the verifier
		 * returned by the server against all stored verfs. */
		if (!memcmp(req->wb_verf.verifier, data->verf.verifier, sizeof(data->verf.verifier))) {
			/* We have a match */
			nfs_inode_remove_request(req);
			dprintk(" OK\n");
			goto next;
		}
		/* We have a mismatch. Write the page again */
		dprintk(" mismatch\n");
		nfs_mark_request_dirty(req);
	next:
		nfs_unlock_request(req);
		res++;
	}
	sub_page_state(nr_unstable,res);
}
#endif

int nfs_flush_inode(struct inode *inode, unsigned long idx_start,
		   unsigned int npages, int how)
{
	LIST_HEAD(head);
	int			res,
				error = 0;

	spin_lock(&nfs_wreq_lock);
	res = nfs_scan_dirty(inode, &head, idx_start, npages);
	spin_unlock(&nfs_wreq_lock);
	if (res)
		error = nfs_flush_list(&head, NFS_SERVER(inode)->wpages, how);
	if (error < 0)
		return error;
	return res;
}

#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
int nfs_commit_inode(struct inode *inode, unsigned long idx_start,
		    unsigned int npages, int how)
{
	LIST_HEAD(head);
	int			res,
				error = 0;

	spin_lock(&nfs_wreq_lock);
	res = nfs_scan_commit(inode, &head, idx_start, npages);
	if (res) {
		res += nfs_scan_commit(inode, &head, 0, 0);
		spin_unlock(&nfs_wreq_lock);
		error = nfs_commit_list(&head, how);
	} else
		spin_unlock(&nfs_wreq_lock);
	if (error < 0)
		return error;
	return res;
}
#endif

int nfs_sync_inode(struct inode *inode, unsigned long idx_start,
		  unsigned int npages, int how)
{
	int	error,
		wait;

	wait = how & FLUSH_WAIT;
	how &= ~FLUSH_WAIT;

	do {
		error = 0;
		if (wait)
			error = nfs_wait_on_requests(inode, idx_start, npages);
		if (error == 0)
			error = nfs_flush_inode(inode, idx_start, npages, how);
#if defined(CONFIG_NFS_V3) || defined(CONFIG_NFS_V4)
		if (error == 0)
			error = nfs_commit_inode(inode, idx_start, npages, how);
#endif
	} while (error > 0);
	return error;
}

int nfs_init_writepagecache(void)
{
	nfs_wdata_cachep = kmem_cache_create("nfs_write_data",
					     sizeof(struct nfs_write_data),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL, NULL);
	if (nfs_wdata_cachep == NULL)
		return -ENOMEM;

	nfs_wdata_mempool = mempool_create(MIN_POOL_WRITE,
					   mempool_alloc_slab,
					   mempool_free_slab,
					   nfs_wdata_cachep);
	if (nfs_wdata_mempool == NULL)
		return -ENOMEM;

	nfs_commit_mempool = mempool_create(MIN_POOL_COMMIT,
					   mempool_alloc_slab,
					   mempool_free_slab,
					   nfs_wdata_cachep);
	if (nfs_commit_mempool == NULL)
		return -ENOMEM;

	return 0;
}

void nfs_destroy_writepagecache(void)
{
	mempool_destroy(nfs_commit_mempool);
	mempool_destroy(nfs_wdata_mempool);
	if (kmem_cache_destroy(nfs_wdata_cachep))
		printk(KERN_INFO "nfs_write_data: not all structures were freed\n");
}


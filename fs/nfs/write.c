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
#include <linux/malloc.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/file.h>

#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs_flushd.h>
#include <linux/nfs_page.h>
#include <asm/uaccess.h>
#include <linux/smp_lock.h>

#define NFS_PARANOIA 1
#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

/*
 * Spinlock
 */
spinlock_t nfs_wreq_lock = SPIN_LOCK_UNLOCKED;
static atomic_t	nfs_nr_requests = ATOMIC_INIT(0);

/*
 * Local structures
 *
 * This is the struct where the WRITE/COMMIT arguments go.
 */
struct nfs_write_data {
	struct rpc_task		task;
	struct inode		*inode;
	struct rpc_cred		*cred;
	struct nfs_writeargs	args;		/* argument struct */
	struct nfs_writeres	res;		/* result struct */
	struct nfs_fattr	fattr;
	struct nfs_writeverf	verf;
	struct list_head	pages;		/* Coalesced requests we wish to flush */
};

/*
 * Local function declarations
 */
static struct nfs_page * nfs_update_request(struct file*, struct inode *,
					    struct page *,
					    unsigned int, unsigned int);
static void	nfs_strategy(struct inode *inode);
static void	nfs_writeback_done(struct rpc_task *);
#ifdef CONFIG_NFS_V3
static void	nfs_commit_done(struct rpc_task *);
#endif

/* Hack for future NFS swap support */
#ifndef IS_SWAPFILE
# define IS_SWAPFILE(inode)	(0)
#endif

static kmem_cache_t *nfs_page_cachep;
static kmem_cache_t *nfs_wdata_cachep;

static __inline__ struct nfs_page *nfs_page_alloc(void)
{
	struct nfs_page	*p;
	p = kmem_cache_alloc(nfs_page_cachep, SLAB_KERNEL);
	if (p) {
		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->wb_hash);
		INIT_LIST_HEAD(&p->wb_list);
		init_waitqueue_head(&p->wb_wait);
	}
	return p;
}

static __inline__ void nfs_page_free(struct nfs_page *p)
{
	kmem_cache_free(nfs_page_cachep, p);
}

static __inline__ struct nfs_write_data *nfs_writedata_alloc(void)
{
	struct nfs_write_data	*p;
	p = kmem_cache_alloc(nfs_wdata_cachep, SLAB_NFS);
	if (p) {
		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->pages);
	}
	return p;
}

static __inline__ void nfs_writedata_free(struct nfs_write_data *p)
{
	kmem_cache_free(nfs_wdata_cachep, p);
}

static void nfs_writedata_release(struct rpc_task *task)
{
	struct nfs_write_data	*wdata = (struct nfs_write_data *)task->tk_calldata;
	nfs_writedata_free(wdata);
}

/*
 * This function will be used to simulate weak cache consistency
 * under NFSv2 when the NFSv3 attribute patch is included.
 * For the moment, we just call nfs_refresh_inode().
 */
static __inline__ int
nfs_write_attributes(struct inode *inode, struct nfs_fattr *fattr)
{
	if ((fattr->valid & NFS_ATTR_FATTR) && !(fattr->valid & NFS_ATTR_WCC)) {
		fattr->pre_size  = NFS_CACHE_ISIZE(inode);
		fattr->pre_mtime = NFS_CACHE_MTIME(inode);
		fattr->pre_ctime = NFS_CACHE_CTIME(inode);
		fattr->valid |= NFS_ATTR_WCC;
	}
	return nfs_refresh_inode(inode, fattr);
}

/*
 * Write a page synchronously.
 * Offset is the data offset within the page.
 */
static int
nfs_writepage_sync(struct file *file, struct inode *inode, struct page *page,
		   unsigned int offset, unsigned int count)
{
	struct rpc_cred	*cred = NULL;
	loff_t		base;
	unsigned int	wsize = NFS_SERVER(inode)->wsize;
	int		result, refresh = 0, written = 0, flags;
	u8		*buffer;
	struct nfs_fattr fattr;
	struct nfs_writeverf verf;


	if (file)
		cred = nfs_file_cred(file);

	lock_kernel();
	dprintk("NFS:      nfs_writepage_sync(%x/%Ld %d@%Ld)\n",
		inode->i_dev, (long long)NFS_FILEID(inode),
		count, (long long)(page_offset(page) + offset));

	buffer = kmap(page) + offset;
	base = page_offset(page) + offset;

	flags = ((IS_SWAPFILE(inode)) ? NFS_RW_SWAP : 0) | NFS_RW_SYNC;

	do {
		if (count < wsize && !IS_SWAPFILE(inode))
			wsize = count;

		result = NFS_PROTO(inode)->write(inode, cred, &fattr, flags,
						 base, wsize, buffer, &verf);
		nfs_write_attributes(inode, &fattr);

		if (result < 0) {
			/* Must mark the page invalid after I/O error */
			ClearPageUptodate(page);
			goto io_error;
		}
		if (result != wsize)
			printk("NFS: short write, wsize=%u, result=%d\n",
			wsize, result);
		refresh = 1;
		buffer  += wsize;
	        base    += wsize;
		written += wsize;
		count   -= wsize;
		/*
		 * If we've extended the file, update the inode
		 * now so we don't invalidate the cache.
		 */
		if (base > inode->i_size)
			inode->i_size = base;
	} while (count);

	if (PageError(page))
		ClearPageError(page);

io_error:
	kunmap(page);

	unlock_kernel();
	return written? written : result;
}

static int
nfs_writepage_async(struct file *file, struct inode *inode, struct page *page,
		    unsigned int offset, unsigned int count)
{
	struct nfs_page	*req;
	int		status;

	req = nfs_update_request(file, inode, page, offset, count);
	status = (IS_ERR(req)) ? PTR_ERR(req) : 0;
	if (status < 0)
		goto out;
	nfs_release_request(req);
	nfs_strategy(inode);
 out:
	return status;
}

/*
 * Write an mmapped page to the server.
 */
int
nfs_writepage(struct page *page)
{
	struct inode *inode;
	unsigned long end_index;
	unsigned offset = PAGE_CACHE_SIZE;
	int err;
	struct address_space *mapping = page->mapping;

	if (!mapping)
		BUG();
	inode = mapping->host;
	if (!inode)
		BUG();
	end_index = inode->i_size >> PAGE_CACHE_SHIFT;

	/* Ensure we've flushed out any previous writes */
	nfs_wb_page(inode,page);

	/* easy case */
	if (page->index < end_index)
		goto do_it;
	/* things got complicated... */
	offset = inode->i_size & (PAGE_CACHE_SIZE-1);

	/* OK, are we completely out? */
	err = -EIO;
	if (page->index >= end_index+1 || !offset)
		goto out;
do_it:
	if (!PageError(page) && NFS_SERVER(inode)->rsize >= PAGE_CACHE_SIZE) {
		err = nfs_writepage_async(NULL, inode, page, 0, offset);
		if (err >= 0)
			goto out_ok;
	}
	err = nfs_writepage_sync(NULL, inode, page, 0, offset); 
	if ( err == offset) {
out_ok:
		err = 0;
	}
out:
	UnlockPage(page);
	return err; 
}

/*
 * Check whether the file range we want to write to is locked by
 * us.
 */
static int
region_locked(struct inode *inode, struct nfs_page *req)
{
	struct file_lock	*fl;
	loff_t			rqstart, rqend;

	/* Don't optimize writes if we don't use NLM */
	if (NFS_SERVER(inode)->flags & NFS_MOUNT_NONLM)
		return 0;

	rqstart = page_offset(req->wb_page) + req->wb_offset;
	rqend = rqstart + req->wb_bytes;
	for (fl = inode->i_flock; fl; fl = fl->fl_next) {
		if (fl->fl_owner == current->files && (fl->fl_flags & FL_POSIX)
		    && fl->fl_type == F_WRLCK
		    && fl->fl_start <= rqstart && rqend <= fl->fl_end) {
			return 1;
		}
	}

	return 0;
}

/*
 * Insert a write request into an inode
 */
static inline void
nfs_inode_add_request(struct inode *inode, struct nfs_page *req)
{
	if (!list_empty(&req->wb_hash))
		return;
	if (!NFS_WBACK_BUSY(req))
		printk(KERN_ERR "NFS: unlocked request attempted hashed!\n");
	if (list_empty(&inode->u.nfs_i.writeback))
		atomic_inc(&inode->i_count);
	inode->u.nfs_i.npages++;
	list_add(&req->wb_hash, &inode->u.nfs_i.writeback);
	req->wb_count++;
}

/*
 * Insert a write request into an inode
 */
static inline void
nfs_inode_remove_request(struct nfs_page *req)
{
	struct inode *inode;
	spin_lock(&nfs_wreq_lock);
	if (list_empty(&req->wb_hash)) {
		spin_unlock(&nfs_wreq_lock);
		return;
	}
	if (!NFS_WBACK_BUSY(req))
		printk(KERN_ERR "NFS: unlocked request attempted unhashed!\n");
	inode = req->wb_inode;
	list_del(&req->wb_hash);
	INIT_LIST_HEAD(&req->wb_hash);
	inode->u.nfs_i.npages--;
	if ((inode->u.nfs_i.npages == 0) != list_empty(&inode->u.nfs_i.writeback))
		printk(KERN_ERR "NFS: desynchronized value of nfs_i.npages.\n");
	if (list_empty(&inode->u.nfs_i.writeback))
		iput(inode);
	if (!nfs_have_writebacks(inode) && !nfs_have_read(inode))
		inode_remove_flushd(inode);
	spin_unlock(&nfs_wreq_lock);
	nfs_release_request(req);
}

/*
 * Find a request
 */
static inline struct nfs_page *
_nfs_find_request(struct inode *inode, struct page *page)
{
	struct list_head	*head, *next;

	head = &inode->u.nfs_i.writeback;
	next = head->next;
	while (next != head) {
		struct nfs_page *req = nfs_inode_wb_entry(next);
		next = next->next;
		if (page_index(req->wb_page) != page_index(page))
			continue;
		req->wb_count++;
		return req;
	}
	return NULL;
}

static struct nfs_page *
nfs_find_request(struct inode *inode, struct page *page)
{
	struct nfs_page		*req;

	spin_lock(&nfs_wreq_lock);
	req = _nfs_find_request(inode, page);
	spin_unlock(&nfs_wreq_lock);
	return req;
}

/*
 * Insert a write request into a sorted list
 */
void nfs_list_add_request(struct nfs_page *req, struct list_head *head)
{
	struct list_head *prev;

	if (!list_empty(&req->wb_list)) {
		printk(KERN_ERR "NFS: Add to list failed!\n");
		return;
	}
	if (!NFS_WBACK_BUSY(req))
		printk(KERN_ERR "NFS: unlocked request attempted added to list!\n");
	prev = head->prev;
	while (prev != head) {
		struct nfs_page	*p = nfs_list_entry(prev);
		if (page_index(p->wb_page) < page_index(req->wb_page))
			break;
		prev = prev->prev;
	}
	list_add(&req->wb_list, prev);
	req->wb_list_head = head;
}

/*
 * Insert a write request into an inode
 */
void nfs_list_remove_request(struct nfs_page *req)
{
	if (list_empty(&req->wb_list))
		return;
	if (!NFS_WBACK_BUSY(req))
		printk(KERN_ERR "NFS: unlocked request attempted removed from list!\n");
	list_del(&req->wb_list);
	INIT_LIST_HEAD(&req->wb_list);
	req->wb_list_head = NULL;
}

/*
 * Add a request to the inode's dirty list.
 */
static inline void
nfs_mark_request_dirty(struct nfs_page *req)
{
	struct inode *inode = req->wb_inode;

	spin_lock(&nfs_wreq_lock);
	if (list_empty(&req->wb_list)) {
		nfs_list_add_request(req, &inode->u.nfs_i.dirty);
		inode->u.nfs_i.ndirty++;
	}
	spin_unlock(&nfs_wreq_lock);
	/*
	 * NB: the call to inode_schedule_scan() must lie outside the
	 *     spinlock since it can run flushd().
	 */
	inode_schedule_scan(inode, req->wb_timeout);
}

/*
 * Check if a request is dirty
 */
static inline int
nfs_dirty_request(struct nfs_page *req)
{
	struct inode *inode = req->wb_inode;
	return !list_empty(&req->wb_list) && req->wb_list_head == &inode->u.nfs_i.dirty;
}

#ifdef CONFIG_NFS_V3
/*
 * Add a request to the inode's commit list.
 */
static inline void
nfs_mark_request_commit(struct nfs_page *req)
{
	struct inode *inode = req->wb_inode;

	spin_lock(&nfs_wreq_lock);
	if (list_empty(&req->wb_list)) {
		nfs_list_add_request(req, &inode->u.nfs_i.commit);
		inode->u.nfs_i.ncommit++;
	}
	spin_unlock(&nfs_wreq_lock);
	/*
	 * NB: the call to inode_schedule_scan() must lie outside the
	 *     spinlock since it can run flushd().
	 */
	inode_schedule_scan(inode, req->wb_timeout);
}
#endif

/*
 * Create a write request.
 * Page must be locked by the caller. This makes sure we never create
 * two different requests for the same page, and avoids possible deadlock
 * when we reach the hard limit on the number of dirty pages.
 * It should be safe to sleep here.
 */
struct nfs_page *nfs_create_request(struct file *file, struct inode *inode,
				    struct page *page,
				    unsigned int offset, unsigned int count)
{
	struct nfs_reqlist	*cache = NFS_REQUESTLIST(inode);
	struct nfs_page		*req = NULL;
	long			timeout;

	/* Deal with hard/soft limits.
	 */
	do {
		/* If we're over the global soft limit, wake up all requests */
		if (atomic_read(&nfs_nr_requests) >= MAX_REQUEST_SOFT) {
			dprintk("NFS:      hit soft limit (%d requests)\n",
				atomic_read(&nfs_nr_requests));
			if (!cache->task)
				nfs_reqlist_init(NFS_SERVER(inode));
			nfs_wake_flushd();
		}

		/* If we haven't reached the local hard limit yet,
		 * try to allocate the request struct */
		if (atomic_read(&cache->nr_requests) < MAX_REQUEST_HARD) {
			req = nfs_page_alloc();
			if (req != NULL)
				break;
		}

		/* We're over the hard limit. Wait for better times */
		dprintk("NFS:      create_request sleeping (total %d pid %d)\n",
			atomic_read(&cache->nr_requests), current->pid);

		timeout = 1 * HZ;
		if (NFS_SERVER(inode)->flags & NFS_MOUNT_INTR) {
			interruptible_sleep_on_timeout(&cache->request_wait,
						       timeout);
			if (signalled())
				break;
		} else
			sleep_on_timeout(&cache->request_wait, timeout);

		dprintk("NFS:      create_request waking up (tot %d pid %d)\n",
			atomic_read(&cache->nr_requests), current->pid);
	} while (!req);
	if (!req)
		return NULL;

	/* Initialize the request struct. Initially, we assume a
	 * long write-back delay. This will be adjusted in
	 * update_nfs_request below if the region is not locked. */
	req->wb_page    = page;
	page_cache_get(page);
	req->wb_offset  = offset;
	req->wb_bytes   = count;
	req->wb_file    = file;

	/* If we have a struct file, use its cached credentials
	 * else cache the current process' credentials. */
	if (file) {
		get_file(file);
		req->wb_cred	= nfs_file_cred(file);
	} else
		req->wb_cred = rpcauth_lookupcred(NFS_CLIENT(inode)->cl_auth, 0);
	req->wb_inode   = inode;
	req->wb_count   = 1;

	/* register request's existence */
	atomic_inc(&cache->nr_requests);
	atomic_inc(&nfs_nr_requests);
	return req;
}


/*
 * Release all resources associated with a write request after it
 * has been committed to stable storage
 *
 * Note: Should always be called with the spinlock held!
 */
void
nfs_release_request(struct nfs_page *req)
{
	struct inode		*inode = req->wb_inode;
	struct nfs_reqlist	*cache = NFS_REQUESTLIST(inode);
	struct page		*page = req->wb_page;

	spin_lock(&nfs_wreq_lock);
	if (--req->wb_count) {
		spin_unlock(&nfs_wreq_lock);
		return;
	}
	spin_unlock(&nfs_wreq_lock);

	if (!list_empty(&req->wb_list)) {
		printk(KERN_ERR "NFS: Request released while still on a list!\n");
		nfs_list_remove_request(req);
	}
	if (!list_empty(&req->wb_hash)) {
		printk(KERN_ERR "NFS: Request released while still hashed!\n");
		nfs_inode_remove_request(req);
	}
	if (NFS_WBACK_BUSY(req))
		printk(KERN_ERR "NFS: Request released while still locked!\n");

	/* Release struct file or cached credential */
	if (req->wb_file)
		fput(req->wb_file);
	else
		rpcauth_releasecred(NFS_CLIENT(inode)->cl_auth, req->wb_cred);
	page_cache_release(page);
	nfs_page_free(req);
	/* wake up anyone waiting to allocate a request */
	atomic_dec(&cache->nr_requests);
	atomic_dec(&nfs_nr_requests);
	wake_up(&cache->request_wait);
#ifdef NFS_PARANOIA
	if (atomic_read(&cache->nr_requests) < 0)
		BUG();
	if (atomic_read(&nfs_nr_requests) < 0)
		BUG();
#endif
}

/*
 * Wait for a request to complete.
 *
 * Interruptible by signals only if mounted with intr flag.
 */
static int
nfs_wait_on_request(struct nfs_page *req)
{
	struct inode	*inode = req->wb_inode;
        struct rpc_clnt	*clnt = NFS_CLIENT(inode);

	if (!NFS_WBACK_BUSY(req))
		return 0;
	return nfs_wait_event(clnt, req->wb_wait, !NFS_WBACK_BUSY(req));
}

/*
 * Wait for a request to complete.
 *
 * Interruptible by signals only if mounted with intr flag.
 */
static int
nfs_wait_on_requests(struct inode *inode, struct file *file, unsigned long idx_start, unsigned int npages)
{
	struct list_head	*p, *head;
	unsigned long		idx_end;
	unsigned int		res = 0;
	int			error;

	if (npages == 0)
		idx_end = ~0;
	else
		idx_end = idx_start + npages - 1;

	spin_lock(&nfs_wreq_lock);
	head = &inode->u.nfs_i.writeback;
	p = head->next;
	while (p != head) {
		unsigned long pg_idx;
		struct nfs_page *req = nfs_inode_wb_entry(p);

		p = p->next;

		if (file && req->wb_file != file)
			continue;

		pg_idx = page_index(req->wb_page);
		if (pg_idx < idx_start || pg_idx > idx_end)
			continue;

		if (!NFS_WBACK_BUSY(req))
			continue;
		req->wb_count++;
		spin_unlock(&nfs_wreq_lock);
		error = nfs_wait_on_request(req);
		nfs_release_request(req);
		if (error < 0)
			return error;
		spin_lock(&nfs_wreq_lock);
		p = head->next;
		res++;
	}
	spin_unlock(&nfs_wreq_lock);
	return res;
}

/*
 * Scan cluster for dirty pages and send as many of them to the
 * server as possible.
 */
int nfs_scan_list_timeout(struct list_head *head, struct list_head *dst, struct inode *inode)
{
	struct list_head	*p;
        struct nfs_page		*req;
        int			pages = 0;

	p = head->next;
        while (p != head) {
		req = nfs_list_entry(p);
		p = p->next;
		if (time_after(req->wb_timeout, jiffies)) {
			if (time_after(NFS_NEXTSCAN(inode), req->wb_timeout))
				NFS_NEXTSCAN(inode) = req->wb_timeout;
			continue;
		}
		if (!nfs_lock_request(req))
			continue;
		nfs_list_remove_request(req);
		nfs_list_add_request(req, dst);
		pages++;
	}
	return pages;
}

static int
nfs_scan_dirty_timeout(struct inode *inode, struct list_head *dst)
{
	int	pages;
	spin_lock(&nfs_wreq_lock);
	pages = nfs_scan_list_timeout(&inode->u.nfs_i.dirty, dst, inode);
	inode->u.nfs_i.ndirty -= pages;
	if ((inode->u.nfs_i.ndirty == 0) != list_empty(&inode->u.nfs_i.dirty))
		printk(KERN_ERR "NFS: desynchronized value of nfs_i.ndirty.\n");
	spin_unlock(&nfs_wreq_lock);
	return pages;
}

#ifdef CONFIG_NFS_V3
static int
nfs_scan_commit_timeout(struct inode *inode, struct list_head *dst)
{
	int	pages;
	spin_lock(&nfs_wreq_lock);
	pages = nfs_scan_list_timeout(&inode->u.nfs_i.commit, dst, inode);
	inode->u.nfs_i.ncommit -= pages;
	if ((inode->u.nfs_i.ncommit == 0) != list_empty(&inode->u.nfs_i.commit))
		printk(KERN_ERR "NFS: desynchronized value of nfs_i.ncommit.\n");
	spin_unlock(&nfs_wreq_lock);
	return pages;
}
#endif

int nfs_scan_list(struct list_head *src, struct list_head *dst, struct file *file, unsigned long idx_start, unsigned int npages)
{
	struct list_head	*p;
	struct nfs_page		*req;
	unsigned long		idx_end;
	int			res;

	res = 0;
	if (npages == 0)
		idx_end = ~0;
	else
		idx_end = idx_start + npages - 1;
	p = src->next;
	while (p != src) {
		unsigned long pg_idx;

		req = nfs_list_entry(p);
		p = p->next;

		if (file && req->wb_file != file)
			continue;

		pg_idx = page_index(req->wb_page);
		if (pg_idx < idx_start || pg_idx > idx_end)
			continue;

		if (!nfs_lock_request(req))
			continue;
		nfs_list_remove_request(req);
		nfs_list_add_request(req, dst);
		res++;
	}
	return res;
}

static int
nfs_scan_dirty(struct inode *inode, struct list_head *dst, struct file *file, unsigned long idx_start, unsigned int npages)
{
	int	res;
	spin_lock(&nfs_wreq_lock);
	res = nfs_scan_list(&inode->u.nfs_i.dirty, dst, file, idx_start, npages);
	inode->u.nfs_i.ndirty -= res;
	if ((inode->u.nfs_i.ndirty == 0) != list_empty(&inode->u.nfs_i.dirty))
		printk(KERN_ERR "NFS: desynchronized value of nfs_i.ndirty.\n");
	spin_unlock(&nfs_wreq_lock);
	return res;
}

#ifdef CONFIG_NFS_V3
static int
nfs_scan_commit(struct inode *inode, struct list_head *dst, struct file *file, unsigned long idx_start, unsigned int npages)
{
	int	res;
	spin_lock(&nfs_wreq_lock);
	res = nfs_scan_list(&inode->u.nfs_i.commit, dst, file, idx_start, npages);
	inode->u.nfs_i.ncommit -= res;
	if ((inode->u.nfs_i.ncommit == 0) != list_empty(&inode->u.nfs_i.commit))
		printk(KERN_ERR "NFS: desynchronized value of nfs_i.ncommit.\n");
	spin_unlock(&nfs_wreq_lock);
	return res;
}
#endif


int nfs_coalesce_requests(struct list_head *src, struct list_head *dst, unsigned int maxpages)
{
	struct nfs_page		*req = NULL;
	unsigned int		pages = 0;

	while (!list_empty(src)) {
		struct nfs_page	*prev = req;

		req = nfs_list_entry(src->next);
		if (prev) {
			if (req->wb_file != prev->wb_file)
				break;
			if (page_index(req->wb_page) != page_index(prev->wb_page)+1)
				break;

			if (req->wb_offset != 0)
				break;
		}
		nfs_list_remove_request(req);
		nfs_list_add_request(req, dst);
		pages++;
		if (req->wb_offset + req->wb_bytes != PAGE_CACHE_SIZE)
			break;
		if (pages >= maxpages)
			break;
	}
	return pages;
}

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
		req = _nfs_find_request(inode, page);
		if (req) {
			if (!nfs_lock_request(req)) {
				spin_unlock(&nfs_wreq_lock);
				nfs_wait_on_request(req);
				nfs_release_request(req);
				continue;
			}
			spin_unlock(&nfs_wreq_lock);
			if (new)
				nfs_release_request(new);
			break;
		}

		req = new;
		if (req) {
			nfs_lock_request(req);
			nfs_inode_add_request(inode, req);
			spin_unlock(&nfs_wreq_lock);
			nfs_mark_request_dirty(req);
			break;
		}
		spin_unlock(&nfs_wreq_lock);

		/*
		 * If we're over the soft limit, flush out old requests
		 */
		if (inode->u.nfs_i.npages >= MAX_REQUEST_SOFT)
			nfs_wb_file(inode, file);
		new = nfs_create_request(file, inode, page, offset, bytes);
		if (!new)
			return ERR_PTR(-ENOMEM);
		/* If the region is locked, adjust the timeout */
		if (region_locked(inode, new))
			new->wb_timeout = jiffies + NFS_WRITEBACK_LOCKDELAY;
		else
			new->wb_timeout = jiffies + NFS_WRITEBACK_DELAY;
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
		nfs_release_request(req);
		return ERR_PTR(-EBUSY);
	}

	/* Okay, the request matches. Update the region */
	if (offset < req->wb_offset) {
		req->wb_offset = offset;
		req->wb_bytes = rqend - req->wb_offset;
	}

	if (end > rqend)
		req->wb_bytes = end - req->wb_offset;

	nfs_unlock_request(req);

	return req;
}

/*
 * This is the strategy routine for NFS.
 * It is called by nfs_updatepage whenever the user wrote up to the end
 * of a page.
 *
 * We always try to submit a set of requests in parallel so that the
 * server's write code can gather writes. This is mainly for the benefit
 * of NFSv2.
 *
 * We never submit more requests than we think the remote can handle.
 * For UDP sockets, we make sure we don't exceed the congestion window;
 * for TCP, we limit the number of requests to 8.
 *
 * NFS_STRATEGY_PAGES gives the minimum number of requests for NFSv2 that
 * should be sent out in one go. This is for the benefit of NFSv2 servers
 * that perform write gathering.
 *
 * FIXME: Different servers may have different sweet spots.
 * Record the average congestion window in server struct?
 */
#define NFS_STRATEGY_PAGES      8
static void
nfs_strategy(struct inode *inode)
{
	unsigned int	dirty, wpages;

	dirty  = inode->u.nfs_i.ndirty;
	wpages = NFS_SERVER(inode)->wpages;
#ifdef CONFIG_NFS_V3
	if (NFS_PROTO(inode)->version == 2) {
		if (dirty >= NFS_STRATEGY_PAGES * wpages)
			nfs_flush_file(inode, NULL, 0, 0, 0);
	} else {
		if (dirty >= wpages)
			nfs_flush_file(inode, NULL, 0, 0, 0);
		if (inode->u.nfs_i.ncommit > NFS_STRATEGY_PAGES * wpages &&
		    atomic_read(&nfs_nr_requests) > MAX_REQUEST_SOFT)
			nfs_commit_file(inode, NULL, 0, 0, 0);
	}
#else
	if (dirty >= NFS_STRATEGY_PAGES * wpages)
		nfs_flush_file(inode, NULL, 0, 0, 0);
#endif
	/*
	 * If we're running out of free requests, flush out everything
	 * in order to reduce memory useage...
	 */
	if (inode->u.nfs_i.npages > MAX_REQUEST_SOFT)
		nfs_wb_all(inode);
}

int
nfs_flush_incompatible(struct file *file, struct page *page)
{
	struct inode	*inode = file->f_dentry->d_inode;
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
	req = nfs_find_request(inode,page);
	if (req) {
		if (req->wb_file != file || req->wb_page != page)
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
	struct inode	*inode = dentry->d_inode;
	struct nfs_page	*req;
	int		synchronous = file->f_flags & O_SYNC;
	int		status = 0;

	dprintk("NFS:      nfs_updatepage(%s/%s %d@%Ld)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		count, (long long)(page_offset(page) +offset));

	/*
	 * If wsize is smaller than page size, update and write
	 * page synchronously.
	 */
	if (NFS_SERVER(inode)->wsize < PAGE_SIZE)
		return nfs_writepage_sync(file, inode, page, offset, count);

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

	if (req->wb_bytes == PAGE_CACHE_SIZE)
		SetPageUptodate(page);

	status = 0;
	if (synchronous) {
		int error;

		error = nfs_sync_file(inode, file, page_index(page), 1, FLUSH_SYNC|FLUSH_STABLE);
		if (error < 0 || (error = file->f_error) < 0)
			status = error;
		file->f_error = 0;
	} else {
		/* If we wrote past the end of the page.
		 * Call the strategy routine so it can send out a bunch
		 * of requests.
		 */
		if (req->wb_offset == 0 && req->wb_bytes == PAGE_CACHE_SIZE)
			nfs_strategy(inode);
	}
	nfs_release_request(req);
done:
        dprintk("NFS:      nfs_updatepage returns %d (isize %Ld)\n",
                                                status, (long long)inode->i_size);
	if (status < 0)
		ClearPageUptodate(page);
	return status;
}

/*
 * Set up the argument/result storage required for the RPC call.
 */
static void
nfs_write_rpcsetup(struct list_head *head, struct nfs_write_data *data)
{
	struct nfs_page		*req;
	struct iovec		*iov;
	unsigned int		count;

	/* Set up the RPC argument and reply structs
	 * NB: take care not to mess about with data->commit et al. */

	iov = data->args.iov;
	count = 0;
	while (!list_empty(head)) {
		struct nfs_page *req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_list_add_request(req, &data->pages);
		iov->iov_base = kmap(req->wb_page) + req->wb_offset;
		iov->iov_len = req->wb_bytes;
		count += req->wb_bytes;
		iov++;
		data->args.nriov++;
	}
	req = nfs_list_entry(data->pages.next);
	data->inode = req->wb_inode;
	data->cred = req->wb_cred;
	data->args.fh     = NFS_FH(req->wb_inode);
	data->args.offset = page_offset(req->wb_page) + req->wb_offset;
	data->args.count  = count;
	data->res.fattr   = &data->fattr;
	data->res.count   = count;
	data->res.verf    = &data->verf;
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
	struct rpc_task		*task;
	struct rpc_message	msg;
	int                     flags,
				async = !(how & FLUSH_SYNC),
				stable = (how & FLUSH_STABLE);
	sigset_t		oldset;


	data = nfs_writedata_alloc();
	if (!data)
		goto out_bad;
	task = &data->task;

	/* Set the initial flags for the task.  */
	flags = (async) ? RPC_TASK_ASYNC : 0;

	/* Set up the argument struct */
	nfs_write_rpcsetup(head, data);
	if (stable) {
		if (!inode->u.nfs_i.ncommit)
			data->args.stable = NFS_FILE_SYNC;
		else
			data->args.stable = NFS_DATA_SYNC;
	} else
		data->args.stable = NFS_UNSTABLE;

	/* Finalize the task. */
	rpc_init_task(task, clnt, nfs_writeback_done, flags);
	task->tk_calldata = data;
	/* Release requests */
	task->tk_release = nfs_writedata_release;

#ifdef CONFIG_NFS_V3
	msg.rpc_proc = (NFS_PROTO(inode)->version == 3) ? NFS3PROC_WRITE : NFSPROC_WRITE;
#else
	msg.rpc_proc = NFSPROC_WRITE;
#endif
	msg.rpc_argp = &data->args;
	msg.rpc_resp = &data->res;
	msg.rpc_cred = data->cred;

	dprintk("NFS: %4d initiated write call (req %x/%Ld count %d nriov %d)\n",
		task->tk_pid, 
		inode->i_dev,
		(long long)NFS_FILEID(inode),
		data->args.count, data->args.nriov);

	rpc_clnt_sigmask(clnt, &oldset);
	rpc_call_setup(task, &msg, 0);
	rpc_execute(task);
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

static int
nfs_flush_list(struct inode *inode, struct list_head *head, int how)
{
	LIST_HEAD(one_request);
	struct nfs_page		*req;
	int			error = 0;
	unsigned int		pages = 0,
				wpages = NFS_SERVER(inode)->wpages;

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
static void
nfs_writeback_done(struct rpc_task *task)
{
	struct nfs_write_data	*data = (struct nfs_write_data *) task->tk_calldata;
	struct nfs_writeargs	*argp = &data->args;
	struct nfs_writeres	*resp = &data->res;
	struct inode		*inode = data->inode;
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
#ifdef CONFIG_NFS_V3
	if (resp->verf->committed < argp->stable && task->tk_status >= 0) {
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
			dprintk("NFS: faulty NFSv3 server %s:"
				" (committed = %d) != (stable = %d)\n",
				NFS_SERVER(inode)->hostname,
				resp->verf->committed, argp->stable);
			complain = jiffies + 300 * HZ;
		}
	}
#endif

	/*
	 * Update attributes as result of writeback.
	 * FIXME: There is an inherent race with invalidate_inode_pages and
	 *	  writebacks since the page->count is kept > 1 for as long
	 *	  as the page has a write request pending.
	 */
	nfs_write_attributes(inode, resp->fattr);
	while (!list_empty(&data->pages)) {
		req = nfs_list_entry(data->pages.next);
		nfs_list_remove_request(req);
		page = req->wb_page;

		kunmap(page);

		dprintk("NFS: write (%x/%Ld %d@%Ld)",
			req->wb_inode->i_dev,
			(long long)NFS_FILEID(req->wb_inode),
			req->wb_bytes,
			(long long)(page_offset(page) + req->wb_offset));

		if (task->tk_status < 0) {
			ClearPageUptodate(page);
			SetPageError(page);
			if (req->wb_file)
				req->wb_file->f_error = task->tk_status;
			nfs_inode_remove_request(req);
			dprintk(", error = %d\n", task->tk_status);
			goto next;
		}

#ifdef CONFIG_NFS_V3
		if (resp->verf->committed != NFS_UNSTABLE) {
			nfs_inode_remove_request(req);
			dprintk(" OK\n");
			goto next;
		}
		memcpy(&req->wb_verf, resp->verf, sizeof(req->wb_verf));
		req->wb_timeout = jiffies + NFS_COMMIT_DELAY;
		nfs_mark_request_commit(req);
		dprintk(" marked for commit\n");
#else
		nfs_inode_remove_request(req);
#endif
	next:
		nfs_unlock_request(req);
	}
}


#ifdef CONFIG_NFS_V3
/*
 * Set up the argument/result storage required for the RPC call.
 */
static void
nfs_commit_rpcsetup(struct list_head *head, struct nfs_write_data *data)
{
	struct nfs_page		*first, *last;
	struct inode		*inode;
	loff_t			start, end, len;

	/* Set up the RPC argument and reply structs
	 * NB: take care not to mess about with data->commit et al. */

	list_splice(head, &data->pages);
	INIT_LIST_HEAD(head);
	first = nfs_list_entry(data->pages.next);
	last = nfs_list_entry(data->pages.prev);
	inode = first->wb_inode;

	/*
	 * Determine the offset range of requests in the COMMIT call.
	 * We rely on the fact that data->pages is an ordered list...
	 */
	start = page_offset(first->wb_page) + first->wb_offset;
	end = page_offset(last->wb_page) + (last->wb_offset + last->wb_bytes);
	len = end - start;
	/* If 'len' is not a 32-bit quantity, pass '0' in the COMMIT call */
	if (end >= inode->i_size || len < 0 || len > (~((u32)0) >> 1))
		len = 0;

	data->inode	  = inode;
	data->cred	  = first->wb_cred;
	data->args.fh     = NFS_FH(inode);
	data->args.offset = start;
	data->res.count   = data->args.count = (u32)len;
	data->res.fattr   = &data->fattr;
	data->res.verf    = &data->verf;
}

/*
 * Commit dirty pages
 */
static int
nfs_commit_list(struct list_head *head, int how)
{
	struct rpc_message	msg;
	struct rpc_clnt		*clnt;
	struct nfs_write_data	*data;
	struct rpc_task         *task;
	struct nfs_page         *req;
	int                     flags,
				async = !(how & FLUSH_SYNC);
	sigset_t		oldset;

	data = nfs_writedata_alloc();

	if (!data)
		goto out_bad;
	task = &data->task;

	flags = (async) ? RPC_TASK_ASYNC : 0;

	/* Set up the argument struct */
	nfs_commit_rpcsetup(head, data);
	req = nfs_list_entry(data->pages.next);
	clnt = NFS_CLIENT(req->wb_inode);

	rpc_init_task(task, clnt, nfs_commit_done, flags);
	task->tk_calldata = data;
	/* Release requests */
	task->tk_release = nfs_writedata_release;

	msg.rpc_proc = NFS3PROC_COMMIT;
	msg.rpc_argp = &data->args;
	msg.rpc_resp = &data->res;
	msg.rpc_cred = data->cred;

	dprintk("NFS: %4d initiated commit call\n", task->tk_pid);
	rpc_clnt_sigmask(clnt, &oldset);
	rpc_call_setup(task, &msg, 0);
	rpc_execute(task);
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
static void
nfs_commit_done(struct rpc_task *task)
{
	struct nfs_write_data	*data = (struct nfs_write_data *)task->tk_calldata;
	struct nfs_writeres	*resp = &data->res;
	struct nfs_page		*req;
	struct inode		*inode = data->inode;

        dprintk("NFS: %4d nfs_commit_done (status %d)\n",
                                task->tk_pid, task->tk_status);

	nfs_write_attributes(inode, resp->fattr);
	while (!list_empty(&data->pages)) {
		req = nfs_list_entry(data->pages.next);
		nfs_list_remove_request(req);

		dprintk("NFS: commit (%x/%Ld %d@%Ld)",
			req->wb_inode->i_dev,
			(long long)NFS_FILEID(req->wb_inode),
			req->wb_bytes,
			(long long)(page_offset(req->wb_page) + req->wb_offset));
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
	}
}
#endif

int nfs_flush_file(struct inode *inode, struct file *file, unsigned long idx_start,
		   unsigned int npages, int how)
{
	LIST_HEAD(head);
	int			res,
				error = 0;

	res = nfs_scan_dirty(inode, &head, file, idx_start, npages);
	if (res)
		error = nfs_flush_list(inode, &head, how);
	if (error < 0)
		return error;
	return res;
}

int nfs_flush_timeout(struct inode *inode, int how)
{
	LIST_HEAD(head);
	int			pages,
				error = 0;

	pages = nfs_scan_dirty_timeout(inode, &head);
	if (pages)
		error = nfs_flush_list(inode, &head, how);
	if (error < 0)
		return error;
	return pages;
}

#ifdef CONFIG_NFS_V3
int nfs_commit_file(struct inode *inode, struct file *file, unsigned long idx_start,
		    unsigned int npages, int how)
{
	LIST_HEAD(head);
	int			res,
				error = 0;

	res = nfs_scan_commit(inode, &head, file, idx_start, npages);
	if (res)
		error = nfs_commit_list(&head, how);
	if (error < 0)
		return error;
	return res;
}

int nfs_commit_timeout(struct inode *inode, int how)
{
	LIST_HEAD(head);
	int			pages,
				error = 0;

	pages = nfs_scan_commit_timeout(inode, &head);
	if (pages) {
		pages += nfs_scan_commit(inode, &head, NULL, 0, 0);
		error = nfs_commit_list(&head, how);
	}
	if (error < 0)
		return error;
	return pages;
}
#endif

int nfs_sync_file(struct inode *inode, struct file *file, unsigned long idx_start,
		  unsigned int npages, int how)
{
	int	error,
		wait;

	wait = how & FLUSH_WAIT;
	how &= ~FLUSH_WAIT;

	if (!inode && file)
		inode = file->f_dentry->d_inode;

	do {
		error = 0;
		if (wait)
			error = nfs_wait_on_requests(inode, file, idx_start, npages);
		if (error == 0)
			error = nfs_flush_file(inode, file, idx_start, npages, how);
#ifdef CONFIG_NFS_V3
		if (error == 0)
			error = nfs_commit_file(inode, file, idx_start, npages, how);
#endif
	} while (error > 0);
	return error;
}

int nfs_init_nfspagecache(void)
{
	nfs_page_cachep = kmem_cache_create("nfs_page",
					    sizeof(struct nfs_page),
					    0, SLAB_HWCACHE_ALIGN,
					    NULL, NULL);
	if (nfs_page_cachep == NULL)
		return -ENOMEM;

	nfs_wdata_cachep = kmem_cache_create("nfs_write_data",
					     sizeof(struct nfs_write_data),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL, NULL);
	if (nfs_wdata_cachep == NULL)
		return -ENOMEM;

	return 0;
}

void nfs_destroy_nfspagecache(void)
{
	if (kmem_cache_destroy(nfs_page_cachep))
		printk(KERN_INFO "nfs_page: not all structures were freed\n");
	if (kmem_cache_destroy(nfs_wdata_cachep))
		printk(KERN_INFO "nfs_write_data: not all structures were freed\n");
}


/*
 * linux/fs/nfs/read.c
 *
 * Block I/O for NFS
 *
 * Partial copy of Linus' read cache modifications to fs/nfs/file.c
 * modified for async RPC by okir@monad.swb.de
 *
 * We do an ugly hack here in order to return proper error codes to the
 * user program when a read request failed: since generic_file_read
 * only checks the return value of inode->i_op->readpage() which is always 0
 * for async RPC, we set the error bit of the page to 1 when an error occurs,
 * and make nfs_readpage transmit requests synchronously when encountering this.
 * This is only a small problem, though, since we now retry all operations
 * within the RPC code when root squashing is suspected.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/pagemap.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/nfs_flushd.h>
#include <linux/smp_lock.h>

#include <asm/system.h>

#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

struct nfs_read_data {
	struct rpc_task		task;
	struct inode		*inode;
	struct rpc_cred		*cred;
	struct nfs_readargs	args;	/* XDR argument struct */
	struct nfs_readres	res;	/* ... and result struct */
	struct nfs_fattr	fattr;	/* fattr storage */
	struct list_head	pages;	/* Coalesced read requests */
};

/*
 * Local function declarations
 */
static void	nfs_readpage_result(struct rpc_task *task);

/* Hack for future NFS swap support */
#ifndef IS_SWAPFILE
# define IS_SWAPFILE(inode)	(0)
#endif

static kmem_cache_t *nfs_rdata_cachep;

static __inline__ struct nfs_read_data *nfs_readdata_alloc(void)
{
	struct nfs_read_data   *p;
	p = kmem_cache_alloc(nfs_rdata_cachep, SLAB_NFS);
	if (p) {
		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->pages);
	}
	return p;
}

static __inline__ void nfs_readdata_free(struct nfs_read_data *p)
{
	kmem_cache_free(nfs_rdata_cachep, p);
}

static void nfs_readdata_release(struct rpc_task *task)
{
        struct nfs_read_data   *data = (struct nfs_read_data *)task->tk_calldata;
        nfs_readdata_free(data);
}

/*
 * Read a page synchronously.
 */
static int
nfs_readpage_sync(struct file *file, struct inode *inode, struct page *page)
{
	struct rpc_cred	*cred = NULL;
	struct nfs_fattr fattr;
	loff_t		offset = page_offset(page);
	char		*buffer;
	int		rsize = NFS_SERVER(inode)->rsize;
	int		result;
	int		count = PAGE_CACHE_SIZE;
	int		flags = IS_SWAPFILE(inode)? NFS_RPC_SWAPFLAGS : 0;
	int		eof;

	dprintk("NFS: nfs_readpage_sync(%p)\n", page);

	if (file)
		cred = nfs_file_cred(file);

	/*
	 * This works now because the socket layer never tries to DMA
	 * into this buffer directly.
	 */
	buffer = kmap(page);
	do {
		if (count < rsize)
			rsize = count;

		dprintk("NFS: nfs_proc_read(%s, (%x/%Ld), %Ld, %d, %p)\n",
			NFS_SERVER(inode)->hostname,
			inode->i_dev, (long long)NFS_FILEID(inode),
			(long long)offset, rsize, buffer);

		lock_kernel();
		result = NFS_PROTO(inode)->read(inode, cred, &fattr, flags,
						offset, rsize, buffer, &eof);
		nfs_refresh_inode(inode, &fattr);
		unlock_kernel();

		/*
		 * Even if we had a partial success we can't mark the page
		 * cache valid.
		 */
		if (result < 0) {
			if (result == -EISDIR)
				result = -EINVAL;
			goto io_error;
		}
		count  -= result;
		offset += result;
		buffer += result;
		if (result < rsize)	/* NFSv2ism */
			break;
	} while (count);

	memset(buffer, 0, count);
	flush_dcache_page(page);
	SetPageUptodate(page);
	if (PageError(page))
		ClearPageError(page);
	result = 0;

io_error:
	kunmap(page);
	UnlockPage(page);
	return result;
}

static inline struct nfs_page *
_nfs_find_read(struct inode *inode, struct page *page)
{
	struct list_head	*head, *next;

	head = &inode->u.nfs_i.read;
	next = head->next;
	while (next != head) {
		struct nfs_page *req = nfs_list_entry(next);
		next = next->next;
		if (page_index(req->wb_page) != page_index(page))
			continue;
		req->wb_count++;
		return req;
	}
	return NULL;
}

static struct nfs_page *
nfs_find_read(struct inode *inode, struct page *page)
{
	struct nfs_page *req;
	spin_lock(&nfs_wreq_lock);
	req = _nfs_find_read(inode, page);
	spin_unlock(&nfs_wreq_lock);
	return req;
}

/*
 * Add a request to the inode's asynchronous read list.
 */
static inline void
nfs_mark_request_read(struct nfs_page *req)
{
	struct inode *inode = req->wb_inode;

	spin_lock(&nfs_wreq_lock);
	if (list_empty(&req->wb_list)) {
		nfs_list_add_request(req, &inode->u.nfs_i.read);
		inode->u.nfs_i.nread++;
	}
	spin_unlock(&nfs_wreq_lock);
	/*
	 * NB: the call to inode_schedule_scan() must lie outside the
	 *     spinlock since it can run flushd().
	 */
	inode_schedule_scan(inode, req->wb_timeout);
}

static int
nfs_readpage_async(struct file *file, struct inode *inode, struct page *page)
{
	struct nfs_page	*req, *new = NULL;
	int		result;

	for (;;) {
		result = 0;
		if (Page_Uptodate(page))
			break;

		req = nfs_find_read(inode, page);
		if (req) {
			if (page != req->wb_page) {
				nfs_release_request(req);
				nfs_pagein_inode(inode, page_index(page), 0);
				continue;
			}
			nfs_release_request(req);
			break;
		}

		if (new) {
			nfs_lock_request(new);
			new->wb_timeout = jiffies + NFS_READ_DELAY;
			nfs_mark_request_read(new);
			nfs_unlock_request(new);
			new = NULL;
			break;
		}

		result = -ENOMEM;
		new = nfs_create_request(file, inode, page, 0, PAGE_CACHE_SIZE);
		if (!new)
			break;
	}

	if (inode->u.nfs_i.nread >= NFS_SERVER(inode)->rpages ||
	    page_index(page) == (inode->i_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT)
		nfs_pagein_inode(inode, 0, 0);
	if (new)
		nfs_release_request(new);
	return result;
}

/*
 * Set up the NFS read request struct
 */
static void
nfs_read_rpcsetup(struct list_head *head, struct nfs_read_data *data)
{
	struct nfs_page		*req;
	struct iovec		*iov;
	unsigned int		count;

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
	data->inode	  = req->wb_inode;
	data->cred	  = req->wb_cred;
	data->args.fh     = NFS_FH(req->wb_inode);
	data->args.offset = page_offset(req->wb_page) + req->wb_offset;
	data->args.count  = count;
	data->res.fattr   = &data->fattr;
	data->res.count   = count;
	data->res.eof     = 0;
}

static void
nfs_async_read_error(struct list_head *head)
{
	struct nfs_page	*req;
	struct page	*page;

	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		page = req->wb_page;
		nfs_list_remove_request(req);
		SetPageError(page);
		UnlockPage(page);
		nfs_unlock_request(req);
		nfs_release_request(req);
	}
}

static int
nfs_pagein_one(struct list_head *head, struct inode *inode)
{
	struct rpc_task		*task;
	struct rpc_clnt		*clnt = NFS_CLIENT(inode);
	struct nfs_read_data	*data;
	struct rpc_message	msg;
	int			flags;
	sigset_t		oldset;

	data = nfs_readdata_alloc();
	if (!data)
		goto out_bad;
	task = &data->task;

	/* N.B. Do we need to test? Never called for swapfile inode */
	flags = RPC_TASK_ASYNC | (IS_SWAPFILE(inode)? NFS_RPC_SWAPFLAGS : 0);

	nfs_read_rpcsetup(head, data);

	/* Finalize the task. */
	rpc_init_task(task, clnt, nfs_readpage_result, flags);
	task->tk_calldata = data;
	/* Release requests */
	task->tk_release = nfs_readdata_release;

#ifdef CONFIG_NFS_V3
	msg.rpc_proc = (NFS_PROTO(inode)->version == 3) ? NFS3PROC_READ : NFSPROC_READ;
#else
	msg.rpc_proc = NFSPROC_READ;
#endif
	msg.rpc_argp = &data->args;
	msg.rpc_resp = &data->res;
	msg.rpc_cred = data->cred;

	/* Start the async call */
	dprintk("NFS: %4d initiated read call (req %x/%Ld count %d nriov %d.\n",
		task->tk_pid,
		inode->i_dev, (long long)NFS_FILEID(inode),
		data->args.count, data->args.nriov);

	rpc_clnt_sigmask(clnt, &oldset);
	rpc_call_setup(task, &msg, 0);
	rpc_execute(task);
	rpc_clnt_sigunmask(clnt, &oldset);
	return 0;
out_bad:
	nfs_async_read_error(head);
	return -ENOMEM;
}

static int
nfs_pagein_list(struct inode *inode, struct list_head *head)
{
	LIST_HEAD(one_request);
	struct nfs_page		*req;
	int			error = 0;
	unsigned int		pages = 0,
				rpages = NFS_SERVER(inode)->rpages;

	while (!list_empty(head)) {
		pages += nfs_coalesce_requests(head, &one_request, rpages);
		req = nfs_list_entry(one_request.next);
		error = nfs_pagein_one(&one_request, req->wb_inode);
		if (error < 0)
			break;
	}
	if (error >= 0)
		return pages;

	nfs_async_read_error(head);
	return error;
}

static int
nfs_scan_read_timeout(struct inode *inode, struct list_head *dst)
{
	int	pages;
	spin_lock(&nfs_wreq_lock);
	pages = nfs_scan_list_timeout(&inode->u.nfs_i.read, dst, inode);
	inode->u.nfs_i.nread -= pages;
	if ((inode->u.nfs_i.nread == 0) != list_empty(&inode->u.nfs_i.read))
		printk(KERN_ERR "NFS: desynchronized value of nfs_i.nread.\n");
	spin_unlock(&nfs_wreq_lock);
	return pages;
}

static int
nfs_scan_read(struct inode *inode, struct list_head *dst, unsigned long idx_start, unsigned int npages)
{
	int	res;
	spin_lock(&nfs_wreq_lock);
	res = nfs_scan_list(&inode->u.nfs_i.read, dst, NULL, idx_start, npages);
	inode->u.nfs_i.nread -= res;
	if ((inode->u.nfs_i.nread == 0) != list_empty(&inode->u.nfs_i.read))
		printk(KERN_ERR "NFS: desynchronized value of nfs_i.nread.\n");
	spin_unlock(&nfs_wreq_lock);
	return res;
}

int nfs_pagein_inode(struct inode *inode, unsigned long idx_start,
		     unsigned int npages)
{
	LIST_HEAD(head);
	int	res,
		error = 0;

	res = nfs_scan_read(inode, &head, idx_start, npages);
	if (res)
		error = nfs_pagein_list(inode, &head);
	if (error < 0)
		return error;
	return res;
}

int nfs_pagein_timeout(struct inode *inode)
{
	LIST_HEAD(head);
	int	pages,
		error = 0;

	pages = nfs_scan_read_timeout(inode, &head);
	if (pages)
		error = nfs_pagein_list(inode, &head);
	if (error < 0)
		return error;
	return pages;
}

/*
 * This is the callback from RPC telling us whether a reply was
 * received or some error occurred (timeout or socket shutdown).
 */
static void
nfs_readpage_result(struct rpc_task *task)
{
	struct nfs_read_data	*data = (struct nfs_read_data *) task->tk_calldata;
	struct inode		*inode = data->inode;
	int			count = data->res.count;

	dprintk("NFS: %4d nfs_readpage_result, (status %d)\n",
		task->tk_pid, task->tk_status);

	nfs_refresh_inode(inode, &data->fattr);
	while (!list_empty(&data->pages)) {
		struct nfs_page *req = nfs_list_entry(data->pages.next);
		struct page *page = req->wb_page;
		nfs_list_remove_request(req);

		if (task->tk_status >= 0 && count >= 0) {
			SetPageUptodate(page);
			count -= PAGE_CACHE_SIZE;
		} else
			SetPageError(page);
		flush_dcache_page(page);
		kunmap(page);
		UnlockPage(page);

		dprintk("NFS: read (%x/%Ld %d@%Ld)\n",
                        req->wb_inode->i_dev,
                        (long long)NFS_FILEID(req->wb_inode),
                        req->wb_bytes,
                        (long long)(page_offset(page) + req->wb_offset));
		nfs_unlock_request(req);
		nfs_release_request(req);
	}
}

/*
 * Read a page over NFS.
 * We read the page synchronously in the following cases:
 *  -	The NFS rsize is smaller than PAGE_CACHE_SIZE. We could kludge our way
 *	around this by creating several consecutive read requests, but
 *	that's hardly worth it.
 *  -	The error flag is set for this page. This happens only when a
 *	previous async read operation failed.
 */
int
nfs_readpage(struct file *file, struct page *page)
{
	struct inode *inode;
	int		error;

	if (!file) {
		struct address_space *mapping = page->mapping;
		if (!mapping)
			BUG();
		inode = mapping->host;
	} else
		inode = file->f_dentry->d_inode;
	if (!inode)
		BUG();

	dprintk("NFS: nfs_readpage (%p %ld@%lu)\n",
		page, PAGE_CACHE_SIZE, page->index);
	/*
	 * Try to flush any pending writes to the file..
	 *
	 * NOTE! Because we own the page lock, there cannot
	 * be any new pending writes generated at this point
	 * for this page (other pages can be written to).
	 */
	error = nfs_wb_page(inode, page);
	if (error)
		goto out_error;

	error = -1;
	if (!PageError(page) && NFS_SERVER(inode)->rsize >= PAGE_CACHE_SIZE)
		error = nfs_readpage_async(file, inode, page);
	if (error >= 0)
		goto out;

	error = nfs_readpage_sync(file, inode, page);
	if (error < 0 && IS_SWAPFILE(inode))
		printk("Aiee.. nfs swap-in of page failed!\n");
out:
	return error;

out_error:
	UnlockPage(page);
	goto out;
}

int nfs_init_readpagecache(void)
{
	nfs_rdata_cachep = kmem_cache_create("nfs_read_data",
					     sizeof(struct nfs_read_data),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL, NULL);
	if (nfs_rdata_cachep == NULL)
		return -ENOMEM;

	return 0;
}

void nfs_destroy_readpagecache(void)
{
	if (kmem_cache_destroy(nfs_rdata_cachep))
		printk(KERN_INFO "nfs_read_data: not all structures were freed\n");
}

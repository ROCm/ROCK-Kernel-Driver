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
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/mempool.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/smp_lock.h>

#include <asm/system.h>

#define NFSDBG_FACILITY		NFSDBG_PAGECACHE

static int nfs_pagein_one(struct list_head *, struct inode *);

static kmem_cache_t *nfs_rdata_cachep;
static mempool_t *nfs_rdata_mempool;

#define MIN_POOL_READ	(32)

static __inline__ struct nfs_read_data *nfs_readdata_alloc(void)
{
	struct nfs_read_data   *p;
	p = (struct nfs_read_data *)mempool_alloc(nfs_rdata_mempool, SLAB_NOFS);
	if (p) {
		memset(p, 0, sizeof(*p));
		INIT_LIST_HEAD(&p->pages);
	}
	return p;
}

static __inline__ void nfs_readdata_free(struct nfs_read_data *p)
{
	mempool_free(p, nfs_rdata_mempool);
}

void nfs_readdata_release(struct rpc_task *task)
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
	unsigned int	rsize = NFS_SERVER(inode)->rsize;
	unsigned int	count = PAGE_CACHE_SIZE;
	int		result;
	struct nfs_read_data	rdata = {
		.flags		= (IS_SWAPFILE(inode)? NFS_RPC_SWAPFLAGS : 0),
		.cred		= NULL,
		.inode		= inode,
		.args		= {
			.fh		= NFS_FH(inode),
			.pages		= &page,
			.pgbase		= 0UL,
			.count		= rsize,
		},
		.res		= {
			.fattr		= &rdata.fattr,
		}
	};

	dprintk("NFS: nfs_readpage_sync(%p)\n", page);

	/*
	 * This works now because the socket layer never tries to DMA
	 * into this buffer directly.
	 */
	do {
		if (count < rsize)
			rdata.args.count = count;
		rdata.res.count = rdata.args.count;
		rdata.args.offset = page_offset(page) + rdata.args.pgbase;

		dprintk("NFS: nfs_proc_read(%s, (%s/%Ld), %Lu, %u)\n",
			NFS_SERVER(inode)->hostname,
			inode->i_sb->s_id,
			(long long)NFS_FILEID(inode),
			(unsigned long long)rdata.args.pgbase,
			rdata.args.count);

		lock_kernel();
		result = NFS_PROTO(inode)->read(&rdata, file);
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
		count -= result;
		rdata.args.pgbase += result;
		if (result < rdata.args.count)	/* NFSv2ism */
			break;
	} while (count);

	if (count)
		memclear_highpage_flush(page, rdata.args.pgbase, count);
	SetPageUptodate(page);
	if (PageError(page))
		ClearPageError(page);
	result = 0;

io_error:
	unlock_page(page);
	return result;
}

static int
nfs_readpage_async(struct file *file, struct inode *inode, struct page *page)
{
	LIST_HEAD(one_request);
	struct nfs_page	*new;

	new = nfs_create_request(file, inode, page, 0, PAGE_CACHE_SIZE);
	if (IS_ERR(new)) {
		unlock_page(page);
		return PTR_ERR(new);
	}
	nfs_lock_request(new);
	nfs_list_add_request(new, &one_request);
	nfs_pagein_one(&one_request, inode);
	return 0;
}

/*
 * Set up the NFS read request struct
 */
static void
nfs_read_rpcsetup(struct list_head *head, struct nfs_read_data *data)
{
	struct inode		*inode;
	struct nfs_page		*req;
	struct page		**pages;
	unsigned int		count;

	pages = data->pagevec;
	count = 0;
	while (!list_empty(head)) {
		req = nfs_list_entry(head->next);
		nfs_list_remove_request(req);
		nfs_list_add_request(req, &data->pages);
		*pages++ = req->wb_page;
		count += req->wb_bytes;
	}
	req = nfs_list_entry(data->pages.next);
	data->inode	  = inode = req->wb_inode;
	data->cred	  = req->wb_cred;

	NFS_PROTO(inode)->read_setup(data, count);

	dprintk("NFS: %4d initiated read call (req %s/%Ld, %u bytes @ offset %Lu.\n",
			data->task.tk_pid,
			inode->i_sb->s_id,
			(long long)NFS_FILEID(inode),
			count,
			(unsigned long long)req_offset(req));
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
		unlock_page(page);
		nfs_clear_request(req);
		nfs_release_request(req);
		nfs_unlock_request(req);
	}
}

static int
nfs_pagein_one(struct list_head *head, struct inode *inode)
{
	struct rpc_clnt		*clnt = NFS_CLIENT(inode);
	struct nfs_read_data	*data;
	sigset_t		oldset;

	data = nfs_readdata_alloc();
	if (!data)
		goto out_bad;

	nfs_read_rpcsetup(head, data);

	/* Start the async call */
	rpc_clnt_sigmask(clnt, &oldset);
	lock_kernel();
	rpc_execute(&data->task);
	unlock_kernel();
	rpc_clnt_sigunmask(clnt, &oldset);
	return 0;
out_bad:
	nfs_async_read_error(head);
	return -ENOMEM;
}

int
nfs_pagein_list(struct list_head *head, int rpages)
{
	LIST_HEAD(one_request);
	struct nfs_page		*req;
	int			error = 0;
	unsigned int		pages = 0;

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

/*
 * This is the callback from RPC telling us whether a reply was
 * received or some error occurred (timeout or socket shutdown).
 */
void
nfs_readpage_result(struct rpc_task *task)
{
	struct nfs_read_data *data = (struct nfs_read_data *)task->tk_calldata;
	unsigned int count = data->res.count;

	dprintk("NFS: %4d nfs_readpage_result, (status %d)\n",
		task->tk_pid, task->tk_status);

	while (!list_empty(&data->pages)) {
		struct nfs_page *req = nfs_list_entry(data->pages.next);
		struct page *page = req->wb_page;
		nfs_list_remove_request(req);

		if (task->tk_status >= 0) {
			if (count < PAGE_CACHE_SIZE) {
				memclear_highpage_flush(page,
							req->wb_pgbase + count,
							req->wb_bytes - count);

				count = 0;
			} else
				count -= PAGE_CACHE_SIZE;
			SetPageUptodate(page);
		} else
			SetPageError(page);
		unlock_page(page);

		dprintk("NFS: read (%s/%Ld %d@%Ld)\n",
                        req->wb_inode->i_sb->s_id,
                        (long long)NFS_FILEID(req->wb_inode),
                        req->wb_bytes,
                        (long long)req_offset(req));
		nfs_clear_request(req);
		nfs_release_request(req);
		nfs_unlock_request(req);
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
	struct inode *inode = page->mapping->host;
	int		error;

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

	if (!PageError(page) && NFS_SERVER(inode)->rsize >= PAGE_CACHE_SIZE) {
		error = nfs_readpage_async(file, inode, page);
		goto out;
	}

	error = nfs_readpage_sync(file, inode, page);
	if (error < 0 && IS_SWAPFILE(inode))
		printk("Aiee.. nfs swap-in of page failed!\n");
out:
	return error;

out_error:
	unlock_page(page);
	goto out;
}

struct nfs_readdesc {
	struct list_head *head;
	struct file *filp;
};

static int
readpage_sync_filler(void *data, struct page *page)
{
	struct nfs_readdesc *desc = (struct nfs_readdesc *)data;
	return nfs_readpage_sync(desc->filp, page->mapping->host, page);
}

static int
readpage_async_filler(void *data, struct page *page)
{
	struct nfs_readdesc *desc = (struct nfs_readdesc *)data;
	struct inode *inode = page->mapping->host;
	struct nfs_page *new;

	nfs_wb_page(inode, page);
	new = nfs_create_request(desc->filp, inode, page, 0, PAGE_CACHE_SIZE);
	if (IS_ERR(new)) {
			SetPageError(page);
			unlock_page(page);
			return PTR_ERR(new);
	}
	nfs_lock_request(new);
	nfs_list_add_request(new, desc->head);
	return 0;
}

int
nfs_readpages(struct file *filp, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	LIST_HEAD(head);
	struct nfs_readdesc desc = {
		.filp		= filp,
		.head		= &head,
	};
	struct nfs_server *server = NFS_SERVER(mapping->host);
	int is_sync = server->rsize < PAGE_CACHE_SIZE;
	int ret;

	ret = read_cache_pages(mapping, pages,
			       is_sync ? readpage_sync_filler :
					 readpage_async_filler,
			       &desc);
	if (!list_empty(&head)) {
		int err = nfs_pagein_list(&head, server->rpages);
		if (!ret)
			ret = err;
	}
	return ret;
}

int nfs_init_readpagecache(void)
{
	nfs_rdata_cachep = kmem_cache_create("nfs_read_data",
					     sizeof(struct nfs_read_data),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL, NULL);
	if (nfs_rdata_cachep == NULL)
		return -ENOMEM;

	nfs_rdata_mempool = mempool_create(MIN_POOL_READ,
					   mempool_alloc_slab,
					   mempool_free_slab,
					   nfs_rdata_cachep);
	if (nfs_rdata_mempool == NULL)
		return -ENOMEM;

	return 0;
}

void nfs_destroy_readpagecache(void)
{
	mempool_destroy(nfs_rdata_mempool);
	if (kmem_cache_destroy(nfs_rdata_cachep))
		printk(KERN_INFO "nfs_read_data: not all structures were freed\n");
}

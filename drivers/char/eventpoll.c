/*
 *  drivers/char/eventpoll.c ( Efficent event polling implementation )
 *  Copyright (C) 2001,...,2002  Davide Libenzi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/random.h>
#include <linux/smp_lock.h>
#include <linux/wrapper.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/fcblist.h>
#include <linux/rwsem.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/mman.h>
#include <asm/atomic.h>
#include <linux/eventpoll.h>



#define EVENTPOLLFS_MAGIC 0x03111965 /* My birthday should work for this :) */

#define DEBUG_EPOLL 0

#if DEBUG_EPOLL > 0
#define DPRINTK(x) printk x
#define DNPRINTK(n, x) do { if ((n) <= DEBUG_EPOLL) printk x; } while (0)
#else /* #if DEBUG_EPOLL > 0 */
#define DPRINTK(x) (void) 0
#define DNPRINTK(n, x) (void) 0
#endif /* #if DEBUG_EPOLL > 0 */

#define DEBUG_DPI 0

#if DEBUG_DPI != 0
#define DPI_SLAB_DEBUG (SLAB_DEBUG_FREE | SLAB_RED_ZONE /* | SLAB_POISON */)
#else /* #if DEBUG_DPI != 0 */
#define DPI_SLAB_DEBUG 0
#endif /* #if DEBUG_DPI != 0 */

#define INITIAL_HASH_BITS 7
#define MAX_HASH_BITS 18
#define RESIZE_LENGTH 2

#define DPI_MEM_ALLOC()	(struct epitem *) kmem_cache_alloc(dpi_cache, SLAB_KERNEL)
#define DPI_MEM_FREE(p) kmem_cache_free(dpi_cache, p)
#define IS_FILE_EPOLL(f) ((f)->f_op == &eventpoll_fops)


/*
 * Type used for versioning events snapshots inside the double buffer.
 */
typedef unsigned long long event_version_t;

/*
 * This structure is stored inside the "private_data" member of the file
 * structure and rapresent the main data sructure for the eventpoll
 * interface.
 */
struct eventpoll {
	/*
	 * Protect the evenpoll interface from sys_epoll_ctl(2), ioctl(EP_POLL)
	 * and ->write() concurrency. It basically serialize the add/remove/edit
	 * of items in the interest set.
	 */
	struct rw_semaphore acsem;

	/*
	 * Protect the this structure access. When the "acsem" is acquired
	 * togheter with this one, "acsem" should be acquired first. Or,
	 * "lock" nests inside "acsem".
	 */
	rwlock_t lock;

	/* Wait queue used by sys_epoll_wait() and ioctl(EP_POLL) */
	wait_queue_head_t wq;

	/* Wait queue used by file->poll() */
	wait_queue_head_t poll_wait;

	/* This is the hash used to store the "struct epitem" elements */
	struct list_head *hash;

	unsigned int hbits;
	unsigned int hmask;
	atomic_t hents;
	atomic_t resize;

	/* Number of pages currently allocated in each side of the double buffer */
	int numpages;

	/*
	 * Current page set pointer, switched from "pages0" and "pages1" each time
	 * ep_poll() returns events to the caller.
	 */
	char **pages;

	/* Each one of these contains the pages allocated for each side of
	 * the double buffer.
	 */
	char *pages0[MAX_EVENTPOLL_PAGES];
	char *pages1[MAX_EVENTPOLL_PAGES];

	/*
	 * Variable containing the vma base address where the double buffer
	 * pages are mapped onto.
	 */
	unsigned long vmabase;

	/*
	 * Certain functions cannot be called if the double buffer pages are
	 * not allocated and if the memory mapping is not in place. This tells
	 * us that everything is setup to fully use the interface.
	 */
	atomic_t mmapped;

	/* Number of events currently available inside the current snapshot */
	int eventcnt;

	/*
	 * Variable storing the current "version" of the snapshot. It is used
	 * to validate the validity of the current slot pointed by the "index"
	 * member of a "struct epitem".
	 */
	event_version_t ver;
};

/*
 * Each file descriptor added to the eventpoll interface will
 * have an entry of this type linked to the hash.
 */
struct epitem {
	/* List header used to link this structure to the eventpoll hash */
	struct list_head llink;

	/* The "container" of this item */
	struct eventpoll *ep;

	/* The file this item refers to */
	struct file *file;

	/* The structure that describe the interested events and the source fd */
	struct pollfd pfd;

	/*
	 * The index inside the current double buffer that stores the active
	 * event slot for this item ( file ).
	 */
	int index;

	/*
	 * The version that is used to validate if the current slot is still
	 * valid or if it refers to an old snapshot. It is matches togheter
	 * with the one inside the eventpoll structure.
	 */
	event_version_t ver;
};




static int ep_getfd(int *efd, struct inode **einode, struct file **efile);
static int ep_alloc_pages(char **pages, int numpages);
static int ep_free_pages(char **pages, int numpages);
static int ep_init(struct eventpoll *ep);
static void ep_free(struct eventpoll *ep);
static struct epitem *ep_find_nl(struct eventpoll *ep, int fd);
static struct epitem *ep_find(struct eventpoll *ep, int fd);
static int ep_hashresize(struct eventpoll *ep, unsigned long *kflags);
static int ep_insert(struct eventpoll *ep, struct pollfd *pfd);
static int ep_remove(struct eventpoll *ep, struct epitem *dpi);
static void notify_proc(struct file *file, void *data, unsigned long *local,
			long *event);
static int open_eventpoll(struct inode *inode, struct file *file);
static int close_eventpoll(struct inode *inode, struct file *file);
static unsigned int poll_eventpoll(struct file *file, poll_table *wait);
static int write_eventpoll(struct file *file, const char *buffer, size_t count,
			   loff_t *ppos);
static int ep_poll(struct eventpoll *ep, struct evpoll *dvp);
static int ep_do_alloc_pages(struct eventpoll *ep, int numpages);
static int ioctl_eventpoll(struct inode *inode, struct file *file,
			   unsigned int cmd, unsigned long arg);
static void eventpoll_mm_open(struct vm_area_struct * vma);
static void eventpoll_mm_close(struct vm_area_struct * vma);
static int mmap_eventpoll(struct file *file, struct vm_area_struct *vma);
static int eventpollfs_delete_dentry(struct dentry *dentry);
static struct inode *get_eventpoll_inode(void);
static struct super_block *eventpollfs_get_sb(struct file_system_type *fs_type,
					      int flags, char *dev_name, void *data);



/* Slab cache used to allocate "struct epitem" */
static kmem_cache_t *dpi_cache;

/* Virtual fs used to allocate inodes for eventpoll files */
static struct vfsmount *eventpoll_mnt;

/* File callbacks that implement the eventpoll file behaviour */
static struct file_operations eventpoll_fops = {
	.write		= write_eventpoll,
	.ioctl		= ioctl_eventpoll,
	.mmap		= mmap_eventpoll,
	.open		= open_eventpoll,
	.release	= close_eventpoll,
	.poll		= poll_eventpoll
};

/* Memory mapping callbacks for the eventpoll file */
static struct vm_operations_struct eventpoll_mmap_ops = {
	.open		= eventpoll_mm_open,
	.close		= eventpoll_mm_close,
};

/*
 * The "struct miscdevice" is used to register the eventpoll device
 * to make it suitable to be openend from a /dev file.
 */
static struct miscdevice eventpoll_miscdev = {
	EVENTPOLL_MINOR, "eventpoll", &eventpoll_fops
};

/*
 * This is used to register the virtual file system from where
 * eventpoll inodes are allocated.
 */
static struct file_system_type eventpoll_fs_type = {
	.name		= "eventpollfs",
	.get_sb		= eventpollfs_get_sb,
	.kill_sb	= kill_anon_super,
};

/* Very basic directory entry operations for the eventpoll virtual file system */
static struct dentry_operations eventpollfs_dentry_operations = {
	.d_delete	= eventpollfs_delete_dentry,
};



/*
 * It opens an eventpoll file descriptor by allocating space for "maxfds"
 * file descriptors. It is the kernel part of the userspace epoll_create(2).
 */
asmlinkage int sys_epoll_create(int maxfds)
{
	int error = -EINVAL, fd;
	unsigned long addr;
	struct inode *inode;
	struct file *file;
	struct eventpoll *ep;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_create(%d)\n",
		     current, maxfds));

	/*
	 * It is not possible to store more than MAX_FDS_IN_EVENTPOLL file
	 * descriptors inside the eventpoll interface.
	 */
	if (maxfds > MAX_FDS_IN_EVENTPOLL)
		goto eexit_1;

	/*
	 * Creates all the items needed to setup an eventpoll file. That is,
	 * a file structure, and inode and a free file descriptor.
	 */
	error = ep_getfd(&fd, &inode, &file);
	if (error)
		goto eexit_1;

	/*
	 * Calls the code to initialize the eventpoll file. This code is
	 * the same as the "open" file operation callback because inside
	 * ep_getfd() we did what the kernel usually does before invoking
	 * corresponding file "open" callback.
	 */
	error = open_eventpoll(inode, file);
	if (error)
		goto eexit_2;

	/* The "private_data" member is setup by open_eventpoll() */
	ep = file->private_data;

	/* Alloc pages for the event double buffer */
	error = ep_do_alloc_pages(ep, EP_FDS_PAGES(maxfds + 1));
	if (error)
		goto eexit_2;

	/*
	 * Create a user space mapping of the event double buffer to
	 * avoid kernel to user space memory copy when returning events
	 * to the caller.
	 */
	down_write(&current->mm->mmap_sem);
	addr = do_mmap_pgoff(file, 0, EP_MAP_SIZE(maxfds + 1), PROT_READ,
			     MAP_PRIVATE, 0);
	up_write(&current->mm->mmap_sem);
	error = PTR_ERR((void *) addr);
	if (IS_ERR((void *) addr))
		goto eexit_2;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_create(%d) = %d\n",
		     current, maxfds, fd));

	return fd;

eexit_2:
	sys_close(fd);
eexit_1:
	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_create(%d) = %d\n",
		     current, maxfds, error));
	return error;
}


/*
 * The following function implement the controller interface for the eventpoll
 * file that enable the insertion/removal/change of file descriptors inside
 * the interest set. It rapresents the kernel part of the user spcae epoll_ctl(2).
 */
asmlinkage int sys_epoll_ctl(int epfd, int op, int fd, unsigned int events)
{
	int error = -EBADF;
	struct file *file;
	struct eventpoll *ep;
	struct epitem *dpi;
	struct pollfd pfd;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_ctl(%d, %d, %d, %u)\n",
		     current, epfd, op, fd, events));

	file = fget(epfd);
	if (!file)
		goto eexit_1;

	/*
	 * We have to check that the file structure underneath the file descriptor
	 * the user passed to us _is_ an eventpoll file.
	 */
	error = -EINVAL;
	if (!IS_FILE_EPOLL(file))
		goto eexit_2;

	/*
	 * At this point it is safe to assume that the "private_data" contains
	 * our own data structure.
	 */
	ep = file->private_data;

	down_write(&ep->acsem);

	pfd.fd = fd;
	pfd.events = events | POLLERR | POLLHUP;
	pfd.revents = 0;

	dpi = ep_find(ep, fd);

	error = -EINVAL;
	switch (op) {
	case EP_CTL_ADD:
		if (!dpi)
			error = ep_insert(ep, &pfd);
		else
			error = -EEXIST;
		break;
	case EP_CTL_DEL:
		if (dpi)
			error = ep_remove(ep, dpi);
		else
			error = -ENOENT;
		break;
	case EP_CTL_MOD:
		if (dpi) {
			dpi->pfd.events = events;
			error = 0;
		} else
			error = -ENOENT;
		break;
	}

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_ctl(%d, %d, %d, %u) = %d\n",
		     current, epfd, op, fd, events, error));

	up_write(&ep->acsem);

eexit_2:
	fput(file);
eexit_1:
	return error;
}


/*
 * Implement the event wait interface for the eventpoll file. It is the kernel
 * part of the user space epoll_wait(2).
 */
asmlinkage int sys_epoll_wait(int epfd, struct pollfd const **events, int timeout)
{
	int error = -EBADF;
	void *eaddr;
	struct file *file;
	struct eventpoll *ep;
	struct evpoll dvp;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_wait(%d, %p, %d)\n",
		     current, epfd, events, timeout));

	file = fget(epfd);
	if (!file)
		goto eexit_1;

	/*
	 * We have to check that the file structure underneath the file descriptor
	 * the user passed to us _is_ an eventpoll file.
	 */
	error = -EINVAL;
	if (!IS_FILE_EPOLL(file))
		goto eexit_2;

	/*
	 * At this point it is safe to assume that the "private_data" contains
	 * our own data structure.
	 */
	ep = file->private_data;

	/*
	 * It is possible that the user created an eventpoll file by open()ing
	 * the corresponding /dev/ file and he did not perform the correct
	 * initialization required by the old /dev/epoll interface. This test
	 * protect us from this scenario.
	 */
	error = -EINVAL;
	if (!atomic_read(&ep->mmapped))
		goto eexit_2;

	dvp.ep_timeout = timeout;
	error = ep_poll(ep, &dvp);
	if (error > 0) {
		eaddr = (void *) (ep->vmabase + dvp.ep_resoff);
		if (copy_to_user(events, &eaddr, sizeof(struct pollfd *)))
			error = -EFAULT;
	}

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_wait(%d, %p, %d) = %d\n",
		     current, epfd, events, timeout, error));

eexit_2:
	fput(file);
eexit_1:
	return error;
}


/*
 * Creates the file descriptor to be used by the epoll interface.
 */
static int ep_getfd(int *efd, struct inode **einode, struct file **efile)
{
	struct qstr this;
	char name[32];
	struct dentry *dentry;
	struct inode *inode;
	struct file *file;
	int error, fd;

	/* Get an ready to use file */
	error = -ENFILE;
	file = get_empty_filp();
	if (!file)
		goto eexit_1;

	/* Allocates an inode from the eventpoll file system */
	inode = get_eventpoll_inode();
	error = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto eexit_2;

	/* Allocates a free descriptor to plug the file onto */
	error = get_unused_fd();
	if (error < 0)
		goto eexit_3;
	fd = error;

	/*
	 * Link the inode to a directory entry by creating a unique name
	 * using the inode number.
	 */
	error = -ENOMEM;
	sprintf(name, "[%lu]", inode->i_ino);
	this.name = name;
	this.len = strlen(name);
	this.hash = inode->i_ino;
	dentry = d_alloc(eventpoll_mnt->mnt_sb->s_root, &this);
	if (!dentry)
		goto eexit_4;
	dentry->d_op = &eventpollfs_dentry_operations;
	d_add(dentry, inode);
	file->f_vfsmnt = mntget(eventpoll_mnt);
	file->f_dentry = dget(dentry);

	/*
	 * Initialize the file as read/write because it could be used
	 * with write() to add/remove/change interest sets.
	 */
	file->f_pos = 0;
	file->f_flags = O_RDWR;
	file->f_op = &eventpoll_fops;
	file->f_mode = FMODE_READ | FMODE_WRITE;
	file->f_version = 0;
	file->private_data = NULL;

	/* Install the new setup file into the allocated fd. */
	fd_install(fd, file);

	*efd = fd;
	*einode = inode;
	*efile = file;
	return 0;

eexit_4:
	put_unused_fd(fd);
eexit_3:
	iput(inode);
eexit_2:
	put_filp(file);
eexit_1:
	return error;
}


static int ep_alloc_pages(char **pages, int numpages)
{
	int ii;

	for (ii = 0; ii < numpages; ii++) {
		pages[ii] = (char *) __get_free_pages(GFP_KERNEL, 0);
		if (!pages[ii]) {
			for (--ii; ii >= 0; ii--) {
				ClearPageReserved(virt_to_page(pages[ii]));
				free_pages((unsigned long) pages[ii], 0);
			}
			return -ENOMEM;
		}
		SetPageReserved(virt_to_page(pages[ii]));
	}
	return 0;
}


static int ep_free_pages(char **pages, int numpages)
{
	int ii;

	for (ii = 0; ii < numpages; ii++) {
		ClearPageReserved(virt_to_page(pages[ii]));
		free_pages((unsigned long) pages[ii], 0);
	}
	return 0;
}


static int ep_init(struct eventpoll *ep)
{
	int ii, hentries;

	init_rwsem(&ep->acsem);
	rwlock_init(&ep->lock);
	init_waitqueue_head(&ep->wq);
	init_waitqueue_head(&ep->poll_wait);
	ep->hbits = INITIAL_HASH_BITS;
	ep->hmask = (1 << ep->hbits) - 1;
	atomic_set(&ep->hents, 0);
	atomic_set(&ep->resize, 0);
	atomic_set(&ep->mmapped, 0);
	ep->numpages = 0;
	ep->vmabase = 0;
	ep->pages = ep->pages0;
	ep->eventcnt = 0;
	ep->ver = 1;

	hentries = ep->hmask + 1;
	if (!(ep->hash = (struct list_head *) vmalloc(hentries * sizeof(struct list_head))))
		return -ENOMEM;

	for (ii = 0; ii < hentries; ii++)
		INIT_LIST_HEAD(&ep->hash[ii]);

	return 0;
}


static void ep_free(struct eventpoll *ep)
{
	int ii;
	struct list_head *lsthead;

	/*
	 * Walks through the whole hash by unregistering file callbacks and
	 * freeing each "struct epitem".
	 */
	for (ii = 0; ii <= ep->hmask; ii++) {
		lsthead = &ep->hash[ii];
		while (!list_empty(lsthead)) {
			struct epitem *dpi = list_entry(lsthead->next, struct epitem, llink);

			file_notify_delcb(dpi->file, notify_proc);
			list_del(lsthead->next);
			DPI_MEM_FREE(dpi);
		}
	}
	/*
	 * At this point we can free the hash and the pages used for the event
	 * double buffer. The ep_free() function is called from the "close"
	 * file operations callback, and this garanties us that the pages are
	 * already unmapped.
	 */
	vfree(ep->hash);
	if (ep->numpages > 0) {
		ep_free_pages(ep->pages0, ep->numpages);
		ep_free_pages(ep->pages1, ep->numpages);
	}
}


/*
 * No lock version of ep_find(), used when the code had to acquire the lock
 * before calling the function.
 */
static struct epitem *ep_find_nl(struct eventpoll *ep, int fd)
{
	struct epitem *dpi = NULL;
	struct list_head *lsthead, *lnk;

	lsthead = &ep->hash[fd & ep->hmask];
	list_for_each(lnk, lsthead) {
		dpi = list_entry(lnk, struct epitem, llink);

		if (dpi->pfd.fd == fd) break;
		dpi = NULL;
	}

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ep_find(%d) -> %p\n",
		     current, fd, dpi));

	return dpi;
}


static struct epitem *ep_find(struct eventpoll *ep, int fd)
{
	struct epitem *dpi;
	unsigned long flags;

	read_lock_irqsave(&ep->lock, flags);

	dpi = ep_find_nl(ep, fd);

	read_unlock_irqrestore(&ep->lock, flags);

	return dpi;
}


static int ep_hashresize(struct eventpoll *ep, unsigned long *kflags)
{
	struct list_head *hash, *oldhash;
	unsigned int hbits = ep->hbits + 1;
	unsigned int hmask = (1 << hbits) - 1;
	int ii, res, hentries = hmask + 1;
	unsigned long flags = *kflags;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ep_hashresize(%p) bits=%u\n",
		     current, ep, hbits));

	write_unlock_irqrestore(&ep->lock, flags);

	res = -ENOMEM;
	if (!(hash = (struct list_head *) vmalloc(hentries * sizeof(struct list_head)))) {
		write_lock_irqsave(&ep->lock, flags);
		goto eexit_1;
	}

	for (ii = 0; ii < hentries; ii++)
		INIT_LIST_HEAD(&hash[ii]);

	write_lock_irqsave(&ep->lock, flags);

	oldhash = ep->hash;
	for (ii = 0; ii <= ep->hmask; ii++) {
		struct list_head *oldhead = &oldhash[ii], *lnk;

		while (!list_empty(oldhead)) {
			struct epitem *dpi = list_entry(lnk = oldhead->next, struct epitem, llink);

			list_del(lnk);
			list_add(lnk, &hash[dpi->pfd.fd & hmask]);
		}
	}

	ep->hash = hash;
	ep->hbits = hbits;
	ep->hmask = hmask;

	write_unlock_irqrestore(&ep->lock, flags);
	vfree(oldhash);
	write_lock_irqsave(&ep->lock, flags);

	res = 0;
eexit_1:
	*kflags = flags;
	atomic_dec(&ep->resize);
	return res;
}


static int ep_insert(struct eventpoll *ep, struct pollfd *pfd)
{
	int error;
	struct epitem *dpi;
	struct file *file;
	unsigned long flags;

	if (atomic_read(&ep->hents) >= (ep->numpages * POLLFD_X_PAGE))
		return -E2BIG;

	file = fget(pfd->fd);
	if (!file)
		return -EBADF;

	error = -ENOMEM;
	if (!(dpi = DPI_MEM_ALLOC()))
		goto eexit_1;

	INIT_LIST_HEAD(&dpi->llink);
	dpi->ep = ep;
	dpi->file = file;
	dpi->pfd = *pfd;
	dpi->index = -1;
	dpi->ver = ep->ver - 1;

	write_lock_irqsave(&ep->lock, flags);

	list_add(&dpi->llink, &ep->hash[pfd->fd & ep->hmask]);
	atomic_inc(&ep->hents);

	if (!atomic_read(&ep->resize) &&
	    (atomic_read(&ep->hents) >> ep->hbits) > RESIZE_LENGTH &&
	    ep->hbits < MAX_HASH_BITS) {
		atomic_inc(&ep->resize);
		ep_hashresize(ep, &flags);
	}

	write_unlock_irqrestore(&ep->lock, flags);

	file_notify_addcb(file, notify_proc, dpi);

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ep_insert(%p, %d)\n",
		     current, ep, pfd->fd));

	error = 0;
eexit_1:
	fput(file);

	return error;
}


/*
 * Removes a "struct epitem" from the eventpoll hash and deallocates
 * all the associated resources.
 */
static int ep_remove(struct eventpoll *ep, struct epitem *dpi)
{
	unsigned long flags;
	struct pollfd *pfd, *lpfd;
	struct epitem *ldpi;

	/* First, removes the callback from the file callback list */
	file_notify_delcb(dpi->file, notify_proc);

	write_lock_irqsave(&ep->lock, flags);

	list_del(&dpi->llink);
	atomic_dec(&ep->hents);

	/*
	 * This is to remove stale events. We don't want that the removed file
	 * has a pending event that might be associated with a file inserted
	 * at a later time inside the eventpoll interface. this code checks
	 * if the currently removed file has a valid pending event and, if it does,
	 * manages things to remove it and decrement the currently available
	 * event count.
	 */
	if (dpi->index >= 0 && dpi->ver == ep->ver && dpi->index < ep->eventcnt) {
		pfd = (struct pollfd *) (ep->pages[EVENT_PAGE_INDEX(dpi->index)] +
					 EVENT_PAGE_OFFSET(dpi->index));
		if (pfd->fd == dpi->pfd.fd && dpi->index < --ep->eventcnt) {
			lpfd = (struct pollfd *) (ep->pages[EVENT_PAGE_INDEX(ep->eventcnt)] +
						  EVENT_PAGE_OFFSET(ep->eventcnt));
			*pfd = *lpfd;

			if ((ldpi = ep_find_nl(ep, pfd->fd))) ldpi->index = dpi->index;
		}
	}

	write_unlock_irqrestore(&ep->lock, flags);

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ep_remove(%p, %d)\n",
		     current, ep, dpi->pfd.fd));

	/* At this point it is safe to free the eventpoll item */
	DPI_MEM_FREE(dpi);

	return 0;
}


/*
 * This is the event notify callback that is called from fs/fcblist.c because
 * of the registration ( file_notify_addcb() ) done in ep_insert().
 */
static void notify_proc(struct file *file, void *data, unsigned long *local,
			long *event)
{
	struct epitem *dpi = data;
	struct eventpoll *ep = dpi->ep;
	struct pollfd *pfd;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: notify(%p, %p, %ld, %ld) ep=%p\n",
		     current, file, data, event[0], event[1], ep));

	/*
	 * We don't need to disable IRQs here because the callback dispatch
	 * routine inside fs/fcblist.c already call us with disabled IRQ.
	 */
	write_lock(&ep->lock);

	/* We're not expecting any of those events. Jump out soon ... */
	if (!(dpi->pfd.events & event[1]))
		goto out;

	/*
	 * This logic determins if an active even slot is available for the
	 * currently signaled file, or if we have to make space for a new one
	 * and increment the number of ready file descriptors ( ep->eventcnt ).
	 */
	if (dpi->index < 0 || dpi->ver != ep->ver) {
		if (ep->eventcnt >= (ep->numpages * POLLFD_X_PAGE))
			goto out;
		dpi->index = ep->eventcnt++;
		dpi->ver = ep->ver;
		pfd = (struct pollfd *) (ep->pages[EVENT_PAGE_INDEX(dpi->index)] +
					 EVENT_PAGE_OFFSET(dpi->index));
		*pfd = dpi->pfd;
	} else {
		pfd = (struct pollfd *) (ep->pages[EVENT_PAGE_INDEX(dpi->index)] +
					 EVENT_PAGE_OFFSET(dpi->index));
		if (pfd->fd != dpi->pfd.fd) {
			if (ep->eventcnt >= (ep->numpages * POLLFD_X_PAGE))
				goto out;
			dpi->index = ep->eventcnt++;
			pfd = (struct pollfd *) (ep->pages[EVENT_PAGE_INDEX(dpi->index)] +
						 EVENT_PAGE_OFFSET(dpi->index));
			*pfd = dpi->pfd;
		}
	}

	/*
	 * Merge event bits into the corresponding event slot inside the
	 * double buffer.
	 */
	pfd->revents |= (pfd->events & event[1]);

	/*
	 * Wake up ( if active ) both the eventpoll wait list and the ->poll()
	 * wait list.
	 */
	if (waitqueue_active(&ep->wq))
		wake_up(&ep->wq);
	if (waitqueue_active(&ep->poll_wait))
		wake_up(&ep->poll_wait);
out:
	write_unlock(&ep->lock);
}


static int open_eventpoll(struct inode *inode, struct file *file)
{
	int res;
	struct eventpoll *ep;

	if (!(ep = kmalloc(sizeof(struct eventpoll), GFP_KERNEL)))
		return -ENOMEM;

	memset(ep, 0, sizeof(*ep));
	if ((res = ep_init(ep))) {
		kfree(ep);
		return res;
	}

	file->private_data = ep;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: open() ep=%p\n", current, ep));
	return 0;
}


static int close_eventpoll(struct inode *inode, struct file *file)
{
	struct eventpoll *ep = file->private_data;

	if (ep) {
		ep_free(ep);
		kfree(ep);
	}

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: close() ep=%p\n", current, ep));
	return 0;
}


static unsigned int poll_eventpoll(struct file *file, poll_table *wait)
{
	struct eventpoll *ep = file->private_data;

	poll_wait(file, &ep->poll_wait, wait);
	if (ep->eventcnt)
		return POLLIN | POLLRDNORM;

	return 0;
}


static int write_eventpoll(struct file *file, const char *buffer, size_t count,
			   loff_t *ppos)
{
	int rcount;
	struct eventpoll *ep = file->private_data;
	struct epitem *dpi;
	struct pollfd pfd;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: write(%p, %d)\n", current, ep, count));

	/* The size of the write must be a multiple of sizeof(struct pollfd) */
	rcount = -EINVAL;
	if (count % sizeof(struct pollfd))
		goto eexit_1;

	/*
	 * And we have also to verify that that area is correctly accessible
	 * for the user.
	 */
	if ((rcount = verify_area(VERIFY_READ, buffer, count)))
		goto eexit_1;

	down_write(&ep->acsem);

	rcount = 0;

	while (count > 0) {
		if (__copy_from_user(&pfd, buffer, sizeof(pfd))) {
			rcount = -EFAULT;
			goto eexit_2;
		}

		dpi = ep_find(ep, pfd.fd);

		if (pfd.fd >= current->files->max_fds || !current->files->fd[pfd.fd])
			pfd.events = POLLREMOVE;
		if (pfd.events & POLLREMOVE) {
			if (dpi) {
				ep_remove(ep, dpi);
				rcount += sizeof(pfd);
			}
		}
		else if (dpi) {
			dpi->pfd.events = pfd.events;
			rcount += sizeof(pfd);
		} else {
			pfd.revents = 0;
			if (!ep_insert(ep, &pfd))
				rcount += sizeof(pfd);
		}

		buffer += sizeof(pfd);
		count -= sizeof(pfd);
	}

eexit_2:
	up_write(&ep->acsem);
eexit_1:
	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: write(%p, %d) = %d\n",
		     current, ep, count, rcount));

	return rcount;
}


static int ep_poll(struct eventpoll *ep, struct evpoll *dvp)
{
	int res = 0;
	long timeout;
	unsigned long flags;
	wait_queue_t wait;

	/*
	 * We don't want ep_poll() to be called if the correct sequence
	 * of operations are performed to initialize it. This won't happen
	 * for the system call interface but it could happen using the
	 * old /dev/epoll interface, that is maintained for compatibility.
	 */
	if (!atomic_read(&ep->mmapped))
		return -EINVAL;

	write_lock_irqsave(&ep->lock, flags);

	res = 0;
	if (!ep->eventcnt) {
		/*
		 * We don't have any available event to return to the caller.
		 * We need to sleep here, and we will be wake up by
		 * notify_proc() when events will become available.
		 */
		init_waitqueue_entry(&wait, current);
		add_wait_queue(&ep->wq, &wait);

		/*
		 * Calculate the timeout by checking for the "infinite" value ( -1 )
		 * and the overflow condition ( > MAX_SCHEDULE_TIMEOUT / HZ ). The
		 * passed timeout is in milliseconds, that why (t * HZ) / 1000.
		 */
		timeout = dvp->ep_timeout == -1 || dvp->ep_timeout > MAX_SCHEDULE_TIMEOUT / HZ ?
			MAX_SCHEDULE_TIMEOUT: (dvp->ep_timeout * HZ) / 1000;

		for (;;) {
			/*
			 * We don't want to sleep if the notify_proc() sends us
			 * a wakeup in between. That's why we set the task state
			 * to TASK_INTERRUPTIBLE before doing the checks.
			 */
			set_current_state(TASK_INTERRUPTIBLE);
			if (ep->eventcnt || !timeout)
				break;
			if (signal_pending(current)) {
				res = -EINTR;
				break;
			}

			write_unlock_irqrestore(&ep->lock, flags);
			timeout = schedule_timeout(timeout);
			write_lock_irqsave(&ep->lock, flags);
		}
		remove_wait_queue(&ep->wq, &wait);

		set_current_state(TASK_RUNNING);
	}

	/*
	 * If we've been wake up because of events became available, we need to:
	 *
	 * 1) null the number of available ready file descriptors
	 * 2) increment the version of the current ( next ) snapshot
	 * 3) swap the double buffer to return the current one to the caller
	 * 4) set the current ( for the user, previous for the interface ) offset
	 */
	if (!res && ep->eventcnt) {
		res = ep->eventcnt;
		ep->eventcnt = 0;
		++ep->ver;
		if (ep->pages == ep->pages0) {
			ep->pages = ep->pages1;
			dvp->ep_resoff = 0;
		} else {
			ep->pages = ep->pages0;
			dvp->ep_resoff = ep->numpages * PAGE_SIZE;
		}
	}

	write_unlock_irqrestore(&ep->lock, flags);

	return res;
}


static int ep_do_alloc_pages(struct eventpoll *ep, int numpages)
{
	int res, pgalloc, pgcpy;
	unsigned long flags;
	char **pages, **pages0, **pages1;

	if (atomic_read(&ep->mmapped))
		return -EBUSY;
	if (numpages > MAX_EVENTPOLL_PAGES)
		return -EINVAL;

	pgalloc = numpages - ep->numpages;
	if ((pages = (char **) vmalloc(2 * (pgalloc + 1) * sizeof(char *))) == NULL)
		return -ENOMEM;
	pages0 = &pages[0];
	pages1 = &pages[pgalloc + 1];

	if ((res = ep_alloc_pages(pages0, pgalloc)))
		goto eexit_1;

	if ((res = ep_alloc_pages(pages1, pgalloc))) {
		ep_free_pages(pages0, pgalloc);
		goto eexit_1;
	}

	write_lock_irqsave(&ep->lock, flags);
	pgcpy = (ep->numpages + pgalloc) > numpages ? numpages - ep->numpages: pgalloc;
	if (pgcpy > 0) {
		memcpy(&ep->pages0[ep->numpages], pages0, pgcpy * sizeof(char *));
		memcpy(&ep->pages1[ep->numpages], pages1, pgcpy * sizeof(char *));
		ep->numpages += pgcpy;
	}
	write_unlock_irqrestore(&ep->lock, flags);

	if (pgcpy < pgalloc) {
		if (pgcpy < 0)
			pgcpy = 0;
		ep_free_pages(&pages0[pgcpy], pgalloc - pgcpy);
		ep_free_pages(&pages1[pgcpy], pgalloc - pgcpy);
	}

eexit_1:
	vfree(pages);
	return res;
}


static int ioctl_eventpoll(struct inode *inode, struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	int res;
	struct eventpoll *ep = file->private_data;
	struct epitem *dpi;
	unsigned long flags;
	struct pollfd pfd;
	struct evpoll dvp;

	switch (cmd) {
	case EP_ALLOC:
		res = ep_do_alloc_pages(ep, EP_FDS_PAGES(arg));

		DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ioctl(%p, EP_ALLOC, %lu) == %d\n",
			     current, ep, arg, res));
		return res;

	case EP_FREE:
		if (atomic_read(&ep->mmapped))
			return -EBUSY;

		res = -EINVAL;
		write_lock_irqsave(&ep->lock, flags);
		if (ep->numpages > 0) {
			ep_free_pages(ep->pages0, ep->numpages);
			ep_free_pages(ep->pages1, ep->numpages);
			ep->numpages = 0;
			ep->pages = ep->pages0;
			res = 0;
		}
		write_unlock_irqrestore(&ep->lock, flags);

		DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ioctl(%p, EP_FREE) == %d\n",
			     current, ep, res));
		return res;

	case EP_POLL:
		if (copy_from_user(&dvp, (void *) arg, sizeof(struct evpoll)))
			return -EFAULT;

		DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ioctl(%p, EP_POLL, %d)\n",
			     current, ep, dvp.ep_timeout));

		res = ep_poll(ep, &dvp);

		DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ioctl(%p, EP_POLL, %d) == %d\n",
			     current, ep, dvp.ep_timeout, res));

		if (res > 0 && copy_to_user((void *) arg, &dvp, sizeof(struct evpoll)))
			res = -EFAULT;

		return res;

	case EP_ISPOLLED:
		if (copy_from_user(&pfd, (void *) arg, sizeof(struct pollfd)))
			return 0;

		read_lock_irqsave(&ep->lock, flags);

		res = 0;
		if (!(dpi = ep_find_nl(ep, pfd.fd)))
			goto is_not_polled;

		pfd = dpi->pfd;
		res = 1;

	is_not_polled:
		read_unlock_irqrestore(&ep->lock, flags);

		if (res)
			copy_to_user((void *) arg, &pfd, sizeof(struct pollfd));

		DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ioctl(%p, EP_ISPOLLED, %d) == %d\n",
			     current, ep, pfd.fd, res));
		return res;
	}

	return -EINVAL;
}


static void eventpoll_mm_open(struct vm_area_struct * vma)
{
	struct file *file = vma->vm_file;
	struct eventpoll *ep = file->private_data;

	if (ep) atomic_inc(&ep->mmapped);

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: mm_open(%p)\n", current, ep));
}


static void eventpoll_mm_close(struct vm_area_struct * vma)
{
	struct file *file = vma->vm_file;
	struct eventpoll *ep = file->private_data;

	if (ep && atomic_dec_and_test(&ep->mmapped))
		ep->vmabase = 0;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: mm_close(%p)\n", current, ep));
}


static int mmap_eventpoll(struct file *file, struct vm_area_struct *vma)
{
	struct eventpoll *ep = file->private_data;
	unsigned long start;
	int ii, res, numpages;
	size_t mapsize;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: mmap(%p, %lx, %lx)\n",
		     current, ep, vma->vm_start, vma->vm_pgoff << PAGE_SHIFT));

	/*
	 * We need the eventpoll file to be RW but we don't want it to be
	 * mapped RW. This test perform the test and reject RW mmaping.
	 */
	if (vma->vm_flags & VM_WRITE)
		return -EACCES;

	if ((vma->vm_pgoff << PAGE_SHIFT) != 0)
		return -EINVAL;

	/*
	 * We need to verify that the mapped area covers all the allocated
	 * double buffer.
	 */
	mapsize = PAGE_ALIGN(vma->vm_end - vma->vm_start);
	numpages = mapsize >> PAGE_SHIFT;

	res = -EINVAL;
	if (numpages != (2 * ep->numpages))
		goto eexit_1;

	/*
	 * Map the double buffer starting from "vma->vm_start" up to
	 * "vma->vm_start + ep->numpages * PAGE_SIZE".
	 */
	start = vma->vm_start;
	for (ii = 0; ii < ep->numpages; ii++) {
		if ((res = remap_page_range(vma, start, __pa(ep->pages0[ii]),
					    PAGE_SIZE, vma->vm_page_prot)))
			goto eexit_1;
		start += PAGE_SIZE;
	}
	for (ii = 0; ii < ep->numpages; ii++) {
		if ((res = remap_page_range(vma, start, __pa(ep->pages1[ii]),
					    PAGE_SIZE, vma->vm_page_prot)))
			goto eexit_1;
		start += PAGE_SIZE;
	}
	vma->vm_ops = &eventpoll_mmap_ops;

	/* Saves the base mapping address for later use in sys_epoll_wait(2) */
	ep->vmabase = vma->vm_start;

	/*
	 * Ok, mapping has been done. We can open the door to functions that
	 * requires the mapping to be in place.
	 */
	atomic_set(&ep->mmapped, 1);

	res = 0;
eexit_1:

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: mmap(%p, %lx, %lx) == %d\n",
		     current, ep, vma->vm_start, vma->vm_pgoff << PAGE_SHIFT, res));
	return res;
}


static int eventpollfs_delete_dentry(struct dentry *dentry)
{

	return 1;
}


static struct inode *get_eventpoll_inode(void)
{
	int error = -ENOMEM;
	struct inode *inode = new_inode(eventpoll_mnt->mnt_sb);

	if (!inode)
		goto eexit_1;

	inode->i_fop = &eventpoll_fops;

	/*
	 * Mark the inode dirty from the very beginning,
	 * that way it will never be moved to the dirty
	 * list because "mark_inode_dirty()" will think
	 * that it already _is_ on the dirty list.
	 */
	inode->i_state = I_DIRTY;
	inode->i_mode = S_IRUSR | S_IWUSR;
	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_blksize = PAGE_SIZE;
	return inode;

eexit_1:
	return ERR_PTR(error);
}


static struct super_block *eventpollfs_get_sb(struct file_system_type *fs_type,
					      int flags, char *dev_name, void *data)
{

	return get_sb_pseudo(fs_type, "eventpoll:", NULL, EVENTPOLLFS_MAGIC);
}


static int __init eventpoll_init(void)
{
	int error;

	/* Allocates slab cache used to allocate "struct epitem" items */
	error = -ENOMEM;
	dpi_cache = kmem_cache_create("eventpoll",
				      sizeof(struct epitem),
				      __alignof__(struct epitem),
				      DPI_SLAB_DEBUG, NULL, NULL);
	if (!dpi_cache)
		goto eexit_1;

	/*
	 * Register the virtual file system that will be the source of inodes
	 * for the eventpoll files
	 */
	error = register_filesystem(&eventpoll_fs_type);
	if (error)
		goto eexit_2;

	/* Mount the above commented virtual file system */
	eventpoll_mnt = kern_mount(&eventpoll_fs_type);
	error = PTR_ERR(eventpoll_mnt);
	if (IS_ERR(eventpoll_mnt))
		goto eexit_3;

	/*
	 * This is to maintain compatibility with the old /dev/epoll interface.
	 * We need to register a misc device so that the caller can open(2) it
	 * through a file inside /dev.
	 */
	error = misc_register(&eventpoll_miscdev);
	if (error)
		goto eexit_4;

	printk(KERN_INFO "[%p] eventpoll: driver installed.\n", current);

	return error;

eexit_4:
	mntput(eventpoll_mnt);
eexit_3:
	unregister_filesystem(&eventpoll_fs_type);
eexit_2:
	kmem_cache_destroy(dpi_cache);
eexit_1:

	return error;
}


static void __exit eventpoll_exit(void)
{
	/* Undo all operations done inside eventpoll_init() */
	unregister_filesystem(&eventpoll_fs_type);
	mntput(eventpoll_mnt);
	misc_deregister(&eventpoll_miscdev);
	kmem_cache_destroy(dpi_cache);
}

module_init(eventpoll_init);
module_exit(eventpoll_exit);

MODULE_LICENSE("GPL");


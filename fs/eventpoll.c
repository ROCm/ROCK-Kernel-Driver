/*
 *  drivers/char/eventpoll.c ( Efficent event polling implementation )
 *  Copyright (C) 2001,...,2002	 Davide Libenzi
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
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/smp_lock.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
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


/* Maximum size of the hash in bits ( 2^N ) */
#define EP_MAX_HASH_BITS 17

/* Minimum size of the hash in bits ( 2^N ) */
#define EP_MIN_HASH_BITS 9

/*
 * Event buffer dimension used to cache events before sending them in
 * userspace with a __copy_to_user(). The event buffer is in stack,
 * so keep this size fairly small.
 */
#define EP_EVENT_BUFF_SIZE 32

/* Maximum number of wait queue we can attach to */
#define EP_MAX_POLL_QUEUE 2

/* Number of hash entries ( "struct list_head" ) inside a page */
#define EP_HENTRY_X_PAGE (PAGE_SIZE / sizeof(struct list_head))

/* Maximum size of the hash in pages */
#define EP_MAX_HPAGES ((1 << EP_MAX_HASH_BITS) / EP_HENTRY_X_PAGE + 1)

/* Macro to allocate a "struct epitem" from the slab cache */
#define DPI_MEM_ALLOC()	(struct epitem *) kmem_cache_alloc(dpi_cache, SLAB_KERNEL)

/* Macro to free a "struct epitem" to the slab cache */
#define DPI_MEM_FREE(p) kmem_cache_free(dpi_cache, p)

/* Fast test to see if the file is an evenpoll file */
#define IS_FILE_EPOLL(f) ((f)->f_op == &eventpoll_fops)

/*
 * Remove the item from the list and perform its initialization.
 * This is usefull for us because we can test if the item is linked
 * using "EP_IS_LINKED(p)".
 */
#define EP_LIST_DEL(p) do { list_del(p); INIT_LIST_HEAD(p); } while (0)

/* Tells us if the item is currently linked */
#define EP_IS_LINKED(p) (!list_empty(p))

/* Get the "struct epitem" from a wait queue pointer */
#define EP_ITEM_FROM_WAIT(p) ((struct epitem *) container_of(p, struct eppoll_entry, wait)->base)





/*
 * This structure is stored inside the "private_data" member of the file
 * structure and rapresent the main data sructure for the eventpoll
 * interface.
 */
struct eventpoll {
	/* Used to link to the "struct eventpoll" list ( eplist ) */
	struct list_head llink;

	/* Protect the this structure access */
	rwlock_t lock;

	/* Wait queue used by sys_epoll_wait() */
	wait_queue_head_t wq;

	/* Wait queue used by file->poll() */
	wait_queue_head_t poll_wait;

	/* List of ready file descriptors */
	struct list_head rdllist;

	/* Size of the hash */
	unsigned int hashbits;

	/* Number of pages currently allocated for the hash */
	int nhpages;

	/* Pages for the "struct epitem" hash */
	char *hpages[EP_MAX_HPAGES];
};

/* Wait structure used by the poll hooks */
struct eppoll_entry {
	/* The "base" pointer is set to the container "struct epitem" */
	void *base;

	/*
	 * Wait queue item that will be linked to the target file wait
	 * queue head.
	 */
	wait_queue_t wait;

	/* The wait queue head that linked the "wait" wait queue item */
	wait_queue_head_t *whead;
};

/*
 * Each file descriptor added to the eventpoll interface will
 * have an entry of this type linked to the hash.
 */
struct epitem {
	/* List header used to link this structure to the eventpoll hash */
	struct list_head llink;

	/* List header used to link this structure to the eventpoll ready list */
	struct list_head rdllink;

	/* Number of active wait queue attached to poll operations */
	int nwait;

	/* Wait queue used to attach poll operations */
	struct eppoll_entry wait[EP_MAX_POLL_QUEUE];

	/* The "container" of this item */
	struct eventpoll *ep;

	/* The file this item refers to */
	struct file *file;

	/* The structure that describe the interested events and the source fd */
	struct pollfd pfd;

	/*
	 * Used to keep track of the usage count of the structure. This avoids
	 * that the structure will desappear from underneath our processing.
	 */
	atomic_t usecnt;
};



static unsigned int ep_get_hash_bits(unsigned int hintsize);
static int ep_getfd(int *efd, struct inode **einode, struct file **efile);
static int ep_alloc_pages(char **pages, int numpages);
static int ep_free_pages(char **pages, int numpages);
static int ep_file_init(struct file *file, unsigned int hashbits);
static unsigned int ep_hash_index(struct eventpoll *ep, struct file *file);
static struct list_head *ep_hash_entry(struct eventpoll *ep, unsigned int index);
static int ep_init(struct eventpoll *ep, unsigned int hashbits);
static void ep_free(struct eventpoll *ep);
static struct epitem *ep_find(struct eventpoll *ep, struct file *file);
static void ep_use_epitem(struct epitem *dpi);
static void ep_release_epitem(struct epitem *dpi);
static void ep_ptable_queue_proc(void *priv, wait_queue_head_t *whead);
static int ep_insert(struct eventpoll *ep, struct pollfd *pfd, struct file *tfile);
static int ep_modify(struct eventpoll *ep, struct epitem *dpi, unsigned int events);
static int ep_unlink(struct eventpoll *ep, struct epitem *dpi);
static int ep_remove(struct eventpoll *ep, struct epitem *dpi);
static int ep_poll_callback(wait_queue_t *wait, unsigned mode, int sync);
static int ep_eventpoll_close(struct inode *inode, struct file *file);
static unsigned int ep_eventpoll_poll(struct file *file, poll_table *wait);
static int ep_events_transfer(struct eventpoll *ep, struct pollfd *events, int maxevents);
static int ep_poll(struct eventpoll *ep, struct pollfd *events, int maxevents,
		   int timeout);
static int eventpollfs_delete_dentry(struct dentry *dentry);
static struct inode *ep_eventpoll_inode(void);
static struct super_block *eventpollfs_get_sb(struct file_system_type *fs_type,
					      int flags, char *dev_name, void *data);



/* Use to link togheter all the "struct eventpoll" */
static struct list_head eplist;

/* Serialize the access to "eplist" */
static rwlock_t eplock;

/* Slab cache used to allocate "struct epitem" */
static kmem_cache_t *dpi_cache;

/* Virtual fs used to allocate inodes for eventpoll files */
static struct vfsmount *eventpoll_mnt;

/* File callbacks that implement the eventpoll file behaviour */
static struct file_operations eventpoll_fops = {
	.release	= ep_eventpoll_close,
	.poll		= ep_eventpoll_poll
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
 * Calculate the size of the hash in bits. The returned size will be
 * bounded between EP_MIN_HASH_BITS and EP_MAX_HASH_BITS.
 */
static unsigned int ep_get_hash_bits(unsigned int hintsize)
{
	unsigned int i, val;

	for (i = 0, val = 1; val < hintsize && i < 8 * sizeof(int); i++, val <<= 1);
	return i <  EP_MIN_HASH_BITS ?  EP_MIN_HASH_BITS:
		(i > EP_MAX_HASH_BITS ? EP_MAX_HASH_BITS: i);
}


/*
 * This is called from inside fs/file_table.c:__fput() to unlink files
 * from the eventpoll interface. We need to have this facility to cleanup
 * correctly files that are closed without being removed from the eventpoll
 * interface.
 */
void ep_notify_file_close(struct file *file)
{
	unsigned long flags;
	struct list_head *lnk;
	struct eventpoll *ep;
	struct epitem *dpi;

	read_lock_irqsave(&eplock, flags);
	list_for_each(lnk, &eplist) {
		ep = list_entry(lnk, struct eventpoll, llink);

		while ((dpi = ep_find(ep, file))) {
			ep_remove(ep, dpi);
			ep_release_epitem(dpi);
		}
	}
	read_unlock_irqrestore(&eplock, flags);
}


/*
 * It opens an eventpoll file descriptor by suggesting a storage of "size"
 * file descriptors. The size parameter is just an hint about how to size
 * data structures. It won't prevent the user to store more than "size"
 * file descriptors inside the epoll interface. It is the kernel part of
 * the userspace epoll_create(2).
 */
asmlinkage int sys_epoll_create(int size)
{
	int error, fd;
	unsigned int hashbits;
	struct inode *inode;
	struct file *file;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_create(%d)\n",
		     current, size));

	/* Correctly size the hash */
	hashbits = ep_get_hash_bits((unsigned int) size);

	/*
	 * Creates all the items needed to setup an eventpoll file. That is,
	 * a file structure, and inode and a free file descriptor.
	 */
	error = ep_getfd(&fd, &inode, &file);
	if (error)
		goto eexit_1;

	/* Setup the file internal data structure ( "struct eventpoll" ) */
	error = ep_file_init(file, hashbits);
	if (error)
		goto eexit_2;


	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_create(%d) = %d\n",
		     current, size, fd));

	return fd;

eexit_2:
	sys_close(fd);
eexit_1:
	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_create(%d) = %d\n",
		     current, size, error));
	return error;
}


/*
 * The following function implement the controller interface for the eventpoll
 * file that enable the insertion/removal/change of file descriptors inside
 * the interest set. It rapresents the kernel part of the user spcae epoll_ctl(2).
 */
asmlinkage int sys_epoll_ctl(int epfd, int op, int fd, unsigned int events)
{
	int error;
	struct file *file, *tfile;
	struct eventpoll *ep;
	struct epitem *dpi;
	struct pollfd pfd;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_ctl(%d, %d, %d, %u)\n",
		     current, epfd, op, fd, events));

	/* Get the "struct file *" for the eventpoll file */
	error = -EBADF;
	file = fget(epfd);
	if (!file)
		goto eexit_1;

	/* Get the "struct file *" for the target file */
	tfile = fget(fd);
	if (!tfile)
		goto eexit_2;

	/* The target file descriptor must support poll */
	error = -EPERM;
	if (!tfile->f_op || !tfile->f_op->poll)
		goto eexit_3;

	/*
	 * We have to check that the file structure underneath the file descriptor
	 * the user passed to us _is_ an eventpoll file.
	 */
	error = -EINVAL;
	if (!IS_FILE_EPOLL(file))
		goto eexit_3;

	/*
	 * At this point it is safe to assume that the "private_data" contains
	 * our own data structure.
	 */
	ep = file->private_data;

	/*
	 * Try to lookup the file inside our hash table. When an item is found
	 * ep_find() increases the usage count of the item so that it won't
	 * desappear underneath us. The only thing that might happen, if someone
	 * tries very hard, is a double insertion of the same file descriptor.
	 * This does not rapresent a problem though and we don't really want
	 * to put an extra syncronization object to deal with this harmless condition.
	 */
	dpi = ep_find(ep, tfile);

	error = -EINVAL;
	switch (op) {
	case EP_CTL_ADD:
		if (!dpi) {
			pfd.fd = fd;
			pfd.events = events | POLLERR | POLLHUP;
			pfd.revents = 0;

			error = ep_insert(ep, &pfd, tfile);
		} else
			error = -EEXIST;
		break;
	case EP_CTL_DEL:
		if (dpi)
			error = ep_remove(ep, dpi);
		else
			error = -ENOENT;
		break;
	case EP_CTL_MOD:
		if (dpi)
			error = ep_modify(ep, dpi, events | POLLERR | POLLHUP);
		else
			error = -ENOENT;
		break;
	}

	/*
	 * The function ep_find() increments the usage count of the structure
	 * so, if this is not NULL, we need to release it.
	 */
	if (dpi)
		ep_release_epitem(dpi);

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_ctl(%d, %d, %d, %u) = %d\n",
		     current, epfd, op, fd, events, error));

eexit_3:
	fput(tfile);
eexit_2:
	fput(file);
eexit_1:
	return error;
}


/*
 * Implement the event wait interface for the eventpoll file. It is the kernel
 * part of the user space epoll_wait(2).
 */
asmlinkage int sys_epoll_wait(int epfd, struct pollfd *events, int maxevents,
			      int timeout)
{
	int error;
	struct file *file;
	struct eventpoll *ep;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_wait(%d, %p, %d, %d)\n",
		     current, epfd, events, maxevents, timeout));

	/* The maximum number of event must be greater than zero */
	if (maxevents <= 0)
		return -EINVAL;

	/* Verify that the area passed by the user is writeable */
	if ((error = verify_area(VERIFY_WRITE, events, maxevents * sizeof(struct pollfd))))
		goto eexit_1;

	/* Get the "struct file *" for the eventpoll file */
	error = -EBADF;
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

	/* Time to fish for events ... */
	error = ep_poll(ep, events, maxevents, timeout);

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_wait(%d, %p, %d, %d) = %d\n",
		     current, epfd, events, maxevents, timeout, error));

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
	inode = ep_eventpoll_inode();
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
	file->f_flags = O_RDONLY;
	file->f_op = &eventpoll_fops;
	file->f_mode = FMODE_READ;
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
	int i;

	for (i = 0; i < numpages; i++) {
		pages[i] = (char *) __get_free_pages(GFP_KERNEL, 0);
		if (!pages[i]) {
			for (--i; i >= 0; i--) {
				ClearPageReserved(virt_to_page(pages[i]));
				free_pages((unsigned long) pages[i], 0);
			}
			return -ENOMEM;
		}
		SetPageReserved(virt_to_page(pages[i]));
	}
	return 0;
}


static int ep_free_pages(char **pages, int numpages)
{
	int i;

	for (i = 0; i < numpages; i++) {
		ClearPageReserved(virt_to_page(pages[i]));
		free_pages((unsigned long) pages[i], 0);
	}
	return 0;
}


static int ep_file_init(struct file *file, unsigned int hashbits)
{
	int error;
	unsigned long flags;
	struct eventpoll *ep;

	if (!(ep = kmalloc(sizeof(struct eventpoll), GFP_KERNEL)))
		return -ENOMEM;

	memset(ep, 0, sizeof(*ep));

	error = ep_init(ep, hashbits);
	if (error) {
		kfree(ep);
		return error;
	}

	file->private_data = ep;

	/* Add the structure to the linked list that links "struct eventpoll" */
	write_lock_irqsave(&eplock, flags);
	list_add(&ep->llink, &eplist);
	write_unlock_irqrestore(&eplock, flags);

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ep_file_init() ep=%p\n",
		     current, ep));
	return 0;
}


/*
 * Calculate the index of the hash relative to "file".
 */
static unsigned int ep_hash_index(struct eventpoll *ep, struct file *file)
{

	return (unsigned int) hash_ptr(file, ep->hashbits);
}


/*
 * Returns the hash entry ( struct list_head * ) of the passed index.
 */
static struct list_head *ep_hash_entry(struct eventpoll *ep, unsigned int index)
{

	return (struct list_head *) (ep->hpages[index / EP_HENTRY_X_PAGE] +
				     (index % EP_HENTRY_X_PAGE) * sizeof(struct list_head));
}


static int ep_init(struct eventpoll *ep, unsigned int hashbits)
{
	int error;
	unsigned int i, hsize;

	INIT_LIST_HEAD(&ep->llink);
	rwlock_init(&ep->lock);
	init_waitqueue_head(&ep->wq);
	init_waitqueue_head(&ep->poll_wait);
	INIT_LIST_HEAD(&ep->rdllist);

	/* Hash allocation and setup */
	hsize = 1 << hashbits;
	ep->hashbits = hashbits;
	ep->nhpages = (int) (hsize / EP_HENTRY_X_PAGE + (hsize % EP_HENTRY_X_PAGE ? 1: 0));
	error = ep_alloc_pages(ep->hpages, ep->nhpages);
	if (error)
		goto eexit_1;

	/* Initialize hash buckets */
	for (i = 0; i < hsize; i++)
		INIT_LIST_HEAD(ep_hash_entry(ep, i));

	return 0;
eexit_1:
	return error;
}


static void ep_free(struct eventpoll *ep)
{
	unsigned int i, hsize;
	unsigned long flags;
	struct list_head *lsthead;

	/*
	 * Walks through the whole hash by unregistering file callbacks and
	 * freeing each "struct epitem".
	 */
	for (i = 0, hsize = 1 << ep->hashbits; i < hsize; i++) {
		lsthead = ep_hash_entry(ep, i);

		/*
		 * We need to lock this because we could be hit by
		 * ep_notify_file_close() while we're freeing this.
		 */
		write_lock_irqsave(&ep->lock, flags);

		while (!list_empty(lsthead)) {
			struct epitem *dpi = list_entry(lsthead->next, struct epitem, llink);

			/* The function ep_unlink() must be called with held lock */
			ep_unlink(ep, dpi);

			/* We release the lock before releasing the "struct epitem" */
			write_unlock_irqrestore(&ep->lock, flags);

			ep_release_epitem(dpi);

			/* And then we reaquire the lock ... */
			write_lock_irqsave(&ep->lock, flags);
		}

		write_unlock_irqrestore(&ep->lock, flags);
	}

	/* Remove the structure to the linked list that links "struct eventpoll" */
	write_lock_irqsave(&eplock, flags);
	EP_LIST_DEL(&ep->llink);
	write_unlock_irqrestore(&eplock, flags);

	/* Free hash pages */
	ep_free_pages(ep->hpages, ep->nhpages);
}


/*
 * Search the file inside the eventpoll hash. It add usage count to
 * the returned item, so the caller must call ep_release_epitem()
 * after finished using the "struct epitem".
 */
static struct epitem *ep_find(struct eventpoll *ep, struct file *file)
{
	unsigned long flags;
	struct list_head *lsthead, *lnk;
	struct epitem *dpi = NULL;

	read_lock_irqsave(&ep->lock, flags);

	lsthead = ep_hash_entry(ep, ep_hash_index(ep, file));
	list_for_each(lnk, lsthead) {
		dpi = list_entry(lnk, struct epitem, llink);

		if (dpi->file == file) {
			ep_use_epitem(dpi);
			break;
		}
		dpi = NULL;
	}

	read_unlock_irqrestore(&ep->lock, flags);

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ep_find(%p) -> %p\n",
		     current, file, dpi));

	return dpi;
}


/*
 * Increment the usage count of the "struct epitem" making it sure
 * that the user will have a valid pointer to reference.
 */
static void ep_use_epitem(struct epitem *dpi)
{

	atomic_inc(&dpi->usecnt);
}


/*
 * Decrement ( release ) the usage count by signaling that the user
 * has finished using the structure. It might lead to freeing the
 * structure itself if the count goes to zero.
 */
static void ep_release_epitem(struct epitem *dpi)
{

	if (atomic_dec_and_test(&dpi->usecnt))
		DPI_MEM_FREE(dpi);
}


/*
 * This is the callback that is used to add our wait queue to the
 * target file wakeup lists.
 */
static void ep_ptable_queue_proc(void *priv, wait_queue_head_t *whead)
{
	struct epitem *dpi = priv;

	/* No more than EP_MAX_POLL_QUEUE wait queue are supported */
	if (dpi->nwait < EP_MAX_POLL_QUEUE) {
		add_wait_queue(whead, &dpi->wait[dpi->nwait].wait);
		dpi->wait[dpi->nwait].whead = whead;
		dpi->nwait++;
	}
}


static int ep_insert(struct eventpoll *ep, struct pollfd *pfd, struct file *tfile)
{
	int error, i, revents;
	unsigned long flags;
	struct epitem *dpi;
	poll_table pt;

	error = -ENOMEM;
	if (!(dpi = DPI_MEM_ALLOC()))
		goto eexit_1;

	/* Item initialization follow here ... */
	INIT_LIST_HEAD(&dpi->llink);
	INIT_LIST_HEAD(&dpi->rdllink);
	dpi->ep = ep;
	dpi->file = tfile;
	dpi->pfd = *pfd;
	atomic_set(&dpi->usecnt, 1);
	dpi->nwait = 0;
	for (i = 0; i < EP_MAX_POLL_QUEUE; i++) {
		init_waitqueue_func_entry(&dpi->wait[i].wait, ep_poll_callback);
		dpi->wait[i].whead = NULL;
		dpi->wait[i].base = dpi;
	}

	/* Initialize the poll table using the queue callback */
	poll_initwait_ex(&pt, 1, ep_ptable_queue_proc, dpi);

	/* We have to drop the new item inside our item list to keep track of it */
	write_lock_irqsave(&ep->lock, flags);

	/* Add the current item to the hash table */
	list_add(&dpi->llink, ep_hash_entry(ep, ep_hash_index(ep, tfile)));

	/* Attach the item to the poll hooks and get current event bits */
	revents = tfile->f_op->poll(tfile, &pt);

	/* If the file is already "ready" we drop it inside the ready list */
	if ((revents & pfd->events) && !EP_IS_LINKED(&dpi->rdllink))
		list_add(&dpi->rdllink, &ep->rdllist);

	write_unlock_irqrestore(&ep->lock, flags);

	poll_freewait(&pt);

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ep_insert(%p, %d)\n",
		     current, ep, pfd->fd));

	return 0;

eexit_1:
	return error;
}


/*
 * Modify the interest event mask by dropping an event if the new mask
 * has a match in the current file status.
 */
static int ep_modify(struct eventpoll *ep, struct epitem *dpi, unsigned int events)
{
	unsigned int revents;
	unsigned long flags;
	poll_table pt;

	/*
	 * This is a special poll table initialization that will
	 * make poll_wait() to not perform any wait queue insertion when
	 * called by file->f_op->poll(). This is a fast way to retrieve
	 * file events with perform any queue insertion, hence saving CPU cycles.
	 */
	poll_initwait_ex(&pt, 0, NULL, NULL);

	write_lock_irqsave(&ep->lock, flags);

	/* Get current event bits */
	revents = dpi->file->f_op->poll(dpi->file, &pt);

	/* Set the new event interest mask */
	dpi->pfd.events = events;

	/* If the file is already "ready" we drop it inside the ready list */
	if ((revents & events) && EP_IS_LINKED(&dpi->llink) && !EP_IS_LINKED(&dpi->rdllink))
		list_add(&dpi->rdllink, &ep->rdllist);

	write_unlock_irqrestore(&ep->lock, flags);

	poll_freewait(&pt);

	return 0;
}


/*
 * Unlink the "struct epitem" from all places it might have been hooked up.
 * This function must be called with write IRQ lock on "ep->lock".
 */
static int ep_unlink(struct eventpoll *ep, struct epitem *dpi)
{
	int i;

	/*
	 * It can happen that this one is called for an item already unlinked.
	 * The check protect us from doing a double unlink ( crash ).
	 */
	if (!EP_IS_LINKED(&dpi->llink))
		goto not_linked;

	/*
	 * At this point is safe to do the job, unlink the item from our list.
	 * This operation togheter with the above check closes the door to
	 * double unlinks.
	 */
	EP_LIST_DEL(&dpi->llink);

	/* Removes poll wait queue hooks */
	for (i = 0; i < dpi->nwait; i++)
		remove_wait_queue(dpi->wait[i].whead, &dpi->wait[i].wait);

	/*
	 * If the item we are going to remove is inside the ready file descriptors
	 * we want to remove it from this list to avoid stale events.
	 */
	if (EP_IS_LINKED(&dpi->rdllink))
		EP_LIST_DEL(&dpi->rdllink);

not_linked:

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ep_unlink(%p, %d)\n",
		     current, ep, dpi->pfd.fd));

	return 0;
}


/*
 * Removes a "struct epitem" from the eventpoll hash and deallocates
 * all the associated resources.
 */
static int ep_remove(struct eventpoll *ep, struct epitem *dpi)
{
	int error;
	unsigned long flags;

	/* We need to acquire the write IRQ lock before calling ep_unlink() */
	write_lock_irqsave(&ep->lock, flags);

	/* Really unlink the item from the hash */
	error = ep_unlink(ep, dpi);

	write_unlock_irqrestore(&ep->lock, flags);

	if (error)
		goto eexit_1;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ep_remove(%p, %d)\n",
		     current, ep, dpi->pfd.fd));

	/* At this point it is safe to free the eventpoll item */
	ep_release_epitem(dpi);

	error = 0;
eexit_1:
	return error;
}


/*
 * This is the callback that is passed to the wait queue wakeup
 * machanism. It is called by the stored file descriptors when they
 * have events to report.
 */
static int ep_poll_callback(wait_queue_t *wait, unsigned mode, int sync)
{
	unsigned long flags;
	struct epitem *dpi = EP_ITEM_FROM_WAIT(wait);
	struct eventpoll *ep = dpi->ep;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: poll_callback(%p) dpi=%p ep=%p\n",
		     current, dpi->file, dpi, ep));

	write_lock_irqsave(&ep->lock, flags);

	/* If this file is already in the ready list we exit soon */
	if (EP_IS_LINKED(&dpi->rdllink))
		goto is_linked;

	list_add(&dpi->rdllink, &ep->rdllist);

is_linked:
	/*
	 * Wake up ( if active ) both the eventpoll wait list and the ->poll()
	 * wait list.
	 */
	if (waitqueue_active(&ep->wq))
		wake_up(&ep->wq);
	if (waitqueue_active(&ep->poll_wait))
		wake_up(&ep->poll_wait);

	write_unlock_irqrestore(&ep->lock, flags);
	return 1;
}


static int ep_eventpoll_close(struct inode *inode, struct file *file)
{
	struct eventpoll *ep = file->private_data;

	if (ep) {
		ep_free(ep);
		kfree(ep);
	}

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: close() ep=%p\n", current, ep));
	return 0;
}


static unsigned int ep_eventpoll_poll(struct file *file, poll_table *wait)
{
	unsigned int pollflags = 0;
	unsigned long flags;
	struct eventpoll *ep = file->private_data;

	/* Insert inside our poll wait queue */
	poll_wait(file, &ep->poll_wait, wait);

	/* Check our condition */
	read_lock_irqsave(&ep->lock, flags);
	if (!list_empty(&ep->rdllist))
		pollflags = POLLIN | POLLRDNORM;
	read_unlock_irqrestore(&ep->lock, flags);

	return pollflags;
}


/*
 * Perform the transfer of events to user space. Optimize the copy by
 * caching EP_EVENT_BUFF_SIZE events at a time and then copying it to user space.
 */
static int ep_events_transfer(struct eventpoll *ep, struct pollfd *events, int maxevents)
{
	int eventcnt, ebufcnt, revents;
	unsigned long flags;
	struct list_head *lsthead = &ep->rdllist;
	struct pollfd eventbuf[EP_EVENT_BUFF_SIZE];
	poll_table pt;

	/*
	 * This is a special poll table initialization that will
	 * make poll_wait() to not perform any wait queue insertion when
	 * called by file->f_op->poll(). This is a fast way to retrieve
	 * file events with perform any queue insertion, hence saving CPU cycles.
	 */
	poll_initwait_ex(&pt, 0, NULL, NULL);

	write_lock_irqsave(&ep->lock, flags);

	for (eventcnt = 0, ebufcnt = 0; (eventcnt + ebufcnt) < maxevents && !list_empty(lsthead);) {
		struct epitem *dpi = list_entry(lsthead->next, struct epitem, rdllink);

		/* Remove the item from the ready list */
		EP_LIST_DEL(&dpi->rdllink);

		/*
		 * If the item is not linked to the main has table this means that
		 * it's on the way to be removed and we don't want to send events
		 * to such file descriptor.
		 */
		if (!EP_IS_LINKED(&dpi->llink))
			continue;

		/* Fetch event bits from the signaled file */
		revents = dpi->file->f_op->poll(dpi->file, &pt);

		if (revents & dpi->pfd.events) {
			eventbuf[ebufcnt] = dpi->pfd;
			eventbuf[ebufcnt].revents = revents & eventbuf[ebufcnt].events;
			ebufcnt++;

			/* If our buffer page is full we need to flush it to user space */
			if (ebufcnt == EP_EVENT_BUFF_SIZE) {
				/*
				 * We need to drop the irqlock before using the function
				 * __copy_to_user() because it might fault.
				 */
				write_unlock_irqrestore(&ep->lock, flags);
				if (__copy_to_user(&events[eventcnt], eventbuf,
						   ebufcnt * sizeof(struct pollfd))) {
					poll_freewait(&pt);
					return -EFAULT;
				}
				eventcnt += ebufcnt;
				ebufcnt = 0;
				write_lock_irqsave(&ep->lock, flags);
			}
		}
	}
	write_unlock_irqrestore(&ep->lock, flags);

	/* There might be still something inside our event buffer */
	if (ebufcnt) {
		if (__copy_to_user(&events[eventcnt], eventbuf,
				   ebufcnt * sizeof(struct pollfd)))
			eventcnt = -EFAULT;
		else
			eventcnt += ebufcnt;
	}

	poll_freewait(&pt);

	return eventcnt;
}


static int ep_poll(struct eventpoll *ep, struct pollfd *events, int maxevents,
		   int timeout)
{
	int res = 0, eavail;
	unsigned long flags;
	long jtimeout;
	wait_queue_t wait;

	/*
	 * Calculate the timeout by checking for the "infinite" value ( -1 ).
	 * The passed timeout is in milliseconds, that why (t * HZ) / 1000.
	 */
	jtimeout = timeout == -1 ? MAX_SCHEDULE_TIMEOUT: (timeout * HZ) / 1000;

retry:
	write_lock_irqsave(&ep->lock, flags);

	res = 0;
	if (list_empty(&ep->rdllist)) {
		/*
		 * We don't have any available event to return to the caller.
		 * We need to sleep here, and we will be wake up by
		 * ep_poll_callback() when events will become available.
		 */
		init_waitqueue_entry(&wait, current);
		add_wait_queue(&ep->wq, &wait);

		for (;;) {
			/*
			 * We don't want to sleep if the ep_poll_callback() sends us
			 * a wakeup in between. That's why we set the task state
			 * to TASK_INTERRUPTIBLE before doing the checks.
			 */
			set_current_state(TASK_INTERRUPTIBLE);
			if (!list_empty(&ep->rdllist) || !jtimeout)
				break;
			if (signal_pending(current)) {
				res = -EINTR;
				break;
			}

			write_unlock_irqrestore(&ep->lock, flags);
			jtimeout = schedule_timeout(jtimeout);
			write_lock_irqsave(&ep->lock, flags);
		}
		remove_wait_queue(&ep->wq, &wait);

		set_current_state(TASK_RUNNING);
	}

	/* Is it worth to try to dig for events ? */
	eavail = !list_empty(&ep->rdllist);

	write_unlock_irqrestore(&ep->lock, flags);

	/*
	 * Try to transfer events to user space. In case we get 0 events and
	 * there's still timeout left over, we go trying again in search of
	 * more luck.
	 */
	if (!res && eavail &&
	    !(res = ep_events_transfer(ep, events, maxevents)) && jtimeout)
		goto retry;

	return res;
}


static int eventpollfs_delete_dentry(struct dentry *dentry)
{

	return 1;
}


static struct inode *ep_eventpoll_inode(void)
{
	int error = -ENOMEM;
	struct inode *inode = new_inode(eventpoll_mnt->mnt_sb);

	if (!inode)
		goto eexit_1;

	inode->i_fop = &eventpoll_fops;

	/*
	 * Mark the inode dirty from the very beginning,
	 * that way it will never be moved to the dirty
	 * list because mark_inode_dirty() will think
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

	/* Initialize the list that will link "struct eventpoll" */
	INIT_LIST_HEAD(&eplist);

	/* Initialize the rwlock used to access "eplist" */
	rwlock_init(&eplock);

	/* Allocates slab cache used to allocate "struct epitem" items */
	error = -ENOMEM;
	dpi_cache = kmem_cache_create("eventpoll",
				      sizeof(struct epitem),
				      0,
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

	printk(KERN_INFO "[%p] eventpoll: driver installed.\n", current);

	return 0;

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
	kmem_cache_destroy(dpi_cache);
}

module_init(eventpoll_init);
module_exit(eventpoll_exit);

MODULE_LICENSE("GPL");


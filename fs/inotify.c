/*
 * fs/inotify.c - inode-based file event notifications
 *
 * Authors:
 *	John McCutchan	<ttb@tentacle.dhs.org>
 *	Robert Love	<rml@novell.com>
 *
 * Copyright (C) 2005 John McCutchan
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/writeback.h>
#include <linux/inotify.h>

#include <asm/ioctls.h>

static atomic_t inotify_cookie;

static kmem_cache_t *watch_cachep;
static kmem_cache_t *event_cachep;

static int max_user_devices;
static int max_user_watches;
static unsigned int max_queued_events;

/*
 * Lock ordering:
 *
 * dentry->d_lock (used to keep d_move() away from dentry->d_parent)
 * iprune_sem (synchronize shrink_icache_memory())
 * 	inode_lock (protects the super_block->s_inodes list)
 * 	inode->inotify_sem (protects inode->inotify_watches and watches->i_list)
 * 		inotify_dev->sem (protects inotify_device and watches->d_list)
 */

/*
 * Lifetimes of the three main data structures--inotify_device, inode, and
 * inotify_watch--are managed by reference count.
 *
 * inotify_device: Lifetime is from open until release.  Additional references
 * can bump the count via get_inotify_dev() and drop the count via
 * put_inotify_dev().
 *
 * inotify_watch: Lifetime is from create_watch() to destory_watch().
 * Additional references can bump the count via get_inotify_watch() and drop
 * the count via put_inotify_watch().
 *
 * inode: Pinned so long as the inode is associated with a watch, from
 * create_watch() to put_inotify_watch().
 */

/*
 * struct inotify_device - represents an open instance of an inotify device
 *
 * This structure is protected by the semaphore 'sem'.
 */
struct inotify_device {
	wait_queue_head_t 	wq;		/* wait queue for i/o */
	struct idr		idr;		/* idr mapping wd -> watch */
	struct semaphore	sem;		/* protects this bad boy */
	struct list_head 	events;		/* list of queued events */
	struct list_head	watches;	/* list of watches */
	atomic_t		count;		/* reference count */
	struct user_struct	*user;		/* user who opened this dev */
	unsigned int		queue_size;	/* size of the queue (bytes) */
	unsigned int		event_count;	/* number of pending events */
	unsigned int		max_events;	/* maximum number of events */
};

/*
 * struct inotify_kernel_event - An inotify event, originating from a watch and
 * queued for user-space.  A list of these is attached to each instance of the
 * device.  In read(), this list is walked and all events that can fit in the
 * buffer are returned.
 *
 * Protected by dev->sem of the device in which we are queued.
 */
struct inotify_kernel_event {
	struct inotify_event	event;	/* the user-space event */
	struct list_head        list;	/* entry in inotify_device's list */
	char			*name;	/* filename, if any */
};

/*
 * struct inotify_watch - represents a watch request on a specific inode
 *
 * d_list is protected by dev->sem of the associated watch->dev.
 * i_list and mask are protected by inode->inotify_sem of the associated inode.
 * dev, inode, and wd are never written to once the watch is created.
 */
struct inotify_watch {
	struct list_head	d_list;	/* entry in inotify_device's list */
	struct list_head	i_list;	/* entry in inode's list */
	atomic_t		count;	/* reference count */
	struct inotify_device	*dev;	/* associated device */
	struct inode		*inode;	/* associated inode */
	s32 			wd;	/* watch descriptor */
	u32			mask;	/* event mask for this watch */
};

static ssize_t show_max_queued_events(struct class_device *class, char *buf)
{
	return sprintf(buf, "%d\n", max_queued_events);
}

static ssize_t store_max_queued_events(struct class_device *class,
				       const char *buf, size_t count)
{
	unsigned int max;

	if (sscanf(buf, "%u", &max) > 0 && max > 0) {
		max_queued_events = max;
		return strlen(buf);
	}
	return -EINVAL;
}

static ssize_t show_max_user_devices(struct class_device *class, char *buf)
{
	return sprintf(buf, "%d\n", max_user_devices);
}

static ssize_t store_max_user_devices(struct class_device *class,
				      const char *buf, size_t count)
{
	int max;

	if (sscanf(buf, "%d", &max) > 0 && max > 0) {
		max_user_devices = max;
		return strlen(buf);
	}
	return -EINVAL;
}

static ssize_t show_max_user_watches(struct class_device *class, char *buf)
{
	return sprintf(buf, "%d\n", max_user_watches);
}

static ssize_t store_max_user_watches(struct class_device *class,
				      const char *buf, size_t count)
{
	int max;

	if (sscanf(buf, "%d", &max) > 0 && max > 0) {
		max_user_watches = max;
		return strlen(buf);
	}
	return -EINVAL;
}

static CLASS_DEVICE_ATTR(max_queued_events, S_IRUGO | S_IWUSR,
			 show_max_queued_events, store_max_queued_events);
static CLASS_DEVICE_ATTR(max_user_devices, S_IRUGO | S_IWUSR,
			 show_max_user_devices, store_max_user_devices);
static CLASS_DEVICE_ATTR(max_user_watches, S_IRUGO | S_IWUSR,
			 show_max_user_watches, store_max_user_watches);

static inline void get_inotify_dev(struct inotify_device *dev)
{
	atomic_inc(&dev->count);
}

static inline void put_inotify_dev(struct inotify_device *dev)
{
	if (atomic_dec_and_test(&dev->count)) {
		atomic_dec(&dev->user->inotify_devs);
		free_uid(dev->user);
		kfree(dev);
	}
}

static inline void get_inotify_watch(struct inotify_watch *watch)
{
	atomic_inc(&watch->count);
}

/*
 * put_inotify_watch - decrements the ref count on a given watch.  cleans up
 * the watch and its references if the count reaches zero.
 */
static inline void put_inotify_watch(struct inotify_watch *watch)
{
	if (atomic_dec_and_test(&watch->count)) {
		put_inotify_dev(watch->dev);
		iput(watch->inode);
		kmem_cache_free(watch_cachep, watch);
	}
}

/*
 * kernel_event - create a new kernel event with the given parameters
 *
 * This function can sleep.
 */
static struct inotify_kernel_event * kernel_event(s32 wd, u32 mask, u32 cookie,
						  const char *name)
{
	struct inotify_kernel_event *kevent;

	kevent = kmem_cache_alloc(event_cachep, GFP_KERNEL);
	if (unlikely(!kevent))
		return NULL;

	/* we hand this out to user-space, so zero it just in case */
	memset(&kevent->event, 0, sizeof(struct inotify_event));

	kevent->event.wd = wd;
	kevent->event.mask = mask;
	kevent->event.cookie = cookie;

	INIT_LIST_HEAD(&kevent->list);

	if (name) {
		size_t len, rem, event_size = sizeof(struct inotify_event);

		/*
		 * We need to pad the filename so as to properly align an
		 * array of inotify_event structures.  Because the structure is
		 * small and the common case is a small filename, we just round
		 * up to the next multiple of the structure's sizeof.  This is
		 * simple and safe for all architectures.
		 */
		len = strlen(name) + 1;
		rem = event_size - len;
		if (len > event_size) {
			rem = event_size - (len % event_size);
			if (len % event_size == 0)
				rem = 0;
		}

		kevent->name = kmalloc(len + rem, GFP_KERNEL);
		if (unlikely(!kevent->name)) {
			kmem_cache_free(event_cachep, kevent);
			return NULL;
		}
		memcpy(kevent->name, name, len);
		if (rem)
			memset(kevent->name + len, 0, rem);		
		kevent->event.len = len + rem;
	} else {
		kevent->event.len = 0;
		kevent->name = NULL;
	}

	return kevent;
}

/*
 * inotify_dev_get_event - return the next event in the given dev's queue
 *
 * Caller must hold dev->sem.
 */
static inline struct inotify_kernel_event *
inotify_dev_get_event(struct inotify_device *dev)
{
	return list_entry(dev->events.next, struct inotify_kernel_event, list);
}

/*
 * inotify_dev_queue_event - add a new event to the given device
 *
 * Caller must hold dev->sem.  Can sleep (calls kernel_event()).
 */
static void inotify_dev_queue_event(struct inotify_device *dev,
				    struct inotify_watch *watch, u32 mask,
				    u32 cookie, const char *name)
{
	struct inotify_kernel_event *kevent, *last;

	/* coalescing: drop this event if it is a dupe of the previous */
	last = inotify_dev_get_event(dev);
	if (last && last->event.mask == mask && last->event.wd == watch->wd &&
			last->event.cookie == cookie) {
		const char *lastname = last->name;

		if (!name && !lastname)
			return;
		if (name && lastname && !strcmp(lastname, name))
			return;
	}

	/* the queue overflowed and we already sent the Q_OVERFLOW event */
	if (unlikely(dev->event_count > dev->max_events))
		return;

	/* if the queue overflows, we need to notify user space */
	if (unlikely(dev->event_count == dev->max_events))
		kevent = kernel_event(-1, IN_Q_OVERFLOW, cookie, NULL);
	else
		kevent = kernel_event(watch->wd, mask, cookie, name);

	if (unlikely(!kevent))
		return;

	/* queue the event and wake up anyone waiting */
	dev->event_count++;
	dev->queue_size += sizeof(struct inotify_event) + kevent->event.len;
	list_add_tail(&kevent->list, &dev->events);
	wake_up_interruptible(&dev->wq);
}

/*
 * remove_kevent - cleans up and ultimately frees the given kevent
 *
 * Caller must hold dev->sem.
 */
static void remove_kevent(struct inotify_device *dev,
			  struct inotify_kernel_event *kevent)
{
	list_del(&kevent->list);

	dev->event_count--;
	dev->queue_size -= sizeof(struct inotify_event) + kevent->event.len;

	kfree(kevent->name);
	kmem_cache_free(event_cachep, kevent);
}

/*
 * inotify_dev_event_dequeue - destroy an event on the given device
 *
 * Caller must hold dev->sem.
 */
static void inotify_dev_event_dequeue(struct inotify_device *dev)
{
	if (!list_empty(&dev->events)) {
		struct inotify_kernel_event *kevent;
		kevent = inotify_dev_get_event(dev);
		remove_kevent(dev, kevent);
	}
}

/*
 * inotify_dev_get_wd - returns the next WD for use by the given dev
 *
 * Callers must hold dev->sem.  This function can sleep.
 */
static int inotify_dev_get_wd(struct inotify_device *dev,
			      struct inotify_watch *watch)
{
	int ret;

	do {
		if (unlikely(!idr_pre_get(&dev->idr, GFP_KERNEL)))
			return -ENOSPC;
		ret = idr_get_new(&dev->idr, watch, &watch->wd);
	} while (ret == -EAGAIN);

	return ret;
}

/*
 * create_watch - creates a watch on the given device.
 *
 * Callers must hold dev->sem.  Calls inotify_dev_get_wd() so may sleep.
 * Both 'dev' and 'inode' (by way of nameidata) need to be pinned.
 */
static struct inotify_watch *create_watch(struct inotify_device *dev,
					  u32 mask, struct inode *inode)
{
	struct inotify_watch *watch;
	int ret;

	if (atomic_read(&dev->user->inotify_watches) >= max_user_watches)
		return ERR_PTR(-ENOSPC);

	watch = kmem_cache_alloc(watch_cachep, GFP_KERNEL);
	if (unlikely(!watch))
		return ERR_PTR(-ENOMEM);

	ret = inotify_dev_get_wd(dev, watch);
	if (unlikely(ret)) {
		kmem_cache_free(watch_cachep, watch);
		return ERR_PTR(ret);
	}

	watch->mask = mask;
	atomic_set(&watch->count, 0);
	INIT_LIST_HEAD(&watch->d_list);
	INIT_LIST_HEAD(&watch->i_list);

	/* save a reference to device and bump the count to make it official */
	get_inotify_dev(dev);
	watch->dev = dev;

	/*
	 * Save a reference to the inode and bump the ref count to make it
	 * official.  We hold a reference to nameidata, which makes this safe.
	 */
	watch->inode = igrab(inode);

	/* bump our own count, corresponding to our entry in dev->watches */
	get_inotify_watch(watch);

	atomic_inc(&dev->user->inotify_watches);

	return watch;
}

/*
 * inotify_find_dev - find the watch associated with the given inode and dev
 *
 * Callers must hold inode->inotify_sem.
 */
static struct inotify_watch *inode_find_dev(struct inode *inode,
					    struct inotify_device *dev)
{
	struct inotify_watch *watch;

	list_for_each_entry(watch, &inode->inotify_watches, i_list) {
		if (watch->dev == dev)
			return watch;
	}

	return NULL;
}

/*
 * remove_watch_no_event - remove_watch() without the IN_IGNORED event.
 */
static void remove_watch_no_event(struct inotify_watch *watch,
				  struct inotify_device *dev)
{
	list_del(&watch->i_list);
	list_del(&watch->d_list);

	atomic_dec(&dev->user->inotify_watches);
	idr_remove(&dev->idr, watch->wd);
	put_inotify_watch(watch);
}

/*
 * remove_watch - Remove a watch from both the device and the inode.  Sends
 * the IN_IGNORED event to the given device signifying that the inode is no
 * longer watched.
 *
 * Callers must hold both inode->inotify_sem and dev->sem.  We drop a
 * reference to the inode before returning.
 *
 * The inode is not iput() so as to remain atomic.  If the inode needs to be
 * iput(), the call returns one.  Otherwise, it returns zero.
 */
static void remove_watch(struct inotify_watch *watch,struct inotify_device *dev)
{
	inotify_dev_queue_event(dev, watch, IN_IGNORED, 0, NULL);
	remove_watch_no_event(watch, dev);
}

/*
 * inotify_inode_watched - returns nonzero if there are watches on this inode
 * and zero otherwise.  We call this lockless, we do not care if we race.
 */
static inline int inotify_inode_watched(struct inode *inode)
{
	return !list_empty(&inode->inotify_watches);
}

/* Kernel API */

/**
 * inotify_inode_queue_event - queue an event to all watches on this inode
 * @inode: inode event is originating from
 * @mask: event mask describing this event
 * @cookie: cookie for synchronization, or zero
 * @name: filename, if any
 */
void inotify_inode_queue_event(struct inode *inode, u32 mask, u32 cookie,
			       const char *name)
{
	struct inotify_watch *watch, *next;

	if (!inotify_inode_watched(inode))
		return;

	down(&inode->inotify_sem);
	list_for_each_entry_safe(watch, next, &inode->inotify_watches, i_list) {
		u32 watch_mask = watch->mask;
		if (watch_mask & mask) {
			struct inotify_device *dev = watch->dev;
			get_inotify_watch(watch);
			down(&dev->sem);
			inotify_dev_queue_event(dev, watch, mask, cookie, name);
			if (watch_mask & IN_ONESHOT)
				remove_watch_no_event(watch, dev);
			up(&dev->sem);
			put_inotify_watch(watch);
		}
	}
	up(&inode->inotify_sem);
}
EXPORT_SYMBOL_GPL(inotify_inode_queue_event);

/**
 * inotify_dentry_parent_queue_event - queue an event to a dentry's parent
 * @dentry: the dentry in question, we queue against this dentry's parent
 * @mask: event mask describing this event
 * @cookie: cookie for synchronization, or zero
 * @name: filename, if any
 */
void inotify_dentry_parent_queue_event(struct dentry *dentry, u32 mask,
				       u32 cookie, const char *name)
{
	struct dentry *parent;
	struct inode *inode;

	spin_lock(&dentry->d_lock);
	parent = dentry->d_parent;
	inode = parent->d_inode;

	if (inotify_inode_watched(inode)) {
		dget(parent);
		spin_unlock(&dentry->d_lock);
		inotify_inode_queue_event(inode, mask, cookie, name);
		dput(parent);
	} else
		spin_unlock(&dentry->d_lock);
}
EXPORT_SYMBOL_GPL(inotify_dentry_parent_queue_event);

/**
 * inotify_get_cookie - return a unique cookie for use in synchronizing events.
 */
u32 inotify_get_cookie(void)
{
	return atomic_inc_return(&inotify_cookie);
}
EXPORT_SYMBOL_GPL(inotify_get_cookie);

/**
 * inotify_unmount_inodes - an sb is unmounting.  handle any watched inodes.
 * @list: list of inodes being unmounted (sb->s_inodes)
 *
 * Called with inode_lock held, protecting the unmounting super block's list
 * of inodes, and with iprune_sem held, keeping shrink_icache_memory() at bay.
 * We temporarily drop inode_lock, however, and CAN block.
 */
void inotify_unmount_inodes(struct list_head *list)
{
	struct inode *inode, *next_i;

	list_for_each_entry_safe(inode, next_i, list, i_sb_list) {
		struct inotify_watch *watch, *next_w;
		struct list_head *watches;

		/*
		 * We cannot __iget() an inode in state I_CLEAR or I_FREEING,
		 * which is fine because by that point the inode cannot have
		 * any associated watches.
		 */
		if (inode->i_state & (I_CLEAR | I_FREEING))
			continue;

		/* In case the remove_watch() drops a reference */
		__iget(inode);

		/*
		 * We can safely drop inode_lock here because the per-sb list
		 * of inodes must not change during unmount and iprune_sem
		 * keeps shrink_icache_memory() away.
		 */
		spin_unlock(&inode_lock);

		/* for each watch, send IN_UNMOUNT and then remove it */
		down(&inode->inotify_sem);
		watches = &inode->inotify_watches;
		list_for_each_entry_safe(watch, next_w, watches, i_list) {
			struct inotify_device *dev = watch->dev;
			down(&dev->sem);
			inotify_dev_queue_event(dev, watch, IN_UNMOUNT,0,NULL);
			remove_watch(watch, dev);
			up(&dev->sem);
		}
		up(&inode->inotify_sem);
		iput(inode);		

		spin_lock(&inode_lock);
	}
}
EXPORT_SYMBOL_GPL(inotify_unmount_inodes);

/**
 * inotify_inode_is_dead - an inode has been deleted, cleanup any watches
 * @inode: inode that is about to be removed
 */
void inotify_inode_is_dead(struct inode *inode)
{
	struct inotify_watch *watch, *next;

	down(&inode->inotify_sem);
	list_for_each_entry_safe(watch, next, &inode->inotify_watches, i_list) {
		struct inotify_device *dev = watch->dev;
		down(&dev->sem);
		remove_watch(watch, dev);
		up(&dev->sem);
	}
	up(&inode->inotify_sem);
}
EXPORT_SYMBOL_GPL(inotify_inode_is_dead);

/* Device Interface */

static unsigned int inotify_poll(struct file *file, poll_table *wait)
{
	struct inotify_device *dev = file->private_data;
	int ret = 0;

	poll_wait(file, &dev->wq, wait);
	down(&dev->sem);
	if (!list_empty(&dev->events))
		ret = POLLIN | POLLRDNORM;
	up(&dev->sem);

	return ret;
}

static ssize_t inotify_read(struct file *file, char __user *buf,
			    size_t count, loff_t *pos)
{
	size_t event_size = sizeof (struct inotify_event);
	struct inotify_device *dev;
	char __user *start;
	int ret;
	DEFINE_WAIT(wait);

	start = buf;
	dev = file->private_data;

	while (1) {
		int events;

		prepare_to_wait(&dev->wq, &wait, TASK_INTERRUPTIBLE);

		down(&dev->sem);
		events = !list_empty(&dev->events);
		up(&dev->sem);
		if (events) {
			ret = 0;
			break;
		}

		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		schedule();
	}

	finish_wait(&dev->wq, &wait);
	if (ret)
		return ret;

	down(&dev->sem);
	while (1) {
		struct inotify_kernel_event *kevent;

		ret = buf - start;
		if (list_empty(&dev->events))
			break;

		kevent = inotify_dev_get_event(dev);
		if (event_size + kevent->event.len > count)
			break;

		if (copy_to_user(buf, &kevent->event, event_size)) {
			ret = -EFAULT;
			break;
		}
		buf += event_size;
		count -= event_size;

		if (kevent->name) {
			if (copy_to_user(buf, kevent->name, kevent->event.len)){
				ret = -EFAULT;
				break;
			}
			buf += kevent->event.len;
			count -= kevent->event.len;
		}

		remove_kevent(dev, kevent);
	}
	up(&dev->sem);

	return ret;
}

static int inotify_open(struct inode *inode, struct file *file)
{
	struct inotify_device *dev;
	struct user_struct *user;
	int ret;

	user = get_uid(current->user);

	if (unlikely(atomic_read(&user->inotify_devs) >= max_user_devices)) {
		ret = -EMFILE;
		goto out_err;
	}

	dev = kmalloc(sizeof(struct inotify_device), GFP_KERNEL);
	if (unlikely(!dev)) {
		ret = -ENOMEM;
		goto out_err;
	}

	idr_init(&dev->idr);
	INIT_LIST_HEAD(&dev->events);
	INIT_LIST_HEAD(&dev->watches);
	init_waitqueue_head(&dev->wq);
	sema_init(&dev->sem, 1);
	dev->event_count = 0;
	dev->queue_size = 0;
	dev->max_events = max_queued_events;
	dev->user = user;
	atomic_set(&dev->count, 0);

	get_inotify_dev(dev);
	atomic_inc(&user->inotify_devs);

	file->private_data = dev;

	ret = nonseekable_open(inode, file);
	if (ret)
		goto out_err;

	return 0;
out_err:
	free_uid(user);
	return ret;
}

static int inotify_release(struct inode *ignored, struct file *file)
{
	struct inotify_device *dev = file->private_data;

	/*
	 * Destroy all of the watches on this device.  Unfortunately, not very
	 * pretty.  We cannot do a simple iteration over the list, because we
	 * do not know the inode until we iterate to the watch.  But we need to
	 * hold inode->inotify_sem before dev->sem.  The following works.
	 */
	while (1) {
		struct inotify_watch *watch;
		struct list_head *watches;
		struct inode *inode;

		down(&dev->sem);
		watches = &dev->watches;
		if (list_empty(watches)) {
			up(&dev->sem);
			break;
		}
		watch = list_entry(watches->next, struct inotify_watch, d_list);
		get_inotify_watch(watch);
		up(&dev->sem);

		inode = watch->inode;
		down(&inode->inotify_sem);
		down(&dev->sem);
		remove_watch_no_event(watch, dev);
		up(&dev->sem);
		up(&inode->inotify_sem);
		put_inotify_watch(watch);
	}

	/* destroy all of the events on this device */
	down(&dev->sem);
	while (!list_empty(&dev->events))
		inotify_dev_event_dequeue(dev);
	up(&dev->sem);

	/* free this device: the put matching the get in inotify_open() */
	put_inotify_dev(dev);

	return 0;
}

static int inotify_add_watch(struct inotify_device *dev, int fd, u32 mask)
{
	struct inotify_watch *watch, *old;
	struct inode *inode;
	struct file *filp;
	int ret;

	filp = fget(fd);
	if (!filp)
		return -EBADF;
	inode = filp->f_dentry->d_inode;

	down(&inode->inotify_sem);
	down(&dev->sem);

	/* don't let user-space set invalid bits: we don't want flags set */
	mask &= IN_ALL_EVENTS;
	if (!mask) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Handle the case of re-adding a watch on an (inode,dev) pair that we
	 * are already watching.  We just update the mask and return its wd.
	 */
	old = inode_find_dev(inode, dev);
	if (unlikely(old)) {
		old->mask = mask;
		ret = old->wd;
		goto out;
	}

	watch = create_watch(dev, mask, inode);
	if (unlikely(IS_ERR(watch))) {
		ret = PTR_ERR(watch);
		goto out;
	}

	/* Add the watch to the device's and the inode's list */
	list_add(&watch->d_list, &dev->watches);
	list_add(&watch->i_list, &inode->inotify_watches);
	ret = watch->wd;

out:
	up(&dev->sem);
	up(&inode->inotify_sem);
	fput(filp);

	return ret;
}

/*
 * inotify_ignore - handle the INOTIFY_IGNORE ioctl, asking that a given wd be
 * removed from the device.
 *
 * Can sleep.
 */
static int inotify_ignore(struct inotify_device *dev, s32 wd)
{
	struct inotify_watch *watch;
	struct inode *inode;

	down(&dev->sem);
	watch = idr_find(&dev->idr, wd);
	if (unlikely(!watch)) {
		up(&dev->sem);
		return -EINVAL;
	}
	get_inotify_watch(watch);
	inode = watch->inode;
	up(&dev->sem);

	down(&inode->inotify_sem);
	down(&dev->sem);

	/* make sure that we did not race */
	watch = idr_find(&dev->idr, wd);
	if (likely(watch))
		remove_watch(watch, dev);

	up(&dev->sem);
	up(&inode->inotify_sem);
	put_inotify_watch(watch);

	return 0;
}

static long inotify_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct inotify_device *dev;
	struct inotify_watch_request request;
	void __user *p;
	int ret = -ENOTTY;
	s32 wd;

	dev = file->private_data;
	p = (void __user *) arg;

	switch (cmd) {
	case INOTIFY_WATCH:
		if (unlikely(copy_from_user(&request, p, sizeof (request)))) {
			ret = -EFAULT;
			break;
		}
		ret = inotify_add_watch(dev, request.fd, request.mask);
		break;
	case INOTIFY_IGNORE:
		if (unlikely(get_user(wd, (int __user *) p))) {
			ret = -EFAULT;
			break;
		}
		ret = inotify_ignore(dev, wd);
		break;
	case FIONREAD:
		ret = put_user(dev->queue_size, (int __user *) p);
		break;
	}

	return ret;
}

static struct file_operations inotify_fops = {
	.owner		= THIS_MODULE,
	.poll		= inotify_poll,
	.read		= inotify_read,
	.open		= inotify_open,
	.release	= inotify_release,
	.unlocked_ioctl	= inotify_ioctl,
	.compat_ioctl	= inotify_ioctl,
};

static struct miscdevice inotify_device = {
	.minor  = MISC_DYNAMIC_MINOR,
	.name	= "inotify",
	.fops	= &inotify_fops,
};

/*
 * inotify_init - Our initialization function.  Note that we cannnot return
 * error because we have compiled-in VFS hooks.  So an (unlikely) failure here
 * must result in panic().
 */
static int __init inotify_init(void)
{
	struct class_device *class;
	int ret;

	ret = misc_register(&inotify_device);
	if (unlikely(ret))
		panic("inotify: misc_register returned %d\n", ret);

	max_queued_events = 8192;
	max_user_devices = 128;
	max_user_watches = 8192;

	class = inotify_device.class;
	class_device_create_file(class, &class_device_attr_max_queued_events);
	class_device_create_file(class, &class_device_attr_max_user_devices);
	class_device_create_file(class, &class_device_attr_max_user_watches);

	atomic_set(&inotify_cookie, 0);

	watch_cachep = kmem_cache_create("inotify_watch_cache",
					 sizeof(struct inotify_watch),
					 0, SLAB_PANIC, NULL, NULL);
	event_cachep = kmem_cache_create("inotify_event_cache",
					 sizeof(struct inotify_kernel_event),
					 0, SLAB_PANIC, NULL, NULL);

	printk(KERN_INFO "inotify device minor=%d\n", inotify_device.minor);

	return 0;
}

module_init(inotify_init);

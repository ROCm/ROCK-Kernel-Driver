/*
 *  Fast Userspace Mutexes (which I call "Futexes!").
 *  (C) Rusty Russell, IBM 2002
 *
 *  Thanks to Ben LaHaise for yelling "hashed waitqueues" loudly
 *  enough at me, Linus for the original (flawed) idea, Matthew
 *  Kirkwood for proof-of-concept implementation.
 *
 *  "The futexes are also cursed."
 *  "But they come in a choice of three flavours!"
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/hash.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/futex.h>
#include <linux/vcache.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/file.h>
#include <linux/dcache.h>

/* Simple "sleep if unchanged" interface. */

/* FIXME: This may be way too small. --RR */
#define FUTEX_HASHBITS 6

extern void send_sigio(struct fown_struct *fown, int fd, int band);

/* Everyone needs a dentry and inode */
static struct vfsmount *futex_mnt;

/* We use this instead of a normal wait_queue_t, so we can wake only
   the relevent ones (hashed queues may be shared) */
struct futex_q {
	struct list_head list;
	wait_queue_head_t waiters;

	/* Page struct and offset within it. */
	struct page *page;
	unsigned int offset;

	/* the virtual => physical cache */
	vcache_t vcache;

	/* For fd, sigio sent using these. */
	int fd;
	struct file *filp;
};

/* The key for the hash is the address + index + offset within page */
static struct list_head futex_queues[1<<FUTEX_HASHBITS];
static spinlock_t futex_lock = SPIN_LOCK_UNLOCKED;

static inline struct list_head *hash_futex(struct page *page,
					   unsigned long offset)
{
	unsigned long h;

	/* struct page is shared, so we can hash on its address */
	h = (unsigned long)page + offset;
	return &futex_queues[hash_long(h, FUTEX_HASHBITS)];
}

/* Waiter either waiting in FUTEX_WAIT or poll(), or expecting signal */
static inline void tell_waiter(struct futex_q *q)
{
	wake_up_all(&q->waiters);
	if (q->filp)
		send_sigio(&q->filp->f_owner, q->fd, POLL_IN);
}

/* Get kernel address of the user page and pin it. */
static struct page *pin_page(unsigned long page_start)
{
	struct mm_struct *mm = current->mm;
	struct page *page = NULL;
	int err;

	down_read(&mm->mmap_sem);

	err = get_user_pages(current, mm, page_start,
			     1 /* one page */,
			     0 /* not writable */,
			     0 /* don't force */,
			     &page,
			     NULL /* don't return vmas */);
	up_read(&mm->mmap_sem);
	if (err < 0)
		return ERR_PTR(err);
	return page;
}

static inline void unpin_page(struct page *page)
{
	page_cache_release(page);
}

static int futex_wake(unsigned long uaddr, unsigned int offset, int num)
{
	struct list_head *i, *next, *head;
	struct page *page;
	int ret;

	page = pin_page(uaddr - offset);
	ret = IS_ERR(page);
	if (ret)
		goto out;
	head = hash_futex(page, offset);

	spin_lock(&futex_lock);
	list_for_each_safe(i, next, head) {
		struct futex_q *this = list_entry(i, struct futex_q, list);

		if (this->page == page && this->offset == offset) {
			list_del_init(i);
			tell_waiter(this);
			ret++;
			if (ret >= num)
				break;
		}
	}
	spin_unlock(&futex_lock);
	unpin_page(page);
out:
	return ret;
}

static void futex_vcache_callback(vcache_t *vcache, struct page *new_page)
{
	struct futex_q *q = container_of(vcache, struct futex_q, vcache);
	struct list_head *head = hash_futex(new_page, q->offset);

	BUG_ON(list_empty(&q->list));

	spin_lock(&futex_lock);

	q->page = new_page;
	list_del_init(&q->list);
	list_add_tail(&q->list, head);

	spin_unlock(&futex_lock);
}

/* Add at end to avoid starvation */
static inline int queue_me(struct list_head *head,
			    struct futex_q *q,
			    struct page *page,
			    unsigned int offset,
			    int fd,
			    struct file *filp,
			    unsigned long uaddr)
{
	struct page *tmp;
	int ret = 0;

	q->offset = offset;
	q->fd = fd;
	q->filp = filp;

	spin_lock(&vcache_lock);
	spin_lock(&futex_lock);
	spin_lock(&current->mm->page_table_lock);

	/*
	 * Has the mapping changed meanwhile?
	 */
	tmp = follow_page(current->mm, uaddr, 0);

	if (tmp == page) {
		q->page = page;
		list_add_tail(&q->list, head);
		/*
		 * We register a futex callback to this virtual address,
		 * to make sure a COW properly rehashes the futex-queue.
		 */
		__attach_vcache(&q->vcache, uaddr, current->mm, futex_vcache_callback);
	} else
		ret = 1;

	spin_unlock(&current->mm->page_table_lock);
	spin_unlock(&futex_lock);
	spin_unlock(&vcache_lock);

	return ret;
}

/* Return 1 if we were still queued (ie. 0 means we were woken) */
static inline int unqueue_me(struct futex_q *q)
{
	int ret = 0;

	spin_lock(&futex_lock);
	if (!list_empty(&q->list)) {
		list_del(&q->list);
		ret = 1;
	}
	spin_unlock(&futex_lock);
	return ret;
}

static int futex_wait(unsigned long uaddr,
		      int offset,
		      int val,
		      unsigned long time)
{
	DECLARE_WAITQUEUE(wait, current);
	struct list_head *head;
	int ret = 0, curval;
	struct page *page;
	struct futex_q q;

repeat_lookup:
	page = pin_page(uaddr - offset);
	ret = IS_ERR(page);
	if (ret)
		goto out;
	head = hash_futex(page, offset);

	set_current_state(TASK_INTERRUPTIBLE);
	init_waitqueue_head(&q.waiters);
	add_wait_queue(&q.waiters, &wait);
	if (queue_me(head, &q, page, offset, -1, NULL, uaddr)) {
		unpin_page(page);
		goto repeat_lookup;
	}

	/* Page is pinned, but may no longer be in this address space. */
	if (get_user(curval, (int *)uaddr) != 0) {
		ret = -EFAULT;
		goto out;
	}

	if (curval != val) {
		ret = -EWOULDBLOCK;
		goto out;
	}
	time = schedule_timeout(time);
	if (time == 0) {
		ret = -ETIMEDOUT;
		goto out;
	}
	if (signal_pending(current)) {
		ret = -EINTR;
		goto out;
	}
out:
	detach_vcache(&q.vcache);
	set_current_state(TASK_RUNNING);
	/* Were we woken up anyway? */
	if (!unqueue_me(&q))
		ret = 0;
	if (page)
		unpin_page(page);

	return ret;
}

static int futex_close(struct inode *inode, struct file *filp)
{
	struct futex_q *q = filp->private_data;

	spin_lock(&futex_lock);
	if (!list_empty(&q->list)) {
		list_del(&q->list);
		/* Noone can be polling on us now. */
		BUG_ON(waitqueue_active(&q->waiters));
	}
	spin_unlock(&futex_lock);
	unpin_page(q->page);
	kfree(filp->private_data);
	return 0;
}

/* This is one-shot: once it's gone off you need a new fd */
static unsigned int futex_poll(struct file *filp,
			       struct poll_table_struct *wait)
{
	struct futex_q *q = filp->private_data;
	int ret = 0;

	poll_wait(filp, &q->waiters, wait);
	spin_lock(&futex_lock);
	if (list_empty(&q->list))
		ret = POLLIN | POLLRDNORM;
	spin_unlock(&futex_lock);

	return ret;
}

static struct file_operations futex_fops = {
	.release	= futex_close,
	.poll		= futex_poll,
};

/* Signal allows caller to avoid the race which would occur if they
   set the sigio stuff up afterwards. */
static int futex_fd(unsigned long uaddr, int offset, int signal)
{
	struct page *page = NULL;
	struct list_head *head;
	struct futex_q *q;
	struct file *filp;
	int ret;

	ret = -EINVAL;
	if (signal < 0 || signal > _NSIG)
		goto out;

	ret = get_unused_fd();
	if (ret < 0)
		goto out;
	filp = get_empty_filp();
	if (!filp) {
		put_unused_fd(ret);
		ret = -ENFILE;
		goto out;
	}
	filp->f_op = &futex_fops;
	filp->f_vfsmnt = mntget(futex_mnt);
	filp->f_dentry = dget(futex_mnt->mnt_root);

	if (signal) {
		int ret;
		
		ret = f_setown(filp, current->tgid, 1);
		if (ret) {
			put_unused_fd(ret);
			put_filp(filp);
			goto out;
		}
		filp->f_owner.signum = signal;
	}

	q = kmalloc(sizeof(*q), GFP_KERNEL);
	if (!q) {
		put_unused_fd(ret);
		put_filp(filp);
		ret = -ENOMEM;
		goto out;
	}

repeat_lookup:
	page = pin_page(uaddr - offset);
	ret = IS_ERR(page);
	if (ret) {
		put_unused_fd(ret);
		put_filp(filp);
		kfree(q);
		page = NULL;
		goto out;
	}
	head = hash_futex(page, offset);

	/* Initialize queue structure, and add to hash table. */
	filp->private_data = q;
	init_waitqueue_head(&q->waiters);
	if (queue_me(head, q, page, offset, ret, filp, uaddr)) {
		unpin_page(page);
		goto repeat_lookup;
	}

	/* Now we map fd to filp, so userspace can access it */
	fd_install(ret, filp);
	page = NULL;
out:
	if (page)
		unpin_page(page);
	return ret;
}

asmlinkage int sys_futex(unsigned long uaddr, int op, int val, struct timespec *utime)
{
	unsigned long time = MAX_SCHEDULE_TIMEOUT;
	unsigned long pos_in_page;
	int ret;

	if (utime) {
		struct timespec t;
		if (copy_from_user(&t, utime, sizeof(t)) != 0)
			return -EFAULT;
		time = timespec_to_jiffies(&t) + 1;
	}

	pos_in_page = uaddr % PAGE_SIZE;

	/* Must be "naturally" aligned, and not on page boundary. */
	if ((pos_in_page % __alignof__(int)) != 0
	    || pos_in_page + sizeof(int) > PAGE_SIZE)
		return -EINVAL;

	switch (op) {
	case FUTEX_WAIT:
		ret = futex_wait(uaddr, pos_in_page, val, time);
		break;
	case FUTEX_WAKE:
		ret = futex_wake(uaddr, pos_in_page, val);
		break;
	case FUTEX_FD:
		/* non-zero val means F_SETOWN(getpid()) & F_SETSIG(val) */
		ret = futex_fd(uaddr, pos_in_page, val);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static struct super_block *
futexfs_get_sb(struct file_system_type *fs_type,
	       int flags, char *dev_name, void *data)
{
	return get_sb_pseudo(fs_type, "futex", NULL, 0xBAD1DEA);
}

static struct file_system_type futex_fs_type = {
	.name		= "futexfs",
	.get_sb		= futexfs_get_sb,
};

static int __init init(void)
{
	unsigned int i;

	register_filesystem(&futex_fs_type);
	futex_mnt = kern_mount(&futex_fs_type);

	for (i = 0; i < ARRAY_SIZE(futex_queues); i++)
		INIT_LIST_HEAD(&futex_queues[i]);
	return 0;
}
__initcall(init);

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
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/pagemap.h>
#include <asm/uaccess.h>

/* Simple "sleep if unchanged" interface. */

/* FIXME: This may be way too small. --RR */
#define FUTEX_HASHBITS 6

/* We use this instead of a normal wait_queue_t, so we can wake only
   the relevent ones (hashed queues may be shared) */
struct futex_q {
	struct list_head list;
	wait_queue_head_t waiters;
	/* Page struct and offset within it. */
	struct page *page;
	unsigned int offset;
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

static inline void tell_waiter(struct futex_q *q)
{
	wake_up_all(&q->waiters);
}

static int futex_wake(struct list_head *head,
		      struct page *page,
		      unsigned int offset,
		      int num)
{
	struct list_head *i, *next;
	int num_woken = 0;

	spin_lock(&futex_lock);
	list_for_each_safe(i, next, head) {
		struct futex_q *this = list_entry(i, struct futex_q, list);

		if (this->page == page && this->offset == offset) {
			list_del_init(i);
			tell_waiter(this);
			num_woken++;
			if (num_woken >= num) break;
		}
	}
	spin_unlock(&futex_lock);
	return num_woken;
}

/* Add at end to avoid starvation */
static inline void queue_me(struct list_head *head,
			    wait_queue_t *wait,
			    struct futex_q *q,
			    struct page *page,
			    unsigned int offset)
{
	add_wait_queue(&q->waiters, wait);
	q->page = page;
	q->offset = offset;

	spin_lock(&futex_lock);
	list_add_tail(&q->list, head);
	spin_unlock(&futex_lock);
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

/* Get kernel address of the user page and pin it. */
static struct page *pin_page(unsigned long page_start)
{
	struct mm_struct *mm = current->mm;
	struct page *page;
	int err;

	down_read(&mm->mmap_sem);
	err = get_user_pages(current, current->mm, page_start,
			     1 /* one page */,
			     1 /* writable */,
			     0 /* don't force */,
			     &page,
			     NULL /* don't return vmas */);
	up_read(&mm->mmap_sem);

	if (err < 0)
		return ERR_PTR(err);
	return page;
}

static int futex_wait(struct list_head *head,
		      struct page *page,
		      int offset,
		      int val,
		      int *uaddr,
		      unsigned long time)
{
	int curval;
	struct futex_q q;
	DECLARE_WAITQUEUE(wait, current);
	int ret = 0;

	set_current_state(TASK_INTERRUPTIBLE);
	queue_me(head, &wait, &q, page, offset);

	/* Page is pinned, but may no longer be in this address space. */
	if (get_user(curval, uaddr) != 0) {
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
	set_current_state(TASK_RUNNING);
	/* Were we woken up anyway? */
	if (!unqueue_me(&q))
		return 0;
	return ret;
}

asmlinkage int sys_futex(void *uaddr, int op, int val, struct timespec *utime)
{
	int ret;
	unsigned long pos_in_page;
	struct list_head *head;
	struct page *page;
	unsigned long time = MAX_SCHEDULE_TIMEOUT;

	if (utime) {
		struct timespec t;
		if (copy_from_user(&t, utime, sizeof(t)) != 0)
			return -EFAULT;
		time = timespec_to_jiffies(&t) + 1;
	}

	pos_in_page = ((unsigned long)uaddr) % PAGE_SIZE;

	/* Must be "naturally" aligned, and not on page boundary. */
	if ((pos_in_page % __alignof__(int)) != 0
	    || pos_in_page + sizeof(int) > PAGE_SIZE)
		return -EINVAL;

	/* Simpler if it doesn't vanish underneath us. */
	page = pin_page((unsigned long)uaddr - pos_in_page);
	if (IS_ERR(page))
		return PTR_ERR(page);

	head = hash_futex(page, pos_in_page);
	switch (op) {
	case FUTEX_WAIT:
		ret = futex_wait(head, page, pos_in_page, val, uaddr, time);
		break;
	case FUTEX_WAKE:
		ret = futex_wake(head, page, pos_in_page, val);
		break;
	default:
		ret = -EINVAL;
	}
	page_cache_release(page);

	return ret;
}

static int __init init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(futex_queues); i++)
		INIT_LIST_HEAD(&futex_queues[i]);
	return 0;
}
__initcall(init);

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
#include <asm/atomic.h>

/* These mutexes are a very simple counter: the winner is the one who
   decrements from 1 to 0.  The counter starts at 1 when the lock is
   free.  A value other than 0 or 1 means someone may be sleeping.
   This is simple enough to work on all architectures, but has the
   problem that if we never "up" the semaphore it could eventually
   wrap around. */

/* FIXME: This may be way too small. --RR */
#define FUTEX_HASHBITS 6

/* We use this instead of a normal wait_queue_t, so we can wake only
   the relevent ones (hashed queues may be shared) */
struct futex_q {
	struct list_head list;
	struct task_struct *task;
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

static inline void wake_one_waiter(struct list_head *head,
				   struct page *page,
				   unsigned int offset)
{
	struct list_head *i;

	spin_lock(&futex_lock);
	list_for_each(i, head) {
		struct futex_q *this = list_entry(i, struct futex_q, list);

		if (this->page == page && this->offset == offset) {
			wake_up_process(this->task);
			break;
		}
	}
	spin_unlock(&futex_lock);
}

/* Add at end to avoid starvation */
static inline void queue_me(struct list_head *head,
			    struct futex_q *q,
			    struct page *page,
			    unsigned int offset)
{
	q->task = current;
	q->page = page;
	q->offset = offset;

	spin_lock(&futex_lock);
	list_add_tail(&q->list, head);
	spin_unlock(&futex_lock);
}

static inline void unqueue_me(struct futex_q *q)
{
	spin_lock(&futex_lock);
	list_del(&q->list);
	spin_unlock(&futex_lock);
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

/* Try to decrement the user count to zero. */
static int decrement_to_zero(struct page *page, unsigned int offset)
{
	atomic_t *count;
	int ret = 0;

	count = kmap(page) + offset;
	/* If we take the semaphore from 1 to 0, it's ours.  If it's
           zero, decrement anyway, to indicate we are waiting.  If
           it's negative, don't decrement so we don't wrap... */
	if (atomic_read(count) >= 0 && atomic_dec_and_test(count))
		ret = 1;
	kunmap(page);
	return ret;
}

/* Simplified from arch/ppc/kernel/semaphore.c: Paul M. is a genius. */
static int futex_down(struct list_head *head, struct page *page, int offset)
{
	int retval = 0;
	struct futex_q q;

	current->state = TASK_INTERRUPTIBLE;
	queue_me(head, &q, page, offset);

	while (!decrement_to_zero(page, offset)) {
		if (signal_pending(current)) {
			retval = -EINTR;
			break;
		}
		schedule();
		current->state = TASK_INTERRUPTIBLE;
	}
	current->state = TASK_RUNNING;
	unqueue_me(&q);
	/* If we were signalled, we might have just been woken: we
	   must wake another one.  Otherwise we need to wake someone
	   else (if they are waiting) so they drop the count below 0,
	   and when we "up" in userspace, we know there is a
	   waiter. */
	wake_one_waiter(head, page, offset);
	return retval;
}

static int futex_up(struct list_head *head, struct page *page, int offset)
{
	atomic_t *count;

	count = kmap(page) + offset;
	atomic_set(count, 1);
	smp_wmb();
	kunmap(page);
	wake_one_waiter(head, page, offset);
	return 0;
}

asmlinkage int sys_futex(void *uaddr, int op)
{
	int ret;
	unsigned long pos_in_page;
	struct list_head *head;
	struct page *page;

	pos_in_page = ((unsigned long)uaddr) % PAGE_SIZE;

	/* Must be "naturally" aligned, and not on page boundary. */
	if ((pos_in_page % __alignof__(atomic_t)) != 0
	    || pos_in_page + sizeof(atomic_t) > PAGE_SIZE)
		return -EINVAL;

	/* Simpler if it doesn't vanish underneath us. */
	page = pin_page((unsigned long)uaddr - pos_in_page);
	if (IS_ERR(page))
		return PTR_ERR(page);

	head = hash_futex(page, pos_in_page);
	switch (op) {
	case FUTEX_UP:
		ret = futex_up(head, page, pos_in_page);
		break;
	case FUTEX_DOWN:
		ret = futex_down(head, page, pos_in_page);
		break;
	/* Add other lock types here... */
	default:
		ret = -EINVAL;
	}
	put_page(page);

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

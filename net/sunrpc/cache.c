/*
 * net/sunrpc/cache.c
 *
 * Generic code for various authentication-related caches
 * used by sunrpc clients and servers.
 *
 * Copyright (C) 2002 Neil Brown <neilb@cse.unsw.edu.au>
 *
 * Released under terms in GPL version 2.  See COPYING.
 *
 */

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <asm/ioctls.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/cache.h>
#include <linux/sunrpc/stats.h>

#define	 RPCDBG_FACILITY RPCDBG_CACHE

void cache_init(struct cache_head *h)
{
	time_t now = CURRENT_TIME;
	h->next = NULL;
	h->flags = 0;
	atomic_set(&h->refcnt, 0);
	h->expiry_time = now + CACHE_NEW_EXPIRY;
	h->last_refresh = now;
}


/*
 * This is the generic cache management routine for all
 * the authentication caches.
 * It checks the currency of a cache item and will (later)
 * initiate an upcall to fill it if needed.
 *
 *
 * Returns 0 if the cache_head can be used, or cache_puts it and returns
 * -EAGAIN if upcall is pending,
 * -ENOENT if cache entry was negative
 */
int cache_check(struct cache_detail *detail,
		    struct cache_head *h, struct cache_req *rqstp)
{
	int rv;

	/* First decide return status as best we can */
	if (!test_bit(CACHE_VALID, &h->flags) ||
	    h->expiry_time < CURRENT_TIME)
		rv = -EAGAIN;
	else if (detail->flush_time > h->last_refresh)
		rv = -EAGAIN;
	else {
		/* entry is valid */
		if (test_bit(CACHE_NEGATIVE, &h->flags))
			rv = -ENOENT;
		else rv = 0;
	}

	/* up-call processing goes here later */
	/* if cache_pending, initiate upcall if none pending.
	 * if upcall cannot be initiated, change to CACHE_NEGATIVE
	 */
	if (rv == CACHE_PENDING) rv = CACHE_NEGATIVE;

	if (rv == CACHE_PENDING)
		cache_defer_req(rqstp, h);

	if (rv == -EAGAIN /* && cannot do upcall */)
		rv = -ENOENT;

	if (rv && h)
		detail->cache_put(h, detail);
	return rv;
}

void cache_fresh(struct cache_detail *detail,
		 struct cache_head *head, time_t expiry)
{

	head->expiry_time = expiry;
	head->last_refresh = CURRENT_TIME;
	set_bit(CACHE_VALID, &head->flags);
	clear_bit(CACHE_PENDING, &head->flags);
}

/*
 * caches need to be periodically cleaned.
 * For this we maintain a list of cache_detail and
 * a current pointer into that list and into the table
 * for that entry.
 *
 * Each time clean_cache is called it finds the next non-empty entry
 * in the current table and walks the list in that entry
 * looking for entries that can be removed.
 *
 * An entry gets removed if:
 * - The expiry is before current time
 * - The last_refresh time is before the flush_time for that cache
 *
 * later we might drop old entries with non-NEVER expiry if that table
 * is getting 'full' for some definition of 'full'
 *
 * The question of "how often to scan a table" is an interesting one
 * and is answered in part by the use of the "nextcheck" field in the
 * cache_detail.
 * When a scan of a table begins, the nextcheck field is set to a time
 * that is well into the future.
 * While scanning, if an expiry time is found that is earlier than the
 * current nextcheck time, nextcheck is set to that expiry time.
 * If the flush_time is ever set to a time earlier than the nextcheck
 * time, the nextcheck time is then set to that flush_time.
 *
 * A table is then only scanned if the current time is at least
 * the nextcheck time.
 * 
 */

static LIST_HEAD(cache_list);
static spinlock_t cache_list_lock = SPIN_LOCK_UNLOCKED;
static struct cache_detail *current_detail;
static int current_index;

struct file_operations cache_file_operations;

void cache_register(struct cache_detail *cd)
{
	cd->proc_ent = proc_mkdir(cd->name, proc_net_rpc);
	if (cd->proc_ent) {
		struct proc_dir_entry *p;
		cd->proc_ent->owner = THIS_MODULE;
		
		p = create_proc_entry("channel", S_IFREG|S_IRUSR|S_IWUSR,
				      cd->proc_ent);
		if (p) {
			p->proc_fops = &cache_file_operations;
			p->owner = THIS_MODULE;
			p->data = cd;
		}
	}
	rwlock_init(&cd->hash_lock);
	INIT_LIST_HEAD(&cd->queue);
	spin_lock(&cache_list_lock);
	cd->nextcheck = 0;
	cd->entries = 0;
	list_add(&cd->others, &cache_list);
	spin_unlock(&cache_list_lock);
}

int cache_unregister(struct cache_detail *cd)
{
	cache_purge(cd);
	spin_lock(&cache_list_lock);
	write_lock(&cd->hash_lock);
	if (cd->entries || atomic_read(&cd->inuse)) {
		write_unlock(&cd->hash_lock);
		spin_unlock(&cache_list_lock);
		return -EBUSY;
	}
	if (current_detail == cd)
		current_detail = NULL;
	list_del_init(&cd->others);
	write_unlock(&cd->hash_lock);
	spin_unlock(&cache_list_lock);
	if (cd->proc_ent) {
		cd->proc_ent = NULL;
		remove_proc_entry(cd->name, proc_net_rpc);
	}
	return 0;
}

struct cache_detail *cache_find(char *name)
{
	struct list_head *l;

	spin_lock(&cache_list_lock);
	list_for_each(l, &cache_list) {
		struct cache_detail *cd = list_entry(l, struct cache_detail, others);
		
		if (strcmp(cd->name, name)==0) {
			atomic_inc(&cd->inuse);
			spin_unlock(&cache_list_lock);
			return cd;
		}
	}
	spin_unlock(&cache_list_lock);
	return NULL;
}

/* cache_drop must be called on any cache returned by
 * cache_find, after it has been used
 */
void cache_drop(struct cache_detail *detail)
{
	atomic_dec(&detail->inuse);
}

/* clean cache tries to find something to clean
 * and cleans it.
 * It returns 1 if it cleaned something,
 *            0 if it didn't find anything this time
 *           -1 if it fell off the end of the list.
 */
int cache_clean(void)
{
	int rv = 0;
	struct list_head *next;

	spin_lock(&cache_list_lock);

	/* find a suitable table if we don't already have one */
	while (current_detail == NULL ||
	    current_index >= current_detail->hash_size) {
		if (current_detail)
			next = current_detail->others.next;
		else
			next = cache_list.next;
		if (next == &cache_list) {
			current_detail = NULL;
			spin_unlock(&cache_list_lock);
			return -1;
		}
		current_detail = list_entry(next, struct cache_detail, others);
		if (current_detail->nextcheck > CURRENT_TIME)
			current_index = current_detail->hash_size;
		else {
			current_index = 0;
			current_detail->nextcheck = CURRENT_TIME+30*60;
		}
	}

	/* find a non-empty bucket in the table */
	while (current_detail &&
	       current_index < current_detail->hash_size &&
	       current_detail->hash_table[current_index] == NULL)
		current_index++;

	/* find a cleanable entry in the bucket and clean it, or set to next bucket */
	
	if (current_detail && current_index < current_detail->hash_size) {
		struct cache_head *ch, **cp;
		
		write_lock(&current_detail->hash_lock);

		/* Ok, now to clean this strand */
			
		cp = & current_detail->hash_table[current_index];
		ch = *cp;
		for (; ch; cp= & ch->next, ch= *cp) {
			if (atomic_read(&ch->refcnt))
				continue;
			if (ch->expiry_time < CURRENT_TIME
			    || ch->last_refresh < current_detail->flush_time
				)
				break;
			if (current_detail->nextcheck > ch->expiry_time)
				current_detail->nextcheck = ch->expiry_time+1;
		}
		if (ch) {
			cache_get(ch);
			clear_bit(CACHE_HASHED, &ch->flags);
			*cp = ch->next;
			ch->next = NULL;
			current_detail->entries--;
			rv = 1;
		}
		write_unlock(&current_detail->hash_lock);
		if (ch)
			current_detail->cache_put(ch, current_detail);
		else
			current_index ++;
	}
	spin_unlock(&cache_list_lock);

	return rv;
}

/* 
 * Clean all caches promptly.  This just calls cache_clean
 * repeatedly until we are sure that every cache has had a chance to 
 * be fully cleaned
 */
void cache_flush(void)
{
	while (cache_clean() != -1)
		cond_resched();
	while (cache_clean() != -1)
		cond_resched();
}

void cache_purge(struct cache_detail *detail)
{
	detail->flush_time = CURRENT_TIME+1;
	detail->nextcheck = CURRENT_TIME;
	cache_flush();
}



/*
 * Deferral and Revisiting of Requests.
 *
 * If a cache lookup finds a pending entry, we
 * need to defer the request and revisit it later.
 * All deferred requests are stored in a hash table,
 * indexed by "struct cache_head *".
 * As it may be wasteful to store a whole request
 * structure, we allow the request to provide a 
 * deferred form, which must contain a
 * 'struct cache_deferred_req'
 * This cache_deferred_req contains a method to allow
 * it to be revisited when cache info is available
 */

#define	DFR_HASHSIZE	(PAGE_SIZE/sizeof(struct list_head))
#define	DFR_HASH(item)	((((long)item)>>4 ^ (((long)item)>>13)) % DFR_HASHSIZE)

#define	DFR_MAX	300	/* ??? */

spinlock_t cache_defer_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(cache_defer_list);
static struct list_head cache_defer_hash[DFR_HASHSIZE];
static int cache_defer_cnt;

void cache_defer_req(struct cache_req *req, struct cache_head *item)
{
	struct cache_deferred_req *dreq;
	int hash = DFR_HASH(item);

	dreq = req->defer(req);
	if (dreq == NULL)
		return;

	dreq->item = item;
	dreq->recv_time = CURRENT_TIME;

	spin_lock(&cache_defer_lock);

	list_add(&dreq->recent, &cache_defer_list);

	if (cache_defer_hash[hash].next == NULL)
		INIT_LIST_HEAD(&cache_defer_hash[hash]);
	list_add(&dreq->hash, &cache_defer_hash[hash]);

	/* it is in, now maybe clean up */
	dreq = NULL;
	if (++cache_defer_cnt > DFR_MAX) {
		/* too much in the cache, randomly drop
		 * first or last
		 */
		if (net_random()&1) 
			dreq = list_entry(cache_defer_list.next,
					  struct cache_deferred_req,
					  recent);
		else
			dreq = list_entry(cache_defer_list.prev,
					  struct cache_deferred_req,
					  recent);
		list_del(&dreq->recent);
		list_del(&dreq->hash);
		cache_defer_cnt--;
	}
	spin_unlock(&cache_defer_lock);

	if (dreq) {
		/* there was one too many */
		dreq->revisit(dreq, 1);
	}
	if (test_bit(CACHE_VALID, &item->flags)) {
		/* must have just been validated... */
		cache_revisit_request(item);
	}
}

void cache_revisit_request(struct cache_head *item)
{
	struct cache_deferred_req *dreq;
	struct list_head pending;

	struct list_head *lp;
	int hash = DFR_HASH(item);

	INIT_LIST_HEAD(&pending);
	spin_lock(&cache_defer_lock);
	
	lp = cache_defer_hash[hash].next;
	if (lp) {
		while (lp != &cache_defer_hash[hash]) {
			dreq = list_entry(lp, struct cache_deferred_req, hash);
			lp = lp->next;
			if (dreq->item == item) {
				list_del(&dreq->hash);
				list_move(&dreq->recent, &pending);
				cache_defer_cnt--;
			}
		}
	}
	spin_unlock(&cache_defer_lock);

	while (!list_empty(&pending)) {
		dreq = list_entry(pending.next, struct cache_deferred_req, recent);
		list_del_init(&dreq->recent);
		dreq->revisit(dreq, 0);
	}
}

/*
 * communicate with user-space
 *
 * We have a magic /proc file - /proc/sunrpc/cache
 * On read, you get a full request, or block
 * On write, an update request is processed
 * Poll works if anything to read, and always allows write
 *
 * Implemented by linked list of requests.  Each open file has 
 * a ->private that also exists in this list.  New request are added
 * to the end and may wakeup and preceeding readers.
 * New readers are added to the head.  If, on read, an item is found with
 * CACHE_UPCALLING clear, we free it from the list.
 *
 */

static spinlock_t queue_lock = SPIN_LOCK_UNLOCKED;
static DECLARE_MUTEX(queue_io_sem);

struct cache_queue {
	struct list_head	list;
	int			reader;	/* if 0, then request */
};
struct cache_request {
	struct cache_queue	q;
	struct cache_head	*item;
	char			* buf;
	int			len;
	int			readers;
};
struct cache_reader {
	struct cache_queue	q;
	int			offset;	/* if non-0, we have a refcnt on next request */
	char			*page;
};

static ssize_t
cache_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	struct cache_reader *rp = filp->private_data;
	struct cache_request *rq;
	struct cache_detail *cd = PDE(filp->f_dentry->d_inode)->data;
	int err;

	if (ppos != &filp->f_pos)
		return -ESPIPE;

	if (count == 0)
		return 0;

	down(&queue_io_sem); /* protect against multiple concurrent
			      * readers on this file */
 again:
	spin_lock(&queue_lock);
	/* need to find next request */
	while (rp->q.list.next != &cd->queue &&
	       list_entry(rp->q.list.next, struct cache_queue, list)
	       ->reader) {
		struct list_head *next = rp->q.list.next;
		list_move(&rp->q.list, next);
	}
	if (rp->q.list.next == &cd->queue) {
		spin_unlock(&queue_lock);
		up(&queue_io_sem);
		if (rp->offset)
			BUG();
		return 0;
	}
	rq = container_of(rp->q.list.next, struct cache_request, q.list);
	if (rq->q.reader) BUG();
	if (rp->offset == 0)
		rq->readers++;
	spin_unlock(&queue_lock);

	if (rp->offset == 0 && !test_bit(CACHE_PENDING, &rq->item->flags)) {
		err = -EAGAIN;
		spin_lock(&queue_lock);
		list_move(&rp->q.list, &rq->q.list);
		spin_unlock(&queue_lock);
	} else {
		if (rp->offset + count > rq->len)
			count = rq->len - rp->offset;
		err = -EFAULT;
		if (copy_to_user(buf, rq->buf + rp->offset, count))
			goto out;
		rp->offset += count;
		if (rp->offset >= rq->len) {
			rp->offset = 0;
			spin_lock(&queue_lock);
			list_move(&rp->q.list, &rq->q.list);
			spin_unlock(&queue_lock);
		}
		err = 0;
	}
 out:
	if (rp->offset == 0) {
		/* need to release rq */
		spin_lock(&queue_lock);
		rq->readers--;
		if (rq->readers == 0 &&
		    !test_bit(CACHE_PENDING, &rq->item->flags)) {
			list_del(&rq->q.list);
			spin_unlock(&queue_lock);
			cd->cache_put(rq->item, cd);
			kfree(rq->buf);
			kfree(rq);
		} else
			spin_unlock(&queue_lock);
	}
	if (err == -EAGAIN)
		goto again;
	up(&queue_io_sem);
	return err ? err :  count;
}

static ssize_t
cache_write(struct file *filp, const char *buf, size_t count,
	    loff_t *ppos)
{
	int err;
	struct cache_reader *rp = filp->private_data;
	struct cache_detail *cd = PDE(filp->f_dentry->d_inode)->data;

	if (ppos != &filp->f_pos)
		return -ESPIPE;

	if (count == 0)
		return 0;
	if (count > PAGE_SIZE)
		return -EINVAL;

	down(&queue_io_sem);

	if (rp->page == NULL) {
		rp->page = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (rp->page == NULL) {
			up(&queue_io_sem);
			return -ENOMEM;
		}
	}

	if (copy_from_user(rp->page, buf, count)) {
		up(&queue_io_sem);
		return -EFAULT;
	}
	if (count < PAGE_SIZE)
		rp->page[count] = '\0';
	if (cd->cache_parse)
		err = cd->cache_parse(cd, rp->page, count);
	else
		err = -EINVAL;

	up(&queue_io_sem);
	return err ? err : count;
}

static DECLARE_WAIT_QUEUE_HEAD(queue_wait);

static unsigned int
cache_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask;
	struct cache_reader *rp = filp->private_data;
	struct cache_queue *cq;
	struct cache_detail *cd = PDE(filp->f_dentry->d_inode)->data;

	poll_wait(filp, &queue_wait, wait);

	/* alway allow write */
	mask = POLL_OUT | POLLWRNORM;
	spin_lock(&queue_lock);

	for (cq= &rp->q; &cq->list != &cd->queue;
	     cq = list_entry(cq->list.next, struct cache_queue, list))
		if (!cq->reader) {
			mask |= POLLIN | POLLRDNORM;
			break;
		}
	spin_unlock(&queue_lock);
	return mask;
}

static int
cache_ioctl(struct inode *ino, struct file *filp,
	    unsigned int cmd, unsigned long arg)
{
	int len = 0;
	struct cache_reader *rp = filp->private_data;
	struct cache_queue *cq;
	struct cache_detail *cd = PDE(ino)->data;

	if (cmd != FIONREAD)
		return -EINVAL;
	spin_lock(&queue_lock);

	/* only find the length remaining in current request,
	 * or the length of the next request
	 */
	for (cq= &rp->q; &cq->list != &cd->queue;
	     cq = list_entry(cq->list.next, struct cache_queue, list))
		if (!cq->reader) {
			struct cache_request *cr =
				container_of(cq, struct cache_request, q);
			len = cr->len - rp->offset;
			break;
		}
	spin_unlock(&queue_lock);

	return put_user(len, (int *)arg);
}

static int
cache_open(struct inode *inode, struct file *filp)
{
	struct cache_reader *rp;
	struct cache_detail *cd = PDE(inode)->data;

	rp = kmalloc(sizeof(*rp), GFP_KERNEL);
	if (!rp)
		return -ENOMEM;
	rp->page = NULL;
	rp->offset = 0;
	rp->q.reader = 1;
	spin_lock(&queue_lock);
	list_add(&rp->q.list, &cd->queue);
	spin_unlock(&queue_lock);
	filp->private_data = rp;
	return 0;
}

static int
cache_release(struct inode *inode, struct file *filp)
{
	struct cache_reader *rp = filp->private_data;
	struct cache_detail *cd = PDE(inode)->data;

	spin_lock(&queue_lock);
	if (rp->offset) {
		struct cache_queue *cq;
		for (cq= &rp->q; &cq->list != &cd->queue;
		     cq = list_entry(cq->list.next, struct cache_queue, list))
			if (!cq->reader) {
				container_of(cq, struct cache_request, q)
					->readers--;
				break;
			}
		rp->offset = 0;
	}
	list_del(&rp->q.list);
	spin_unlock(&queue_lock);

	if (rp->page)
		kfree(rp->page);

	filp->private_data = NULL;
	kfree(rp);
	return 0;
}



struct file_operations cache_file_operations = {
	.llseek		= no_llseek,
	.read		= cache_read,
	.write		= cache_write,
	.poll		= cache_poll,
	.ioctl		= cache_ioctl, /* for FIONREAD */
	.open		= cache_open,
	.release	= cache_release,
};

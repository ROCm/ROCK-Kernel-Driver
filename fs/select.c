/*
 * This file contains the procedures for the handling of select and poll
 *
 * Created for Linux based loosely upon Mathius Lattner's minix
 * patches by Peter MacDonald. Heavily edited by Linus.
 *
 *  4 February 1994
 *     COFF/ELF binary emulation. If the process has the STICKY_TIMEOUTS
 *     flag set in its personality we do *not* modify the given timeout
 *     parameter to reflect time remaining.
 *
 *  24 January 2000
 *     Changed sys_poll()/do_poll() to use PAGE_SIZE chunk-based allocation 
 *     of fds to overcome nfds < 16390 descriptors limit (Tigran Aivazian).
 * 
 *  Dec 2001
 *     Stack allocation and fast path (Andi Kleen) 
 */

#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/poll.h>
#include <linux/personality.h> /* for STICKY_TIMEOUTS */
#include <linux/file.h>
#include <linux/fs.h>

#include <asm/uaccess.h>

#define ROUND_UP(x,y) (((x)+(y)-1)/(y))
#define DEFAULT_POLLMASK (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM)

/*
 * Ok, Peter made a complicated, but straightforward multiple_wait() function.
 * I have rewritten this, taking some shortcuts: This code may not be easy to
 * follow, but it should be free of race-conditions, and it's practical. If you
 * understand what I'm doing here, then you understand how the linux
 * sleep/wakeup mechanism works.
 *
 * Two very simple procedures, poll_wait() and poll_freewait() make all the
 * work.  poll_wait() is an inline-function defined in <linux/poll.h>,
 * as all select/poll functions have to call it to add an entry to the
 * poll table.
 */

void poll_freewait(poll_table* pt)
{
	struct poll_table_page * p = pt->table;
	while (p) {
		struct poll_table_entry * entry;
		struct poll_table_page *old;

		entry = p->entry;
		while (entry > p->entries) {
			entry--;
			remove_wait_queue(entry->wait_address,&entry->wait);
			fput(entry->filp);
		}
		old = p;
		p = p->next;
		if (old != &pt->inline_page) 
			free_page((unsigned long) old);
	}
}

void __pollwait(struct file * filp, wait_queue_head_t * wait_address, poll_table *p)
{
	struct poll_table_page *table = p->table;
	struct poll_table_page *new_table = NULL;
	int sz;

	if (!table) { 
		new_table = &p->inline_page; 
	} else { 
		sz = (table == &p->inline_page) ? POLL_INLINE_TABLE_LEN : PAGE_SIZE; 
		if ((char*)table->entry >= (char*)table + sz) {
			new_table = (struct poll_table_page *)__get_free_page(GFP_KERNEL);
			if (!new_table) {
				p->error = -ENOMEM;
				__set_current_state(TASK_RUNNING);
				return;
			}
		}
	} 

	if (new_table) { 
		new_table->entry = new_table->entries;
		new_table->next = table;
		p->table = new_table;
		table = new_table;
	}

	/* Add a new entry */
	{
		struct poll_table_entry * entry = table->entry;
		table->entry = entry+1;
	 	get_file(filp);
	 	entry->filp = filp;
		entry->wait_address = wait_address;
		init_waitqueue_entry(&entry->wait, current);
		add_wait_queue(wait_address,&entry->wait);
	}
}

#define __IN(fds, n)		(fds->in + n)
#define __OUT(fds, n)		(fds->out + n)
#define __EX(fds, n)		(fds->ex + n)
#define __RES_IN(fds, n)	(fds->res_in + n)
#define __RES_OUT(fds, n)	(fds->res_out + n)
#define __RES_EX(fds, n)	(fds->res_ex + n)

#define BITS(fds, n)		(*__IN(fds, n)|*__OUT(fds, n)|*__EX(fds, n))

#define ISSET(i,m)	(((i)&*(m)) != 0)
#define SET(i,m)	(*(m) |= (i))

#define POLLIN_SET (POLLRDNORM | POLLRDBAND | POLLIN | POLLHUP | POLLERR)
#define POLLOUT_SET (POLLWRBAND | POLLWRNORM | POLLOUT | POLLERR)
#define POLLEX_SET (POLLPRI)

int do_select(int n, fd_set_bits *fds, long *timeout)
{
	poll_table table, *wait;
	int retval, off, maxoff;
	long __timeout = *timeout;

	poll_initwait(&table);
	wait = &table;
	if (!__timeout)
		wait = NULL;
	
	retval = 0;
	maxoff = FDS_LONGS(n);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		for (off = 0; off <= maxoff; off++) { 
			unsigned long val = BITS(fds, off); 

			while (val) { 
				int k = ffz(~val), index; 
				unsigned long mask, bit;
				struct file *file;

				bit = (1UL << k); 
				val &= ~bit; 

				index = off*BITS_PER_LONG + k;
				if (index >= n) 
					break;

				file = fget(index);
				mask = POLLNVAL;
				if (file) {
					mask = DEFAULT_POLLMASK;
					if (file->f_op && file->f_op->poll)
						mask = file->f_op->poll(file, wait);
					fput(file);
				} else { 
					/* This error will shadow all other results. 
					 * This matches previous linux behaviour */
					retval = -EBADF; 
					goto out; 
				} 
				if ((mask & POLLIN_SET) && ISSET(bit, __IN(fds,off))) {
					SET(bit, __RES_IN(fds,off));
					retval++;
					wait = NULL;
				}
				if ((mask & POLLOUT_SET) && ISSET(bit,__OUT(fds,off))) {
					SET(bit, __RES_OUT(fds,off));
					retval++;
					wait = NULL;
				}
				if ((mask & POLLEX_SET) && ISSET(bit, __EX(fds,off))) {
					SET(bit, __RES_EX(fds,off));
					retval++;
					wait = NULL;
				}
			}
		}

		wait = NULL;
		if (retval || !__timeout || signal_pending(current))
			break;
		if (table.error) {
			retval = table.error;
			break;
		}
		__timeout = schedule_timeout(__timeout);
	}

out:	
	current->state = TASK_RUNNING;

	poll_freewait(&table);

	/*
	 * Update the caller timeout.
	 */
	*timeout = __timeout;
	return retval;
}

/*
 * We do a VERIFY_WRITE here even though we are only reading this time:
 * we'll write to it eventually..
 */

static int get_fd_set(unsigned long nr, void *ufdset, unsigned long *fdset)
{
	unsigned long rounded = FDS_BYTES(nr);
	if (ufdset) {
		int error = verify_area(VERIFY_WRITE, ufdset, rounded);
		if (!error && __copy_from_user(fdset, ufdset, rounded))
			error = -EFAULT;
		if (nr % __NFDBITS) {
			unsigned long mask = ~(~0UL << (nr % __NFDBITS)); 
			fdset[nr/__NFDBITS] &= mask; 
		} 
		return error;
	}
	memset(fdset, 0, rounded);
	return 0;
}

/*
 * We can actually return ERESTARTSYS instead of EINTR, but I'd
 * like to be certain this leads to no problems. So I return
 * EINTR just for safety.
 *
 * Update: ERESTARTSYS breaks at least the xview clock binary, so
 * I'm trying ERESTARTNOHAND which restart only when you want to.
 */
#define MAX_SELECT_SECONDS \
	((unsigned long) (MAX_SCHEDULE_TIMEOUT / HZ)-1)

asmlinkage long
sys_select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp)
{
	fd_set_bits fds;
	unsigned long *bits;
	long timeout;
	int ret, size, max_fdset;
	unsigned long stack_bits[FDS_LONGS(FAST_SELECT_MAX) * 6]; 

	timeout = MAX_SCHEDULE_TIMEOUT;
	if (tvp) {
		time_t sec, usec;

		if ((ret = verify_area(VERIFY_READ, tvp, sizeof(*tvp)))
		    || (ret = __get_user(sec, &tvp->tv_sec))
		    || (ret = __get_user(usec, &tvp->tv_usec)))
			goto out_nofds;

		ret = -EINVAL;
		if (sec < 0 || usec < 0)
			goto out_nofds;

		if ((unsigned long) sec < MAX_SELECT_SECONDS) {
			timeout = ROUND_UP(usec, 1000000/HZ);
			timeout += sec * (unsigned long) HZ;
		}
	}

	ret = -EINVAL;
	if (n < 0)
		goto out_nofds;

	/* max_fdset can increase, so grab it once to avoid race */
	max_fdset = current->files->max_fdset;
	if (n > max_fdset)
		n = max_fdset;

	/*
	 * We need 6 bitmaps (in/out/ex for both incoming and outgoing),
	 * since we used fdset we need to allocate memory in units of
	 * long-words. 
	 */
	size = FDS_LONGS(n);
	bits = stack_bits;
	if (n >= FAST_SELECT_MAX) { 
		ret = -ENOMEM;
		bits = kmalloc(sizeof(unsigned long)*6*size, GFP_KERNEL);
		if (!bits)
			goto out_nofds;
	} 

	fds.in      = bits;
	fds.out     = bits +   size;
	fds.ex      = bits + 2*size;
	fds.res_in  = bits + 3*size;
	fds.res_out = bits + 4*size;
	fds.res_ex  = bits + 5*size;

	if ((ret = get_fd_set(n, inp, fds.in)) ||
	    (ret = get_fd_set(n, outp, fds.out)) ||
	    (ret = get_fd_set(n, exp, fds.ex)))
		goto out;
	memset(fds.res_in, 0, 3*size); 

	ret = do_select(n, &fds, &timeout);

	if (tvp && !(current->personality & STICKY_TIMEOUTS)) {
		time_t sec = 0, usec = 0;
		if (timeout) {
			sec = timeout / HZ;
			usec = timeout % HZ;
			usec *= (1000000/HZ);
		}
		__put_user(sec, &tvp->tv_sec);
		__put_user(usec, &tvp->tv_usec);
	}

	if (ret < 0)
		goto out;
	if (!ret) {
		ret = -ERESTARTNOHAND;
		if (signal_pending(current))
			goto out;
		ret = 0;
	}

	set_fd_set(n, inp, fds.res_in);
	set_fd_set(n, outp, fds.res_out);
	set_fd_set(n, exp, fds.res_ex);

out:
	if (n >= FAST_SELECT_MAX) 
		kfree(bits);
out_nofds:

	return ret;
}

#define POLLFD_PER_PAGE  ((PAGE_SIZE) / sizeof(struct pollfd))

static void do_pollfd(unsigned int num, struct pollfd * fdpage,
	poll_table ** pwait, int *count)
{
	int i;

	for (i = 0; i < num; i++) {
		int fd;
		unsigned int mask;
		struct pollfd *fdp;

		mask = 0;
		fdp = fdpage+i;
		fd = fdp->fd;
		if (fd >= 0) {
			struct file * file = fget(fd);
			mask = POLLNVAL;
			if (file != NULL) {
				mask = DEFAULT_POLLMASK;
				if (file->f_op && file->f_op->poll)
					mask = file->f_op->poll(file, *pwait);
				mask &= fdp->events | POLLERR | POLLHUP;
				fput(file);
			}
			if (mask) {
				*pwait = NULL;
				(*count)++;
			}
		}
		fdp->revents = mask;
	}
}

static int do_poll(unsigned int nfds, unsigned int nchunks, unsigned int nleft, 
	struct pollfd *fds[], poll_table *wait, long timeout)
{
	int count;
	poll_table* pt = wait;

	for (;;) {
		unsigned int i;

		set_current_state(TASK_INTERRUPTIBLE);
		count = 0;
		for (i=0; i < nchunks; i++)
			do_pollfd(POLLFD_PER_PAGE, fds[i], &pt, &count);
		if (nleft)
			do_pollfd(nleft, fds[nchunks], &pt, &count);
		pt = NULL;
		if (count || !timeout || signal_pending(current))
			break;
		count = wait->error;
		if (count)
			break;
		timeout = schedule_timeout(timeout);
	}
	current->state = TASK_RUNNING;
	return count;
}

static int fast_poll(poll_table *table, poll_table *wait, struct pollfd *ufds, 
		     unsigned int nfds, long timeout)
{ 
	poll_table *pt = wait; 
	struct pollfd fds[FAST_POLL_MAX];
	int count, i; 

	if (copy_from_user(fds, ufds, nfds * sizeof(struct pollfd)))
		return -EFAULT; 
	for (;;) { 
		set_current_state(TASK_INTERRUPTIBLE);
		count = 0; 
		do_pollfd(nfds, fds, &pt, &count); 
		pt = NULL;
		if (count || !timeout || signal_pending(current))
			break;
		count = wait->error; 
		if (count) 
			break; 		
		timeout = schedule_timeout(timeout);
	} 
	current->state = TASK_RUNNING;
	for (i = 0; i < nfds; i++) 
		__put_user(fds[i].revents, &ufds[i].revents);
	poll_freewait(table);	
	if (!count && signal_pending(current)) 
		return -EINTR; 
	return count; 
} 

asmlinkage long sys_poll(struct pollfd * ufds, unsigned int nfds, long timeout)
{
	int i, j, err, fdcount;
	struct pollfd **fds;
	poll_table table, *wait;
	int nchunks, nleft; 

	/* Do a sanity check on nfds ... */
	if (nfds > NR_OPEN)
		return -EINVAL;

	if (timeout) {
		/* Careful about overflow in the intermediate values */
		if ((unsigned long) timeout < MAX_SCHEDULE_TIMEOUT / HZ)
			timeout = (unsigned long)(timeout*HZ+999)/1000+1;
		else /* Negative or overflow */
			timeout = MAX_SCHEDULE_TIMEOUT;
	}


	poll_initwait(&table);
	wait = &table;
	if (!timeout)
		wait = NULL;

	if (nfds < FAST_POLL_MAX) 
		return fast_poll(&table, wait, ufds, nfds, timeout); 

	err = -ENOMEM;
	fds = (struct pollfd **)kmalloc(
		(1 + (nfds - 1) / POLLFD_PER_PAGE) * sizeof(struct pollfd *),
		GFP_KERNEL);
	if (fds == NULL)
		goto out;
	
	nchunks = 0;
	nleft = nfds;
	while (nleft > POLLFD_PER_PAGE) { 
		fds[nchunks] = (struct pollfd *)__get_free_page(GFP_KERNEL);
		if (fds[nchunks] == NULL)
			goto out_fds;
		nchunks++;
		nleft -= POLLFD_PER_PAGE;
	}
	if (nleft) { 
		fds[nchunks] = (struct pollfd *)__get_free_page(GFP_KERNEL);
		if (fds[nchunks] == NULL)
			goto out_fds;
	} 
	
	err = -EFAULT;
	for (i=0; i < nchunks; i++)
		if (copy_from_user(fds[i], ufds + i*POLLFD_PER_PAGE, PAGE_SIZE))
			goto out_fds1;
	
	if (nleft) {
		if (copy_from_user(fds[nchunks], ufds + nchunks*POLLFD_PER_PAGE, 
				   nleft * sizeof(struct pollfd)))
			goto out_fds1;
	}

	fdcount = do_poll(nfds, nchunks, nleft, fds, wait, timeout);

	/* OK, now copy the revents fields back to user space. */
	for(i=0; i < nchunks; i++)
		for (j=0; j < POLLFD_PER_PAGE; j++, ufds++)
			__put_user((fds[i] + j)->revents, &ufds->revents);
	if (nleft)
		for (j=0; j < nleft; j++, ufds++)
			__put_user((fds[nchunks] + j)->revents, &ufds->revents);

	err = fdcount;
	if (!fdcount && signal_pending(current))
		err = -EINTR;

out_fds1:
	if (nleft)
		free_page((unsigned long)(fds[nchunks]));
out_fds:
	for (i=0; i < nchunks; i++)
		free_page((unsigned long)(fds[i]));
	kfree(fds);
out:
	poll_freewait(&table);
	return err;
}

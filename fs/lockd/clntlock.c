/*
 * linux/fs/lockd/clntlock.c
 *
 * Lock handling for the client side NLM implementation
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#define __KERNEL_SYSCALLS__

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/nfs_fs.h>
#include <linux/unistd.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/lockd/lockd.h>
#include <linux/smp_lock.h>

#define NLMDBG_FACILITY		NLMDBG_CIENT

/*
 * Local function prototypes
 */
static int			reclaimer(void *ptr);

/*
 * The following functions handle blocking and granting from the
 * client perspective.
 */

/*
 * This is the representation of a blocked client lock.
 */
struct nlm_wait {
	struct nlm_wait *	b_next;		/* linked list */
	wait_queue_head_t	b_wait;		/* where to wait on */
	struct nlm_host *	b_host;
	struct file_lock *	b_lock;		/* local file lock */
	unsigned short		b_reclaim;	/* got to reclaim lock */
	u32			b_status;	/* grant callback status */
};

static struct nlm_wait *	nlm_blocked;

/*
 * Block on a lock
 */
int
nlmclnt_block(struct nlm_host *host, struct file_lock *fl, u32 *statp)
{
	struct nlm_wait	block, **head;
	int		err;
	u32		pstate;

	block.b_host   = host;
	block.b_lock   = fl;
	init_waitqueue_head(&block.b_wait);
	block.b_status = NLM_LCK_BLOCKED;
	block.b_next   = nlm_blocked;
	nlm_blocked    = &block;

	/* Remember pseudo nsm state */
	pstate = host->h_state;

	/* Go to sleep waiting for GRANT callback. Some servers seem
	 * to lose callbacks, however, so we're going to poll from
	 * time to time just to make sure.
	 *
	 * For now, the retry frequency is pretty high; normally 
	 * a 1 minute timeout would do. See the comment before
	 * nlmclnt_lock for an explanation.
	 */
	sleep_on_timeout(&block.b_wait, 30*HZ);

	for (head = &nlm_blocked; *head; head = &(*head)->b_next) {
		if (*head == &block) {
			*head = block.b_next;
			break;
		}
	}

	if (!signalled()) {
		*statp = block.b_status;
		return 0;
	}

	/* Okay, we were interrupted. Cancel the pending request
	 * unless the server has rebooted.
	 */
	if (pstate == host->h_state && (err = nlmclnt_cancel(host, fl)) < 0)
		printk(KERN_NOTICE
			"lockd: CANCEL call failed (errno %d)\n", -err);

	return -ERESTARTSYS;
}

/*
 * The server lockd has called us back to tell us the lock was granted
 */
u32
nlmclnt_grant(struct nlm_lock *lock)
{
	struct nlm_wait	*block;

	/*
	 * Look up blocked request based on arguments. 
	 * Warning: must not use cookie to match it!
	 */
	for (block = nlm_blocked; block; block = block->b_next) {
		if (nlm_compare_locks(block->b_lock, &lock->fl))
			break;
	}

	/* Ooops, no blocked request found. */
	if (block == NULL)
		return nlm_lck_denied;

	/* Alright, we found the lock. Set the return status and
	 * wake up the caller.
	 */
	block->b_status = NLM_LCK_GRANTED;
	wake_up(&block->b_wait);

	return nlm_granted;
}

/*
 * The following procedures deal with the recovery of locks after a
 * server crash.
 */

/*
 * Reclaim all locks on server host. We do this by spawning a separate
 * reclaimer thread.
 * FIXME: should bump MOD_USE_COUNT while reclaiming
 */
void
nlmclnt_recovery(struct nlm_host *host, u32 newstate)
{
	if (!host->h_reclaiming++) {
		if (host->h_nsmstate == newstate)
			return;
		printk(KERN_WARNING
			"lockd: Uh-oh! Interfering reclaims for host %s",
			host->h_name);
		host->h_monitored = 0;
		host->h_nsmstate = newstate;
		host->h_state++;
		nlm_release_host(host);
	} else {
		host->h_monitored = 0;
		host->h_nsmstate = newstate;
		host->h_state++;
		nlm_get_host(host);
		kernel_thread(reclaimer, host, 0);
	}
}

static int
reclaimer(void *ptr)
{
	struct nlm_host	  *host = (struct nlm_host *) ptr;
	struct nlm_wait	  *block;
	struct list_head *tmp;

	/* This one ensures that our parent doesn't terminate while the
	 * reclaim is in progress */
	lock_kernel();
	lockd_up();

	/* First, reclaim all locks that have been granted previously. */
restart:
	tmp = file_lock_list.next;
	while (tmp != &file_lock_list) {
		struct file_lock *fl = list_entry(tmp, struct file_lock, fl_link);
		struct inode *inode = fl->fl_file->f_dentry->d_inode;
		if (inode->i_sb->s_magic == NFS_SUPER_MAGIC &&
				nlm_cmp_addr(NFS_ADDR(inode), &host->h_addr) &&
				fl->fl_u.nfs_fl.state != host->h_state &&
				(fl->fl_u.nfs_fl.flags & NFS_LCK_GRANTED)) {
			fl->fl_u.nfs_fl.flags &= ~ NFS_LCK_GRANTED;
			nlmclnt_reclaim(host, fl);	/* This sleeps */
			goto restart;
		}
		tmp = tmp->next;
	}

	host->h_reclaiming = 0;
	wake_up(&host->h_gracewait);

	/* Now, wake up all processes that sleep on a blocked lock */
	for (block = nlm_blocked; block; block = block->b_next) {
		if (block->b_host == host) {
			block->b_status = NLM_LCK_DENIED_GRACE_PERIOD;
			wake_up(&block->b_wait);
		}
	}

	/* Release host handle after use */
	nlm_release_host(host);
	lockd_down();
	unlock_kernel();

	return 0;
}

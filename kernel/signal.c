/*
 *  linux/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-02  Modified for POSIX.1b signals by Richard Henderson
 */

#define __KERNEL_SYSCALLS__

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/unistd.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/binfmts.h>
#include <asm/param.h>
#include <asm/uaccess.h>
#include <asm/siginfo.h>

/*
 * SLAB caches for signal bits.
 */

static kmem_cache_t *sigqueue_cachep;

atomic_t nr_queued_signals;
int max_queued_signals = 1024;

/*********************************************************

    POSIX thread group signal behavior:

----------------------------------------------------------
|                    |  userspace       |  kernel        |
----------------------------------------------------------
|  SIGHUP            |  load-balance    |  kill-all      |
|  SIGINT            |  load-balance    |  kill-all      |
|  SIGQUIT           |  load-balance    |  kill-all+core |
|  SIGILL            |  specific        |  kill-all+core |
|  SIGTRAP           |  specific        |  kill-all+core |
|  SIGABRT/SIGIOT    |  specific        |  kill-all+core |
|  SIGBUS            |  specific        |  kill-all+core |
|  SIGFPE            |  specific        |  kill-all+core |
|  SIGKILL           |  n/a             |  kill-all      |
|  SIGUSR1           |  load-balance    |  kill-all      |
|  SIGSEGV           |  specific        |  kill-all+core |
|  SIGUSR2           |  load-balance    |  kill-all      |
|  SIGPIPE           |  specific        |  kill-all      |
|  SIGALRM           |  load-balance    |  kill-all      |
|  SIGTERM           |  load-balance    |  kill-all      |
|  SIGCHLD           |  load-balance    |  ignore        |
|  SIGCONT           |  specific        |  continue-all  |
|  SIGSTOP           |  n/a             |  stop-all      |
|  SIGTSTP           |  load-balance    |  stop-all      |
|  SIGTTIN           |  load-balance    |  stop-all      |
|  SIGTTOU           |  load-balance    |  stop-all      |
|  SIGURG            |  load-balance    |  ignore        |
|  SIGXCPU           |  specific        |  kill-all+core |
|  SIGXFSZ           |  specific        |  kill-all+core |
|  SIGVTALRM         |  load-balance    |  kill-all      |
|  SIGPROF           |  specific        |  kill-all      |
|  SIGPOLL/SIGIO     |  load-balance    |  kill-all      |
|  SIGSYS/SIGUNUSED  |  specific        |  kill-all+core |
|  SIGSTKFLT         |  specific        |  kill-all      |
|  SIGWINCH          |  load-balance    |  ignore        |
|  SIGPWR            |  load-balance    |  kill-all      |
|  SIGRTMIN-SIGRTMAX |  load-balance    |  kill-all      |
----------------------------------------------------------
*/

/* Some systems do not have a SIGSTKFLT and the kernel never
 * generates such signals anyways.
 */
#ifdef SIGSTKFLT
#define M_SIGSTKFLT	M(SIGSTKFLT)
#else
#define M_SIGSTKFLT	0
#endif

#define M(sig) (1UL << (sig))

#define SIG_USER_SPECIFIC_MASK (\
	M(SIGILL)    |  M(SIGTRAP)   |  M(SIGABRT)   |  M(SIGBUS)    | \
	M(SIGFPE)    |  M(SIGSEGV)   |  M(SIGPIPE)   |  M(SIGXFSZ)   | \
	M(SIGPROF)   |  M(SIGSYS)    |  M_SIGSTKFLT |  M(SIGCONT)   )

#define SIG_USER_LOAD_BALANCE_MASK (\
        M(SIGHUP)    |  M(SIGINT)    |  M(SIGQUIT)   |  M(SIGUSR1)   | \
        M(SIGUSR2)   |  M(SIGALRM)   |  M(SIGTERM)   |  M(SIGCHLD)   | \
        M(SIGURG)    |  M(SIGVTALRM) |  M(SIGPOLL)   |  M(SIGWINCH)  | \
        M(SIGPWR)    |  M(SIGTSTP)   |  M(SIGTTIN)   |  M(SIGTTOU)   )

#define SIG_KERNEL_SPECIFIC_MASK (\
        M(SIGCHLD)   |   M(SIGURG)   |  M(SIGWINCH)                  )

#define SIG_KERNEL_BROADCAST_MASK (\
	M(SIGHUP)    |  M(SIGINT)    |  M(SIGQUIT)   |  M(SIGILL)    | \
	M(SIGTRAP)   |  M(SIGABRT)   |  M(SIGBUS)    |  M(SIGFPE)    | \
	M(SIGKILL)   |  M(SIGUSR1)   |  M(SIGSEGV)   |  M(SIGUSR2)   | \
	M(SIGPIPE)   |  M(SIGALRM)   |  M(SIGTERM)   |  M(SIGXCPU)   | \
	M(SIGXFSZ)   |  M(SIGVTALRM) |  M(SIGPROF)   |  M(SIGPOLL)   | \
	M(SIGSYS)    |  M_SIGSTKFLT  |  M(SIGPWR)    |  M(SIGCONT)   | \
        M(SIGSTOP)   |  M(SIGTSTP)   |  M(SIGTTIN)   |  M(SIGTTOU)   )

#define SIG_KERNEL_ONLY_MASK (\
	M(SIGKILL)   |  M(SIGSTOP)                                   )

#define SIG_KERNEL_COREDUMP_MASK (\
        M(SIGQUIT)   |  M(SIGILL)    |  M(SIGTRAP)   |  M(SIGABRT)   | \
        M(SIGFPE)    |  M(SIGSEGV)   |  M(SIGBUS)    |  M(SIGSYS)    | \
        M(SIGXCPU)   |  M(SIGXFSZ)                                   )

#define T(sig, mask) \
	((1UL << (sig)) & mask)

#define sig_user_specific(sig) \
		(((sig) < SIGRTMIN)  && T(sig, SIG_USER_SPECIFIC_MASK))
#define sig_user_load_balance(sig) \
		(((sig) >= SIGRTMIN) || T(sig, SIG_USER_LOAD_BALANCE_MASK))
#define sig_kernel_specific(sig) \
		(((sig) < SIGRTMIN)  && T(sig, SIG_KERNEL_SPECIFIC_MASK))
#define sig_kernel_broadcast(sig) \
		(((sig) >= SIGRTMIN) || T(sig, SIG_KERNEL_BROADCAST_MASK))
#define sig_kernel_only(sig) \
		(((sig) < SIGRTMIN)  && T(sig, SIG_KERNEL_ONLY_MASK))
#define sig_kernel_coredump(sig) \
		(((sig) < SIGRTMIN)  && T(sig, SIG_KERNEL_COREDUMP_MASK))

#define sig_user_defined(t, sig) \
	(((t)->sig->action[(sig)-1].sa.sa_handler != SIG_DFL) &&	\
	 ((t)->sig->action[(sig)-1].sa.sa_handler != SIG_IGN))

#define sig_ignored(t, sig) \
	(((sig) != SIGCHLD) && \
		((t)->sig->action[(sig)-1].sa.sa_handler == SIG_IGN))

void __init signals_init(void)
{
	sigqueue_cachep =
		kmem_cache_create("sigqueue",
				  sizeof(struct sigqueue),
				  __alignof__(struct sigqueue),
				  0, NULL, NULL);
	if (!sigqueue_cachep)
		panic("signals_init(): cannot create sigqueue SLAB cache");
}

#define PENDING(p,b) has_pending_signals(&(p)->signal, (b))

void recalc_sigpending_tsk(struct task_struct *t)
{
	if (PENDING(&t->pending, &t->blocked) ||
			PENDING(&t->sig->shared_pending, &t->blocked))
		set_tsk_thread_flag(t, TIF_SIGPENDING);
	else
		clear_tsk_thread_flag(t, TIF_SIGPENDING);
}

void recalc_sigpending(void)
{
	if (PENDING(&current->pending, &current->blocked) ||
		    PENDING(&current->sig->shared_pending, &current->blocked))
		set_thread_flag(TIF_SIGPENDING);
	else
		clear_thread_flag(TIF_SIGPENDING);
}

/* Given the mask, find the first available signal that should be serviced. */

static int
next_signal(struct sigpending *pending, sigset_t *mask)
{
	unsigned long i, *s, *m, x;
	int sig = 0;
	
	s = pending->signal.sig;
	m = mask->sig;
	switch (_NSIG_WORDS) {
	default:
		for (i = 0; i < _NSIG_WORDS; ++i, ++s, ++m)
			if ((x = *s &~ *m) != 0) {
				sig = ffz(~x) + i*_NSIG_BPW + 1;
				break;
			}
		break;

	case 2: if ((x = s[0] &~ m[0]) != 0)
			sig = 1;
		else if ((x = s[1] &~ m[1]) != 0)
			sig = _NSIG_BPW + 1;
		else
			break;
		sig += ffz(~x);
		break;

	case 1: if ((x = *s &~ *m) != 0)
			sig = ffz(~x) + 1;
		break;
	}
	
	return sig;
}

static void flush_sigqueue(struct sigpending *queue)
{
	struct sigqueue *q, *n;

	sigemptyset(&queue->signal);
	q = queue->head;
	queue->head = NULL;
	queue->tail = &queue->head;

	while (q) {
		n = q->next;
		kmem_cache_free(sigqueue_cachep, q);
		atomic_dec(&nr_queued_signals);
		q = n;
	}
}

/*
 * Flush all pending signals for a task.
 */

void
flush_signals(struct task_struct *t)
{
	clear_tsk_thread_flag(t,TIF_SIGPENDING);
	flush_sigqueue(&t->pending);
}

static inline void __remove_thread_group(struct task_struct *tsk, struct signal_struct *sig)
{
	if (tsk == sig->curr_target)
		sig->curr_target = next_thread(tsk);
	list_del_init(&tsk->thread_group);
}

void remove_thread_group(struct task_struct *tsk, struct signal_struct *sig)
{
	write_lock_irq(&tasklist_lock);
	spin_lock(&tsk->sig->siglock);

	__remove_thread_group(tsk, sig);

	spin_unlock(&tsk->sig->siglock);
	write_unlock_irq(&tasklist_lock);
}

/*
 * This function expects the tasklist_lock write-locked.
 */
void __exit_sighand(struct task_struct *tsk)
{
	struct signal_struct * sig = tsk->sig;

	if (!sig)
		BUG();
	if (!atomic_read(&sig->count))
		BUG();
	spin_lock(&sig->siglock);
	spin_lock(&tsk->sigmask_lock);
	tsk->sig = NULL;
	if (atomic_dec_and_test(&sig->count)) {
		__remove_thread_group(tsk, sig);
		spin_unlock(&sig->siglock);
		flush_sigqueue(&sig->shared_pending);
		kmem_cache_free(sigact_cachep, sig);
	} else {
		struct task_struct *leader = tsk->group_leader;
		/*
		 * If we are the last non-leader member of the thread
		 * group, and the leader is zombie, then notify the
		 * group leader's parent process.
		 *
		 * (subtle: here we also rely on the fact that if we are the
		 *  thread group leader then we are not zombied yet.)
		 */
		if (atomic_read(&sig->count) == 1 &&
					leader->state == TASK_ZOMBIE) {
			__remove_thread_group(tsk, sig);
			spin_unlock(&sig->siglock);
			do_notify_parent(leader, leader->exit_signal);
		} else {
			__remove_thread_group(tsk, sig);
			spin_unlock(&sig->siglock);
		}
	}
	clear_tsk_thread_flag(tsk,TIF_SIGPENDING);
	flush_sigqueue(&tsk->pending);

	spin_unlock(&tsk->sigmask_lock);
}

void exit_sighand(struct task_struct *tsk)
{
	write_lock_irq(&tasklist_lock);
	__exit_sighand(tsk);
	write_unlock_irq(&tasklist_lock);
}

/*
 * Flush all handlers for a task.
 */

void
flush_signal_handlers(struct task_struct *t)
{
	int i;
	struct k_sigaction *ka = &t->sig->action[0];
	for (i = _NSIG ; i != 0 ; i--) {
		if (ka->sa.sa_handler != SIG_IGN)
			ka->sa.sa_handler = SIG_DFL;
		ka->sa.sa_flags = 0;
		sigemptyset(&ka->sa.sa_mask);
		ka++;
	}
}

/*
 * sig_exit - cause the current task to exit due to a signal.
 */

void
sig_exit(int sig, int exit_code, struct siginfo *info)
{
	sigaddset(&current->pending.signal, sig);
	recalc_sigpending();
	current->flags |= PF_SIGNALED;

	if (current->sig->group_exit)
		exit_code = current->sig->group_exit_code;

	do_exit(exit_code);
	/* NOTREACHED */
}

/* Notify the system that a driver wants to block all signals for this
 * process, and wants to be notified if any signals at all were to be
 * sent/acted upon.  If the notifier routine returns non-zero, then the
 * signal will be acted upon after all.  If the notifier routine returns 0,
 * then then signal will be blocked.  Only one block per process is
 * allowed.  priv is a pointer to private data that the notifier routine
 * can use to determine if the signal should be blocked or not.  */

void
block_all_signals(int (*notifier)(void *priv), void *priv, sigset_t *mask)
{
	unsigned long flags;

	spin_lock_irqsave(&current->sigmask_lock, flags);
	current->notifier_mask = mask;
	current->notifier_data = priv;
	current->notifier = notifier;
	spin_unlock_irqrestore(&current->sigmask_lock, flags);
}

/* Notify the system that blocking has ended. */

void
unblock_all_signals(void)
{
	unsigned long flags;

	spin_lock_irqsave(&current->sigmask_lock, flags);
	current->notifier = NULL;
	current->notifier_data = NULL;
	recalc_sigpending();
	spin_unlock_irqrestore(&current->sigmask_lock, flags);
}

static inline int collect_signal(int sig, struct sigpending *list, siginfo_t *info)
{
	if (sigismember(&list->signal, sig)) {
		/* Collect the siginfo appropriate to this signal.  */
		struct sigqueue *q, **pp;
		pp = &list->head;
		while ((q = *pp) != NULL) {
			if (q->info.si_signo == sig)
				goto found_it;
			pp = &q->next;
		}

		/* Ok, it wasn't in the queue.  This must be
		   a fast-pathed signal or we must have been
		   out of queue space.  So zero out the info.
		 */
		sigdelset(&list->signal, sig);
		info->si_signo = sig;
		info->si_errno = 0;
		info->si_code = 0;
		info->si_pid = 0;
		info->si_uid = 0;
		return 1;

found_it:
		if ((*pp = q->next) == NULL)
			list->tail = pp;

		/* Copy the sigqueue information and free the queue entry */
		copy_siginfo(info, &q->info);
		kmem_cache_free(sigqueue_cachep,q);
		atomic_dec(&nr_queued_signals);

		/* Non-RT signals can exist multiple times.. */
		if (sig >= SIGRTMIN) {
			while ((q = *pp) != NULL) {
				if (q->info.si_signo == sig)
					goto found_another;
				pp = &q->next;
			}
		}

		sigdelset(&list->signal, sig);
found_another:
		return 1;
	}
	return 0;
}

/*
 * Dequeue a signal and return the element to the caller, which is 
 * expected to free it.
 *
 * All callers have to hold the siglock and the sigmask_lock.
 */

int dequeue_signal(struct sigpending *pending, sigset_t *mask, siginfo_t *info)
{
	int sig = 0;

	sig = next_signal(pending, mask);
	if (sig) {
		if (current->notifier) {
			if (sigismember(current->notifier_mask, sig)) {
				if (!(current->notifier)(current->notifier_data)) {
					clear_thread_flag(TIF_SIGPENDING);
					return 0;
				}
			}
		}

		if (!collect_signal(sig, pending, info))
			sig = 0;
				
		/* XXX: Once POSIX.1b timers are in, if si_code == SI_TIMER,
		   we need to xchg out the timer overrun values.  */
	}
	recalc_sigpending();

	return sig;
}

static int rm_from_queue(int sig, struct sigpending *s)
{
	struct sigqueue *q, **pp;

	if (!sigismember(&s->signal, sig))
		return 0;

	sigdelset(&s->signal, sig);

	pp = &s->head;

	while ((q = *pp) != NULL) {
		if (q->info.si_signo == sig) {
			if ((*pp = q->next) == NULL)
				s->tail = pp;
			kmem_cache_free(sigqueue_cachep,q);
			atomic_dec(&nr_queued_signals);
			continue;
		}
		pp = &q->next;
	}
	return 1;
}

/*
 * Remove signal sig from t->pending.
 * Returns 1 if sig was found.
 *
 * All callers must be holding t->sigmask_lock.
 */
static int rm_sig_from_queue(int sig, struct task_struct *t)
{
	return rm_from_queue(sig, &t->pending);
}

/*
 * Bad permissions for sending the signal
 */
static inline int bad_signal(int sig, struct siginfo *info, struct task_struct *t)
{
	return (!info || ((unsigned long)info != 1 &&
			(unsigned long)info != 2 && SI_FROMUSER(info)))
	    && ((sig != SIGCONT) || (current->session != t->session))
	    && (current->euid ^ t->suid) && (current->euid ^ t->uid)
	    && (current->uid ^ t->suid) && (current->uid ^ t->uid)
	    && !capable(CAP_KILL);
}

/*
 * Signal type:
 *    < 0 : global action (kill - spread to all non-blocked threads)
 *    = 0 : ignored
 *    > 0 : wake up.
 */
static int signal_type(int sig, struct signal_struct *signals)
{
	unsigned long handler;

	if (!signals)
		return 0;
	
	handler = (unsigned long) signals->action[sig-1].sa.sa_handler;
	if (handler > 1)
		return 1;

	/* "Ignore" handler.. Illogical, but that has an implicit handler for SIGCHLD */
	if (handler == 1)
		return sig == SIGCHLD;

	/* Default handler. Normally lethal, but.. */
	switch (sig) {

	/* Ignored */
	case SIGCONT: case SIGWINCH:
	case SIGCHLD: case SIGURG:
		return 0;

	/* Implicit behaviour */
	case SIGTSTP: case SIGTTIN: case SIGTTOU:
		return 1;

	/* Implicit actions (kill or do special stuff) */
	default:
		return -1;
	}
}
		

/*
 * Determine whether a signal should be posted or not.
 *
 * Signals with SIG_IGN can be ignored, except for the
 * special case of a SIGCHLD. 
 *
 * Some signals with SIG_DFL default to a non-action.
 */
static int ignored_signal(int sig, struct task_struct *t)
{
	/* Don't ignore traced or blocked signals */
	if ((t->ptrace & PT_PTRACED) || sigismember(&t->blocked, sig))
		return 0;

	return signal_type(sig, t->sig) == 0;
}

/*
 * Handle TASK_STOPPED cases etc implicit behaviour
 * of certain magical signals.
 *
 * SIGKILL gets spread out to every thread. 
 */
static void handle_stop_signal(int sig, struct task_struct *t)
{
	switch (sig) {
	case SIGKILL: case SIGCONT:
		/* Wake up the process if stopped.  */
		if (t->state == TASK_STOPPED)
			wake_up_process(t);
		t->exit_code = 0;
		rm_sig_from_queue(SIGSTOP, t);
		rm_sig_from_queue(SIGTSTP, t);
		rm_sig_from_queue(SIGTTOU, t);
		rm_sig_from_queue(SIGTTIN, t);
		break;

	case SIGSTOP: case SIGTSTP:
	case SIGTTIN: case SIGTTOU:
		/* If we're stopping again, cancel SIGCONT */
		rm_sig_from_queue(SIGCONT, t);
		break;
	}
}

static int send_signal(int sig, struct siginfo *info, struct sigpending *signals)
{
	struct sigqueue * q = NULL;

	/*
	 * fast-pathed signals for kernel-internal things like SIGSTOP
	 * or SIGKILL.
	 */
	if ((unsigned long)info == 2)
		goto out_set;

	/* Real-time signals must be queued if sent by sigqueue, or
	   some other real-time mechanism.  It is implementation
	   defined whether kill() does so.  We attempt to do so, on
	   the principle of least surprise, but since kill is not
	   allowed to fail with EAGAIN when low on memory we just
	   make sure at least one signal gets delivered and don't
	   pass on the info struct.  */

	if (atomic_read(&nr_queued_signals) < max_queued_signals)
		q = kmem_cache_alloc(sigqueue_cachep, GFP_ATOMIC);

	if (q) {
		atomic_inc(&nr_queued_signals);
		q->next = NULL;
		*signals->tail = q;
		signals->tail = &q->next;
		switch ((unsigned long) info) {
			case 0:
				q->info.si_signo = sig;
				q->info.si_errno = 0;
				q->info.si_code = SI_USER;
				q->info.si_pid = current->pid;
				q->info.si_uid = current->uid;
				break;
			case 1:
				q->info.si_signo = sig;
				q->info.si_errno = 0;
				q->info.si_code = SI_KERNEL;
				q->info.si_pid = 0;
				q->info.si_uid = 0;
				break;
			default:
				copy_siginfo(&q->info, info);
				break;
		}
	} else if (sig >= SIGRTMIN && info && (unsigned long)info != 1
		   && info->si_code != SI_USER)
		/*
		 * Queue overflow, abort.  We may abort if the signal was rt
		 * and sent by user using something other than kill().
		 */
		return -EAGAIN;

out_set:
	sigaddset(&signals->signal, sig);
	return 0;
}

/*
 * Tell a process that it has a new active signal..
 *
 * NOTE! we rely on the previous spin_lock to
 * lock interrupts for us! We can only be called with
 * "sigmask_lock" held, and the local interrupt must
 * have been disabled when that got acquired!
 *
 * No need to set need_resched since signal event passing
 * goes through ->blocked
 */
inline void signal_wake_up(struct task_struct *t)
{
	set_tsk_thread_flag(t,TIF_SIGPENDING);

	/*
	 * If the task is running on a different CPU 
	 * force a reschedule on the other CPU to make
	 * it notice the new signal quickly.
	 *
	 * The code below is a tad loose and might occasionally
	 * kick the wrong CPU if we catch the process in the
	 * process of changing - but no harm is done by that
	 * other than doing an extra (lightweight) IPI interrupt.
	 */
	if (t->state == TASK_RUNNING)
		kick_if_running(t);
	if (t->state & TASK_INTERRUPTIBLE) {
		wake_up_process(t);
		return;
	}
}

static int deliver_signal(int sig, struct siginfo *info, struct task_struct *t)
{
	int retval = send_signal(sig, info, &t->pending);

	if (!retval && !sigismember(&t->blocked, sig))
		signal_wake_up(t);

	return retval;
}

static int
__send_sig_info(int sig, struct siginfo *info, struct task_struct *t, int shared)
{
	int ret;

	if (!irqs_disabled())
		BUG();
#if CONFIG_SMP
	if (!spin_is_locked(&t->sig->siglock))
		BUG();
#endif
	ret = -EINVAL;
	if (sig < 0 || sig > _NSIG)
		goto out_nolock;
	/* The somewhat baroque permissions check... */
	ret = -EPERM;
	if (bad_signal(sig, info, t))
		goto out_nolock;
	ret = security_ops->task_kill(t, info, sig);
	if (ret)
		goto out_nolock;

	/* The null signal is a permissions and process existence probe.
	   No signal is actually delivered.  Same goes for zombies. */
	ret = 0;
	if (!sig || !t->sig)
		goto out_nolock;

	spin_lock(&t->sigmask_lock);
	handle_stop_signal(sig, t);

	/* Optimize away the signal, if it's a signal that can be
	   handled immediately (ie non-blocked and untraced) and
	   that is ignored (either explicitly or by default).  */

	if (ignored_signal(sig, t))
		goto out;

#define LEGACY_QUEUE(sigptr, sig) \
	(((sig) < SIGRTMIN) && sigismember(&(sigptr)->signal, (sig)))

	if (!shared) {
		/* Support queueing exactly one non-rt signal, so that we
		   can get more detailed information about the cause of
		   the signal. */
		if (LEGACY_QUEUE(&t->pending, sig))
			goto out;

		ret = deliver_signal(sig, info, t);
	} else {
		if (LEGACY_QUEUE(&t->sig->shared_pending, sig))
			goto out;
		ret = send_signal(sig, info, &t->sig->shared_pending);
	}
out:
	spin_unlock(&t->sigmask_lock);
out_nolock:
	return ret;
}

/*
 * Force a signal that the process can't ignore: if necessary
 * we unblock the signal and change any SIG_IGN to SIG_DFL.
 */

int
force_sig_info(int sig, struct siginfo *info, struct task_struct *t)
{
	unsigned long int flags;

	spin_lock_irqsave(&t->sigmask_lock, flags);
	if (t->sig == NULL) {
		spin_unlock_irqrestore(&t->sigmask_lock, flags);
		return -ESRCH;
	}

	if (t->sig->action[sig-1].sa.sa_handler == SIG_IGN)
		t->sig->action[sig-1].sa.sa_handler = SIG_DFL;
	sigdelset(&t->blocked, sig);
	recalc_sigpending_tsk(t);
	spin_unlock_irqrestore(&t->sigmask_lock, flags);

	return send_sig_info(sig, (void *)1, t);
}

static int
__force_sig_info(int sig, struct task_struct *t)
{
	unsigned long int flags;

	spin_lock_irqsave(&t->sigmask_lock, flags);
	if (t->sig == NULL) {
		spin_unlock_irqrestore(&t->sigmask_lock, flags);
		return -ESRCH;
	}

	if (t->sig->action[sig-1].sa.sa_handler == SIG_IGN)
		t->sig->action[sig-1].sa.sa_handler = SIG_DFL;
	sigdelset(&t->blocked, sig);
	recalc_sigpending_tsk(t);
	spin_unlock_irqrestore(&t->sigmask_lock, flags);

	return __send_sig_info(sig, (void *)2, t, 0);
}

#define can_take_signal(p, sig)	\
	(((unsigned long) p->sig->action[sig-1].sa.sa_handler > 1) && \
	!sigismember(&p->blocked, sig) && (task_curr(p) || !signal_pending(p)))

static inline
int load_balance_thread_group(struct task_struct *p, int sig,
				struct siginfo *info)
{
	struct task_struct *tmp;
	int ret;

	/*
	 * if the specified thread is not blocking this signal
	 * then deliver it.
	 */
	if (can_take_signal(p, sig))
		return __send_sig_info(sig, info, p, 0);

	/*
	 * Otherwise try to find a suitable thread.
	 * If no such thread is found then deliver to
	 * the original thread.
	 */

	tmp = p->sig->curr_target;

	if (!tmp || tmp->tgid != p->tgid)
		/* restart balancing at this thread */
		p->sig->curr_target = p;

	else for (;;) {
		if (list_empty(&p->thread_group))
			BUG();
		if (!tmp || tmp->tgid != p->tgid)
			BUG();

		/*
		 * Do not send signals that are ignored or blocked,
		 * or to not-running threads that are overworked:
		 */
		if (!can_take_signal(tmp, sig)) {
			tmp = next_thread(tmp);
			p->sig->curr_target = tmp;
			if (tmp == p)
				break;
			continue;
		}
		ret = __send_sig_info(sig, info, tmp, 0);
		return ret;
	}
	/*
	 * No suitable thread was found - put the signal
	 * into the shared-pending queue.
	 */
	return __send_sig_info(sig, info, p, 1);
}

int __broadcast_thread_group(struct task_struct *p, int sig)
{
	struct task_struct *tmp;
	struct list_head *entry;
	int err = 0;

	/* send a signal to the head of the list */
	err = __force_sig_info(sig, p);

	/* send a signal to all members of the list */
	list_for_each(entry, &p->thread_group) {
		tmp = list_entry(entry, task_t, thread_group);
		err = __force_sig_info(sig, tmp);
	}
	return err;
}

int
send_sig_info(int sig, struct siginfo *info, struct task_struct *p)
{
	unsigned long flags;
	int ret = 0;

	if (!p)
		BUG();
	if (!p->sig)
		BUG();
	spin_lock_irqsave(&p->sig->siglock, flags);

	/* not a thread group - normal signal behavior */
	if (list_empty(&p->thread_group) || !sig)
		goto out_send;

	if (sig_user_defined(p, sig)) {
		if (sig_user_specific(sig))
			goto out_send;
		if (sig_user_load_balance(sig)) {
			ret = load_balance_thread_group(p, sig, info);
			goto out_unlock;
		}

		/* must not happen */
		BUG();
	}
	/* optimize away ignored signals: */
	if (sig_ignored(p, sig))
		goto out_unlock;

	/* blocked (or ptraced) signals get posted */
	spin_lock(&p->sigmask_lock);
	if ((p->ptrace & PT_PTRACED) || sigismember(&p->blocked, sig) ||
					sigismember(&p->real_blocked, sig)) {
		spin_unlock(&p->sigmask_lock);
		goto out_send;
	}
	spin_unlock(&p->sigmask_lock);

	if (sig_kernel_broadcast(sig) || sig_kernel_coredump(sig)) {
		ret = __broadcast_thread_group(p, sig);
		goto out_unlock;
	}
	if (sig_kernel_specific(sig))
		goto out_send;

	/* must not happen */
	BUG();
out_send:
	ret = __send_sig_info(sig, info, p, 0);
out_unlock:
	spin_unlock_irqrestore(&p->sig->siglock, flags);
	return ret;
}

/*
 * kill_pg_info() sends a signal to a process group: this is what the tty
 * control characters do (^C, ^Z etc)
 */

int __kill_pg_info(int sig, struct siginfo *info, pid_t pgrp)
{
	int retval = -EINVAL;
	if (pgrp > 0) {
		struct task_struct *p;

		retval = -ESRCH;
		for_each_task(p) {
			if (p->pgrp == pgrp && thread_group_leader(p)) {
				int err = send_sig_info(sig, info, p);
				if (retval)
					retval = err;
			}
		}
	}
	return retval;
}

int
kill_pg_info(int sig, struct siginfo *info, pid_t pgrp)
{
	int retval;

	read_lock(&tasklist_lock);
	retval = __kill_pg_info(sig, info, pgrp);
	read_unlock(&tasklist_lock);

	return retval;
}

/*
 * kill_sl_info() sends a signal to the session leader: this is used
 * to send SIGHUP to the controlling process of a terminal when
 * the connection is lost.
 */

int
kill_sl_info(int sig, struct siginfo *info, pid_t sess)
{
	int retval = -EINVAL;
	if (sess > 0) {
		struct task_struct *p;

		retval = -ESRCH;
		read_lock(&tasklist_lock);
		for_each_task(p) {
			if (p->leader && p->session == sess) {
				int err = send_sig_info(sig, info, p);
				if (retval)
					retval = err;
			}
		}
		read_unlock(&tasklist_lock);
	}
	return retval;
}

inline int
kill_proc_info(int sig, struct siginfo *info, pid_t pid)
{
	int error;
	struct task_struct *p;

	read_lock(&tasklist_lock);
	p = find_task_by_pid(pid);
	error = -ESRCH;
	if (p)
		error = send_sig_info(sig, info, p);
	read_unlock(&tasklist_lock);
	return error;
}


/*
 * kill_something_info() interprets pid in interesting ways just like kill(2).
 *
 * POSIX specifies that kill(-1,sig) is unspecified, but what we have
 * is probably wrong.  Should make it like BSD or SYSV.
 */

static int kill_something_info(int sig, struct siginfo *info, int pid)
{
	if (!pid) {
		return kill_pg_info(sig, info, current->pgrp);
	} else if (pid == -1) {
		int retval = 0, count = 0;
		struct task_struct * p;

		read_lock(&tasklist_lock);
		for_each_task(p) {
			if (p->pid > 1 && p != current && thread_group_leader(p)) {
				int err = send_sig_info(sig, info, p);
				++count;
				if (err != -EPERM)
					retval = err;
			}
		}
		read_unlock(&tasklist_lock);
		return count ? retval : -ESRCH;
	} else if (pid < 0) {
		return kill_pg_info(sig, info, -pid);
	} else {
		return kill_proc_info(sig, info, pid);
	}
}

/*
 * These are for backward compatibility with the rest of the kernel source.
 */

int
send_sig(int sig, struct task_struct *p, int priv)
{
	return send_sig_info(sig, (void*)(long)(priv != 0), p);
}

void
force_sig(int sig, struct task_struct *p)
{
	force_sig_info(sig, (void*)1L, p);
}

int
kill_pg(pid_t pgrp, int sig, int priv)
{
	return kill_pg_info(sig, (void *)(long)(priv != 0), pgrp);
}

int
kill_sl(pid_t sess, int sig, int priv)
{
	return kill_sl_info(sig, (void *)(long)(priv != 0), sess);
}

int
kill_proc(pid_t pid, int sig, int priv)
{
	return kill_proc_info(sig, (void *)(long)(priv != 0), pid);
}

/*
 * Joy. Or not. Pthread wants us to wake up every thread
 * in our parent group.
 */
static inline void wake_up_parent(struct task_struct *p)
{
	struct task_struct *parent = p->parent, *tsk = parent;

	/*
	 * Fortunately this is not necessary for thread groups:
	 */
	if (p->tgid == tsk->tgid) {
		wake_up_interruptible(&tsk->wait_chldexit);
		return;
	}
	spin_lock_irq(&parent->sig->siglock);
	do {
		wake_up_interruptible(&tsk->wait_chldexit);
		tsk = next_thread(tsk);
		if (tsk->sig != parent->sig)
			BUG();
	} while (tsk != parent);
	spin_unlock_irq(&parent->sig->siglock);
}

/*
 * Let a parent know about a status change of a child.
 */

void do_notify_parent(struct task_struct *tsk, int sig)
{
	struct siginfo info;
	int why, status;

	if (delay_group_leader(tsk))
		return;
	if (sig == -1)
		BUG();

	info.si_signo = sig;
	info.si_errno = 0;
	info.si_pid = tsk->pid;
	info.si_uid = tsk->uid;

	/* FIXME: find out whether or not this is supposed to be c*time. */
	info.si_utime = tsk->utime;
	info.si_stime = tsk->stime;

	status = tsk->exit_code & 0x7f;
	why = SI_KERNEL;	/* shouldn't happen */
	switch (tsk->state) {
	case TASK_STOPPED:
		/* FIXME -- can we deduce CLD_TRAPPED or CLD_CONTINUED? */
		if (tsk->ptrace & PT_PTRACED)
			why = CLD_TRAPPED;
		else
			why = CLD_STOPPED;
		break;

	default:
		if (tsk->exit_code & 0x80)
			why = CLD_DUMPED;
		else if (tsk->exit_code & 0x7f)
			why = CLD_KILLED;
		else {
			why = CLD_EXITED;
			status = tsk->exit_code >> 8;
		}
		break;
	}
	info.si_code = why;
	info.si_status = status;

	send_sig_info(sig, &info, tsk->parent);
	wake_up_parent(tsk);
}


/*
 * We need the tasklist lock because it's the only
 * thing that protects out "parent" pointer.
 *
 * exit.c calls "do_notify_parent()" directly, because
 * it already has the tasklist lock.
 */
void
notify_parent(struct task_struct *tsk, int sig)
{
	if (sig != -1) {
		read_lock(&tasklist_lock);
		do_notify_parent(tsk, sig);
		read_unlock(&tasklist_lock);
	}
}

#ifndef HAVE_ARCH_GET_SIGNAL_TO_DELIVER

int get_signal_to_deliver(siginfo_t *info, struct pt_regs *regs)
{
	sigset_t *mask = &current->blocked;

	for (;;) {
		unsigned long signr = 0;
		struct k_sigaction *ka;

		local_irq_disable();
		if (current->sig->shared_pending.head) {
			spin_lock(&current->sig->siglock);
			signr = dequeue_signal(&current->sig->shared_pending, mask, info);
			spin_unlock(&current->sig->siglock);
		}
		if (!signr) {
			spin_lock(&current->sigmask_lock);
			signr = dequeue_signal(&current->pending, mask, info);
			spin_unlock(&current->sigmask_lock);
		}
		local_irq_enable();

		if (!signr)
			break;

		if ((current->ptrace & PT_PTRACED) && signr != SIGKILL) {
			/* Let the debugger run.  */
			current->exit_code = signr;
			set_current_state(TASK_STOPPED);
			notify_parent(current, SIGCHLD);
			schedule();

			/* We're back.  Did the debugger cancel the sig?  */
			signr = current->exit_code;
			if (signr == 0)
				continue;
			current->exit_code = 0;

			/* The debugger continued.  Ignore SIGSTOP.  */
			if (signr == SIGSTOP)
				continue;

			/* Update the siginfo structure.  Is this good?  */
			if (signr != info->si_signo) {
				info->si_signo = signr;
				info->si_errno = 0;
				info->si_code = SI_USER;
				info->si_pid = current->parent->pid;
				info->si_uid = current->parent->uid;
			}

			/* If the (new) signal is now blocked, requeue it.  */
			if (sigismember(&current->blocked, signr)) {
				send_sig_info(signr, info, current);
				continue;
			}
		}

		ka = &current->sig->action[signr-1];
		if (ka->sa.sa_handler == SIG_IGN) {
			if (signr != SIGCHLD)
				continue;
			/* Check for SIGCHLD: it's special.  */
			while (sys_wait4(-1, NULL, WNOHANG, NULL) > 0)
				/* nothing */;
			continue;
		}

		if (ka->sa.sa_handler == SIG_DFL) {
			int exit_code = signr;

			/* Init gets no signals it doesn't want.  */
			if (current->pid == 1)
				continue;

			switch (signr) {
			case SIGCONT: case SIGCHLD: case SIGWINCH: case SIGURG:
				continue;

			case SIGTSTP: case SIGTTIN: case SIGTTOU:
				if (is_orphaned_pgrp(current->pgrp))
					continue;
				/* FALLTHRU */

			case SIGSTOP: {
				struct signal_struct *sig;
				set_current_state(TASK_STOPPED);
				current->exit_code = signr;
				sig = current->parent->sig;
				if (sig && !(sig->action[SIGCHLD-1].sa.sa_flags & SA_NOCLDSTOP))
					notify_parent(current, SIGCHLD);
				schedule();
				continue;
			}

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGABRT: case SIGFPE: case SIGSEGV:
			case SIGBUS: case SIGSYS: case SIGXCPU: case SIGXFSZ:
				if (do_coredump(signr, regs))
					exit_code |= 0x80;
				/* FALLTHRU */

			default:
				sig_exit(signr, exit_code, info);
				/* NOTREACHED */
			}
		}
		return signr;
	}
	return 0;
}

#endif

EXPORT_SYMBOL(recalc_sigpending);
EXPORT_SYMBOL(dequeue_signal);
EXPORT_SYMBOL(flush_signals);
EXPORT_SYMBOL(force_sig);
EXPORT_SYMBOL(force_sig_info);
EXPORT_SYMBOL(kill_pg);
EXPORT_SYMBOL(kill_pg_info);
EXPORT_SYMBOL(kill_proc);
EXPORT_SYMBOL(kill_proc_info);
EXPORT_SYMBOL(kill_sl);
EXPORT_SYMBOL(kill_sl_info);
EXPORT_SYMBOL(notify_parent);
EXPORT_SYMBOL(send_sig);
EXPORT_SYMBOL(send_sig_info);
EXPORT_SYMBOL(block_all_signals);
EXPORT_SYMBOL(unblock_all_signals);


/*
 * System call entry points.
 */

/*
 * We don't need to get the kernel lock - this is all local to this
 * particular thread.. (and that's good, because this is _heavily_
 * used by various programs)
 */

asmlinkage long
sys_rt_sigprocmask(int how, sigset_t *set, sigset_t *oset, size_t sigsetsize)
{
	int error = -EINVAL;
	sigset_t old_set, new_set;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		goto out;

	if (set) {
		error = -EFAULT;
		if (copy_from_user(&new_set, set, sizeof(*set)))
			goto out;
		sigdelsetmask(&new_set, sigmask(SIGKILL)|sigmask(SIGSTOP));

		spin_lock_irq(&current->sigmask_lock);
		old_set = current->blocked;

		error = 0;
		switch (how) {
		default:
			error = -EINVAL;
			break;
		case SIG_BLOCK:
			sigorsets(&new_set, &old_set, &new_set);
			break;
		case SIG_UNBLOCK:
			signandsets(&new_set, &old_set, &new_set);
			break;
		case SIG_SETMASK:
			break;
		}

		current->blocked = new_set;
		recalc_sigpending();
		spin_unlock_irq(&current->sigmask_lock);
		if (error)
			goto out;
		if (oset)
			goto set_old;
	} else if (oset) {
		spin_lock_irq(&current->sigmask_lock);
		old_set = current->blocked;
		spin_unlock_irq(&current->sigmask_lock);

	set_old:
		error = -EFAULT;
		if (copy_to_user(oset, &old_set, sizeof(*oset)))
			goto out;
	}
	error = 0;
out:
	return error;
}

long do_sigpending(void *set, unsigned long sigsetsize)
{
	long error = -EINVAL;
	sigset_t pending;

	if (sigsetsize > sizeof(sigset_t))
		goto out;

	spin_lock_irq(&current->sigmask_lock);
	sigandsets(&pending, &current->blocked, &current->pending.signal);
	spin_unlock_irq(&current->sigmask_lock);

	error = -EFAULT;
	if (!copy_to_user(set, &pending, sigsetsize))
		error = 0;
out:
	return error;
}	

asmlinkage long
sys_rt_sigpending(sigset_t *set, size_t sigsetsize)
{
	return do_sigpending(set, sigsetsize);
}

#ifndef HAVE_ARCH_COPY_SIGINFO_TO_USER

int copy_siginfo_to_user(siginfo_t *to, siginfo_t *from)
{
	int err;

	if (!access_ok (VERIFY_WRITE, to, sizeof(siginfo_t)))
		return -EFAULT;
	if (from->si_code < 0)
		return __copy_to_user(to, from, sizeof(siginfo_t))
			? -EFAULT : 0;
	/*
	 * If you change siginfo_t structure, please be sure
	 * this code is fixed accordingly.
	 * It should never copy any pad contained in the structure
	 * to avoid security leaks, but must copy the generic
	 * 3 ints plus the relevant union member.
	 */
	err = __put_user(from->si_signo, &to->si_signo);
	err |= __put_user(from->si_errno, &to->si_errno);
	err |= __put_user((short)from->si_code, &to->si_code);
	switch (from->si_code & __SI_MASK) {
	case __SI_KILL:
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		break;
	case __SI_TIMER:
		err |= __put_user(from->si_timer1, &to->si_timer1);
		err |= __put_user(from->si_timer2, &to->si_timer2);
		break;
	case __SI_POLL:
		err |= __put_user(from->si_band, &to->si_band);
		err |= __put_user(from->si_fd, &to->si_fd);
		break;
	case __SI_FAULT:
		err |= __put_user(from->si_addr, &to->si_addr);
		break;
	case __SI_CHLD:
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		err |= __put_user(from->si_status, &to->si_status);
		err |= __put_user(from->si_utime, &to->si_utime);
		err |= __put_user(from->si_stime, &to->si_stime);
		break;
	case __SI_RT: /* This is not generated by the kernel as of now. */
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		err |= __put_user(from->si_int, &to->si_int);
		err |= __put_user(from->si_ptr, &to->si_ptr);
		break;
	default: /* this is just in case for now ... */
		err |= __put_user(from->si_pid, &to->si_pid);
		err |= __put_user(from->si_uid, &to->si_uid);
		break;
	}
	return err;
}

#endif

asmlinkage long
sys_rt_sigtimedwait(const sigset_t *uthese, siginfo_t *uinfo,
		    const struct timespec *uts, size_t sigsetsize)
{
	int ret, sig;
	sigset_t these;
	struct timespec ts;
	siginfo_t info;
	long timeout = 0;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&these, uthese, sizeof(these)))
		return -EFAULT;
		
	/*
	 * Invert the set of allowed signals to get those we
	 * want to block.
	 */
	sigdelsetmask(&these, sigmask(SIGKILL)|sigmask(SIGSTOP));
	signotset(&these);

	if (uts) {
		if (copy_from_user(&ts, uts, sizeof(ts)))
			return -EFAULT;
		if (ts.tv_nsec >= 1000000000L || ts.tv_nsec < 0
		    || ts.tv_sec < 0)
			return -EINVAL;
	}

	spin_lock_irq(&current->sig->siglock);
	spin_lock(&current->sigmask_lock);
	sig = dequeue_signal(&current->sig->shared_pending, &these, &info);
	if (!sig)
		sig = dequeue_signal(&current->pending, &these, &info);
	if (!sig) {
		timeout = MAX_SCHEDULE_TIMEOUT;
		if (uts)
			timeout = (timespec_to_jiffies(&ts)
				   + (ts.tv_sec || ts.tv_nsec));

		if (timeout) {
			/* None ready -- temporarily unblock those we're
			 * interested while we are sleeping in so that we'll
			 * be awakened when they arrive.  */
			current->real_blocked = current->blocked;
			sigandsets(&current->blocked, &current->blocked, &these);
			recalc_sigpending();
			spin_unlock(&current->sigmask_lock);
			spin_unlock_irq(&current->sig->siglock);

			current->state = TASK_INTERRUPTIBLE;
			timeout = schedule_timeout(timeout);

			spin_lock_irq(&current->sig->siglock);
			spin_lock(&current->sigmask_lock);
			sig = dequeue_signal(&current->sig->shared_pending, &these, &info);
			if (!sig)
				sig = dequeue_signal(&current->pending, &these, &info);
			current->blocked = current->real_blocked;
			siginitset(&current->real_blocked, 0);
			recalc_sigpending();
		}
	}
	spin_unlock(&current->sigmask_lock);
	spin_unlock_irq(&current->sig->siglock);

	if (sig) {
		ret = sig;
		if (uinfo) {
			if (copy_siginfo_to_user(uinfo, &info))
				ret = -EFAULT;
		}
	} else {
		ret = -EAGAIN;
		if (timeout)
			ret = -EINTR;
	}

	return ret;
}

asmlinkage long
sys_kill(int pid, int sig)
{
	struct siginfo info;

	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = SI_USER;
	info.si_pid = current->pid;
	info.si_uid = current->uid;

	return kill_something_info(sig, &info, pid);
}

/*
 *  Send a signal to only one task, even if it's a CLONE_THREAD task.
 */
asmlinkage long
sys_tkill(int pid, int sig)
{
	struct siginfo info;
	int error;
	struct task_struct *p;

	/* This is only valid for single tasks */
	if (pid <= 0)
		return -EINVAL;

	info.si_signo = sig;
	info.si_errno = 0;
	info.si_code = SI_TKILL;
	info.si_pid = current->pid;
	info.si_uid = current->uid;

	read_lock(&tasklist_lock);
	p = find_task_by_pid(pid);
	error = -ESRCH;
	if (p) {
		spin_lock_irq(&p->sig->siglock);
		error = __send_sig_info(sig, &info, p, 0);
		spin_unlock_irq(&p->sig->siglock);
	}
	read_unlock(&tasklist_lock);
	return error;
}

asmlinkage long
sys_rt_sigqueueinfo(int pid, int sig, siginfo_t *uinfo)
{
	siginfo_t info;

	if (copy_from_user(&info, uinfo, sizeof(siginfo_t)))
		return -EFAULT;

	/* Not even root can pretend to send signals from the kernel.
	   Nor can they impersonate a kill(), which adds source info.  */
	if (info.si_code >= 0)
		return -EPERM;
	info.si_signo = sig;

	/* POSIX.1b doesn't mention process groups.  */
	return kill_proc_info(sig, &info, pid);
}

int
do_sigaction(int sig, const struct k_sigaction *act, struct k_sigaction *oact)
{
	struct k_sigaction *k;

	if (sig < 1 || sig > _NSIG || (act && sig_kernel_only(sig)))
		return -EINVAL;

	k = &current->sig->action[sig-1];

	spin_lock_irq(&current->sig->siglock);

	if (oact)
		*oact = *k;

	if (act) {
		*k = *act;
		sigdelsetmask(&k->sa.sa_mask, sigmask(SIGKILL) | sigmask(SIGSTOP));

		/*
		 * POSIX 3.3.1.3:
		 *  "Setting a signal action to SIG_IGN for a signal that is
		 *   pending shall cause the pending signal to be discarded,
		 *   whether or not it is blocked."
		 *
		 *  "Setting a signal action to SIG_DFL for a signal that is
		 *   pending and whose default action is to ignore the signal
		 *   (for example, SIGCHLD), shall cause the pending signal to
		 *   be discarded, whether or not it is blocked"
		 *
		 * Note the silly behaviour of SIGCHLD: SIG_IGN means that the
		 * signal isn't actually ignored, but does automatic child
		 * reaping, while SIG_DFL is explicitly said by POSIX to force
		 * the signal to be ignored.
		 */

		if (k->sa.sa_handler == SIG_IGN
		    || (k->sa.sa_handler == SIG_DFL
			&& (sig == SIGCONT ||
			    sig == SIGCHLD ||
			    sig == SIGWINCH ||
			    sig == SIGURG))) {
			spin_lock_irq(&current->sigmask_lock);
			if (rm_sig_from_queue(sig, current))
				recalc_sigpending();
			spin_unlock_irq(&current->sigmask_lock);
		}
	}

	spin_unlock_irq(&current->sig->siglock);
	return 0;
}

int 
do_sigaltstack (const stack_t *uss, stack_t *uoss, unsigned long sp)
{
	stack_t oss;
	int error;

	if (uoss) {
		oss.ss_sp = (void *) current->sas_ss_sp;
		oss.ss_size = current->sas_ss_size;
		oss.ss_flags = sas_ss_flags(sp);
	}

	if (uss) {
		void *ss_sp;
		size_t ss_size;
		int ss_flags;

		error = -EFAULT;
		if (verify_area(VERIFY_READ, uss, sizeof(*uss))
		    || __get_user(ss_sp, &uss->ss_sp)
		    || __get_user(ss_flags, &uss->ss_flags)
		    || __get_user(ss_size, &uss->ss_size))
			goto out;

		error = -EPERM;
		if (on_sig_stack (sp))
			goto out;

		error = -EINVAL;
		/*
		 *
		 * Note - this code used to test ss_flags incorrectly
		 *  	  old code may have been written using ss_flags==0
		 *	  to mean ss_flags==SS_ONSTACK (as this was the only
		 *	  way that worked) - this fix preserves that older
		 *	  mechanism
		 */
		if (ss_flags != SS_DISABLE && ss_flags != SS_ONSTACK && ss_flags != 0)
			goto out;

		if (ss_flags == SS_DISABLE) {
			ss_size = 0;
			ss_sp = NULL;
		} else {
			error = -ENOMEM;
			if (ss_size < MINSIGSTKSZ)
				goto out;
		}

		current->sas_ss_sp = (unsigned long) ss_sp;
		current->sas_ss_size = ss_size;
	}

	if (uoss) {
		error = -EFAULT;
		if (copy_to_user(uoss, &oss, sizeof(oss)))
			goto out;
	}

	error = 0;
out:
	return error;
}

asmlinkage long
sys_sigpending(old_sigset_t *set)
{
	return do_sigpending(set, sizeof(*set));
}

#if !defined(__alpha__)
/* Alpha has its own versions with special arguments.  */

asmlinkage long
sys_sigprocmask(int how, old_sigset_t *set, old_sigset_t *oset)
{
	int error;
	old_sigset_t old_set, new_set;

	if (set) {
		error = -EFAULT;
		if (copy_from_user(&new_set, set, sizeof(*set)))
			goto out;
		new_set &= ~(sigmask(SIGKILL)|sigmask(SIGSTOP));

		spin_lock_irq(&current->sigmask_lock);
		old_set = current->blocked.sig[0];

		error = 0;
		switch (how) {
		default:
			error = -EINVAL;
			break;
		case SIG_BLOCK:
			sigaddsetmask(&current->blocked, new_set);
			break;
		case SIG_UNBLOCK:
			sigdelsetmask(&current->blocked, new_set);
			break;
		case SIG_SETMASK:
			current->blocked.sig[0] = new_set;
			break;
		}

		recalc_sigpending();
		spin_unlock_irq(&current->sigmask_lock);
		if (error)
			goto out;
		if (oset)
			goto set_old;
	} else if (oset) {
		old_set = current->blocked.sig[0];
	set_old:
		error = -EFAULT;
		if (copy_to_user(oset, &old_set, sizeof(*oset)))
			goto out;
	}
	error = 0;
out:
	return error;
}

#ifndef __sparc__
asmlinkage long
sys_rt_sigaction(int sig, const struct sigaction *act, struct sigaction *oact,
		 size_t sigsetsize)
{
	struct k_sigaction new_sa, old_sa;
	int ret = -EINVAL;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		goto out;

	if (act) {
		if (copy_from_user(&new_sa.sa, act, sizeof(new_sa.sa)))
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_sa : NULL, oact ? &old_sa : NULL);

	if (!ret && oact) {
		if (copy_to_user(oact, &old_sa.sa, sizeof(old_sa.sa)))
			return -EFAULT;
	}
out:
	return ret;
}
#endif /* __sparc__ */
#endif

#if !defined(__alpha__) && !defined(__ia64__) && !defined(__arm__)
/*
 * For backwards compatibility.  Functionality superseded by sigprocmask.
 */
asmlinkage long
sys_sgetmask(void)
{
	/* SMP safe */
	return current->blocked.sig[0];
}

asmlinkage long
sys_ssetmask(int newmask)
{
	int old;

	spin_lock_irq(&current->sigmask_lock);
	old = current->blocked.sig[0];

	siginitset(&current->blocked, newmask & ~(sigmask(SIGKILL)|
						  sigmask(SIGSTOP)));
	recalc_sigpending();
	spin_unlock_irq(&current->sigmask_lock);

	return old;
}
#endif /* !defined(__alpha__) */

#if !defined(__alpha__) && !defined(__ia64__) && !defined(__mips__) && \
    !defined(__arm__)
/*
 * For backwards compatibility.  Functionality superseded by sigaction.
 */
asmlinkage unsigned long
sys_signal(int sig, __sighandler_t handler)
{
	struct k_sigaction new_sa, old_sa;
	int ret;

	new_sa.sa.sa_handler = handler;
	new_sa.sa.sa_flags = SA_ONESHOT | SA_NOMASK;

	ret = do_sigaction(sig, &new_sa, &old_sa);

	return ret ? ret : (unsigned long)old_sa.sa.sa_handler;
}
#endif /* !alpha && !__ia64__ && !defined(__mips__) && !defined(__arm__) */

#ifndef HAVE_ARCH_SYS_PAUSE

asmlinkage int
sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

#endif /* HAVE_ARCH_SYS_PAUSE */

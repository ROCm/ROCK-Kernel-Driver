/*
 *  linux/kernel/exit.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/personality.h>
#include <linux/tty.h>
#include <linux/namespace.h>
#include <linux/security.h>
#include <linux/acct.h>
#include <linux/file.h>
#include <linux/binfmts.h>
#include <linux/ptrace.h>
#include <linux/profile.h>
#include <linux/mount.h>
#include <linux/proc_fs.h>
#include <linux/mempolicy.h>

#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>

extern void sem_exit (void);
extern struct task_struct *child_reaper;

int getrusage(struct task_struct *, int, struct rusage __user *);

static void __unhash_process(struct task_struct *p)
{
	nr_threads--;
	detach_pid(p, PIDTYPE_PID);
	detach_pid(p, PIDTYPE_TGID);
	if (thread_group_leader(p)) {
		detach_pid(p, PIDTYPE_PGID);
		detach_pid(p, PIDTYPE_SID);
		if (p->pid)
			__get_cpu_var(process_counts)--;
	}

	REMOVE_LINKS(p);
}

void release_task(struct task_struct * p)
{
	int zap_leader;
	task_t *leader;
	struct dentry *proc_dentry;

repeat: 
	BUG_ON(p->state < TASK_ZOMBIE);
 
	atomic_dec(&p->user->processes);
	spin_lock(&p->proc_lock);
	proc_dentry = proc_pid_unhash(p);
	write_lock_irq(&tasklist_lock);
	if (unlikely(p->ptrace))
		__ptrace_unlink(p);
	BUG_ON(!list_empty(&p->ptrace_list) || !list_empty(&p->ptrace_children));
	__exit_signal(p);
	__exit_sighand(p);
	__unhash_process(p);

	/*
	 * If we are the last non-leader member of the thread
	 * group, and the leader is zombie, then notify the
	 * group leader's parent process. (if it wants notification.)
	 */
	zap_leader = 0;
	leader = p->group_leader;
	if (leader != p && thread_group_empty(leader) && leader->state == TASK_ZOMBIE) {
		BUG_ON(leader->exit_signal == -1);
		do_notify_parent(leader, leader->exit_signal);
		/*
		 * If we were the last child thread and the leader has
		 * exited already, and the leader's parent ignores SIGCHLD,
		 * then we are the one who should release the leader.
		 *
		 * do_notify_parent() will have marked it self-reaping in
		 * that case.
		 */
		zap_leader = (leader->exit_signal == -1);
	}

	p->parent->cutime += p->utime + p->cutime;
	p->parent->cstime += p->stime + p->cstime;
	p->parent->cmin_flt += p->min_flt + p->cmin_flt;
	p->parent->cmaj_flt += p->maj_flt + p->cmaj_flt;
	p->parent->cnvcsw += p->nvcsw + p->cnvcsw;
	p->parent->cnivcsw += p->nivcsw + p->cnivcsw;
	sched_exit(p);
	write_unlock_irq(&tasklist_lock);
	spin_unlock(&p->proc_lock);
	proc_pid_flush(proc_dentry);
	release_thread(p);
	put_task_struct(p);

	p = leader;
	if (unlikely(zap_leader))
		goto repeat;
}

/* we are using it only for SMP init */

void unhash_process(struct task_struct *p)
{
	struct dentry *proc_dentry;

	spin_lock(&p->proc_lock);
	proc_dentry = proc_pid_unhash(p);
	write_lock_irq(&tasklist_lock);
	__unhash_process(p);
	write_unlock_irq(&tasklist_lock);
	spin_unlock(&p->proc_lock);
	proc_pid_flush(proc_dentry);
}

/*
 * This checks not only the pgrp, but falls back on the pid if no
 * satisfactory pgrp is found. I dunno - gdb doesn't work correctly
 * without this...
 */
int session_of_pgrp(int pgrp)
{
	struct task_struct *p;
	struct list_head *l;
	struct pid *pid;
	int sid = -1;

	read_lock(&tasklist_lock);
	for_each_task_pid(pgrp, PIDTYPE_PGID, p, l, pid)
		if (p->signal->session > 0) {
			sid = p->signal->session;
			goto out;
		}
	p = find_task_by_pid(pgrp);
	if (p)
		sid = p->signal->session;
out:
	read_unlock(&tasklist_lock);
	
	return sid;
}

/*
 * Determine if a process group is "orphaned", according to the POSIX
 * definition in 2.2.2.52.  Orphaned process groups are not to be affected
 * by terminal-generated stop signals.  Newly orphaned process groups are
 * to receive a SIGHUP and a SIGCONT.
 *
 * "I ask you, have you ever known what it is to be an orphan?"
 */
static int will_become_orphaned_pgrp(int pgrp, task_t *ignored_task)
{
	struct task_struct *p;
	struct list_head *l;
	struct pid *pid;
	int ret = 1;

	for_each_task_pid(pgrp, PIDTYPE_PGID, p, l, pid) {
		if (p == ignored_task
				|| p->state >= TASK_ZOMBIE 
				|| p->real_parent->pid == 1)
			continue;
		if (process_group(p->real_parent) != pgrp
			    && p->real_parent->signal->session == p->signal->session) {
			ret = 0;
			break;
		}
	}
	return ret;	/* (sighing) "Often!" */
}

int is_orphaned_pgrp(int pgrp)
{
	int retval;

	read_lock(&tasklist_lock);
	retval = will_become_orphaned_pgrp(pgrp, NULL);
	read_unlock(&tasklist_lock);

	return retval;
}

static inline int has_stopped_jobs(int pgrp)
{
	int retval = 0;
	struct task_struct *p;
	struct list_head *l;
	struct pid *pid;

	for_each_task_pid(pgrp, PIDTYPE_PGID, p, l, pid) {
		if (p->state != TASK_STOPPED)
			continue;

		/* If p is stopped by a debugger on a signal that won't
		   stop it, then don't count p as stopped.  This isn't
		   perfect but it's a good approximation.  */
		if (unlikely (p->ptrace)
		    && p->exit_code != SIGSTOP
		    && p->exit_code != SIGTSTP
		    && p->exit_code != SIGTTOU
		    && p->exit_code != SIGTTIN)
			continue;

		retval = 1;
		break;
	}
	return retval;
}

/**
 * reparent_to_init() - Reparent the calling kernel thread to the init task.
 *
 * If a kernel thread is launched as a result of a system call, or if
 * it ever exits, it should generally reparent itself to init so that
 * it is correctly cleaned up on exit.
 *
 * The various task state such as scheduling policy and priority may have
 * been inherited from a user process, so we reset them to sane values here.
 *
 * NOTE that reparent_to_init() gives the caller full capabilities.
 */
void reparent_to_init(void)
{
	write_lock_irq(&tasklist_lock);

	ptrace_unlink(current);
	/* Reparent to init */
	REMOVE_LINKS(current);
	current->parent = child_reaper;
	current->real_parent = child_reaper;
	SET_LINKS(current);

	/* Set the exit signal to SIGCHLD so we signal init on exit */
	current->exit_signal = SIGCHLD;

	if ((current->policy == SCHED_NORMAL) && (task_nice(current) < 0))
		set_user_nice(current, 0);
	/* cpus_allowed? */
	/* rt_priority? */
	/* signals? */
	security_task_reparent_to_init(current);
	memcpy(current->rlim, init_task.rlim, sizeof(*(current->rlim)));
	atomic_inc(&(INIT_USER->__count));
	switch_uid(INIT_USER);

	write_unlock_irq(&tasklist_lock);
}

void __set_special_pids(pid_t session, pid_t pgrp)
{
	struct task_struct *curr = current;

	if (curr->signal->session != session) {
		detach_pid(curr, PIDTYPE_SID);
		curr->signal->session = session;
		attach_pid(curr, PIDTYPE_SID, session);
	}
	if (process_group(curr) != pgrp) {
		detach_pid(curr, PIDTYPE_PGID);
		curr->signal->pgrp = pgrp;
		attach_pid(curr, PIDTYPE_PGID, pgrp);
	}
}

void set_special_pids(pid_t session, pid_t pgrp)
{
	write_lock_irq(&tasklist_lock);
	__set_special_pids(session, pgrp);
	write_unlock_irq(&tasklist_lock);
}

/*
 * Let kernel threads use this to say that they
 * allow a certain signal (since daemonize() will
 * have disabled all of them by default).
 */
int allow_signal(int sig)
{
	if (sig < 1 || sig > _NSIG)
		return -EINVAL;

	spin_lock_irq(&current->sighand->siglock);
	sigdelset(&current->blocked, sig);
	if (!current->mm) {
		/* Kernel threads handle their own signals.
		   Let the signal code know it'll be handled, so
		   that they don't get converted to SIGKILL or
		   just silently dropped */
		current->sighand->action[(sig)-1].sa.sa_handler = (void *)2;
	}
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	return 0;
}

EXPORT_SYMBOL(allow_signal);

int disallow_signal(int sig)
{
	if (sig < 1 || sig > _NSIG)
		return -EINVAL;

	spin_lock_irq(&current->sighand->siglock);
	sigaddset(&current->blocked, sig);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	return 0;
}

EXPORT_SYMBOL(disallow_signal);

/*
 *	Put all the gunge required to become a kernel thread without
 *	attached user resources in one place where it belongs.
 */

void daemonize(const char *name, ...)
{
	va_list args;
	struct fs_struct *fs;
	sigset_t blocked;

	va_start(args, name);
	vsnprintf(current->comm, sizeof(current->comm), name, args);
	va_end(args);

	/*
	 * If we were started as result of loading a module, close all of the
	 * user space pages.  We don't need them, and if we didn't close them
	 * they would be locked into memory.
	 */
	exit_mm(current);

	set_special_pids(1, 1);
	current->signal->tty = NULL;

	/* Block and flush all signals */
	sigfillset(&blocked);
	sigprocmask(SIG_BLOCK, &blocked, NULL);
	flush_signals(current);

	/* Become as one with the init task */

	exit_fs(current);	/* current->fs->count--; */
	fs = init_task.fs;
	current->fs = fs;
	atomic_inc(&fs->count);
 	exit_files(current);
	current->files = init_task.files;
	atomic_inc(&current->files->count);

	reparent_to_init();
}

EXPORT_SYMBOL(daemonize);

static inline void close_files(struct files_struct * files)
{
	int i, j;

	j = 0;
	for (;;) {
		unsigned long set;
		i = j * __NFDBITS;
		if (i >= files->max_fdset || i >= files->max_fds)
			break;
		set = files->open_fds->fds_bits[j++];
		while (set) {
			if (set & 1) {
				struct file * file = xchg(&files->fd[i], NULL);
				if (file)
					filp_close(file, files);
			}
			i++;
			set >>= 1;
		}
	}
}

struct files_struct *get_files_struct(struct task_struct *task)
{
	struct files_struct *files;

	task_lock(task);
	files = task->files;
	if (files)
		atomic_inc(&files->count);
	task_unlock(task);

	return files;
}

void fastcall put_files_struct(struct files_struct *files)
{
	if (atomic_dec_and_test(&files->count)) {
		close_files(files);
		/*
		 * Free the fd and fdset arrays if we expanded them.
		 */
		if (files->fd != &files->fd_array[0])
			free_fd_array(files->fd, files->max_fds);
		if (files->max_fdset > __FD_SETSIZE) {
			free_fdset(files->open_fds, files->max_fdset);
			free_fdset(files->close_on_exec, files->max_fdset);
		}
		kmem_cache_free(files_cachep, files);
	}
}

EXPORT_SYMBOL(put_files_struct);

static inline void __exit_files(struct task_struct *tsk)
{
	struct files_struct * files = tsk->files;

	if (files) {
		task_lock(tsk);
		tsk->files = NULL;
		task_unlock(tsk);
		put_files_struct(files);
	}
}

void exit_files(struct task_struct *tsk)
{
	__exit_files(tsk);
}

static inline void __put_fs_struct(struct fs_struct *fs)
{
	/* No need to hold fs->lock if we are killing it */
	if (atomic_dec_and_test(&fs->count)) {
		dput(fs->root);
		mntput(fs->rootmnt);
		dput(fs->pwd);
		mntput(fs->pwdmnt);
		if (fs->altroot) {
			dput(fs->altroot);
			mntput(fs->altrootmnt);
		}
		kmem_cache_free(fs_cachep, fs);
	}
}

void put_fs_struct(struct fs_struct *fs)
{
	__put_fs_struct(fs);
}

static inline void __exit_fs(struct task_struct *tsk)
{
	struct fs_struct * fs = tsk->fs;

	if (fs) {
		task_lock(tsk);
		tsk->fs = NULL;
		task_unlock(tsk);
		__put_fs_struct(fs);
	}
}

void exit_fs(struct task_struct *tsk)
{
	__exit_fs(tsk);
}

EXPORT_SYMBOL_GPL(exit_fs);

/*
 * Turn us into a lazy TLB process if we
 * aren't already..
 */
static inline void __exit_mm(struct task_struct * tsk)
{
	struct mm_struct *mm = tsk->mm;

	mm_release(tsk, mm);
	if (!mm)
		return;
	/*
	 * Serialize with any possible pending coredump.
	 * We must hold mmap_sem around checking core_waiters
	 * and clearing tsk->mm.  The core-inducing thread
	 * will increment core_waiters for each thread in the
	 * group with ->mm != NULL.
	 */
	down_read(&mm->mmap_sem);
	if (mm->core_waiters) {
		up_read(&mm->mmap_sem);
		down_write(&mm->mmap_sem);
		if (!--mm->core_waiters)
			complete(mm->core_startup_done);
		up_write(&mm->mmap_sem);

		wait_for_completion(&mm->core_done);
		down_read(&mm->mmap_sem);
	}
	atomic_inc(&mm->mm_count);
	if (mm != tsk->active_mm) BUG();
	/* more a memory barrier than a real lock */
	task_lock(tsk);
	tsk->mm = NULL;
	up_read(&mm->mmap_sem);
	enter_lazy_tlb(mm, current);
	task_unlock(tsk);
	mmput(mm);
}

void exit_mm(struct task_struct *tsk)
{
	__exit_mm(tsk);
}

EXPORT_SYMBOL(exit_mm);

static inline void choose_new_parent(task_t *p, task_t *reaper, task_t *child_reaper)
{
	/*
	 * Make sure we're not reparenting to ourselves and that
	 * the parent is not a zombie.
	 */
	if (p == reaper || reaper->state >= TASK_ZOMBIE)
		p->real_parent = child_reaper;
	else
		p->real_parent = reaper;
	if (p->parent == p->real_parent)
		BUG();
}

static inline void reparent_thread(task_t *p, task_t *father, int traced)
{
	/* We don't want people slaying init.  */
	if (p->exit_signal != -1)
		p->exit_signal = SIGCHLD;
	p->self_exec_id++;

	if (p->pdeath_signal)
		/* We already hold the tasklist_lock here.  */
		group_send_sig_info(p->pdeath_signal, (void *) 0, p);

	/* Move the child from its dying parent to the new one.  */
	if (unlikely(traced)) {
		/* Preserve ptrace links if someone else is tracing this child.  */
		list_del_init(&p->ptrace_list);
		if (p->parent != p->real_parent)
			list_add(&p->ptrace_list, &p->real_parent->ptrace_children);
	} else {
		/* If this child is being traced, then we're the one tracing it
		 * anyway, so let go of it.
		 */
		p->ptrace = 0;
		list_del_init(&p->sibling);
		p->parent = p->real_parent;
		list_add_tail(&p->sibling, &p->parent->children);

		/* If we'd notified the old parent about this child's death,
		 * also notify the new parent.
		 */
		if (p->state == TASK_ZOMBIE && p->exit_signal != -1 &&
		    thread_group_empty(p))
			do_notify_parent(p, p->exit_signal);
	}

	/*
	 * process group orphan check
	 * Case ii: Our child is in a different pgrp
	 * than we are, and it was the only connection
	 * outside, so the child pgrp is now orphaned.
	 */
	if ((process_group(p) != process_group(father)) &&
	    (p->signal->session == father->signal->session)) {
		int pgrp = process_group(p);

		if (will_become_orphaned_pgrp(pgrp, NULL) && has_stopped_jobs(pgrp)) {
			__kill_pg_info(SIGHUP, (void *)1, pgrp);
			__kill_pg_info(SIGCONT, (void *)1, pgrp);
		}
	}
}

/*
 * When we die, we re-parent all our children.
 * Try to give them to another thread in our thread
 * group, and if no such member exists, give it to
 * the global child reaper process (ie "init")
 */
static inline void forget_original_parent(struct task_struct * father)
{
	struct task_struct *p, *reaper = father;
	struct list_head *_p, *_n;

	reaper = father->group_leader;
	if (reaper == father)
		reaper = child_reaper;

	/*
	 * There are only two places where our children can be:
	 *
	 * - in our child list
	 * - in our ptraced child list
	 *
	 * Search them and reparent children.
	 */
	list_for_each_safe(_p, _n, &father->children) {
		p = list_entry(_p,struct task_struct,sibling);
		if (father == p->real_parent) {
			choose_new_parent(p, reaper, child_reaper);
			reparent_thread(p, father, 0);
		} else {
			ptrace_unlink (p);
			if (p->state == TASK_ZOMBIE && p->exit_signal != -1 &&
			    thread_group_empty(p))
				do_notify_parent(p, p->exit_signal);
		}
	}
	list_for_each_safe(_p, _n, &father->ptrace_children) {
		p = list_entry(_p,struct task_struct,ptrace_list);
		choose_new_parent(p, reaper, child_reaper);
		reparent_thread(p, father, 1);
	}
}

/*
 * Send signals to all our closest relatives so that they know
 * to properly mourn us..
 */
static void exit_notify(struct task_struct *tsk)
{
	int state;
	struct task_struct *t;

	if (signal_pending(tsk) && !tsk->signal->group_exit
	    && !thread_group_empty(tsk)) {
		/*
		 * This occurs when there was a race between our exit
		 * syscall and a group signal choosing us as the one to
		 * wake up.  It could be that we are the only thread
		 * alerted to check for pending signals, but another thread
		 * should be woken now to take the signal since we will not.
		 * Now we'll wake all the threads in the group just to make
		 * sure someone gets all the pending signals.
		 */
		read_lock(&tasklist_lock);
		spin_lock_irq(&tsk->sighand->siglock);
		for (t = next_thread(tsk); t != tsk; t = next_thread(t))
			if (!signal_pending(t) && !(t->flags & PF_EXITING)) {
				recalc_sigpending_tsk(t);
				if (signal_pending(t))
					signal_wake_up(t, 0);
			}
		spin_unlock_irq(&tsk->sighand->siglock);
		read_unlock(&tasklist_lock);
	}

	write_lock_irq(&tasklist_lock);

	/*
	 * This does two things:
	 *
  	 * A.  Make init inherit all the child processes
	 * B.  Check to see if any process groups have become orphaned
	 *	as a result of our exiting, and if they have any stopped
	 *	jobs, send them a SIGHUP and then a SIGCONT.  (POSIX 3.2.2.2)
	 */

	forget_original_parent(tsk);
	BUG_ON(!list_empty(&tsk->children));

	/*
	 * Check to see if any process groups have become orphaned
	 * as a result of our exiting, and if they have any stopped
	 * jobs, send them a SIGHUP and then a SIGCONT.  (POSIX 3.2.2.2)
	 *
	 * Case i: Our father is in a different pgrp than we are
	 * and we were the only connection outside, so our pgrp
	 * is about to become orphaned.
	 */
	 
	t = tsk->real_parent;
	
	if ((process_group(t) != process_group(tsk)) &&
	    (t->signal->session == tsk->signal->session) &&
	    will_become_orphaned_pgrp(process_group(tsk), tsk) &&
	    has_stopped_jobs(process_group(tsk))) {
		__kill_pg_info(SIGHUP, (void *)1, process_group(tsk));
		__kill_pg_info(SIGCONT, (void *)1, process_group(tsk));
	}

	/* Let father know we died 
	 *
	 * Thread signals are configurable, but you aren't going to use
	 * that to send signals to arbitary processes. 
	 * That stops right now.
	 *
	 * If the parent exec id doesn't match the exec id we saved
	 * when we started then we know the parent has changed security
	 * domain.
	 *
	 * If our self_exec id doesn't match our parent_exec_id then
	 * we have changed execution domain as these two values started
	 * the same after a fork.
	 *	
	 */
	
	if (tsk->exit_signal != SIGCHLD && tsk->exit_signal != -1 &&
	    ( tsk->parent_exec_id != t->self_exec_id  ||
	      tsk->self_exec_id != tsk->parent_exec_id)
	    && !capable(CAP_KILL))
		tsk->exit_signal = SIGCHLD;


	/* If something other than our normal parent is ptracing us, then
	 * send it a SIGCHLD instead of honoring exit_signal.  exit_signal
	 * only has special meaning to our real parent.
	 */
	if (tsk->exit_signal != -1 && thread_group_empty(tsk)) {
		int signal = tsk->parent == tsk->real_parent ? tsk->exit_signal : SIGCHLD;
		do_notify_parent(tsk, signal);
	} else if (tsk->ptrace) {
		do_notify_parent(tsk, SIGCHLD);
	}

	state = TASK_ZOMBIE;
	if (tsk->exit_signal == -1 && tsk->ptrace == 0)
		state = TASK_DEAD;
	tsk->state = state;
	tsk->flags |= PF_DEAD;

	/*
	 * In the preemption case it must be impossible for the task
	 * to get runnable again, so use "_raw_" unlock to keep
	 * preempt_count elevated until we schedule().
	 *
	 * To avoid deadlock on SMP, interrupts must be unmasked.  If we
	 * don't, subsequently called functions (e.g, wait_task_inactive()
	 * via release_task()) will spin, with interrupt flags
	 * unwittingly blocked, until the other task sleeps.  That task
	 * may itself be waiting for smp_call_function() to answer and
	 * complete, and with interrupts blocked that will never happen.
	 */
	_raw_write_unlock(&tasklist_lock);
	local_irq_enable();

	/* If the process is dead, release it - nobody will wait for it */
	if (state == TASK_DEAD)
		release_task(tsk);

}

asmlinkage NORET_TYPE void do_exit(long code)
{
	struct task_struct *tsk = current;

	if (unlikely(in_interrupt()))
		panic("Aiee, killing interrupt handler!");
	if (unlikely(!tsk->pid))
		panic("Attempted to kill the idle task!");
	if (unlikely(tsk->pid == 1))
		panic("Attempted to kill init!");
	if (tsk->io_context)
		exit_io_context();
	tsk->flags |= PF_EXITING;
	del_timer_sync(&tsk->real_timer);

	if (unlikely(in_atomic()))
		printk(KERN_INFO "note: %s[%d] exited with preempt_count %d\n",
				current->comm, current->pid,
				preempt_count());

	profile_exit_task(tsk);
 
	if (unlikely(current->ptrace & PT_TRACE_EXIT)) {
		current->ptrace_message = code;
		ptrace_notify((PTRACE_EVENT_EXIT << 8) | SIGTRAP);
	}

	acct_process(code);
	__exit_mm(tsk);

	exit_sem(tsk);
	__exit_files(tsk);
	__exit_fs(tsk);
	exit_namespace(tsk);
	exit_thread();
#ifdef CONFIG_NUMA
	mpol_free(tsk->mempolicy);
#endif

	if (tsk->signal->leader)
		disassociate_ctty(1);

	module_put(tsk->thread_info->exec_domain->module);
	if (tsk->binfmt)
		module_put(tsk->binfmt->module);

	tsk->exit_code = code;
	exit_notify(tsk);
	schedule();
	BUG();
	/* Avoid "noreturn function does return".  */
	for (;;) ;
}

NORET_TYPE void complete_and_exit(struct completion *comp, long code)
{
	if (comp)
		complete(comp);
	
	do_exit(code);
}

EXPORT_SYMBOL(complete_and_exit);

asmlinkage long sys_exit(int error_code)
{
	do_exit((error_code&0xff)<<8);
}

task_t fastcall *next_thread(task_t *p)
{
	struct pid_link *link = p->pids + PIDTYPE_TGID;
	struct list_head *tmp, *head = &link->pidptr->task_list;

#ifdef CONFIG_SMP
	if (!p->sighand)
		BUG();
	if (!spin_is_locked(&p->sighand->siglock) &&
				!rwlock_is_locked(&tasklist_lock))
		BUG();
#endif
	tmp = link->pid_chain.next;
	if (tmp == head)
		tmp = head->next;

	return pid_task(tmp, PIDTYPE_TGID);
}

EXPORT_SYMBOL(next_thread);

/*
 * Take down every thread in the group.  This is called by fatal signals
 * as well as by sys_exit_group (below).
 */
NORET_TYPE void
do_group_exit(int exit_code)
{
	BUG_ON(exit_code & 0x80); /* core dumps don't get here */

	if (current->signal->group_exit)
		exit_code = current->signal->group_exit_code;
	else if (!thread_group_empty(current)) {
		struct signal_struct *const sig = current->signal;
		struct sighand_struct *const sighand = current->sighand;
		read_lock(&tasklist_lock);
		spin_lock_irq(&sighand->siglock);
		if (sig->group_exit)
			/* Another thread got here before we took the lock.  */
			exit_code = sig->group_exit_code;
		else {
			sig->group_exit = 1;
			sig->group_exit_code = exit_code;
			zap_other_threads(current);
		}
		spin_unlock_irq(&sighand->siglock);
		read_unlock(&tasklist_lock);
	}

	do_exit(exit_code);
	/* NOTREACHED */
}

/*
 * this kills every thread in the thread group. Note that any externally
 * wait4()-ing process will get the correct exit code - even if this
 * thread is not the thread group leader.
 */
asmlinkage void sys_exit_group(int error_code)
{
	do_group_exit((error_code & 0xff) << 8);
}

static int eligible_child(pid_t pid, int options, task_t *p)
{
	if (pid > 0) {
		if (p->pid != pid)
			return 0;
	} else if (!pid) {
		if (process_group(p) != process_group(current))
			return 0;
	} else if (pid != -1) {
		if (process_group(p) != -pid)
			return 0;
	}

	/*
	 * Do not consider detached threads that are
	 * not ptraced:
	 */
	if (p->exit_signal == -1 && !p->ptrace)
		return 0;

	/* Wait for all children (clone and not) if __WALL is set;
	 * otherwise, wait for clone children *only* if __WCLONE is
	 * set; otherwise, wait for non-clone children *only*.  (Note:
	 * A "clone" child here is one that reports to its parent
	 * using a signal other than SIGCHLD.) */
	if (((p->exit_signal != SIGCHLD) ^ ((options & __WCLONE) != 0))
	    && !(options & __WALL))
		return 0;
	/*
	 * Do not consider thread group leaders that are
	 * in a non-empty thread group:
	 */
	if (current->tgid != p->tgid && delay_group_leader(p))
		return 2;

	if (security_task_wait(p))
		return 0;

	return 1;
}

/*
 * Handle sys_wait4 work for one task in state TASK_ZOMBIE.  We hold
 * read_lock(&tasklist_lock) on entry.  If we return zero, we still hold
 * the lock and this task is uninteresting.  If we return nonzero, we have
 * released the lock and the system call should return.
 */
static int wait_task_zombie(task_t *p, unsigned int __user *stat_addr, struct rusage __user *ru)
{
	unsigned long state;
	int retval;

	/*
	 * Try to move the task's state to DEAD
	 * only one thread is allowed to do this:
	 */
	state = xchg(&p->state, TASK_DEAD);
	if (state != TASK_ZOMBIE) {
		BUG_ON(state != TASK_DEAD);
		return 0;
	}
	if (unlikely(p->exit_signal == -1 && p->ptrace == 0))
		/*
		 * This can only happen in a race with a ptraced thread
		 * dying on another processor.
		 */
		return 0;

	/*
	 * Now we are sure this task is interesting, and no other
	 * thread can reap it because we set its state to TASK_DEAD.
	 */
	read_unlock(&tasklist_lock);

	retval = ru ? getrusage(p, RUSAGE_BOTH, ru) : 0;
	if (!retval && stat_addr) {
		if (p->signal->group_exit)
			retval = put_user(p->signal->group_exit_code, stat_addr);
		else
			retval = put_user(p->exit_code, stat_addr);
	}
	if (retval) {
		p->state = TASK_ZOMBIE;
		return retval;
	}
	retval = p->pid;
	if (p->real_parent != p->parent) {
		write_lock_irq(&tasklist_lock);
		/* Double-check with lock held.  */
		if (p->real_parent != p->parent) {
			__ptrace_unlink(p);
			p->state = TASK_ZOMBIE;
			/* If this is a detached thread, this is where it goes away.  */
			if (p->exit_signal == -1) {
				/* release_task takes the lock itself.  */
				write_unlock_irq(&tasklist_lock);
				release_task (p);
			}
			else {
				do_notify_parent(p, p->exit_signal);
				write_unlock_irq(&tasklist_lock);
			}
			p = NULL;
		}
		else
			write_unlock_irq(&tasklist_lock);
	}
	if (p != NULL)
		release_task(p);
	BUG_ON(!retval);
	return retval;
}

/*
 * Handle sys_wait4 work for one task in state TASK_STOPPED.  We hold
 * read_lock(&tasklist_lock) on entry.  If we return zero, we still hold
 * the lock and this task is uninteresting.  If we return nonzero, we have
 * released the lock and the system call should return.
 */
static int wait_task_stopped(task_t *p, int delayed_group_leader,
			     unsigned int __user *stat_addr,
			     struct rusage __user *ru)
{
	int retval, exit_code;

	if (!p->exit_code)
		return 0;
	if (delayed_group_leader && !(p->ptrace & PT_PTRACED) &&
	    p->signal && p->signal->group_stop_count > 0)
		/*
		 * A group stop is in progress and this is the group leader.
		 * We won't report until all threads have stopped.
		 */
		return 0;

	/*
	 * Now we are pretty sure this task is interesting.
	 * Make sure it doesn't get reaped out from under us while we
	 * give up the lock and then examine it below.  We don't want to
	 * keep holding onto the tasklist_lock while we call getrusage and
	 * possibly take page faults for user memory.
	 */
	get_task_struct(p);
	read_unlock(&tasklist_lock);
	write_lock_irq(&tasklist_lock);

	/*
	 * This uses xchg to be atomic with the thread resuming and setting
	 * it.  It must also be done with the write lock held to prevent a
	 * race with the TASK_ZOMBIE case.
	 */
	exit_code = xchg(&p->exit_code, 0);
	if (unlikely(p->state > TASK_STOPPED)) {
		/*
		 * The task resumed and then died.  Let the next iteration
		 * catch it in TASK_ZOMBIE.  Note that exit_code might
		 * already be zero here if it resumed and did _exit(0).
		 * The task itself is dead and won't touch exit_code again;
		 * other processors in this function are locked out.
		 */
		p->exit_code = exit_code;
		exit_code = 0;
	}
	if (unlikely(exit_code == 0)) {
		/*
		 * Another thread in this function got to it first, or it
		 * resumed, or it resumed and then died.
		 */
		write_unlock_irq(&tasklist_lock);
		put_task_struct(p);
		read_lock(&tasklist_lock);
		return 0;
	}

	/* move to end of parent's list to avoid starvation */
	remove_parent(p);
	add_parent(p, p->parent);

	write_unlock_irq(&tasklist_lock);

	retval = ru ? getrusage(p, RUSAGE_BOTH, ru) : 0;
	if (!retval && stat_addr)
		retval = put_user((exit_code << 8) | 0x7f, stat_addr);
	if (!retval)
		retval = p->pid;
	put_task_struct(p);

	BUG_ON(!retval);
	return retval;
}

asmlinkage long sys_wait4(pid_t pid,unsigned int __user *stat_addr, int options, struct rusage __user *ru)
{
	DECLARE_WAITQUEUE(wait, current);
	struct task_struct *tsk;
	int flag, retval;

	if (options & ~(WNOHANG|WUNTRACED|__WNOTHREAD|__WCLONE|__WALL))
		return -EINVAL;

	add_wait_queue(&current->wait_chldexit,&wait);
repeat:
	flag = 0;
	current->state = TASK_INTERRUPTIBLE;
	read_lock(&tasklist_lock);
	tsk = current;
	do {
		struct task_struct *p;
		struct list_head *_p;
		int ret;

		list_for_each(_p,&tsk->children) {
			p = list_entry(_p,struct task_struct,sibling);

			ret = eligible_child(pid, options, p);
			if (!ret)
				continue;
			flag = 1;

			switch (p->state) {
			case TASK_STOPPED:
				if (!(options & WUNTRACED) &&
				    !(p->ptrace & PT_PTRACED))
					continue;
				retval = wait_task_stopped(p, ret == 2,
							   stat_addr, ru);
				if (retval != 0) /* He released the lock.  */
					goto end_wait4;
				break;
			case TASK_ZOMBIE:
				/*
				 * Eligible but we cannot release it yet:
				 */
				if (ret == 2)
					continue;
				retval = wait_task_zombie(p, stat_addr, ru);
				if (retval != 0) /* He released the lock.  */
					goto end_wait4;
				break;
			}
		}
		if (!flag) {
			list_for_each (_p,&tsk->ptrace_children) {
				p = list_entry(_p,struct task_struct,ptrace_list);
				if (!eligible_child(pid, options, p))
					continue;
				flag = 1;
				break;
			}
		}
		if (options & __WNOTHREAD)
			break;
		tsk = next_thread(tsk);
		if (tsk->signal != current->signal)
			BUG();
	} while (tsk != current);
	read_unlock(&tasklist_lock);
	if (flag) {
		retval = 0;
		if (options & WNOHANG)
			goto end_wait4;
		retval = -ERESTARTSYS;
		if (signal_pending(current))
			goto end_wait4;
		schedule();
		goto repeat;
	}
	retval = -ECHILD;
end_wait4:
	current->state = TASK_RUNNING;
	remove_wait_queue(&current->wait_chldexit,&wait);
	return retval;
}

#ifdef __ARCH_WANT_SYS_WAITPID

/*
 * sys_waitpid() remains for compatibility. waitpid() should be
 * implemented by calling sys_wait4() from libc.a.
 */
asmlinkage long sys_waitpid(pid_t pid, unsigned __user *stat_addr, int options)
{
	return sys_wait4(pid, stat_addr, options, NULL);
}

#endif

/*
 * perfmon_syscalls.c: perfmon2 system call interface
 *
 * This file implements the perfmon2 interface which
 * provides access to the hardware performance counters
 * of the host processor.
 *
 * The initial version of perfmon.c was written by
 * Ganesh Venkitachalam, IBM Corp.
 *
 * Then it was modified for perfmon-1.x by Stephane Eranian and
 * David Mosberger, Hewlett Packard Co.
 *
 * Version Perfmon-2.x is a complete rewrite of perfmon-1.x
 * by Stephane Eranian, Hewlett Packard Co.
 *
 * Copyright (c) 1999-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *                David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * More information about perfmon available at:
 * 	http://perfmon2.sf.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/ptrace.h>
#include <linux/perfmon_kern.h>
#include <linux/uaccess.h>
#include "perfmon_priv.h"

/*
 * Context locking rules:
 * ---------------------
 * 	- any thread with access to the file descriptor of a context can
 * 	  potentially issue perfmon calls
 *
 * 	- calls must be serialized to guarantee correctness
 *
 * 	- as soon as a context is attached to a thread or CPU, it may be
 * 	  actively monitoring. On some architectures, such as IA-64, this
 * 	  is true even though the pfm_start() call has not been made. This
 * 	  comes from the fact that on some architectures, it is possible to
 * 	  start/stop monitoring from userland.
 *
 *	- If monitoring is active, then there can PMU interrupts. Because
 *	  context accesses must be serialized, the perfmon system calls
 *	  must mask interrupts as soon as the context is attached.
 *
 *	- perfmon system calls that operate with the context unloaded cannot
 *	  assume it is actually unloaded when they are called. They first need
 *	  to check and for that they need interrupts masked. Then, if the
 *	  context is actually unloaded, they can unmask interrupts.
 *
 *	- interrupt masking holds true for other internal perfmon functions as
 *	  well. Except for PMU interrupt handler because those interrupts
 *	  cannot be nested.
 *
 * 	- we mask ALL interrupts instead of just the PMU interrupt because we
 * 	  also need to protect against timer interrupts which could trigger
 * 	  a set switch.
 */
#ifdef CONFIG_UTRACE
#include <linux/utrace.h>

static u32
stopper_quiesce(struct utrace_attached_engine *engine, struct task_struct *tsk)
{
	PFM_DBG("quiesced [%d]", tsk->pid);
	complete(engine->data);
	return UTRACE_ACTION_RESUME;
}

void
pfm_resume_task(struct task_struct *t, void *data)
{
	PFM_DBG("utrace detach [%d]", t->pid);
	(void) utrace_detach(t, data);
}

static const struct utrace_engine_ops utrace_ops =
{
	.report_quiesce = stopper_quiesce,
};

static int pfm_wait_task_stopped(struct task_struct *task, void **data)
{
	DECLARE_COMPLETION_ONSTACK(done);
	struct utrace_attached_engine *eng;
	int ret;

	eng = utrace_attach(task, UTRACE_ATTACH_CREATE, &utrace_ops, &done);
	if (IS_ERR(eng))
		return PTR_ERR(eng);

	ret = utrace_set_flags(task, eng,
			       UTRACE_ACTION_QUIESCE | UTRACE_EVENT(QUIESCE));
	PFM_DBG("wait quiesce [%d]", task->pid);
	if (!ret)
		ret = wait_for_completion_interruptible(&done);

	if (ret)
		(void) utrace_detach(task, eng);
	else
		*data = eng;
	return 0;
}
#else /* !CONFIG_UTRACE */
static int pfm_wait_task_stopped(struct task_struct *task, void **data)
{
	int ret;

	*data = NULL;

	/*
	 * returns 0 if cannot attach
	 */
	ret = ptrace_may_access(task, PTRACE_MODE_ATTACH);
	PFM_DBG("may_attach=%d", ret);
	if (!ret)
		return -EPERM;

	ret = ptrace_check_attach(task, 0);
	PFM_DBG("check_attach=%d", ret);
	return ret;
}
void pfm_resume_task(struct task_struct *t, void *data)
{}
#endif

struct pfm_syscall_cookie {
	struct file *filp;
	int fput_needed;
};

/*
 * cannot attach if :
 * 	- kernel task
 * 	- task not owned by caller (checked by ptrace_may_attach())
 * 	- task is dead or zombie
 * 	- cannot use blocking notification when self-monitoring
 */
static int pfm_task_incompatible(struct pfm_context *ctx,
				 struct task_struct *task)
{
	/*
	 * cannot attach to a kernel thread
	 */
	if (!task->mm) {
		PFM_DBG("cannot attach to kernel thread [%d]", task->pid);
		return -EPERM;
	}

	/*
	 * cannot use block on notification when
	 * self-monitoring.
	 */
	if (ctx->flags.block && task == current) {
		PFM_DBG("cannot use block on notification when self-monitoring"
			"[%d]", task->pid);
		return -EINVAL;
	}
	/*
	 * cannot attach to a zombie task
	 */
	if (task->exit_state == EXIT_ZOMBIE || task->exit_state == EXIT_DEAD) {
		PFM_DBG("cannot attach to zombie/dead task [%d]", task->pid);
		return -EBUSY;
	}
	return 0;
}

/**
 * pfm_get_task -- check permission and acquire task to monitor
 * @ctx: perfmon context
 * @pid: identification of the task to check
 * @task: upon return, a pointer to the task to monitor
 *
 * This function  is used in per-thread mode only AND when not
 * self-monitoring. It finds the task to monitor and checks
 * that the caller has permissions to attach. It also checks
 * that the task is stopped via ptrace so that we can safely
 * modify its state.
 *
 * task refcount is incremented when succesful.
 */
static int pfm_get_task(struct pfm_context *ctx, pid_t pid,
			struct task_struct **task, void **data)
{
	struct task_struct *p;
	int ret = 0, ret1 = 0;

	*data = NULL;

	/*
	 * When attaching to another thread we must ensure
	 * that the thread is actually stopped.
	 *
	 * As a consequence, only the ptracing parent can actually
	 * attach a context to a thread. Obviously, this constraint
	 * does not exist for self-monitoring threads.
	 *
	 * We use ptrace_may_attach() to check for permission.
	 */
	read_lock(&tasklist_lock);

	p = find_task_by_vpid(pid);
	if (p)
		get_task_struct(p);

	read_unlock(&tasklist_lock);

	if (!p) {
		PFM_DBG("task not found %d", pid);
		return -ESRCH;
	}

	ret = pfm_task_incompatible(ctx, p);
	if (ret)
		goto error;

	ret = pfm_wait_task_stopped(p, data);
	if (ret)
		goto error;

	*task = p;

	return 0;
error:
	if (!(ret1 || ret))
		ret = -EPERM;

	put_task_struct(p);

	return ret;
}

/*
 * context must be locked when calling this function
 */
int pfm_check_task_state(struct pfm_context *ctx, int check_mask,
			 unsigned long *flags, void **resume)
{
	struct task_struct *task;
	unsigned long local_flags, new_flags;
	int state, ret;

	*resume = NULL;

recheck:
	/*
	 * task is NULL for system-wide context
	 */
	task = ctx->task;
	state = ctx->state;
	local_flags = *flags;

	PFM_DBG("state=%d check_mask=0x%x", state, check_mask);
	/*
	 * if the context is detached, then we do not touch
	 * hardware, therefore there is not restriction on when we can
	 * access it.
	 */
	if (state == PFM_CTX_UNLOADED)
		return 0;
	/*
	 * no command can operate on a zombie context.
	 * A context becomes zombie when the file that identifies
	 * it is closed while the context is still attached to the
	 * thread it monitors.
	 */
	if (state == PFM_CTX_ZOMBIE)
		return -EINVAL;

	/*
	 * at this point, state is PFM_CTX_LOADED or PFM_CTX_MASKED
	 */

	/*
	 * some commands require the context to be unloaded to operate
	 */
	if (check_mask & PFM_CMD_UNLOADED)  {
		PFM_DBG("state=%d, cmd needs context unloaded", state);
		return -EBUSY;
	}

	/*
	 * self-monitoring always ok.
	 */
	if (task == current)
		return 0;

	/*
	 * for syswide, the calling thread must be running on the cpu
	 * the context is bound to.
	 */
	if (ctx->flags.system) {
		if (ctx->cpu != smp_processor_id())
			return -EBUSY;
		return 0;
	}

	/*
	 * at this point, monitoring another thread
	 */

	/*
	 * the pfm_unload_context() command is allowed on masked context
	 */
	if (state == PFM_CTX_MASKED && !(check_mask & PFM_CMD_UNLOAD))
		return 0;

	/*
	 * When we operate on another thread, we must wait for it to be
	 * stopped and completely off any CPU as we need to access the
	 * PMU state (or machine state).
	 *
	 * A thread can be put in the STOPPED state in various ways
	 * including PTRACE_ATTACH, or when it receives a SIGSTOP signal.
	 * We enforce that the thread must be ptraced, so it is stopped
	 * AND it CANNOT wake up while we operate on it because this
	 * would require an action from the ptracing parent which is the
	 * thread that is calling this function.
	 *
	 * The dependency on ptrace, imposes that only the ptracing
	 * parent can issue command on a thread. This is unfortunate
	 * but we do not know of a better way of doing this.
	 */
	if (check_mask & PFM_CMD_STOPPED) {

		spin_unlock_irqrestore(&ctx->lock, local_flags);

		/*
		 * check that the thread is ptraced AND STOPPED
		 */
		ret = pfm_wait_task_stopped(task, resume);

		spin_lock_irqsave(&ctx->lock, new_flags);

		/*
		 * flags may be different than when we released the lock
		 */
		*flags = new_flags;

		if (ret)
			return ret;
		/*
		 * we must recheck to verify if state has changed
		 */
		if (unlikely(ctx->state != state)) {
			PFM_DBG("old_state=%d new_state=%d",
				state,
				ctx->state);
			goto recheck;
		}
	}
	return 0;
}

/*
 * pfm_get_args - Function used to copy the syscall argument into kernel memory.
 * @ureq: user argument
 * @sz: user argument size
 * @lsz: size of stack buffer
 * @laddr: stack buffer address
 * @req: point to start of kernel copy of the argument
 * @ptr_free: address of kernel copy to free
 *
 * There are two options:
 * 	- use a stack buffer described by laddr (addresses) and lsz (size)
 * 	- allocate memory
 *
 * return:
 * 	< 0 : in case of error (ptr_free may not be updated)
 * 	  0 : success
 *      - req: points to base of kernel copy of arguments
 *	- ptr_free: address of buffer to free by caller on exit.
 *		    NULL if using the stack buffer
 *
 * when ptr_free is not NULL upon return, the caller must kfree()
 */
int pfm_get_args(void __user *ureq, size_t sz, size_t lsz, void *laddr,
		 void **req, void **ptr_free)
{
	void *addr;

	/*
	 * check syadmin argument limit
	 */
	if (unlikely(sz > pfm_controls.arg_mem_max)) {
		PFM_DBG("argument too big %zu max=%zu",
			sz,
			pfm_controls.arg_mem_max);
		return -E2BIG;
	}

	/*
	 * check if vector fits on stack buffer
	 */
	if (sz > lsz) {
		addr = kmalloc(sz, GFP_KERNEL);
		if (unlikely(addr == NULL))
			return -ENOMEM;
		*ptr_free = addr;
	} else {
		addr = laddr;
		*req = laddr;
		*ptr_free = NULL;
	}

	/*
	 * bring the data in
	 */
	if (unlikely(copy_from_user(addr, ureq, sz))) {
		if (addr != laddr)
			kfree(addr);
		return -EFAULT;
	}

	/*
	 * base address of kernel buffer
	 */
	*req = addr;

	return 0;
}

/**
 * pfm_acquire_ctx_from_fd -- get ctx from file descriptor
 * @fd: file descriptor
 * @ctx: pointer to pointer of context updated on return
 * @cookie: opaque structure to use for release
 *
 * This helper function extracts the ctx from the file descriptor.
 * It also increments the refcount of the file structure. Thus
 * it updates the cookie so the refcount can be decreased when
 * leaving the perfmon syscall via pfm_release_ctx_from_fd
 */
static int pfm_acquire_ctx_from_fd(int fd, struct pfm_context **ctx,
				   struct pfm_syscall_cookie *cookie)
{
	struct file *filp;
	int fput_needed;

	filp = fget_light(fd, &fput_needed);
	if (unlikely(filp == NULL)) {
		PFM_DBG("invalid fd %d", fd);
		return -EBADF;
	}

	*ctx = filp->private_data;

	if (unlikely(!*ctx || filp->f_op != &pfm_file_ops)) {
		PFM_DBG("fd %d not related to perfmon", fd);
		return -EBADF;
	}
	cookie->filp = filp;
	cookie->fput_needed = fput_needed;

	return 0;
}

/**
 * pfm_release_ctx_from_fd -- decrease refcount of file associated with context
 * @cookie: the cookie structure initialized by pfm_acquire_ctx_from_fd
 */
static inline void pfm_release_ctx_from_fd(struct pfm_syscall_cookie *cookie)
{
	fput_light(cookie->filp, cookie->fput_needed);
}

/*
 * unlike the other perfmon system calls, this one returns a file descriptor
 * or a value < 0 in case of error, very much like open() or socket()
 */
asmlinkage long sys_pfm_create_context(struct pfarg_ctx __user *ureq,
				       char __user *fmt_name,
				       void __user *fmt_uarg, size_t fmt_size)
{
	struct pfarg_ctx req;
	struct pfm_smpl_fmt *fmt = NULL;
	void *fmt_arg = NULL;
	int ret;

	PFM_DBG("req=%p fmt=%p fmt_arg=%p size=%zu",
		ureq, fmt_name, fmt_uarg, fmt_size);

	if (perfmon_disabled)
		return -ENOSYS;

	if (copy_from_user(&req, ureq, sizeof(req)))
		return -EFAULT;

	if (fmt_name) {
		ret = pfm_get_smpl_arg(fmt_name, fmt_uarg, fmt_size, &fmt_arg, &fmt);
		if (ret)
			goto abort;
	}

	ret = __pfm_create_context(&req, fmt, fmt_arg, PFM_NORMAL, NULL);

	kfree(fmt_arg);
abort:
	return ret;
}

asmlinkage long sys_pfm_write_pmcs(int fd, struct pfarg_pmc __user *ureq, int count)
{
	struct pfm_context *ctx;
	struct task_struct *task;
	struct pfm_syscall_cookie cookie;
	struct pfarg_pmc pmcs[PFM_PMC_STK_ARG];
	struct pfarg_pmc *req;
	void *fptr, *resume;
	unsigned long flags;
	size_t sz;
	int ret;

	PFM_DBG("fd=%d req=%p count=%d", fd, ureq, count);

	if (count < 0 || count >= PFM_MAX_ARG_COUNT(ureq)) {
		PFM_DBG("invalid arg count %d", count);
		return -EINVAL;
	}

	sz = count*sizeof(*ureq);

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	ret = pfm_get_args(ureq, sz, sizeof(pmcs), pmcs, (void **)&req, &fptr);
	if (ret)
		goto error;

	spin_lock_irqsave(&ctx->lock, flags);

	task = ctx->task;

	ret = pfm_check_task_state(ctx, PFM_CMD_STOPPED, &flags, &resume);
	if (!ret)
		ret = __pfm_write_pmcs(ctx, req, count);

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (resume)
		pfm_resume_task(task, resume);

	/*
	 * This function may be on the critical path.
	 * We want to avoid the branch if unecessary.
	 */
	if (fptr)
		kfree(fptr);
error:
	pfm_release_ctx_from_fd(&cookie);
	return ret;
}

asmlinkage long sys_pfm_write_pmds(int fd, struct pfarg_pmd __user *ureq, int count)
{
	struct pfm_context *ctx;
	struct task_struct *task;
	struct pfm_syscall_cookie cookie;
	struct pfarg_pmd pmds[PFM_PMD_STK_ARG];
	struct pfarg_pmd *req;
	void *fptr, *resume;
	unsigned long flags;
	size_t sz;
	int ret;

	PFM_DBG("fd=%d req=%p count=%d", fd, ureq, count);

	if (count < 0 || count >= PFM_MAX_ARG_COUNT(ureq)) {
		PFM_DBG("invalid arg count %d", count);
		return -EINVAL;
	}

	sz = count*sizeof(*ureq);

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	ret = pfm_get_args(ureq, sz, sizeof(pmds), pmds, (void **)&req, &fptr);
	if (ret)
		goto error;

	spin_lock_irqsave(&ctx->lock, flags);

	task = ctx->task;

	ret = pfm_check_task_state(ctx, PFM_CMD_STOPPED, &flags, &resume);
	if (!ret)
		ret = __pfm_write_pmds(ctx, req, count, 0);

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (resume)
		pfm_resume_task(task, resume);

	if (fptr)
		kfree(fptr);
error:
	pfm_release_ctx_from_fd(&cookie);
	return ret;
}

asmlinkage long sys_pfm_read_pmds(int fd, struct pfarg_pmd __user *ureq, int count)
{
	struct pfm_context *ctx;
	struct task_struct *task;
	struct pfm_syscall_cookie cookie;
	struct pfarg_pmd pmds[PFM_PMD_STK_ARG];
	struct pfarg_pmd *req;
	void *fptr, *resume;
	unsigned long flags;
	size_t sz;
	int ret;

	PFM_DBG("fd=%d req=%p count=%d", fd, ureq, count);

	if (count < 0 || count >= PFM_MAX_ARG_COUNT(ureq))
		return -EINVAL;

	sz = count*sizeof(*ureq);

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	ret = pfm_get_args(ureq, sz, sizeof(pmds), pmds, (void **)&req, &fptr);
	if (ret)
		goto error;

	spin_lock_irqsave(&ctx->lock, flags);

	task = ctx->task;

	ret = pfm_check_task_state(ctx, PFM_CMD_STOPPED, &flags, &resume);
	if (!ret)
		ret = __pfm_read_pmds(ctx, req, count);

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (copy_to_user(ureq, req, sz))
		ret = -EFAULT;

	if (resume)
		pfm_resume_task(task, resume);

	if (fptr)
		kfree(fptr);
error:
	pfm_release_ctx_from_fd(&cookie);
	return ret;
}

asmlinkage long sys_pfm_restart(int fd)
{
	struct pfm_context *ctx;
	struct task_struct *task;
	struct pfm_syscall_cookie cookie;
	void *resume;
	unsigned long flags;
	int ret, info;

	PFM_DBG("fd=%d", fd);

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	spin_lock_irqsave(&ctx->lock, flags);

	task = ctx->task;

	ret = pfm_check_task_state(ctx, 0, &flags, &resume);
	if (!ret)
		ret = __pfm_restart(ctx, &info);

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (resume)
		pfm_resume_task(task, resume);
	/*
	 * In per-thread mode with blocking notification, i.e.
	 * ctx->flags.blocking=1, we need to defer issuing the
	 * complete to unblock the blocked monitored thread.
	 * Otherwise we have a potential deadlock due to a lock
	 * inversion between the context lock and the task_rq_lock()
	 * which can happen if one thread is in this call and the other
	 * (the monitored thread) is in the context switch code.
	 *
	 * It is safe to access the context outside the critical section
	 * because:
	 * 	- we are protected by the fget_light(), thus the context
	 * 	  cannot disappear
	 */
	if (ret == 0 && info == 1)
		complete(&ctx->restart_complete);

	pfm_release_ctx_from_fd(&cookie);
	return ret;
}

asmlinkage long sys_pfm_stop(int fd)
{
	struct pfm_context *ctx;
	struct task_struct *task;
	struct pfm_syscall_cookie cookie;
	void *resume;
	unsigned long flags;
	int ret;
	int release_info;

	PFM_DBG("fd=%d", fd);

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	spin_lock_irqsave(&ctx->lock, flags);

	task = ctx->task;

	ret = pfm_check_task_state(ctx, PFM_CMD_STOPPED, &flags, &resume);
	if (!ret)
		ret = __pfm_stop(ctx, &release_info);

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (resume)
		pfm_resume_task(task, resume);

	/*
	 * defer cancellation of timer to avoid race
	 * with pfm_handle_switch_timeout()
	 *
	 * applies only when self-monitoring
	 */
	if (release_info & 0x2)
		hrtimer_cancel(&__get_cpu_var(pfm_hrtimer));

	pfm_release_ctx_from_fd(&cookie);
	return ret;
}

asmlinkage long sys_pfm_start(int fd, struct pfarg_start __user *ureq)
{
	struct pfm_context *ctx;
	struct task_struct *task;
	struct pfm_syscall_cookie cookie;
	void *resume;
	struct pfarg_start req;
	unsigned long flags;
	int ret;

	PFM_DBG("fd=%d req=%p", fd, ureq);

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	/*
	 * the one argument is actually optional
	 */
	if (ureq && copy_from_user(&req, ureq, sizeof(req)))
		return -EFAULT;

	spin_lock_irqsave(&ctx->lock, flags);

	task = ctx->task;

	ret = pfm_check_task_state(ctx, PFM_CMD_STOPPED, &flags, &resume);
	if (!ret)
		ret = __pfm_start(ctx, ureq ? &req : NULL);

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (resume)
		pfm_resume_task(task, resume);

	pfm_release_ctx_from_fd(&cookie);
	return ret;
}

asmlinkage long sys_pfm_load_context(int fd, struct pfarg_load __user *ureq)
{
	struct pfm_context *ctx;
	struct task_struct *task;
	struct pfm_syscall_cookie cookie;
	void *resume, *dummy_resume;
	unsigned long flags;
	struct pfarg_load req;
	int ret;

	PFM_DBG("fd=%d req=%p", fd, ureq);

	if (copy_from_user(&req, ureq, sizeof(req)))
		return -EFAULT;

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	task = current;

	/*
	 * in per-thread mode (not self-monitoring), get a reference
	 * on task to monitor. This must be done with interrupts enabled
	 * Upon succesful return, refcount on task is increased.
	 *
	 * fget_light() is protecting the context.
	 */
	if (!ctx->flags.system && req.load_pid != current->pid) {
		ret = pfm_get_task(ctx, req.load_pid, &task, &resume);
		if (ret)
			goto error;
	}

	/*
	 * irqsave is required to avoid race in case context is already
	 * loaded or with switch timeout in the case of self-monitoring
	 */
	spin_lock_irqsave(&ctx->lock, flags);

	ret = pfm_check_task_state(ctx, PFM_CMD_UNLOADED, &flags, &dummy_resume);
	if (!ret)
		ret = __pfm_load_context(ctx, &req, task);

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (resume)
		pfm_resume_task(task, resume);

	/*
	 * in per-thread mode (not self-monitoring), we need
	 * to decrease refcount on task to monitor:
	 *   - load successful: we have a reference to the task in ctx->task
	 *   - load failed    : undo the effect of pfm_get_task()
	 */
	if (task != current)
		put_task_struct(task);
error:
	pfm_release_ctx_from_fd(&cookie);
	return ret;
}

asmlinkage long sys_pfm_unload_context(int fd)
{
	struct pfm_context *ctx;
	struct task_struct *task;
	struct pfm_syscall_cookie cookie;
	void *resume;
	unsigned long flags;
	int ret;
	int is_system, release_info = 0;
	u32 cpu;

	PFM_DBG("fd=%d", fd);

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	is_system = ctx->flags.system;

	spin_lock_irqsave(&ctx->lock, flags);

	cpu = ctx->cpu;
	task = ctx->task;

	ret = pfm_check_task_state(ctx, PFM_CMD_STOPPED|PFM_CMD_UNLOAD,
				   &flags, &resume);
	if (!ret)
		ret = __pfm_unload_context(ctx, &release_info);

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (resume)
		pfm_resume_task(task, resume);

	/*
	 * cancel time now that context is unlocked
	 * avoid race with pfm_handle_switch_timeout()
	 */
	if (release_info & 0x2) {
		int r;
		r = hrtimer_cancel(&__get_cpu_var(pfm_hrtimer));
		PFM_DBG("timeout cancel=%d", r);
	}

	if (release_info & 0x1)
		pfm_session_release(is_system, cpu);

	pfm_release_ctx_from_fd(&cookie);
	return ret;
}

asmlinkage long sys_pfm_create_evtsets(int fd, struct pfarg_setdesc __user *ureq, int count)
{
	struct pfm_context *ctx;
	struct pfm_syscall_cookie cookie;
	struct pfarg_setdesc *req;
	void *fptr, *resume;
	unsigned long flags;
	size_t sz;
	int ret;

	PFM_DBG("fd=%d req=%p count=%d", fd, ureq, count);

	if (count < 0 || count >= PFM_MAX_ARG_COUNT(ureq))
		return -EINVAL;

	sz = count*sizeof(*ureq);

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	ret = pfm_get_args(ureq, sz, 0, NULL, (void **)&req, &fptr);
	if (ret)
		goto error;

	/*
	 * must mask interrupts because we do not know the state of context,
	 * could be attached and we could be getting PMU interrupts. So
	 * we mask and lock context and we check and possibly relax masking
	 */
	spin_lock_irqsave(&ctx->lock, flags);

	ret = pfm_check_task_state(ctx, PFM_CMD_UNLOADED, &flags, &resume);
	if (!ret)
		ret = __pfm_create_evtsets(ctx, req, count);

	spin_unlock_irqrestore(&ctx->lock, flags);
	/*
	 * context must be unloaded for this command. The resume pointer
	 * is necessarily NULL, thus no need to call pfm_resume_task()
	 */
	kfree(fptr);

error:
	pfm_release_ctx_from_fd(&cookie);
	return ret;
}

asmlinkage long  sys_pfm_getinfo_evtsets(int fd, struct pfarg_setinfo __user *ureq, int count)
{
	struct pfm_context *ctx;
	struct task_struct *task;
	struct pfm_syscall_cookie cookie;
	struct pfarg_setinfo *req;
	void *fptr, *resume;
	unsigned long flags;
	size_t sz;
	int ret;

	PFM_DBG("fd=%d req=%p count=%d", fd, ureq, count);

	if (count < 0 || count >= PFM_MAX_ARG_COUNT(ureq))
		return -EINVAL;

	sz = count*sizeof(*ureq);

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	ret = pfm_get_args(ureq, sz, 0, NULL, (void **)&req, &fptr);
	if (ret)
		goto error;

	/*
	 * this command operates even when context is loaded, so we need
	 * to keep interrupts masked to avoid a race with PMU interrupt
	 * which may switch the active set
	 */
	spin_lock_irqsave(&ctx->lock, flags);

	task = ctx->task;

	ret = pfm_check_task_state(ctx, 0, &flags, &resume);
	if (!ret)
		ret = __pfm_getinfo_evtsets(ctx, req, count);

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (resume)
		pfm_resume_task(task, resume);

	if (copy_to_user(ureq, req, sz))
		ret = -EFAULT;

	kfree(fptr);
error:
	pfm_release_ctx_from_fd(&cookie);
	return ret;
}

asmlinkage long sys_pfm_delete_evtsets(int fd, struct pfarg_setinfo __user *ureq, int count)
{
	struct pfm_context *ctx;
	struct pfm_syscall_cookie cookie;
	struct pfarg_setinfo *req;
	void *fptr, *resume;
	unsigned long flags;
	size_t sz;
	int ret;

	PFM_DBG("fd=%d req=%p count=%d", fd, ureq, count);

	if (count < 0 || count >= PFM_MAX_ARG_COUNT(ureq))
		return -EINVAL;

	sz = count*sizeof(*ureq);

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	ret = pfm_get_args(ureq, sz, 0, NULL, (void **)&req, &fptr);
	if (ret)
		goto error;

	/*
	 * must mask interrupts because we do not know the state of context,
	 * could be attached and we could be getting PMU interrupts
	 */
	spin_lock_irqsave(&ctx->lock, flags);

	ret = pfm_check_task_state(ctx, PFM_CMD_UNLOADED, &flags, &resume);
	if (!ret)
		ret = __pfm_delete_evtsets(ctx, req, count);

	spin_unlock_irqrestore(&ctx->lock, flags);
	/*
	 * context must be unloaded for this command. The resume pointer
	 * is necessarily NULL, thus no need to call pfm_resume_task()
	 */
	kfree(fptr);

error:
	pfm_release_ctx_from_fd(&cookie);
	return ret;
}

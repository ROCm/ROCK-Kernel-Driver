/*
 * perfmon_file.c: perfmon2 file input/output functions
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
#include <linux/module.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/vfs.h>
#include <linux/pagemap.h>
#include <linux/mount.h>
#include <linux/perfmon_kern.h>
#include "perfmon_priv.h"

#define PFMFS_MAGIC 0xa0b4d889	/* perfmon filesystem magic number */

struct pfm_controls pfm_controls = {
	.sys_group = PFM_GROUP_PERM_ANY,
	.task_group = PFM_GROUP_PERM_ANY,
	.arg_mem_max = PAGE_SIZE,
	.smpl_buffer_mem_max = ~0,
};
EXPORT_SYMBOL(pfm_controls);

static int __init enable_debug(char *str)
{
	pfm_controls.debug = 1;
	PFM_INFO("debug output enabled\n");
	return 1;
}
__setup("perfmon_debug", enable_debug);

static int pfmfs_delete_dentry(struct dentry *dentry)
{
	return 1;
}

static struct dentry_operations pfmfs_dentry_operations = {
	.d_delete = pfmfs_delete_dentry,
};

int pfm_buf_map_pagefault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	void *kaddr;
	unsigned long address;
	struct pfm_context *ctx;
	size_t size;

	address = (unsigned long)vmf->virtual_address;

	ctx = vma->vm_private_data;
	if (ctx == NULL) {
		PFM_DBG("no ctx");
		return VM_FAULT_SIGBUS;
	}
	/*
	 * size available to user (maybe different from real_smpl_size
	 */
	size = ctx->smpl_size;

	if ((address < vma->vm_start) ||
	    (address >= (vma->vm_start + size)))
		return VM_FAULT_SIGBUS;

	kaddr = ctx->smpl_addr + (address - vma->vm_start);

	vmf->page = vmalloc_to_page(kaddr);
	get_page(vmf->page);

	PFM_DBG("[%d] start=%p ref_count=%d",
		current->pid,
		kaddr, page_count(vmf->page));

	return 0;
}

/*
 * we need to determine whther or not we are closing the last reference
 * to the file and thus are going to end up in pfm_close() which eventually
 * calls pfm_release_buf_space(). In that function, we update the accouting
 * for locked_vm given that we are actually freeing the sampling buffer. The
 * issue is that there are multiple paths leading to pfm_release_buf_space(),
 * from exit(), munmap(), close(). The path coming from munmap() is problematic
 * becuse do_munmap() grabs mmap_sem in write-mode which is also what
 * pfm_release_buf_space does. To avoid deadlock, we need to determine where
 * we are calling from and skip the locking. The vm_ops->close() callback
 * is invoked for each remove_vma() independently of the number of references
 * left on the file descriptor, therefore simple reference counter does not
 * work. We need to determine if this is the last call, and then set a flag
 * to skip the locking.
 */
static void pfm_buf_map_close(struct vm_area_struct *vma)
{
	struct file *file;
	struct pfm_context *ctx;

	file = vma->vm_file;
	ctx = vma->vm_private_data;

	/*
	 * if file is going to close, then pfm_close() will
	 * be called, do not lock in pfm_release_buf
	 */
	if (atomic_read(&file->f_count) == 1)
		ctx->flags.mmap_nlock = 1;
}

/*
 * we do not have a close callback because, the locked
 * memory accounting must be done when the actual buffer
 * is freed. Munmap does not free the page backing the vma
 * because they may still be in use by the PMU interrupt handler.
 */
struct vm_operations_struct pfm_buf_map_vm_ops = {
	.fault = pfm_buf_map_pagefault,
	.close = pfm_buf_map_close
};

static int pfm_mmap_buffer(struct pfm_context *ctx, struct vm_area_struct *vma,
			   size_t size)
{
	if (ctx->smpl_addr == NULL) {
		PFM_DBG("no sampling buffer to map");
		return -EINVAL;
	}

	if (size > ctx->smpl_size) {
		PFM_DBG("mmap size=%zu >= actual buf size=%zu",
			size,
			ctx->smpl_size);
		return -EINVAL;
	}

	vma->vm_ops = &pfm_buf_map_vm_ops;
	vma->vm_private_data = ctx;

	return 0;
}

static int pfm_mmap(struct file *file, struct vm_area_struct *vma)
{
	size_t size;
	struct pfm_context *ctx;
	unsigned long flags;
	int ret;

	PFM_DBG("pfm_file_ops");

	ctx  = file->private_data;
	size = (vma->vm_end - vma->vm_start);

	if (ctx == NULL)
		return -EINVAL;

	ret = -EINVAL;

	spin_lock_irqsave(&ctx->lock, flags);

	if (vma->vm_flags & VM_WRITE) {
		PFM_DBG("cannot map buffer for writing");
		goto done;
	}

	PFM_DBG("vm_pgoff=%lu size=%zu vm_start=0x%lx",
		vma->vm_pgoff,
		size,
		vma->vm_start);

	ret = pfm_mmap_buffer(ctx, vma, size);
	if (ret == 0)
		vma->vm_flags |= VM_RESERVED;

	PFM_DBG("ret=%d vma_flags=0x%lx vma_start=0x%lx vma_size=%lu",
		ret,
		vma->vm_flags,
		vma->vm_start,
		vma->vm_end-vma->vm_start);
done:
	spin_unlock_irqrestore(&ctx->lock, flags);

	return ret;
}

/*
 * Extract one message from queue.
 *
 * return:
 * 	-EAGAIN:  when non-blocking and nothing is* in the queue.
 * 	-ERESTARTSYS: when blocking and signal is pending
 * 	Otherwise returns size of message (sizeof(pfarg_msg))
 */
ssize_t __pfm_read(struct pfm_context *ctx, union pfarg_msg *msg_buf, int non_block)
{
	ssize_t ret = 0;
	unsigned long flags;
	DECLARE_WAITQUEUE(wait, current);

	/*
	 * we must masks interrupts to avoid a race condition
	 * with the PMU interrupt handler.
	 */
	spin_lock_irqsave(&ctx->lock, flags);

	while (pfm_msgq_is_empty(ctx)) {

		/*
		 * handle non-blocking reads
		 * return -EAGAIN
		 */
		ret = -EAGAIN;
		if (non_block)
			break;

		add_wait_queue(&ctx->msgq_wait, &wait);
		set_current_state(TASK_INTERRUPTIBLE);

		spin_unlock_irqrestore(&ctx->lock, flags);

		schedule();

		/*
		 * during this window, another thread may call
		 * pfm_read() and steal our message
		 */

		spin_lock_irqsave(&ctx->lock, flags);

		remove_wait_queue(&ctx->msgq_wait, &wait);
		set_current_state(TASK_RUNNING);

		/*
		 * check for pending signals
		 * return -ERESTARTSYS
		 */
		ret = -ERESTARTSYS;
		if (signal_pending(current))
			break;

		/*
		 * we may have a message
		 */
		ret = 0;
	}

	/*
	 * extract message
	 */
	if (ret == 0) {
		/*
		 * copy the oldest message into msg_buf.
		 * We cannot directly call copy_to_user()
		 * because interrupts masked. This is done
		 * in the caller
		 */
		pfm_get_next_msg(ctx, msg_buf);

		ret = sizeof(*msg_buf);

		PFM_DBG("extracted type=%d", msg_buf->type);
	}

	spin_unlock_irqrestore(&ctx->lock, flags);

	PFM_DBG("blocking=%d ret=%zd", non_block, ret);

	return ret;
}

static ssize_t pfm_read(struct file *filp, char __user *buf, size_t size,
			loff_t *ppos)
{
	struct pfm_context *ctx;
	union pfarg_msg msg_buf;
	int non_block, ret;

	PFM_DBG_ovfl("buf=%p size=%zu", buf, size);

	ctx = filp->private_data;
	if (ctx == NULL) {
		PFM_ERR("no ctx for pfm_read");
		return -EINVAL;
	}

	non_block = filp->f_flags & O_NONBLOCK;

#ifdef CONFIG_IA64_PERFMON_COMPAT
	/*
	 * detect IA-64 v2.0 context read (message size is different)
	 * nops on all other architectures
	 */
	if (unlikely(ctx->flags.ia64_v20_compat))
		return pfm_arch_compat_read(ctx,  buf, non_block, size);
#endif
	/*
	 * cannot extract partial messages.
	 * check even when there is no message
	 *
	 * cannot extract more than one message per call. Bytes
	 * above sizeof(msg) are ignored.
	 */
	if (size < sizeof(msg_buf)) {
		PFM_DBG("message is too small size=%zu must be >=%zu)",
			size,
			sizeof(msg_buf));
		return -EINVAL;
	}

	ret =  __pfm_read(ctx, &msg_buf, non_block);
	if (ret > 0) {
		if (copy_to_user(buf, &msg_buf, sizeof(msg_buf)))
			ret = -EFAULT;
	}
	PFM_DBG_ovfl("ret=%d", ret);
	return ret;
}

static ssize_t pfm_write(struct file *file, const char __user *ubuf,
			  size_t size, loff_t *ppos)
{
	PFM_DBG("pfm_write called");
	return -EINVAL;
}

static unsigned int pfm_poll(struct file *filp, poll_table *wait)
{
	struct pfm_context *ctx;
	unsigned long flags;
	unsigned int mask = 0;

	PFM_DBG("pfm_file_ops");

	if (filp->f_op != &pfm_file_ops) {
		PFM_ERR("pfm_poll bad magic");
		return 0;
	}

	ctx = filp->private_data;
	if (ctx == NULL) {
		PFM_ERR("pfm_poll no ctx");
		return 0;
	}

	PFM_DBG("before poll_wait");

	poll_wait(filp, &ctx->msgq_wait, wait);

	/*
	 * pfm_msgq_is_empty() is non-atomic
	 *
	 * filp is protected by fget() at upper level
	 * context cannot be closed by another thread.
	 *
	 * There may be a race with a PMU interrupt adding
	 * messages to the queue. But we are interested in
	 * queue not empty, so adding more messages should
	 * not really be a problem.
	 *
	 * There may be a race with another thread issuing
	 * a read() and stealing messages from the queue thus
	 * may return the wrong answer. This could potentially
	 * lead to a blocking read, because nothing is
	 * available in the queue
	 */
	spin_lock_irqsave(&ctx->lock, flags);

	if (!pfm_msgq_is_empty(ctx))
		mask =  POLLIN | POLLRDNORM;

	spin_unlock_irqrestore(&ctx->lock, flags);

	PFM_DBG("after poll_wait mask=0x%x", mask);

	return mask;
}

static int pfm_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		     unsigned long arg)
{
	PFM_DBG("pfm_ioctl called");
	return -EINVAL;
}

/*
 * interrupt cannot be masked when entering this function
 */
static inline int __pfm_fasync(int fd, struct file *filp,
			       struct pfm_context *ctx, int on)
{
	int ret;

	PFM_DBG("in  fd=%d on=%d async_q=%p",
		fd,
		on,
		ctx->async_queue);

	ret = fasync_helper(fd, filp, on, &ctx->async_queue);

	PFM_DBG("out fd=%d on=%d async_q=%p ret=%d",
		fd,
		on,
		ctx->async_queue, ret);

	return ret;
}

static int pfm_fasync(int fd, struct file *filp, int on)
{
	struct pfm_context *ctx;
	int ret;

	PFM_DBG("pfm_file_ops");

	ctx = filp->private_data;
	if (ctx == NULL) {
		PFM_ERR("pfm_fasync no ctx");
		return -EBADF;
	}

	/*
	 * we cannot mask interrupts during this call because this may
	 * may go to sleep if memory is not readily avalaible.
	 *
	 * We are protected from the context disappearing by the
	 * get_fd()/put_fd() done in caller. Serialization of this function
	 * is ensured by caller.
	 */
	ret = __pfm_fasync(fd, filp, ctx, on);

	PFM_DBG("pfm_fasync called on fd=%d on=%d async_queue=%p ret=%d",
		fd,
		on,
		ctx->async_queue, ret);

	return ret;
}

#ifdef CONFIG_SMP
static void __pfm_close_remote_cpu(void *info)
{
	struct pfm_context *ctx = info;
	int can_release;

	BUG_ON(ctx != __get_cpu_var(pmu_ctx));

	/*
	 * we are in IPI interrupt handler which has always higher
	 * priority than PMU interrupt, therefore we do not need to
	 * mask interrupts. context locking is not needed because we
	 * are in close(), no more user references.
	 *
	 * can_release is ignored, release done on calling CPU
	 */
	__pfm_unload_context(ctx, &can_release);

	/*
	 * we cannot free context here because we are in_interrupt().
	 * we free on the calling CPU
	 */
}

static int pfm_close_remote_cpu(u32 cpu, struct pfm_context *ctx)
{
	BUG_ON(irqs_disabled());
	return smp_call_function_single(cpu, __pfm_close_remote_cpu, ctx, 1);
}
#endif /* CONFIG_SMP */

/*
 * called either on explicit close() or from exit_files().
 * Only the LAST user of the file gets to this point, i.e., it is
 * called only ONCE.
 *
 * IMPORTANT: we get called ONLY when the refcnt on the file gets to zero
 * (fput()),i.e, last task to access the file. Nobody else can access the
 * file at this point.
 *
 * When called from exit_files(), the VMA has been freed because exit_mm()
 * is executed before exit_files().
 *
 * When called from exit_files(), the current task is not yet ZOMBIE but we
 * flush the PMU state to the context.
 */
int __pfm_close(struct pfm_context *ctx, struct file *filp)
{
	unsigned long flags;
	int state;
	int can_free = 1, can_unload = 1;
	int is_system, can_release = 0;
	u32 cpu;

	/*
	 * no risk of ctx of filp disappearing so we can operate outside
	 * of spin_lock(). fasync_helper() runs with interrupts masked,
	 * thus there is no risk with the PMU interrupt handler
	 *
	 * In case of zombie, we will not have the async struct anymore
	 * thus kill_fasync() will not do anything
	 *
	 * fd is not used when removing the entry so we pass -1
	 */
	if (filp->f_flags & FASYNC)
		__pfm_fasync (-1, filp, ctx, 0);

	spin_lock_irqsave(&ctx->lock, flags);

	state = ctx->state;
	is_system = ctx->flags.system;
	cpu = ctx->cpu;

	PFM_DBG("state=%d", state);

	/*
	 * check if unload is needed
	 */
	if (state == PFM_CTX_UNLOADED)
		goto doit;

#ifdef CONFIG_SMP
	/*
	 * we need to release the resource on the ORIGINAL cpu.
	 * we need to release the context lock to avoid deadlocks
	 * on the original CPU, especially in the context switch
	 * routines. It is safe to unlock because we are in close(),
	 * in other words, there is no more access from user level.
	 * we can also unmask interrupts on this CPU because the
	 * context is running on the original CPU. Context will be
	 * unloaded and the session will be released on the original
	 * CPU. Upon return, the caller is guaranteed that the context
	 * is gone from original CPU.
	 */
	if (is_system && cpu != smp_processor_id()) {
		spin_unlock_irqrestore(&ctx->lock, flags);
		pfm_close_remote_cpu(cpu, ctx);
		can_release = 1;
		goto free_it;
	}

	if (!is_system && ctx->task != current) {
		/*
		 * switch context to zombie state
		 */
		ctx->state = PFM_CTX_ZOMBIE;

		PFM_DBG("zombie ctx for [%d]", ctx->task->pid);
		/*
		 * must check if other thread is using block overflow
		 * notification mode. If so make sure it will not block
		 * because there will not be any pfm_restart() issued.
		 * When the thread notices the ZOMBIE state, it will clean
		 * up what is left of the context
		 */
		if (state == PFM_CTX_MASKED && ctx->flags.block) {
			/*
			 * force task to wake up from MASKED state
			 */
			PFM_DBG("waking up [%d]", ctx->task->pid);

			complete(&ctx->restart_complete);
		}
		/*
		 * PMU session will be release by monitored task when it notices
		 * ZOMBIE state as part of pfm_unload_context()
		 */
		can_unload = can_free = 0;
	}
#endif
	if (can_unload)
		__pfm_unload_context(ctx, &can_release);
doit:
	spin_unlock_irqrestore(&ctx->lock, flags);

#ifdef CONFIG_SMP
free_it:
#endif
	if (can_release)
		pfm_session_release(is_system, cpu);

	if (can_free)
		pfm_free_context(ctx);

	return 0;
}

static int pfm_close(struct inode *inode, struct file *filp)
{
	struct pfm_context *ctx;

	PFM_DBG("called filp=%p", filp);

	ctx = filp->private_data;
	if (ctx == NULL) {
		PFM_ERR("no ctx");
		return -EBADF;
	}
	return __pfm_close(ctx, filp);
}

static int pfm_no_open(struct inode *irrelevant, struct file *dontcare)
{
	PFM_DBG("pfm_file_ops");

	return -ENXIO;
}


const struct file_operations pfm_file_ops = {
	.llseek = no_llseek,
	.read = pfm_read,
	.write = pfm_write,
	.poll = pfm_poll,
	.ioctl = pfm_ioctl,
	.open = pfm_no_open, /* special open to disallow open via /proc */
	.fasync = pfm_fasync,
	.release = pfm_close,
	.mmap = pfm_mmap
};

static int pfmfs_get_sb(struct file_system_type *fs_type,
			int flags, const char *dev_name,
			void *data, struct vfsmount *mnt)
{
	return get_sb_pseudo(fs_type, "pfm:", NULL, PFMFS_MAGIC, mnt);
}

static struct file_system_type pfm_fs_type = {
	.name     = "pfmfs",
	.get_sb   = pfmfs_get_sb,
	.kill_sb  = kill_anon_super,
};

/*
 * pfmfs should _never_ be mounted by userland - too much of security hassle,
 * no real gain from having the whole whorehouse mounted. So we don't need
 * any operations on the root directory. However, we need a non-trivial
 * d_name - pfm: will go nicely and kill the special-casing in procfs.
 */
static struct vfsmount *pfmfs_mnt;

int __init pfm_init_fs(void)
{
	int err = register_filesystem(&pfm_fs_type);
	if (!err) {
		pfmfs_mnt = kern_mount(&pfm_fs_type);
		err = PTR_ERR(pfmfs_mnt);
		if (IS_ERR(pfmfs_mnt))
			unregister_filesystem(&pfm_fs_type);
		else
			err = 0;
	}
	return err;
}

int pfm_alloc_fd(struct file **cfile)
{
	int fd, ret = 0;
	struct file *file = NULL;
	struct inode * inode;
	char name[32];
	struct qstr this;

	fd = get_unused_fd();
	if (fd < 0)
		return -ENFILE;

	ret = -ENFILE;

	file = get_empty_filp();
	if (!file)
		goto out;

	/*
	 * allocate a new inode
	 */
	inode = new_inode(pfmfs_mnt->mnt_sb);
	if (!inode)
		goto out;

	PFM_DBG("new inode ino=%ld @%p", inode->i_ino, inode);

	inode->i_sb = pfmfs_mnt->mnt_sb;
	inode->i_mode = S_IFCHR|S_IRUGO;
	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;

	sprintf(name, "[%lu]", inode->i_ino);
	this.name = name;
	this.hash = inode->i_ino;
	this.len = strlen(name);

	ret = -ENOMEM;

	/*
	 * allocate a new dcache entry
	 */
	file->f_dentry = d_alloc(pfmfs_mnt->mnt_sb->s_root, &this);
	if (!file->f_dentry)
		goto out;

	file->f_dentry->d_op = &pfmfs_dentry_operations;

	d_add(file->f_dentry, inode);
	file->f_vfsmnt = mntget(pfmfs_mnt);
	file->f_mapping = inode->i_mapping;

	file->f_op = &pfm_file_ops;
	file->f_mode = FMODE_READ;
	file->f_flags = O_RDONLY;
	file->f_pos  = 0;

	*cfile = file;

	return fd;
out:
	if (file)
		put_filp(file);
	put_unused_fd(fd);
	return ret;
}

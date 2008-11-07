/*
 * This file implements the IA-64 specific
 * support for the perfmon2 interface
 *
 * Copyright (c) 1999-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
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
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/perfmon_kern.h>
#include <linux/uaccess.h>

asmlinkage long sys_pfm_stop(int fd);
asmlinkage long sys_pfm_start(int fd, struct pfarg_start __user *st);
asmlinkage long sys_pfm_unload_context(int fd);
asmlinkage long sys_pfm_restart(int fd);
asmlinkage long sys_pfm_load_context(int fd, struct pfarg_load __user *ld);

ssize_t pfm_sysfs_res_show(char *buf, size_t sz, int what);

extern ssize_t __pfm_read(struct pfm_context *ctx,
			  union pfarg_msg *msg_buf,
			  int non_block);
/*
 * function providing some help for backward compatiblity with old IA-64
 * applications. In the old model, certain attributes of a counter were
 * passed via the PMC, now they are passed via the PMD.
 */
static int pfm_compat_update_pmd(struct pfm_context *ctx, u16 set_id, u16 cnum,
				 u32 rflags,
				 unsigned long *smpl_pmds,
				 unsigned long *reset_pmds,
				 u64 eventid)
{
	struct pfm_event_set *set;
	int is_counting;
	unsigned long *impl_pmds;
	u32 flags = 0;
	u16 max_pmd;

	impl_pmds = ctx->regs.pmds;
	max_pmd	= ctx->regs.max_pmd;

	/*
	 * given that we do not maintain PMC ->PMD dependencies
	 * we cannot figure out what to do in case PMCxx != PMDxx
	 */
	if (cnum > max_pmd)
		return 0;

	/*
	 * assumes PMCxx controls PMDxx which is always true for counters
	 * on Itanium PMUs.
	 */
	is_counting = pfm_pmu_conf->pmd_desc[cnum].type & PFM_REG_C64;
	set = pfm_find_set(ctx, set_id, 0);

	/*
	 * for v2.0, we only allowed counting PMD to generate
	 * user-level notifications. Same thing with randomization.
	 */
	if (is_counting) {
		if (rflags & PFM_REGFL_OVFL_NOTIFY)
			flags |= PFM_REGFL_OVFL_NOTIFY;
		if (rflags & PFM_REGFL_RANDOM)
			flags |= PFM_REGFL_RANDOM;
		/*
		 * verify validity of smpl_pmds
		 */
		if (unlikely(bitmap_subset(smpl_pmds,
					   impl_pmds, max_pmd) == 0)) {
			PFM_DBG("invalid smpl_pmds=0x%llx for pmd%u",
				  (unsigned long long)smpl_pmds[0], cnum);
			return -EINVAL;
		}
		/*
		 * verify validity of reset_pmds
		 */
		if (unlikely(bitmap_subset(reset_pmds,
					   impl_pmds, max_pmd) == 0)) {
			PFM_DBG("invalid reset_pmds=0x%lx for pmd%u",
				  reset_pmds[0], cnum);
			return -EINVAL;
		}
		/*
		 * ensures that a PFM_READ_PMDS succeeds with a
		 * corresponding PFM_WRITE_PMDS
		 */
		__set_bit(cnum, set->used_pmds);

	} else if (rflags & (PFM_REGFL_OVFL_NOTIFY|PFM_REGFL_RANDOM)) {
		PFM_DBG("cannot set ovfl_notify or random on pmd%u", cnum);
		return -EINVAL;
	}

	set->pmds[cnum].flags = flags;

	if (is_counting) {
		bitmap_copy(set->pmds[cnum].reset_pmds,
			    reset_pmds,
			    max_pmd);

		bitmap_copy(set->pmds[cnum].smpl_pmds,
			    smpl_pmds,
			    max_pmd);

		set->pmds[cnum].eventid = eventid;

		/*
		 * update ovfl_notify
		 */
		if (rflags & PFM_REGFL_OVFL_NOTIFY)
			__set_bit(cnum, set->ovfl_notify);
		else
			__clear_bit(cnum, set->ovfl_notify);

	}
	PFM_DBG("pmd%u flags=0x%x eventid=0x%lx r_pmds=0x%lx s_pmds=0x%lx",
		  cnum, flags,
		  eventid,
		  reset_pmds[0],
		  smpl_pmds[0]);

	return 0;
}


int __pfm_write_ibrs_old(struct pfm_context *ctx, void *arg, int count)
{
	struct pfarg_dbreg *req = arg;
	struct pfarg_pmc pmc;
	int i, ret = 0;

	memset(&pmc, 0, sizeof(pmc));

	for (i = 0; i < count; i++, req++) {
		pmc.reg_num   = 256+req->dbreg_num;
		pmc.reg_value = req->dbreg_value;
		pmc.reg_flags = 0;
		pmc.reg_set   = req->dbreg_set;

		ret = __pfm_write_pmcs(ctx, &pmc, 1);

		req->dbreg_flags &= ~PFM_REG_RETFL_MASK;
		req->dbreg_flags |= pmc.reg_flags;

		if (ret)
			return ret;
	}
	return 0;
}

static long pfm_write_ibrs_old(int fd, void __user *ureq, int count)
{
	struct pfm_context *ctx;
	struct task_struct *task;
	struct file *filp;
	struct pfarg_dbreg *req = NULL;
	void *fptr, *resume;
	unsigned long flags;
	size_t sz;
	int ret, fput_needed;

	if (count < 1 || count >= PFM_MAX_ARG_COUNT(req))
		return -EINVAL;

	sz = count*sizeof(*req);

	filp = fget_light(fd, &fput_needed);
	if (unlikely(filp == NULL)) {
		PFM_DBG("invalid fd %d", fd);
		return -EBADF;
	}

	ctx = filp->private_data;
	ret = -EBADF;

	if (unlikely(!ctx || filp->f_op != &pfm_file_ops)) {
		PFM_DBG("fd %d not related to perfmon", fd);
		goto error;
	}

	ret = pfm_get_args(ureq, sz, 0, NULL, (void **)&req, &fptr);
	if (ret)
		goto error;

	spin_lock_irqsave(&ctx->lock, flags);

	task = ctx->task;

	ret = pfm_check_task_state(ctx, PFM_CMD_STOPPED, &flags, &resume);
	if (ret == 0)
		ret = __pfm_write_ibrs_old(ctx, req, count);

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (resume)
		pfm_resume_task(task, resume);

	if (copy_to_user(ureq, req, sz))
		ret = -EFAULT;

	kfree(fptr);
error:
	fput_light(filp, fput_needed);
	return ret;
}

int __pfm_write_dbrs_old(struct pfm_context *ctx, void *arg, int count)
{
	struct pfarg_dbreg *req = arg;
	struct pfarg_pmc pmc;
	int i, ret = 0;

	memset(&pmc, 0, sizeof(pmc));

	for (i = 0; i < count; i++, req++) {
		pmc.reg_num   = 264+req->dbreg_num;
		pmc.reg_value = req->dbreg_value;
		pmc.reg_flags = 0;
		pmc.reg_set   = req->dbreg_set;

		ret = __pfm_write_pmcs(ctx, &pmc, 1);

		req->dbreg_flags &= ~PFM_REG_RETFL_MASK;
		req->dbreg_flags |= pmc.reg_flags;
		if (ret)
			return ret;
	}
	return 0;
}

static long pfm_write_dbrs_old(int fd, void __user *ureq, int count)
{
	struct pfm_context *ctx;
	struct task_struct *task;
	struct file *filp;
	struct pfarg_dbreg *req = NULL;
	void *fptr, *resume;
	unsigned long flags;
	size_t sz;
	int ret, fput_needed;

	if (count < 1 || count >= PFM_MAX_ARG_COUNT(req))
		return -EINVAL;

	sz = count*sizeof(*req);

	filp = fget_light(fd, &fput_needed);
	if (unlikely(filp == NULL)) {
		PFM_DBG("invalid fd %d", fd);
		return -EBADF;
	}

	ctx = filp->private_data;
	ret = -EBADF;

	if (unlikely(!ctx || filp->f_op != &pfm_file_ops)) {
		PFM_DBG("fd %d not related to perfmon", fd);
		goto error;
	}

	ret = pfm_get_args(ureq, sz, 0, NULL, (void **)&req, &fptr);
	if (ret)
		goto error;

	spin_lock_irqsave(&ctx->lock, flags);

	task = ctx->task;

	ret = pfm_check_task_state(ctx, PFM_CMD_STOPPED, &flags, &resume);
	if (ret == 0)
		ret = __pfm_write_dbrs_old(ctx, req, count);

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (resume)
		pfm_resume_task(task, resume);

	if (copy_to_user(ureq, req, sz))
		ret = -EFAULT;

	kfree(fptr);
error:
	fput_light(filp, fput_needed);
	return ret;
}

int __pfm_write_pmcs_old(struct pfm_context *ctx, struct pfarg_reg *req_old,
			 int count)
{
	struct pfarg_pmc req;
	unsigned int i;
	int ret, error_code;

	memset(&req, 0, sizeof(req));

	for (i = 0; i < count; i++, req_old++) {
		req.reg_num   = req_old->reg_num;
		req.reg_set   = req_old->reg_set;
		req.reg_flags = 0;
		req.reg_value = req_old->reg_value;

		ret = __pfm_write_pmcs(ctx, (void *)&req, 1);
		req_old->reg_flags &= ~PFM_REG_RETFL_MASK;
		req_old->reg_flags |= req.reg_flags;

		if (ret)
			return ret;

		ret = pfm_compat_update_pmd(ctx, req_old->reg_set,
				      req_old->reg_num,
				      (u32)req_old->reg_flags,
				      req_old->reg_smpl_pmds,
				      req_old->reg_reset_pmds,
				      req_old->reg_smpl_eventid);

		error_code = ret ? PFM_REG_RETFL_EINVAL : 0;
		req_old->reg_flags &= ~PFM_REG_RETFL_MASK;
		req_old->reg_flags |= error_code;

		if (ret)
			return ret;
	}
	return 0;
}

static long pfm_write_pmcs_old(int fd, void __user *ureq, int count)
{
	struct pfm_context *ctx;
	struct task_struct *task;
	struct file *filp;
	struct pfarg_reg *req = NULL;
	void *fptr, *resume;
	unsigned long flags;
	size_t sz;
	int ret, fput_needed;

	if (count < 1 || count >= PFM_MAX_ARG_COUNT(req))
		return -EINVAL;

	sz = count*sizeof(*req);

	filp = fget_light(fd, &fput_needed);
	if (unlikely(filp == NULL)) {
		PFM_DBG("invalid fd %d", fd);
		return -EBADF;
	}

	ctx = filp->private_data;
	ret = -EBADF;

	if (unlikely(!ctx || filp->f_op != &pfm_file_ops)) {
		PFM_DBG("fd %d not related to perfmon", fd);
		goto error;
	}

	ret = pfm_get_args(ureq, sz, 0, NULL, (void **)&req, &fptr);
	if (ret)
		goto error;

	spin_lock_irqsave(&ctx->lock, flags);

	task = ctx->task;

	ret = pfm_check_task_state(ctx, PFM_CMD_STOPPED, &flags, &resume);
	if (ret == 0)
		ret = __pfm_write_pmcs_old(ctx, req, count);

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (resume)
		pfm_resume_task(task, resume);

	if (copy_to_user(ureq, req, sz))
		ret = -EFAULT;

	kfree(fptr);

error:
	fput_light(filp, fput_needed);
	return ret;
}

int __pfm_write_pmds_old(struct pfm_context *ctx, struct pfarg_reg *req_old,
			 int count)
{
	struct pfarg_pmd req;
	int i, ret;

	memset(&req, 0, sizeof(req));

	for (i = 0; i < count; i++, req_old++) {
		req.reg_num   = req_old->reg_num;
		req.reg_set   = req_old->reg_set;
		req.reg_value = req_old->reg_value;
		/* flags passed with pmcs in v2.0 */

		req.reg_long_reset  = req_old->reg_long_reset;
		req.reg_short_reset = req_old->reg_short_reset;
		req.reg_random_mask = req_old->reg_random_mask;
		/*
		 * reg_random_seed is ignored since v2.3
		 */

		/*
		 * skip last_reset_val not used for writing
		 * skip smpl_pmds, reset_pmds, eventid, ovfl_swtch_cnt
		 * as set in pfm_write_pmcs_old.
		 *
		 * ovfl_switch_cnt ignored, not implemented in v2.0
		 */
		ret = __pfm_write_pmds(ctx, (void *)&req, 1, 1);

		req_old->reg_flags &= ~PFM_REG_RETFL_MASK;
		req_old->reg_flags |= req.reg_flags;

		if (ret)
			return ret;
	}
	return 0;
}

static long pfm_write_pmds_old(int fd, void __user *ureq, int count)
{
	struct pfm_context *ctx;
	struct task_struct *task;
	struct file *filp;
	struct pfarg_reg *req = NULL;
	void *fptr, *resume;
	unsigned long flags;
	size_t sz;
	int ret, fput_needed;

	if (count < 1 || count >= PFM_MAX_ARG_COUNT(req))
		return -EINVAL;

	sz = count*sizeof(*req);

	filp = fget_light(fd, &fput_needed);
	if (unlikely(filp == NULL)) {
		PFM_DBG("invalid fd %d", fd);
		return -EBADF;
	}

	ctx = filp->private_data;
	ret = -EBADF;

	if (unlikely(!ctx || filp->f_op != &pfm_file_ops)) {
		PFM_DBG("fd %d not related to perfmon", fd);
		goto error;
	}

	ret = pfm_get_args(ureq, sz, 0, NULL, (void **)&req, &fptr);
	if (ret)
		goto error;

	spin_lock_irqsave(&ctx->lock, flags);

	task = ctx->task;

	ret = pfm_check_task_state(ctx, PFM_CMD_STOPPED, &flags, &resume);
	if (ret == 0)
		ret = __pfm_write_pmds_old(ctx, req, count);

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (copy_to_user(ureq, req, sz))
		ret = -EFAULT;

	if (resume)
		pfm_resume_task(task, resume);

	kfree(fptr);
error:
	fput_light(filp, fput_needed);
	return ret;
}

int __pfm_read_pmds_old(struct pfm_context *ctx, struct pfarg_reg *req_old,
			int count)
{
	struct pfarg_pmd req;
	int i, ret;

	memset(&req, 0, sizeof(req));

	for (i = 0; i < count; i++, req_old++) {
		req.reg_num   = req_old->reg_num;
		req.reg_set   = req_old->reg_set;

		/* skip value not used for reading */
		req.reg_flags = req_old->reg_flags;

		/* skip short/long_reset not used for reading */
		/* skip last_reset_val not used for reading */
		/* skip ovfl_switch_cnt not used for reading */

		ret = __pfm_read_pmds(ctx, (void *)&req, 1);

		req_old->reg_flags &= ~PFM_REG_RETFL_MASK;
		req_old->reg_flags |= req.reg_flags;
		if (ret)
			return ret;

		/* update fields */
		req_old->reg_value = req.reg_value;

		req_old->reg_last_reset_val  = req.reg_last_reset_val;
		req_old->reg_ovfl_switch_cnt = req.reg_ovfl_switch_cnt;
	}
	return 0;
}

static long pfm_read_pmds_old(int fd, void __user *ureq, int count)
{
	struct pfm_context *ctx;
	struct task_struct *task;
	struct file *filp;
	struct pfarg_reg *req = NULL;
	void *fptr, *resume;
	unsigned long flags;
	size_t sz;
	int ret, fput_needed;

	if (count < 1 || count >= PFM_MAX_ARG_COUNT(req))
		return -EINVAL;

	sz = count*sizeof(*req);

	filp = fget_light(fd, &fput_needed);
	if (unlikely(filp == NULL)) {
		PFM_DBG("invalid fd %d", fd);
		return -EBADF;
	}

	ctx = filp->private_data;
	ret = -EBADF;

	if (unlikely(!ctx || filp->f_op != &pfm_file_ops)) {
		PFM_DBG("fd %d not related to perfmon", fd);
		goto error;
	}

	ret = pfm_get_args(ureq, sz, 0, NULL, (void **)&req, &fptr);
	if (ret)
		goto error;

	spin_lock_irqsave(&ctx->lock, flags);

	task = ctx->task;

	ret = pfm_check_task_state(ctx, PFM_CMD_STOPPED, &flags, &resume);
	if (ret == 0)
		ret = __pfm_read_pmds_old(ctx, req, count);

	spin_unlock_irqrestore(&ctx->lock, flags);

	if (resume)
		pfm_resume_task(task, resume);

	if (copy_to_user(ureq, req, sz))
		ret = -EFAULT;

	kfree(fptr);
error:
	fput_light(filp, fput_needed);
	return ret;
}

/*
 * OBSOLETE: use /proc/perfmon_map instead
 */
static long pfm_get_default_pmcs_old(int fd, void __user *ureq, int count)
{
	struct pfarg_reg *req = NULL;
	void *fptr;
	size_t sz;
	int ret, i;
	unsigned int cnum;

	if (count < 1)
		return -EINVAL;

	/*
	 * ensure the pfm_pmu_conf does not disappear while
	 * we use it
	 */
	ret = pfm_pmu_conf_get(1);
	if (ret)
		return ret;

	sz = count*sizeof(*ureq);

	ret = pfm_get_args(ureq, sz, 0, NULL, (void **)&req, &fptr);
	if (ret)
		goto error;


	for (i = 0; i < count; i++, req++) {
		cnum   = req->reg_num;

		if (i >= PFM_MAX_PMCS ||
		    (pfm_pmu_conf->pmc_desc[cnum].type & PFM_REG_I) == 0) {
			req->reg_flags = PFM_REG_RETFL_EINVAL;
			break;
		}
		req->reg_value = pfm_pmu_conf->pmc_desc[cnum].dfl_val;
		req->reg_flags = 0;

		PFM_DBG("pmc[%u]=0x%lx", cnum, req->reg_value);
	}

	if (copy_to_user(ureq, req, sz))
		ret = -EFAULT;

	kfree(fptr);
error:
	pfm_pmu_conf_put();

	return ret;
}

/*
 * allocate a sampling buffer and remaps it into the user address space of
 * the task. This is only in compatibility mode
 *
 * function called ONLY on current task
 */
int pfm_smpl_buf_alloc_compat(struct pfm_context *ctx, size_t rsize,
			      struct file *filp)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = NULL;
	struct pfm_arch_context *ctx_arch;
	size_t size;
	int ret;
	extern struct vm_operations_struct pfm_buf_map_vm_ops;

	ctx_arch = pfm_ctx_arch(ctx);

	/*
	 * allocate buffer + map desc
	 */
	ret = pfm_smpl_buf_alloc(ctx, rsize);
	if (ret)
		return ret;

	size = ctx->smpl_size;


	/* allocate vma */
	vma = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);
	if (!vma) {
		PFM_DBG("Cannot allocate vma");
		goto error_kmem;
	}
	memset(vma, 0, sizeof(*vma));

	/*
	 * partially initialize the vma for the sampling buffer
	 */
	vma->vm_mm	     = mm;
	vma->vm_flags	     = VM_READ | VM_MAYREAD | VM_RESERVED;
	vma->vm_page_prot    = PAGE_READONLY;
	vma->vm_ops	     = &pfm_buf_map_vm_ops;
	vma->vm_file	     = filp;
	vma->vm_private_data = ctx;
	vma->vm_pgoff        = 0;

	/*
	 * simulate effect of mmap()
	 */
	get_file(filp);

	/*
	 * Let's do the difficult operations next.
	 *
	 * now we atomically find some area in the address space and
	 * remap the buffer into it.
	 */
	down_write(&current->mm->mmap_sem);

	/* find some free area in address space, must have mmap sem held */
	vma->vm_start = get_unmapped_area(NULL, 0, size, 0,
					  MAP_PRIVATE|MAP_ANONYMOUS);
	if (vma->vm_start == 0) {
		PFM_DBG("cannot find unmapped area of size %zu", size);
		up_write(&current->mm->mmap_sem);
		goto error;
	}
	vma->vm_end = vma->vm_start + size;

	PFM_DBG("aligned_size=%zu mapped @0x%lx", size, vma->vm_start);
	/*
	 * now insert the vma in the vm list for the process, must be
	 * done with mmap lock held
	 */
	insert_vm_struct(mm, vma);

	mm->total_vm  += size >> PAGE_SHIFT;

	up_write(&current->mm->mmap_sem);

	/*
	 * IMPORTANT: we do not issue the fput()
	 * because we want to increase the ref count
	 * on the descriptor to simulate what mmap()
	 * would do
	 */

	/*
	 * used to propagate vaddr to syscall stub
	 */
	ctx_arch->ctx_smpl_vaddr = (void *)vma->vm_start;

	return 0;
error:
	kmem_cache_free(vm_area_cachep, vma);
error_kmem:
	pfm_smpl_buf_space_release(ctx, ctx->smpl_size);
	vfree(ctx->smpl_addr);
	return -ENOMEM;
}

#define PFM_DEFAULT_SMPL_UUID { \
		0x4d, 0x72, 0xbe, 0xc0, 0x06, 0x64, 0x41, 0x43, 0x82,\
		0xb4, 0xd3, 0xfd, 0x27, 0x24, 0x3c, 0x97}

static pfm_uuid_t old_default_uuid = PFM_DEFAULT_SMPL_UUID;
static pfm_uuid_t null_uuid;

/*
 * function invoked in case, pfm_context_create fails
 * at the last operation, copy_to_user. It needs to
 * undo memory allocations and free the file descriptor
 */
static void pfm_undo_create_context_fd(int fd, struct pfm_context *ctx)
{
	struct files_struct *files = current->files;
	struct file *file;
	int fput_needed;

	file = fget_light(fd, &fput_needed);
	/*
	 * there is no fd_uninstall(), so we do it
	 * here. put_unused_fd() does not remove the
	 * effect of fd_install().
	 */

	spin_lock(&files->file_lock);
	files->fd_array[fd] = NULL;
	spin_unlock(&files->file_lock);

	fput_light(file, fput_needed);

	/*
	 * decrement ref count and kill file
	 */
	put_filp(file);

	put_unused_fd(fd);

	pfm_free_context(ctx);
}

static int pfm_get_smpl_arg_old(pfm_uuid_t uuid, void __user *fmt_uarg,
				size_t usize, void **arg,
				struct pfm_smpl_fmt **fmt)
{
	struct pfm_smpl_fmt *f;
	void *addr = NULL;
	size_t sz;
	int ret;

	if (!memcmp(uuid, null_uuid, sizeof(pfm_uuid_t)))
		return 0;

	if (memcmp(uuid, old_default_uuid, sizeof(pfm_uuid_t))) {
		PFM_DBG("compatibility mode supports only default sampling format");
		return -EINVAL;
	}
	/*
	 * find fmt and increase refcount
	 */
	f = pfm_smpl_fmt_get("default-old");
	if (f == NULL) {
		PFM_DBG("default-old buffer format not found");
		return -EINVAL;
	}

	/*
	 * expected format argument size
	 */
	sz = f->fmt_arg_size;

	/*
	 * check user size matches expected size
	 * usize = -1 is for IA-64 backward compatibility
	 */
	ret = -EINVAL;
	if (sz != usize && usize != -1) {
		PFM_DBG("invalid arg size %zu, format expects %zu",
			usize, sz);
		goto error;
	}

	ret = -ENOMEM;
	addr = kmalloc(sz, GFP_KERNEL);
	if (addr == NULL)
		goto error;

	ret = -EFAULT;
	if (copy_from_user(addr, fmt_uarg, sz))
		goto error;

	*arg = addr;
	*fmt = f;
	return 0;

error:
	kfree(addr);
	pfm_smpl_fmt_put(f);
	return ret;
}

static long pfm_create_context_old(int fd, void __user *ureq, int count)
{
	struct pfm_context *new_ctx;
	struct pfm_arch_context *ctx_arch;
	struct pfm_smpl_fmt *fmt = NULL;
	struct pfarg_context req_old;
	void __user *usmpl_arg;
	void *smpl_arg = NULL;
	struct pfarg_ctx req;
	int ret;

	if (count != 1)
		return -EINVAL;

	if (copy_from_user(&req_old, ureq, sizeof(req_old)))
		return -EFAULT;

	memset(&req, 0, sizeof(req));

	/*
	 * sampling format args are following pfarg_context
	 */
	usmpl_arg = ureq+sizeof(req_old);

	ret = pfm_get_smpl_arg_old(req_old.ctx_smpl_buf_id, usmpl_arg, -1,
				   &smpl_arg, &fmt);
	if (ret)
		return ret;

	req.ctx_flags = req_old.ctx_flags;

	/*
	 * returns file descriptor if >=0, or error code */
	ret = __pfm_create_context(&req, fmt, smpl_arg, PFM_COMPAT, &new_ctx);
	if (ret >= 0) {
		ctx_arch = pfm_ctx_arch(new_ctx);
		req_old.ctx_fd = ret;
		req_old.ctx_smpl_vaddr = ctx_arch->ctx_smpl_vaddr;
	}

	if (copy_to_user(ureq, &req_old, sizeof(req_old))) {
		pfm_undo_create_context_fd(req_old.ctx_fd, new_ctx);
		ret = -EFAULT;
	}

	kfree(smpl_arg);

	return ret;
}

/*
 * obsolete call: use /proc/perfmon
 */
static long pfm_get_features_old(int fd, void __user *arg, int count)
{
	struct pfarg_features req;
	int ret = 0;

	if (count != 1)
		return -EINVAL;

	memset(&req, 0, sizeof(req));

	req.ft_version = PFM_VERSION;

	if (copy_to_user(arg, &req, sizeof(req)))
		ret = -EFAULT;

	return ret;
}

static long pfm_debug_old(int fd, void __user *arg, int count)
{
	int m;

	if (count != 1)
		return -EINVAL;

	if (get_user(m, (int __user *)arg))
		return -EFAULT;


	pfm_controls.debug = m == 0 ? 0 : 1;

	PFM_INFO("debugging %s (timing reset)",
		 pfm_controls.debug ? "on" : "off");

	if (m == 0)
		for_each_online_cpu(m) {
			memset(&per_cpu(pfm_stats, m), 0,
			       sizeof(struct pfm_stats));
		}
	return 0;
}

static long pfm_unload_context_old(int fd, void __user *arg, int count)
{
	if (count)
		return -EINVAL;

	return sys_pfm_unload_context(fd);
}

static long pfm_restart_old(int fd, void __user *arg, int count)
{
	if (count)
		return -EINVAL;

	return sys_pfm_restart(fd);
}

static long pfm_stop_old(int fd, void __user *arg, int count)
{
	if (count)
		return -EINVAL;

	return sys_pfm_stop(fd);
}

static long pfm_start_old(int fd, void __user *arg, int count)
{
	if (count > 1)
		return -EINVAL;

	return sys_pfm_start(fd, arg);
}

static long pfm_load_context_old(int fd, void __user *ureq, int count)
{
	if (count != 1)
		return -EINVAL;

	return sys_pfm_load_context(fd, ureq);
}

/*
 * perfmon command descriptions
 */
struct pfm_cmd_desc {
	long (*cmd_func)(int fd, void __user *arg, int count);
};

/*
 * functions MUST be listed in the increasing order of
 * their index (see permfon.h)
 */
#define PFM_CMD(name)  \
	{ .cmd_func = name,  \
	}
#define PFM_CMD_NONE		\
	{ .cmd_func = NULL   \
	}

static struct pfm_cmd_desc pfm_cmd_tab[] = {
/* 0  */PFM_CMD_NONE,
/* 1  */PFM_CMD(pfm_write_pmcs_old),
/* 2  */PFM_CMD(pfm_write_pmds_old),
/* 3  */PFM_CMD(pfm_read_pmds_old),
/* 4  */PFM_CMD(pfm_stop_old),
/* 5  */PFM_CMD(pfm_start_old),
/* 6  */PFM_CMD_NONE,
/* 7  */PFM_CMD_NONE,
/* 8  */PFM_CMD(pfm_create_context_old),
/* 9  */PFM_CMD_NONE,
/* 10 */PFM_CMD(pfm_restart_old),
/* 11 */PFM_CMD_NONE,
/* 12 */PFM_CMD(pfm_get_features_old),
/* 13 */PFM_CMD(pfm_debug_old),
/* 14 */PFM_CMD_NONE,
/* 15 */PFM_CMD(pfm_get_default_pmcs_old),
/* 16 */PFM_CMD(pfm_load_context_old),
/* 17 */PFM_CMD(pfm_unload_context_old),
/* 18 */PFM_CMD_NONE,
/* 19 */PFM_CMD_NONE,
/* 20 */PFM_CMD_NONE,
/* 21 */PFM_CMD_NONE,
/* 22 */PFM_CMD_NONE,
/* 23 */PFM_CMD_NONE,
/* 24 */PFM_CMD_NONE,
/* 25 */PFM_CMD_NONE,
/* 26 */PFM_CMD_NONE,
/* 27 */PFM_CMD_NONE,
/* 28 */PFM_CMD_NONE,
/* 29 */PFM_CMD_NONE,
/* 30 */PFM_CMD_NONE,
/* 31 */PFM_CMD_NONE,
/* 32 */PFM_CMD(pfm_write_ibrs_old),
/* 33 */PFM_CMD(pfm_write_dbrs_old),
};
#define PFM_CMD_COUNT ARRAY_SIZE(pfm_cmd_tab)

/*
 * system-call entry point (must return long)
 */
asmlinkage long sys_perfmonctl(int fd, int cmd, void __user *arg, int count)
{
	if (perfmon_disabled)
		return -ENOSYS;

	if (unlikely(cmd < 0 || cmd >= PFM_CMD_COUNT
		     || pfm_cmd_tab[cmd].cmd_func == NULL)) {
		PFM_DBG("invalid cmd=%d", cmd);
		return -EINVAL;
	}
	return (long)pfm_cmd_tab[cmd].cmd_func(fd, arg, count);
}

/*
 * Called from pfm_read() for a perfmon v2.0 context.
 *
 * compatibility mode pfm_read() routine. We need a separate
 * routine because the definition of the message has changed.
 * The pfm_msg and pfarg_msg structures are different.
 *
 * return: sizeof(pfm_msg_t) on success, -errno otherwise
 */
ssize_t pfm_arch_compat_read(struct pfm_context *ctx,
			     char __user *buf,
			     int non_block,
			     size_t size)
{
	union pfarg_msg msg_buf;
	pfm_msg_t old_msg_buf;
	pfm_ovfl_msg_t *o_msg;
	struct pfarg_ovfl_msg *n_msg;
	int ret;

	PFM_DBG("msg=%p size=%zu", buf, size);

	/*
	 * cannot extract partial messages.
	 * check even when there is no message
	 *
	 * cannot extract more than one message per call. Bytes
	 * above sizeof(msg) are ignored.
	 */
	if (size < sizeof(old_msg_buf)) {
		PFM_DBG("message is too small size=%zu must be >=%zu)",
			size,
			sizeof(old_msg_buf));
		return -EINVAL;
	}

	ret =  __pfm_read(ctx, &msg_buf, non_block);
	if (ret < 1)
		return ret;

	/*
	 * force return value to old message size
	 */
	ret = sizeof(old_msg_buf);

	o_msg = &old_msg_buf.pfm_ovfl_msg;
	n_msg = &msg_buf.pfm_ovfl_msg;

	switch (msg_buf.type) {
	case PFM_MSG_OVFL:
		o_msg->msg_type   = PFM_MSG_OVFL;
		o_msg->msg_ctx_fd = 0;
		o_msg->msg_active_set = n_msg->msg_active_set;
		o_msg->msg_tstamp = 0;

		o_msg->msg_ovfl_pmds[0] = n_msg->msg_ovfl_pmds[0];
		o_msg->msg_ovfl_pmds[1] = n_msg->msg_ovfl_pmds[1];
		o_msg->msg_ovfl_pmds[2] = n_msg->msg_ovfl_pmds[2];
		o_msg->msg_ovfl_pmds[3] = n_msg->msg_ovfl_pmds[3];
		break;
	case PFM_MSG_END:
		o_msg->msg_type = PFM_MSG_END;
		o_msg->msg_ctx_fd = 0;
		o_msg->msg_tstamp = 0;
		break;
	default:
		PFM_DBG("unknown msg type=%d", msg_buf.type);
	}
	if (copy_to_user(buf, &old_msg_buf, sizeof(old_msg_buf)))
		ret = -EFAULT;
	PFM_DBG_ovfl("ret=%d", ret);
	return ret;
}

/*
 * legacy /proc/perfmon simplified interface (we only maintain the
 * global information (no more per-cpu stats, use
 * /sys/devices/system/cpu/cpuXX/perfmon
 */
static struct proc_dir_entry 	*perfmon_proc;

static void *pfm_proc_start(struct seq_file *m, loff_t *pos)
{
	if (*pos == 0)
		return (void *)1;

	return NULL;
}

static void *pfm_proc_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return pfm_proc_start(m, pos);
}

static void pfm_proc_stop(struct seq_file *m, void *v)
{
}

/*
 * this is a simplified version of the legacy /proc/perfmon.
 * We have retained ONLY the key information that tools are actually
 * using
 */
static void pfm_proc_show_header(struct seq_file *m)
{
	char buf[128];

	pfm_sysfs_res_show(buf, sizeof(buf), 3);

	seq_printf(m, "perfmon version            : %u.%u\n",
		PFM_VERSION_MAJ, PFM_VERSION_MIN);

	seq_printf(m, "model                      : %s", buf);
}

static int pfm_proc_show(struct seq_file *m, void *v)
{
	pfm_proc_show_header(m);
	return 0;
}

struct seq_operations pfm_proc_seq_ops = {
	.start = pfm_proc_start,
	.next =	pfm_proc_next,
	.stop =	pfm_proc_stop,
	.show =	pfm_proc_show
};

static int pfm_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &pfm_proc_seq_ops);
}


static struct file_operations pfm_proc_fops = {
	.open = pfm_proc_open,
	.read = seq_read,
	.llseek	= seq_lseek,
	.release = seq_release,
};

/*
 * called from pfm_arch_init(), global initialization, called once
 */
int __init pfm_ia64_compat_init(void)
{
	/*
	 * create /proc/perfmon
	 */
	perfmon_proc = create_proc_entry("perfmon", S_IRUGO, NULL);
	if (perfmon_proc == NULL) {
		PFM_ERR("cannot create /proc entry, perfmon disabled");
		return -1;
	}
	perfmon_proc->proc_fops = &pfm_proc_fops;
	return 0;
}

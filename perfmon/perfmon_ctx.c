/*
 * perfmon_ctx.c: perfmon2 context functions
 *
 * This file implements the perfmon2 interface which
 * provides access to the hardware performance counters
 * of the host processor.
 *
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
#include <linux/perfmon_kern.h>
#include "perfmon_priv.h"

/*
 * context memory pool pointer
 */
static struct kmem_cache *pfm_ctx_cachep;

/**
 * pfm_free_context - de-allocate context and associated resources
 * @ctx: context to free
 */
void pfm_free_context(struct pfm_context *ctx)
{
	pfm_arch_context_free(ctx);

	pfm_free_sets(ctx);

	pfm_smpl_buf_free(ctx);

	PFM_DBG("free ctx @0x%p", ctx);
	kmem_cache_free(pfm_ctx_cachep, ctx);
	/*
	 * decrease refcount on:
	 * 	- PMU description table
	 * 	- sampling format
	 */
	pfm_pmu_conf_put();
	pfm_pmu_release();
}

/**
 * pfm_ctx_flags_sane - check if context flags passed by user are okay
 * @ctx_flags: flags passed user on pfm_create_context
 *
 * return:
 * 	 0 if successful
 * 	<0 and error code otherwise
 */
static inline int pfm_ctx_flags_sane(u32 ctx_flags)
{
	if (ctx_flags & PFM_FL_SYSTEM_WIDE) {
		if (ctx_flags & PFM_FL_NOTIFY_BLOCK) {
			PFM_DBG("cannot use blocking mode in syswide mode");
			return -EINVAL;
		}
	}
	return 0;
}

/**
 * pfm_ctx_permissions - check authorization to create new context
 * @ctx_flags: context flags passed by user
 *
 * check for permissions to create a context.
 *
 * A sysadmin may decide to restrict creation of per-thread
 * and/or system-wide context to a group of users using the
 * group id via /sys/kernel/perfmon/task_group  and
 * /sys/kernel/perfmon/sys_group.
 *
 * Once we identify a user level package which can be used
 * to grant/revoke Linux capabilites at login via PAM, we will
 * be able to use capabilities. We would also need to increase
 * the size of cap_t to support more than 32 capabilities (it
 * is currently defined as u32 and 32 capabilities are alrady
 * defined).
 */
static inline int pfm_ctx_permissions(u32 ctx_flags)
{
	if ((ctx_flags & PFM_FL_SYSTEM_WIDE)
	    && pfm_controls.sys_group != PFM_GROUP_PERM_ANY
	    && !in_group_p(pfm_controls.sys_group)) {
		PFM_DBG("user group not allowed to create a syswide ctx");
		return -EPERM;
	} else if (pfm_controls.task_group != PFM_GROUP_PERM_ANY
		   && !in_group_p(pfm_controls.task_group)) {
		PFM_DBG("user group not allowed to create a task context");
		return -EPERM;
	}
	return 0;
}

/**
 * __pfm_create_context - allocate and initialize a perfmon context
 * @req : pfarg_ctx from user
 * @fmt : pointer sampling format, NULL if not used
 * @fmt_arg: pointer to argument to sampling format, NULL if not used
 * @mode: PFM_NORMAL or PFM_COMPAT(IA-64 v2.0 compatibility)
 * @ctx : address of new context upon succesful return, undefined otherwise
 *
 * function used to allocate a new context. A context is allocated along
 * with the default event set. If a sampling format is used, the buffer
 * may be allocated and initialized.
 *
 * The file descriptor identifying the context is allocated and returned
 * to caller.
 *
 * This function operates with no locks and interrupts are enabled.
 * return:
 * 	>=0: the file descriptor to identify the context
 * 	<0 : the error code
 */
int __pfm_create_context(struct pfarg_ctx *req,
			 struct pfm_smpl_fmt *fmt,
			 void *fmt_arg,
			 int mode,
			 struct pfm_context **new_ctx)
{
	struct pfm_context *ctx;
	struct file *filp = NULL;
	u32 ctx_flags;
	int fd = 0, ret;

	ctx_flags = req->ctx_flags;

	/* Increase refcount on PMU description */
	ret = pfm_pmu_conf_get(1);
	if (ret < 0)
		goto error_conf;

	ret = pfm_ctx_flags_sane(ctx_flags);
	if (ret < 0)
		goto error_alloc;

	ret = pfm_ctx_permissions(ctx_flags);
	if (ret < 0)
		goto error_alloc;

	/*
	 * we can use GFP_KERNEL and potentially sleep because we do
	 * not hold any lock at this point.
	 */
	might_sleep();
	ret = -ENOMEM;
	ctx = kmem_cache_zalloc(pfm_ctx_cachep, GFP_KERNEL);
	if (!ctx)
		goto error_alloc;

	PFM_DBG("alloc ctx @0x%p", ctx);

	INIT_LIST_HEAD(&ctx->set_list);
	spin_lock_init(&ctx->lock);
	init_completion(&ctx->restart_complete);
	init_waitqueue_head(&ctx->msgq_wait);

	/*
	 * context is unloaded
	 */
	ctx->state = PFM_CTX_UNLOADED;

	/*
	 * initialization of context's flags
	 * must be done before pfm_find_set()
	 */
	ctx->flags.block = (ctx_flags & PFM_FL_NOTIFY_BLOCK) ? 1 : 0;
	ctx->flags.system = (ctx_flags & PFM_FL_SYSTEM_WIDE) ? 1: 0;
	ctx->flags.no_msg = (ctx_flags & PFM_FL_OVFL_NO_MSG) ? 1: 0;
	ctx->flags.ia64_v20_compat = mode == PFM_COMPAT ? 1 : 0;

	ret = pfm_pmu_acquire(ctx);
	if (ret)
		goto error_file;
	/*
	 * check if PMU is usable
	 */
	if (!(ctx->regs.num_pmcs && ctx->regs.num_pmcs)) {
		PFM_DBG("no usable PMU registers");
		ret = -EBUSY;
		goto error_file;
	}

	/*
	 * link to format, must be done first for correct
	 * error handling in pfm_context_free()
	 */
	ctx->smpl_fmt = fmt;

	ret = -ENFILE;
	fd = pfm_alloc_fd(&filp);
	if (fd < 0)
		goto error_file;

	/*
	 * initialize arch-specific section
	 * must be done before fmt_init()
	 */
	ret = pfm_arch_context_create(ctx, ctx_flags);
	if (ret)
		goto error_set;

	ret = -ENOMEM;

	/*
	 * add initial set
	 */
	if (pfm_create_initial_set(ctx))
		goto error_set;

	/*
	 * does the user want to sample?
	 * must be done after pfm_pmu_acquire() because
	 * needs ctx->regs
	 */
	if (fmt) {
		ret = pfm_setup_smpl_fmt(ctx, ctx_flags, fmt_arg, filp);
		if (ret)
			goto error_set;
	}

	filp->private_data = ctx;

	ctx->last_act = PFM_INVALID_ACTIVATION;
	ctx->last_cpu = -1;

	/*
	 * initialize notification message queue
	 */
	ctx->msgq_head = ctx->msgq_tail = 0;

	PFM_DBG("flags=0x%x system=%d notify_block=%d no_msg=%d"
		" use_fmt=%d ctx_fd=%d mode=%d",
		ctx_flags,
		ctx->flags.system,
		ctx->flags.block,
		ctx->flags.no_msg,
		!!fmt,
		fd, mode);

	if (new_ctx)
		*new_ctx = ctx;

	/*
	 * we defer the fd_install until we are certain the call succeeded
	 * to ensure we do not have to undo its effect. Neither put_filp()
	 * nor put_unused_fd() undoes the effect of fd_install().
	 */
	fd_install(fd, filp);

	return fd;

error_set:
	put_filp(filp);
	put_unused_fd(fd);
error_file:
	/*
	 * calls the right *_put() functions
	 * calls pfm_release_pmu()
	 */
	pfm_free_context(ctx);
	return ret;
error_alloc:
	pfm_pmu_conf_put();
error_conf:
	pfm_smpl_fmt_put(fmt);
	return ret;
}

/**
 * pfm_init_ctx -- initialize context SLAB
 *
 * called from pfm_init
 */
int __init pfm_init_ctx(void)
{
	pfm_ctx_cachep = kmem_cache_create("pfm_context",
				   sizeof(struct pfm_context)+PFM_ARCH_CTX_SIZE,
				   SLAB_HWCACHE_ALIGN, 0, NULL);
	if (!pfm_ctx_cachep) {
		PFM_ERR("cannot initialize context slab");
		return -ENOMEM;
	}
	return 0;
}

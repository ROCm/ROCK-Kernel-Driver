/*
 * Copyright (c) 1999-2006 Hewlett-Packard Development Company, L.P.
 *               Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * This file implements the new default sampling buffer format
 * for the perfmon2 subsystem.
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
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <linux/perfmon_kern.h>
#include <linux/perfmon_dfl_smpl.h>

MODULE_AUTHOR("Stephane Eranian <eranian@hpl.hp.com>");
MODULE_DESCRIPTION("new perfmon default sampling format");
MODULE_LICENSE("GPL");

static int pfm_dfl_fmt_validate(u32 ctx_flags, u16 npmds, void *data)
{
	struct pfm_dfl_smpl_arg *arg = data;
	u64 min_buf_size;

	if (data == NULL) {
		PFM_DBG("no argument passed");
		return -EINVAL;
	}

	/*
	 * sanity check in case size_t is smaller then u64
	 */
#if BITS_PER_LONG == 4
#define MAX_SIZE_T	(1ULL<<(sizeof(size_t)<<3))
	if (sizeof(size_t) < sizeof(arg->buf_size)) {
		if (arg->buf_size >= MAX_SIZE_T)
			return -ETOOBIG;
	}
#endif

	/*
	 * compute min buf size. npmds is the maximum number
	 * of implemented PMD registers.
	 */
	min_buf_size = sizeof(struct pfm_dfl_smpl_hdr)
		+ (sizeof(struct pfm_dfl_smpl_entry) + (npmds*sizeof(u64)));

	PFM_DBG("validate ctx_flags=0x%x flags=0x%x npmds=%u "
		"min_buf_size=%llu buf_size=%llu\n",
		ctx_flags,
		arg->buf_flags,
		npmds,
		(unsigned long long)min_buf_size,
		(unsigned long long)arg->buf_size);

	/*
	 * must hold at least the buffer header + one minimally sized entry
	 */
	if (arg->buf_size < min_buf_size)
		return -EINVAL;

	return 0;
}

static int pfm_dfl_fmt_get_size(u32 flags, void *data, size_t *size)
{
	struct pfm_dfl_smpl_arg *arg = data;

	/*
	 * size has been validated in default_validate
	 * we can never loose bits from buf_size.
	 */
	*size = (size_t)arg->buf_size;

	return 0;
}

static int pfm_dfl_fmt_init(struct pfm_context *ctx, void *buf, u32 ctx_flags,
			    u16 npmds, void *data)
{
	struct pfm_dfl_smpl_hdr *hdr;
	struct pfm_dfl_smpl_arg *arg = data;

	hdr = buf;

	hdr->hdr_version = PFM_DFL_SMPL_VERSION;
	hdr->hdr_buf_size = arg->buf_size;
	hdr->hdr_buf_flags = arg->buf_flags;
	hdr->hdr_cur_offs = sizeof(*hdr);
	hdr->hdr_overflows = 0;
	hdr->hdr_count = 0;
	hdr->hdr_min_buf_space = sizeof(struct pfm_dfl_smpl_entry) + (npmds*sizeof(u64));
	/*
	 * due to cache aliasing, it may be necessary to flush the cache
	 * on certain architectures (e.g., MIPS)
	 */
	pfm_cacheflush(hdr, sizeof(*hdr));

	PFM_DBG("buffer=%p buf_size=%llu hdr_size=%zu hdr_version=%u.%u "
		  "min_space=%llu npmds=%u",
		  buf,
		  (unsigned long long)hdr->hdr_buf_size,
		  sizeof(*hdr),
		  PFM_VERSION_MAJOR(hdr->hdr_version),
		  PFM_VERSION_MINOR(hdr->hdr_version),
		  (unsigned long long)hdr->hdr_min_buf_space,
		  npmds);

	return 0;
}

/*
 * called from pfm_overflow_handler() to record a new sample
 *
 * context is locked, interrupts are disabled (no preemption)
 */
static int pfm_dfl_fmt_handler(struct pfm_context *ctx,
			       unsigned long ip, u64 tstamp, void *data)
{
	struct pfm_dfl_smpl_hdr *hdr;
	struct pfm_dfl_smpl_entry *ent;
	struct pfm_ovfl_arg *arg;
	void *cur, *last;
	u64 *e;
	size_t entry_size, min_size;
	u16 npmds, i;
	u16 ovfl_pmd;
	void *buf;

	hdr = ctx->smpl_addr;
	arg = &ctx->ovfl_arg;

        buf = hdr;
	cur = buf+hdr->hdr_cur_offs;
	last = buf+hdr->hdr_buf_size;
	ovfl_pmd = arg->ovfl_pmd;
	min_size = hdr->hdr_min_buf_space;

	/*
	 * precheck for sanity
	 */
	if ((last - cur) < min_size)
		goto full;

	npmds = arg->num_smpl_pmds;

	ent = (struct pfm_dfl_smpl_entry *)cur;

	entry_size = sizeof(*ent) + (npmds << 3);

	/* position for first pmd */
	e = (u64 *)(ent+1);

	hdr->hdr_count++;

	PFM_DBG_ovfl("count=%llu cur=%p last=%p free_bytes=%zu ovfl_pmd=%d "
		     "npmds=%u",
		     (unsigned long long)hdr->hdr_count,
		     cur, last,
		     (last-cur),
		     ovfl_pmd,
		     npmds);

	/*
	 * current = task running at the time of the overflow.
	 *
	 * per-task mode:
	 * 	- this is usually the task being monitored.
	 * 	  Under certain conditions, it might be a different task
	 *
	 * system-wide:
	 * 	- this is not necessarily the task controlling the session
	 */
	ent->pid = current->pid;
	ent->ovfl_pmd = ovfl_pmd;
	ent->last_reset_val = arg->pmd_last_reset;

	/*
	 * where did the fault happen (includes slot number)
	 */
	ent->ip = ip;

	ent->tstamp = tstamp;
	ent->cpu = smp_processor_id();
	ent->set = arg->active_set;
	ent->tgid = current->tgid;

	/*
	 * selectively store PMDs in increasing index number
	 */
	if (npmds) {
		u64 *val = arg->smpl_pmds_values;
		for (i = 0; i < npmds; i++)
			*e++ = *val++;
	}

	/*
	 * update position for next entry
	 */
	hdr->hdr_cur_offs += entry_size;
	cur += entry_size;

	pfm_cacheflush(hdr, sizeof(*hdr));
	pfm_cacheflush(ent, entry_size);

	/*
	 * post check to avoid losing the last sample
	 */
	if ((last - cur) < min_size)
		goto full;

	/* reset before returning from interrupt handler */
	arg->ovfl_ctrl = PFM_OVFL_CTRL_RESET;

	return 0;
full:
	PFM_DBG_ovfl("sampling buffer full free=%zu, count=%llu",
		     last-cur,
		     (unsigned long long)hdr->hdr_count);

	/*
	 * increment number of buffer overflows.
	 * important to detect duplicate set of samples.
	 */
	hdr->hdr_overflows++;

	/*
	 * request notification and masking of monitoring.
	 * Notification is still subject to the overflowed
	 * register having the FL_NOTIFY flag set.
	 */
	arg->ovfl_ctrl = PFM_OVFL_CTRL_NOTIFY | PFM_OVFL_CTRL_MASK;

	return -ENOBUFS; /* we are full, sorry */
}

static int pfm_dfl_fmt_restart(struct pfm_context *ctx, u32 *ovfl_ctrl)
{
	struct pfm_dfl_smpl_hdr *hdr;

	hdr = ctx->smpl_addr;

	hdr->hdr_count = 0;
	hdr->hdr_cur_offs = sizeof(*hdr);

	pfm_cacheflush(hdr, sizeof(*hdr));

	*ovfl_ctrl = PFM_OVFL_CTRL_RESET;

	return 0;
}

static int pfm_dfl_fmt_exit(void *buf)
{
	return 0;
}

static struct pfm_smpl_fmt dfl_fmt = {
	.fmt_name = "default",
	.fmt_version = 0x10000,
	.fmt_arg_size = sizeof(struct pfm_dfl_smpl_arg),
	.fmt_validate = pfm_dfl_fmt_validate,
	.fmt_getsize = pfm_dfl_fmt_get_size,
	.fmt_init = pfm_dfl_fmt_init,
	.fmt_handler = pfm_dfl_fmt_handler,
	.fmt_restart = pfm_dfl_fmt_restart,
	.fmt_exit = pfm_dfl_fmt_exit,
	.fmt_flags = PFM_FMT_BUILTIN_FLAG,
	.owner = THIS_MODULE
};

static int pfm_dfl_fmt_init_module(void)
{
	return pfm_fmt_register(&dfl_fmt);
}

static void pfm_dfl_fmt_cleanup_module(void)
{
	pfm_fmt_unregister(&dfl_fmt);
}

module_init(pfm_dfl_fmt_init_module);
module_exit(pfm_dfl_fmt_cleanup_module);

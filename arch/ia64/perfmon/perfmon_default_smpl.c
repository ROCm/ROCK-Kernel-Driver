/*
 * Copyright (c) 2002-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * This file implements the old default sampling buffer format
 * for the Linux/ia64 perfmon-2 subsystem. This is for backward
 * compatibility only. use the new default format in perfmon/
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
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/sysctl.h>

#ifdef MODULE
#define FMT_FLAGS	0
#else
#define FMT_FLAGS	PFM_FMTFL_IS_BUILTIN
#endif

#include <linux/perfmon_kern.h>
#include <asm/perfmon_default_smpl.h>

MODULE_AUTHOR("Stephane Eranian <eranian@hpl.hp.com>");
MODULE_DESCRIPTION("perfmon old default sampling format");
MODULE_LICENSE("GPL");

static int pfm_default_fmt_validate(u32 flags, u16 npmds, void *data)
{
	struct pfm_default_smpl_arg *arg = data;
	size_t min_buf_size;

	if (data == NULL) {
		PFM_DBG("no argument passed");
		return -EINVAL;
	}

	/*
	 * compute min buf size. All PMD are manipulated as 64bit entities
	 */
	min_buf_size = sizeof(struct pfm_default_smpl_hdr)
	     + (sizeof(struct pfm_default_smpl_entry) + (npmds*sizeof(u64)));

	PFM_DBG("validate flags=0x%x npmds=%u min_buf_size=%lu "
		  "buf_size=%lu CPU%d", flags, npmds, min_buf_size,
		  arg->buf_size, smp_processor_id());

	/*
	 * must hold at least the buffer header + one minimally sized entry
	 */
	if (arg->buf_size < min_buf_size)
		return -EINVAL;

	return 0;
}

static int pfm_default_fmt_get_size(unsigned int flags, void *data,
				    size_t *size)
{
	struct pfm_default_smpl_arg *arg = data;

	/*
	 * size has been validated in default_validate
	 */
	*size = arg->buf_size;

	return 0;
}

static int pfm_default_fmt_init(struct pfm_context *ctx, void *buf,
				u32 flags, u16 npmds, void *data)
{
	struct pfm_default_smpl_hdr *hdr;
	struct pfm_default_smpl_arg *arg = data;

	hdr = buf;

	hdr->hdr_version      = PFM_DEFAULT_SMPL_VERSION;
	hdr->hdr_buf_size     = arg->buf_size;
	hdr->hdr_cur_offs     = sizeof(*hdr);
	hdr->hdr_overflows    = 0;
	hdr->hdr_count        = 0;

	PFM_DBG("buffer=%p buf_size=%lu hdr_size=%lu "
		  "hdr_version=%u cur_offs=%lu",
		  buf,
		  hdr->hdr_buf_size,
		  sizeof(*hdr),
		  hdr->hdr_version,
		  hdr->hdr_cur_offs);

	return 0;
}

static int pfm_default_fmt_handler(struct pfm_context *ctx,
				   unsigned long ip, u64 tstamp, void *data)
{
	struct pfm_default_smpl_hdr *hdr;
	struct pfm_default_smpl_entry *ent;
	void *cur, *last, *buf;
	u64 *e;
	size_t entry_size;
	u16 npmds, i, ovfl_pmd;
	struct pfm_ovfl_arg *arg;

	hdr = ctx->smpl_addr;
	arg = &ctx->ovfl_arg;

	buf = hdr;
	cur = buf+hdr->hdr_cur_offs;
	last = buf+hdr->hdr_buf_size;
	ovfl_pmd = arg->ovfl_pmd;

	/*
	 * precheck for sanity
	 */
	if ((last - cur) < PFM_DEFAULT_MAX_ENTRY_SIZE)
		goto full;

	npmds = arg->num_smpl_pmds;

	ent = cur;

	prefetch(arg->smpl_pmds_values);

	entry_size = sizeof(*ent) + (npmds << 3);

	/* position for first pmd */
	e = (unsigned long *)(ent+1);

	hdr->hdr_count++;

	PFM_DBG_ovfl("count=%lu cur=%p last=%p free_bytes=%lu "
		       "ovfl_pmd=%d npmds=%u",
		       hdr->hdr_count,
		       cur, last,
		       last-cur,
		       ovfl_pmd,
		       npmds);

	/*
	 * current = task running at the time of the overflow.
	 *
	 * per-task mode:
	 * 	- this is ususally the task being monitored.
	 * 	  Under certain conditions, it might be a different task
	 *
	 * system-wide:
	 * 	- this is not necessarily the task controlling the session
	 */
	ent->pid            = current->pid;
	ent->ovfl_pmd  	    = ovfl_pmd;
	ent->last_reset_val = arg->pmd_last_reset;

	/*
	 * where did the fault happen (includes slot number)
	 */
	ent->ip = ip;

	ent->tstamp    = tstamp;
	ent->cpu       = smp_processor_id();
	ent->set       = arg->active_set;
	ent->tgid      = current->tgid;

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
	cur               += entry_size;

	/*
	 * post check to avoid losing the last sample
	 */
	if ((last - cur) < PFM_DEFAULT_MAX_ENTRY_SIZE)
		goto full;

	/*
	 * reset before returning from interrupt handler
	 */
	arg->ovfl_ctrl = PFM_OVFL_CTRL_RESET;
	return 0;
full:
	PFM_DBG_ovfl("smpl buffer full free=%lu, count=%lu",
		       last-cur, hdr->hdr_count);

	/*
	 * increment number of buffer overflow.
	 * important to detect duplicate set of samples.
	 */
	hdr->hdr_overflows++;

	/*
	 * request notification and masking of monitoring.
	 * Notification is still subject to the overflowed
	 */
	arg->ovfl_ctrl = PFM_OVFL_CTRL_NOTIFY | PFM_OVFL_CTRL_MASK;

	return -ENOBUFS; /* we are full, sorry */
}

static int pfm_default_fmt_restart(int is_active, u32 *ovfl_ctrl, void *buf)
{
	struct pfm_default_smpl_hdr *hdr;

	hdr = buf;

	hdr->hdr_count    = 0;
	hdr->hdr_cur_offs = sizeof(*hdr);

	*ovfl_ctrl = PFM_OVFL_CTRL_RESET;

	return 0;
}

static int pfm_default_fmt_exit(void *buf)
{
	return 0;
}

static struct pfm_smpl_fmt default_fmt = {
	.fmt_name = "default-old",
	.fmt_version = 0x10000,
	.fmt_arg_size = sizeof(struct pfm_default_smpl_arg),
	.fmt_validate = pfm_default_fmt_validate,
	.fmt_getsize = pfm_default_fmt_get_size,
	.fmt_init = pfm_default_fmt_init,
	.fmt_handler = pfm_default_fmt_handler,
	.fmt_restart = pfm_default_fmt_restart,
	.fmt_exit = pfm_default_fmt_exit,
	.fmt_flags = FMT_FLAGS,
	.owner = THIS_MODULE
};

static int pfm_default_fmt_init_module(void)
{
	int ret;

	return pfm_fmt_register(&default_fmt);
	return ret;
}

static void pfm_default_fmt_cleanup_module(void)
{
	pfm_fmt_unregister(&default_fmt);
}

module_init(pfm_default_fmt_init_module);
module_exit(pfm_default_fmt_cleanup_module);

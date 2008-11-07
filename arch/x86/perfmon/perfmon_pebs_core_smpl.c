/*
 * Copyright (c) 2005-2007 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * This file implements the Precise Event Based Sampling (PEBS)
 * sampling format for Intel Core and Atom  processors.
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

#include <asm/msr.h>
#include <asm/perfmon_pebs_core_smpl.h>

MODULE_AUTHOR("Stephane Eranian <eranian@hpl.hp.com>");
MODULE_DESCRIPTION("Intel Core Precise Event-Based Sampling (PEBS)");
MODULE_LICENSE("GPL");

#define ALIGN_PEBS(a, order) \
		((a)+(1UL<<(order))-1) & ~((1UL<<(order))-1)

#define PEBS_PADDING_ORDER 8 /* log2(256) padding for PEBS alignment constraint */

static int pfm_pebs_core_fmt_validate(u32 flags, u16 npmds, void *data)
{
	struct pfm_pebs_core_smpl_arg *arg = data;
	size_t min_buf_size;

	/*
	 * need to define at least the size of the buffer
	 */
	if (data == NULL) {
		PFM_DBG("no argument passed");
		return -EINVAL;
	}

	/*
	 * compute min buf size. npmds is the maximum number
	 * of implemented PMD registers.
	 */
	min_buf_size = sizeof(struct pfm_pebs_core_smpl_hdr)
		     + sizeof(struct pfm_pebs_core_smpl_entry)
		     + (1UL<<PEBS_PADDING_ORDER); /* padding for alignment */

	PFM_DBG("validate flags=0x%x min_buf_size=%zu buf_size=%zu",
		  flags,
		  min_buf_size,
		  arg->buf_size);

	/*
	 * must hold at least the buffer header + one minimally sized entry
	 */
	if (arg->buf_size < min_buf_size)
		return -EINVAL;

	return 0;
}

static int pfm_pebs_core_fmt_get_size(unsigned int flags, void *data, size_t *size)
{
	struct pfm_pebs_core_smpl_arg *arg = data;

	/*
	 * size has been validated in pfm_pebs_core_fmt_validate()
	 */
	*size = arg->buf_size + (1UL<<PEBS_PADDING_ORDER);

	return 0;
}

static int pfm_pebs_core_fmt_init(struct pfm_context *ctx, void *buf,
			     u32 flags, u16 npmds, void *data)
{
	struct pfm_arch_context *ctx_arch;
	struct pfm_pebs_core_smpl_hdr *hdr;
	struct pfm_pebs_core_smpl_arg *arg = data;
	u64 pebs_start, pebs_end;
	struct pfm_ds_area_core *ds;

	ctx_arch = pfm_ctx_arch(ctx);

	hdr = buf;
	ds = &hdr->ds;

	/*
	 * align PEBS buffer base
	 */
	pebs_start = ALIGN_PEBS((unsigned long)(hdr+1), PEBS_PADDING_ORDER);
	pebs_end = pebs_start + arg->buf_size + 1;

	hdr->version = PFM_PEBS_CORE_SMPL_VERSION;
	hdr->buf_size = arg->buf_size;
	hdr->overflows = 0;

	/*
	 * express PEBS buffer base as offset from the end of the header
	 */
	hdr->start_offs = pebs_start - (unsigned long)(hdr+1);

	/*
	 * PEBS buffer boundaries
	 */
	ds->pebs_buf_base = pebs_start;
	ds->pebs_abs_max = pebs_end;

	/*
	 * PEBS starting position
	 */
	ds->pebs_index = pebs_start;

	/*
	 * PEBS interrupt threshold
	 */
	ds->pebs_intr_thres = pebs_start
			    + arg->intr_thres
			    * sizeof(struct pfm_pebs_core_smpl_entry);

	/*
	 * save counter reset value for PEBS counter
	 */
	ds->pebs_cnt_reset = arg->cnt_reset;

	/*
	 * keep track of DS AREA
	 */
	ctx_arch->ds_area = ds;
	ctx_arch->flags.use_ds = 1;
	ctx_arch->flags.use_pebs = 1;

	PFM_DBG("buffer=%p buf_size=%llu offs=%llu pebs_start=0x%llx "
		  "pebs_end=0x%llx ds=%p pebs_thres=0x%llx cnt_reset=0x%llx",
		  buf,
		  (unsigned long long)hdr->buf_size,
		  (unsigned long long)hdr->start_offs,
		  (unsigned long long)pebs_start,
		  (unsigned long long)pebs_end,
		  ds,
		  (unsigned long long)ds->pebs_intr_thres,
		  (unsigned long long)ds->pebs_cnt_reset);

	return 0;
}

static int pfm_pebs_core_fmt_handler(struct pfm_context *ctx,
			       unsigned long ip, u64 tstamp, void *data)
{
	struct pfm_pebs_core_smpl_hdr *hdr;
	struct pfm_ovfl_arg *arg;

	hdr = ctx->smpl_addr;
	arg = &ctx->ovfl_arg;

	PFM_DBG_ovfl("buffer full");
	/*
	 * increment number of buffer overflows.
	 * important to detect duplicate set of samples.
	 */
	hdr->overflows++;

	/*
	 * request notification and masking of monitoring.
	 * Notification is still subject to the overflowed
	 * register having the FL_NOTIFY flag set.
	 */
	arg->ovfl_ctrl = PFM_OVFL_CTRL_NOTIFY | PFM_OVFL_CTRL_MASK;

	return -ENOBUFS; /* we are full, sorry */
}

static int pfm_pebs_core_fmt_restart(int is_active, u32 *ovfl_ctrl,
				void *buf)
{
	struct pfm_pebs_core_smpl_hdr *hdr = buf;

	/*
	 * reset index to base of buffer
	 */
	hdr->ds.pebs_index = hdr->ds.pebs_buf_base;

	*ovfl_ctrl = PFM_OVFL_CTRL_RESET;

	return 0;
}

static int pfm_pebs_core_fmt_exit(void *buf)
{
	return 0;
}

static struct pfm_smpl_fmt pebs_core_fmt = {
	.fmt_name = PFM_PEBS_CORE_SMPL_NAME,
	.fmt_version = 0x1,
	.fmt_arg_size = sizeof(struct pfm_pebs_core_smpl_arg),
	.fmt_validate = pfm_pebs_core_fmt_validate,
	.fmt_getsize = pfm_pebs_core_fmt_get_size,
	.fmt_init = pfm_pebs_core_fmt_init,
	.fmt_handler = pfm_pebs_core_fmt_handler,
	.fmt_restart = pfm_pebs_core_fmt_restart,
	.fmt_exit = pfm_pebs_core_fmt_exit,
	.fmt_flags = PFM_FMT_BUILTIN_FLAG,
	.owner = THIS_MODULE,
};

static int __init pfm_pebs_core_fmt_init_module(void)
{
	if (!cpu_has_pebs) {
		PFM_INFO("processor does not have PEBS support");
		return -1;
	}
	/*
	 * cpu_has_pebs is not enough to identify Intel Core PEBS
	 * which is different fro Pentium 4 PEBS. Therefore we do
	 * a more detailed check here
	 */
	if (current_cpu_data.x86 != 6) {
		PFM_INFO("not a supported Intel processor");
		return -1;
	}

	switch (current_cpu_data.x86_model) {
	case 15: /* Merom */
	case 23: /* Penryn */
	case 28: /* Atom (Silverthorne) */
	case 29: /* Dunnington */
		break;
	default:
		PFM_INFO("not a supported Intel processor");
		return -1;
	}
	return pfm_fmt_register(&pebs_core_fmt);
}

static void __exit pfm_pebs_core_fmt_cleanup_module(void)
{
	pfm_fmt_unregister(&pebs_core_fmt);
}

module_init(pfm_pebs_core_fmt_init_module);
module_exit(pfm_pebs_core_fmt_cleanup_module);

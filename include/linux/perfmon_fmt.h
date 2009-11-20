/*
 * Copyright (c) 2001-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * Interface for custom sampling buffer format modules
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
#ifndef __PERFMON_FMT_H__
#define __PERFMON_FMT_H__ 1

#include <linux/kobject.h>

typedef int (*fmt_validate_t)(u32 flags, u16 npmds, void *arg);
typedef	int (*fmt_getsize_t)(u32 flags, void *arg, size_t *size);
typedef int (*fmt_init_t)(struct pfm_context *ctx, void *buf, u32 flags,
			  u16 nmpds, void *arg);
typedef int (*fmt_restart_t)(struct pfm_context *ctx, u32 *ovfl_ctrl);
typedef int (*fmt_exit_t)(void *buf);
typedef int (*fmt_handler_t)(struct pfm_context *ctx,
			     unsigned long ip, u64 stamp, void *data);
typedef int (*fmt_load_t)(struct pfm_context *ctx);
typedef int (*fmt_unload_t)(struct pfm_context *ctx);

typedef int (*fmt_stop_t)(struct pfm_context *ctx);
typedef int (*fmt_start_t)(struct pfm_context *ctx);

struct pfm_smpl_fmt {
	char		*fmt_name;	/* name of the format (required) */
	size_t		fmt_arg_size;	/* size of fmt args for ctx create */
	u32		fmt_flags;	/* format specific flags */
	u32		fmt_version;	/* format version number */

	fmt_validate_t	fmt_validate;	/* validate context flags */
	fmt_getsize_t	fmt_getsize;	/* get size for sampling buffer */
	fmt_init_t	fmt_init;	/* initialize buffer area */
	fmt_handler_t	fmt_handler;	/* overflow handler (required) */
	fmt_restart_t	fmt_restart;	/* restart after notification  */
	fmt_exit_t	fmt_exit;	/* context termination */
	fmt_load_t	fmt_load;	/* load context */
	fmt_unload_t	fmt_unload;	/* unload context */
	fmt_start_t	fmt_start;	/* start monitoring */
	fmt_stop_t	fmt_stop;	/* stop monitoring */

	struct list_head fmt_list;	/* internal use only */

	struct kobject	kobj;		/* sysfs internal use only */
	struct module	*owner;		/* pointer to module owner */
	u32		fmt_qdepth;	/* Max notify queue depth (required) */
};
#define to_smpl_fmt(n) container_of(n, struct pfm_smpl_fmt, kobj)

#define PFM_FMTFL_IS_BUILTIN	0x1	/* fmt is compiled in */
/*
 * we need to know whether the format is builtin or compiled
 * as a module
 */
#ifdef MODULE
#define PFM_FMT_BUILTIN_FLAG	0	/* not built as a module */
#else
#define PFM_FMT_BUILTIN_FLAG	PFM_PMUFL_IS_BUILTIN /* built as a module */
#endif

int pfm_fmt_register(struct pfm_smpl_fmt *fmt);
int pfm_fmt_unregister(struct pfm_smpl_fmt *fmt);
void pfm_sysfs_builtin_fmt_add(void);

int  pfm_sysfs_add_fmt(struct pfm_smpl_fmt *fmt);
void pfm_sysfs_remove_fmt(struct pfm_smpl_fmt *fmt);

#endif /* __PERFMON_FMT_H__ */

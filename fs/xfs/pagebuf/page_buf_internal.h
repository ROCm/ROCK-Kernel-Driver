/*
 * Copyright (c) 2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

/*
 * Written by Steve Lord at SGI
 */

#ifndef __PAGE_BUF_PRIVATE_H__
#define __PAGE_BUF_PRIVATE_H__

#include <linux/percpu.h>
#include "page_buf.h"

#define _PAGE_BUF_INTERNAL_
#define PB_DEFINE_TRACES
#include "page_buf_trace.h"

#ifdef PAGEBUF_LOCK_TRACKING
#define PB_SET_OWNER(pb)	(pb->pb_last_holder = current->pid)
#define PB_CLEAR_OWNER(pb)	(pb->pb_last_holder = -1)
#define PB_GET_OWNER(pb)	(pb->pb_last_holder)
#else
#define PB_SET_OWNER(pb)
#define PB_CLEAR_OWNER(pb)
#define PB_GET_OWNER(pb)
#endif /* PAGEBUF_LOCK_TRACKING */

/* Tracing utilities for pagebuf */
typedef struct {
	int			event;
	unsigned long		pb;
	page_buf_flags_t	flags;
	unsigned short		hold;
	unsigned short		lock_value;
	void			*task;
	void			*misc;
	void			*ra;
	loff_t			offset;
	size_t			size;
} pagebuf_trace_t;

struct pagebuf_trace_buf {
	pagebuf_trace_t		*buf;
	volatile int		start;
	volatile int		end;
};

#define PB_TRACE_BUFSIZE	1024
#define CIRC_INC(i)     (((i) + 1) & (PB_TRACE_BUFSIZE - 1))

/*
 * Tunable pagebuf parameters
 */

typedef struct pb_sysctl_val {
	int min;
	int val;
	int max;
} pb_sysctl_val_t;

typedef struct pagebuf_param {
	pb_sysctl_val_t	flush_interval;	/* interval between runs of the
					 * delwri flush daemon.  */
	pb_sysctl_val_t	age_buffer;	/* time for buffer to age before
					 * we flush it.  */
	pb_sysctl_val_t	stats_clear;	/* clear the pagebuf stats */
	pb_sysctl_val_t	debug;		/* debug tracing on or off */
} pagebuf_param_t;

enum {
	PB_FLUSH_INT = 1,
	PB_FLUSH_AGE = 2,
	PB_STATS_CLEAR = 3,
	PB_DEBUG = 4
};

extern pagebuf_param_t	pb_params;

/*
 * Pagebuf statistics
 */

struct pbstats {
	u_int32_t	pb_get;
	u_int32_t	pb_create;
	u_int32_t	pb_get_locked;
	u_int32_t	pb_get_locked_waited;
	u_int32_t	pb_busy_locked;
	u_int32_t	pb_miss_locked;
	u_int32_t	pb_page_retries;
	u_int32_t	pb_page_found;
	u_int32_t	pb_get_read;
};

DECLARE_PER_CPU(struct pbstats, pbstats);

/* We don't disable preempt, not too worried about poking the
 * wrong cpu's stat for now */
#define PB_STATS_INC(count)	(__get_cpu_var(pbstats).count++)

#ifndef STATIC
# define STATIC	static
#endif

#endif /* __PAGE_BUF_PRIVATE_H__ */

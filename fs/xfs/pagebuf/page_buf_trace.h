/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
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

#ifndef __PAGEBUF_TRACE__
#define __PAGEBUF_TRACE__

#ifdef PB_DEFINE_TRACES
#define PB_TRACE_START	typedef enum {
#define PB_TRACE_REC(x)	pb_trace_point_##x
#define PB_TRACE_END	} pb_trace_var_t;
#else
#define PB_TRACE_START	static char	*event_names[] = {
#define PB_TRACE_REC(x)	#x
#define PB_TRACE_END	};
#endif

PB_TRACE_START
PB_TRACE_REC(get),
PB_TRACE_REC(get_obj),
PB_TRACE_REC(free_obj),
PB_TRACE_REC(look_pg),
PB_TRACE_REC(get_read),
PB_TRACE_REC(no_daddr),
PB_TRACE_REC(hold),
PB_TRACE_REC(rele),
PB_TRACE_REC(done),
PB_TRACE_REC(ioerror),
PB_TRACE_REC(iostart),
PB_TRACE_REC(end_io),
PB_TRACE_REC(do_io),
PB_TRACE_REC(ioreq),
PB_TRACE_REC(iowait),
PB_TRACE_REC(iowaited),
PB_TRACE_REC(free_lk),
PB_TRACE_REC(freed_l),
PB_TRACE_REC(cmp),
PB_TRACE_REC(get_lk),
PB_TRACE_REC(got_lk),
PB_TRACE_REC(skip),
PB_TRACE_REC(lock),
PB_TRACE_REC(locked),
PB_TRACE_REC(unlock),
PB_TRACE_REC(avl_ret),
PB_TRACE_REC(condlck),
PB_TRACE_REC(avl_ins),
PB_TRACE_REC(walkq1),
PB_TRACE_REC(walkq2),
PB_TRACE_REC(walkq3),
PB_TRACE_REC(delwri_q),
PB_TRACE_REC(delwri_uq),
PB_TRACE_REC(pin),
PB_TRACE_REC(unpin),
PB_TRACE_REC(file_write),
PB_TRACE_REC(external),
PB_TRACE_END

extern void pb_trace_func(page_buf_t *, int, void *, void *);
#ifdef PAGEBUF_TRACE
# define PB_TRACE(pb, event, misc)		\
	pb_trace_func(pb, event, (void *) misc,	\
			(void *)__builtin_return_address(0))
#else
# define PB_TRACE(pb, event, misc)	do { } while (0)
#endif

#endif	/* __PAGEBUF_TRACE__ */

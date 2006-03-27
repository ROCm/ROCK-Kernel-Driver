/* taskstats.h - exporting per-task statistics
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2006
 *           (C) Balbir Singh,   IBM Corp. 2006
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _LINUX_TASKSTATS_H
#define _LINUX_TASKSTATS_H

/* Format for per-task data returned to userland when
 *	- a task exits
 *	- listener requests stats for a task
 *
 * The struct is versioned. Newer versions should only add fields to
 * the bottom of the struct to maintain backward compatibility.
 *
 * To create the next version, bump up the taskstats_version variable
 * and delineate the start of newly added fields with a comment indicating
 * the version number.
 */

#define TASKSTATS_VERSION	1
#define TASKSTATS_NOPID	-1

struct taskstats {
	/* Maintain 64-bit alignment while extending */
	/* Version 1 */

	/* XXX_count is number of delay values recorded.
	 * XXX_total is corresponding cumulative delay in nanoseconds
	 */

#define TASKSTATS_NOCPUSTATS	1
	__u64	cpu_count;
	__u64	cpu_delay_total;	/* wait, while runnable, for cpu */
	__u64	blkio_count;
	__u64	blkio_delay_total;	/* sync,block io completion wait*/
	__u64	swapin_count;
	__u64	swapin_delay_total;	/* swapin page fault wait*/

	__u64	cpu_run_real_total;	/* cpu "wall-clock" running time
					 * Potentially accounts for cpu
					 * virtualization, on some arches
					 */
	__u64	cpu_run_virtual_total;	/* cpu "virtual" running time
					 * Uses time intervals as seen by
					 * the kernel
					 */
};


#define TASKSTATS_LISTEN_GROUP	0x1

/*
 * Commands sent from userspace
 * Not versioned. New commands should only be inserted at the enum's end
 */

enum {
	TASKSTATS_CMD_UNSPEC = 0,	/* Reserved */
	TASKSTATS_CMD_GET,		/* user->kernel request/get-response */
	TASKSTATS_CMD_NEW,		/* kernel->user event */
	__TASKSTATS_CMD_MAX,
};

#define TASKSTATS_CMD_MAX (__TASKSTATS_CMD_MAX - 1)

enum {
	TASKSTATS_TYPE_UNSPEC = 0,	/* Reserved */
	TASKSTATS_TYPE_PID,		/* Process id */
	TASKSTATS_TYPE_TGID,		/* Thread group id */
	TASKSTATS_TYPE_STATS,		/* taskstats structure */
	TASKSTATS_TYPE_AGGR_PID,	/* contains pid + stats */
	TASKSTATS_TYPE_AGGR_TGID,	/* contains tgid + stats */
	__TASKSTATS_TYPE_MAX,
};

#define TASKSTATS_TYPE_MAX (__TASKSTATS_TYPE_MAX - 1)

enum {
	TASKSTATS_CMD_ATTR_UNSPEC = 0,
	TASKSTATS_CMD_ATTR_PID,
	TASKSTATS_CMD_ATTR_TGID,
	__TASKSTATS_CMD_ATTR_MAX,
};

#define TASKSTATS_CMD_ATTR_MAX (__TASKSTATS_CMD_ATTR_MAX - 1)

/* NETLINK_GENERIC related info */

#define TASKSTATS_GENL_NAME	"TASKSTATS"
#define TASKSTATS_GENL_VERSION	0x1

#ifdef __KERNEL__

#include <linux/sched.h>

enum {
	TASKSTATS_MSG_UNICAST,		/* send data only to requester */
	TASKSTATS_MSG_MULTICAST,	/* send data to a group */
};

#ifdef CONFIG_TASKSTATS
extern void taskstats_exit_pid(struct task_struct *);
#else
static inline void taskstats_exit_pid(struct task_struct *tsk)
{}
#endif

#endif /* __KERNEL__ */
#endif /* _LINUX_TASKSTATS_H */

#ifndef _LINUX_CGROUP_FREEZER_H
#define _LINUX_CGROUP_FREEZER_H
/*
 * cgroup_freezer.h -  control group freezer subsystem interface
 *
 * Copyright IBM Corporation, 2007
 *
 * Author : Cedric Le Goater <clg@fr.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/cgroup.h>

#ifdef CONFIG_CGROUP_FREEZER

enum freezer_state {
	STATE_RUNNING = 0,
	STATE_FREEZING,
	STATE_FROZEN,
};

struct freezer {
	struct cgroup_subsys_state css;
	enum freezer_state state;
	spinlock_t lock; /* protects _writes_ to state */
};

static inline struct freezer *cgroup_freezer(
		struct cgroup *cgroup)
{
	return container_of(
		cgroup_subsys_state(cgroup, freezer_subsys_id),
		struct freezer, css);
}

static inline struct freezer *task_freezer(struct task_struct *task)
{
	return container_of(task_subsys_state(task, freezer_subsys_id),
			    struct freezer, css);
}

static inline int cgroup_frozen(struct task_struct *task)
{
	struct freezer *freezer;
	enum freezer_state state;

	task_lock(task);
	freezer = task_freezer(task);
	state = freezer->state;
	task_unlock(task);

	return state == STATE_FROZEN;
}

#else /* !CONFIG_CGROUP_FREEZER */

static inline int cgroup_frozen(struct task_struct *task)
{
	return 0;
}

#endif /* !CONFIG_CGROUP_FREEZER */

#endif /* _LINUX_CGROUP_FREEZER_H */

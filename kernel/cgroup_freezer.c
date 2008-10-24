/*
 * cgroup_freezer.c -  control group freezer subsystem
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

#include <linux/module.h>
#include <linux/cgroup.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/freezer.h>
#include <linux/cgroup_freezer.h>
#include <linux/seq_file.h>

/*
 * Buffer size for freezer state is limited by cgroups write_string()
 * interface. See cgroups code for the current size.
 */
static const char *freezer_state_strs[] = {
	"RUNNING",
	"FREEZING",
	"FROZEN",
};
#define STATE_MAX_STRLEN 8

/*
 * State diagram (transition labels in parenthesis):
 *
 *  RUNNING -(FROZEN)-> FREEZING -(FROZEN)-> FROZEN
 *    ^ ^                  |                   |
 *    | |____(RUNNING)_____|                   |
 *    |___________________________(RUNNING)____|
 */

struct cgroup_subsys freezer_subsys;

/* Locks taken and their ordering:
 *
 * freezer_create(), freezer_destroy():
 * cgroup_lock [ by cgroup core ]
 *
 * can_attach():
 * cgroup_lock
 *
 * cgroup_frozen():
 * task_lock
 *
 * freezer_fork():
 * task_lock
 *  freezer->lock
 *   sighand->siglock
 *
 * freezer_read():
 * cgroup_lock
 *  freezer->lock
 *   read_lock css_set_lock
 *
 * freezer_write():
 * cgroup_lock
 *   freezer->lock
 *    read_lock css_set_lock
 *     [unfreeze: task_lock (reaquire freezer->lock)]
 *      sighand->siglock
 */
static struct cgroup_subsys_state *freezer_create(struct cgroup_subsys *ss,
						  struct cgroup *cgroup)
{
	struct freezer *freezer;

	freezer = kzalloc(sizeof(struct freezer), GFP_KERNEL);
	if (!freezer)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&freezer->lock);
	freezer->state = STATE_RUNNING;
	return &freezer->css;
}

static void freezer_destroy(struct cgroup_subsys *ss,
			    struct cgroup *cgroup)
{
	kfree(cgroup_freezer(cgroup));
}

/* Task is frozen or will freeze immediately when next it gets woken */
static bool is_task_frozen_enough(struct task_struct *task)
{
	return (frozen(task) || (task_is_stopped_or_traced(task) && freezing(task)));
}

/*
 * The call to cgroup_lock() in the freezer.state write method prevents
 * a write to that file racing against an attach, and hence the
 * can_attach() result will remain valid until the attach completes.
 */
static int freezer_can_attach(struct cgroup_subsys *ss,
			      struct cgroup *new_cgroup,
			      struct task_struct *task)
{
	struct freezer *freezer;
	int retval;

	/* Anything frozen can't move or be moved to/from */

	if (is_task_frozen_enough(task))
		return -EBUSY;

	freezer = cgroup_freezer(new_cgroup);
	if (freezer->state == STATE_FROZEN)
		return -EBUSY;

	retval = 0;
	task_lock(task);
	freezer = task_freezer(task);
	if (freezer->state == STATE_FROZEN)
		retval = -EBUSY;
	task_unlock(task);
	return retval;
}

static void freezer_fork(struct cgroup_subsys *ss, struct task_struct *task)
{
	struct freezer *freezer;

	task_lock(task);
	freezer = task_freezer(task);

	BUG_ON(freezer->state == STATE_FROZEN);

	/* Locking avoids race with FREEZING -> RUNNING transitions. */
	spin_lock_irq(&freezer->lock);
	if (freezer->state == STATE_FREEZING)
		freeze_task(task, true);
	spin_unlock_irq(&freezer->lock);

	task_unlock(task);
}

/*
 * caller must hold freezer->lock
 */
static void check_if_frozen(struct cgroup *cgroup,
			     struct freezer *freezer)
{
	struct cgroup_iter it;
	struct task_struct *task;
	unsigned int nfrozen = 0, ntotal = 0;

	cgroup_iter_start(cgroup, &it);
	while ((task = cgroup_iter_next(cgroup, &it))) {
		ntotal++;
		if (is_task_frozen_enough(task))
			nfrozen++;
	}

	/*
	 * Transition to FROZEN when no new tasks can be added ensures
	 * that we never exist in the FROZEN state while there are unfrozen
	 * tasks.
	 */
	if (nfrozen == ntotal)
		freezer->state = STATE_FROZEN;
	cgroup_iter_end(cgroup, &it);
}

static ssize_t freezer_read(struct cgroup *cgroup, struct cftype *cft,
			    struct seq_file *m)
{
	struct freezer *freezer;
	enum freezer_state state;

	if (!cgroup_lock_live_group(cgroup))
		return -ENODEV;

	freezer = cgroup_freezer(cgroup);
	spin_lock_irq(&freezer->lock);
	state = freezer->state;
	if (state == STATE_FREEZING) {
		/* We change from FREEZING to FROZEN lazily if the cgroup was
		 * only partially frozen when we exitted write. */
		check_if_frozen(cgroup, freezer);
		state = freezer->state;
	}
	spin_unlock_irq(&freezer->lock);
	cgroup_unlock();

	seq_puts(m, freezer_state_strs[state]);
	seq_putc(m, '\n');
	return 0;
}

static int try_to_freeze_cgroup(struct cgroup *cgroup, struct freezer *freezer)
{
	struct cgroup_iter it;
	struct task_struct *task;
	unsigned int num_cant_freeze_now = 0;

	freezer->state = STATE_FREEZING;
	cgroup_iter_start(cgroup, &it);
	while ((task = cgroup_iter_next(cgroup, &it))) {
		if (!freeze_task(task, true))
			continue;
		if (is_task_frozen_enough(task))
			continue;
		if (!freezing(task) && !freezer_should_skip(task))
			num_cant_freeze_now++;
	}
	cgroup_iter_end(cgroup, &it);

	return num_cant_freeze_now ? -EBUSY : 0;
}

static int unfreeze_cgroup(struct cgroup *cgroup, struct freezer *freezer)
{
	struct cgroup_iter it;
	struct task_struct *task;

	cgroup_iter_start(cgroup, &it);
	while ((task = cgroup_iter_next(cgroup, &it))) {
		int do_wake;

		/* Drop freezer->lock to fix lock ordering (see freezer_fork) */
		spin_unlock_irq(&freezer->lock);
		task_lock(task);
		spin_lock_irq(&freezer->lock);
		do_wake = __thaw_process(task);
		task_unlock(task);
		if (do_wake)
			wake_up_process(task);
	}
	cgroup_iter_end(cgroup, &it);
	freezer->state = STATE_RUNNING;

	return 0;
}

static int freezer_change_state(struct cgroup *cgroup,
				enum freezer_state goal_state)
{
	struct freezer *freezer;
	int retval = 0;

	freezer = cgroup_freezer(cgroup);
	spin_lock_irq(&freezer->lock);
	check_if_frozen(cgroup, freezer); /* may update freezer->state */
	if (goal_state == freezer->state)
		goto out;
	switch (freezer->state) {
	case STATE_RUNNING:
		retval = try_to_freeze_cgroup(cgroup, freezer);
		break;
	case STATE_FREEZING:
		if (goal_state == STATE_FROZEN) {
			/* Userspace is retrying after
			 * "/bin/echo FROZEN > freezer.state" returned -EBUSY */
			retval = try_to_freeze_cgroup(cgroup, freezer);
			break;
		}
		/* state == FREEZING and goal_state == RUNNING, so unfreeze */
	case STATE_FROZEN:
		retval = unfreeze_cgroup(cgroup, freezer);
		break;
	default:
		break;
	}
out:
	spin_unlock_irq(&freezer->lock);

	return retval;
}

static ssize_t freezer_write(struct cgroup *cgroup,
			     struct cftype *cft,
			     struct file *file,
			     const char __user *userbuf,
			     size_t nbytes, loff_t *unused_ppos)
{
	char buffer[STATE_MAX_STRLEN + 1];
	int retval = 0;
	enum freezer_state goal_state;

	if (nbytes >= PATH_MAX)
		return -E2BIG;
	nbytes = min(sizeof(buffer) - 1, nbytes);
	if (copy_from_user(buffer, userbuf, nbytes))
		return -EFAULT;
	buffer[nbytes + 1] = 0; /* nul-terminate */
	strstrip(buffer); /* remove any trailing whitespace */
	if (strcmp(buffer, freezer_state_strs[STATE_RUNNING]) == 0)
		goal_state = STATE_RUNNING;
	else if (strcmp(buffer, freezer_state_strs[STATE_FROZEN]) == 0)
		goal_state = STATE_FROZEN;
	else
		return -EIO;

	if (!cgroup_lock_live_group(cgroup))
		return -ENODEV;
	retval = freezer_change_state(cgroup, goal_state);
	cgroup_unlock();
	return retval;
}

static struct cftype files[] = {
	{
		.name = "state",
		.read_seq_string = freezer_read,
		.write = freezer_write,
	},
};

static int freezer_populate(struct cgroup_subsys *ss, struct cgroup *cgroup)
{
	return cgroup_add_files(cgroup, ss, files, ARRAY_SIZE(files));
}

struct cgroup_subsys freezer_subsys = {
	.name		= "freezer",
	.create		= freezer_create,
	.destroy	= freezer_destroy,
	.populate	= freezer_populate,
	.subsys_id	= freezer_subsys_id,
	.can_attach	= freezer_can_attach,
	.attach		= NULL,
	.fork		= freezer_fork,
	.exit		= NULL,
};

/*
 * Copyright (C) 2003 Sistina Software.
 *
 * Module Author: Heinz Mauelshagen
 *
 * This file is released under the GPL.
 *
 * Path selector housekeeping (register/unregister/...)
 */

#include "dm.h"
#include "dm-path-selector.h"

#include <linux/slab.h>

struct ps_internal {
	struct path_selector_type pt;

	struct list_head list;
	long use;
};

static LIST_HEAD(_path_selectors);
static DECLARE_MUTEX(_lock);

struct path_selector_type *__find_path_selector_type(const char *name)
{
	struct ps_internal *li;

	list_for_each_entry (li, &_path_selectors, list) {
		if (!strcmp(name, li->pt.name))
			return &li->pt;
	}

	return NULL;
}

struct path_selector_type *dm_get_path_selector(const char *name)
{
	struct path_selector_type *lb;

	if (!name)
		return NULL;

	down(&_lock);
	lb = __find_path_selector_type(name);
	if (lb) {
		struct ps_internal *li = (struct ps_internal *) lb;
		li->use++;
	}
	up(&_lock);

	return lb;
}

void dm_put_path_selector(struct path_selector_type *l)
{
	struct ps_internal *li = (struct ps_internal *) l;

	down(&_lock);
	if (--li->use < 0)
		BUG();
	up(&_lock);

	return;
}

static struct ps_internal *_alloc_path_selector(struct path_selector_type *pt)
{
	struct ps_internal *psi = kmalloc(sizeof(*psi), GFP_KERNEL);

	if (psi) {
		memset(psi, 0, sizeof(*psi));
		memcpy(psi, pt, sizeof(*pt));
	}

	return psi;
}

int dm_register_path_selector(struct path_selector_type *pst)
{
	int r = 0;
	struct ps_internal *psi = _alloc_path_selector(pst);

	if (!psi)
		return -ENOMEM;

	down(&_lock);
	if (__find_path_selector_type(pst->name)) {
		kfree(psi);
		r = -EEXIST;
	} else
		list_add(&psi->list, &_path_selectors);

	up(&_lock);

	return r;
}

int dm_unregister_path_selector(struct path_selector_type *pst)
{
	struct ps_internal *psi;

	down(&_lock);
	psi = (struct ps_internal *) __find_path_selector_type(pst->name);
	if (!psi) {
		up(&_lock);
		return -EINVAL;
	}

	if (psi->use) {
		up(&_lock);
		return -ETXTBSY;
	}

	list_del(&psi->list);
	up(&_lock);

	kfree(psi);

	return 0;
}

/*-----------------------------------------------------------------
 * Path handling code, paths are held in lists
 *---------------------------------------------------------------*/
struct path_info {
	struct list_head list;
	struct path *path;
};

static struct path_info *path_lookup(struct list_head *head, struct path *p)
{
	struct path_info *pi;

	list_for_each_entry (pi, head, list)
		if (pi->path == p)
			return pi;

	return NULL;
}

/*-----------------------------------------------------------------
 * Round robin selector
 *---------------------------------------------------------------*/
struct selector {
	spinlock_t lock;

	struct list_head valid_paths;
	struct list_head invalid_paths;
};

static struct selector *alloc_selector(void)
{
	struct selector *s = kmalloc(sizeof(*s), GFP_KERNEL);

	if (s) {
		INIT_LIST_HEAD(&s->valid_paths);
		INIT_LIST_HEAD(&s->invalid_paths);
		s->lock = SPIN_LOCK_UNLOCKED;
	}

	return s;
}

/* Path selector constructor */
static int rr_ctr(struct path_selector *ps)
{
	struct selector *s;

	s = alloc_selector();
	if (!s)
		return -ENOMEM;

	ps->context = s;
	return 0;
}

static void free_paths(struct list_head *paths)
{
	struct path_info *pi, *next;

	list_for_each_entry_safe (pi, next, paths, list) {
		list_del(&pi->list);
		kfree(pi);
	}
}

/* Path selector destructor */
static void rr_dtr(struct path_selector *ps)
{
	struct selector *s = (struct selector *) ps->context;
	free_paths(&s->valid_paths);
	free_paths(&s->invalid_paths);
	kfree(s);
}

/* Path add context */
static int rr_add_path(struct path_selector *ps, struct path *path,
		       int argc, char **argv, char **error)
{
	struct selector *s = (struct selector *) ps->context;
	struct path_info *pi;

	/* parse the path arguments */
	if (argc != 0) {
		*error = "round-robin ps: incorrect number of arguments";
		return -EINVAL;
	}

	/* allocate the path */
	pi = kmalloc(sizeof(*pi), GFP_KERNEL);
	if (!pi) {
		*error = "round-robin ps: Error allocating path context";
		return -ENOMEM;
	}

	pi->path = path;

	spin_lock(&s->lock);
	list_add(&pi->list, &s->valid_paths);
	spin_unlock(&s->lock);

	return 0;
}

static void rr_fail_path(struct path_selector *ps, struct path *p)
{
	unsigned long flags;
	struct selector *s = (struct selector *) ps->context;
	struct path_info *pi;

	/*
	 * This function will be called infrequently so we don't
	 * mind the expense of these searches.
	 */
	spin_lock_irqsave(&s->lock, flags);
	pi = path_lookup(&s->valid_paths, p);
	if (!pi)
		pi = path_lookup(&s->invalid_paths, p);

	if (!pi)
		DMWARN("asked to change the state of an unknown path");

	else
		list_move(&pi->list, &s->invalid_paths);

	spin_unlock_irqrestore(&s->lock, flags);
}

/* Path selector */
static struct path *rr_select_path(struct path_selector *ps)
{
	unsigned long flags;
	struct selector *s = (struct selector *) ps->context;
	struct path_info *pi = NULL;

	spin_lock_irqsave(&s->lock, flags);
	if (!list_empty(&s->valid_paths)) {
		pi = list_entry(s->valid_paths.next, struct path_info, list);
		list_move_tail(&pi->list, &s->valid_paths);
	}
	spin_unlock_irqrestore(&s->lock, flags);

	return pi ? pi->path : NULL;
}

/* Path status */
static int rr_status(struct path_selector *ps, struct path *path,
		     status_type_t type, char *result, unsigned int maxlen)
{
	return 0;
}

static struct path_selector_type rr_ps = {
	.name = "round-robin",
	.table_args = 0,
	.info_args = 0,
	.ctr = rr_ctr,
	.dtr = rr_dtr,
	.add_path = rr_add_path,
	.fail_path = rr_fail_path,
	.select_path = rr_select_path,
	.status = rr_status,
};

/*
 * (Un)register all path selectors (FIXME: remove this after tests)
 */
int dm_register_path_selectors(void)
{
	return dm_register_path_selector(&rr_ps);
}

void dm_unregister_path_selectors(void)
{
	dm_unregister_path_selector(&rr_ps);
}

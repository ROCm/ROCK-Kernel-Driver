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
	struct path_selector_type pst;

	struct list_head list;
	long use;
};

#define pst_to_psi(__pst) container_of((__pst), struct ps_internal, pst)

static LIST_HEAD(_path_selectors);
static DECLARE_MUTEX(_lock);

struct path_selector_type *__find_path_selector_type(const char *name)
{
	struct ps_internal *li;

	list_for_each_entry (li, &_path_selectors, list) {
		if (!strcmp(name, li->pst.name))
			return &li->pst;
	}

	return NULL;
}

struct path_selector_type *dm_get_path_selector(const char *name)
{
	struct path_selector_type *pst;

	if (!name)
		return NULL;

	down(&_lock);
	pst = __find_path_selector_type(name);
	if (pst) {
		struct ps_internal *psi = pst_to_psi(pst);

		if (psi->use == 0 && !try_module_get(pst->module))
			psi = NULL;
		else
			psi->use++;
	}
	up(&_lock);

	return pst;
}

void dm_put_path_selector(struct path_selector_type *pst)
{
	struct ps_internal *psi;

	down(&_lock);
	pst = __find_path_selector_type(pst->name);
	if (!pst)
		return;

	psi = pst_to_psi(pst);
	if (--psi->use == 0)
		module_put(psi->pst.module);

	if (psi->use < 0)
		BUG();
	up(&_lock);
}

static struct ps_internal *_alloc_path_selector(struct path_selector_type *pt)
{
	struct ps_internal *psi = kmalloc(sizeof(*psi), GFP_KERNEL);

	if (psi) {
		memset(psi, 0, sizeof(*psi));
		memcpy(&psi->pst, pt, sizeof(*pt));
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

EXPORT_SYMBOL(dm_register_path_selector);

int dm_unregister_path_selector(struct path_selector_type *pst)
{
	struct ps_internal *psi;

	down(&_lock);
	pst = __find_path_selector_type(pst->name);
	if (!pst) {
		up(&_lock);
		return -EINVAL;
	}

	psi = pst_to_psi(pst);
	if (psi->use) {
		up(&_lock);
		return -ETXTBSY;
	}

	list_del(&psi->list);
	up(&_lock);

	kfree(psi);

	return 0;
}

EXPORT_SYMBOL(dm_unregister_path_selector);

/*-----------------------------------------------------------------
 * Path handling code, paths are held in lists
 *---------------------------------------------------------------*/

/* FIXME: get rid of this */
#define RR_FAIL_COUNT	1

struct path_info {
	struct list_head list;
	struct path *path;
	unsigned fail_count;
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

#define RR_MIN_IO		1000

struct selector {
	spinlock_t lock;

	struct path_info *current_path;
	unsigned current_count;

	struct list_head valid_paths;
	struct list_head invalid_paths;
};

static struct selector *alloc_selector(void)
{
	struct selector *s = kmalloc(sizeof(*s), GFP_KERNEL);

	if (s) {
		memset(s, 0, sizeof(*s));
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

	pi->fail_count = 0;
	pi->path = path;

	spin_lock(&s->lock);
	list_add(&pi->list, &s->valid_paths);
	spin_unlock(&s->lock);

	return 0;
}

static void rr_end_io(struct path_selector *ps, struct bio *bio, int error,
		      union map_info *info)
{
	unsigned long flags;
	struct selector *s = (struct selector *) ps->context;
	struct path_info *pi = (struct path_info *)info->ptr;

	if (likely(!error))
		return;

	spin_lock_irqsave(&s->lock, flags);

	if (++pi->fail_count == RR_FAIL_COUNT) {
		list_move(&pi->list, &s->invalid_paths);

		if (pi == s->current_path)
			s->current_path = NULL;
	}

	spin_unlock_irqrestore(&s->lock, flags);
}

/* Path selector */
static struct path *rr_select_path(struct path_selector *ps, struct bio *bio,
				   union map_info *info)
{
	unsigned long flags;
	struct selector *s = (struct selector *) ps->context;
	struct path_info *pi = NULL;

	spin_lock_irqsave(&s->lock, flags);

	/* Do we need to select a new path? */
	if (s->current_path && s->current_count-- > 0) {
		pi = s->current_path;
		goto done;
	}

	if (!list_empty(&s->valid_paths)) {
		pi = list_entry(s->valid_paths.next, struct path_info, list);
		list_move_tail(&pi->list, &s->valid_paths);

		s->current_path = pi;
		s->current_count = RR_MIN_IO;
	}

 done:
	spin_unlock_irqrestore(&s->lock, flags);

	info->ptr = pi;
	return pi ? pi->path : NULL;
}

/* Path status */
static int rr_status(struct path_selector *ps, struct path *path,
		     status_type_t type, char *result, unsigned int maxlen)
{
	unsigned long flags;
	struct path_info *pi;
	int failed = 0;
	struct selector *s = (struct selector *) ps->context;
	int sz = 0;

	if (type == STATUSTYPE_TABLE)
		return 0;

	spin_lock_irqsave(&s->lock, flags);

	/*
	 * Is status called often for testing or something?
	 * If so maybe a ps's info should be allocated w/ path
	 * so a simple container_of can be used.
	 */
	pi = path_lookup(&s->valid_paths, path);
	if (!pi) {
		failed = 1;
		pi = path_lookup(&s->invalid_paths, path);
	}

	sz = scnprintf(result, maxlen, "%s %u ", failed ? "F" : "A",
		       pi->fail_count);

	spin_unlock_irqrestore(&s->lock, flags);

	return sz;
}

static struct path_selector_type rr_ps = {
	.name = "round-robin",
	.module = THIS_MODULE,
	.table_args = 0,
	.info_args = 0,
	.ctr = rr_ctr,
	.dtr = rr_dtr,
	.add_path = rr_add_path,
	.end_io = rr_end_io,
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

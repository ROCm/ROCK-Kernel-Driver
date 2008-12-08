/*
 * Copyright (C) 2004-2005 IBM Corp.  All Rights Reserved.
 * Copyright (C) 2006-2008 NEC Corporation.
 *
 * dm-queue-length.c
 *
 * Module Author: Stefan Bader, IBM
 * Modified by: Kiyoshi Ueda, NEC
 *
 * This file is released under the GPL.
 *
 * Load balancing path selector.
 */

#include "dm.h"
#include "dm-path-selector.h"

#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <asm/atomic.h>

#define DM_MSG_PREFIX	"multipath queue-length"
#define QL_MIN_IO	128
#define QL_VERSION	"0.1.0"

struct selector {
	struct list_head	valid_paths;
	struct list_head	failed_paths;
};

struct path_info {
	struct list_head	list;
	struct dm_path		*path;
	unsigned int		repeat_count;
	atomic_t		qlen;
};

static struct selector *alloc_selector(void)
{
	struct selector *s = kzalloc(sizeof(*s), GFP_KERNEL);

	if (s) {
		INIT_LIST_HEAD(&s->valid_paths);
		INIT_LIST_HEAD(&s->failed_paths);
	}

	return s;
}

static int ql_create(struct path_selector *ps, unsigned argc, char **argv)
{
	struct selector *s = alloc_selector();

	if (!s)
		return -ENOMEM;

	ps->context = s;

	return 0;
}

static void ql_free_paths(struct list_head *paths)
{
	struct path_info *cpi, *npi;

	list_for_each_entry_safe(cpi, npi, paths, list) {
		list_del(&cpi->list);
		cpi->path->pscontext = NULL;
		kfree(cpi);
	}
}

static void ql_destroy(struct path_selector *ps)
{
	struct selector *s = (struct selector *) ps->context;

	ql_free_paths(&s->valid_paths);
	ql_free_paths(&s->failed_paths);
	kfree(s);
	ps->context = NULL;
}

static int ql_add_path(struct path_selector *ps, struct dm_path *path,
		       int argc, char **argv, char **error)
{
	struct selector *s = (struct selector *) ps->context;
	struct path_info *pi;
	unsigned int repeat_count = QL_MIN_IO;

	/* Parse the arguments */
	if (argc > 1) {
		*error = "queue-length ps: incorrect number of arguments";
		return -EINVAL;
	}

	/* First path argument is number of I/Os before switching path. */
	if ((argc == 1) && (sscanf(argv[0], "%u", &repeat_count) != 1)) {
		*error = "queue-length ps: invalid repeat count";
		return -EINVAL;
	}

	/* Allocate the path information structure */
	pi = kmalloc(sizeof(*pi), GFP_KERNEL);
	if (!pi) {
		*error = "queue-length ps: Error allocating path information";
		return -ENOMEM;
	}

	pi->path = path;
	pi->repeat_count = repeat_count;
	atomic_set(&pi->qlen, 0);
	path->pscontext = pi;

	list_add_tail(&pi->list, &s->valid_paths);

	return 0;
}

static void ql_fail_path(struct path_selector *ps, struct dm_path *path)
{
	struct selector *s = (struct selector *) ps->context;
	struct path_info *pi = path->pscontext;

	list_move(&pi->list, &s->failed_paths);
}

static int ql_reinstate_path(struct path_selector *ps, struct dm_path *path)
{
	struct selector *s = (struct selector *) ps->context;
	struct path_info *pi = path->pscontext;

	list_move_tail(&pi->list, &s->valid_paths);

	return 0;
}

static inline int ql_compare_qlen(struct path_info *pi1, struct path_info *pi2)
{
	return atomic_read(&pi1->qlen) - atomic_read(&pi2->qlen);
}

static struct dm_path *ql_select_path(struct path_selector *ps,
				      unsigned *repeat_count, size_t nr_bytes)
{
	struct selector *s = (struct selector *) ps->context;
	struct path_info *cpi = NULL, *spi = NULL;

	if (list_empty(&s->valid_paths))
		return NULL;

	/* Change preferred (first in list) path to evenly balance. */
	list_move_tail(s->valid_paths.next, &s->valid_paths);

	list_for_each_entry(cpi, &s->valid_paths, list) {
		if (!spi)
			spi = cpi;
		else if (ql_compare_qlen(cpi, spi) < 0)
			spi = cpi;
	}

	if (spi)
		*repeat_count = spi->repeat_count;

	return spi ? spi->path : NULL;
}

static int ql_start_io(struct path_selector *ps, struct dm_path *path,
		       size_t nr_bytes)
{
	struct path_info *pi = path->pscontext;

	atomic_inc(&pi->qlen);

	return 0;
}

static int ql_end_io(struct path_selector *ps, struct dm_path *path,
		     size_t nr_bytes)
{
	struct path_info *pi = path->pscontext;

	atomic_dec(&pi->qlen);

	return 0;
}

static int ql_status(struct path_selector *ps, struct dm_path *path,
		     status_type_t type, char *result, unsigned int maxlen)
{
	int sz = 0;
	struct path_info *pi;

	/* When called with (path == NULL), return selector status/args. */
	if (!path)
		DMEMIT("0 ");
	else {
		pi = path->pscontext;

		switch (type) {
		case STATUSTYPE_INFO:
			DMEMIT("%u ", atomic_read(&pi->qlen));
			break;
		case STATUSTYPE_TABLE:
			DMEMIT("%u ", pi->repeat_count);
			break;
		}
	}

	return sz;
}

static struct path_selector_type ql_ps = {
	.name		= "queue-length",
	.module		= THIS_MODULE,
	.table_args	= 1,
	.info_args	= 1,
	.create		= ql_create,
	.destroy	= ql_destroy,
	.status		= ql_status,
	.add_path	= ql_add_path,
	.fail_path	= ql_fail_path,
	.reinstate_path	= ql_reinstate_path,
	.select_path	= ql_select_path,
	.start_io	= ql_start_io,
	.end_io		= ql_end_io,
};

static int __init dm_ql_init(void)
{
	int r = dm_register_path_selector(&ql_ps);

	if (r < 0)
		DMERR("register failed %d", r);

	DMINFO("version " QL_VERSION " loaded");

	return r;
}

static void __exit dm_ql_exit(void)
{
	int r = dm_unregister_path_selector(&ql_ps);

	if (r < 0)
		DMERR("unregister failed %d", r);
}

module_init(dm_ql_init);
module_exit(dm_ql_exit);

MODULE_AUTHOR("Stefan Bader <Stefan.Bader at de.ibm.com>");
MODULE_DESCRIPTION(
	"(C) Copyright IBM Corp. 2004,2005   All Rights Reserved.\n"
	DM_NAME " load balancing path selector (dm-queue-length.c version "
	QL_VERSION ")"
);
MODULE_LICENSE("GPL");

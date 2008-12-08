/*
 *****************************************************************************
 *                                                                           *
 *     (C)  Copyright 2008 Hewlett-Packard Development Company, L.P          *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify it   *
 * under the terms of the GNU General Public License as published by the Free*
 * Software  Foundation; either version 2 of the License, or (at your option)*
 * any later version.                                                        *
 *                                                                           *
 * This program is distributed in the hope that it will be useful, but       *
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY*
 * or FITNESS FOR  A PARTICULAR PURPOSE. See the GNU General Public License  *
 * for more details.                                                         *
 *                                                                           *
 * You should have received a copy of the GNU General Public License along   *
 * with this program; if not, write to the Free Software Foundation, Inc.,   *
 * 675 Mass Ave, Cambridge, MA 02139, USA.                                   *
 *                                                                           *
 *                                                                           *
 *****************************************************************************
 */

#include "dm.h"
#include "dm-path-selector.h"

#include <linux/slab.h>


#define RR_MIN_IO	-1

#define DM_MSG_PREFIX "multipath leastpendingio-path"

/*-----------------------------------------------------------------
 * Path-handling code, paths are held in lists
 *---------------------------------------------------------------*/
struct path_info {
	struct list_head list;
	struct dm_path *path;
	unsigned repeat_count;
	unsigned io_count;
};

static void free_paths(struct list_head *paths)
{
	struct path_info *pi, *next;

	list_for_each_entry_safe(pi, next, paths, list) {
		list_del(&pi->list);
		kfree(pi);
	}
}

/*-----------------------------------------------------------------
 * leastpendingio-path selector
 *---------------------------------------------------------------*/

struct selector {
	struct list_head valid_paths;
	struct list_head invalid_paths;
};

static struct selector *alloc_selector(void)
{
	struct selector *s = kmalloc(sizeof(*s), GFP_KERNEL);

	if (s) {
		INIT_LIST_HEAD(&s->valid_paths);
		INIT_LIST_HEAD(&s->invalid_paths);
	}

	return s;
}

static int lpp_create(struct path_selector *ps, unsigned argc, char **argv)
{
	struct selector *s;

	s = alloc_selector();
	if (!s)
		return -ENOMEM;

	ps->context = s;
	return 0;
}

static void lpp_destroy(struct path_selector *ps)
{
	struct selector *s = (struct selector *) ps->context;

	free_paths(&s->valid_paths);
	free_paths(&s->invalid_paths);
	kfree(s);
	ps->context = NULL;
}

static int lpp_status(struct path_selector *ps, struct dm_path *path,
		     status_type_t type, char *result, unsigned int maxlen)
{
	struct path_info *pi;
	int sz = 0;

	if (!path)
		DMEMIT("0 ");
	else {
		switch(type) {
		case STATUSTYPE_INFO:
			break;
		case STATUSTYPE_TABLE:
			pi = path->pscontext;
			DMEMIT("%u ", pi->repeat_count);
			break;
		}
	}

	return sz;
}

/*
 * Called during initialisation to register each path with an
 * optional repeat_count.
 */
static int lpp_add_path(struct path_selector *ps, struct dm_path *path,
		       int argc, char **argv, char **error)
{
	struct selector *s = (struct selector *) ps->context;
	struct path_info *pi;
	unsigned repeat_count = RR_MIN_IO;

	if (argc > 1) {
		*error = "leastpendingio-path ps: incorrect number of arguments";
		return -EINVAL;
	}

	/* First path argument is number of I/Os before switching path */
	if ((argc == 1) && (sscanf(argv[0], "%u", &repeat_count) != 1)) {
		*error = "leastpendingio-path ps: invalid repeat count";
		return -EINVAL;
	}

	/* allocate the path */
	pi = kmalloc(sizeof(*pi), GFP_KERNEL);
	if (!pi) {
		*error = "leastpendingio-path ps: Error allocating path context";
		return -ENOMEM;
	}

	pi->path = path;
	pi->repeat_count = repeat_count;
	pi->io_count = 0;

	path->pscontext = pi;

	list_add(&pi->list, &s->valid_paths);

	return 0;
}

static void lpp_fail_path(struct path_selector *ps, struct dm_path *p)
{
	struct selector *s = (struct selector *) ps->context;
	struct path_info *pi = p->pscontext;

	pi->io_count = 0;

	list_move(&pi->list, &s->invalid_paths);
}

static int lpp_reinstate_path(struct path_selector *ps, struct dm_path *p)
{
	struct selector *s = (struct selector *) ps->context;
	struct path_info *pi = p->pscontext;

	pi->io_count = 0;

	list_move(&pi->list, &s->valid_paths);

	return 0;
}

static struct dm_path* lpp_find_least_pending_io_path(struct selector *s)
{
	struct path_info *pi = NULL,*next = NULL,*least_io_path = NULL;
	struct list_head *paths = NULL;

	if(list_empty(&s->valid_paths)){
		return NULL;
	}
	paths = &s->valid_paths;
	list_for_each_entry_safe(pi, next, paths, list) {
		if(least_io_path == NULL)
			least_io_path = pi;
		if(least_io_path->io_count < pi->io_count) {
			least_io_path = pi;
		}
		if(least_io_path->io_count == 0)
			break;

	}
	if(least_io_path){
		least_io_path->io_count ++;
		return least_io_path->path;
	}

	return NULL;
}

static struct dm_path *lpp_select_path(struct path_selector *ps,
				       unsigned *repeat_count, size_t nr_bytes)
{
	struct selector *s = (struct selector *) ps->context;
	return lpp_find_least_pending_io_path(s);
}

static int lpp_end_io(struct path_selector *ps,struct dm_path *path)
{
	struct path_info *pi = NULL;

	DMINFO("leastpendingio-path: lpp_end_io");

	pi = path->pscontext;
	if(pi == NULL)
		return 1;

	pi->io_count--;

	return 0;
}

static struct path_selector_type lpp_ps = {
	.name = "leastpendingio-path",
	.module = THIS_MODULE,
	.table_args = 1,
	.info_args = 0,
	.create = lpp_create,
	.destroy = lpp_destroy,
	.status = lpp_status,
	.add_path = lpp_add_path,
	.fail_path = lpp_fail_path,
	.reinstate_path = lpp_reinstate_path,
	.select_path = lpp_select_path,
	.end_io	= lpp_end_io,
};



static int __init dm_lpp_init(void)
{
	int r = dm_register_path_selector(&lpp_ps);

	if (r < 0)
		DMERR("leastpendingio-path: register failed %d", r);

	DMINFO("dm-leastpendingio-path version 1.0.0 loaded");

	return r;
}

static void __exit dm_lpp_exit(void)
{
	int r = dm_unregister_path_selector(&lpp_ps);

	if (r < 0)
		DMERR("leastpendingio-path: unregister failed %d", r);
}

module_init(dm_lpp_init);
module_exit(dm_lpp_exit);

MODULE_DESCRIPTION(DM_NAME " leastpendingio-path multipath path selector");
MODULE_AUTHOR("Sakshi Chaitanya Veni <vsakshi@hp.com>");
MODULE_LICENSE("GPL");

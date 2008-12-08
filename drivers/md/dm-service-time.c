/*
 * Copyright (C) 2007-2008 NEC Corporation.  All Rights Reserved.
 *
 * Module Author: Kiyoshi Ueda
 *
 * This file is released under the GPL.
 *
 * Throughput oriented path selector.
 */

#include "dm.h"
#include "dm-path-selector.h"

#define DM_MSG_PREFIX	"multipath service-time"
#define ST_MIN_IO	2
#define ST_VERSION	"0.1.0"

struct selector {
	struct list_head valid_paths;
	struct list_head failed_paths;
};

struct path_info {
	struct list_head list;
	struct dm_path *path;
	unsigned int repeat_count;

	atomic_t in_flight;	/* Total size of in-flight I/Os */
	size_t perf;		/* Recent performance of the path */
	sector_t last_sectors;	/* Total sectors of the last disk_stat_read */
	size_t last_io_ticks;	/* io_ticks of the last disk_stat_read */
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

static int st_create(struct path_selector *ps, unsigned argc, char **argv)
{
	struct selector *s = alloc_selector();

	if (!s)
		return -ENOMEM;

	ps->context = s;
	return 0;
}

static void free_paths(struct list_head *paths)
{
	struct path_info *pi, *next;

	list_for_each_entry_safe(pi, next, paths, list) {
		list_del(&pi->list);
		pi->path->pscontext = NULL;
		kfree(pi);
	}
}

static void st_destroy(struct path_selector *ps)
{
	struct selector *s = (struct selector *) ps->context;

	free_paths(&s->valid_paths);
	free_paths(&s->failed_paths);
	kfree(s);
	ps->context = NULL;
}

static int st_status(struct path_selector *ps, struct dm_path *path,
		     status_type_t type, char *result, unsigned int maxlen)
{
	int sz = 0;
	struct path_info *pi;

	if (!path)
		DMEMIT("0 ");
	else {
		pi = path->pscontext;

		switch (type) {
		case STATUSTYPE_INFO:
			DMEMIT("if:%08lu pf:%06lu ",
			       (unsigned long) atomic_read(&pi->in_flight),
			       pi->perf);
			break;
		case STATUSTYPE_TABLE:
			DMEMIT("%u ", pi->repeat_count);
			break;
		}
	}

	return sz;
}

static int st_add_path(struct path_selector *ps, struct dm_path *path,
		       int argc, char **argv, char **error)
{
	struct selector *s = (struct selector *) ps->context;
	struct path_info *pi;
	unsigned int repeat_count = ST_MIN_IO;
	struct gendisk *disk = path->dev->bdev->bd_disk;

	if (argc > 1) {
		*error = "service-time ps: incorrect number of arguments";
		return -EINVAL;
	}

	/* First path argument is number of I/Os before switching path. */
	if ((argc == 1) && (sscanf(argv[0], "%u", &repeat_count) != 1)) {
		*error = "service-time ps: invalid repeat count";
		return -EINVAL;
	}

	/* allocate the path */
	pi = kmalloc(sizeof(*pi), GFP_KERNEL);
	if (!pi) {
		*error = "service-time ps: Error allocating path context";
		return -ENOMEM;
	}

	pi->path = path;
	pi->repeat_count = repeat_count;

	pi->perf = 0;
	pi->last_sectors = disk_stat_read(disk, sectors[READ])
			   + disk_stat_read(disk, sectors[WRITE]);
	pi->last_io_ticks = disk_stat_read(disk, io_ticks);
	atomic_set(&pi->in_flight, 0);

	path->pscontext = pi;

	list_add_tail(&pi->list, &s->valid_paths);

	return 0;
}

static void st_fail_path(struct path_selector *ps, struct dm_path *path)
{
	struct selector *s = (struct selector *) ps->context;
	struct path_info *pi = path->pscontext;

	list_move(&pi->list, &s->failed_paths);
}

static int st_reinstate_path(struct path_selector *ps, struct dm_path *path)
{
	struct selector *s = (struct selector *) ps->context;
	struct path_info *pi = path->pscontext;

	list_move_tail(&pi->list, &s->valid_paths);

	return 0;
}

static void stats_update(struct path_info *pi)
{
	sector_t sectors;
	size_t io_ticks, tmp;
	struct gendisk *disk = pi->path->dev->bdev->bd_disk;

	sectors = disk_stat_read(disk, sectors[READ])
		  + disk_stat_read(disk, sectors[WRITE]);
	io_ticks = disk_stat_read(disk, io_ticks);

	if ((sectors != pi->last_sectors) && (io_ticks != pi->last_io_ticks)) {
		tmp = (sectors - pi->last_sectors) << 9;
		do_div(tmp, jiffies_to_msecs((io_ticks - pi->last_io_ticks)));
		pi->perf = tmp;

		pi->last_sectors = sectors;
		pi->last_io_ticks = io_ticks;
	}
}

static int st_compare_load(struct path_info *pi1, struct path_info *pi2,
			   size_t new_io)
{
	size_t if1, if2;

	if1 = atomic_read(&pi1->in_flight);
	if2 = atomic_read(&pi2->in_flight);

	/*
	 * Case 1: No performace data available. Choose less loaded path.
	 */
	if (!pi1->perf || !pi2->perf)
		return if1 - if2;

	/*
	 * Case 2: Calculate service time. Choose faster path.
	 *           if ((if1+new_io)/pi1->perf < (if2+new_io)/pi2->perf) pi1.
	 *           if ((if1+new_io)/pi1->perf > (if2+new_io)/pi2->perf) pi2.
	 *         To avoid do_div(), use
	 *           if ((if1+new_io)*pi2->perf < (if2+new_io)*pi1->perf) pi1.
	 *           if ((if1+new_io)*pi2->perf > (if2+new_io)*pi1->perf) pi2.
	 */
	if1 = (if1 + new_io) << 10;
	if2 = (if2 + new_io) << 10;
	do_div(if1, pi1->perf);
	do_div(if2, pi2->perf);

	if (if1 != if2)
		return if1 - if2;

	/*
	 * Case 3: Service time is equal. Choose faster path.
	 */
	return pi2->perf - pi1->perf;
}

static struct dm_path *st_select_path(struct path_selector *ps,
				      unsigned *repeat_count, size_t nr_bytes)
{
	struct selector *s = (struct selector *) ps->context;
	struct path_info *pi = NULL, *best = NULL;

	if (list_empty(&s->valid_paths))
		return NULL;

	/* Change preferred (first in list) path to evenly balance. */
	list_move_tail(s->valid_paths.next, &s->valid_paths);

	/* Update performance information before best path selection */
	list_for_each_entry(pi, &s->valid_paths, list)
		stats_update(pi);

	list_for_each_entry(pi, &s->valid_paths, list) {
		if (!best)
			best = pi;
		else if (st_compare_load(pi, best, nr_bytes) < 0)
			best = pi;
	}

	if (best) {
		*repeat_count = best->repeat_count;
		return best->path;
	}

	return NULL;
}

static int st_start_io(struct path_selector *ps, struct dm_path *path,
		       size_t nr_bytes)
{
	struct path_info *pi = path->pscontext;

	atomic_add(nr_bytes, &pi->in_flight);

	return 0;
}

static int st_end_io(struct path_selector *ps, struct dm_path *path,
		     size_t nr_bytes)
{
	struct path_info *pi = path->pscontext;

	atomic_sub(nr_bytes, &pi->in_flight);

	return 0;
}

static struct path_selector_type st_ps = {
	.name		= "service-time",
	.module		= THIS_MODULE,
	.table_args	= 1,
	.info_args	= 2,
	.create		= st_create,
	.destroy	= st_destroy,
	.status		= st_status,
	.add_path	= st_add_path,
	.fail_path	= st_fail_path,
	.reinstate_path	= st_reinstate_path,
	.select_path	= st_select_path,
	.start_io	= st_start_io,
	.end_io		= st_end_io,
};

static int __init dm_st_init(void)
{
	int r = dm_register_path_selector(&st_ps);

	if (r < 0)
		DMERR("register failed %d", r);

	DMINFO("version " ST_VERSION " loaded");

	return r;
}

static void __exit dm_st_exit(void)
{
	int r = dm_unregister_path_selector(&st_ps);

	if (r < 0)
		DMERR("unregister failed %d", r);
}

module_init(dm_st_init);
module_exit(dm_st_exit);

MODULE_DESCRIPTION(DM_NAME " throughput oriented path selector");
MODULE_AUTHOR("Kiyoshi Ueda <k-ueda@ct.jp.nec.com>");
MODULE_LICENSE("GPL");

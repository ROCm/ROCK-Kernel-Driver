/*
 * Copyright (C) 2003 Sistina Software Limited.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include "dm-path-selector.h"
#include "dm-bio-list.h"
#include "dm-bio-record.h"

#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/workqueue.h>
#include <asm/atomic.h>

/* FIXME: get rid of this */
#define MPATH_FAIL_COUNT	1

/*
 * We don't want to call the path selector for every single io
 * that comes through, so instead we only consider changing paths
 * every MPATH_MIN_IO ios.  This number should be selected to be
 * big enough that we can reduce the overhead of the path
 * selector, but also small enough that we don't take the policy
 * decision away from the path selector.
 *
 * So people should _not_ be tuning this number to try and get
 * the most performance from some particular type of hardware.
 * All the smarts should be going into the path selector.
 */
#define MPATH_MIN_IO		1000

/* Path properties */
struct path {
	struct list_head list;

	struct dm_dev *dev;
	struct priority_group *pg;

	spinlock_t failed_lock;
	int has_failed;
	unsigned fail_count;
};

struct priority_group {
	struct list_head list;

	struct multipath *m;
	struct path_selector *ps;

	unsigned nr_paths;
	struct list_head paths;
};

/* Multipath context */
struct multipath {
	struct list_head list;
	struct dm_target *ti;

	unsigned nr_priority_groups;
	struct list_head priority_groups;

	spinlock_t lock;
	unsigned nr_valid_paths;

	struct path *current_path;
	unsigned current_count;

	struct work_struct dispatch_failed;
	struct bio_list failed_ios;

	struct work_struct trigger_event;

	/*
	 * We must use a mempool of mpath_io structs so that we
	 * can resubmit bios on error.
	 */
	mempool_t *details_pool;
};

struct mpath_io {
	struct path *path;
	struct dm_bio_details details;
};

#define MIN_IOS 256
static kmem_cache_t *_details_cache;

static void dispatch_failed_ios(void *data);
static void trigger_event(void *data);

static struct path *alloc_path(void)
{
	struct path *path = kmalloc(sizeof(*path), GFP_KERNEL);

	if (path) {
		memset(path, 0, sizeof(*path));
		path->failed_lock = SPIN_LOCK_UNLOCKED;
		path->fail_count = MPATH_FAIL_COUNT;
	}

	return path;
}

static inline void free_path(struct path *p)
{
	kfree(p);
}

static struct priority_group *alloc_priority_group(void)
{
	struct priority_group *pg;

	pg = kmalloc(sizeof(*pg), GFP_KERNEL);
	if (!pg)
		return NULL;

	pg->ps = kmalloc(sizeof(*pg->ps), GFP_KERNEL);
	if (!pg->ps) {
		kfree(pg);
		return NULL;
	}
	memset(pg->ps, 0, sizeof(*pg->ps));

	INIT_LIST_HEAD(&pg->paths);

	return pg;
}

static void free_paths(struct list_head *paths, struct dm_target *ti)
{
	struct path *path, *tmp;

	list_for_each_entry_safe (path, tmp, paths, list) {
		list_del(&path->list);
		dm_put_device(ti, path->dev);
		free_path(path);
	}
}

static void free_priority_group(struct priority_group *pg,
				struct dm_target *ti)
{
	struct path_selector *ps = pg->ps;

	if (ps) {
		if (ps->type) {
			ps->type->dtr(ps);
			dm_put_path_selector(ps->type);
		}
		kfree(ps);
	}

	free_paths(&pg->paths, ti);
	kfree(pg);
}

static struct multipath *alloc_multipath(void)
{
	struct multipath *m;

	m = kmalloc(sizeof(*m), GFP_KERNEL);
	if (m) {
		memset(m, 0, sizeof(*m));
		INIT_LIST_HEAD(&m->priority_groups);
		m->lock = SPIN_LOCK_UNLOCKED;
		INIT_WORK(&m->dispatch_failed, dispatch_failed_ios, m);
		INIT_WORK(&m->trigger_event, trigger_event, m);
		m->details_pool = mempool_create(MIN_IOS, mempool_alloc_slab,
						 mempool_free_slab, _details_cache);
		if (!m->details_pool) {
			kfree(m);
			return NULL;
		}
	}

	return m;
}

static void free_multipath(struct multipath *m)
{
	struct priority_group *pg, *tmp;

	list_for_each_entry_safe (pg, tmp, &m->priority_groups, list) {
		list_del(&pg->list);
		free_priority_group(pg, m->ti);
	}

	mempool_destroy(m->details_pool);
	kfree(m);
}

/*-----------------------------------------------------------------
 * The multipath daemon is responsible for resubmitting failed ios.
 *---------------------------------------------------------------*/
static int __choose_path(struct multipath *m)
{
	struct priority_group *pg;
	struct path *path = NULL;

	if (m->nr_valid_paths) {
		/* loop through the priority groups until we find a valid path. */
		list_for_each_entry (pg, &m->priority_groups, list) {
			path = pg->ps->type->select_path(pg->ps);
			if (path)
				break;
		}
	}

	m->current_path = path;
	m->current_count = MPATH_MIN_IO;

	return 0;
}

static struct path *get_current_path(struct multipath *m)
{
	struct path *path;
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);

	/* Do we need to select a new path? */
	if (!m->current_path || --m->current_count == 0)
		__choose_path(m);

	path = m->current_path;

	spin_unlock_irqrestore(&m->lock, flags);

	return path;
}

static int map_io(struct multipath *m, struct bio *bio, struct path **chosen)
{
	*chosen = get_current_path(m);
	if (!*chosen)
		return -EIO;

	bio->bi_bdev = (*chosen)->dev->bdev;
	return 0;
}

static void dispatch_failed_ios(void *data)
{
	struct multipath *m = (struct multipath *) data;

	unsigned long flags;
	struct bio *bio = NULL, *next;

	spin_lock_irqsave(&m->lock, flags);
	bio = bio_list_get(&m->failed_ios);
	spin_unlock_irqrestore(&m->lock, flags);

	while (bio) {
		next = bio->bi_next;
		bio->bi_next = NULL;
		generic_make_request(bio);
		bio = next;
	}
}

static void trigger_event(void *data)
{
	struct multipath *m = (struct multipath *) data;
	dm_table_event(m->ti->table);
}

/*-----------------------------------------------------------------
 * Constructor/argument parsing:
 * <num priority groups> [<selector>
 * <num paths> <num selector args> [<path> [<arg>]* ]+ ]+
 *---------------------------------------------------------------*/
struct param {
	unsigned min;
	unsigned max;
	char *error;
};

#define ESTR(s) ("dm-multipath: " s)

static int read_param(struct param *param, char *str, unsigned *v, char **error)
{
	if (!str ||
	    (sscanf(str, "%u", v) != 1) ||
	    (*v < param->min) ||
	    (*v > param->max)) {
		*error = param->error;
		return -EINVAL;
	}

	return 0;
}

struct arg_set {
	unsigned argc;
	char **argv;
};

static char *shift(struct arg_set *as)
{
	char *r;

	if (as->argc) {
		as->argc--;
		r = *as->argv;
		as->argv++;
		return r;
	}

	return NULL;
}

static void consume(struct arg_set *as, unsigned n)
{
	BUG_ON (as->argc < n);
	as->argc -= n;
	as->argv += n;
}

static struct path *parse_path(struct arg_set *as, struct path_selector *ps,
			       struct dm_target *ti)
{
	int r;
	struct path *p;

	/* we need at least a path arg */
	if (as->argc < 1) {
		ti->error = ESTR("no device given");
		return NULL;
	}

	p = alloc_path();
	if (!p)
		return NULL;

	r = dm_get_device(ti, shift(as), ti->begin, ti->len,
			  dm_table_get_mode(ti->table), &p->dev);
	if (r) {
		ti->error = ESTR("error getting device");
		goto bad;
	}

	r = ps->type->add_path(ps, p, as->argc, as->argv, &ti->error);
	if (r) {
		dm_put_device(ti, p->dev);
		goto bad;
	}

	return p;

 bad:
	free_path(p);
	return NULL;
}

static struct priority_group *parse_priority_group(struct arg_set *as,
						   struct multipath *m,
						   struct dm_target *ti)
{
	static struct param _params[] = {
		{1, 1024, ESTR("invalid number of paths")},
		{0, 1024, ESTR("invalid number of selector args")}
	};

	int r;
	unsigned i, nr_selector_args, nr_params;
	struct priority_group *pg;
	struct path_selector_type *pst;

	if (as->argc < 2) {
		as->argc = 0;
		ti->error = ESTR("not enough priority group aruments");
		return NULL;
	}

	pg = alloc_priority_group();
	if (!pg) {
		ti->error = ESTR("couldn't allocate priority group");
		return NULL;
	}
	pg->m = m;

	pst = dm_get_path_selector(shift(as));
	if (!pst) {
		ti->error = ESTR("unknown path selector type");
		goto bad;
	}

	r = pst->ctr(pg->ps);
	if (r) {
		/* FIXME: need to put the pst ? fix after
		 * factoring out the register */
		goto bad;
	}
	pg->ps->type = pst;

	/*
	 * read the paths
	 */
	r = read_param(_params, shift(as), &pg->nr_paths, &ti->error);
	if (r)
		goto bad;

	r = read_param(_params + 1, shift(as), &nr_selector_args, &ti->error);
	if (r)
		goto bad;

	nr_params = 1 + nr_selector_args;
	for (i = 0; i < pg->nr_paths; i++) {
		struct path *path;
		struct arg_set path_args;

		if (as->argc < nr_params)
			goto bad;

		path_args.argc = nr_params;
		path_args.argv = as->argv;

		path = parse_path(&path_args, pg->ps, ti);
		if (!path)
			goto bad;

		path->pg = pg;
		list_add_tail(&path->list, &pg->paths);
		consume(as, nr_params);
	}

	return pg;

 bad:
	free_priority_group(pg, ti);
	return NULL;
}

static int multipath_ctr(struct dm_target *ti, unsigned int argc,
			 char **argv)
{
	/* target parameters */
	static struct param _params[] = {
		{1, 1024, ESTR("invalid number of priority groups")},
	};

	int r;
	struct multipath *m;
	struct arg_set as;

	as.argc = argc;
	as.argv = argv;

	m = alloc_multipath();
	if (!m) {
		ti->error = ESTR("can't allocate multipath");
		return -EINVAL;
	}

	r = read_param(_params, shift(&as), &m->nr_priority_groups, &ti->error);
	if (r)
		goto bad;

	/* parse the priority groups */
	while (as.argc) {
		struct priority_group *pg;
		pg = parse_priority_group(&as, m, ti);
		if (!pg)
			goto bad;

		m->nr_valid_paths += pg->nr_paths;
		list_add_tail(&pg->list, &m->priority_groups);
	}

	ti->private = m;
	m->ti = ti;

	return 0;

 bad:
	free_multipath(m);
	return -EINVAL;
}

static void multipath_dtr(struct dm_target *ti)
{
	struct multipath *m = (struct multipath *) ti->private;
	free_multipath(m);
}

static int multipath_map(struct dm_target *ti, struct bio *bio,
			 union map_info *map_context)
{
	int r;
	struct mpath_io *io;
	struct multipath *m = (struct multipath *) ti->private;

	io = mempool_alloc(m->details_pool, GFP_NOIO);
	dm_bio_record(&io->details, bio);

	bio->bi_rw |= (1 << BIO_RW_FAILFAST);
	r = map_io(m, bio, &io->path);
	if (r) {
		mempool_free(io, m->details_pool);
		return r;
	}

	map_context->ptr = io;
	return 1;
}

static void fail_path(struct path *path)
{
	unsigned long flags;
	struct multipath *m;

	spin_lock_irqsave(&path->failed_lock, flags);

	/* FIXME: path->fail_count is brain dead */
	if (!path->has_failed && !--path->fail_count) {
		m = path->pg->m;

		path->has_failed = 1;
		path->pg->ps->type->fail_path(path->pg->ps, path);
		schedule_work(&m->trigger_event);

		spin_lock(&m->lock);
		m->nr_valid_paths--;

		if (path == m->current_path)
			m->current_path = NULL;

		spin_unlock(&m->lock);
	}

	spin_unlock_irqrestore(&path->failed_lock, flags);
}

static int do_end_io(struct multipath *m, struct bio *bio,
		     int error, struct mpath_io *io)
{
	int r;

	if (error) {
		spin_lock(&m->lock);
		if (!m->nr_valid_paths) {
			spin_unlock(&m->lock);
			return -EIO;
		}
		spin_unlock(&m->lock);

		fail_path(io->path);

		/* remap */
		dm_bio_restore(&io->details, bio);
		r = map_io(m, bio, &io->path);
		if (r)
			/* no paths left */
			return -EIO;

		/* queue for the daemon to resubmit */
		spin_lock(&m->lock);
		bio_list_add(&m->failed_ios, bio);
		spin_unlock(&m->lock);

		schedule_work(&m->dispatch_failed);
		return 1;	/* io not complete */
	}

	return 0;
}

static int multipath_end_io(struct dm_target *ti, struct bio *bio,
			    int error, union map_info *map_context)
{
	struct multipath *m = (struct multipath *) ti->private;
	struct mpath_io *io = (struct mpath_io *) map_context->ptr;
	int r;

	r  = do_end_io(m, bio, error, io);
	if (r <= 0)
		mempool_free(io, m->details_pool);

	return r;
}

/*
 * Info string has the following format:
 * num_groups [num_paths num_selector_args [path_dev A|F fail_count [selector_args]* ]+ ]+
 *
 * Table string has the following format (identical to the constructor string):
 * num_groups [priority selector-name num_paths num_selector_args [path_dev [selector_args]* ]+ ]+
 */
static int multipath_status(struct dm_target *ti, status_type_t type,
			    char *result, unsigned int maxlen)
{
	int sz = 0;
	unsigned long flags;
	struct multipath *m = (struct multipath *) ti->private;
	struct priority_group *pg;
	struct path *p;
	char buffer[32];

#define EMIT(x...) sz += ((sz >= maxlen) ? \
			  0 : scnprintf(result + sz, maxlen - sz, x))

	switch (type) {
	case STATUSTYPE_INFO:
		EMIT("%u ", m->nr_priority_groups);

		list_for_each_entry(pg, &m->priority_groups, list) {
			EMIT("%u %u ", pg->nr_paths, pg->ps->type->info_args);

			list_for_each_entry(p, &pg->paths, list) {
				format_dev_t(buffer, p->dev->bdev->bd_dev);
				spin_lock_irqsave(&p->failed_lock, flags);
				EMIT("%s %s %u ", buffer,
				     p->has_failed ? "F" : "A", p->fail_count);
				pg->ps->type->status(pg->ps, p, type,
						     result + sz, maxlen - sz);
				spin_unlock_irqrestore(&p->failed_lock, flags);
			}
		}
		break;

	case STATUSTYPE_TABLE:
		EMIT("%u ", m->nr_priority_groups);

		list_for_each_entry(pg, &m->priority_groups, list) {
			EMIT("%s %u %u ", pg->ps->type->name,
			     pg->nr_paths, pg->ps->type->table_args);

			list_for_each_entry(p, &pg->paths, list) {
				format_dev_t(buffer, p->dev->bdev->bd_dev);
				EMIT("%s ", buffer);
				pg->ps->type->status(pg->ps, p, type,
						     result + sz, maxlen - sz);

			}
		}
		break;
	}

	return 0;
}

/*-----------------------------------------------------------------
 * Module setup
 *---------------------------------------------------------------*/
static struct target_type multipath_target = {
	.name = "multipath",
	.version = {1, 0, 2},
	.module = THIS_MODULE,
	.ctr = multipath_ctr,
	.dtr = multipath_dtr,
	.map = multipath_map,
	.end_io = multipath_end_io,
	.status = multipath_status,
};

int __init dm_multipath_init(void)
{
	int r;

	/* allocate a slab for the dm_ios */
	_details_cache = kmem_cache_create("dm_mpath", sizeof(struct mpath_io),
					   0, 0, NULL, NULL);
	if (!_details_cache)
		return -ENOMEM;

	r = dm_register_target(&multipath_target);
	if (r < 0) {
		DMERR("%s: register failed %d", multipath_target.name, r);
		kmem_cache_destroy(_details_cache);
		return -EINVAL;
	}

	r = dm_register_path_selectors();
	if (r && r != -EEXIST) {
		dm_unregister_target(&multipath_target);
		kmem_cache_destroy(_details_cache);
		return r;
	}

	DMINFO("dm_multipath v0.2.0");
	return r;
}

void __exit dm_multipath_exit(void)
{
	int r;

	dm_unregister_path_selectors();
	r = dm_unregister_target(&multipath_target);
	if (r < 0)
		DMERR("%s: target unregister failed %d",
		      multipath_target.name, r);
	kmem_cache_destroy(_details_cache);
}

module_init(dm_multipath_init);
module_exit(dm_multipath_exit);

MODULE_DESCRIPTION(DM_NAME " multipath target");
MODULE_AUTHOR("Sistina software <dm@uk.sistina.com>");
MODULE_LICENSE("GPL");

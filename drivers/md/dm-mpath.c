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
#include <linux/completion.h>
#include <asm/atomic.h>

/* Path properties */
struct path {
	struct list_head list;

	struct dm_dev *dev;
	struct priority_group *pg;
};

inline struct block_device *dm_path_to_bdev(struct path *path)
{
	return path->dev->bdev;
}

struct priority_group {
	struct list_head list;

	struct multipath *m;
	struct path_selector ps;

	unsigned nr_paths;
	struct list_head paths;
};

#define ps_to_pg(__ps) container_of((__ps), struct priority_group, ps)

/* Multipath context */
struct multipath {
	struct list_head list;
	struct dm_target *ti;

	spinlock_t lock;

	unsigned nr_priority_groups;
	struct list_head priority_groups;
	int initializing_pg;
	struct completion init_pg_wait;
	struct priority_group *current_pg;

	struct work_struct dispatch_failed;
	struct bio_list failed_ios;

	struct work_struct trigger_event;

	/*
	 * We must use a mempool of mp_io structs so that we
	 * can resubmit bios on error.
	 */
	mempool_t *mpio_pool;
};

struct mpath_io {
	struct path *path;
	union map_info info;
	struct dm_bio_details details;
};

#define MIN_IOS 256
static kmem_cache_t *_mpio_cache;

static void dispatch_failed_ios(void *data);
static void trigger_event(void *data);

static struct path *alloc_path(void)
{
	struct path *path = kmalloc(sizeof(*path), GFP_KERNEL);

	if (path)
		memset(path, 0, sizeof(*path));
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

	memset(pg, 0, sizeof(*pg));
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
	struct path_selector *ps = &pg->ps;

	if (ps->type) {
		ps->type->dtr(ps);
		dm_put_path_selector(ps->type);
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
		init_completion(&m->init_pg_wait);
		m->initializing_pg = 0;
		m->lock = SPIN_LOCK_UNLOCKED;
		INIT_WORK(&m->dispatch_failed, dispatch_failed_ios, m);
		INIT_WORK(&m->trigger_event, trigger_event, m);
		m->mpio_pool = mempool_create(MIN_IOS, mempool_alloc_slab,
					      mempool_free_slab, _mpio_cache);
		if (!m->mpio_pool) {
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

	mempool_destroy(m->mpio_pool);
	kfree(m);
}

static void __ps_init_complete(struct multipath *m,
			       struct priority_group *pg)
{
	m->initializing_pg = 0;
	m->current_pg = pg;
	complete_all(&m->init_pg_wait);
	schedule_work(&m->dispatch_failed);
}

void dm_ps_init_complete(struct path_selector *ps)
{
	unsigned long flags;
	struct priority_group *pg = ps_to_pg(ps);
	struct multipath *m = pg->m;

	spin_lock_irqsave(&m->lock, flags);
	__ps_init_complete(m, pg);
	spin_unlock_irqrestore(&m->lock, flags);
}

EXPORT_SYMBOL(dm_ps_init_complete);

static int select_group(struct multipath *m, struct mpath_io *mpio,
			struct bio *bio)
{
	struct priority_group *pg = NULL;
	int err;

	m->current_pg = NULL;
	m->initializing_pg = 1;
	init_completion(&m->init_pg_wait);

	list_for_each_entry (pg, &m->priority_groups, list) {

		if (pg->ps.type->init) {
			spin_unlock_irq(&m->lock);
			err = pg->ps.type->init(&pg->ps);
			spin_lock_irq(&m->lock);

			if (err == DM_PS_INITIALIZING)
				return DM_PS_INITIALIZING;
			else if (err == DM_PS_FAILED)
				continue;
		}

		mpio->path = pg->ps.type->select_path(&pg->ps, bio,
						      &mpio->info);
		if (mpio->path)
			break;
	}

	__ps_init_complete(m, mpio->path ? pg : NULL);
	return mpio->path ? DM_PS_SUCCESS : DM_PS_FAILED;
}
  
static int select_path1(struct multipath *m, struct mpath_io *mpio,
		       struct bio *bio, int wait)
{
	mpio->path = NULL;

 retest:
	/*
	 * completion event, current_pg, initializing_pg and
	 * in the case of wait=0 adding to the failed_ios list for
	 * resubmission are protected under the m->lock to avoid races.
	 */
	if (unlikely(m->initializing_pg)) {
		if (!wait)
			return -EWOULDBLOCK;

		spin_unlock_irq(&m->lock);
		wait_for_completion(&m->init_pg_wait);
		spin_lock_irq(&m->lock);
		goto retest;
	}

	if (m->current_pg) {
		struct path_selector *ps = &m->current_pg->ps;

		mpio->path = ps->type->select_path(ps, bio, &mpio->info);
		if (!mpio->path &&
		    (select_group(m, mpio, bio) == DM_PS_INITIALIZING))
			/*
			 * while the lock was dropped the
			 * initialization might have completed.
			 */
			goto retest;
	}

	return mpio->path ? 0 : -EIO;
}

static int map_io(struct multipath *m, struct mpath_io *mpio,
		  struct bio *bio, int wait)
{
	int err;

	spin_lock_irq(&m->lock);
	err = select_path1(m, mpio, bio, wait);
	if (err == -EWOULDBLOCK)
		/*
		 * when the ps init is completed it will
		 * remap and submit this bio
		 */
		bio_list_add(&m->failed_ios, bio);
	spin_unlock_irq(&m->lock);

	if (err)
		return err;

	bio->bi_bdev = mpio->path->dev->bdev;
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
		int err;
		struct mpath_io *mpio;
		union map_info *info;

		next = bio->bi_next;
		bio->bi_next = NULL;

		info = dm_get_mapinfo(bio);
		mpio = info->ptr;

		/*
		 * For -EWOULDBLOCK the bio could not be mapped
		 * due to a ps initialization. The bio has been
		 * requeued, and the work will be processed when
		 * the initialization is completed.
		 */
		err = map_io(m, mpio, bio, 0);
		if (!err)
			generic_make_request(bio);
		else if (err != -EWOULDBLOCK)
			/* no paths left */
			bio_endio(bio, bio->bi_size, -EIO);

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

	r = pst->ctr(&pg->ps);
	if (r) {
		dm_put_path_selector(pst);
		goto bad;
	}
	pg->ps.type = pst;

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

		path = parse_path(&path_args, &pg->ps, ti);
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

		list_add_tail(&pg->list, &m->priority_groups);
	}
	m->current_pg = list_entry(m->priority_groups.next,
				   struct priority_group, list);
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
			 union map_info *info)
{
	int r;
	struct mpath_io *mpio;
	struct multipath *m = (struct multipath *) ti->private;

	mpio = mempool_alloc(m->mpio_pool, GFP_NOIO);
	dm_bio_record(&mpio->details, bio);

	bio->bi_rw |= (1 << BIO_RW_FAILFAST);
	r = map_io(m, mpio, bio, 1);
	if (unlikely(r)) {
		mempool_free(mpio, m->mpio_pool);
		return r;
	}

	info->ptr = mpio;
	return 1;
}

static int do_end_io(struct multipath *m, struct bio *bio,
		     int error, struct mpath_io *mpio)
{
	struct path_selector *ps = &mpio->path->pg->ps;

	ps->type->end_io(ps, bio, error, &mpio->info);
	if (error) {

		dm_bio_restore(&mpio->details, bio);

		/* queue for the daemon to resubmit or fail */
		spin_lock(&m->lock);
		bio_list_add(&m->failed_ios, bio);
		/*
		 * If a ps is initializing we do not queue the work
		 * becuase when the ps initialization has completed
		 * it will queue the dispatch function to be run.
		 */
		if (!m->initializing_pg)
			schedule_work(&m->dispatch_failed);
		spin_unlock(&m->lock);

		return 1;	/* io not complete */
	}

	return 0;
}

static int multipath_end_io(struct dm_target *ti, struct bio *bio,
			    int error, union map_info *info)
{
	struct multipath *m = (struct multipath *) ti->private;
	struct mpath_io *io = (struct mpath_io *) info->ptr;
	int r;

	/*
	 * If we report to dm that we are going to retry the
	 * bio, but that fails due to a pst->init failure
	 * calling bio_endio from dm-mpath.c will end up
	 * calling dm-mpath's endio fn, so this test catches
	 * that case.
	 */
	if (io->path)
		r = do_end_io(m, bio, error, io);
	else
		r = -EIO;

	if (r <= 0)
		mempool_free(io, m->mpio_pool);

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
			EMIT("%u %u ", pg->nr_paths, pg->ps.type->info_args);

			list_for_each_entry(p, &pg->paths, list) {
				format_dev_t(buffer, p->dev->bdev->bd_dev);
				EMIT("%s ", buffer);
				sz += pg->ps.type->status(&pg->ps, p, type,
						     result + sz, maxlen - sz);
			}
		}
		break;

	case STATUSTYPE_TABLE:
		EMIT("%u ", m->nr_priority_groups);

		list_for_each_entry(pg, &m->priority_groups, list) {
			EMIT("%s %u %u ", pg->ps.type->name,
			     pg->nr_paths, pg->ps.type->table_args);

			list_for_each_entry(p, &pg->paths, list) {
				format_dev_t(buffer, p->dev->bdev->bd_dev);
				EMIT("%s ", buffer);
				sz += pg->ps.type->status(&pg->ps, p, type,
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

static int __init dm_multipath_init(void)
{
	int r;

	/* allocate a slab for the dm_ios */
	_mpio_cache = kmem_cache_create("dm_mpath", sizeof(struct mpath_io),
					0, 0, NULL, NULL);
	if (!_mpio_cache)
		return -ENOMEM;

	r = dm_register_target(&multipath_target);
	if (r < 0) {
		DMERR("%s: register failed %d", multipath_target.name, r);
		kmem_cache_destroy(_mpio_cache);
		return -EINVAL;
	}

	r = dm_register_path_selectors();
	if (r && r != -EEXIST) {
		dm_unregister_target(&multipath_target);
		kmem_cache_destroy(_mpio_cache);
		return r;
	}

	DMINFO("dm_multipath v0.2.0");
	return r;
}

static void __exit dm_multipath_exit(void)
{
	int r;

	dm_unregister_path_selectors();
	r = dm_unregister_target(&multipath_target);
	if (r < 0)
		DMERR("%s: target unregister failed %d",
		      multipath_target.name, r);
	kmem_cache_destroy(_mpio_cache);
}

module_init(dm_multipath_init);
module_exit(dm_multipath_exit);

MODULE_DESCRIPTION(DM_NAME " multipath target");
MODULE_AUTHOR("Sistina software <dm@uk.sistina.com>");
MODULE_LICENSE("GPL");

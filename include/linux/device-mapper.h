/*
 * Copyright (C) 2001 Sistina Software (UK) Limited.
 *
 * This file is released under the LGPL.
 */

#ifndef _LINUX_DEVICE_MAPPER_H
#define _LINUX_DEVICE_MAPPER_H

struct dm_target;
struct dm_table;
struct dm_dev;

typedef enum { STATUSTYPE_INFO, STATUSTYPE_TABLE } status_type_t;

/*
 * In the constructor the target parameter will already have the
 * table, type, begin and len fields filled in.
 */
typedef int (*dm_ctr_fn) (struct dm_target *target,
			  unsigned int argc, char **argv);

/*
 * The destructor doesn't need to free the dm_target, just
 * anything hidden ti->private.
 */
typedef void (*dm_dtr_fn) (struct dm_target *ti);

/*
 * The map function must return:
 * < 0: error
 * = 0: The target will handle the io by resubmitting it later
 * > 0: simple remap complete
 */
typedef int (*dm_map_fn) (struct dm_target *ti, struct bio *bio);

typedef void (*dm_suspend_fn) (struct dm_target *ti);
typedef void (*dm_resume_fn) (struct dm_target *ti);

typedef int (*dm_status_fn) (struct dm_target *ti, status_type_t status_type,
			     char *result, unsigned int maxlen);

void dm_error(const char *message);

/*
 * Constructors should call these functions to ensure destination devices
 * are opened/closed correctly.
 * FIXME: too many arguments.
 */
int dm_get_device(struct dm_target *ti, const char *path, sector_t start,
		  sector_t len, int mode, struct dm_dev **result);
void dm_put_device(struct dm_target *ti, struct dm_dev *d);

/*
 * Information about a target type
 */
struct target_type {
	const char *name;
	struct module *module;
	dm_ctr_fn ctr;
	dm_dtr_fn dtr;
	dm_map_fn map;
	dm_suspend_fn suspend;
	dm_resume_fn resume;
	dm_status_fn status;
};

struct io_restrictions {
	unsigned short		max_sectors;
	unsigned short		max_phys_segments;
	unsigned short		max_hw_segments;
	unsigned short		hardsect_size;
	unsigned int		max_segment_size;
	unsigned long		seg_boundary_mask;
};

struct dm_target {
	struct dm_table *table;
	struct target_type *type;

	/* target limits */
	sector_t begin;
	sector_t len;

	/* FIXME: turn this into a mask, and merge with io_restrictions */
	sector_t split_io;

	/*
	 * These are automaticall filled in by
	 * dm_table_get_device.
	 */
	struct io_restrictions limits;

	/* target specific data */
	void *private;

	/* Used to provide an error string from the ctr */
	char *error;
};

int dm_register_target(struct target_type *t);
int dm_unregister_target(struct target_type *t);

#endif				/* _LINUX_DEVICE_MAPPER_H */

/*
 * Copyright (C) 2001, 2002 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#include "dm.h"

#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <linux/dm-ioctl.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/devfs_fs_kernel.h>

#include <asm/uaccess.h>

#define DM_DRIVER_EMAIL "dm@uk.sistina.com"

/*-----------------------------------------------------------------
 * The ioctl interface needs to be able to look up devices by
 * name or uuid.
 *---------------------------------------------------------------*/
struct hash_cell {
	struct list_head name_list;
	struct list_head uuid_list;

	char *name;
	char *uuid;
	struct mapped_device *md;
};

#define NUM_BUCKETS 64
#define MASK_BUCKETS (NUM_BUCKETS - 1)
static struct list_head _name_buckets[NUM_BUCKETS];
static struct list_head _uuid_buckets[NUM_BUCKETS];

void dm_hash_remove_all(void);

/*
 * Guards access to all three tables.
 */
static DECLARE_RWSEM(_hash_lock);

static void init_buckets(struct list_head *buckets)
{
	unsigned int i;

	for (i = 0; i < NUM_BUCKETS; i++)
		INIT_LIST_HEAD(buckets + i);
}

int dm_hash_init(void)
{
	init_buckets(_name_buckets);
	init_buckets(_uuid_buckets);
	devfs_mk_dir(DM_DIR);
	return 0;
}

void dm_hash_exit(void)
{
	dm_hash_remove_all();
	devfs_remove(DM_DIR);
}

/*-----------------------------------------------------------------
 * Hash function:
 * We're not really concerned with the str hash function being
 * fast since it's only used by the ioctl interface.
 *---------------------------------------------------------------*/
static unsigned int hash_str(const char *str)
{
	const unsigned int hash_mult = 2654435387U;
	unsigned int h = 0;

	while (*str)
		h = (h + (unsigned int) *str++) * hash_mult;

	return h & MASK_BUCKETS;
}

/*-----------------------------------------------------------------
 * Code for looking up a device by name
 *---------------------------------------------------------------*/
static struct hash_cell *__get_name_cell(const char *str)
{
	struct list_head *tmp;
	struct hash_cell *hc;
	unsigned int h = hash_str(str);

	list_for_each (tmp, _name_buckets + h) {
		hc = list_entry(tmp, struct hash_cell, name_list);
		if (!strcmp(hc->name, str))
			return hc;
	}

	return NULL;
}

static struct hash_cell *__get_uuid_cell(const char *str)
{
	struct list_head *tmp;
	struct hash_cell *hc;
	unsigned int h = hash_str(str);

	list_for_each (tmp, _uuid_buckets + h) {
		hc = list_entry(tmp, struct hash_cell, uuid_list);
		if (!strcmp(hc->uuid, str))
			return hc;
	}

	return NULL;
}

/*-----------------------------------------------------------------
 * Inserting, removing and renaming a device.
 *---------------------------------------------------------------*/
static inline char *kstrdup(const char *str)
{
	char *r = kmalloc(strlen(str) + 1, GFP_KERNEL);
	if (r)
		strcpy(r, str);
	return r;
}

static struct hash_cell *alloc_cell(const char *name, const char *uuid,
				    struct mapped_device *md)
{
	struct hash_cell *hc;

	hc = kmalloc(sizeof(*hc), GFP_KERNEL);
	if (!hc)
		return NULL;

	hc->name = kstrdup(name);
	if (!hc->name) {
		kfree(hc);
		return NULL;
	}

	if (!uuid)
		hc->uuid = NULL;

	else {
		hc->uuid = kstrdup(uuid);
		if (!hc->uuid) {
			kfree(hc->name);
			kfree(hc);
			return NULL;
		}
	}

	INIT_LIST_HEAD(&hc->name_list);
	INIT_LIST_HEAD(&hc->uuid_list);
	hc->md = md;
	return hc;
}

static void free_cell(struct hash_cell *hc)
{
	if (hc) {
		kfree(hc->name);
		kfree(hc->uuid);
		kfree(hc);
	}
}

/*
 * devfs stuff.
 */
static int register_with_devfs(struct hash_cell *hc)
{
	struct gendisk *disk = dm_disk(hc->md);

	devfs_mk_bdev(MKDEV(disk->major, disk->first_minor),
		       S_IFBLK | S_IRUSR | S_IWUSR | S_IRGRP,
		       DM_DIR "/%s", hc->name);
	return 0;
}

static int unregister_with_devfs(struct hash_cell *hc)
{
	devfs_remove(DM_DIR"/%s", hc->name);
	return 0;
}

/*
 * The kdev_t and uuid of a device can never change once it is
 * initially inserted.
 */
int dm_hash_insert(const char *name, const char *uuid, struct mapped_device *md)
{
	struct hash_cell *cell;

	/*
	 * Allocate the new cells.
	 */
	cell = alloc_cell(name, uuid, md);
	if (!cell)
		return -ENOMEM;

	/*
	 * Insert the cell into all three hash tables.
	 */
	down_write(&_hash_lock);
	if (__get_name_cell(name))
		goto bad;

	list_add(&cell->name_list, _name_buckets + hash_str(name));

	if (uuid) {
		if (__get_uuid_cell(uuid)) {
			list_del(&cell->name_list);
			goto bad;
		}
		list_add(&cell->uuid_list, _uuid_buckets + hash_str(uuid));
	}
	register_with_devfs(cell);
	dm_get(md);
	up_write(&_hash_lock);

	return 0;

 bad:
	up_write(&_hash_lock);
	free_cell(cell);
	return -EBUSY;
}

void __hash_remove(struct hash_cell *hc)
{
	/* remove from the dev hash */
	list_del(&hc->uuid_list);
	list_del(&hc->name_list);
	unregister_with_devfs(hc);
	dm_put(hc->md);
	free_cell(hc);
}

void dm_hash_remove_all(void)
{
	int i;
	struct hash_cell *hc;
	struct list_head *tmp, *n;

	down_write(&_hash_lock);
	for (i = 0; i < NUM_BUCKETS; i++) {
		list_for_each_safe (tmp, n, _name_buckets + i) {
			hc = list_entry(tmp, struct hash_cell, name_list);
			__hash_remove(hc);
		}
	}
	up_write(&_hash_lock);
}

int dm_hash_rename(const char *old, const char *new)
{
	char *new_name, *old_name;
	struct hash_cell *hc;

	/*
	 * duplicate new.
	 */
	new_name = kstrdup(new);
	if (!new_name)
		return -ENOMEM;

	down_write(&_hash_lock);

	/*
	 * Is new free ?
	 */
	hc = __get_name_cell(new);
	if (hc) {
		DMWARN("asked to rename to an already existing name %s -> %s",
		       old, new);
		up_write(&_hash_lock);
		kfree(new_name);
		return -EBUSY;
	}

	/*
	 * Is there such a device as 'old' ?
	 */
	hc = __get_name_cell(old);
	if (!hc) {
		DMWARN("asked to rename a non existent device %s -> %s",
		       old, new);
		up_write(&_hash_lock);
		kfree(new_name);
		return -ENXIO;
	}

	/*
	 * rename and move the name cell.
	 */
	unregister_with_devfs(hc);

	list_del(&hc->name_list);
	old_name = hc->name;
	hc->name = new_name;
	list_add(&hc->name_list, _name_buckets + hash_str(new_name));

	/* rename the device node in devfs */
	register_with_devfs(hc);

	up_write(&_hash_lock);
	kfree(old_name);
	return 0;
}


/*-----------------------------------------------------------------
 * Implementation of the ioctl commands
 *---------------------------------------------------------------*/

/*
 * All the ioctl commands get dispatched to functions with this
 * prototype.
 */
typedef int (*ioctl_fn)(struct dm_ioctl *param, struct dm_ioctl *user);

/*
 * Check a string doesn't overrun the chunk of
 * memory we copied from userland.
 */
static int valid_str(char *str, void *begin, void *end)
{
	while (((void *) str >= begin) && ((void *) str < end))
		if (!*str++)
			return 0;

	return -EINVAL;
}

static int next_target(struct dm_target_spec *last, uint32_t next,
		       void *begin, void *end,
		       struct dm_target_spec **spec, char **params)
{
	*spec = (struct dm_target_spec *)
	    ((unsigned char *) last + next);
	*params = (char *) (*spec + 1);

	if (*spec < (last + 1) || ((void *) *spec > end))
		return -EINVAL;

	return valid_str(*params, begin, end);
}

static int populate_table(struct dm_table *table, struct dm_ioctl *args)
{
	int r, first = 1;
	unsigned int i = 0;
	struct dm_target_spec *spec;
	char *params;
	void *begin, *end;

	if (!args->target_count) {
		DMWARN("populate_table: no targets specified");
		return -EINVAL;
	}

	begin = (void *) args;
	end = begin + args->data_size;

	for (i = 0; i < args->target_count; i++) {

		if (first)
			r = next_target((struct dm_target_spec *) args,
					args->data_start,
					begin, end, &spec, &params);
		else
			r = next_target(spec, spec->next, begin, end,
					&spec, &params);

		if (r) {
			DMWARN("unable to find target");
			return -EINVAL;
		}

		r = dm_table_add_target(table, spec->target_type,
					(sector_t) spec->sector_start,
					(sector_t) spec->length,
					params);
		if (r) {
			DMWARN("internal error adding target to table");
			return -EINVAL;
		}

		first = 0;
	}

	return dm_table_complete(table);
}

/*
 * Round up the ptr to the next 'align' boundary.  Obviously
 * 'align' must be a power of 2.
 */
static inline void *align_ptr(void *ptr, unsigned int align)
{
	align--;
	return (void *) (((unsigned long) (ptr + align)) & ~align);
}

/*
 * Copies a dm_ioctl and an optional additional payload to
 * userland.
 */
static int results_to_user(struct dm_ioctl *user, struct dm_ioctl *param,
			   void *data, uint32_t len)
{
	int r;
	void *ptr = NULL;

	if (data) {
		ptr = align_ptr(user + 1, sizeof(unsigned long));
		param->data_start = ptr - (void *) user;
	}

	/*
	 * The version number has already been filled in, so we
	 * just copy later fields.
	 */
	r = copy_to_user(&user->data_size, &param->data_size,
			 sizeof(*param) - sizeof(param->version));
	if (r)
		return -EFAULT;

	if (data) {
		if (param->data_start + len > param->data_size)
			return -ENOSPC;

		if (copy_to_user(ptr, data, len))
			r = -EFAULT;
	}

	return r;
}

/*
 * Fills in a dm_ioctl structure, ready for sending back to
 * userland.
 */
static int __info(struct mapped_device *md, struct dm_ioctl *param)
{
	struct dm_table *table;
	struct block_device *bdev;
	struct gendisk *disk = dm_disk(md);

	param->flags = DM_EXISTS_FLAG;
	if (dm_suspended(md))
		param->flags |= DM_SUSPEND_FLAG;

	bdev = bdget_disk(disk, 0);
	if (!bdev)
		return -ENXIO;

	param->dev = old_encode_dev(bdev->bd_dev);
	param->open_count = bdev->bd_openers;
	bdput(bdev);

	if (disk->policy)
		param->flags |= DM_READONLY_FLAG;

	table = dm_get_table(md);
	param->target_count = dm_table_get_num_targets(table);
	dm_table_put(table);

	return 0;
}

/*
 * Always use UUID for lookups if it's present, otherwise use name.
 */
static inline struct mapped_device *find_device(struct dm_ioctl *param)
{
	struct hash_cell *hc;
	struct mapped_device *md = NULL;

	down_read(&_hash_lock);
	hc = *param->uuid ? __get_uuid_cell(param->uuid) :
		__get_name_cell(param->name);
	if (hc) {
		md = hc->md;

		/*
		 * Sneakily write in both the name and the uuid
		 * while we have the cell.
		 */
		strlcpy(param->name, hc->name, sizeof(param->name));
		if (hc->uuid)
			strlcpy(param->uuid, hc->uuid, sizeof(param->uuid));
		else
			param->uuid[0] = '\0';

		dm_get(md);
	}
	up_read(&_hash_lock);

	return md;
}

#define ALIGNMENT sizeof(int)
static void *_align(void *ptr, unsigned int a)
{
	register unsigned long align = --a;

	return (void *) (((unsigned long) ptr + align) & ~align);
}

/*
 * Copies device info back to user space, used by
 * the create and info ioctls.
 */
static int info(struct dm_ioctl *param, struct dm_ioctl *user)
{
	struct mapped_device *md;

	param->flags = 0;

	md = find_device(param);
	if (!md)
		/*
		 * Device not found - returns cleared exists flag.
		 */
		goto out;

	__info(md, param);
	dm_put(md);

      out:
	return results_to_user(user, param, NULL, 0);
}

static inline int get_mode(struct dm_ioctl *param)
{
	int mode = FMODE_READ | FMODE_WRITE;

	if (param->flags & DM_READONLY_FLAG)
		mode = FMODE_READ;

	return mode;
}

static int check_name(const char *name)
{
	if (name[0] == '/') {
		DMWARN("invalid device name");
		return -EINVAL;
	}

	return 0;
}

static int create(struct dm_ioctl *param, struct dm_ioctl *user)
{
	int r;
	struct dm_table *t;
	struct mapped_device *md;

	r = check_name(param->name);
	if (r)
		return r;

	r = dm_table_create(&t, get_mode(param));
	if (r)
		return r;

	r = populate_table(t, param);
	if (r) {
		dm_table_put(t);
		return r;
	}

	if (param->flags & DM_PERSISTENT_DEV_FLAG)
		r = dm_create_with_minor(MINOR(old_decode_dev(param->dev)), &md);
	else
		r = dm_create(&md);

	if (r) {
		dm_table_put(t);
		return r;
	}

	/* suspend the device */
	r = dm_suspend(md);
	if (r) {
		DMWARN("suspend failed");
		dm_table_put(t);
		dm_put(md);
		return r;
	}
	/* swap in the table */
	r = dm_swap_table(md, t);
	if (r) {
		DMWARN("table swap failed");
		dm_table_put(t);
		dm_put(md);
		return r;
	}

	/* resume the device */
	r = dm_resume(md);
	if (r) {
		DMWARN("resume failed");
		dm_table_put(t);
		dm_put(md);
		return r;
	}

	dm_table_put(t);	/* md will have grabbed its own reference */

	set_disk_ro(dm_disk(md), (param->flags & DM_READONLY_FLAG) ? 1 : 0);
	r = dm_hash_insert(param->name, *param->uuid ? param->uuid : NULL, md);
	dm_put(md);

	return r ? r : info(param, user);
}

/*
 * Build up the status struct for each target
 */
static int __status(struct mapped_device *md, struct dm_ioctl *param,
		    char *outbuf, size_t *len)
{
	unsigned int i, num_targets;
	struct dm_target_spec *spec;
	char *outptr;
	status_type_t type;
	struct dm_table *table = dm_get_table(md);

	if (param->flags & DM_STATUS_TABLE_FLAG)
		type = STATUSTYPE_TABLE;
	else
		type = STATUSTYPE_INFO;

	outptr = outbuf;

	/* Get all the target info */
	num_targets = dm_table_get_num_targets(table);
	for (i = 0; i < num_targets; i++) {
		struct dm_target *ti = dm_table_get_target(table, i);

		if (outptr - outbuf +
		    sizeof(struct dm_target_spec) > param->data_size) {
			dm_table_put(table);
			return -ENOMEM;
		}

		spec = (struct dm_target_spec *) outptr;

		spec->status = 0;
		spec->sector_start = ti->begin;
		spec->length = ti->len;
		strlcpy(spec->target_type, ti->type->name,
			sizeof(spec->target_type));

		outptr += sizeof(struct dm_target_spec);

		/* Get the status/table string from the target driver */
		if (ti->type->status)
			ti->type->status(ti, type, outptr,
					 outbuf + param->data_size - outptr);
		else
			outptr[0] = '\0';

		outptr += strlen(outptr) + 1;
		_align(outptr, ALIGNMENT);
		spec->next = outptr - outbuf;
	}

	param->target_count = num_targets;
	*len = outptr - outbuf;
	dm_table_put(table);

	return 0;
}

/*
 * Return the status of a device as a text string for each
 * target.
 */
static int get_status(struct dm_ioctl *param, struct dm_ioctl *user)
{
	struct mapped_device *md;
	size_t len = 0;
	int ret;
	char *outbuf = NULL;

	md = find_device(param);
	if (!md)
		/*
		 * Device not found - returns cleared exists flag.
		 */
		goto out;

	/* We haven't a clue how long the resultant data will be so
	   just allocate as much as userland has allowed us and make sure
	   we don't overun it */
	outbuf = kmalloc(param->data_size, GFP_KERNEL);
	if (!outbuf)
		goto out;
	/*
	 * Get the status of all targets
	 */
	__status(md, param, outbuf, &len);

	/*
	 * Setup the basic dm_ioctl structure.
	 */
	__info(md, param);

      out:
	if (md)
		dm_put(md);

	ret = results_to_user(user, param, outbuf, len);

	if (outbuf)
		kfree(outbuf);

	return ret;
}

/*
 * Wait for a device to report an event
 */
static int wait_device_event(struct dm_ioctl *param, struct dm_ioctl *user)
{
	struct mapped_device *md;
	DECLARE_WAITQUEUE(wq, current);

	md = find_device(param);
	if (!md)
		/*
		 * Device not found - returns cleared exists flag.
		 */
		goto out;

	/*
	 * Setup the basic dm_ioctl structure.
	 */
	__info(md, param);

	/*
	 * Wait for a notification event
	 */
	set_current_state(TASK_INTERRUPTIBLE);
 	if (!dm_add_wait_queue(md, &wq, dm_get_event_nr(md))) {
 		schedule();
 		dm_remove_wait_queue(md, &wq);
 	}
  	set_current_state(TASK_RUNNING);
 	dm_put(md);

      out:
	return results_to_user(user, param, NULL, 0);
}

/*
 * Retrieves a list of devices used by a particular dm device.
 */
static int dep(struct dm_ioctl *param, struct dm_ioctl *user)
{
	int r;
	unsigned int count;
	struct mapped_device *md;
	struct list_head *tmp;
	size_t len = 0;
	struct dm_target_deps *deps = NULL;
	struct dm_table *table;

	md = find_device(param);
	if (!md)
		goto out;
	table = dm_get_table(md);

	/*
	 * Setup the basic dm_ioctl structure.
	 */
	__info(md, param);

	/*
	 * Count the devices.
	 */
	count = 0;
	list_for_each(tmp, dm_table_get_devices(table))
	    count++;

	/*
	 * Allocate a kernel space version of the dm_target_status
	 * struct.
	 */
	if (array_too_big(sizeof(*deps), sizeof(*deps->dev), count)) {
		dm_table_put(table);
		dm_put(md);
		return -ENOMEM;
	}

	len = sizeof(*deps) + (sizeof(*deps->dev) * count);
	deps = kmalloc(len, GFP_KERNEL);
	if (!deps) {
		dm_table_put(table);
		dm_put(md);
		return -ENOMEM;
	}

	/*
	 * Fill in the devices.
	 */
	deps->count = count;
	count = 0;
	list_for_each(tmp, dm_table_get_devices(table)) {
		struct dm_dev *dd = list_entry(tmp, struct dm_dev, list);
		deps->dev[count++] = old_encode_dev(dd->bdev->bd_dev);
	}
	dm_table_put(table);
	dm_put(md);

      out:
	r = results_to_user(user, param, deps, len);

	kfree(deps);
	return r;
}

static int remove(struct dm_ioctl *param, struct dm_ioctl *user)
{
	struct hash_cell *hc;

	down_write(&_hash_lock);
	hc = *param->uuid ? __get_uuid_cell(param->uuid) :
		__get_name_cell(param->name);
	if (!hc) {
		DMWARN("device doesn't appear to be in the dev hash table.");
		up_write(&_hash_lock);
		return -EINVAL;
	}

	/*
	 * You may ask the interface to drop its reference to an
	 * in use device.  This is no different to unlinking a
	 * file that someone still has open.  The device will not
	 * actually be destroyed until the last opener closes it.
	 * The name and uuid of the device (both are interface
	 * properties) will be available for reuse immediately.
	 *
	 * You don't want to drop a _suspended_ device from the
	 * interface, since that will leave you with no way of
	 * resuming it.
	 */
	if (dm_suspended(hc->md)) {
		DMWARN("refusing to remove a suspended device.");
		up_write(&_hash_lock);
		return -EPERM;
	}

	__hash_remove(hc);
	up_write(&_hash_lock);
	return 0;
}

static int remove_all(struct dm_ioctl *param, struct dm_ioctl *user)
{
	dm_hash_remove_all();
	return 0;
}

static int suspend(struct dm_ioctl *param, struct dm_ioctl *user)
{
	int r;
	struct mapped_device *md;

	md = find_device(param);
	if (!md)
		return -ENXIO;

	if (param->flags & DM_SUSPEND_FLAG)
		r = dm_suspend(md);
	else
		r = dm_resume(md);

	dm_put(md);
	return r;
}

static int reload(struct dm_ioctl *param, struct dm_ioctl *user)
{
	int r;
	struct mapped_device *md;
	struct dm_table *t;

	r = dm_table_create(&t, get_mode(param));
	if (r)
		return r;

	r = populate_table(t, param);
	if (r) {
		dm_table_put(t);
		return r;
	}

	md = find_device(param);
	if (!md) {
		dm_table_put(t);
		return -ENXIO;
	}

	r = dm_swap_table(md, t);
	if (r) {
		dm_put(md);
		dm_table_put(t);
		return r;
	}
	dm_table_put(t);	/* md will have taken its own reference */

	set_disk_ro(dm_disk(md), (param->flags & DM_READONLY_FLAG) ? 1 : 0);
	dm_put(md);

	r = info(param, user);
	return r;
}

static int rename(struct dm_ioctl *param, struct dm_ioctl *user)
{
	int r;
	char *new_name = (char *) param + param->data_start;

	if (valid_str(new_name, (void *) param,
		      (void *) param + param->data_size)) {
		DMWARN("Invalid new logical volume name supplied.");
		return -EINVAL;
	}

	r = check_name(new_name);
	if (r)
		return r;

	return dm_hash_rename(param->name, new_name);
}


/*-----------------------------------------------------------------
 * Implementation of open/close/ioctl on the special char
 * device.
 *---------------------------------------------------------------*/
static ioctl_fn lookup_ioctl(unsigned int cmd)
{
	static struct {
		int cmd;
		ioctl_fn fn;
	} _ioctls[] = {
		{DM_VERSION_CMD, NULL},	/* version is dealt with elsewhere */
		{DM_REMOVE_ALL_CMD, remove_all},
		{DM_DEV_CREATE_CMD, create},
		{DM_DEV_REMOVE_CMD, remove},
		{DM_DEV_RELOAD_CMD, reload},
		{DM_DEV_RENAME_CMD, rename},
		{DM_DEV_SUSPEND_CMD, suspend},
		{DM_DEV_DEPS_CMD, dep},
		{DM_DEV_STATUS_CMD, info},
		{DM_TARGET_STATUS_CMD, get_status},
		{DM_TARGET_WAIT_CMD, wait_device_event},
	};

	return (cmd >= ARRAY_SIZE(_ioctls)) ? NULL : _ioctls[cmd].fn;
}

/*
 * As well as checking the version compatibility this always
 * copies the kernel interface version out.
 */
static int check_version(unsigned int cmd, struct dm_ioctl *user)
{
	uint32_t version[3];
	int r = 0;

	if (copy_from_user(version, user->version, sizeof(version)))
		return -EFAULT;

	if ((DM_VERSION_MAJOR != version[0]) ||
	    (DM_VERSION_MINOR < version[1])) {
		DMWARN("ioctl interface mismatch: "
		       "kernel(%u.%u.%u), user(%u.%u.%u), cmd(%d)",
		       DM_VERSION_MAJOR, DM_VERSION_MINOR,
		       DM_VERSION_PATCHLEVEL,
		       version[0], version[1], version[2], cmd);
		r = -EINVAL;
	}

	/*
	 * Fill in the kernel version.
	 */
	version[0] = DM_VERSION_MAJOR;
	version[1] = DM_VERSION_MINOR;
	version[2] = DM_VERSION_PATCHLEVEL;
	if (copy_to_user(user->version, version, sizeof(version)))
		return -EFAULT;

	return r;
}

static void free_params(struct dm_ioctl *param)
{
	vfree(param);
}

static int copy_params(struct dm_ioctl *user, struct dm_ioctl **param)
{
	struct dm_ioctl tmp, *dmi;

	if (copy_from_user(&tmp, user, sizeof(tmp)))
		return -EFAULT;

	if (tmp.data_size < sizeof(tmp))
		return -EINVAL;

	dmi = (struct dm_ioctl *) vmalloc(tmp.data_size);
	if (!dmi)
		return -ENOMEM;

	if (copy_from_user(dmi, user, tmp.data_size)) {
		vfree(dmi);
		return -EFAULT;
	}

	*param = dmi;
	return 0;
}

static int validate_params(uint cmd, struct dm_ioctl *param)
{
	/* Ignores parameters */
	if (cmd == DM_REMOVE_ALL_CMD)
		return 0;

	/* Unless creating, either name of uuid but not both */
	if (cmd != DM_DEV_CREATE_CMD) {
		if ((!*param->uuid && !*param->name) ||
		    (*param->uuid && *param->name)) {
			DMWARN("one of name or uuid must be supplied");
			return -EINVAL;
		}
	}

	/* Ensure strings are terminated */
	param->name[DM_NAME_LEN - 1] = '\0';
	param->uuid[DM_UUID_LEN - 1] = '\0';

	return 0;
}

static int ctl_ioctl(struct inode *inode, struct file *file,
		     uint command, ulong u)
{
	int r = 0;
	unsigned int cmd;
	struct dm_ioctl *param;
	struct dm_ioctl *user = (struct dm_ioctl *) u;
	ioctl_fn fn = NULL;

	/* only root can play with this */
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (_IOC_TYPE(command) != DM_IOCTL)
		return -ENOTTY;

	cmd = _IOC_NR(command);

	/*
	 * Check the interface version passed in.  This also
	 * writes out the kernels interface version.
	 */
	r = check_version(cmd, user);
	if (r)
		return r;

	/*
	 * Nothing more to do for the version command.
	 */
	if (cmd == DM_VERSION_CMD)
		return 0;

	fn = lookup_ioctl(cmd);
	if (!fn) {
		DMWARN("dm_ctl_ioctl: unknown command 0x%x", command);
		return -ENOTTY;
	}

	/*
	 * Copy the parameters into kernel space.
	 */
	r = copy_params(user, &param);
	if (r)
		return r;

	r = validate_params(cmd, param);
	if (r) {
		free_params(param);
		return r;
	}

	r = fn(param, user);
	free_params(param);
	return r;
}

static struct file_operations _ctl_fops = {
	.ioctl	 = ctl_ioctl,
	.owner	 = THIS_MODULE,
};

static struct miscdevice _dm_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= DM_NAME,
	.devfs_name	= "mapper/control",
	.fops		= &_ctl_fops
};

/*
 * Create misc character device and link to DM_DIR/control.
 */
int __init dm_interface_init(void)
{
	int r;

	r = dm_hash_init();
	if (r)
		return r;

	r = misc_register(&_dm_misc);
	if (r) {
		DMERR("misc_register failed for control device");
		dm_hash_exit();
		return r;
	}

	DMINFO("%d.%d.%d%s initialised: %s", DM_VERSION_MAJOR,
	       DM_VERSION_MINOR, DM_VERSION_PATCHLEVEL, DM_VERSION_EXTRA,
	       DM_DRIVER_EMAIL);
	return 0;

	if (misc_deregister(&_dm_misc) < 0)
		DMERR("misc_deregister failed for control device");
	dm_hash_exit();
	return r;
}

void dm_interface_exit(void)
{
	if (misc_deregister(&_dm_misc) < 0)
		DMERR("misc_deregister failed for control device");
	dm_hash_exit();
}

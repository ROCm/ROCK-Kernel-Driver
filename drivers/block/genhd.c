/*
 *  gendisk handling
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/blk.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/kmod.h>

#define MAX_PROBE_HASH 255	/* random */

static struct subsystem block_subsys;

/*
 * Can be merged with blk_probe or deleted altogether. Later.
 *
 * Modified under both block_subsys.rwsem and major_names_lock.
 */
static struct blk_major_name {
	struct blk_major_name *next;
	int major;
	char name[16];
} *major_names[MAX_PROBE_HASH];

static spinlock_t major_names_lock = SPIN_LOCK_UNLOCKED;

static struct blk_probe {
	struct blk_probe *next;
	dev_t dev;
	unsigned long range;
	struct module *owner;
	struct gendisk *(*get)(dev_t dev, int *part, void *data);
	int (*lock)(dev_t, void *);
	void *data;
} *probes[MAX_PROBE_HASH];

/* index in the above - for now: assume no multimajor ranges */
static inline int major_to_index(int major)
{
	return major % MAX_PROBE_HASH;
}

static inline int dev_to_index(dev_t dev)
{
	return major_to_index(MAJOR(dev));
}

/*
 * __bdevname may be called from interrupts, and must be atomic
 */
const char *__bdevname(dev_t dev)
{
	static char buffer[40];
	char *name = "unknown-block";
	unsigned int major = MAJOR(dev);
	unsigned int minor = MINOR(dev);
	int index = major_to_index(major);
	struct blk_major_name *n;
	unsigned long flags;

	spin_lock_irqsave(&major_names_lock, flags);
	for (n = major_names[index]; n; n = n->next)
		if (n->major == major)
			break;
	if (n)
		name = &(n->name[0]);
	sprintf(buffer, "%s(%u,%u)", name, major, minor);
	spin_unlock_irqrestore(&major_names_lock, flags);

	return buffer;
}

/* get block device names in somewhat random order */
int get_blkdev_list(char *p)
{
	struct blk_major_name *n;
	int i, len;

	len = sprintf(p, "\nBlock devices:\n");

	down_read(&block_subsys.rwsem);
	for (i = 0; i < ARRAY_SIZE(major_names); i++) {
		for (n = major_names[i]; n; n = n->next)
			len += sprintf(p+len, "%3d %s\n",
				       n->major, n->name);
	}
	up_read(&block_subsys.rwsem);

	return len;
}

int register_blkdev(unsigned int major, const char *name)
{
	struct blk_major_name **n, *p;
	int index, ret = 0;
	unsigned long flags;

	down_write(&block_subsys.rwsem);

	/* temporary */
	if (major == 0) {
		for (index = ARRAY_SIZE(major_names)-1; index > 0; index--) {
			if (major_names[index] == NULL)
				break;
		}

		if (index == 0) {
			printk("register_blkdev: failed to get major for %s\n",
			       name);
			ret = -EBUSY;
			goto out;
		}
		major = index;
		ret = major;
	}

	p = kmalloc(sizeof(struct blk_major_name), GFP_KERNEL);
	if (p == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	p->major = major;
	strncpy(p->name, name, sizeof(p->name)-1);
	p->name[sizeof(p->name)-1] = 0;
	p->next = 0;
	index = major_to_index(major);

	spin_lock_irqsave(&major_names_lock, flags);
	for (n = &major_names[index]; *n; n = &(*n)->next) {
		if ((*n)->major == major)
			break;
	}
	if (!*n)
		*n = p;
	else
		ret = -EBUSY;
	spin_unlock_irqrestore(&major_names_lock, flags);

	if (ret < 0) {
		printk("register_blkdev: cannot get major %d for %s\n",
		       major, name);
		kfree(p);
	}
out:
	up_write(&block_subsys.rwsem);
	return ret;
}

/* todo: make void - error printk here */
int unregister_blkdev(unsigned int major, const char *name)
{
	struct blk_major_name **n;
	struct blk_major_name *p = NULL;
	int index = major_to_index(major);
	unsigned long flags;
	int ret = 0;

	down_write(&block_subsys.rwsem);
	spin_lock_irqsave(&major_names_lock, flags);
	for (n = &major_names[index]; *n; n = &(*n)->next)
		if ((*n)->major == major)
			break;
	if (!*n || strcmp((*n)->name, name))
		ret = -EINVAL;
	else {
		p = *n;
		*n = p->next;
	}
	spin_unlock_irqrestore(&major_names_lock, flags);
	up_write(&block_subsys.rwsem);
	kfree(p);

	return ret;
}

/*
 * Register device numbers dev..(dev+range-1)
 * range must be nonzero
 * The hash chain is sorted on range, so that subranges can override.
 */
void blk_register_region(dev_t dev, unsigned long range, struct module *module,
			 struct gendisk *(*probe)(dev_t, int *, void *),
			 int (*lock)(dev_t, void *), void *data)
{
	int index = dev_to_index(dev);
	struct blk_probe *p = kmalloc(sizeof(struct blk_probe), GFP_KERNEL);
	struct blk_probe **s;

	if (p == NULL)
		return;

	p->owner = module;
	p->get = probe;
	p->lock = lock;
	p->dev = dev;
	p->range = range;
	p->data = data;
	down_write(&block_subsys.rwsem);
	for (s = &probes[index]; *s && (*s)->range < range; s = &(*s)->next)
		;
	p->next = *s;
	*s = p;
	up_write(&block_subsys.rwsem);
}

void blk_unregister_region(dev_t dev, unsigned long range)
{
	int index = dev_to_index(dev);
	struct blk_probe **s;

	down_write(&block_subsys.rwsem);
	for (s = &probes[index]; *s; s = &(*s)->next) {
		struct blk_probe *p = *s;
		if (p->dev == dev && p->range == range) {
			*s = p->next;
			kfree(p);
			break;
		}
	}
	up_write(&block_subsys.rwsem);
}

EXPORT_SYMBOL(blk_register_region);
EXPORT_SYMBOL(blk_unregister_region);

static struct gendisk *exact_match(dev_t dev, int *part, void *data)
{
	return data;
}

static int exact_lock(dev_t dev, void *data)
{
	struct gendisk *p = data;

	if (!get_disk(p))
		return -1;
	return 0;
}

/**
 * add_gendisk - add partitioning information to kernel list
 * @disk: per-device partitioning information
 *
 * This function registers the partitioning information in @disk
 * with the kernel.
 */
void add_disk(struct gendisk *disk)
{
	disk->flags |= GENHD_FL_UP;
	blk_register_region(MKDEV(disk->major, disk->first_minor),
			    disk->minors, NULL, exact_match, exact_lock, disk);
	register_disk(disk);
	elv_register_queue(disk);
}

EXPORT_SYMBOL(add_disk);
EXPORT_SYMBOL(del_gendisk);	/* in partitions/check.c */

void unlink_gendisk(struct gendisk *disk)
{
	elv_unregister_queue(disk);
	blk_unregister_region(MKDEV(disk->major, disk->first_minor),
			      disk->minors);
}

/**
 * get_gendisk - get partitioning information for a given device
 * @dev: device to get partitioning information for
 *
 * This function gets the structure containing partitioning
 * information for the given device @dev.
 */
struct gendisk *
get_gendisk(dev_t dev, int *part)
{
	int index = dev_to_index(dev);
	struct gendisk *disk;
	struct blk_probe *p;
	unsigned best = ~0U;

retry:
	down_read(&block_subsys.rwsem);
	for (p = probes[index]; p; p = p->next) {
		struct gendisk *(*probe)(dev_t, int *, void *);
		struct module *owner;
		void *data;

		if (p->dev > dev || p->dev + p->range - 1 < dev)
			continue;
		if (p->range - 1 >= best)
			break;
		if (!try_module_get(p->owner))
			continue;
		owner = p->owner;
		data = p->data;
		probe = p->get;
		best = p->range - 1;
		*part = dev - p->dev;
		if (p->lock && p->lock(dev, data) < 0) {
			module_put(owner);
			continue;
		}
		up_read(&block_subsys.rwsem);
		disk = probe(dev, part, data);
		/* Currently ->owner protects _only_ ->probe() itself. */
		module_put(owner);
		if (disk)
			return disk;
		goto retry;		/* this terminates: best decreases */
	}
	up_read(&block_subsys.rwsem);
	return NULL;
}

#ifdef CONFIG_PROC_FS
/* iterator */
static void *part_start(struct seq_file *part, loff_t *pos)
{
	struct list_head *p;
	loff_t l = *pos;

	down_read(&block_subsys.rwsem);
	list_for_each(p, &block_subsys.kset.list)
		if (!l--)
			return list_entry(p, struct gendisk, kobj.entry);
	return NULL;
}

static void *part_next(struct seq_file *part, void *v, loff_t *pos)
{
	struct list_head *p = ((struct gendisk *)v)->kobj.entry.next;
	++*pos;
	return p==&block_subsys.kset.list ? NULL : 
		list_entry(p, struct gendisk, kobj.entry);
}

static void part_stop(struct seq_file *part, void *v)
{
	up_read(&block_subsys.rwsem);
}

static int show_partition(struct seq_file *part, void *v)
{
	struct gendisk *sgp = v;
	int n;
	char buf[64];

	if (&sgp->kobj.entry == block_subsys.kset.list.next)
		seq_puts(part, "major minor  #blocks  name\n\n");

	/* Don't show non-partitionable devices or empty devices */
	if (!get_capacity(sgp) || sgp->minors == 1)
		return 0;

	/* show the full disk and all non-0 size partitions of it */
	seq_printf(part, "%4d  %4d %10llu %s\n",
		sgp->major, sgp->first_minor,
		(unsigned long long)get_capacity(sgp) >> 1,
		disk_name(sgp, 0, buf));
	for (n = 0; n < sgp->minors - 1; n++) {
		if (sgp->part[n].nr_sects == 0)
			continue;
		seq_printf(part, "%4d  %4d %10llu %s\n",
			sgp->major, n + 1 + sgp->first_minor,
			(unsigned long long)sgp->part[n].nr_sects >> 1 ,
			disk_name(sgp, n + 1, buf));
	}

	return 0;
}

struct seq_operations partitions_op = {
	.start =part_start,
	.next =	part_next,
	.stop =	part_stop,
	.show =	show_partition
};
#endif


extern int blk_dev_init(void);

static struct gendisk *base_probe(dev_t dev, int *part, void *data)
{
	char name[30];
	sprintf(name, "block-major-%d", MAJOR(dev));
	request_module(name);
	return NULL;
}

int __init device_init(void)
{
	struct blk_probe *base = kmalloc(sizeof(struct blk_probe), GFP_KERNEL);
	int i;
	memset(base, 0, sizeof(struct blk_probe));
	base->dev = 1;
	base->range = ~0;		/* range 1 .. ~0 */
	base->get = base_probe;
	for (i = 0; i < MAX_PROBE_HASH; i++)
		probes[i] = base;	/* must remain last in chain */
	blk_dev_init();
	subsystem_register(&block_subsys);
	return 0;
}

subsys_initcall(device_init);



/*
 * kobject & sysfs bindings for block devices
 */

#define to_disk(obj) container_of(obj,struct gendisk,kobj)

struct disk_attribute {
	struct attribute attr;
	ssize_t (*show)(struct gendisk *, char *);
};

static ssize_t disk_attr_show(struct kobject *kobj, struct attribute *attr,
			      char *page)
{
	struct gendisk *disk = to_disk(kobj);
	struct disk_attribute *disk_attr =
		container_of(attr,struct disk_attribute,attr);
	ssize_t ret = 0;

	if (disk_attr->show)
		ret = disk_attr->show(disk,page);
	return ret;
}

static struct sysfs_ops disk_sysfs_ops = {
	.show	= &disk_attr_show,
};

static ssize_t disk_dev_read(struct gendisk * disk, char *page)
{
	dev_t base = MKDEV(disk->major, disk->first_minor); 
	return sprintf(page, "%04x\n", (unsigned)base);
}
static ssize_t disk_range_read(struct gendisk * disk, char *page)
{
	return sprintf(page, "%d\n", disk->minors);
}
static ssize_t disk_size_read(struct gendisk * disk, char *page)
{
	return sprintf(page, "%llu\n", (unsigned long long)get_capacity(disk));
}

static inline unsigned jiffies_to_msec(unsigned jif)
{
#if 1000 % HZ == 0
	return jif * (1000 / HZ);
#elif HZ % 1000 == 0
	return jif / (HZ / 1000);
#else
	return (jif / HZ) * 1000 + (jif % HZ) * 1000 / HZ;
#endif
}
static ssize_t disk_stats_read(struct gendisk * disk, char *page)
{
	disk_round_stats(disk);
	return sprintf(page,
		"%8u %8u %8llu %8u "
		"%8u %8u %8llu %8u "
		"%8u %8u %8u"
		"\n",
		disk_stat_read(disk, reads), disk_stat_read(disk, read_merges),
		(unsigned long long)disk_stat_read(disk, read_sectors),
		jiffies_to_msec(disk_stat_read(disk, read_ticks)),
		disk_stat_read(disk, writes), 
		disk_stat_read(disk, write_merges),
		(unsigned long long)disk_stat_read(disk, write_sectors),
		jiffies_to_msec(disk_stat_read(disk, write_ticks)),
		disk_stat_read(disk, in_flight), 
		jiffies_to_msec(disk_stat_read(disk, io_ticks)),
		jiffies_to_msec(disk_stat_read(disk, time_in_queue)));
}
static struct disk_attribute disk_attr_dev = {
	.attr = {.name = "dev", .mode = S_IRUGO },
	.show	= disk_dev_read
};
static struct disk_attribute disk_attr_range = {
	.attr = {.name = "range", .mode = S_IRUGO },
	.show	= disk_range_read
};
static struct disk_attribute disk_attr_size = {
	.attr = {.name = "size", .mode = S_IRUGO },
	.show	= disk_size_read
};
static struct disk_attribute disk_attr_stat = {
	.attr = {.name = "stat", .mode = S_IRUGO },
	.show	= disk_stats_read
};

static struct attribute * default_attrs[] = {
	&disk_attr_dev.attr,
	&disk_attr_range.attr,
	&disk_attr_size.attr,
	&disk_attr_stat.attr,
	NULL,
};

static void disk_release(struct kobject * kobj)
{
	struct gendisk *disk = to_disk(kobj);
	kfree(disk->random);
	kfree(disk->part);
	free_disk_stats(disk);
	kfree(disk);
}

static struct kobj_type ktype_block = {
	.release	= disk_release,
	.sysfs_ops	= &disk_sysfs_ops,
	.default_attrs	= default_attrs,
};


/* declare block_subsys. */
static decl_subsys(block,&ktype_block);


struct gendisk *alloc_disk(int minors)
{
	struct gendisk *disk = kmalloc(sizeof(struct gendisk), GFP_KERNEL);
	if (disk) {
		memset(disk, 0, sizeof(struct gendisk));
		if (!init_disk_stats(disk)) {
			kfree(disk);
			return NULL;
		}
		if (minors > 1) {
			int size = (minors - 1) * sizeof(struct hd_struct);
			disk->part = kmalloc(size, GFP_KERNEL);
			if (!disk->part) {
				kfree(disk);
				return NULL;
			}
			memset(disk->part, 0, size);
		}
		disk->minors = minors;
		while (minors >>= 1)
			disk->minor_shift++;
		kobj_set_kset_s(disk,block_subsys);
		kobject_init(&disk->kobj);
		rand_initialize_disk(disk);
	}
	return disk;
}

struct gendisk *get_disk(struct gendisk *disk)
{
	struct module *owner;
	struct kobject *kobj;

	if (!disk->fops)
		return NULL;
	owner = disk->fops->owner;
	if (owner && !try_module_get(owner))
		return NULL;
	kobj = kobject_get(&disk->kobj);
	if (kobj == NULL) {
		module_put(owner);
		return NULL;
	}
	return to_disk(kobj);

}

void put_disk(struct gendisk *disk)
{
	if (disk)
		kobject_put(&disk->kobj);
}

EXPORT_SYMBOL(alloc_disk);
EXPORT_SYMBOL(get_disk);
EXPORT_SYMBOL(put_disk);

void set_device_ro(struct block_device *bdev, int flag)
{
	struct gendisk *disk = bdev->bd_disk;
	if (bdev->bd_contains != bdev) {
		int part = bdev->bd_dev - MKDEV(disk->major, disk->first_minor);
		struct hd_struct *p = &disk->part[part-1];
		p->policy = flag;
	} else
		disk->policy = flag;
}

void set_disk_ro(struct gendisk *disk, int flag)
{
	int i;
	disk->policy = flag;
	for (i = 0; i < disk->minors - 1; i++)
		disk->part[i].policy = flag;
}

int bdev_read_only(struct block_device *bdev)
{
	struct gendisk *disk;
	if (!bdev)
		return 0;
	disk = bdev->bd_disk;
	if (bdev->bd_contains != bdev) {
		int part = bdev->bd_dev - MKDEV(disk->major, disk->first_minor);
		struct hd_struct *p = &disk->part[part-1];
		return p->policy;
	} else
		return disk->policy;
}

EXPORT_SYMBOL(bdev_read_only);
EXPORT_SYMBOL(set_device_ro);
EXPORT_SYMBOL(set_disk_ro);

/*
 * Generic dump device interfaces for flexible system dump 
 * (Enables variation of dump target types e.g disk, network, memory)
 *
 * These interfaces have evolved based on discussions on lkcd-devel. 
 * Eventually the intent is to support primary and secondary or 
 * alternate targets registered at the same time, with scope for 
 * situation based failover or multiple dump devices used for parallel 
 * dump i/o.
 *
 * Started: Oct 2002 - Suparna Bhattacharya (suparna@in.ibm.com)
 *
 * Copyright (C) 2001 - 2002 Matt D. Robinson.  All rights reserved.
 * Copyright (C) 2002 International Business Machines Corp. 
 *
 * This code is released under version 2 of the GNU GPL.
 */

#ifndef _LINUX_DUMPDEV_H
#define _LINUX_DUMPDEV_H

#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/netpoll.h>
#include <linux/bio.h>

/* Determined by the dump target (device) type */

struct dump_dev_driver {
	struct kobject kobj;
};

struct dump_dev;

struct dump_dev_ops {
	int (*open)(struct dump_dev *, const char *); /* configure */
	int (*release)(struct dump_dev *); /* unconfigure */
	int (*silence)(struct dump_dev *); /* when dump starts */
	int (*resume)(struct dump_dev *); /* when dump is over */
	int (*seek)(struct dump_dev *, loff_t);
	/* trigger a write (async in nature typically) */
	int (*write)(struct dump_dev *, void *, unsigned long);
	/* not usually used during dump, but option available */
	int (*read)(struct dump_dev *, void *, unsigned long);
	/* use to poll for completion */
	int (*ready)(struct dump_dev *, void *); 
	int (*ioctl)(struct dump_dev *, unsigned int, unsigned long);
};

struct dump_dev {
	int type; /* 1 = blockdev, 2 = netdev */
	unsigned long device_id; /* interpreted differently for various types */
	struct dump_dev_ops *ops;
	struct list_head list;
	loff_t curr_offset;
	struct netpoll np;
};

/*
 * dump_dev type variations: 
 */

/* block */
struct dump_blockdev {
	struct dump_dev ddev;
	dev_t dev_id;
	struct block_device *bdev;
	struct bio *bio;
	loff_t start_offset;
	loff_t limit;
	int err;
};

static inline struct dump_blockdev *DUMP_BDEV(struct dump_dev *dev)
{
	return container_of(dev, struct dump_blockdev, ddev);
}


/* mem  - for internal use by soft-boot based dumper */
struct dump_memdev {
	struct dump_dev ddev;
	unsigned long indirect_map_root;
	unsigned long nr_free;
	struct page *curr_page;
	unsigned long *curr_map;
	unsigned long curr_map_offset;
	unsigned long last_offset;
	unsigned long last_used_offset;
	unsigned long last_bs_offset;
};	

static inline struct dump_memdev *DUMP_MDEV(struct dump_dev *dev)
{
	return container_of(dev, struct dump_memdev, ddev);
}

/* Todo/future - meant for raw dedicated interfaces e.g. mini-ide driver */
struct dump_rdev {
	struct dump_dev ddev;
	char name[32];
	int (*reset)(struct dump_rdev *, unsigned int, 
		unsigned long);
	/* ... to do ... */
};

/* just to get the size right when saving config across a soft-reboot */
struct dump_anydev {
	union {
		struct dump_blockdev bddev;
		/* .. add other types here .. */
	};
};



/* Dump device / target operation wrappers */
/* These assume that dump_dev is initiatized to dump_config.dumper->dev */

extern struct dump_dev *dump_dev;

static inline int dump_dev_open(const char *arg)
{
	return dump_dev->ops->open(dump_dev, arg);
}

static inline int dump_dev_release(void)
{
	return dump_dev->ops->release(dump_dev);
}

static inline int dump_dev_silence(void)
{
	return dump_dev->ops->silence(dump_dev);
}

static inline int dump_dev_resume(void)
{
	return dump_dev->ops->resume(dump_dev);
}

static inline int dump_dev_seek(loff_t offset)
{
	return dump_dev->ops->seek(dump_dev, offset);
}

static inline int dump_dev_write(void *buf, unsigned long len)
{
	return dump_dev->ops->write(dump_dev, buf, len);
}

static inline int dump_dev_ready(void *buf)
{
	return dump_dev->ops->ready(dump_dev, buf);
}

static inline int dump_dev_ioctl(unsigned int cmd, unsigned long arg)
{
	if (!dump_dev || !dump_dev->ops->ioctl)
		return -EINVAL;
	return dump_dev->ops->ioctl(dump_dev, cmd, arg);
}

extern int dump_register_device(struct dump_dev *);
extern void dump_unregister_device(struct dump_dev *);

#endif /*  _LINUX_DUMPDEV_H */

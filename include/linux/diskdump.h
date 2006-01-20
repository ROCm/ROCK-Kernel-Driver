#ifndef _LINUX_DISKDUMP_H
#define _LINUX_DISKDUMP_H

/*
 * linux/include/linux/diskdump.h
 *
 * Copyright (c) 2004 FUJITSU LIMITED
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/list.h>
#include <linux/blkdev.h>
#include <linux/utsname.h>
#include <linux/device.h>
#include <linux/nmi.h>
#include <linux/dump.h>

/* The minimum dump I/O unit.  Must be the same as PAGE_SIZE. */
#define DUMP_BLOCK_SIZE		PAGE_SIZE
#define DUMP_BLOCK_SHIFT	PAGE_SHIFT

#define pr_err(fmt,arg...) \
	printk(KERN_ERR fmt,##arg)

#define pr_warn(fmt,arg...) \
	printk(KERN_WARNING fmt,##arg)

#define lkcd_dump_mode()       unlikely(dump_polling_oncpu)

extern unsigned long dump_polling_oncpu;


struct disk_dump_partition;
struct disk_dump_device;

struct disk_dump_type {
	void *(*probe)(struct device *);
	int (*add_device)(struct disk_dump_device *);
	void (*remove_device)(struct disk_dump_device *);
	struct module *owner;
	struct list_head list;
};

struct disk_dump_device_ops {
	int (*quiesce)(struct disk_dump_device *);
	int (*shutdown)(struct disk_dump_device *);
	int (*rw_block)(struct disk_dump_partition *, int rw, unsigned long block_nr, void *buf, int len);
};

struct disk_dump_device {
	struct disk_dump_device_ops ops;
	struct disk_dump_type *dump_type;
	void *device;
	unsigned int max_blocks;
};

struct disk_dump_partition {
	struct disk_dump_device *device;
	struct block_device *bdev;
	unsigned long start_sect;
	unsigned long nr_sects;
};

int register_disk_dump_type(struct disk_dump_type *);
int unregister_disk_dump_type(struct disk_dump_type *);

void diskdump_update(void);

#define diskdump_mdelay(n) 						\
({									\
	unsigned long __ms=(n); 					\
	while (__ms--) {						\
		udelay(1000);						\
		touch_nmi_watchdog();					\
	}								\
})

extern enum disk_dump_states {
	DISK_DUMP_INITIAL,
	DISK_DUMP_RUNNING,
	DISK_DUMP_SUCCESS,
	DISK_DUMP_FAILURE,
}  disk_dump_state;

#endif /* _LINUX_DISKDUMP_H */

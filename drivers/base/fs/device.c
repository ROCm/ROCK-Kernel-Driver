/*
 * drivers/base/fs.c - driver model interface to driverfs 
 *
 * Copyright (c) 2002 Patrick Mochel
 *		 2002 Open Source Development Lab
 */

#undef DEBUG

#include <linux/device.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/stat.h>
#include <linux/limits.h>

int get_devpath_length(struct device * dev)
{
	int length = 1;
	struct device * parent = dev;

	/* walk up the ancestors until we hit the root.
	 * Add 1 to strlen for leading '/' of each level.
	 */
	do {
		length += strlen(parent->bus_id) + 1;
		parent = parent->parent;
	} while (parent);
	return length;
}

void fill_devpath(struct device * dev, char * path, int length)
{
	struct device * parent;
	--length;
	for (parent = dev; parent; parent = parent->parent) {
		int cur = strlen(parent->bus_id);

		/* back up enough to print this bus id with '/' */
		length -= cur;
		strncpy(path + length,parent->bus_id,cur);
		*(path + --length) = '/';
	}

	pr_debug("%s: path = '%s'\n",__FUNCTION__,path);
}


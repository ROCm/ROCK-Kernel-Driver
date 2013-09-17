/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/kobject.h>
#include <linux/kobj_completion.h>

#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

BTRFS_FEAT_ATTR_INCOMPAT(mixed_backref, MIXED_BACKREF);
BTRFS_FEAT_ATTR_INCOMPAT(default_subvol, DEFAULT_SUBVOL);
BTRFS_FEAT_ATTR_INCOMPAT(mixed_groups, MIXED_GROUPS);
BTRFS_FEAT_ATTR_INCOMPAT(compress_lzo, COMPRESS_LZO);
BTRFS_FEAT_ATTR_INCOMPAT(compress_lzov2, COMPRESS_LZOv2);
BTRFS_FEAT_ATTR_INCOMPAT(big_metadata, BIG_METADATA);
BTRFS_FEAT_ATTR_INCOMPAT(extended_iref, EXTENDED_IREF);
BTRFS_FEAT_ATTR_INCOMPAT(raid56, RAID56);
BTRFS_FEAT_ATTR_INCOMPAT(skinny_metadata, SKINNY_METADATA);

static struct attribute *btrfs_supp_feature_attrs[] = {
	BTRFS_FEAT_ATTR_LIST(mixed_backref)
	BTRFS_FEAT_ATTR_LIST(default_subvol)
	BTRFS_FEAT_ATTR_LIST(mixed_groups)
	BTRFS_FEAT_ATTR_LIST(compress_lzo)
	BTRFS_FEAT_ATTR_LIST(compress_lzov2)
	BTRFS_FEAT_ATTR_LIST(big_metadata)
	BTRFS_FEAT_ATTR_LIST(extended_iref)
	BTRFS_FEAT_ATTR_LIST(raid56)
	BTRFS_FEAT_ATTR_LIST(skinny_metadata)
	NULL
};

static ssize_t btrfs_supp_attr_show(struct kobject *kobj, struct attribute *a,
				    char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0\n");
}

static const struct sysfs_ops btrfs_supp_attr_ops = {
	.show = btrfs_supp_attr_show,
};

static struct kobj_type btrfs_supp_feat_ktype = {
	.default_attrs	= btrfs_supp_feature_attrs,
	.sysfs_ops	= &btrfs_supp_attr_ops,
	.release	= kobj_completion_release,
};

/* /sys/fs/btrfs/ entry */
static struct kset *btrfs_kset;
static struct kobj_completion btrfs_features;

int btrfs_init_sysfs(void)
{
	int ret;
	btrfs_kset = kset_create_and_add("btrfs", NULL, fs_kobj);
	if (!btrfs_kset)
		return -ENOMEM;

	kobj_completion_init(&btrfs_features, &btrfs_supp_feat_ktype);
	btrfs_features.kc_kobj.kset = btrfs_kset;
	ret = kobject_add(&btrfs_features.kc_kobj, NULL, "features");
	if (ret) {
		kset_unregister(btrfs_kset);
		return ret;
	}

	return 0;
}

void btrfs_exit_sysfs(void)
{
	kobj_completion_del_and_wait(&btrfs_features);
	kset_unregister(btrfs_kset);
}


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
#include "sysfs.h"

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

const char * const btrfs_feature_set_names[FEAT_MAX] = {
	[FEAT_COMPAT]	 = "compat",
	[FEAT_COMPAT_RO] = "compat_ro",
	[FEAT_INCOMPAT]	 = "incompat",
};

static char btrfs_feature_names[FEAT_MAX][64][13];
static struct btrfs_feature_attr btrfs_feature_attrs[FEAT_MAX][64];

static void init_feature_set_attrs(enum btrfs_feature_set set)
{
	int i;
	int len = strlen(btrfs_feature_set_names[set]) + 4;

	for (i = 0; i < 64; i++) {
		char *name = btrfs_feature_names[set][i];
		struct btrfs_feature_attr *fa;

		snprintf(name, len, "%s:%u", btrfs_feature_set_names[set], i);

		fa = &btrfs_feature_attrs[set][i];
		fa->attr.name = name;
		fa->attr.mode = S_IRUGO;
		fa->feature_set = set;
		fa->feature_bit = (1ULL << i);
	}
}

static void init_feature_attrs(void)
{
	int i;

	init_feature_set_attrs(FEAT_COMPAT);
	init_feature_set_attrs(FEAT_COMPAT_RO);
	init_feature_set_attrs(FEAT_INCOMPAT);

	/* Copy the names over for supported features */
	for (i = 0; btrfs_supp_feature_attrs[i]; i++) {
		struct btrfs_feature_attr *fa;
		struct attribute *attr;
		int n;

		fa = to_btrfs_feature_attr(btrfs_supp_feature_attrs[i]);
		n = ilog2(fa->feature_bit);

		attr = &btrfs_feature_attrs[fa->feature_set][n].attr;
		attr->name = fa->attr.name;

		btrfs_feature_names[fa->feature_set][n][0] = '\0';
	}
}

static u64 get_features(struct btrfs_fs_info *fs_info,
			enum btrfs_feature_set set)
{
	struct btrfs_super_block *disk_super = fs_info->super_copy;
	if (set == FEAT_COMPAT)
		return btrfs_super_compat_flags(disk_super);
	else if (set == FEAT_COMPAT_RO)
		return btrfs_super_compat_ro_flags(disk_super);
	else
		return btrfs_super_incompat_flags(disk_super);
}

#define feat_kobj_to_fs_info(kobj) \
	container_of(kobj, struct btrfs_fs_info, features_kc.kc_kobj)
static ssize_t btrfs_feat_show(struct kobject *kobj, struct attribute *attr,
			       char *buf)
{
	struct btrfs_fs_info *fs_info = feat_kobj_to_fs_info(kobj);
	struct btrfs_feature_attr *fa = to_btrfs_feature_attr(attr);
	u64 features = get_features(fs_info, fa->feature_set);

	return snprintf(buf, PAGE_SIZE, "%u\n", !!(features & fa->feature_bit));
}

static const struct sysfs_ops btrfs_feat_attr_ops = {
	.show	= btrfs_feat_show,
};

static struct kobj_type btrfs_feat_ktype = {
	.sysfs_ops	= &btrfs_feat_attr_ops,
	.release	= kobj_completion_release,
};

static struct attribute *btrfs_attrs[] = {
	NULL,
};

#define super_kobj_to_fs_info(kobj) \
	container_of(kobj, struct btrfs_fs_info, super_kc.kc_kobj)

static ssize_t btrfs_attr_show(struct kobject *kobj,
			       struct attribute *attr, char *buf)
{
	struct btrfs_attr *a = container_of(attr, struct btrfs_attr, attr);
	struct btrfs_fs_info *fs_info = super_kobj_to_fs_info(kobj);

	return a->show ? a->show(a, fs_info, buf) : 0;
}

static ssize_t btrfs_attr_store(struct kobject *kobj,
				struct attribute *attr,
				const char *buf, size_t len)
{
	struct btrfs_attr *a = container_of(attr, struct btrfs_attr, attr);
	struct btrfs_fs_info *fs_info = super_kobj_to_fs_info(kobj);

	return a->store ? a->store(a, fs_info, buf, len) : 0;
}

static const struct sysfs_ops btrfs_attr_ops = {
	.show = btrfs_attr_show,
	.store = btrfs_attr_store,
};

static struct kobj_type btrfs_ktype = {
	.default_attrs	= btrfs_attrs,
	.sysfs_ops	= &btrfs_attr_ops,
	.release	= kobj_completion_release,
};

static int add_per_fs_feature_set(struct btrfs_fs_info *fs_info,
				  enum btrfs_feature_set set)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(btrfs_feature_attrs[0]); i++) {
		struct btrfs_feature_attr *fa = &btrfs_feature_attrs[set][i];
		u64 features = get_features(fs_info, fa->feature_set);
		int error;

		if (!(features & fa->feature_bit))
			continue;

		error = sysfs_create_file(&fs_info->features_kc.kc_kobj,
					  &fa->attr);
		if (error)
			return error;
	}
	return 0;
}

static int add_per_fs_features(struct btrfs_fs_info *fs_info)
{
	enum btrfs_feature_set set;
	int error;

	for (set = FEAT_COMPAT; set < FEAT_MAX; set++) {
		error = add_per_fs_feature_set(fs_info, set);
		if (error)
			return error;
	}

	return 0;
}

/* /sys/fs/btrfs/ entry */
static struct kset *btrfs_kset;

int btrfs_sysfs_add_one(struct btrfs_fs_info *fs_info)
{
	int error;

	kobj_completion_init(&fs_info->super_kc, &btrfs_ktype);
	fs_info->super_kc.kc_kobj.kset = btrfs_kset;
	error = kobject_add(&fs_info->super_kc.kc_kobj, NULL,
			    "%pU", fs_info->fsid);
	if (error)
		return error;

	kobj_completion_init(&fs_info->features_kc, &btrfs_feat_ktype);
	error = kobject_add(&fs_info->features_kc.kc_kobj,
			    &fs_info->super_kc.kc_kobj, "features");
	if (error)
		goto out_super;

	error = add_per_fs_features(fs_info);
	if (error)
		goto out_features;

	return 0;

out_features:
	kobj_completion_del_and_wait(&fs_info->features_kc);
out_super:
	kobj_completion_del_and_wait(&fs_info->super_kc);

	return error;
}

void btrfs_sysfs_remove_one(struct btrfs_fs_info *fs_info)
{
	kobj_completion_del_and_wait(&fs_info->features_kc);
	kobj_completion_del_and_wait(&fs_info->super_kc);
}

static struct kobj_completion btrfs_features;

int btrfs_init_sysfs(void)
{
	int ret;
	btrfs_kset = kset_create_and_add("btrfs", NULL, fs_kobj);
	if (!btrfs_kset)
		return -ENOMEM;

	init_feature_attrs();
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


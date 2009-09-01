/*
 * AppArmor security module
 *
 * This file contains AppArmor /proc/<pid>/attr interface functions
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/security.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/namei.h>

#include "include/security/apparmor.h"
#include "include/audit.h"
#include "include/context.h"
#include "include/policy.h"
#include "include/policy_interface.h"

static char *aa_simple_write_to_buffer(const char __user *userbuf,
				       size_t alloc_size, size_t copy_size,
				       loff_t *pos, const char *operation)
{
	const struct cred *cred;
	struct aa_profile *profile;
	char *data;

	if (*pos != 0) {
		/* only writes from pos 0, that is complete writes */
		data = ERR_PTR(-ESPIPE);
		goto out;
	}

	/*
	 * Don't allow confined processes to load/replace/remove profiles.
	 * No sane person would add rules allowing this to a profile
	 * but we enforce the restriction anyways.
	 */
	cred = aa_current_policy(&profile);
	if (profile) {
		struct aa_audit sa;
		memset(&sa, 0, sizeof(sa));
		sa.operation = operation;
		sa.gfp_mask = GFP_KERNEL;
		sa.error = -EACCES;
		data = ERR_PTR(aa_audit(AUDIT_APPARMOR_DENIED, profile, &sa,
					NULL));
		goto out;
	}

	data = vmalloc(alloc_size);
	if (data == NULL) {
		data = ERR_PTR(-ENOMEM);
		goto out;
	}

	if (copy_from_user(data, userbuf, copy_size)) {
		vfree(data);
		data = ERR_PTR(-EFAULT);
		goto out;
	}

out:
	return data;
}

static struct aa_profile *next_profile(struct aa_profile *profile)
{
	struct aa_profile *parent;
	struct aa_namespace *ns = profile->ns;

	if (!list_empty(&profile->base.profiles))
		return list_first_entry(&profile->base.profiles,
					struct aa_profile, base.list);

	parent = profile->parent;
	while (parent) {
		list_for_each_entry_continue(profile, &parent->base.profiles,
					     base.list)
			return profile;
		profile = parent;
		parent = parent->parent;
	}

	list_for_each_entry_continue(profile, &ns->base.profiles, base.list)
		return profile;

	read_unlock(&ns->base.lock);
	list_for_each_entry_continue(ns, &ns_list, base.list) {
		read_lock(&ns->base.lock);
		return list_first_entry(&ns->base.profiles, struct aa_profile,
					base.list);
		read_unlock(&ns->base.lock);
	}
	return NULL;
}

static void *p_start(struct seq_file *f, loff_t *pos)
	__acquires(ns_list_lock)
{
	struct aa_namespace *ns;
	loff_t l = *pos;

	read_lock(&ns_list_lock);
	if (!list_empty(&ns_list)) {
		struct aa_profile *profile = NULL;
		ns = list_first_entry(&ns_list, typeof(*ns), base.list);
		read_lock(&ns->base.lock);
		if (!list_empty(&ns->base.profiles)) {
			profile = list_first_entry(&ns->base.profiles,
						   typeof(*profile), base.list);
			for ( ; profile && l > 0; l--)
				profile = next_profile(profile);
			return profile;
		} else
			read_unlock(&ns->base.lock);
	}
	return NULL;
}

static void *p_next(struct seq_file *f, void *p, loff_t *pos)
{
	struct aa_profile *profile = (struct aa_profile *) p;

	(*pos)++;
	profile = next_profile(profile);

	return profile;
}

static void p_stop(struct seq_file *f, void *p)
	__releases(ns_list_lock)
{
	struct aa_profile *profile = (struct aa_profile *) p;

	if (profile)
		read_unlock(&profile->ns->base.lock);
	read_unlock(&ns_list_lock);
}

static void print_name(struct seq_file *f, struct aa_profile *profile)
{
	if (profile->parent) {
		print_name(f, profile->parent);
		seq_printf(f, "//");
	}
	seq_printf(f, "%s", profile->base.name);
}

static int seq_show_profile(struct seq_file *f, void *p)
{
	struct aa_profile *profile = (struct aa_profile *)p;

	if (profile->ns != default_namespace)
		seq_printf(f, ":%s:", profile->ns->base.name);
	print_name(f, profile);
	seq_printf(f, " (%s)\n",
		   PROFILE_COMPLAIN(profile) ? "complain" : "enforce");

	return 0;
}

/* Used in apparmorfs.c */
static struct seq_operations apparmorfs_profiles_op = {
	.start =	p_start,
	.next =		p_next,
	.stop =		p_stop,
	.show =		seq_show_profile,
};

static int aa_profiles_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &apparmorfs_profiles_op);
}


static int aa_profiles_release(struct inode *inode, struct file *file)
{
	return seq_release(inode, file);
}

static struct file_operations apparmorfs_profiles_fops = {
	.open =		aa_profiles_open,
	.read =		seq_read,
	.llseek =	seq_lseek,
	.release =	aa_profiles_release,
};

/* apparmor/matching */
static ssize_t aa_matching_read(struct file *file, char __user *buf,
			       size_t size, loff_t *ppos)
{
	const char *matching = "pattern=aadfa audit perms=crwxamlk/ user::other";

	return simple_read_from_buffer(buf, size, ppos, matching,
				       strlen(matching));
}

static struct file_operations apparmorfs_matching_fops = {
	.read = 	aa_matching_read,
};

/* apparmor/features */
static ssize_t aa_features_read(struct file *file, char __user *buf,
				size_t size, loff_t *ppos)
{
	const char *features = "file=3.1 capability=2.0 network=1.0 "
			       "change_hat=1.5 change_profile=1.1 "
			       "aanamespaces=1.1 rlimit=1.1";

	return simple_read_from_buffer(buf, size, ppos, features,
				       strlen(features));
}

static struct file_operations apparmorfs_features_fops = {
	.read = 	aa_features_read,
};

/* apparmor/.load */
static ssize_t aa_profile_load(struct file *f, const char __user *buf,
			       size_t size, loff_t *pos)
{
	char *data;
	ssize_t error;

	data = aa_simple_write_to_buffer(buf, size, size, pos, "profile_load");

	error = PTR_ERR(data);
	if (!IS_ERR(data)) {
		error = aa_interface_add_profiles(data, size);
		vfree(data);
	}

	return error;
}


static struct file_operations apparmorfs_profile_load = {
	.write = aa_profile_load
};

/* apparmor/.replace */
static ssize_t aa_profile_replace(struct file *f, const char __user *buf,
				  size_t size, loff_t *pos)
{
	char *data;
	ssize_t error;

	data = aa_simple_write_to_buffer(buf, size, size, pos,
					 "profile_replace");
	error = PTR_ERR(data);
	if (!IS_ERR(data)) {
		error = aa_interface_replace_profiles(data, size);
		vfree(data);
	}

	return error;
}


static struct file_operations apparmorfs_profile_replace = {
	.write = aa_profile_replace
};

/* apparmor/.remove */
static ssize_t aa_profile_remove(struct file *f, const char __user *buf,
				  size_t size, loff_t *pos)
{
	char *data;
	ssize_t error;

	/*
	 * aa_remove_profile needs a null terminated string so 1 extra
	 * byte is allocated and the copied data is null terminated.
	 */
	data = aa_simple_write_to_buffer(buf, size + 1, size, pos,
					 "profile_remove");

	error = PTR_ERR(data);
	if (!IS_ERR(data)) {
		data[size] = 0;
		error = aa_interface_remove_profiles(data, size);
		vfree(data);
	}

	return error;
}

static struct file_operations apparmorfs_profile_remove = {
	.write = aa_profile_remove
};

static struct dentry *apparmorfs_dentry;
struct dentry *apparmorfs_null;
struct vfsmount *apparmorfs_mnt;

static void aafs_remove(const char *name)
{
	struct dentry *dentry;

	dentry = lookup_one_len(name, apparmorfs_dentry, strlen(name));
	if (!IS_ERR(dentry)) {
		securityfs_remove(dentry);
		dput(dentry);
	}
}

static int aafs_create(const char *name, int mask, struct file_operations *fops)
{
	struct dentry *dentry;

	dentry = securityfs_create_file(name, S_IFREG | mask, apparmorfs_dentry,
					NULL, fops);

	return IS_ERR(dentry) ? PTR_ERR(dentry) : 0;
}

void destroy_apparmorfs(void)
{
	if (apparmorfs_dentry) {
		aafs_remove(".remove");
		aafs_remove(".replace");
		aafs_remove(".load");
		aafs_remove("matching");
		aafs_remove("features");
		aafs_remove("profiles");
		securityfs_remove(apparmorfs_dentry);
		apparmorfs_dentry = NULL;
	}
}

int create_apparmorfs(void)
{
	int error;

	if (!apparmor_initialized)
		return 0;

	if (apparmorfs_dentry) {
		AA_ERROR("%s: AppArmor securityfs already exists\n", __func__);
		return -EEXIST;
	}

	apparmorfs_dentry = securityfs_create_dir("apparmor", NULL);
	if (IS_ERR(apparmorfs_dentry)) {
		error = PTR_ERR(apparmorfs_dentry);
		apparmorfs_dentry = NULL;
		goto error;
	}
	error = aafs_create("profiles", 0440, &apparmorfs_profiles_fops);
	if (error)
		goto error;
	error = aafs_create("matching", 0444, &apparmorfs_matching_fops);
	if (error)
		goto error;
	error = aafs_create("features", 0444, &apparmorfs_features_fops);
	if (error)
		goto error;
	error = aafs_create(".load", 0640, &apparmorfs_profile_load);
	if (error)
		goto error;
	error = aafs_create(".replace", 0640, &apparmorfs_profile_replace);
	if (error)
		goto error;
	error = aafs_create(".remove", 0640, &apparmorfs_profile_remove);
	if (error)
		goto error;

	/* TODO: add support for apparmorfs_null and apparmorfs_mnt */

	/* Report that AppArmor fs is enabled */
	info_message("AppArmor Filesystem Enabled");
	return 0;

error:
	destroy_apparmorfs();
	AA_ERROR("Error creating AppArmor securityfs\n");
	apparmor_disable();
	return error;
}

fs_initcall(create_apparmorfs);


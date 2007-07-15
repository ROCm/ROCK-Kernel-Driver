/*
 *	Copyright (C) 1998-2007 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	AppArmor filesystem (part of securityfs)
 */

#include <linux/security.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>

#include "apparmor.h"
#include "inline.h"

static char *aa_simple_write_to_buffer(const char __user *userbuf,
				       size_t alloc_size, size_t copy_size,
				       loff_t *pos, const char *operation)
{
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
	profile = aa_get_profile(current);
	if (profile) {
		struct aa_audit sa;
		memset(&sa, 0, sizeof(sa));
		sa.operation = operation;
		sa.gfp_mask = GFP_KERNEL;
		sa.error_code = -EACCES;
		data = ERR_PTR(aa_audit_reject(profile, &sa));
		aa_put_profile(profile);
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

/* apparmor/profiles */
extern struct seq_operations apparmorfs_profiles_op;

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
	const char *matching = "pattern=aadfa";

	return simple_read_from_buffer(buf, size, ppos, matching,
				       strlen(matching));
}

static struct file_operations apparmorfs_matching_fops = {
	.read = 	aa_matching_read,
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
		error = aa_add_profile(data, size);
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
		error = aa_replace_profile(data, size);
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
		error = aa_remove_profile(data, size);
		vfree(data);
	}

	return error;
}

static struct file_operations apparmorfs_profile_remove = {
	.write = aa_profile_remove
};

static struct dentry *apparmor_dentry;

static void aafs_remove(const char *name)
{
	struct dentry *dentry;

	dentry = lookup_one_len(name, apparmor_dentry, strlen(name));
	if (!IS_ERR(dentry)) {
		securityfs_remove(dentry);
		dput(dentry);
	}
}

static int aafs_create(const char *name, int mask, struct file_operations *fops)
{
	struct dentry *dentry;

	dentry = securityfs_create_file(name, S_IFREG | mask, apparmor_dentry,
					NULL, fops);

	return IS_ERR(dentry) ? PTR_ERR(dentry) : 0;
}

void destroy_apparmorfs(void)
{
	if (apparmor_dentry) {
		aafs_remove(".remove");
		aafs_remove(".replace");
		aafs_remove(".load");
		aafs_remove("matching");
		aafs_remove("profiles");
		securityfs_remove(apparmor_dentry);
		apparmor_dentry = NULL;
	}
}

int create_apparmorfs(void)
{
	int error;

	if (apparmor_dentry) {
		AA_ERROR("%s: AppArmor securityfs already exists\n",
			__FUNCTION__);
		return -EEXIST;
	}

	apparmor_dentry = securityfs_create_dir("apparmor", NULL);
	if (IS_ERR(apparmor_dentry)) {
		error = PTR_ERR(apparmor_dentry);
		apparmor_dentry = NULL;
 		goto error;
	}
	error = aafs_create("profiles", 0440, &apparmorfs_profiles_fops);
	if (error)
		goto error;
	error = aafs_create("matching", 0444, &apparmorfs_matching_fops);
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

	return 0;

error:
	destroy_apparmorfs();
	AA_ERROR("Error creating AppArmor securityfs\n");
	return error;
}


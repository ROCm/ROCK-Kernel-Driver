/*
 *	Copyright (C) 2005 Novell/SUSE
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
#include "match/match.h"

#define SECFS_AA "apparmor"
static struct dentry *aafs_dentry = NULL;

/* profile */
extern struct seq_operations apparmorfs_profiles_op;
static int aa_prof_open(struct inode *inode, struct file *file);
static int aa_prof_release(struct inode *inode, struct file *file);

static struct file_operations apparmorfs_profiles_fops = {
	.open =		aa_prof_open,
	.read =		seq_read,
	.llseek =	seq_lseek,
	.release =	aa_prof_release,
};

/* matching */
static ssize_t aa_matching_read(struct file *file, char __user *buf,
			       size_t size, loff_t *ppos);

static struct file_operations apparmorfs_matching_fops = {
	.read = 	aa_matching_read,
};


/* interface */
static ssize_t aa_profile_load(struct file *f, const char __user *buf,
			       size_t size, loff_t *pos);
static ssize_t aa_profile_replace(struct file *f, const char __user *buf,
				  size_t size, loff_t *pos);
static ssize_t aa_profile_remove(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos);

static struct file_operations apparmorfs_profile_load = {
	.write = aa_profile_load
};

static struct file_operations apparmorfs_profile_replace = {
	.write = aa_profile_replace
};

static struct file_operations apparmorfs_profile_remove = {
	.write = aa_profile_remove
};


/* control */
static u64 aa_control_get(void *data);
static void aa_control_set(void *data, u64 val);

DEFINE_SIMPLE_ATTRIBUTE(apparmorfs_control_fops, aa_control_get,
			aa_control_set, "%lld\n");



/* table of static entries */

static struct root_entry {
	const char *name;
	int mode;
	int access;
	struct file_operations *fops;
	void *data;

	/* internal fields */
	struct dentry *dentry;
	int parent_index;
} root_entries[] = {
	/* our root, normally /sys/kernel/security/apparmor */
	{SECFS_AA, 	S_IFDIR, 0550},	/* DO NOT EDIT/MOVE */

	/* interface for obtaining list of profiles currently loaded */
	{"profiles", 	S_IFREG, 0440, &apparmorfs_profiles_fops,
				       NULL},

	/* interface for obtaining matching features supported */
	{"matching",  	S_IFREG, 0440, &apparmorfs_matching_fops,
				       NULL},

	/* interface for loading/removing/replacing profiles */
	{".load",    	S_IFREG, 0640, &apparmorfs_profile_load,
				       NULL},
	{".replace", 	S_IFREG, 0640, &apparmorfs_profile_replace,
				       NULL},
	{".remove",  	S_IFREG, 0640, &apparmorfs_profile_remove,
				       NULL},

	/* interface for setting binary config values */
	{"control",  	S_IFDIR, 0550},
	{"complain", 	S_IFREG, 0640, &apparmorfs_control_fops,
				       &apparmor_complain},
	{"audit",    	S_IFREG, 0640, &apparmorfs_control_fops,
				       &apparmor_audit},
	{"debug",    	S_IFREG, 0640, &apparmorfs_control_fops,
				       &apparmor_debug},
	{"logsyscall", 	S_IFREG, 0640, &apparmorfs_control_fops,
				       &apparmor_logsyscall},
	{NULL,       	S_IFDIR, 0},

	/* root end */
	{NULL,       	S_IFDIR, 0}
};

#define AAFS_DENTRY root_entries[0].dentry

static const unsigned int num_entries =
	sizeof(root_entries) / sizeof(struct root_entry);



static int aa_prof_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &apparmorfs_profiles_op);
}


static int aa_prof_release(struct inode *inode, struct file *file)
{
	return seq_release(inode, file);
}

static ssize_t aa_matching_read(struct file *file, char __user *buf,
			       size_t size, loff_t *ppos)
{
	const char *matching = aamatch_features();

	return simple_read_from_buffer(buf, size, ppos, matching,
				       strlen(matching));
}

static char *aa_simple_write_to_buffer(const char __user *userbuf,
				       size_t alloc_size, size_t copy_size,
				       loff_t *pos, const char *msg)
{
	struct aaprofile *active;
	char *data;

	if (*pos != 0) {
		/* only writes from pos 0, that is complete writes */
		data = ERR_PTR(-ESPIPE);
		goto out;
	}

	/* Don't allow confined processes to load/replace/remove profiles.
	 * No sane person would add rules allowing this to a profile
	 * but we enforce the restriction anyways.
	 */
	rcu_read_lock();
	active = get_activeptr_rcu();
	if (active) {
		AA_WARN("REJECTING access to profile %s (%s(%d) "
			"profile %s active %s)\n",
			msg, current->comm, current->pid,
			BASE_PROFILE(active)->name, active->name);

		data = ERR_PTR(-EPERM);
		goto out;
	}
	rcu_read_unlock();

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

static ssize_t aa_profile_load(struct file *f, const char __user *buf,
			       size_t size, loff_t *pos)
{
	char *data;
	ssize_t error;

	data = aa_simple_write_to_buffer(buf, size, size, pos, "load");

	if (!IS_ERR(data)) {
		error = aa_file_prof_add(data, size);
		vfree(data);
	} else {
		error = PTR_ERR(data);
	}

	return error;
}

static ssize_t aa_profile_replace(struct file *f, const char __user *buf,
				  size_t size, loff_t *pos)
{
	char *data;
	ssize_t error;

	data = aa_simple_write_to_buffer(buf, size, size, pos, "replacement");

	if (!IS_ERR(data)) {
		error = aa_file_prof_repl(data, size);
		vfree(data);
	} else {
		error = PTR_ERR(data);
	}

	return error;
}

static ssize_t aa_profile_remove(struct file *f, const char __user *buf,
				  size_t size, loff_t *pos)
{
	char *data;
	ssize_t error;

	/* aa_file_prof_remove needs a null terminated string so 1 extra
	 * byte is allocated and null the copied data is then null terminated
	 */
	data = aa_simple_write_to_buffer(buf, size+1, size, pos, "removal");

	if (!IS_ERR(data)) {
		data[size] = 0;
		error = aa_file_prof_remove(data, size);
		vfree(data);
	} else {
		error = PTR_ERR(data);
	}

	return error;
}

static u64 aa_control_get(void *data)
{
	return *(int *)data;
}

static void aa_control_set(void *data, u64 val)
{
	if (val > 1)
		val = 1;

	*(int*)data = (int)val;
}

static void clear_apparmorfs(void)
{
	unsigned int i;

	for (i=0; i < num_entries;i++) {
		unsigned int index;

		if (root_entries[i].mode == S_IFDIR) {
			if (root_entries[i].name)
				/* defer dir free till all sub-entries freed */
				continue;
			else
				/* cleanup parent */
				index = root_entries[i].parent_index;
		} else {
			index = i;
		}

		if (root_entries[index].dentry) {
			securityfs_remove(root_entries[index].dentry);

			AA_DEBUG("%s: deleted apparmorfs entry name=%s "
				 "dentry=%p\n",
				__FUNCTION__,
				root_entries[index].name,
				root_entries[index].dentry);

			root_entries[index].dentry = NULL;
			root_entries[index].parent_index = 0;
		}
	}
}

static int populate_apparmorfs(struct dentry *root)
{
	unsigned int i, parent_index, depth;

	for (i = 0; i < num_entries; i++) {
		root_entries[i].dentry = NULL;
		root_entries[i].parent_index = 0;
	}

	/* 1. Verify entry 0 is valid [sanity check] */
	if (num_entries == 0 ||
	    !root_entries[0].name ||
	    strcmp(root_entries[0].name, SECFS_AA) != 0 ||
	    root_entries[0].mode != S_IFDIR) {
		AA_ERROR("%s: root entry 0 is not SECFS_AA/dir\n",
			__FUNCTION__);
		goto error;
	}

	/* 2. Build back pointers */
	parent_index = 0;
	depth = 1;

	for (i = 1; i < num_entries; i++) {
		root_entries[i].parent_index = parent_index;

		if (root_entries[i].name &&
		    root_entries[i].mode == S_IFDIR) {
			depth++;
			parent_index = i;
		} else if (!root_entries[i].name) {
			if (root_entries[i].mode != S_IFDIR || depth == 0) {
				AA_ERROR("%s: root_entry %d invalid (%u %d)",
					 __FUNCTION__, i,
					 root_entries[i].mode,
					 root_entries[i].parent_index);
				goto error;
			}

			depth--;
			parent_index = root_entries[parent_index].parent_index;
		}
	}

	if (depth != 0) {
		AA_ERROR("%s: root_entry table not correctly terminated\n",
			__FUNCTION__);
		goto error;
	}

	/* 3. Create root (parent=NULL) */
	root_entries[0].dentry = securityfs_create_file(
					root_entries[0].name,
					root_entries[0].mode |
						root_entries[0].access,
					NULL, NULL, NULL);

	if (IS_ERR(root_entries[0].dentry))
		goto error;
	else
		AA_DEBUG("%s: created securityfs/apparmor [dentry=%p]\n",
			__FUNCTION__, root_entries[0].dentry);


	/* 4. create remaining nodes */
	for (i = 1; i < num_entries; i++) {
		struct dentry *parent;
		void *data = NULL;
		struct file_operations *fops = NULL;

		/* end of directory ? */
		if (!root_entries[i].name)
			continue;

		parent = root_entries[root_entries[i].parent_index].dentry;

		if (root_entries[i].mode != S_IFDIR) {
			data = root_entries[i].data;
			fops = root_entries[i].fops;
		}

		root_entries[i].dentry = securityfs_create_file(
						root_entries[i].name,
						root_entries[i].mode |
							root_entries[i].access,
						parent,
						data,
						fops);

		if (IS_ERR(root_entries[i].dentry))
			goto cleanup_error;

		AA_DEBUG("%s: added apparmorfs entry "
			 "name=%s mode=%x dentry=%p [parent %p]\n",
			__FUNCTION__, root_entries[i].name,
			root_entries[i].mode|root_entries[i].access,
			root_entries[i].dentry, parent);
	}

	return 0;

cleanup_error:
	clear_apparmorfs();

error:
	return -EINVAL;
}

int create_apparmorfs(void)
{
	int error = 0;

	if (AAFS_DENTRY) {
		error = -EEXIST;
		AA_ERROR("%s: AppArmor securityfs already exists\n",
			__FUNCTION__);
	} else {
		error = populate_apparmorfs(aafs_dentry);
		if (error != 0) {
			AA_ERROR("%s: Error populating AppArmor securityfs\n",
				__FUNCTION__);
		}
	}

	return error;
}

void destroy_apparmorfs(void)
{
	if (AAFS_DENTRY)
		clear_apparmorfs();
}

/*
 *	Copyright (C) 2005 Novell/SUSE
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 *
 *	SubDomain filesystem (part of securityfs)
 */

#include <linux/security.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>

#include "subdomain.h"
#include "inline.h"

#define SECFS_SD "subdomain"
static struct dentry *sdfs_dentry = NULL;

/* profile */
extern struct seq_operations subdomainfs_profiles_op;
static int sd_prof_open(struct inode *inode, struct file *file);
static int sd_prof_release(struct inode *inode, struct file *file);

static struct file_operations subdomainfs_profiles_fops = {
	.open =		sd_prof_open,
	.read =		seq_read,
	.llseek =	seq_lseek,
	.release =	sd_prof_release,
};

/* version */
static ssize_t sd_version_read(struct file *file, char __user *buf,
			       size_t size, loff_t *ppos);

static struct file_operations subdomainfs_version_fops = {
	.read = 	sd_version_read,
};

/* interface */
extern ssize_t sd_file_prof_add(void *, size_t);
extern ssize_t sd_file_prof_repl(void *, size_t);
extern ssize_t sd_file_prof_remove(const char *, int);

static ssize_t sd_profile_load(struct file *f, const char __user *buf,
			       size_t size, loff_t *pos);
static ssize_t sd_profile_replace(struct file *f, const char __user *buf,
				  size_t size, loff_t *pos);
static ssize_t sd_profile_remove(struct file *f, const char __user *buf,
				 size_t size, loff_t *pos);

static struct file_operations subdomainfs_profile_load = {
	.write = sd_profile_load
};

static struct file_operations subdomainfs_profile_replace = {
	.write = sd_profile_replace
};

static struct file_operations subdomainfs_profile_remove = {
	.write = sd_profile_remove
};


/* control */
static u64 sd_control_get(void *data);
static void sd_control_set(void *data, u64 val);

DEFINE_SIMPLE_ATTRIBUTE(subdomainfs_control_fops, sd_control_get,
			sd_control_set, "%lld\n");



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
	/* our root, normally /sys/kernel/security/subdomain */
	{SECFS_SD, 	S_IFDIR, 0550},	/* DO NOT EDIT/MOVE */

	/* interface for obtaining list of profiles currently loaded */
	{"profiles", 	S_IFREG, 0440, &subdomainfs_profiles_fops,
				       NULL},

	/* interface for obtaining version# of subdomain */
	{"version",  	S_IFREG, 0440, &subdomainfs_version_fops,
				       NULL},

	/* interface for loading/removing/replacing profiles */
	{".load",    	S_IFREG, 0640, &subdomainfs_profile_load,
				       NULL},
	{".replace", 	S_IFREG, 0640, &subdomainfs_profile_replace,
				       NULL},
	{".remove",  	S_IFREG, 0640, &subdomainfs_profile_remove,
				       NULL},

	/* interface for setting binary config values */
	{"control",  	S_IFDIR, 0550},
	{"complain", 	S_IFREG, 0640, &subdomainfs_control_fops,
				       &subdomain_complain},
	{"audit",    	S_IFREG, 0640, &subdomainfs_control_fops,
				       &subdomain_audit},
	{"debug",    	S_IFREG, 0640, &subdomainfs_control_fops,
				       &subdomain_debug},
	{"logsyscall", 	S_IFREG, 0640, &subdomainfs_control_fops,
				       &subdomain_logsyscall},
	{NULL,       	S_IFDIR, 0},

	/* root end */
	{NULL,       	S_IFDIR, 0}
};

#define SDFS_DENTRY root_entries[0].dentry

static const unsigned int num_entries =
	sizeof(root_entries) / sizeof(struct root_entry);



static int sd_prof_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &subdomainfs_profiles_op);
}


static int sd_prof_release(struct inode *inode, struct file *file)
{
	return seq_release(inode, file);
}

static ssize_t sd_version_read(struct file *file, char __user *buf,
			       size_t size, loff_t *ppos)
{
	const char *version = subdomain_version_nl();

	return simple_read_from_buffer(buf, size, ppos, version,
				       strlen(version));
}

static ssize_t sd_profile_load(struct file *f, const char __user *buf,
			       size_t size, loff_t *pos)
{
	void *data;
	ssize_t error = -EFAULT;

	if (*pos != 0)
		/* only writes from pos 0, that is complete writes */
		return -ESPIPE;

	/* Don't allow confined processes to load profiles.
	 * No sane person would add rules allowing this to a profile
	 * but we enforce the restriction anyways.
	 */
	if (sd_is_confined()) {
		struct subdomain *sd = SD_SUBDOMAIN(current->security);

		SD_WARN("REJECTING access to profile addition (%s(%d) "
			"profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);

		return -EPERM;
	}

	data = vmalloc(size);
	if (data == NULL)
		return -ENOMEM;

	if (copy_from_user(data, buf, size)) {
		error = -EFAULT;
		goto out;
	}

	error = sd_file_prof_add(data, size);

out:
	vfree(data);
	return error;
}

static ssize_t sd_profile_replace(struct file *f, const char __user *buf,
				  size_t size, loff_t *pos)
{
	void *data;
	ssize_t error = -EFAULT;

	if (*pos != 0)
		/* only writes from pos 0, that is complete writes */
		return -ESPIPE;

	/* Don't allow confined processes to replace profiles */
	if (sd_is_confined()) {
		struct subdomain *sd = SD_SUBDOMAIN(current->security);

		SD_WARN("REJECTING access to profile replacement (%s(%d) "
			"profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);

		return -EPERM;
	}

	data = vmalloc(size);
	if (data == NULL)
		return -ENOMEM;

	if (copy_from_user(data, buf, size)) {
		error = -EFAULT;
		goto out;
	}

	error = sd_file_prof_repl(data, size);

out:
	vfree(data);
	return error;
}

static ssize_t sd_profile_remove(struct file *f, const char __user *buf,
				  size_t size, loff_t *pos)
{
	char *data;
	ssize_t error = -EFAULT;

	if (*pos != 0)
		/* only writes from pos 0, that is complete writes */
		return -ESPIPE;

	/* Don't allow confined processes to remove profiles */
	if (sd_is_confined()) {
		struct subdomain *sd = SD_SUBDOMAIN(current->security);

		SD_WARN("REJECTING access to profile removal (%s(%d) "
			"profile %s active %s)\n",
			current->comm, current->pid,
			sd->profile->name, sd->active->name);

		return -EPERM;
	}

	data = (char *)vmalloc(size + 1);
	if (data == NULL)
		return -ENOMEM;

	data[size] = 0;
	if (copy_from_user(data, buf, size)) {
		error = -EFAULT;
		goto out;
	}

	error = sd_file_prof_remove((char *)data, size);

out:
	vfree(data);
	return error;
}

static u64 sd_control_get(void *data)
{
	return *(int *)data;
}

static void sd_control_set(void *data, u64 val)
{
	if (val > 1)
		val = 1;

	*(int*)data = (int)val;
}

static void clear_subdomainfs(void)
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

			SD_DEBUG("%s: deleted subdomainfs entry name=%s "
				 "dentry=%p\n",
				__FUNCTION__,
				root_entries[index].name,
				root_entries[index].dentry);

			root_entries[index].dentry = NULL;
			root_entries[index].parent_index = 0;
		}
	}
}

static int populate_subdomainfs(struct dentry *root)
{
	unsigned int i, parent_index, depth;

#define ENT root_entries[i]

	for (i = 0; i < num_entries; i++) {
		root_entries[i].dentry = NULL;
		root_entries[i].parent_index = 0;
	}

	/* 1. Verify entry 0 is valid [sanity check] */
	if (num_entries == 0 ||
	    !root_entries[0].name ||
	    strcmp(root_entries[0].name, SECFS_SD) != 0 ||
	    root_entries[0].mode != S_IFDIR) {
		SD_ERROR("%s: root entry 0 is not SECFS_SD/dir\n",
			__FUNCTION__);
		goto error;
	}

	/* 2. Verify table structure */
	parent_index = 0;
	depth = 1;

	for (i = 1; i < num_entries; i++) {
		ENT.parent_index = parent_index;

		if (ENT.name && ENT.mode == S_IFDIR) {
			depth++;
			parent_index = i;
		} else if (!ENT.name) {
			if (ENT.mode != S_IFDIR || depth == 0) {
				SD_ERROR("%s: root_entry %d invalid (%u %d)",
					 __FUNCTION__, i,
					 ENT.mode, ENT.parent_index);
				goto error;
			}

			depth--;
			parent_index = root_entries[parent_index].parent_index;
		}
	}

	if (depth != 0) {
		SD_ERROR("%s: root_entry table not correctly terminated\n",
			__FUNCTION__);
		goto error;
	}

	/* 3. Create root (parent=NULL) */
	i=0;

	ENT.dentry = securityfs_create_file(ENT.name,
					ENT.mode | ENT.access,
					NULL, NULL, NULL);

	if (ENT.dentry)
		SD_DEBUG("%s: created securityfs/subdomain [dentry=%p]\n",
			__FUNCTION__, ENT.dentry);
	else
		goto error;


	/* 4. create remaining nodes */
	for (i = 1; i < num_entries; i++) {
		struct dentry *parent;

		/* end of directory ? */
		if (!ENT.name)
			continue;

		parent = root_entries[ENT.parent_index].dentry;

		ENT.dentry = securityfs_create_file(ENT.name,
					ENT.mode | ENT.access,
					parent,
					ENT.mode != S_IFDIR ? ENT.data : NULL,
					ENT.mode != S_IFDIR ? ENT.fops : NULL);

		if (!ENT.dentry)
			goto cleanup_error;

		SD_DEBUG("%s: added subdomainfs entry "
			 "name=%s mode=%x dentry=%p [parent %p]\n",
			__FUNCTION__, ENT.name, ENT.mode|ENT.access,
			ENT.dentry, parent);
	}

	return 1;

cleanup_error:
	clear_subdomainfs();

error:
	return 0;
}

int create_subdomainfs(void)
{
	if (SDFS_DENTRY)
		SD_ERROR("%s: Subdomain securityfs already exists\n",
			__FUNCTION__);
	else if (!populate_subdomainfs(sdfs_dentry))
		SD_ERROR("%s: Error populating Subdomain securityfs\n",
			__FUNCTION__);

	return (SDFS_DENTRY != NULL);
}

int destroy_subdomainfs(void)
{
	if (SDFS_DENTRY)
		clear_subdomainfs();

	return 1;
}

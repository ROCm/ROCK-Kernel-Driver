/*
 * This file is released under the GPL.
 */
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/mount.h>
#include <linux/backing-dev.h>
#include <asm/uaccess.h>
#include <linux/rcfs.h>


extern int rbce_enabled;
extern void get_rule(const char *, char *);
extern int rule_exists(const char *);
extern int change_rule(const char *, char *);
extern int delete_rule(const char *);
//extern int reclassify_pid(int);
extern int set_tasktag(int, char *);
extern int rename_rule(const char *, const char *);


extern int rcfs_register_engine(rbce_eng_callback_t *rcbs);
extern int rcfs_unregister_engine(rbce_eng_callback_t *rcbs);
extern int rcfs_mkroot(struct rcfs_magf *, int , struct dentry **);
extern int rcfs_rmroot(struct dentry *);




static int rbce_unlink(struct inode *, struct dentry *);

#include "info.h"
static ssize_t
rbce_write(struct file *file, const char __user *buf,
		size_t len, loff_t * ppos)
{
	char *line, *ptr;
	int rc = 0, pid;

	line = (char *) kmalloc(len+1, GFP_KERNEL);
	if (!line) {
		return -ENOMEM;
	}
	if (copy_from_user(line, buf, len)) {
		kfree(line);
		return -EFAULT;
	}
	line[len] = '\0';
	ptr = line + strlen(line) - 1;
	if (*ptr == '\n') {
		*ptr = '\0';
	}

#if 0
	if (!strcmp(file->f_dentry->d_name.name, "rbce_reclassify")) {
		pid = simple_strtol(line, NULL, 0);
		rc = reclassify_pid(pid);
	} else 
#endif
	if (!strcmp(file->f_dentry->d_name.name, "rbce_tag")) {
		pid = simple_strtol(line, &ptr, 0);
		rc = set_tasktag(pid, ptr+1); // expected syntax "pid tag"
        } else if (!strcmp(file->f_dentry->d_name.name, "rbce_state")) {
		rbce_enabled = line[0] - '0';
	} else if (!strcmp(file->f_dentry->d_name.name, "rbce_info")) {
		len = -EPERM;
	} else {
		rc = change_rule(file->f_dentry->d_name.name, line);
	}
	if (rc) {
		len = rc;
	}

	// printk("kernel read |%s|\n", line);
	// printk("kernel read-2 |%s|\n", line+1000);
	// printk prints only 1024 bytes once :)
	//
	kfree(line);
	return len;
}

static int
rbce_show(struct seq_file *seq, void *offset)
{
	struct file *file = (struct file *) seq->private;
	char result[256];

	memset(result, 0, 256);
	if (!strcmp(file->f_dentry->d_name.name, "rbce_reclassify") || 
			!strcmp(file->f_dentry->d_name.name, "rbce_tag")) {
		return -EPERM;
	}
	if (!strcmp(file->f_dentry->d_name.name, "rbce_state")) {
		seq_printf(seq, "%d\n", rbce_enabled);
		return 0;
	}
	if (!strcmp(file->f_dentry->d_name.name, "rbce_info")) {
		seq_printf(seq, info);
		return 0;
	}

	get_rule(file->f_dentry->d_name.name, result);
	seq_printf(seq, "%s\n", result);
	return 0;
}

static int
rbce_open(struct inode *inode, struct file *file)
{
	//printk("mnt_mountpoint %s\n", file->f_vfsmnt->mnt_mountpoint->d_name.name);
	//printk("mnt_root %s\n", file->f_vfsmnt->mnt_root->d_name.name);
	return single_open(file, rbce_show, file);
}

static int
rbce_close(struct inode *ino, struct file *file)
{
	const char *name = file->f_dentry->d_name.name;

	if (strcmp(name, "rbce_reclassify") &&
			strcmp(name, "rbce_state") &&
			strcmp(name, "rbce_tag") &&
			strcmp(name, "rbce_info")) {

		if (!rule_exists(name)) {
			// need more stuff to happen in the vfs layer
			rbce_unlink(file->f_dentry->d_parent->d_inode, file->f_dentry);
		}
	}
	return single_release(ino, file);
}

#define RCFS_MAGIC 0x4feedbac

static struct file_operations rbce_file_operations;
static struct inode_operations rbce_file_inode_operations;
static struct inode_operations rbce_dir_inode_operations;

static struct inode *
rbce_get_inode(struct inode *dir, int mode, dev_t dev)
{
	struct inode * inode = new_inode(dir->i_sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_mapping->a_ops = dir->i_mapping->a_ops;
		inode->i_mapping->backing_dev_info = dir->i_mapping->backing_dev_info;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			/* Treat as default assignment */
			inode->i_op = &rbce_file_inode_operations;
			inode->i_fop = &rbce_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &rbce_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inode->i_nlink++;
			break;
		}
	}
	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int
rbce_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct inode * inode = rbce_get_inode(dir, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		if (dir->i_mode & S_ISGID) {
			inode->i_gid = dir->i_gid;
			if (S_ISDIR(mode))
				inode->i_mode |= S_ISGID;
		}
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;

	}
	return error;
}

static int
rbce_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int rc;

	rc = delete_rule(dentry->d_name.name);
	if (rc == 0) {
		if (dir) {
			dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		}
		inode->i_ctime = CURRENT_TIME;
		inode->i_nlink--;
		dput(dentry);
	}
	return rc;
}

static int
rbce_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry)
{
	int rc;
	struct inode *inode = old_dentry->d_inode;
	struct dentry *old_d = list_entry(old_dir->i_dentry.next,
				struct dentry, d_alias);
	struct dentry *new_d = list_entry(new_dir->i_dentry.next,
			struct dentry, d_alias);

	// cannot rename any directory
	if (S_ISDIR(old_dentry->d_inode->i_mode)) {
		return -EINVAL;
	}

	// cannot rename anything under /ce
	if (!strcmp(old_d->d_name.name, "ce")) {
		return -EINVAL;
	}

	// cannot move anything to /ce
	if (!strcmp(new_d->d_name.name, "ce")) {
		return -EINVAL;
	}

	rc = rename_rule(old_dentry->d_name.name, new_dentry->d_name.name);

	if (!rc) {
		old_dir->i_ctime = old_dir->i_mtime = new_dir->i_ctime =
			new_dir->i_mtime = inode->i_ctime = CURRENT_TIME;
	}
	return rc;
}


// CE allows only the rules directory to be created
int
rbce_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int retval = -EINVAL;

	struct dentry *pd = list_entry(dir->i_dentry.next, struct dentry, d_alias);

	// Allow only /rcfs/ce and ce/rules
	if ((!strcmp(pd->d_name.name, "ce") &&
			!strcmp(dentry->d_name.name, "rules")) ||
			(!strcmp(pd->d_name.name, "/") &&
			!strcmp(dentry->d_name.name, "ce"))) {

		if (!strcmp(dentry->d_name.name, "ce")) {
			try_module_get(THIS_MODULE);
		}
		retval = rbce_mknod(dir, dentry, mode | S_IFDIR, 0);
		if (!retval) {
			dir->i_nlink++;
		}
	}
	
	return retval;
}

// CE doesn't allow deletion of directory
int
rbce_rmdir(struct inode *dir, struct dentry *dentry)
{
	int rc;
	// printk("removal of directory %s prohibited\n", dentry->d_name.name);
	rc = simple_rmdir(dir, dentry);

	if (!rc && !strcmp(dentry->d_name.name, "ce")) {
		module_put(THIS_MODULE);
	}
	return rc;
}

static int
rbce_create(struct inode *dir, struct dentry *dentry,
			int mode, struct nameidata *nd)
{
	struct dentry *pd = list_entry(dir->i_dentry.next, struct dentry, d_alias);

	// Under /ce only "rbce_reclassify", "rbce_state", "rbce_tag" and
	// "rbce_info" are allowed
	if (!strcmp(pd->d_name.name, "ce")) {
		if (strcmp(dentry->d_name.name, "rbce_reclassify") &&
				strcmp(dentry->d_name.name, "rbce_state") &&
				strcmp(dentry->d_name.name, "rbce_tag") &&
				strcmp(dentry->d_name.name, "rbce_info")) {
			return -EINVAL;
		}
	}

	return rbce_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int
rbce_link(struct dentry *old_d, struct inode *dir, struct dentry *d)
{
	return -EINVAL;
}

static int
rbce_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	return -EINVAL;
}

/******************************* Magic files  ********************/

#define RBCE_NR_MAGF 6
struct rcfs_magf rbce_magf_files[RBCE_NR_MAGF] = {
	{
		.name    =  "ce", 
		.mode    = RCFS_DEFAULT_DIR_MODE, 
		.i_op    = &rbce_dir_inode_operations, 
	},
	{ 
		.name    =  "rbce_tag", 
		.mode    = RCFS_DEFAULT_FILE_MODE, 
		.i_fop    = &rbce_file_operations, 
	},
	{
		.name    =  "rbce_info", 
		.mode    = RCFS_DEFAULT_FILE_MODE,  
		.i_fop    = &rbce_file_operations, 
	},
	{
		.name    =  "rbce_state", 
		.mode    = RCFS_DEFAULT_FILE_MODE,  
		.i_fop    = &rbce_file_operations, 
	},
	{
		.name    =  "rbce_reclassify", 
		.mode    = RCFS_DEFAULT_FILE_MODE,  
		.i_fop    = &rbce_file_operations, 
	},
	{ 
		.name    =  "rules", 
		.mode    = (RCFS_DEFAULT_DIR_MODE | S_IWUSR), 
		.i_fop   = &simple_dir_operations, 
		.i_op    = &rbce_dir_inode_operations,
	}
};

static struct dentry *ce_root_dentry;

int
rbce_create_magic(void)
{
	return rcfs_mkroot(rbce_magf_files,RBCE_NR_MAGF,&ce_root_dentry);
}

int
rbce_clear_magic(void)
{
	int rc = 0;
	if (ce_root_dentry) 
		rc = rcfs_rmroot(ce_root_dentry);
	return rc;
}


/******************************* File ops ********************/

static struct file_operations rbce_file_operations = {
	.owner          = THIS_MODULE,
	.open           = rbce_open,
	.llseek         = seq_lseek,
	.read           = seq_read,
	.write          = rbce_write,
	.release        = rbce_close,
};

static struct inode_operations rbce_file_inode_operations = {
	.getattr	= simple_getattr,
};

static struct inode_operations rbce_dir_inode_operations = {
	.create		= rbce_create,
	.lookup		= simple_lookup,
	.link		= rbce_link,
	.unlink		= rbce_unlink,
	.symlink	= rbce_symlink,
	.mkdir		= rbce_mkdir,
	.rmdir		= rbce_rmdir,
	.mknod		= rbce_mknod,
	.rename		= rbce_rename,
	.getattr        = simple_getattr,
};

#if 0
static void
rbce_put_super(struct super_block * sb)
{
	module_put(THIS_MODULE);
	printk("rbce_put_super called\n");
}

static struct super_operations rbce_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.put_super = rbce_put_super,
};

static int
rbce_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode * inode;
	struct dentry * root;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = RCFS_MAGIC;
	sb->s_op = &rbce_ops;
	inode = rbce_get_inode(sb, S_IFDIR | 0755, 0);
	if (!inode)
		return -ENOMEM;

	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root;

	return 0;
}

static struct super_block *
rbce_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	 struct super_block *sb = 
		 	get_sb_nodev(fs_type, flags, data, rbce_fill_super);
	 if (sb) {
		try_module_get(THIS_MODULE);
	 }
	 return sb;
}

static struct file_system_type rbce_fs_type = {
	.name		= "rbce",
	.get_sb		= rbce_get_sb,
	.kill_sb	= kill_litter_super,
};

static int
__init init_rbce_fs(void)
{
	return register_filesystem(&rbce_fs_type);
}

static void
__exit exit_rbce_fs(void)
{
	unregister_filesystem(&rbce_fs_type);
}

module_init(init_rbce_fs)
module_exit(exit_rbce_fs)
MODULE_LICENSE("GPL");
#endif

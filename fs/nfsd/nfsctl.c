/*
 * linux/fs/nfsd/nfsctl.c
 *
 * Syscall interface to knfsd.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>

#include <linux/linkage.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/pagemap.h>
#include <linux/init.h>

#include <linux/nfs.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfsd/xdr.h>
#include <linux/nfsd/syscall.h>
#include <linux/nfsd/interface.h>

#include <asm/uaccess.h>

/*
 *	We have a single directory with 8 nodes in it.
 */
enum {
	NFSD_Root = 1,
	NFSD_Svc,
	NFSD_Add,
	NFSD_Del,
	NFSD_Export,
	NFSD_Unexport,
	NFSD_Getfd,
	NFSD_Getfs,
	NFSD_List,
};

/*
 * write() for these nodes.
 */
static ssize_t write_svc(struct file *file, const char *buf, size_t size);
static ssize_t write_add(struct file *file, const char *buf, size_t size);
static ssize_t write_del(struct file *file, const char *buf, size_t size);
static ssize_t write_export(struct file *file, const char *buf, size_t size);
static ssize_t write_unexport(struct file *file, const char *buf, size_t size);
static ssize_t write_getfd(struct file *file, const char *buf, size_t size);
static ssize_t write_getfs(struct file *file, const char *buf, size_t size);

static ssize_t (*write_op[])(struct file *, const char *, size_t) = {
	[NFSD_Svc] = write_svc,
	[NFSD_Add] = write_add,
	[NFSD_Del] = write_del,
	[NFSD_Export] = write_export,
	[NFSD_Unexport] = write_unexport,
	[NFSD_Getfd] = write_getfd,
	[NFSD_Getfs] = write_getfs,
};

static ssize_t fs_write(struct file *file, const char *buf, size_t size, loff_t *pos)
{
	ino_t ino =  file->f_dentry->d_inode->i_ino;
	if (ino >= sizeof(write_op)/sizeof(write_op[0]) || !write_op[ino])
		return -EINVAL;
	return write_op[ino](file, buf, size);
}

/*
 * read(), open() and release() for getfs and getfd (read/write ones).
 * IO on these is a simple transaction - you open() the file, write() to it
 * and that generates a (stored) response.  After that read() will simply
 * access that response.
 */

static ssize_t TA_read(struct file *file, char *buf, size_t size, loff_t *pos)
{
	if (!file->private_data)
		return 0;
	if (*pos >= file->f_dentry->d_inode->i_size)
		return 0;
	if (*pos + size > file->f_dentry->d_inode->i_size)
		size = file->f_dentry->d_inode->i_size - *pos;
	if (copy_to_user(buf, file->private_data + *pos, size))
		return -EFAULT;
	*pos += size;
	return size;
}

static int TA_open(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static int TA_release(struct inode *inode, struct file *file)
{
	void *p = file->private_data;
	file->private_data = NULL;
	kfree(p);
	return 0;
}

static struct file_operations writer_ops = {
	.write	= fs_write,
};

static struct file_operations reader_ops = {
	.write		= fs_write,
	.read		= TA_read,
	.open		= TA_open,
	.release	= TA_release,
};

extern struct seq_operations nfs_exports_op;
static int exports_open(struct inode *inode, struct file *file)
{
	int res;
	res = seq_open(file, &nfs_exports_op);
	if (!res) {
		char *namebuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (namebuf == NULL)
			res = -ENOMEM;
		else
			((struct seq_file *)file->private_data)->private = namebuf;
	}
	return res;
}
static int exports_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = (struct seq_file *)file->private_data;
	kfree(m->private);
	m->private = NULL;
	return seq_release(inode, file);
}

static struct file_operations exports_operations = {
	.open		= exports_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= exports_release,
};

/*
 *	Description of fs contents.
 */
static struct { char *name; struct file_operations *ops; int mode; } files[] = {
	[NFSD_Svc] = {"svc", &writer_ops, S_IWUSR},
	[NFSD_Add] = {"add", &writer_ops, S_IWUSR},
	[NFSD_Del] = {"del", &writer_ops, S_IWUSR},
	[NFSD_Export] = {"export", &writer_ops, S_IWUSR},
	[NFSD_Unexport] = {"unexport", &writer_ops, S_IWUSR},
	[NFSD_Getfd] = {"getfd", &reader_ops, S_IWUSR|S_IRUSR},
	[NFSD_Getfs] = {"getfs", &reader_ops, S_IWUSR|S_IRUSR},
	[NFSD_List] = {"exports", &exports_operations, S_IRUGO},
};

/*----------------------------------------------------------------------------*/
/*
 * payload - write methods
 */

static ssize_t write_svc(struct file *file, const char *buf, size_t size)
{
	struct nfsctl_svc data;
	if (size < sizeof(data))
		return -EINVAL;
	if (copy_from_user(&data, buf, size))
		return -EFAULT;
	return nfsd_svc(data.svc_port, data.svc_nthreads);
}

static ssize_t write_add(struct file *file, const char *buf, size_t size)
{
	struct nfsctl_client data;
	if (size < sizeof(data))
		return -EINVAL;
	if (copy_from_user(&data, buf, size))
		return -EFAULT;
	return exp_addclient(&data);
}

static ssize_t write_del(struct file *file, const char *buf, size_t size)
{
	struct nfsctl_client data;
	if (size < sizeof(data))
		return -EINVAL;
	if (copy_from_user(&data, buf, size))
		return -EFAULT;
	return exp_delclient(&data);
}

static ssize_t write_export(struct file *file, const char *buf, size_t size)
{
	struct nfsctl_export data;
	if (size < sizeof(data))
		return -EINVAL;
	if (copy_from_user(&data, buf, size))
		return -EFAULT;
	return exp_export(&data);
}

static ssize_t write_unexport(struct file *file, const char *buf, size_t size)
{
	struct nfsctl_export data;
	if (size < sizeof(data))
		return -EINVAL;
	if (copy_from_user(&data, buf, size))
		return -EFAULT;
	return exp_unexport(&data);
}

static ssize_t write_getfs(struct file *file, const char *buf, size_t size)
{
	struct nfsctl_fsparm data;
	struct sockaddr_in *sin;
	struct svc_client *clp;
	int err = 0;
	struct knfsd_fh *res;

	if (file->private_data)
		return -EINVAL;
	if (size < sizeof(data))
		return -EINVAL;
	if (copy_from_user(&data, buf, size))
		return -EFAULT;
	if (data.gd_addr.sa_family != AF_INET)
		return -EPROTONOSUPPORT;
	sin = (struct sockaddr_in *)&data.gd_addr;
	if (data.gd_maxlen > NFS3_FHSIZE)
		data.gd_maxlen = NFS3_FHSIZE;
	res = kmalloc(sizeof(struct knfsd_fh), GFP_KERNEL);
	if (!res)
		return -ENOMEM;
	memset(res, 0, sizeof(struct knfsd_fh));
	exp_readlock();
	if (!(clp = exp_getclient(sin)))
		err = -EPERM;
	else
		err = exp_rootfh(clp, data.gd_path, res, data.gd_maxlen);
	exp_readunlock();

	down(&file->f_dentry->d_inode->i_sem);
	if (file->private_data)
		err = -EINVAL;
	if (err)
		kfree(res);
	else {
		file->f_dentry->d_inode->i_size = res->fh_size + (int)&((struct knfsd_fh*)0)->fh_base;
		file->private_data = res;
		err = sizeof(data);
	}
	up(&file->f_dentry->d_inode->i_sem);

	return err;
}

static ssize_t write_getfd(struct file *file, const char *buf, size_t size)
{
	struct nfsctl_fdparm data;
	struct sockaddr_in *sin;
	struct svc_client *clp;
	int err = 0;
	struct knfsd_fh fh;
	char *res;

	if (file->private_data)
		return -EINVAL;
	if (size < sizeof(data))
		return -EINVAL;
	if (copy_from_user(&data, buf, size))
		return -EFAULT;
	if (data.gd_addr.sa_family != AF_INET)
		return -EPROTONOSUPPORT;
	if (data.gd_version < 2 || data.gd_version > NFSSVC_MAXVERS)
		return -EINVAL;
	res = kmalloc(NFS_FHSIZE, GFP_KERNEL);
	if (!res)
		return -ENOMEM;
	sin = (struct sockaddr_in *)&data.gd_addr;
	exp_readlock();
	if (!(clp = exp_getclient(sin)))
		err = -EPERM;
	else
		err = exp_rootfh(clp, data.gd_path, &fh, NFS_FHSIZE);
	exp_readunlock();

	down(&file->f_dentry->d_inode->i_sem);
	if (file->private_data)
		err = -EINVAL;
	if (!err && fh.fh_size > NFS_FHSIZE)
		err = -EINVAL;
	if (err)
		kfree(res);
	else {
		memset(res,0, NFS_FHSIZE);
		memcpy(res, &fh.fh_base, fh.fh_size);
		file->f_dentry->d_inode->i_size = NFS_FHSIZE;
		file->private_data = res;
		err = sizeof(data);
	}
	up(&file->f_dentry->d_inode->i_sem);

	return err;
}

/*----------------------------------------------------------------------------*/
/*
 *	populating the filesystem.
 */

static struct super_operations s_ops = {
	.statfs		= simple_statfs,
};

static int nfsd_fill_super(struct super_block * sb, void * data, int silent)
{
	struct inode *inode;
	struct dentry *root;
	struct dentry *dentry;
	int i;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = 0x6e667364;
	sb->s_op = &s_ops;

	inode = new_inode(sb);
	if (!inode)
		return -ENOMEM;
	inode->i_mode = S_IFDIR | 0755;
	inode->i_uid = inode->i_gid = 0;
	inode->i_blksize = PAGE_CACHE_SIZE;
	inode->i_blocks = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	root = d_alloc_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	for (i = NFSD_Svc; i <= NFSD_List; i++) {
		struct qstr name;
		name.name = files[i].name;
		name.len = strlen(name.name);
		name.hash = full_name_hash(name.name, name.len);
		dentry = d_alloc(root, &name);
		if (!dentry)
			goto out;
		inode = new_inode(sb);
		if (!inode)
			goto out;
		inode->i_mode = S_IFREG | files[i].mode;
		inode->i_uid = inode->i_gid = 0;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_fop = files[i].ops;
		inode->i_ino = i;
		d_add(dentry, inode);
	}
	sb->s_root = root;
	return 0;

out:
	d_genocide(root);
	dput(root);
	return -ENOMEM;
}

static struct super_block *nfsd_get_sb(struct file_system_type *fs_type,
	int flags, char *dev_name, void *data)
{
	return get_sb_single(fs_type, flags, data, nfsd_fill_super);
}

static struct file_system_type nfsd_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfsd",
	.get_sb		= nfsd_get_sb,
	.kill_sb	= kill_litter_super,
};

static int __init init_nfsd(void)
{
	printk(KERN_INFO "Installing knfsd (copyright (C) 1996 okir@monad.swb.de).\n");

	nfsd_stat_init();	/* Statistics */
	nfsd_cache_init();	/* RPC reply cache */
	nfsd_export_init();	/* Exports table */
	nfsd_lockd_init();	/* lockd->nfsd callbacks */
	if (proc_mkdir("fs/nfs", 0)) {
		struct proc_dir_entry *entry;
		entry = create_proc_entry("fs/nfs/exports", 0, NULL);
		if (entry)
			entry->proc_fops =  &exports_operations;
	}
	register_filesystem(&nfsd_fs_type);
	return 0;
}

static void __exit exit_nfsd(void)
{
	nfsd_export_shutdown();
	nfsd_cache_shutdown();
	remove_proc_entry("fs/nfs/exports", NULL);
	remove_proc_entry("fs/nfs", NULL);
	nfsd_stat_shutdown();
	nfsd_lockd_shutdown();
	unregister_filesystem(&nfsd_fs_type);
}

MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");
MODULE_LICENSE("GPL");
module_init(init_nfsd)
module_exit(exit_nfsd)

/*
 * linux/fs/nfsd/nfsctl.c
 *
 * Syscall interface to knfsd.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/linkage.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/pagemap.h>
#include <linux/init.h>

#include <linux/nfs.h>
#include <linux/nfsd_idmap.h>
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
	NFSD_Fh,
	NFSD_Threads,
};

/*
 * write() for these nodes.
 */
static ssize_t write_svc(struct file *file, char *buf, size_t size);
static ssize_t write_add(struct file *file, char *buf, size_t size);
static ssize_t write_del(struct file *file, char *buf, size_t size);
static ssize_t write_export(struct file *file, char *buf, size_t size);
static ssize_t write_unexport(struct file *file, char *buf, size_t size);
static ssize_t write_getfd(struct file *file, char *buf, size_t size);
static ssize_t write_getfs(struct file *file, char *buf, size_t size);
static ssize_t write_filehandle(struct file *file, char *buf, size_t size);
static ssize_t write_threads(struct file *file, char *buf, size_t size);

static ssize_t (*write_op[])(struct file *, char *, size_t) = {
	[NFSD_Svc] = write_svc,
	[NFSD_Add] = write_add,
	[NFSD_Del] = write_del,
	[NFSD_Export] = write_export,
	[NFSD_Unexport] = write_unexport,
	[NFSD_Getfd] = write_getfd,
	[NFSD_Getfs] = write_getfs,
	[NFSD_Fh] = write_filehandle,
	[NFSD_Threads] = write_threads,
};

/* an argresp is stored in an allocated page and holds the 
 * size of the argument or response, along with its content
 */
struct argresp {
	ssize_t size;
	char data[0];
};

/*
 * transaction based IO methods.
 * The file expects a single write which triggers the transaction, and then
 * possibly a read which collects the result - which is stored in a 
 * file-local buffer.
 */
static ssize_t TA_write(struct file *file, const char __user *buf, size_t size, loff_t *pos)
{
	ino_t ino =  file->f_dentry->d_inode->i_ino;
	struct argresp *ar;
	ssize_t rv = 0;

	if (ino >= sizeof(write_op)/sizeof(write_op[0]) || !write_op[ino])
		return -EINVAL;
	if (file->private_data) 
		return -EINVAL; /* only one write allowed per open */
	if (size > PAGE_SIZE - sizeof(struct argresp))
		return -EFBIG;

	ar = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!ar)
		return -ENOMEM;
	ar->size = 0;
	down(&file->f_dentry->d_inode->i_sem);
	if (file->private_data)
		rv = -EINVAL;
	else
		file->private_data = ar;
	up(&file->f_dentry->d_inode->i_sem);
	if (rv) {
		kfree(ar);
		return rv;
	}
	if (copy_from_user(ar->data, buf, size))
		return -EFAULT;
	
	rv =  write_op[ino](file, ar->data, size);
	if (rv>0) {
		ar->size = rv;
		rv = size;
	}
	return rv;
}


static ssize_t TA_read(struct file *file, char __user *buf, size_t size, loff_t *pos)
{
	struct argresp *ar;
	ssize_t rv = 0;
	
	if (file->private_data == NULL)
		rv = TA_write(file, buf, 0, pos);
	if (rv < 0)
		return rv;

	ar = file->private_data;
	if (!ar)
		return 0;
	if (*pos >= ar->size)
		return 0;
	if (*pos + size > ar->size)
		size = ar->size - *pos;
	if (copy_to_user(buf, ar->data + *pos, size))
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

static struct file_operations transaction_ops = {
	.write		= TA_write,
	.read		= TA_read,
	.open		= TA_open,
	.release	= TA_release,
};

extern struct seq_operations nfs_exports_op;
static int exports_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &nfs_exports_op);
}

static struct file_operations exports_operations = {
	.open		= exports_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

/*----------------------------------------------------------------------------*/
/*
 * payload - write methods
 * If the method has a response, the response should be put in buf,
 * and the length returned.  Otherwise return 0 or and -error.
 */

static ssize_t write_svc(struct file *file, char *buf, size_t size)
{
	struct nfsctl_svc *data;
	if (size < sizeof(*data))
		return -EINVAL;
	data = (struct nfsctl_svc*) buf;
	return nfsd_svc(data->svc_port, data->svc_nthreads);
}

static ssize_t write_add(struct file *file, char *buf, size_t size)
{
	struct nfsctl_client *data;
	if (size < sizeof(*data))
		return -EINVAL;
	data = (struct nfsctl_client *)buf;
	return exp_addclient(data);
}

static ssize_t write_del(struct file *file, char *buf, size_t size)
{
	struct nfsctl_client *data;
	if (size < sizeof(*data))
		return -EINVAL;
	data = (struct nfsctl_client *)buf;
	return exp_delclient(data);
}

static ssize_t write_export(struct file *file, char *buf, size_t size)
{
	struct nfsctl_export *data;
	if (size < sizeof(*data))
		return -EINVAL;
	data = (struct nfsctl_export*)buf;
	return exp_export(data);
}

static ssize_t write_unexport(struct file *file, char *buf, size_t size)
{
	struct nfsctl_export *data;

	if (size < sizeof(*data))
		return -EINVAL;
	data = (struct nfsctl_export*)buf;
	return exp_unexport(data);
}

static ssize_t write_getfs(struct file *file, char *buf, size_t size)
{
	struct nfsctl_fsparm *data;
	struct sockaddr_in *sin;
	struct auth_domain *clp;
	int err = 0;
	struct knfsd_fh *res;

	if (size < sizeof(*data))
		return -EINVAL;
	data = (struct nfsctl_fsparm*)buf;
	err = -EPROTONOSUPPORT;
	if (data->gd_addr.sa_family != AF_INET)
		goto out;
	sin = (struct sockaddr_in *)&data->gd_addr;
	if (data->gd_maxlen > NFS3_FHSIZE)
		data->gd_maxlen = NFS3_FHSIZE;

	res = (struct knfsd_fh*)buf;

	exp_readlock();
	if (!(clp = auth_unix_lookup(sin->sin_addr)))
		err = -EPERM;
	else {
		err = exp_rootfh(clp, data->gd_path, res, data->gd_maxlen);
		auth_domain_put(clp);
	}
	exp_readunlock();
	if (err == 0)
		err = res->fh_size + (int)&((struct knfsd_fh*)0)->fh_base;
 out:
	return err;
}

static ssize_t write_getfd(struct file *file, char *buf, size_t size)
{
	struct nfsctl_fdparm *data;
	struct sockaddr_in *sin;
	struct auth_domain *clp;
	int err = 0;
	struct knfsd_fh fh;
	char *res;

	if (size < sizeof(*data))
		return -EINVAL;
	data = (struct nfsctl_fdparm*)buf;
	err = -EPROTONOSUPPORT;
	if (data->gd_addr.sa_family != AF_INET)
		goto out;
	err = -EINVAL;
	if (data->gd_version < 2 || data->gd_version > NFSSVC_MAXVERS)
		goto out;

	res = buf;
	sin = (struct sockaddr_in *)&data->gd_addr;
	exp_readlock();
	if (!(clp = auth_unix_lookup(sin->sin_addr)))
		err = -EPERM;
	else {
		err = exp_rootfh(clp, data->gd_path, &fh, NFS_FHSIZE);
		auth_domain_put(clp);
	}
	exp_readunlock();

	if (err == 0) {
		memset(res,0, NFS_FHSIZE);
		memcpy(res, &fh.fh_base, fh.fh_size);
		err = NFS_FHSIZE;
	}
 out:
	return err;
}

static ssize_t write_filehandle(struct file *file, char *buf, size_t size)
{
	/* request is:
	 *   domain path maxsize
	 * response is
	 *   filehandle
	 *
	 * qword quoting is used, so filehandle will be \x....
	 */
	char *dname, *path;
	int maxsize;
	char *mesg = buf;
	int len;
	struct auth_domain *dom;
	struct knfsd_fh fh;

	if (buf[size-1] != '\n')
		return -EINVAL;
	buf[size-1] = 0;

	dname = mesg;
	len = qword_get(&mesg, dname, size);
	if (len <= 0) return -EINVAL;
	
	path = dname+len+1;
	len = qword_get(&mesg, path, size);
	if (len <= 0) return -EINVAL;

	len = get_int(&mesg, &maxsize);
	if (len)
		return len;

	if (maxsize < NFS_FHSIZE)
		return -EINVAL;
	if (maxsize > NFS3_FHSIZE)
		maxsize = NFS3_FHSIZE;

	if (qword_get(&mesg, mesg, size)>0)
		return -EINVAL;

	/* we have all the words, they are in buf.. */
	dom = unix_domain_find(dname);
	if (!dom)
		return -ENOMEM;

	len = exp_rootfh(dom, path, &fh,  maxsize);
	auth_domain_put(dom);
	if (len)
		return len;
	
	mesg = buf; len = PAGE_SIZE-sizeof(struct argresp);
	qword_addhex(&mesg, &len, (char*)&fh.fh_base, fh.fh_size);
	mesg[-1] = '\n';
	return mesg - buf;	
}

extern int nfsd_nrthreads(void);

static ssize_t write_threads(struct file *file, char *buf, size_t size)
{
	/* if size > 0, look for a number of threads and call nfsd_svc
	 * then write out number of threads as reply
	 */
	char *mesg = buf;
	int rv;
	if (size > 0) {
		int newthreads;
		rv = get_int(&mesg, &newthreads);
		if (rv)
			return rv;
		if (newthreads <0)
			return -EINVAL;
		rv = nfsd_svc(2049, newthreads);
		if (rv)
			return rv;
	}
	sprintf(buf, "%d\n", nfsd_nrthreads());
	return strlen(buf);
}

/*----------------------------------------------------------------------------*/
/*
 *	populating the filesystem.
 */

static int nfsd_fill_super(struct super_block * sb, void * data, int silent)
{
	static struct tree_descr nfsd_files[] = {
		[NFSD_Svc] = {".svc", &transaction_ops, S_IWUSR},
		[NFSD_Add] = {".add", &transaction_ops, S_IWUSR},
		[NFSD_Del] = {".del", &transaction_ops, S_IWUSR},
		[NFSD_Export] = {".export", &transaction_ops, S_IWUSR},
		[NFSD_Unexport] = {".unexport", &transaction_ops, S_IWUSR},
		[NFSD_Getfd] = {".getfd", &transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_Getfs] = {".getfs", &transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_List] = {"exports", &exports_operations, S_IRUGO},
		[NFSD_Fh] = {"filehandle", &transaction_ops, S_IWUSR|S_IRUSR},
		[NFSD_Threads] = {"threads", &transaction_ops, S_IWUSR|S_IRUSR},
		/* last one */ {""}
	};
	return simple_fill_super(sb, 0x6e667364, nfsd_files);
}

static struct super_block *nfsd_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
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
	int retval;
	printk(KERN_INFO "Installing knfsd (copyright (C) 1996 okir@monad.swb.de).\n");

	nfsd_stat_init();	/* Statistics */
	nfsd_cache_init();	/* RPC reply cache */
	nfsd_export_init();	/* Exports table */
	nfsd_lockd_init();	/* lockd->nfsd callbacks */
#ifdef CONFIG_NFSD_V4
	nfsd_idmap_init();      /* Name to ID mapping */
#endif /* CONFIG_NFSD_V4 */
	if (proc_mkdir("fs/nfs", 0)) {
		struct proc_dir_entry *entry;
		entry = create_proc_entry("fs/nfs/exports", 0, NULL);
		if (entry)
			entry->proc_fops =  &exports_operations;
	}
	retval = register_filesystem(&nfsd_fs_type);
	if (retval) {
		nfsd_export_shutdown();
		nfsd_cache_shutdown();
		remove_proc_entry("fs/nfs/exports", NULL);
		remove_proc_entry("fs/nfs", NULL);
		nfsd_stat_shutdown();
		nfsd_lockd_shutdown();
	}
	return retval;
}

static void __exit exit_nfsd(void)
{
	nfsd_export_shutdown();
	nfsd_cache_shutdown();
	remove_proc_entry("fs/nfs/exports", NULL);
	remove_proc_entry("fs/nfs", NULL);
	nfsd_stat_shutdown();
	nfsd_lockd_shutdown();
#ifdef CONFIG_NFSD_V4
	nfsd_idmap_shutdown();
#endif /* CONFIG_NFSD_V4 */
	unregister_filesystem(&nfsd_fs_type);
}

MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");
MODULE_LICENSE("GPL");
module_init(init_nfsd)
module_exit(exit_nfsd)

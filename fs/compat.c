/*
 *  linux/fs/compat.c
 *
 *  Kernel compatibililty routines for e.g. 32 bit syscall support
 *  on 64 bit kernels.
 *
 *  Copyright (C) 2002       Stephen Rothwell, IBM Corporation
 *  Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 *  Copyright (C) 1998       Eddie C. Dost  (ecd@skynet.be)
 *  Copyright (C) 2001,2002  Andi Kleen, SuSE Labs 
 *  Copyright (C) 2003       Pavel Machek (pavel@suse.cz)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/vfs.h>
#include <linux/ioctl32.h>
#include <linux/init.h>
#include <linux/sockios.h>	/* for SIOCDEVPRIVATE */
#include <linux/smb.h>
#include <linux/smb_mount.h>
#include <linux/ncp_mount.h>
#include <linux/smp_lock.h>
#include <linux/syscalls.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <net/sock.h>		/* siocdevprivate_ioctl */

#include <asm/uaccess.h>

/*
 * Not all architectures have sys_utime, so implement this in terms
 * of sys_utimes.
 */
asmlinkage long compat_sys_utime(char *filename, struct compat_utimbuf *t)
{
	struct timeval tv[2];

	if (t) {
		if (get_user(tv[0].tv_sec, &t->actime) ||
		    get_user(tv[1].tv_sec, &t->modtime))
			return -EFAULT;
		tv[0].tv_usec = 0;
		tv[1].tv_usec = 0;
	}
	return do_utimes(filename, t ? tv : NULL);
}

asmlinkage long compat_sys_utimes(char *filename, struct compat_timeval *t)
{
	struct timeval tv[2];

	if (t) { 
		if (get_user(tv[0].tv_sec, &t[0].tv_sec) ||
		    get_user(tv[0].tv_usec, &t[0].tv_usec) ||
		    get_user(tv[1].tv_sec, &t[1].tv_sec) ||
		    get_user(tv[1].tv_usec, &t[1].tv_usec))
			return -EFAULT; 
	} 
	return do_utimes(filename, t ? tv : NULL);
}

asmlinkage long compat_sys_newstat(char * filename,
		struct compat_stat *statbuf)
{
	struct kstat stat;
	int error = vfs_stat(filename, &stat);

	if (!error)
		error = cp_compat_stat(&stat, statbuf);
	return error;
}

asmlinkage long compat_sys_newlstat(char * filename,
		struct compat_stat *statbuf)
{
	struct kstat stat;
	int error = vfs_lstat(filename, &stat);

	if (!error)
		error = cp_compat_stat(&stat, statbuf);
	return error;
}

asmlinkage long compat_sys_newfstat(unsigned int fd,
		struct compat_stat * statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_compat_stat(&stat, statbuf);
	return error;
}

static int put_compat_statfs(struct compat_statfs *ubuf, struct kstatfs *kbuf)
{
	
	if (sizeof ubuf->f_blocks == 4) {
		if ((kbuf->f_blocks | kbuf->f_bfree |
		     kbuf->f_bavail | kbuf->f_files | kbuf->f_ffree) &
		    0xffffffff00000000ULL)
			return -EOVERFLOW;
	}
	if (verify_area(VERIFY_WRITE, ubuf, sizeof(*ubuf)) ||
	    __put_user(kbuf->f_type, &ubuf->f_type) ||
	    __put_user(kbuf->f_bsize, &ubuf->f_bsize) ||
	    __put_user(kbuf->f_blocks, &ubuf->f_blocks) ||
	    __put_user(kbuf->f_bfree, &ubuf->f_bfree) ||
	    __put_user(kbuf->f_bavail, &ubuf->f_bavail) ||
	    __put_user(kbuf->f_files, &ubuf->f_files) ||
	    __put_user(kbuf->f_ffree, &ubuf->f_ffree) ||
	    __put_user(kbuf->f_namelen, &ubuf->f_namelen) ||
	    __put_user(kbuf->f_fsid.val[0], &ubuf->f_fsid.val[0]) ||
	    __put_user(kbuf->f_fsid.val[1], &ubuf->f_fsid.val[1]) ||
	    __put_user(kbuf->f_frsize, &ubuf->f_frsize) ||
	    __put_user(0, &ubuf->f_spare[0]) || 
	    __put_user(0, &ubuf->f_spare[1]) || 
	    __put_user(0, &ubuf->f_spare[2]) || 
	    __put_user(0, &ubuf->f_spare[3]) || 
	    __put_user(0, &ubuf->f_spare[4]))
		return -EFAULT;
	return 0;
}

/*
 * The following statfs calls are copies of code from fs/open.c and
 * should be checked against those from time to time
 */
asmlinkage long compat_sys_statfs(const char *path, struct compat_statfs *buf)
{
	struct nameidata nd;
	int error;

	error = user_path_walk(path, &nd);
	if (!error) {
		struct kstatfs tmp;
		error = vfs_statfs(nd.dentry->d_inode->i_sb, &tmp);
		if (!error && put_compat_statfs(buf, &tmp))
			error = -EFAULT;
		path_release(&nd);
	}
	return error;
}

asmlinkage long compat_sys_fstatfs(unsigned int fd, struct compat_statfs *buf)
{
	struct file * file;
	struct kstatfs tmp;
	int error;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;
	error = vfs_statfs(file->f_dentry->d_inode->i_sb, &tmp);
	if (!error && put_compat_statfs(buf, &tmp))
		error = -EFAULT;
	fput(file);
out:
	return error;
}

static int put_compat_statfs64(struct compat_statfs64 *ubuf, struct kstatfs *kbuf)
{
	if (sizeof ubuf->f_blocks == 4) {
		if ((kbuf->f_blocks | kbuf->f_bfree |
		     kbuf->f_bavail | kbuf->f_files | kbuf->f_ffree) &
		    0xffffffff00000000ULL)
			return -EOVERFLOW;
	}
	if (verify_area(VERIFY_WRITE, ubuf, sizeof(*ubuf)) ||
	    __put_user(kbuf->f_type, &ubuf->f_type) ||
	    __put_user(kbuf->f_bsize, &ubuf->f_bsize) ||
	    __put_user(kbuf->f_blocks, &ubuf->f_blocks) ||
	    __put_user(kbuf->f_bfree, &ubuf->f_bfree) ||
	    __put_user(kbuf->f_bavail, &ubuf->f_bavail) ||
	    __put_user(kbuf->f_files, &ubuf->f_files) ||
	    __put_user(kbuf->f_ffree, &ubuf->f_ffree) ||
	    __put_user(kbuf->f_namelen, &ubuf->f_namelen) ||
	    __put_user(kbuf->f_fsid.val[0], &ubuf->f_fsid.val[0]) ||
	    __put_user(kbuf->f_fsid.val[1], &ubuf->f_fsid.val[1]) ||
	    __put_user(kbuf->f_frsize, &ubuf->f_frsize))
		return -EFAULT;
	return 0;
}

asmlinkage long compat_statfs64(const char *path, compat_size_t sz, struct compat_statfs64 *buf)
{
	struct nameidata nd;
	int error;

	if (sz != sizeof(*buf))
		return -EINVAL;

	error = user_path_walk(path, &nd);
	if (!error) {
		struct kstatfs tmp;
		error = vfs_statfs(nd.dentry->d_inode->i_sb, &tmp);
		if (!error && put_compat_statfs64(buf, &tmp))
			error = -EFAULT;
		path_release(&nd);
	}
	return error;
}

asmlinkage long compat_fstatfs64(unsigned int fd, compat_size_t sz, struct compat_statfs64 *buf)
{
	struct file * file;
	struct kstatfs tmp;
	int error;

	if (sz != sizeof(*buf))
		return -EINVAL;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;
	error = vfs_statfs(file->f_dentry->d_inode->i_sb, &tmp);
	if (!error && put_compat_statfs64(buf, &tmp))
		error = -EFAULT;
	fput(file);
out:
	return error;
}

/* ioctl32 stuff, used by sparc64, parisc, s390x, ppc64, x86_64, MIPS */

#define IOCTL_HASHSIZE 256
struct ioctl_trans *ioctl32_hash_table[IOCTL_HASHSIZE];

extern struct ioctl_trans ioctl_start[];
extern int ioctl_table_size;

static inline unsigned long ioctl32_hash(unsigned long cmd)
{
	return (((cmd >> 6) ^ (cmd >> 4) ^ cmd)) % IOCTL_HASHSIZE;
}

static void ioctl32_insert_translation(struct ioctl_trans *trans)
{
	unsigned long hash;
	struct ioctl_trans *t;

	hash = ioctl32_hash (trans->cmd);
	if (!ioctl32_hash_table[hash])
		ioctl32_hash_table[hash] = trans;
	else {
		t = ioctl32_hash_table[hash];
		while (t->next)
			t = t->next;
		trans->next = 0;
		t->next = trans;
	}
}

static int __init init_sys32_ioctl(void)
{
	int i;

	for (i = 0; i < ioctl_table_size; i++) {
		if (ioctl_start[i].next != 0) { 
			printk("ioctl translation %d bad\n",i); 
			return -1;
		}

		ioctl32_insert_translation(&ioctl_start[i]);
	}
	return 0;
}

__initcall(init_sys32_ioctl);

int register_ioctl32_conversion(unsigned int cmd, int (*handler)(unsigned int,
				unsigned int, unsigned long, struct file *))
{
	struct ioctl_trans *t;
	struct ioctl_trans *new_t;
	unsigned long hash = ioctl32_hash(cmd);

	new_t = kmalloc(sizeof(*new_t), GFP_KERNEL);
	if (!new_t)
		return -ENOMEM;

	lock_kernel(); 
	for (t = ioctl32_hash_table[hash]; t; t = t->next) {
		if (t->cmd == cmd) {
			printk(KERN_ERR "Trying to register duplicated ioctl32 "
					"handler %x\n", cmd);
			unlock_kernel();
			kfree(new_t);
			return -EINVAL; 
		}
	}
	new_t->next = NULL;
	new_t->cmd = cmd;
	new_t->handler = handler;
	ioctl32_insert_translation(new_t);

	unlock_kernel();
	return 0;
}
EXPORT_SYMBOL(register_ioctl32_conversion);

static inline int builtin_ioctl(struct ioctl_trans *t)
{ 
	return t >= ioctl_start && t < (ioctl_start + ioctl_table_size);
} 

/* Problem: 
   This function cannot unregister duplicate ioctls, because they are not
   unique.
   When they happen we need to extend the prototype to pass the handler too. */

int unregister_ioctl32_conversion(unsigned int cmd)
{
	unsigned long hash = ioctl32_hash(cmd);
	struct ioctl_trans *t, *t1;

	lock_kernel(); 

	t = ioctl32_hash_table[hash];
	if (!t) { 
		unlock_kernel();
		return -EINVAL;
	} 

	if (t->cmd == cmd) { 
		if (builtin_ioctl(t)) {
			printk("%p tried to unregister builtin ioctl %x\n",
			       __builtin_return_address(0), cmd);
		} else { 
			ioctl32_hash_table[hash] = t->next;
			unlock_kernel();
			kfree(t);
			return 0;
		}
	} 
	while (t->next) {
		t1 = t->next;
		if (t1->cmd == cmd) { 
			if (builtin_ioctl(t1)) {
				printk("%p tried to unregister builtin "
					"ioctl %x\n",
					__builtin_return_address(0), cmd);
				goto out;
			} else { 
				t->next = t1->next;
				unlock_kernel();
				kfree(t1);
				return 0;
			}
		}
		t = t1;
	}
	printk(KERN_ERR "Trying to free unknown 32bit ioctl handler %x\n",
				cmd);
out:
	unlock_kernel();
	return -EINVAL;
}
EXPORT_SYMBOL(unregister_ioctl32_conversion); 

asmlinkage long compat_sys_ioctl(unsigned int fd, unsigned int cmd,
				unsigned long arg)
{
	struct file * filp;
	int error = -EBADF;
	struct ioctl_trans *t;

	filp = fget(fd);
	if(!filp)
		goto out2;

	if (!filp->f_op || !filp->f_op->ioctl) {
		error = sys_ioctl (fd, cmd, arg);
		goto out;
	}

	lock_kernel();

	t = ioctl32_hash_table[ioctl32_hash (cmd)];

	while (t && t->cmd != cmd)
		t = t->next;
	if (t) {
		if (t->handler) { 
			error = t->handler(fd, cmd, arg, filp);
			unlock_kernel();
		} else {
			unlock_kernel();
			error = sys_ioctl(fd, cmd, arg);
		}
	} else {
		unlock_kernel();
		if (cmd >= SIOCDEVPRIVATE && cmd <= (SIOCDEVPRIVATE + 15)) {
			error = siocdevprivate_ioctl(fd, cmd, arg);
		} else {
			static int count;
			if (++count <= 50) {
				char buf[10];
				char *fn = "?";
				char *path;

				path = (char *)__get_free_page(GFP_KERNEL);

				/* find the name of the device. */
				if (path) {
			       		fn = d_path(filp->f_dentry,
						filp->f_vfsmnt, path,
						PAGE_SIZE);
				}

				sprintf(buf,"'%c'", (cmd>>24) & 0x3f);
				if (!isprint(buf[1]))
				    sprintf(buf, "%02x", buf[1]);
				printk("ioctl32(%s:%d): Unknown cmd fd(%d) "
					"cmd(%08x){%s} arg(%08x) on %s\n",
					current->comm, current->pid,
					(int)fd, (unsigned int)cmd, buf,
					(unsigned int)arg, fn);
				if (path)
					free_page((unsigned long)path);
			}
			error = -EINVAL;
		}
	}
out:
	fput(filp);
out2:
	return error;
}

static int get_compat_flock(struct flock *kfl, struct compat_flock *ufl)
{
	if (!access_ok(VERIFY_READ, ufl, sizeof(*ufl)) ||
	    __get_user(kfl->l_type, &ufl->l_type) ||
	    __get_user(kfl->l_whence, &ufl->l_whence) ||
	    __get_user(kfl->l_start, &ufl->l_start) ||
	    __get_user(kfl->l_len, &ufl->l_len) ||
	    __get_user(kfl->l_pid, &ufl->l_pid))
		return -EFAULT;
	return 0;
}

static int put_compat_flock(struct flock *kfl, struct compat_flock *ufl)
{
	if (!access_ok(VERIFY_WRITE, ufl, sizeof(*ufl)) ||
	    __put_user(kfl->l_type, &ufl->l_type) ||
	    __put_user(kfl->l_whence, &ufl->l_whence) ||
	    __put_user(kfl->l_start, &ufl->l_start) ||
	    __put_user(kfl->l_len, &ufl->l_len) ||
	    __put_user(kfl->l_pid, &ufl->l_pid))
		return -EFAULT;
	return 0;
}

#ifndef HAVE_ARCH_GET_COMPAT_FLOCK64
static int get_compat_flock64(struct flock *kfl, struct compat_flock64 *ufl)
{
	if (!access_ok(VERIFY_READ, ufl, sizeof(*ufl)) ||
	    __get_user(kfl->l_type, &ufl->l_type) ||
	    __get_user(kfl->l_whence, &ufl->l_whence) ||
	    __get_user(kfl->l_start, &ufl->l_start) ||
	    __get_user(kfl->l_len, &ufl->l_len) ||
	    __get_user(kfl->l_pid, &ufl->l_pid))
		return -EFAULT;
	return 0;
}
#endif

#ifndef HAVE_ARCH_PUT_COMPAT_FLOCK64
static int put_compat_flock64(struct flock *kfl, struct compat_flock64 *ufl)
{
	if (!access_ok(VERIFY_WRITE, ufl, sizeof(*ufl)) ||
	    __put_user(kfl->l_type, &ufl->l_type) ||
	    __put_user(kfl->l_whence, &ufl->l_whence) ||
	    __put_user(kfl->l_start, &ufl->l_start) ||
	    __put_user(kfl->l_len, &ufl->l_len) ||
	    __put_user(kfl->l_pid, &ufl->l_pid))
		return -EFAULT;
	return 0;
}
#endif

asmlinkage long compat_sys_fcntl64(unsigned int fd, unsigned int cmd,
		unsigned long arg)
{
	mm_segment_t old_fs;
	struct flock f;
	long ret;

	switch (cmd) {
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
		ret = get_compat_flock(&f, compat_ptr(arg));
		if (ret != 0)
			break;
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = sys_fcntl(fd, cmd, (unsigned long)&f);
		set_fs(old_fs);
		if ((cmd == F_GETLK) && (ret == 0)) {
			if ((f.l_start >= COMPAT_OFF_T_MAX) ||
			    ((f.l_start + f.l_len) >= COMPAT_OFF_T_MAX))
				ret = -EOVERFLOW;
			if (ret == 0)
				ret = put_compat_flock(&f, compat_ptr(arg));
		}
		break;

	case F_GETLK64:
	case F_SETLK64:
	case F_SETLKW64:
		ret = get_compat_flock64(&f, compat_ptr(arg));
		if (ret != 0)
			break;
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = sys_fcntl(fd, (cmd == F_GETLK64) ? F_GETLK :
				((cmd == F_SETLK64) ? F_SETLK : F_SETLKW),
				(unsigned long)&f);
		set_fs(old_fs);
		if ((cmd == F_GETLK64) && (ret == 0)) {
			if ((f.l_start >= COMPAT_LOFF_T_MAX) ||
			    ((f.l_start + f.l_len) >= COMPAT_LOFF_T_MAX))
				ret = -EOVERFLOW;
			if (ret == 0)
				ret = put_compat_flock64(&f, compat_ptr(arg));
		}
		break;

	default:
		ret = sys_fcntl(fd, cmd, arg);
		break;
	}
	return ret;
}

asmlinkage long compat_sys_fcntl(unsigned int fd, unsigned int cmd,
		unsigned long arg)
{
	if ((cmd == F_GETLK64) || (cmd == F_SETLK64) || (cmd == F_SETLKW64))
		return -EINVAL;
	return compat_sys_fcntl64(fd, cmd, arg);
}

asmlinkage long
compat_sys_io_setup(unsigned nr_reqs, u32 *ctx32p)
{
	long ret;
	aio_context_t ctx64;

	mm_segment_t oldfs = get_fs();
	if (unlikely(get_user(ctx64, ctx32p)))
		return -EFAULT;

	set_fs(KERNEL_DS);
	ret = sys_io_setup(nr_reqs, &ctx64);
	set_fs(oldfs);
	/* truncating is ok because it's a user address */
	if (!ret)
		ret = put_user((u32) ctx64, ctx32p);
	return ret;
}

asmlinkage long
compat_sys_io_getevents(aio_context_t ctx_id,
				 unsigned long min_nr,
				 unsigned long nr,
				 struct io_event *events,
				 struct compat_timespec *timeout)
{
	long ret;
	struct timespec t;
	struct timespec *ut = NULL;

	ret = -EFAULT;
	if (unlikely(!access_ok(VERIFY_WRITE, events, 
				nr * sizeof(struct io_event))))
		goto out;
	if (timeout) {
		if (get_compat_timespec(&t, timeout))
			goto out;

		ut = compat_alloc_user_space(sizeof(*ut));
		if (copy_to_user(ut, &t, sizeof(t)) )
			goto out;
	} 
	ret = sys_io_getevents(ctx_id, min_nr, nr, events, ut);
out:
	return ret;
}

static inline long
copy_iocb(long nr, u32 *ptr32, u64 *ptr64)
{
	compat_uptr_t uptr;
	int i;

	for (i = 0; i < nr; ++i) {
		if (get_user(uptr, ptr32 + i))
			return -EFAULT;
		if (put_user((u64)compat_ptr(uptr), ptr64 + i))
			return -EFAULT;
	}
	return 0;
}

#define MAX_AIO_SUBMITS 	(PAGE_SIZE/sizeof(struct iocb *))

asmlinkage long
compat_sys_io_submit(aio_context_t ctx_id, int nr, u32 *iocb)
{
	struct iocb **iocb64; 
	long ret;

	if (unlikely(nr < 0))
		return -EINVAL;

	if (nr > MAX_AIO_SUBMITS)
		nr = MAX_AIO_SUBMITS;
	
	iocb64 = compat_alloc_user_space(nr * sizeof(*iocb64));
	ret = copy_iocb(nr, iocb, (u64 *) iocb64);
	if (!ret)
		ret = sys_io_submit(ctx_id, nr, iocb64);
	return ret;
}

struct compat_ncp_mount_data {
	compat_int_t version;
	compat_uint_t ncp_fd;
	compat_uid_t mounted_uid;
	compat_pid_t wdog_pid;
	unsigned char mounted_vol[NCP_VOLNAME_LEN + 1];
	compat_uint_t time_out;
	compat_uint_t retry_count;
	compat_uint_t flags;
	compat_uid_t uid;
	compat_gid_t gid;
	compat_mode_t file_mode;
	compat_mode_t dir_mode;
};

struct compat_ncp_mount_data_v4 {
	compat_int_t version;
	compat_ulong_t flags;
	compat_ulong_t mounted_uid;
	compat_long_t wdog_pid;
	compat_uint_t ncp_fd;
	compat_uint_t time_out;
	compat_uint_t retry_count;
	compat_ulong_t uid;
	compat_ulong_t gid;
	compat_ulong_t file_mode;
	compat_ulong_t dir_mode;
};

static void *do_ncp_super_data_conv(void *raw_data)
{
	int version = *(unsigned int *)raw_data;

	if (version == 3) {
		struct compat_ncp_mount_data *c_n = raw_data;
		struct ncp_mount_data *n = raw_data;

		n->dir_mode = c_n->dir_mode;
		n->file_mode = c_n->file_mode;
		n->gid = c_n->gid;
		n->uid = c_n->uid;
		memmove (n->mounted_vol, c_n->mounted_vol, (sizeof (c_n->mounted_vol) + 3 * sizeof (unsigned int)));
		n->wdog_pid = c_n->wdog_pid;
		n->mounted_uid = c_n->mounted_uid;
	} else if (version == 4) {
		struct compat_ncp_mount_data_v4 *c_n = raw_data;
		struct ncp_mount_data_v4 *n = raw_data;

		n->dir_mode = c_n->dir_mode;
		n->file_mode = c_n->file_mode;
		n->gid = c_n->gid;
		n->uid = c_n->uid;
		n->retry_count = c_n->retry_count;
		n->time_out = c_n->time_out;
		n->ncp_fd = c_n->ncp_fd;
		n->wdog_pid = c_n->wdog_pid;
		n->mounted_uid = c_n->mounted_uid;
		n->flags = c_n->flags;
	} else if (version != 5) {
		return NULL;
	}

	return raw_data;
}

struct compat_smb_mount_data {
	compat_int_t version;
	compat_uid_t mounted_uid;
	compat_uid_t uid;
	compat_gid_t gid;
	compat_mode_t file_mode;
	compat_mode_t dir_mode;
};

static void *do_smb_super_data_conv(void *raw_data)
{
	struct smb_mount_data *s = raw_data;
	struct compat_smb_mount_data *c_s = raw_data;

	if (c_s->version != SMB_MOUNT_OLDVERSION)
		goto out;
	s->dir_mode = c_s->dir_mode;
	s->file_mode = c_s->file_mode;
	s->gid = c_s->gid;
	s->uid = c_s->uid;
	s->mounted_uid = c_s->mounted_uid;
 out:
	return raw_data;
}

extern int copy_mount_options (const void __user *, unsigned long *);

#define SMBFS_NAME      "smbfs"
#define NCPFS_NAME      "ncpfs"

asmlinkage int compat_sys_mount(char __user * dev_name, char __user * dir_name,
				char __user * type, unsigned long flags,
				void __user * data)
{
	unsigned long type_page;
	unsigned long data_page;
	unsigned long dev_page;
	char *dir_page;
	int retval;

	retval = copy_mount_options (type, &type_page);
	if (retval < 0)
		goto out;

	dir_page = getname(dir_name);
	retval = PTR_ERR(dir_page);
	if (IS_ERR(dir_page))
		goto out1;

	retval = copy_mount_options (dev_name, &dev_page);
	if (retval < 0)
		goto out2;

	retval = copy_mount_options (data, &data_page);
	if (retval < 0)
		goto out3;

	retval = -EINVAL;

	if (type_page) {
		if (!strcmp((char *)type_page, SMBFS_NAME)) {
			do_smb_super_data_conv((void *)data_page);
		} else if (!strcmp((char *)type_page, NCPFS_NAME)) {
			do_ncp_super_data_conv((void *)data_page);
		}
	}

	lock_kernel();
	retval = do_mount((char*)dev_page, dir_page, (char*)type_page,
			flags, (void*)data_page);
	unlock_kernel();

	free_page(data_page);
 out3:
	free_page(dev_page);
 out2:
	putname(dir_page);
 out1:
	free_page(type_page);
 out:
	return retval;
}


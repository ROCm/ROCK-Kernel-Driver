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
#include <linux/smp_lock.h>
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

/* ioctl32 stuff, used by sparc64, parisc, s390x, ppc64, x86_64 */

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

static struct ioctl_trans *ioctl_free_list;

/* Never free them really. This avoids SMP races. With a Read-Copy-Update
   enabled kernel we could just use the RCU infrastructure for this. */
static void free_ioctl(struct ioctl_trans *t) 
{ 
	t->cmd = 0; 
	mb();
	t->next = ioctl_free_list;
	ioctl_free_list = t;
} 

int register_ioctl32_conversion(unsigned int cmd, int (*handler)(unsigned int, unsigned int, unsigned long, struct file *))
{
	struct ioctl_trans *t;
	unsigned long hash = ioctl32_hash(cmd);

	lock_kernel(); 
	for (t = (struct ioctl_trans *)ioctl32_hash_table[hash];
	     t;
	     t = t->next) { 
		if (t->cmd == cmd) {
			printk("Trying to register duplicated ioctl32 handler %x\n", cmd);
			unlock_kernel();
			return -EINVAL; 
		}
	} 

	if (ioctl_free_list) { 
		t = ioctl_free_list; 
		ioctl_free_list = t->next; 
	} else { 
		t = kmalloc(sizeof(struct ioctl_trans), GFP_KERNEL); 
		if (!t) { 
			unlock_kernel();
			return -ENOMEM;
		}
	}
	
	t->next = NULL;
	t->cmd = cmd;
	t->handler = handler; 
	ioctl32_insert_translation(t);

	unlock_kernel();
	return 0;
}

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

	t = (struct ioctl_trans *)ioctl32_hash_table[hash];
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
			free_ioctl(t); 
			unlock_kernel();
		return 0;
		}
	} 
	while (t->next) {
		t1 = (struct ioctl_trans *)(long)t->next;
		if (t1->cmd == cmd) { 
			if (builtin_ioctl(t1)) {
				printk("%p tried to unregister builtin ioctl %x\n",
				       __builtin_return_address(0), cmd);
				goto out;
			} else { 
			t->next = t1->next;
				free_ioctl(t1); 
				unlock_kernel();
			return 0;
			}
		}
		t = t1;
	}
	printk(KERN_ERR "Trying to free unknown 32bit ioctl handler %x\n", cmd);
 out:
	unlock_kernel();
	return -EINVAL;
}

EXPORT_SYMBOL(register_ioctl32_conversion); 
EXPORT_SYMBOL(unregister_ioctl32_conversion); 

asmlinkage long compat_sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
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

	t = (struct ioctl_trans *)ioctl32_hash_table [ioctl32_hash (cmd)];

	while (t && t->cmd != cmd)
		t = (struct ioctl_trans *)t->next;
	if (t) {
		if (t->handler)
			error = t->handler(fd, cmd, arg, filp);
		else
			error = sys_ioctl(fd, cmd, arg);
	} else if (cmd >= SIOCDEVPRIVATE && cmd <= (SIOCDEVPRIVATE + 15)) {
		error = siocdevprivate_ioctl(fd, cmd, arg);
	} else {
		static int count;
		if (++count <= 50) { 
			char buf[10];
			char *path = (char *)__get_free_page(GFP_KERNEL), *fn = "?"; 

			/* find the name of the device. */
			if (path) {
		       		fn = d_path(filp->f_dentry, filp->f_vfsmnt, 
					    path, PAGE_SIZE);
			}

			sprintf(buf,"'%c'", (cmd>>24) & 0x3f); 
			if (!isprint(buf[1]))
			    sprintf(buf, "%02x", buf[1]);
			printk("ioctl32(%s:%d): Unknown cmd fd(%d) "
			       "cmd(%08x){%s} arg(%08x) on %s\n",
			       current->comm, current->pid,
			       (int)fd, (unsigned int)cmd, buf, (unsigned int)arg,
			       fn);
			if (path) 
				free_page((unsigned long)path); 
		}
		error = -EINVAL;
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

extern asmlinkage long sys_fcntl(unsigned int, unsigned int, unsigned long);

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


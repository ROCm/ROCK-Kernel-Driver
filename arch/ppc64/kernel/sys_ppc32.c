/*
 * sys_ppc32.c: Conversion between 32bit and 64bit native syscalls.
 *
 * Copyright (C) 2001 IBM
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <asm/ptrace.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h> 
#include <linux/mm.h> 
#include <linux/file.h> 
#include <linux/signal.h>
#include <linux/resource.h>
#include <linux/times.h>
#include <linux/utsname.h>
#include <linux/timex.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/aio.h>
#include <linux/nfs_fs.h>
#include <linux/smb_fs.h>
#include <linux/smb_mount.h>
#include <linux/ncp_fs.h>
#include <linux/module.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfsd/xdr.h>
#include <linux/nfsd/syscall.h>
#include <linux/poll.h>
#include <linux/personality.h>
#include <linux/stat.h>
#include <linux/filter.h>
#include <linux/highmem.h>
#include <linux/highuid.h>
#include <linux/mman.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/icmpv6.h>
#include <linux/sysctl.h>
#include <linux/binfmts.h>
#include <linux/dnotify.h>
#include <linux/security.h>
#include <linux/compat.h>
#include <linux/ptrace.h>
#include <linux/aio_abi.h>

#include <asm/types.h>
#include <asm/ipc.h>
#include <asm/uaccess.h>

#include <asm/semaphore.h>

#include <net/scm.h>
#include <net/sock.h>
#include <linux/elf.h>
#include <asm/ppcdebug.h>
#include <asm/time.h>
#include <asm/ppc32.h>
#include <asm/mmu_context.h>

typedef ssize_t (*io_fn_t)(struct file *, char *, size_t, loff_t *);
typedef ssize_t (*iov_fn_t)(struct file *, const struct iovec *, unsigned long, loff_t *);

static long do_readv_writev32(int type, struct file *file,
			      const struct compat_iovec *vector, u32 count)
{
	compat_ssize_t tot_len;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov=iovstack, *ivp;
	struct inode *inode;
	long retval, i;
	io_fn_t fn;
	iov_fn_t fnv;

	/*
	 * SuS says "The readv() function *may* fail if the iovcnt argument
	 * was less than or equal to 0, or greater than {IOV_MAX}.  Linux has
	 * traditionally returned zero for zero segments, so...
	 */
	retval = 0;
	if (count == 0)
		goto out;

	/* First get the "struct iovec" from user memory and
	 * verify all the pointers
	 */
	retval = -EINVAL;
	if (count > UIO_MAXIOV)
		goto out;
	if (!file->f_op)
		goto out;
	if (count > UIO_FASTIOV) {
		retval = -ENOMEM;
		iov = kmalloc(count*sizeof(struct iovec), GFP_KERNEL);
		if (!iov)
			goto out;
	}
	retval = -EFAULT;
	if (verify_area(VERIFY_READ, vector, sizeof(struct compat_iovec)*count))
		goto out;

	/*
	 * Single unix specification:
	 * We should -EINVAL if an element length is not >= 0 and fitting an
	 * ssize_t.  The total length is fitting an ssize_t
	 *
	 * Be careful here because iov_len is a size_t not an ssize_t
	 */
	tot_len = 0;
	i = count;
	ivp = iov;
	retval = -EINVAL;
	while(i > 0) {
		compat_ssize_t tmp = tot_len;
		compat_ssize_t len;
		u32 buf;

		if (__get_user(len, &vector->iov_len) ||
		    __get_user(buf, &vector->iov_base)) {
			retval = -EFAULT;
			goto out;
		}
		if (len < 0)	/* size_t not fitting an compat_ssize_t .. */
			goto out;
		tot_len += len;
		if (tot_len < tmp) /* maths overflow on the compat_ssize_t */
			goto out;
		ivp->iov_base = (void *)A(buf);
		ivp->iov_len = (__kernel_size_t) len;
		vector++;
		ivp++;
		i--;
	}
	if (tot_len == 0) {
		retval = 0;
		goto out;
	}

	inode = file->f_dentry->d_inode;
	/* VERIFY_WRITE actually means a read, as we write to user space */
	retval = locks_verify_area((type == READ
				    ? FLOCK_VERIFY_READ : FLOCK_VERIFY_WRITE),
				   inode, file, file->f_pos, tot_len);
	if (retval)
		goto out;

	if (type == READ) {
		fn = file->f_op->read;
		fnv = file->f_op->readv;
	} else {
		fn = (io_fn_t)file->f_op->write;
		fnv = file->f_op->writev;
	}
	if (fnv) {
		retval = fnv(file, iov, count, &file->f_pos);
		goto out;
	}

	/* Do it by hand, with file-ops */
	ivp = iov;
	while (count > 0) {
		void * base;
		int len, nr;

		base = ivp->iov_base;
		len = ivp->iov_len;
		ivp++;
		count--;

		nr = fn(file, base, len, &file->f_pos);

		if (nr < 0) {
			if (!retval)
				retval = nr;
			break;
		}
		retval += nr;
		if (nr != len)
			break;
	}
out:
	if (iov != iovstack)
		kfree(iov);
	if ((retval + (type == READ)) > 0)
		dnotify_parent(file->f_dentry,
			       (type == READ) ? DN_ACCESS : DN_MODIFY);

	return retval;
}

asmlinkage long sys32_readv(int fd, struct compat_iovec *vector, u32 count)
{
	struct file *file;
	int ret = -EBADF;

	file = fget(fd);
	if (!file || !(file->f_mode & FMODE_READ))
		goto out; 

	ret = -EINVAL;
	if (!file->f_op || (!file->f_op->readv && !file->f_op->read))
		goto out;

	ret = do_readv_writev32(READ, file, vector, count);

out:
	if (file)
		fput(file);
	return ret;
}

asmlinkage long sys32_writev(int fd, struct compat_iovec *vector, u32 count)
{
	struct file *file;
	int ret = -EBADF;

	file = fget(fd);
	if (!file || !(file->f_mode & FMODE_WRITE))
		goto out;

	ret = -EINVAL;
	if (!file->f_op || (!file->f_op->writev && !file->f_op->write))
		goto out;

	ret = do_readv_writev32(WRITE, file, vector, count);

out:
	if (file)
		fput(file);
	return ret;
}

struct ncp_mount_data32_v3 {
        int version;
        unsigned int ncp_fd;
        compat_uid_t mounted_uid;
        compat_pid_t wdog_pid;
        unsigned char mounted_vol[NCP_VOLNAME_LEN + 1];
        unsigned int time_out;
        unsigned int retry_count;
        unsigned int flags;
        compat_uid_t uid;
        compat_gid_t gid;
        compat_mode_t file_mode;
        compat_mode_t dir_mode;
};

struct ncp_mount_data32_v4 {
	int version;
	/* all members below are "long" in ABI ... i.e. 32bit on sparc32, while 64bits on sparc64 */
	unsigned int flags;
	unsigned int mounted_uid;
	int wdog_pid;

	unsigned int ncp_fd;
	unsigned int time_out;
	unsigned int retry_count;

	unsigned int uid;
	unsigned int gid;
	unsigned int file_mode;
	unsigned int dir_mode;
};

static void *do_ncp_super_data_conv(void *raw_data)
{
	switch (*(int*)raw_data) {
		case NCP_MOUNT_VERSION:
			{
				struct ncp_mount_data news, *n = &news; 
				struct ncp_mount_data32_v3 *n32 = (struct ncp_mount_data32_v3 *)raw_data;

				n->version = n32->version;
				n->ncp_fd = n32->ncp_fd;
				n->mounted_uid = n32->mounted_uid;
				n->wdog_pid = n32->wdog_pid;
				memmove (n->mounted_vol, n32->mounted_vol, sizeof (n32->mounted_vol));
				n->time_out = n32->time_out;
				n->retry_count = n32->retry_count;
				n->flags = n32->flags;
				n->uid = n32->uid;
				n->gid = n32->gid;
				n->file_mode = n32->file_mode;
				n->dir_mode = n32->dir_mode;
				memcpy(raw_data, n, sizeof(*n)); 
			}
			break;
		case NCP_MOUNT_VERSION_V4:
			{
				struct ncp_mount_data_v4 news, *n = &news; 
				struct ncp_mount_data32_v4 *n32 = (struct ncp_mount_data32_v4 *)raw_data;

				n->version = n32->version;
				n->flags = n32->flags;
				n->mounted_uid = n32->mounted_uid;
				n->wdog_pid = n32->wdog_pid;
				n->ncp_fd = n32->ncp_fd;
				n->time_out = n32->time_out;
				n->retry_count = n32->retry_count;
				n->uid = n32->uid;
				n->gid = n32->gid;
				n->file_mode = n32->file_mode;
				n->dir_mode = n32->dir_mode;
				memcpy(raw_data, n, sizeof(*n)); 
			}
			break;
		default:
			/* do not touch unknown structures */
			break;
	}
	return raw_data;
}

struct smb_mount_data32 {
        int version;
        compat_uid_t mounted_uid;
        compat_uid_t uid;
        compat_gid_t gid;
        compat_mode_t file_mode;
        compat_mode_t dir_mode;
};

static void *do_smb_super_data_conv(void *raw_data)
{
	struct smb_mount_data news, *s = &news;
	struct smb_mount_data32 *s32 = (struct smb_mount_data32 *)raw_data;

	if (s32->version != SMB_MOUNT_OLDVERSION)
		goto out;
	s->version = s32->version;
	s->mounted_uid = s32->mounted_uid;
	s->uid = s32->uid;
	s->gid = s32->gid;
	s->file_mode = s32->file_mode;
	s->dir_mode = s32->dir_mode;
	memcpy(raw_data, s, sizeof(struct smb_mount_data)); 
out:
	return raw_data;
}

static int copy_mount_stuff_to_kernel(const void *user, unsigned long *kernel)
{
	int i;
	unsigned long page;
	struct vm_area_struct *vma;

	*kernel = 0;
	if(!user)
		return 0;
	vma = find_vma(current->mm, (unsigned long)user);
	if(!vma || (unsigned long)user < vma->vm_start)
		return -EFAULT;
	if(!(vma->vm_flags & VM_READ))
		return -EFAULT;
	i = vma->vm_end - (unsigned long) user;
	if(PAGE_SIZE <= (unsigned long) i)
		i = PAGE_SIZE - 1;
	if(!(page = __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	if(copy_from_user((void *) page, user, i)) {
		free_page(page);
		return -EFAULT;
	}
	*kernel = page;
	return 0;
}

#define SMBFS_NAME	"smbfs"
#define NCPFS_NAME	"ncpfs"

asmlinkage long sys32_mount(char *dev_name, char *dir_name, char *type, unsigned long new_flags, u32 data)
{
	unsigned long type_page = 0;
	unsigned long data_page = 0;
	unsigned long dev_page = 0;
	unsigned long dir_page = 0;
	int err, is_smb, is_ncp;
	
	is_smb = is_ncp = 0;

	err = copy_mount_stuff_to_kernel((const void *)type, &type_page);
	if (err)
		goto out;

	if (!type_page) {
		err = -EINVAL;
		goto out;
	}

	is_smb = !strcmp((char *)type_page, SMBFS_NAME);
	is_ncp = !strcmp((char *)type_page, NCPFS_NAME);

	err = copy_mount_stuff_to_kernel((const void *)AA(data), &data_page);
	if (err)
		goto type_out;

	err = copy_mount_stuff_to_kernel(dev_name, &dev_page);
	if (err)
		goto data_out;

	err = copy_mount_stuff_to_kernel(dir_name, &dir_page);
	if (err)
		goto dev_out;

	if (!is_smb && !is_ncp) {
		lock_kernel();
		err = do_mount((char*)dev_page, (char*)dir_page,
				(char*)type_page, new_flags, (char*)data_page);
		unlock_kernel();
	} else {
		if (is_ncp)
			do_ncp_super_data_conv((void *)data_page);
		else
			do_smb_super_data_conv((void *)data_page);

		lock_kernel();
		err = do_mount((char*)dev_page, (char*)dir_page,
				(char*)type_page, new_flags, (char*)data_page);
		unlock_kernel();
	}
	free_page(dir_page);

dev_out:
	free_page(dev_page);

data_out:
	free_page(data_page);

type_out:
	free_page(type_page);

out:
	return err;
}

/* readdir & getdents */
#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x) (((x)+sizeof(u32)-1) & ~(sizeof(u32)-1))

struct old_linux_dirent32 {
	u32		d_ino;
	u32		d_offset;
	unsigned short	d_namlen;
	char		d_name[1];
};

struct readdir_callback32 {
	struct old_linux_dirent32 * dirent;
	int count;
};

static int fillonedir(void * __buf, const char * name, int namlen,
		                  off_t offset, ino_t ino, unsigned int d_type)
{
	struct readdir_callback32 * buf = (struct readdir_callback32 *) __buf;
	struct old_linux_dirent32 * dirent;

	if (buf->count)
		return -EINVAL;
	buf->count++;
	dirent = buf->dirent;
	put_user(ino, &dirent->d_ino);
	put_user(offset, &dirent->d_offset);
	put_user(namlen, &dirent->d_namlen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	return 0;
}

asmlinkage int old32_readdir(unsigned int fd, struct old_linux_dirent32 *dirent, unsigned int count)
{
	int error = -EBADF;
	struct file * file;
	struct readdir_callback32 buf;

	file = fget(fd);
	if (!file)
		goto out;

	buf.count = 0;
	buf.dirent = dirent;

	error = vfs_readdir(file, (filldir_t)fillonedir, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.count;

out_putf:
	fput(file);
out:
	return error;
}

struct linux_dirent32 {
	u32		d_ino;
	u32		d_off;
	unsigned short	d_reclen;
	char		d_name[1];
};

struct getdents_callback32 {
	struct linux_dirent32 * current_dir;
	struct linux_dirent32 * previous;
	int count;
	int error;
};

static int
filldir(void * __buf, const char * name, int namlen, off_t offset, ino_t ino,
		               unsigned int d_type)
{
	struct linux_dirent32 * dirent;
	struct getdents_callback32 * buf = (struct getdents_callback32 *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1);

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;
	if (dirent)
		put_user(offset, &dirent->d_off);
	dirent = buf->current_dir;
	buf->previous = dirent;
	put_user(ino, &dirent->d_ino);
	put_user(reclen, &dirent->d_reclen);
	copy_to_user(dirent->d_name, name, namlen);
	put_user(0, dirent->d_name + namlen);
	((char *) dirent) += reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
}

asmlinkage long sys32_getdents(unsigned int fd, struct linux_dirent32 *dirent, unsigned int count)
{
	struct file * file;
	struct linux_dirent32 * lastdirent;
	struct getdents_callback32 buf;
	int error = -EBADF;

	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = vfs_readdir(file, (filldir_t)filldir, &buf);
	if (error < 0)
		goto out_putf;
	lastdirent = buf.previous;
	error = buf.error;
	if(lastdirent) {
		put_user(file->f_pos, &lastdirent->d_off);
		error = count - buf.count;
	}
 out_putf:
	fput(file);

 out:
	return error;
}
/* end of readdir & getdents */

/*
 * Ooo, nasty.  We need here to frob 32-bit unsigned longs to
 * 64-bit unsigned longs.
 */
static inline int
get_fd_set32(unsigned long n, unsigned long *fdset, u32 *ufdset)
{
	if (ufdset) {
		unsigned long odd;

		if (verify_area(VERIFY_WRITE, ufdset, n*sizeof(u32)))
			return -EFAULT;

		odd = n & 1UL;
		n &= ~1UL;
		while (n) {
			unsigned long h, l;
			__get_user(l, ufdset);
			__get_user(h, ufdset+1);
			ufdset += 2;
			*fdset++ = h << 32 | l;
			n -= 2;
		}
		if (odd)
			__get_user(*fdset, ufdset);
	} else {
		/* Tricky, must clear full unsigned long in the
		 * kernel fdset at the end, this makes sure that
		 * actually happens.
		 */
		memset(fdset, 0, ((n + 1) & ~1)*sizeof(u32));
	}
	return 0;
}

static inline void
set_fd_set32(unsigned long n, u32 *ufdset, unsigned long *fdset)
{
	unsigned long odd;

	if (!ufdset)
		return;

	odd = n & 1UL;
	n &= ~1UL;
	while (n) {
		unsigned long h, l;
		l = *fdset++;
		h = l >> 32;
		__put_user(l, ufdset);
		__put_user(h, ufdset+1);
		ufdset += 2;
		n -= 2;
	}
	if (odd)
		__put_user(*fdset, ufdset);
}



#define MAX_SELECT_SECONDS ((unsigned long) (MAX_SCHEDULE_TIMEOUT / HZ)-1)

asmlinkage long sys32_select(int n, u32 *inp, u32 *outp, u32 *exp, u32 tvp_x)
{
	fd_set_bits fds;
	struct compat_timeval *tvp = (struct compat_timeval *)AA(tvp_x);
	char *bits;
	unsigned long nn;
	long timeout;
	int ret, size, max_fdset;

	timeout = MAX_SCHEDULE_TIMEOUT;
	if (tvp) {
		time_t sec, usec;
		if ((ret = verify_area(VERIFY_READ, tvp, sizeof(*tvp)))
		    || (ret = __get_user(sec, &tvp->tv_sec))
		    || (ret = __get_user(usec, &tvp->tv_usec)))
			goto out_nofds;

		ret = -EINVAL;
		if(sec < 0 || usec < 0)
			goto out_nofds;

		if ((unsigned long) sec < MAX_SELECT_SECONDS) {
			timeout = (usec + 1000000/HZ - 1) / (1000000/HZ);
			timeout += sec * (unsigned long) HZ;
		}
	}

	ret = -EINVAL;
	if (n < 0)
		goto out_nofds;

	/* max_fdset can increase, so grab it once to avoid race */
	max_fdset = current->files->max_fdset;
	if (n > max_fdset)
		n = max_fdset;

	/*
	 * We need 6 bitmaps (in/out/ex for both incoming and outgoing),
	 * since we used fdset we need to allocate memory in units of
	 * long-words. 
	 */
	ret = -ENOMEM;
	size = FDS_BYTES(n);
	bits = kmalloc(6 * size, GFP_KERNEL);
	if (!bits)
		goto out_nofds;
	fds.in      = (unsigned long *)  bits;
	fds.out     = (unsigned long *) (bits +   size);
	fds.ex      = (unsigned long *) (bits + 2*size);
	fds.res_in  = (unsigned long *) (bits + 3*size);
	fds.res_out = (unsigned long *) (bits + 4*size);
	fds.res_ex  = (unsigned long *) (bits + 5*size);

	nn = (n + 8*sizeof(u32) - 1) / (8*sizeof(u32));
	if ((ret = get_fd_set32(nn, fds.in, inp)) ||
	    (ret = get_fd_set32(nn, fds.out, outp)) ||
	    (ret = get_fd_set32(nn, fds.ex, exp)))
		goto out;
	zero_fd_set(n, fds.res_in);
	zero_fd_set(n, fds.res_out);
	zero_fd_set(n, fds.res_ex);

	ret = do_select(n, &fds, &timeout);

	if (tvp && !(current->personality & STICKY_TIMEOUTS)) {
		time_t sec = 0, usec = 0;
		if (timeout) {
			sec = timeout / HZ;
			usec = timeout % HZ;
			usec *= (1000000/HZ);
		}
		put_user(sec, &tvp->tv_sec);
		put_user(usec, &tvp->tv_usec);
	}

	if (ret < 0)
		goto out;
	if (!ret) {
		ret = -ERESTARTNOHAND;
		if (signal_pending(current))
			goto out;
		ret = 0;
	}

	set_fd_set32(nn, inp, fds.res_in);
	set_fd_set32(nn, outp, fds.res_out);
	set_fd_set32(nn, exp, fds.res_ex);
  
out:
	kfree(bits);

out_nofds:
	return ret;
}

int ppc32_select(u32 n, u32* inp, u32* outp, u32* exp, u32 tvp_x)
{
	/* sign extend n */
	return sys32_select((int)n, inp, outp, exp, tvp_x);
}

int cp_compat_stat(struct kstat *stat, struct compat_stat *statbuf)
{
	int err;

	if (stat->size > MAX_NON_LFS || !new_valid_dev(stat->dev) ||
	    !new_valid_dev(stat->rdev))
		return -EOVERFLOW;

	err  = put_user(new_encode_dev(stat->dev), &statbuf->st_dev);
	err |= put_user(stat->ino, &statbuf->st_ino);
	err |= put_user(stat->mode, &statbuf->st_mode);
	err |= put_user(stat->nlink, &statbuf->st_nlink);
	err |= put_user(stat->uid, &statbuf->st_uid);
	err |= put_user(stat->gid, &statbuf->st_gid);
	err |= put_user(new_encode_dev(stat->rdev), &statbuf->st_rdev);
	err |= put_user(stat->size, &statbuf->st_size);
	err |= put_user(stat->atime.tv_sec, &statbuf->st_atime);
	err |= put_user(0, &statbuf->__unused1);
	err |= put_user(stat->mtime.tv_sec, &statbuf->st_mtime);
	err |= put_user(0, &statbuf->__unused2);
	err |= put_user(stat->ctime.tv_sec, &statbuf->st_ctime);
	err |= put_user(0, &statbuf->__unused3);
	err |= put_user(stat->blksize, &statbuf->st_blksize);
	err |= put_user(stat->blocks, &statbuf->st_blocks);
	err |= put_user(0, &statbuf->__unused4[0]);
	err |= put_user(0, &statbuf->__unused4[1]);

	return err;
}

extern asmlinkage long sys_sysfs(int option, unsigned long arg1, unsigned long arg2);

/* Note: it is necessary to treat option as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sysfs(u32 option, u32 arg1, u32 arg2)
{
	return sys_sysfs((int)option, arg1, arg2);
}

/* Handle adjtimex compatibility. */
struct timex32 {
	u32 modes;
	s32 offset, freq, maxerror, esterror;
	s32 status, constant, precision, tolerance;
	struct compat_timeval time;
	s32 tick;
	s32 ppsfreq, jitter, shift, stabil;
	s32 jitcnt, calcnt, errcnt, stbcnt;
	s32  :32; s32  :32; s32  :32; s32  :32;
	s32  :32; s32  :32; s32  :32; s32  :32;
	s32  :32; s32  :32; s32  :32; s32  :32;
};

extern int do_adjtimex(struct timex *);
extern void ppc_adjtimex(void);

asmlinkage long sys32_adjtimex(struct timex32 *utp)
{
	struct timex txc;
	int ret;
	
	memset(&txc, 0, sizeof(struct timex));

	if(get_user(txc.modes, &utp->modes) ||
	   __get_user(txc.offset, &utp->offset) ||
	   __get_user(txc.freq, &utp->freq) ||
	   __get_user(txc.maxerror, &utp->maxerror) ||
	   __get_user(txc.esterror, &utp->esterror) ||
	   __get_user(txc.status, &utp->status) ||
	   __get_user(txc.constant, &utp->constant) ||
	   __get_user(txc.precision, &utp->precision) ||
	   __get_user(txc.tolerance, &utp->tolerance) ||
	   __get_user(txc.time.tv_sec, &utp->time.tv_sec) ||
	   __get_user(txc.time.tv_usec, &utp->time.tv_usec) ||
	   __get_user(txc.tick, &utp->tick) ||
	   __get_user(txc.ppsfreq, &utp->ppsfreq) ||
	   __get_user(txc.jitter, &utp->jitter) ||
	   __get_user(txc.shift, &utp->shift) ||
	   __get_user(txc.stabil, &utp->stabil) ||
	   __get_user(txc.jitcnt, &utp->jitcnt) ||
	   __get_user(txc.calcnt, &utp->calcnt) ||
	   __get_user(txc.errcnt, &utp->errcnt) ||
	   __get_user(txc.stbcnt, &utp->stbcnt))
		return -EFAULT;

	ret = do_adjtimex(&txc);

	/* adjust the conversion of TB to time of day to track adjtimex */
	ppc_adjtimex();

	if(put_user(txc.modes, &utp->modes) ||
	   __put_user(txc.offset, &utp->offset) ||
	   __put_user(txc.freq, &utp->freq) ||
	   __put_user(txc.maxerror, &utp->maxerror) ||
	   __put_user(txc.esterror, &utp->esterror) ||
	   __put_user(txc.status, &utp->status) ||
	   __put_user(txc.constant, &utp->constant) ||
	   __put_user(txc.precision, &utp->precision) ||
	   __put_user(txc.tolerance, &utp->tolerance) ||
	   __put_user(txc.time.tv_sec, &utp->time.tv_sec) ||
	   __put_user(txc.time.tv_usec, &utp->time.tv_usec) ||
	   __put_user(txc.tick, &utp->tick) ||
	   __put_user(txc.ppsfreq, &utp->ppsfreq) ||
	   __put_user(txc.jitter, &utp->jitter) ||
	   __put_user(txc.shift, &utp->shift) ||
	   __put_user(txc.stabil, &utp->stabil) ||
	   __put_user(txc.jitcnt, &utp->jitcnt) ||
	   __put_user(txc.calcnt, &utp->calcnt) ||
	   __put_user(txc.errcnt, &utp->errcnt) ||
	   __put_user(txc.stbcnt, &utp->stbcnt))
		ret = -EFAULT;

	return ret;
}

/* Stuff for NFS server syscalls... */
struct nfsctl_svc32 {
	u16			svc32_port;
	s32			svc32_nthreads;
};

struct nfsctl_client32 {
	s8			cl32_ident[NFSCLNT_IDMAX+1];
	s32			cl32_naddr;
	struct in_addr		cl32_addrlist[NFSCLNT_ADDRMAX];
	s32			cl32_fhkeytype;
	s32			cl32_fhkeylen;
	u8			cl32_fhkey[NFSCLNT_KEYMAX];
};

struct nfsctl_export32 {
	s8			ex32_client[NFSCLNT_IDMAX+1];
	s8			ex32_path[NFS_MAXPATHLEN+1];
	compat_dev_t	ex32_dev;
	compat_ino_t	ex32_ino;
	s32			ex32_flags;
	compat_uid_t	ex32_anon_uid;
	compat_gid_t	ex32_anon_gid;
};

struct nfsctl_fdparm32 {
	struct sockaddr		gd32_addr;
	s8			gd32_path[NFS_MAXPATHLEN+1];
	s32			gd32_version;
};

struct nfsctl_fsparm32 {
	struct sockaddr		gd32_addr;
	s8			gd32_path[NFS_MAXPATHLEN+1];
	s32			gd32_maxlen;
};

struct nfsctl_arg32 {
	s32			ca32_version;	/* safeguard */
	union {
		struct nfsctl_svc32	u32_svc;
		struct nfsctl_client32	u32_client;
		struct nfsctl_export32	u32_export;
		struct nfsctl_fdparm32	u32_getfd;
		struct nfsctl_fsparm32	u32_getfs;
	} u;
#define ca32_svc	u.u32_svc
#define ca32_client	u.u32_client
#define ca32_export	u.u32_export
#define ca32_getfd	u.u32_getfd
#define ca32_getfs	u.u32_getfs
};

union nfsctl_res32 {
	__u8			cr32_getfh[NFS_FHSIZE];
	struct knfsd_fh		cr32_getfs;
};

static int nfs_svc32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= __get_user(karg->ca_svc.svc_port, &arg32->ca32_svc.svc32_port);
	err |= __get_user(karg->ca_svc.svc_nthreads, &arg32->ca32_svc.svc32_nthreads);
	return err;
}

static int nfs_clnt32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_client.cl_ident[0],
			  &arg32->ca32_client.cl32_ident[0],
			  NFSCLNT_IDMAX);
	err |= __get_user(karg->ca_client.cl_naddr, &arg32->ca32_client.cl32_naddr);
	err |= copy_from_user(&karg->ca_client.cl_addrlist[0],
			  &arg32->ca32_client.cl32_addrlist[0],
			  (sizeof(struct in_addr) * NFSCLNT_ADDRMAX));
	err |= __get_user(karg->ca_client.cl_fhkeytype,
		      &arg32->ca32_client.cl32_fhkeytype);
	err |= __get_user(karg->ca_client.cl_fhkeylen,
		      &arg32->ca32_client.cl32_fhkeylen);
	err |= copy_from_user(&karg->ca_client.cl_fhkey[0],
			  &arg32->ca32_client.cl32_fhkey[0],
			  NFSCLNT_KEYMAX);

	if(err) return -EFAULT;
	return 0;
}

static int nfs_exp32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_export.ex_client[0],
			  &arg32->ca32_export.ex32_client[0],
			  NFSCLNT_IDMAX);
	err |= copy_from_user(&karg->ca_export.ex_path[0],
			  &arg32->ca32_export.ex32_path[0],
			  NFS_MAXPATHLEN);
	err |= __get_user(karg->ca_export.ex_dev,
		      &arg32->ca32_export.ex32_dev);
	err |= __get_user(karg->ca_export.ex_ino,
		      &arg32->ca32_export.ex32_ino);
	err |= __get_user(karg->ca_export.ex_flags,
		      &arg32->ca32_export.ex32_flags);
	err |= __get_user(karg->ca_export.ex_anon_uid,
		      &arg32->ca32_export.ex32_anon_uid);
	err |= __get_user(karg->ca_export.ex_anon_gid,
		      &arg32->ca32_export.ex32_anon_gid);
	karg->ca_export.ex_anon_uid = karg->ca_export.ex_anon_uid;
	karg->ca_export.ex_anon_gid = karg->ca_export.ex_anon_gid;

	if(err) return -EFAULT;
	return 0;
}

static int nfs_getfd32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_getfd.gd_addr,
			  &arg32->ca32_getfd.gd32_addr,
			  (sizeof(struct sockaddr)));
	err |= copy_from_user(&karg->ca_getfd.gd_path,
			  &arg32->ca32_getfd.gd32_path,
			  (NFS_MAXPATHLEN+1));
	err |= __get_user(karg->ca_getfd.gd_version,
		      &arg32->ca32_getfd.gd32_version);

	if(err) return -EFAULT;
	return 0;
}

static int nfs_getfs32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_getfs.gd_addr,
			  &arg32->ca32_getfs.gd32_addr,
			  (sizeof(struct sockaddr)));
	err |= copy_from_user(&karg->ca_getfs.gd_path,
			  &arg32->ca32_getfs.gd32_path,
			  (NFS_MAXPATHLEN+1));
	err |= __get_user(karg->ca_getfs.gd_maxlen,
		      &arg32->ca32_getfs.gd32_maxlen);

	if(err) return -EFAULT;
	return 0;
}

/* This really doesn't need translations, we are only passing
 * back a union which contains opaque nfs file handle data.
 */
static int nfs_getfh32_res_trans(union nfsctl_res *kres, union nfsctl_res32 *res32)
{
	int err;

	err = copy_to_user(res32, kres, sizeof(*res32));

	if(err) return -EFAULT;
	return 0;
}

/* Note: it is necessary to treat cmd_parm as an unsigned int, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
int asmlinkage sys32_nfsservctl(u32 cmd_parm, struct nfsctl_arg32 *arg32, union nfsctl_res32 *res32)
{
  int cmd = (int)cmd_parm;
	struct nfsctl_arg *karg = NULL;
	union nfsctl_res *kres = NULL;
	mm_segment_t oldfs;
	int err;

	karg = kmalloc(sizeof(*karg), GFP_USER);
	if(!karg)
		return -ENOMEM;
	if(res32) {
		kres = kmalloc(sizeof(*kres), GFP_USER);
		if(!kres) {
			kfree(karg);
			return -ENOMEM;
		}
	}
	switch(cmd) {
	case NFSCTL_SVC:
		err = nfs_svc32_trans(karg, arg32);
		break;
	case NFSCTL_ADDCLIENT:
		err = nfs_clnt32_trans(karg, arg32);
		break;
	case NFSCTL_DELCLIENT:
		err = nfs_clnt32_trans(karg, arg32);
		break;
	case NFSCTL_EXPORT:
	case NFSCTL_UNEXPORT:
		err = nfs_exp32_trans(karg, arg32);
		break;
	case NFSCTL_GETFD:
		err = nfs_getfd32_trans(karg, arg32);
		break;
	case NFSCTL_GETFS:
		err = nfs_getfs32_trans(karg, arg32);
		break;
	default:
		err = -EINVAL;
		break;
	}
	if(err)
		goto done;
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_nfsservctl(cmd, karg, kres);
	set_fs(oldfs);

	if (err)
		goto done;

	if((cmd == NFSCTL_GETFD) ||
	   (cmd == NFSCTL_GETFS))
		err = nfs_getfh32_res_trans(kres, res32);

done:
	if(karg)
		kfree(karg);
	if(kres)
		kfree(kres);
	return err;
}



/* These are here just in case some old sparc32 binary calls it. */
asmlinkage long sys32_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	
	return -ERESTARTNOHAND;
}



static inline long get_ts32(struct timespec *o, struct compat_timeval *i)
{
	long usec;

	if (!access_ok(VERIFY_READ, i, sizeof(*i)))
		return -EFAULT;
	if (__get_user(o->tv_sec, &i->tv_sec))
		return -EFAULT;
	if (__get_user(usec, &i->tv_usec))
		return -EFAULT;
	o->tv_nsec = usec * 1000;
	return 0;
}

static inline long put_tv32(struct compat_timeval *o, struct timeval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->tv_sec, &o->tv_sec) |
		 __put_user(i->tv_usec, &o->tv_usec)));
}

struct sysinfo32 {
        s32 uptime;
        u32 loads[3];
        u32 totalram;
        u32 freeram;
        u32 sharedram;
        u32 bufferram;
        u32 totalswap;
        u32 freeswap;
        unsigned short procs;
	unsigned short pad;
	u32 totalhigh;
	u32 freehigh;
	u32 mem_unit;
	char _f[20-2*sizeof(int)-sizeof(int)];
};

extern asmlinkage long sys_sysinfo(struct sysinfo *info);

asmlinkage long sys32_sysinfo(struct sysinfo32 *info)
{
	struct sysinfo s;
	int ret, err;
	int bitcount=0;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sysinfo(&s);
	set_fs (old_fs);
	/* Check to see if any memory value is too large for 32-bit and
         * scale down if needed.
         */
	if ((s.totalram >> 32) || (s.totalswap >> 32)) {
	    while (s.mem_unit < PAGE_SIZE) {
		s.mem_unit <<= 1;
		bitcount++;
	    }
	    s.totalram >>=bitcount;
	    s.freeram >>= bitcount;
	    s.sharedram >>= bitcount;
	    s.bufferram >>= bitcount;
	    s.totalswap >>= bitcount;
	    s.freeswap >>= bitcount;
	    s.totalhigh >>= bitcount;
	    s.freehigh >>= bitcount;
	}

	err = put_user (s.uptime, &info->uptime);
	err |= __put_user (s.loads[0], &info->loads[0]);
	err |= __put_user (s.loads[1], &info->loads[1]);
	err |= __put_user (s.loads[2], &info->loads[2]);
	err |= __put_user (s.totalram, &info->totalram);
	err |= __put_user (s.freeram, &info->freeram);
	err |= __put_user (s.sharedram, &info->sharedram);
	err |= __put_user (s.bufferram, &info->bufferram);
	err |= __put_user (s.totalswap, &info->totalswap);
	err |= __put_user (s.freeswap, &info->freeswap);
	err |= __put_user (s.procs, &info->procs);
	err |= __put_user (s.totalhigh, &info->totalhigh);
	err |= __put_user (s.freehigh, &info->freehigh);
	err |= __put_user (s.mem_unit, &info->mem_unit);
	if (err)
		return -EFAULT;
	
	return ret;
}




/* Translations due to time_t size differences.  Which affects all
   sorts of things, like timeval and itimerval.  */
extern struct timezone sys_tz;

asmlinkage long sys32_gettimeofday(struct compat_timeval *tv, struct timezone *tz)
{
	if (tv) {
		struct timeval ktv;
		do_gettimeofday(&ktv);
		if (put_tv32(tv, &ktv))
			return -EFAULT;
	}
	if (tz) {
		if (copy_to_user(tz, &sys_tz, sizeof(sys_tz)))
			return -EFAULT;
	}
	
	return 0;
}



asmlinkage long sys32_settimeofday(struct compat_timeval *tv, struct timezone *tz)
{
	struct timespec kts;
	struct timezone ktz;
	
 	if (tv) {
		if (get_ts32(&kts, tv))
			return -EFAULT;
	}
	if (tz) {
		if (copy_from_user(&ktz, tz, sizeof(ktz)))
			return -EFAULT;
	}

	return do_sys_settimeofday(tv ? &kts : NULL, tz ? &ktz : NULL);
}


struct msgbuf32 {
	compat_long_t mtype; 
	char mtext[1];
};

struct semid_ds32 {
	struct ipc_perm sem_perm;
	compat_time_t sem_otime;
	compat_time_t sem_ctime;
	compat_uptr_t sem_base;
	compat_uptr_t sem_pending;
	compat_uptr_t sem_pending_last;
	compat_uptr_t undo;
	unsigned short sem_nsems;
};

struct semid64_ds32 {
	struct ipc64_perm sem_perm;
	unsigned int __unused1;
	compat_time_t sem_otime;
	unsigned int __unused2;
	compat_time_t sem_ctime;
	compat_ulong_t sem_nsems;
	compat_ulong_t __unused3;
	compat_ulong_t __unused4;
};

struct msqid_ds32 {
	struct ipc_perm msg_perm;
	compat_uptr_t msg_first;
	compat_uptr_t msg_last;
	compat_time_t msg_stime;
	compat_time_t msg_rtime;
	compat_time_t msg_ctime;
	compat_ulong_t msg_lcbytes;
	compat_ulong_t msg_lqbytes;
	unsigned short msg_cbytes;
	unsigned short msg_qnum;
	unsigned short msg_qbytes;
	compat_ipc_pid_t msg_lspid;
	compat_ipc_pid_t msg_lrpid;
};

struct msqid64_ds32 {
	struct ipc64_perm msg_perm;
	unsigned int __unused1;
	compat_time_t msg_stime;
	unsigned int __unused2;
	compat_time_t msg_rtime;
	unsigned int __unused3;
	compat_time_t msg_ctime;
	compat_ulong_t msg_cbytes;
	compat_ulong_t msg_qnum;
	compat_ulong_t msg_qbytes;
	compat_pid_t msg_lspid;
	compat_pid_t msg_lrpid;
	compat_ulong_t __unused4;
	compat_ulong_t __unused5;
};

struct shmid_ds32 {
	struct ipc_perm shm_perm;
	int shm_segsz;
	compat_time_t shm_atime;
	compat_time_t shm_dtime;
	compat_time_t shm_ctime;
	compat_ipc_pid_t shm_cpid;
	compat_ipc_pid_t shm_lpid;
	unsigned short shm_nattch;
	unsigned short __unused;
	compat_uptr_t __unused2;
	compat_uptr_t __unused3;
};

struct shmid64_ds32 {
	struct ipc64_perm shm_perm;
	unsigned int __unused1;
	compat_time_t shm_atime;
	unsigned int __unused2;
	compat_time_t shm_dtime;
	unsigned int __unused3;
	compat_time_t shm_ctime;
	unsigned int __unused4;
	compat_size_t shm_segsz;
	compat_pid_t shm_cpid;
	compat_pid_t shm_lpid;
	compat_ulong_t shm_nattch;
	compat_ulong_t __unused5;
	compat_ulong_t __unused6;
};

/*
 * sys32_ipc() is the de-multiplexer for the SysV IPC calls in 32bit
 * emulation..
 *
 * This is really horribly ugly.
 */
static long do_sys32_semctl(int first, int second, int third, void *uptr)
{
	union semun fourth;
	u32 pad;
	int err, err2;
	mm_segment_t old_fs;

	if (!uptr)
		return -EINVAL;
	err = -EFAULT;
	if (get_user(pad, (u32 *)uptr))
		return err;
	if ((third & ~IPC_64) == SETVAL)
		fourth.val = (int)pad;
	else
		fourth.__pad = (void *)A(pad);
	switch (third & (~IPC_64)) {

	case IPC_INFO:
	case IPC_RMID:
	case SEM_INFO:
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETALL:
	case SETALL:
	case SETVAL:
		err = sys_semctl(first, second, third, fourth);
		break;

	case IPC_STAT:
	case SEM_STAT:
		if (third & IPC_64) {
			struct semid64_ds s64;
			struct semid64_ds32 *usp;

			usp = (struct semid64_ds32 *)A(pad);
			fourth.__pad = &s64;
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_semctl(first, second, third, fourth);
			set_fs(old_fs);
			err2 = copy_to_user(&usp->sem_perm, &s64.sem_perm,
					    sizeof(struct ipc64_perm));
			err2 |= __put_user(s64.sem_otime, &usp->sem_otime);
			err2 |= __put_user(s64.sem_ctime, &usp->sem_ctime);
			err2 |= __put_user(s64.sem_nsems, &usp->sem_nsems);
			if (err2)
				err = -EFAULT;
		} else {
			struct semid_ds s;
			struct semid_ds32 *usp;

			usp = (struct semid_ds32 *)A(pad);
			fourth.__pad = &s;
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_semctl(first, second, third, fourth);
			set_fs(old_fs);
			err2 = copy_to_user(&usp->sem_perm, &s.sem_perm,
					    sizeof(struct ipc_perm));
			err2 |= __put_user(s.sem_otime, &usp->sem_otime);
			err2 |= __put_user(s.sem_ctime, &usp->sem_ctime);
			err2 |= __put_user(s.sem_nsems, &usp->sem_nsems);
			if (err2)
				err = -EFAULT;
		}
		break;
 
	case IPC_SET:
		if (third & IPC_64) {
			struct semid64_ds s64;
			struct semid64_ds32 *usp;

			usp = (struct semid64_ds32 *)A(pad);

			err = get_user(s64.sem_perm.uid, &usp->sem_perm.uid);
			err |= __get_user(s64.sem_perm.gid,
					  &usp->sem_perm.gid);
			err |= __get_user(s64.sem_perm.mode,
					  &usp->sem_perm.mode);
			if (err)
				goto out;
			fourth.__pad = &s64;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_semctl(first, second, third, fourth);
			set_fs(old_fs);

		} else {
			struct semid_ds s;
			struct semid_ds32 *usp;

			usp = (struct semid_ds32 *)A(pad);

			err = get_user(s.sem_perm.uid, &usp->sem_perm.uid);
			err |= __get_user(s.sem_perm.gid,
					  &usp->sem_perm.gid);
			err |= __get_user(s.sem_perm.mode,
					  &usp->sem_perm.mode);
			if (err)
				goto out;
			fourth.__pad = &s;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_semctl(first, second, third, fourth);
			set_fs(old_fs);
		}
		break;
	default:
		err = -EINVAL;
	}
out:
	return err;
}

#define MAXBUF (64*1024)

static int 
do_sys32_msgsnd(int first, int second, int third, void *uptr)
{
	struct msgbuf *p;
	struct msgbuf32 *up = (struct msgbuf32 *)uptr;
	mm_segment_t old_fs;
	int err;

	if (second < 0 || (second >= MAXBUF-sizeof(struct msgbuf)))
		return -EINVAL;

	p = kmalloc(second + sizeof(struct msgbuf), GFP_USER);
	if (!p)
		return -ENOMEM;
	err = get_user(p->mtype, &up->mtype);
	err |= copy_from_user(p->mtext, &up->mtext, second);
	if (err) {
		err = -EFAULT;
		goto out;
	}
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_msgsnd(first, p, second, third);
	set_fs(old_fs);
out:
	kfree(p);
	return err;
}

static int
do_sys32_msgrcv(int first, int second, int msgtyp, int third,
		int version, void *uptr)
{
	struct msgbuf32 *up;
	struct msgbuf *p;
	mm_segment_t old_fs;
	int err;

	if (second < 0 || (second >= MAXBUF-sizeof(struct msgbuf)))
		return -EINVAL;

	if (!version) {
		struct ipc_kludge_32 *uipck = (struct ipc_kludge_32 *)uptr;
		struct ipc_kludge_32 ipck;

		err = -EINVAL;
		if (!uptr)
			goto out;
		err = -EFAULT;
		if (copy_from_user(&ipck, uipck, sizeof(struct ipc_kludge_32)))
			goto out;
		uptr = (void *)A(ipck.msgp);
		msgtyp = ipck.msgtyp;
	}
	err = -ENOMEM;
	p = kmalloc(second + sizeof (struct msgbuf), GFP_USER);
	if (!p)
		goto out;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_msgrcv(first, p, second, msgtyp, third);
	set_fs(old_fs);
	if (err < 0)
		goto free_then_out;
	up = (struct msgbuf32 *)uptr;
	if (put_user(p->mtype, &up->mtype) ||
	    copy_to_user(&up->mtext, p->mtext, err))
		err = -EFAULT;
free_then_out:
	kfree(p);
out:
	return err;
}

static int
do_sys32_msgctl(int first, int second, void *uptr)
{
	int err = -EINVAL, err2;
	mm_segment_t old_fs;

	switch (second & (~IPC_64)) {

	case IPC_INFO:
	case IPC_RMID:
	case MSG_INFO:
		err = sys_msgctl(first, second, (struct msqid_ds *)uptr);
		break;

	case IPC_SET:
		if (second & IPC_64) {
			struct msqid64_ds m64;
			struct msqid64_ds32 *up = (struct msqid64_ds32 *)uptr;

			err2 = copy_from_user(&m64.msg_perm, &up->msg_perm,
					      sizeof(struct ipc64_perm));
			err2 |= __get_user(m64.msg_qbytes, &up->msg_qbytes);
			if (err2) {
				err = -EFAULT;
				break;
			}
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_msgctl(first, second,
					 (struct msqid_ds *)&m64);
			set_fs(old_fs);
		} else {
			struct msqid_ds m;
			struct msqid_ds32 *up = (struct msqid_ds32 *)uptr;

			err2 = copy_from_user(&m.msg_perm, &up->msg_perm,
					      sizeof(struct ipc_perm));
			err2 |= __get_user(m.msg_qbytes, &up->msg_qbytes);
			if (err2) {
				err = -EFAULT;
				break;
			}
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_msgctl(first, second, &m);
			set_fs(old_fs);
		}
		break;

	case IPC_STAT:
	case MSG_STAT:
		if (second & IPC_64) {
			struct msqid64_ds m64;
			struct msqid64_ds32 *up = (struct msqid64_ds32 *)uptr;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_msgctl(first, second,
					 (struct msqid_ds *)&m64);
			set_fs(old_fs);

			err2 = copy_to_user(&up->msg_perm, &m64.msg_perm,
					    sizeof(struct ipc64_perm));
 			err2 |= __put_user(m64.msg_stime, &up->msg_stime);
			err2 |= __put_user(m64.msg_rtime, &up->msg_rtime);
			err2 |= __put_user(m64.msg_ctime, &up->msg_ctime);
			err2 |= __put_user(m64.msg_cbytes, &up->msg_cbytes);
			err2 |= __put_user(m64.msg_qnum, &up->msg_qnum);
			err2 |= __put_user(m64.msg_qbytes, &up->msg_qbytes);
			err2 |= __put_user(m64.msg_lspid, &up->msg_lspid);
			err2 |= __put_user(m64.msg_lrpid, &up->msg_lrpid);
			if (err2)
				err = -EFAULT;
		} else {
			struct msqid64_ds m;
			struct msqid_ds32 *up = (struct msqid_ds32 *)uptr;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_msgctl(first, second, (struct msqid_ds *)&m);
			set_fs(old_fs);

			err2 = copy_to_user(&up->msg_perm, &m.msg_perm,
					    sizeof(struct ipc_perm));
 			err2 |= __put_user(m.msg_stime, &up->msg_stime);
			err2 |= __put_user(m.msg_rtime, &up->msg_rtime);
			err2 |= __put_user(m.msg_ctime, &up->msg_ctime);
			err2 |= __put_user(m.msg_cbytes, &up->msg_cbytes);
			err2 |= __put_user(m.msg_qnum, &up->msg_qnum);
			err2 |= __put_user(m.msg_qbytes, &up->msg_qbytes);
			err2 |= __put_user(m.msg_lspid, &up->msg_lspid);
			err2 |= __put_user(m.msg_lrpid, &up->msg_lrpid);
			if (err2)
				err = -EFAULT;
		}
		break;
	}
	return err;
}

static int
do_sys32_shmat(int first, int second, int third, int version, void *uptr)
{
	unsigned long raddr;
	u32 *uaddr = (u32 *)A((u32)third);
	int err = -EINVAL;

	if (version == 1)
		return err;
	err = sys_shmat(first, uptr, second, &raddr);
	if (err)
		return err;
	err = put_user(raddr, uaddr);
	return err;
}

static int
do_sys32_shmctl(int first, int second, void *uptr)
{
	int err = -EINVAL, err2;
	mm_segment_t old_fs;

	switch (second & (~IPC_64)) {

	case IPC_INFO:
	case IPC_RMID:
	case SHM_LOCK:
	case SHM_UNLOCK:
		err = sys_shmctl(first, second, (struct shmid_ds *)uptr);
		break;
	case IPC_SET:
		if (second & IPC_64) {
			struct shmid64_ds32 *up = (struct shmid64_ds32 *)uptr;
			struct shmid64_ds s64;

			err = get_user(s64.shm_perm.uid, &up->shm_perm.uid);
			err |= __get_user(s64.shm_perm.gid, &up->shm_perm.gid);
			err |= __get_user(s64.shm_perm.mode,
					  &up->shm_perm.mode);
			if (err)
				break;
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_shmctl(first, second,
					 (struct shmid_ds *)&s64);
			set_fs(old_fs);
		} else {
			struct shmid_ds32 *up = (struct shmid_ds32 *)uptr;
			struct shmid_ds s;

			err = get_user(s.shm_perm.uid, &up->shm_perm.uid);
			err |= __get_user(s.shm_perm.gid, &up->shm_perm.gid);
			err |= __get_user(s.shm_perm.mode, &up->shm_perm.mode);
			if (err)
				break;
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_shmctl(first, second, &s);
			set_fs(old_fs);
		}
		break;

	case IPC_STAT:
	case SHM_STAT:
		if (second & IPC_64) {
			struct shmid64_ds32 *up = (struct shmid64_ds32 *)uptr;
			struct shmid64_ds s64;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_shmctl(first, second,
					 (struct shmid_ds *)&s64);
			set_fs(old_fs);
			if (err < 0)
				break;

			err2 = copy_to_user(&up->shm_perm, &s64.shm_perm,
					    sizeof(struct ipc64_perm));
			err2 |= __put_user(s64.shm_atime, &up->shm_atime);
			err2 |= __put_user(s64.shm_dtime, &up->shm_dtime);
			err2 |= __put_user(s64.shm_ctime, &up->shm_ctime);
			err2 |= __put_user(s64.shm_segsz, &up->shm_segsz);
			err2 |= __put_user(s64.shm_nattch, &up->shm_nattch);
			err2 |= __put_user(s64.shm_cpid, &up->shm_cpid);
			err2 |= __put_user(s64.shm_lpid, &up->shm_lpid);
			if (err2)
				err = -EFAULT;
		} else {
			struct shmid_ds32 *up = (struct shmid_ds32 *)uptr;
			struct shmid_ds s;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_shmctl(first, second, &s);
			set_fs(old_fs);
			if (err < 0)
				break;

			err2 = copy_to_user(&up->shm_perm, &s.shm_perm,
					    sizeof(struct ipc_perm));
			err2 |= __put_user (s.shm_atime, &up->shm_atime);
			err2 |= __put_user (s.shm_dtime, &up->shm_dtime);
			err2 |= __put_user (s.shm_ctime, &up->shm_ctime);
			err2 |= __put_user (s.shm_segsz, &up->shm_segsz);
			err2 |= __put_user (s.shm_nattch, &up->shm_nattch);
			err2 |= __put_user (s.shm_cpid, &up->shm_cpid);
			err2 |= __put_user (s.shm_lpid, &up->shm_lpid);
			if (err2)
				err = -EFAULT;
		}
		break;

	case SHM_INFO: {
		struct shm_info si;
		struct shm_info32 {
			int used_ids;
			u32 shm_tot, shm_rss, shm_swp;
			u32 swap_attempts, swap_successes;
		} *uip = (struct shm_info32 *)uptr;

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = sys_shmctl(first, second, (struct shmid_ds *)&si);
		set_fs(old_fs);
		if (err < 0)
			break;
		err2 = put_user(si.used_ids, &uip->used_ids);
		err2 |= __put_user(si.shm_tot, &uip->shm_tot);
		err2 |= __put_user(si.shm_rss, &uip->shm_rss);
		err2 |= __put_user(si.shm_swp, &uip->shm_swp);
		err2 |= __put_user(si.swap_attempts, &uip->swap_attempts);
		err2 |= __put_user(si.swap_successes, &uip->swap_successes);
		if (err2)
			err = -EFAULT;
		break;
	}
	}
	return err;
}

static int sys32_semtimedop(int semid, struct sembuf *tsems, int nsems,
			    const struct compat_timespec *timeout32)
{
	struct compat_timespec t32;
	struct timespec *t64 = compat_alloc_user_space(sizeof(*t64));

	if (copy_from_user(&t32, timeout32, sizeof(t32)))
		return -EFAULT;

	if (put_user(t32.tv_sec, &t64->tv_sec) ||
	    put_user(t32.tv_nsec, &t64->tv_nsec))
		return -EFAULT;

	return sys_semtimedop(semid, tsems, nsems, t64);
}

/*
 * Note: it is necessary to treat first_parm, second_parm, and
 * third_parm as unsigned ints, with the corresponding cast to a
 * signed int to insure that the proper conversion (sign extension)
 * between the register representation of a signed int (msr in 32-bit
 * mode) and the register representation of a signed int (msr in
 * 64-bit mode) is performed.
 */
asmlinkage long sys32_ipc(u32 call, u32 first_parm, u32 second_parm, u32 third_parm, u32 ptr, u32 fifth)
{
	int first  = (int)first_parm;
	int second = (int)second_parm;
	int third  = (int)third_parm;
	int version, err;
	
	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {

	case SEMOP:
		/* struct sembuf is the same on 32 and 64bit :)) */
		err = sys_semtimedop(first, (struct sembuf *)AA(ptr),
				     second, NULL);
		break;
	case SEMTIMEDOP:
		err = sys32_semtimedop(first, (struct sembuf *)AA(ptr), second,
				       (const struct compat_timespec *)AA(fifth));
		break;
	case SEMGET:
		err = sys_semget(first, second, third);
		break;
	case SEMCTL:
		err = do_sys32_semctl(first, second, third,
				      (void *)AA(ptr));
		break;

	case MSGSND:
		err = do_sys32_msgsnd(first, second, third,
				      (void *)AA(ptr));
		break;
	case MSGRCV:
		err = do_sys32_msgrcv(first, second, fifth, third,
				      version, (void *)AA(ptr));
		break;
	case MSGGET:
		err = sys_msgget((key_t)first, second);
		break;
	case MSGCTL:
		err = do_sys32_msgctl(first, second, (void *)AA(ptr));
		break;

	case SHMAT:
		err = do_sys32_shmat(first, second, third,
				     version, (void *)AA(ptr));
		break;
	case SHMDT: 
		err = sys_shmdt((char *)AA(ptr));
		break;
	case SHMGET:
		err = sys_shmget(first, second, third);
		break;
	case SHMCTL:
		err = do_sys32_shmctl(first, second, (void *)AA(ptr));
		break;
	default:
		err = -ENOSYS;
		break;
	}
	return err;
}

extern asmlinkage ssize_t sys_sendfile(int out_fd, int in_fd, off_t* offset, size_t count);

/* Note: it is necessary to treat out_fd and in_fd as unsigned ints, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sendfile(u32 out_fd, u32 in_fd, compat_off_t* offset, u32 count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	off_t of;
	
	if (offset && get_user(of, offset))
		return -EFAULT;
		
	set_fs(KERNEL_DS);
	ret = sys_sendfile((int)out_fd, (int)in_fd, offset ? &of : NULL, count);
	set_fs(old_fs);
	
	if (offset && put_user(of, offset))
		return -EFAULT;
		
	return ret;
}

extern asmlinkage ssize_t sys_sendfile64(int out_fd, int in_fd, loff_t *offset, size_t count);

asmlinkage int sys32_sendfile64(int out_fd, int in_fd, compat_loff_t *offset, s32 count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	loff_t lof;
	
	if (offset && get_user(lof, offset))
		return -EFAULT;
		
	set_fs(KERNEL_DS);
	ret = sys_sendfile64(out_fd, in_fd, offset ? &lof : NULL, count);
	set_fs(old_fs);
	
	if (offset && put_user(lof, offset))
		return -EFAULT;
		
	return ret;
}

/*
 * count32() counts the number of arguments/envelopes
 */
static int count32(u32 * argv, int max)
{
	int i = 0;

	if (argv != NULL) {
		for (;;) {
			u32 p; int error;

			error = get_user(p,argv);
			if (error)
				return error;
			if (!p)
				break;
			argv++;
			if (++i > max)
				return -E2BIG;
		}
	}
	return i;
}

/*
 * 'copy_string32()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 */
static int copy_strings32(int argc, u32 * argv, struct linux_binprm *bprm)
{
	while (argc-- > 0) {
		u32 str;
		int len;
		unsigned long pos;

		if (get_user(str, argv + argc) ||
		    !str ||
		    !(len = strnlen_user((char *)A(str), bprm->p)))
			return -EFAULT;

		if (bprm->p < len)
			return -E2BIG;

		bprm->p -= len;

		pos = bprm->p;
		while (len) {
			char *kaddr;
			struct page *page;
			int offset, bytes_to_copy, new, err;

			offset = pos % PAGE_SIZE;
			page = bprm->page[pos / PAGE_SIZE];
			new = 0;
			if (!page) {
				page = alloc_page(GFP_USER);
				bprm->page[pos / PAGE_SIZE] = page;
				if (!page)
					return -ENOMEM;
				new = 1;
			}
			kaddr = (char *)kmap(page);

			if (new && offset)
				memset(kaddr, 0, offset);
			bytes_to_copy = PAGE_SIZE - offset;
			if (bytes_to_copy > len) {
				bytes_to_copy = len;
				if (new)
					memset(kaddr+offset+len, 0,
					       PAGE_SIZE-offset-len);
			}

			err = copy_from_user(kaddr + offset, (char *)A(str),
					     bytes_to_copy);
			kunmap((unsigned long)kaddr);

			if (err)
				return -EFAULT;

			pos += bytes_to_copy;
			str += bytes_to_copy;
			len -= bytes_to_copy;
		}
	}
	return 0;
}

/*
 * sys32_execve() executes a new program.
 */
static int do_execve32(char * filename, u32 * argv, u32 * envp, struct pt_regs * regs)
{
	struct linux_binprm bprm;
	struct file * file;
	int retval;
	int i;

	file = open_exec(filename);

	retval = PTR_ERR(file);
	if (IS_ERR(file))
		return retval;

	bprm.p = PAGE_SIZE*MAX_ARG_PAGES-sizeof(void *);
	memset(bprm.page, 0, MAX_ARG_PAGES * sizeof(bprm.page[0]));

	bprm.file = file;
	bprm.filename = filename;
	bprm.interp = filename;
	bprm.sh_bang = 0;
	bprm.loader = 0;
	bprm.exec = 0;
	bprm.security = NULL;
	bprm.mm = mm_alloc();
	retval = -ENOMEM;
	if (!bprm.mm)
		goto out_file;

	retval = init_new_context(current, bprm.mm);
	if (retval < 0)
		goto out_mm;

	bprm.argc = count32(argv, bprm.p / sizeof(u32));
	if ((retval = bprm.argc) < 0)
		goto out_mm;

	bprm.envc = count32(envp, bprm.p / sizeof(u32));
	if ((retval = bprm.envc) < 0)
		goto out_mm;

	retval = security_bprm_alloc(&bprm);
	if (retval)
		goto out;

	retval = prepare_binprm(&bprm);
	if (retval < 0) 
		goto out; 

	retval = copy_strings_kernel(1, &bprm.filename, &bprm);
	if (retval < 0) 
		goto out; 

	bprm.exec = bprm.p;
	retval = copy_strings32(bprm.envc, envp, &bprm);
	if (retval < 0) 
		goto out; 

	retval = copy_strings32(bprm.argc, argv, &bprm);
	if (retval < 0) 
		goto out; 

	retval = search_binary_handler(&bprm,regs);
	if (retval >= 0) {
		/* execve success */
		security_bprm_free(&bprm);
		return retval;
	}

out:
	/* Something went wrong, return the inode and free the argument pages*/
	for (i = 0 ; i < MAX_ARG_PAGES ; i++) {
		struct page * page = bprm.page[i];
		if (page)
			__free_page(page);
	}

	if (bprm.security)
		security_bprm_free(&bprm);

out_mm:
	mmdrop(bprm.mm);

out_file:
	if (bprm.file) {
		allow_write_access(bprm.file);
		fput(bprm.file);
	}
	return retval;
}

long sys32_execve(unsigned long a0, unsigned long a1, unsigned long a2,
		  unsigned long a3, unsigned long a4, unsigned long a5,
		  struct pt_regs *regs)
{
	int error;
	char * filename;
	
	filename = getname((char *) a0);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	if (regs->msr & MSR_FP)
		giveup_fpu(current);

	error = do_execve32(filename, (u32*) a1, (u32*) a2, regs);

	if (error == 0)
		current->ptrace &= ~PT_DTRACE;
	putname(filename);

out:
	return error;
}

/* Set up a thread for executing a new program. */
void start_thread32(struct pt_regs* regs, unsigned long nip, unsigned long sp)
{
	set_fs(USER_DS);
	memset(regs->gpr, 0, sizeof(regs->gpr));
	memset(&regs->ctr, 0, 4 * sizeof(regs->ctr));
	regs->nip = nip;
	regs->gpr[1] = sp;
	regs->msr = MSR_USER32;
	if (last_task_used_math == current)
		last_task_used_math = 0;
	current->thread.fpscr = 0;
}

extern asmlinkage int sys_prctl(int option, unsigned long arg2, unsigned long arg3,
				unsigned long arg4, unsigned long arg5);

/* Note: it is necessary to treat option as an unsigned int, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_prctl(u32 option, u32 arg2, u32 arg3, u32 arg4, u32 arg5)
{
	return sys_prctl((int)option,
			 (unsigned long) arg2,
			 (unsigned long) arg3,
			 (unsigned long) arg4,
			 (unsigned long) arg5);
}

extern asmlinkage int sys_sched_rr_get_interval(pid_t pid, struct timespec *interval);

/* Note: it is necessary to treat pid as an unsigned int, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage int sys32_sched_rr_get_interval(u32 pid, struct compat_timespec *interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sched_rr_get_interval((int)pid, &t);
	set_fs (old_fs);
	if (put_compat_timespec(&t, interval))
		return -EFAULT;
	return ret;
}

extern asmlinkage int sys_pciconfig_read(unsigned long bus, unsigned long dfn, unsigned long off,
					 unsigned long len, unsigned char *buf);

asmlinkage int sys32_pciconfig_read(u32 bus, u32 dfn, u32 off, u32 len, u32 ubuf)
{
	return sys_pciconfig_read((unsigned long) bus,
				  (unsigned long) dfn,
				  (unsigned long) off,
				  (unsigned long) len,
				  (unsigned char *)AA(ubuf));
}




extern asmlinkage int sys_pciconfig_write(unsigned long bus, unsigned long dfn, unsigned long off,
					                                unsigned long len, unsigned char *buf);

asmlinkage int sys32_pciconfig_write(u32 bus, u32 dfn, u32 off, u32 len, u32 ubuf)
{
	return sys_pciconfig_write((unsigned long) bus,
				   (unsigned long) dfn,
				   (unsigned long) off,
				   (unsigned long) len,
				   (unsigned char *)AA(ubuf));
}

extern asmlinkage int sys_newuname(struct new_utsname * name);

asmlinkage int ppc64_newuname(struct new_utsname * name)
{
	int errno = sys_newuname(name);

	if (current->personality == PER_LINUX32 && !errno) {
		if(copy_to_user(name->machine, "ppc\0\0", 8)) {
			errno = -EFAULT;
		}
	}
	return errno;
}

extern asmlinkage long sys_personality(unsigned long);

asmlinkage int ppc64_personality(unsigned long personality)
{
	int ret;
	if (current->personality == PER_LINUX32 && personality == PER_LINUX)
		personality = PER_LINUX32;
	ret = sys_personality(personality);
	if (ret == PER_LINUX32)
		ret = PER_LINUX;
	return ret;
}



extern asmlinkage long sys_access(const char * filename, int mode);

/* Note: it is necessary to treat mode as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_access(const char * filename, u32 mode)
{
	return sys_access(filename, (int)mode);
}


extern asmlinkage long sys_creat(const char * pathname, int mode);

/* Note: it is necessary to treat mode as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_creat(const char * pathname, u32 mode)
{
	return sys_creat(pathname, (int)mode);
}


extern asmlinkage long sys_waitpid(pid_t pid, unsigned int * stat_addr, int options);

/* Note: it is necessary to treat pid and options as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_waitpid(u32 pid, unsigned int * stat_addr, u32 options)
{
	return sys_waitpid((int)pid, stat_addr, (int)options);
}


extern asmlinkage long sys_getgroups(int gidsetsize, gid_t *grouplist);

/* Note: it is necessary to treat gidsetsize as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_getgroups(u32 gidsetsize, gid_t *grouplist)
{
	return sys_getgroups((int)gidsetsize, grouplist);
}


extern asmlinkage long sys_getpgid(pid_t pid);

/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_getpgid(u32 pid)
{
	return sys_getpgid((int)pid);
}


extern asmlinkage long sys_getpriority(int which, int who);

/* Note: it is necessary to treat which and who as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_getpriority(u32 which, u32 who)
{
	return sys_getpriority((int)which, (int)who);
}


extern asmlinkage long sys_getsid(pid_t pid);

/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_getsid(u32 pid)
{
	return sys_getsid((int)pid);
}

extern asmlinkage long sys_kill(int pid, int sig);

/* Note: it is necessary to treat pid and sig as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_kill(u32 pid, u32 sig)
{
	return sys_kill((int)pid, (int)sig);
}


extern asmlinkage long sys_mkdir(const char * pathname, int mode);

/* Note: it is necessary to treat mode as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_mkdir(const char * pathname, u32 mode)
{
	return sys_mkdir(pathname, (int)mode);
}

extern asmlinkage long sys_nice(int increment);

long sys32_nice(u32 increment)
{
	/* sign extend increment */
	return sys_nice((int)increment);
}

extern off_t sys_lseek(unsigned int fd, off_t offset, unsigned int origin);

off_t ppc32_lseek(unsigned int fd, u32 offset, unsigned int origin)
{
	/* sign extend n */
	return sys_lseek(fd, (int)offset, origin);
}

/*
 * This is just a version for 32-bit applications which does
 * not force O_LARGEFILE on.
 */
long sys32_open(const char * filename, int flags, int mode)
{
	char * tmp;
	int fd, error;

	tmp = getname(filename);
	fd = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {
		fd = get_unused_fd();
		if (fd >= 0) {
			struct file * f = filp_open(tmp, flags, mode);
			error = PTR_ERR(f);
			if (IS_ERR(f))
				goto out_error;
			fd_install(fd, f);
		}
out:
		putname(tmp);
	}
	return fd;

out_error:
	put_unused_fd(fd);
	fd = error;
	goto out;
}

extern asmlinkage long sys_readlink(const char * path, char * buf, int bufsiz);

/* Note: it is necessary to treat bufsiz as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_readlink(const char * path, char * buf, u32 bufsiz)
{
	return sys_readlink(path, buf, (int)bufsiz);
}

extern asmlinkage long sys_sched_get_priority_max(int policy);

/* Note: it is necessary to treat option as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_get_priority_max(u32 policy)
{
	return sys_sched_get_priority_max((int)policy);
}


extern asmlinkage long sys_sched_get_priority_min(int policy);

/* Note: it is necessary to treat policy as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_get_priority_min(u32 policy)
{
	return sys_sched_get_priority_min((int)policy);
}


extern asmlinkage long sys_sched_getparam(pid_t pid, struct sched_param *param);

/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_getparam(u32 pid, struct sched_param *param)
{
	return sys_sched_getparam((int)pid, param);
}


extern asmlinkage long sys_sched_getscheduler(pid_t pid);

/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_getscheduler(u32 pid)
{
	return sys_sched_getscheduler((int)pid);
}


extern asmlinkage long sys_sched_setparam(pid_t pid, struct sched_param *param);

/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_setparam(u32 pid, struct sched_param *param)
{
	return sys_sched_setparam((int)pid, param);
}


extern asmlinkage long sys_sched_setscheduler(pid_t pid, int policy, struct sched_param *param);

/* Note: it is necessary to treat pid and policy as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_setscheduler(u32 pid, u32 policy, struct sched_param *param)
{
	return sys_sched_setscheduler((int)pid, (int)policy, param);
}


extern asmlinkage long sys_setdomainname(char *name, int len);

/* Note: it is necessary to treat len as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_setdomainname(char *name, u32 len)
{
	return sys_setdomainname(name, (int)len);
}


extern asmlinkage long sys_setgroups(int gidsetsize, gid_t *grouplist);

/* Note: it is necessary to treat gidsetsize as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_setgroups(u32 gidsetsize, gid_t *grouplist)
{
	return sys_setgroups((int)gidsetsize, grouplist);
}


extern asmlinkage long sys_sethostname(char *name, int len);

asmlinkage long sys32_sethostname(char *name, u32 len)
{
	/* sign extend len */
	return sys_sethostname(name, (int)len);
}


extern asmlinkage long sys_setpgid(pid_t pid, pid_t pgid);

/* Note: it is necessary to treat pid and pgid as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_setpgid(u32 pid, u32 pgid)
{
	return sys_setpgid((int)pid, (int)pgid);
}


extern asmlinkage long sys_setpriority(int which, int who, int niceval);

long sys32_setpriority(u32 which, u32 who, u32 niceval)
{
	/* sign extend which, who and niceval */
	return sys_setpriority((int)which, (int)who, (int)niceval);
}

extern asmlinkage long sys_ssetmask(int newmask);

/* Note: it is necessary to treat newmask as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_ssetmask(u32 newmask)
{
	return sys_ssetmask((int) newmask);
}

extern asmlinkage long sys_syslog(int type, char * buf, int len);

long sys32_syslog(u32 type, char * buf, u32 len)
{
	/* sign extend len */
	return sys_syslog(type, buf, (int)len);
}


extern asmlinkage long sys_umask(int mask);

/* Note: it is necessary to treat mask as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_umask(u32 mask)
{
	return sys_umask((int)mask);
}


extern asmlinkage long sys_umount(char * name, int flags);

/* Note: it is necessary to treat flags as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_umount(char * name, u32 flags)
{
	return sys_umount(name, (int)flags);
}

struct __sysctl_args32 {
	u32 name;
	int nlen;
	u32 oldval;
	u32 oldlenp;
	u32 newval;
	u32 newlen;
	u32 __unused[4];
};

extern asmlinkage long sys32_sysctl(struct __sysctl_args32 *args)
{
	struct __sysctl_args32 tmp;
	int error;
	size_t oldlen, *oldlenp = NULL;
	unsigned long addr = (((long)&args->__unused[0]) + 7) & ~7;

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	if (tmp.oldval && tmp.oldlenp) {
		/* Duh, this is ugly and might not work if sysctl_args
		   is in read-only memory, but do_sysctl does indirectly
		   a lot of uaccess in both directions and we'd have to
		   basically copy the whole sysctl.c here, and
		   glibc's __sysctl uses rw memory for the structure
		   anyway.  */
		if (get_user(oldlen, (u32 *)A(tmp.oldlenp)) ||
		    put_user(oldlen, (size_t *)addr))
			return -EFAULT;
		oldlenp = (size_t *)addr;
	}

	lock_kernel();
	error = do_sysctl((int *)A(tmp.name), tmp.nlen, (void *)A(tmp.oldval),
			  oldlenp, (void *)A(tmp.newval), tmp.newlen);
	unlock_kernel();
	if (oldlenp) {
		if (!error) {
			if (get_user(oldlen, (size_t *)addr) ||
			    put_user(oldlen, (u32 *)A(tmp.oldlenp)))
				error = -EFAULT;
		}
		copy_to_user(args->__unused, tmp.__unused, sizeof(tmp.__unused));
	}
	return error;
}

asmlinkage long sys32_time(compat_time_t* tloc)
{
	compat_time_t secs;

	struct timeval tv;

	do_gettimeofday( &tv );
	secs = tv.tv_sec;

	if (tloc) {
		if (put_user(secs,tloc))
			secs = -EFAULT;
	}

	return secs;
}

int sys32_olduname(struct oldold_utsname * name)
{
	int error;
	
	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE,name,sizeof(struct oldold_utsname)))
		return -EFAULT;
  
	down_read(&uts_sem);
	error = __copy_to_user(&name->sysname,&system_utsname.sysname,__OLD_UTS_LEN);
	error -= __put_user(0,name->sysname+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->nodename,&system_utsname.nodename,__OLD_UTS_LEN);
	error -= __put_user(0,name->nodename+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->release,&system_utsname.release,__OLD_UTS_LEN);
	error -= __put_user(0,name->release+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->version,&system_utsname.version,__OLD_UTS_LEN);
	error -= __put_user(0,name->version+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->machine,&system_utsname.machine,__OLD_UTS_LEN);
	error = __put_user(0,name->machine+__OLD_UTS_LEN);
	up_read(&uts_sem);

	error = error ? -EFAULT : 0;
	
	return error;
}

extern unsigned long sys_mmap(unsigned long addr, size_t len,
			      unsigned long prot, unsigned long flags,
			      unsigned long fd, off_t offset);

unsigned long sys32_mmap2(unsigned long addr, size_t len,
			  unsigned long prot, unsigned long flags,
			  unsigned long fd, unsigned long pgoff)
{
	/* This should remain 12 even if PAGE_SIZE changes */
	return sys_mmap(addr, len, prot, flags, fd, pgoff << 12);
}

extern long sys_io_setup(unsigned nr_reqs, aio_context_t *ctx);

long sys32_io_setup(unsigned nr_reqs, u32 *ctx32p)
{
	long ret;
	aio_context_t ctx64;
	mm_segment_t oldfs = get_fs();

	if (get_user((u32)ctx64, ctx32p))
		return -EFAULT;

	set_fs(KERNEL_DS);
	ret = sys_io_setup(nr_reqs, &ctx64);
	set_fs(oldfs);

	/* truncating is ok because it's a user address */
	if (!ret)
		ret = put_user((u32)ctx64, ctx32p);

	return ret;
}

long sys_io_getevents(aio_context_t ctx_id, long min_nr, long nr,
		      struct io_event *events, struct timespec *timeout);

long sys32_io_getevents(aio_context_t ctx_id, u32 min_nr, u32 nr,
			struct io_event *events, struct compat_timespec *t32)
{
	struct timespec t;
	long ret;
	mm_segment_t oldfs = get_fs();

	if (t32) {
		if (get_user(t.tv_sec, &t32->tv_sec) ||
		    __get_user(t.tv_nsec, &t32->tv_nsec))
			return -EFAULT;
	}

	if (verify_area(VERIFY_WRITE, events, nr * sizeof(*events)))
		return -EFAULT;

	set_fs(KERNEL_DS);
	/* sign extend min_nr and nr */
	ret = sys_io_getevents(ctx_id, (int)min_nr, (int)nr, events,
			       t32 ? &t : NULL);
	set_fs(oldfs);

	return ret;
}

long sys32_io_submit(aio_context_t ctx_id, u32 number, u32 *iocbpp)
{
	struct kioctx *ctx;
	long ret = 0;
	int i;
	int nr = (int)number;	/* sign extend */

	if (unlikely(nr < 0))
		return -EINVAL;

	if (unlikely(!access_ok(VERIFY_READ, iocbpp, (nr*sizeof(u32)))))
		return -EFAULT;

	ctx = lookup_ioctx(ctx_id);
	if (unlikely(!ctx)) {
		pr_debug("EINVAL: io_submit: invalid context id\n");
		return -EINVAL;
	}

	for (i=0; i<nr; i++) {
		struct iocb tmp;
		u32 *user_iocb;

		if (unlikely(__get_user((u32)(long)user_iocb, iocbpp + i))) {
			ret = -EFAULT;
			break;
		}

		if (unlikely(copy_from_user(&tmp, user_iocb, sizeof(tmp)))) {
			ret = -EFAULT;
			break;
		}

		ret = io_submit_one(ctx, (struct iocb *)user_iocb, &tmp);
		if (ret)
			break;
	}

	put_ioctx(ctx);
	return i ? i : ret;
}

int get_compat_timeval(struct timeval *tv, struct compat_timeval *ctv)
{
	return (verify_area(VERIFY_READ, ctv, sizeof(*ctv)) ||
		__get_user(tv->tv_sec, &ctv->tv_sec) ||
		__get_user(tv->tv_usec, &ctv->tv_usec)) ? -EFAULT : 0;
}

long sys32_utimes(char *filename, struct compat_timeval *tvs)
{
	char *kfilename;
	struct timeval ktvs[2];
	mm_segment_t old_fs;
	long ret;

	kfilename = getname(filename);
	ret = PTR_ERR(kfilename);
	if (!IS_ERR(kfilename)) {
		if (tvs) {
			if (get_compat_timeval(&ktvs[0], &tvs[0]) ||
			    get_compat_timeval(&ktvs[1], &tvs[1]))
				return -EFAULT;
		}

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = do_utimes(kfilename, (tvs ? &ktvs[0] : NULL));
		set_fs(old_fs);

		putname(kfilename);
	}
	return ret;
}

extern long sys_tgkill(int tgid, int pid, int sig);

long sys32_tgkill(u32 tgid, u32 pid, int sig)
{
	/* sign extend tgid, pid */
	return sys_tgkill((int)tgid, (int)pid, sig);
}

/* 
 * long long munging:
 * The 32 bit ABI passes long longs in an odd even register pair.
 */
extern ssize_t sys_pread64(unsigned int fd, char *buf, size_t count,
			   loff_t pos);

extern ssize_t sys_pwrite64(unsigned int fd, const char *buf, size_t count,
			    loff_t pos);

compat_ssize_t sys32_pread64(unsigned int fd, char *ubuf, compat_size_t count,
			     u32 reg6, u32 poshi, u32 poslo)
{
	return sys_pread64(fd, ubuf, count, ((loff_t)poshi << 32) | poslo);
}

compat_ssize_t sys32_pwrite64(unsigned int fd, char *ubuf, compat_size_t count,
			      u32 reg6, u32 poshi, u32 poslo)
{
	return sys_pwrite64(fd, ubuf, count, ((loff_t)poshi << 32) | poslo);
}

extern ssize_t sys_readahead(int fd, loff_t offset, size_t count);

compat_ssize_t sys32_readahead(int fd, u32 r4, u32 offhi, u32 offlo, u32 count)
{
	return sys_readahead(fd, ((loff_t)offhi << 32) | offlo, count);
}

extern asmlinkage long sys_truncate(const char * path, unsigned long length);
extern asmlinkage long sys_ftruncate(unsigned int fd, unsigned long length);

asmlinkage int sys32_truncate64(const char * path, u32 reg4,
				unsigned long high, unsigned long low)
{
	return sys_truncate(path, (high << 32) | low);
}

asmlinkage int sys32_ftruncate64(unsigned int fd, u32 reg4, unsigned long high,
				 unsigned long low)
{
	return sys_ftruncate(fd, (high << 32) | low);
}

extern long sys_lookup_dcookie(u64 cookie64, char *buf, size_t len);

long ppc32_lookup_dcookie(u32 cookie_high, u32 cookie_low, char *buf,
			  size_t len)
{
	return sys_lookup_dcookie((u64)cookie_high << 32 | cookie_low,
				  buf, len);
}

extern int sys_fadvise64(int fd, loff_t offset, size_t len, int advice);

long ppc32_fadvise64(int fd, u32 unused, u32 offset_high, u32 offset_low,
		     size_t len, int advice)
{
	return sys_fadvise64(fd, (u64)offset_high << 32 | offset_low, len,
			     advice);
}


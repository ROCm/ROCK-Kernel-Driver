/*
 *  linux/fs/read_write.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/slab.h> 
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/smp_lock.h>
#include <linux/dnotify.h>
#include <linux/security.h>

#include <asm/uaccess.h>

struct file_operations generic_ro_fops = {
	llseek:		generic_file_llseek,
	read:		generic_file_read,
	mmap:		generic_file_mmap,
	sendfile:	generic_file_sendfile,
};

loff_t generic_file_llseek(struct file *file, loff_t offset, int origin)
{
	long long retval;
	struct inode *inode = file->f_dentry->d_inode->i_mapping->host;

	down(&inode->i_sem);
	switch (origin) {
		case 2:
			offset += inode->i_size;
			break;
		case 1:
			offset += file->f_pos;
	}
	retval = -EINVAL;
	if (offset>=0 && offset<=inode->i_sb->s_maxbytes) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_version = ++event;
		}
		retval = offset;
	}
	up(&inode->i_sem);
	return retval;
}

loff_t remote_llseek(struct file *file, loff_t offset, int origin)
{
	long long retval;

	lock_kernel();
	switch (origin) {
		case 2:
			offset += file->f_dentry->d_inode->i_size;
			break;
		case 1:
			offset += file->f_pos;
	}
	retval = -EINVAL;
	if (offset>=0 && offset<=file->f_dentry->d_inode->i_sb->s_maxbytes) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_version = ++event;
		}
		retval = offset;
	}
	unlock_kernel();
	return retval;
}

loff_t no_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

loff_t default_llseek(struct file *file, loff_t offset, int origin)
{
	long long retval;

	lock_kernel();
	switch (origin) {
		case 2:
			offset += file->f_dentry->d_inode->i_size;
			break;
		case 1:
			offset += file->f_pos;
	}
	retval = -EINVAL;
	if (offset >= 0) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_version = ++event;
		}
		retval = offset;
	}
	unlock_kernel();
	return retval;
}

static inline loff_t llseek(struct file *file, loff_t offset, int origin)
{
	loff_t (*fn)(struct file *, loff_t, int);

	fn = default_llseek;
	if (file->f_op && file->f_op->llseek)
		fn = file->f_op->llseek;
	return fn(file, offset, origin);
}

asmlinkage off_t sys_lseek(unsigned int fd, off_t offset, unsigned int origin)
{
	off_t retval;
	struct file * file;

	retval = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad;

	retval = security_ops->file_llseek(file);
	if (retval) {
		fput(file);
		goto bad;
	}

	retval = -EINVAL;
	if (origin <= 2) {
		loff_t res = llseek(file, offset, origin);
		retval = res;
		if (res != (loff_t)retval)
			retval = -EOVERFLOW;	/* LFS: should only happen on 32 bit platforms */
	}
	fput(file);
bad:
	return retval;
}

#if !defined(__alpha__)
asmlinkage long sys_llseek(unsigned int fd, unsigned long offset_high,
			   unsigned long offset_low, loff_t * result,
			   unsigned int origin)
{
	int retval;
	struct file * file;
	loff_t offset;

	retval = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad;

	retval = security_ops->file_llseek(file);
	if (retval)
		goto out_putf;

	retval = -EINVAL;
	if (origin > 2)
		goto out_putf;

	offset = llseek(file, ((loff_t) offset_high << 32) | offset_low,
			origin);

	retval = (int)offset;
	if (offset >= 0) {
		retval = -EFAULT;
		if (!copy_to_user(result, &offset, sizeof(offset)))
			retval = 0;
	}
out_putf:
	fput(file);
bad:
	return retval;
}
#endif

ssize_t vfs_read(struct file *file, char *buf, size_t count, loff_t *pos)
{
	struct inode *inode = file->f_dentry->d_inode;
	ssize_t ret;

	if (!(file->f_mode & FMODE_READ))
		return -EBADF;
	if (!file->f_op || !file->f_op->read)
		return -EINVAL;

	ret = locks_verify_area(FLOCK_VERIFY_READ, inode, file, *pos, count);
	if (!ret) {
		ret = security_ops->file_permission (file, MAY_READ);
		if (!ret) {
			ret = file->f_op->read(file, buf, count, pos);
			if (ret > 0)
				dnotify_parent(file->f_dentry, DN_ACCESS);
		}
	}

	return ret;
}

ssize_t vfs_write(struct file *file, const char *buf, size_t count, loff_t *pos)
{
	struct inode *inode = file->f_dentry->d_inode;
	ssize_t ret;

	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (!file->f_op || !file->f_op->write)
		return -EINVAL;

	ret = locks_verify_area(FLOCK_VERIFY_WRITE, inode, file, *pos, count);
	if (!ret) {
		ret = security_ops->file_permission (file, MAY_WRITE);
		if (!ret) {
			ret = file->f_op->write(file, buf, count, pos);
			if (ret > 0)
				dnotify_parent(file->f_dentry, DN_MODIFY);
		}
	}

	return ret;
}

asmlinkage ssize_t sys_read(unsigned int fd, char * buf, size_t count)
{
	struct file *file;
	ssize_t ret = -EBADF;

	file = fget(fd);
	if (file) {
		ret = vfs_read(file, buf, count, &file->f_pos);
		fput(file);
	}

	return ret;
}

asmlinkage ssize_t sys_write(unsigned int fd, const char * buf, size_t count)
{
	struct file *file;
	ssize_t ret = -EBADF;

	file = fget(fd);
	if (file) {
		ret = vfs_write(file, buf, count, &file->f_pos);
		fput(file);
	}

	return ret;
}

asmlinkage ssize_t sys_pread64(unsigned int fd, char *buf,
			     size_t count, loff_t pos)
{
	struct file *file;
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	file = fget(fd);
	if (file) {
		ret = vfs_read(file, buf, count, &pos);
		fput(file);
	}

	return ret;
}

asmlinkage ssize_t sys_pwrite64(unsigned int fd, const char *buf,
			      size_t count, loff_t pos)
{
	struct file *file;
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	file = fget(fd);
	if (file) {
		ret = vfs_write(file, buf, count, &pos);
		fput(file);
	}

	return ret;
}

static ssize_t do_readv_writev(int type, struct file *file,
			       const struct iovec * vector,
			       unsigned long count)
{
	typedef ssize_t (*io_fn_t)(struct file *, char *, size_t, loff_t *);
	typedef ssize_t (*iov_fn_t)(struct file *, const struct iovec *, unsigned long, loff_t *);

	size_t tot_len;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov=iovstack;
	ssize_t ret, i;
	io_fn_t fn;
	iov_fn_t fnv;
	struct inode *inode;

	/*
	 * First get the "struct iovec" from user memory and
	 * verify all the pointers
	 */
	ret = 0;
	if (!count)
		goto out_nofree;
	ret = -EINVAL;
	if (count > UIO_MAXIOV)
		goto out_nofree;
	if (!file->f_op)
		goto out_nofree;
	if (count > UIO_FASTIOV) {
		ret = -ENOMEM;
		iov = kmalloc(count*sizeof(struct iovec), GFP_KERNEL);
		if (!iov)
			goto out_nofree;
	}
	ret = -EFAULT;
	if (copy_from_user(iov, vector, count*sizeof(*vector)))
		goto out;

	/*
	 * Single unix specification:
	 * We should -EINVAL if an element length is not >= 0 and fitting an ssize_t
	 * The total length is fitting an ssize_t
	 *
	 * Be careful here because iov_len is a size_t not an ssize_t
	 */
	 
	tot_len = 0;
	ret = -EINVAL;
	for (i = 0 ; i < count ; i++) {
		ssize_t tmp = tot_len;
		ssize_t len = (ssize_t)iov[i].iov_len;
		if (len < 0)	/* size_t not fitting an ssize_t .. */
			goto out;
		tot_len += len;
		if (tot_len < tmp) /* maths overflow on the ssize_t */
			goto out;
	}

	inode = file->f_dentry->d_inode;
	/* VERIFY_WRITE actually means a read, as we write to user space */
	ret = locks_verify_area((type == VERIFY_WRITE
				 ? FLOCK_VERIFY_READ : FLOCK_VERIFY_WRITE),
				inode, file, file->f_pos, tot_len);
	if (ret) goto out;

	fnv = (type == VERIFY_WRITE ? file->f_op->readv : file->f_op->writev);
	if (fnv) {
		ret = fnv(file, iov, count, &file->f_pos);
		goto out;
	}

	/* VERIFY_WRITE actually means a read, as we write to user space */
	fn = (type == VERIFY_WRITE ? file->f_op->read :
	      (io_fn_t) file->f_op->write);

	ret = 0;
	vector = iov;
	while (count > 0) {
		void * base;
		size_t len;
		ssize_t nr;

		base = vector->iov_base;
		len = vector->iov_len;
		vector++;
		count--;

		nr = fn(file, base, len, &file->f_pos);

		if (nr < 0) {
			if (!ret) ret = nr;
			break;
		}
		ret += nr;
		if (nr != len)
			break;
	}

out:
	if (iov != iovstack)
		kfree(iov);
out_nofree:
	/* VERIFY_WRITE actually means a read, as we write to user space */
	if ((ret + (type == VERIFY_WRITE)) > 0)
		dnotify_parent(file->f_dentry,
			(type == VERIFY_WRITE) ? DN_MODIFY : DN_ACCESS);
	return ret;
}

asmlinkage ssize_t sys_readv(unsigned long fd, const struct iovec * vector,
			     unsigned long count)
{
	struct file * file;
	ssize_t ret;


	ret = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	if (file->f_op && (file->f_mode & FMODE_READ) &&
	    (file->f_op->readv || file->f_op->read)) {
		ret = security_ops->file_permission (file, MAY_READ);
		if (!ret)
			ret = do_readv_writev(VERIFY_WRITE, file, vector, count);
	}
	fput(file);

bad_file:
	return ret;
}

asmlinkage ssize_t sys_writev(unsigned long fd, const struct iovec * vector,
			      unsigned long count)
{
	struct file * file;
	ssize_t ret;


	ret = -EBADF;
	file = fget(fd);
	if (!file)
		goto bad_file;
	if (file->f_op && (file->f_mode & FMODE_WRITE) &&
	    (file->f_op->writev || file->f_op->write)) {
		ret = security_ops->file_permission (file, MAY_WRITE);
		if (!ret)
			ret = do_readv_writev(VERIFY_READ, file, vector, count);
	}
	fput(file);

bad_file:
	return ret;
}

static ssize_t do_sendfile(int out_fd, int in_fd, loff_t *ppos,
			   size_t count, loff_t max)
{
	struct file * in_file, * out_file;
	struct inode * in_inode, * out_inode;
	loff_t pos;
	ssize_t retval;

	/*
	 * Get input file, and verify that it is ok..
	 */
	retval = -EBADF;
	in_file = fget(in_fd);
	if (!in_file)
		goto out;
	if (!(in_file->f_mode & FMODE_READ))
		goto fput_in;
	retval = -EINVAL;
	in_inode = in_file->f_dentry->d_inode;
	if (!in_inode)
		goto fput_in;
	if (!in_file->f_op || !in_file->f_op->sendfile)
		goto fput_in;
	retval = locks_verify_area(FLOCK_VERIFY_READ, in_inode, in_file, in_file->f_pos, count);
	if (retval)
		goto fput_in;

	/*
	 * Get output file, and verify that it is ok..
	 */
	retval = -EBADF;
	out_file = fget(out_fd);
	if (!out_file)
		goto fput_in;
	if (!(out_file->f_mode & FMODE_WRITE))
		goto fput_out;
	retval = -EINVAL;
	if (!out_file->f_op || !out_file->f_op->sendpage)
		goto fput_out;
	out_inode = out_file->f_dentry->d_inode;
	retval = locks_verify_area(FLOCK_VERIFY_WRITE, out_inode, out_file, out_file->f_pos, count);
	if (retval)
		goto fput_out;

	if (!ppos)
		ppos = &in_file->f_pos;

	if (!max)
		max = min(in_inode->i_sb->s_maxbytes, out_inode->i_sb->s_maxbytes);

	pos = *ppos;
	retval = -EINVAL;
	if (unlikely(pos < 0))
		goto fput_out;
	if (unlikely(pos + count > max)) {
		retval = -EOVERFLOW;
		if (pos >= max)
			goto fput_out;
		count = max - pos;
	}

	retval = in_file->f_op->sendfile(out_file, in_file, ppos, count);

	if (*ppos > max)
		retval = -EOVERFLOW;

fput_out:
	fput(out_file);
fput_in:
	fput(in_file);
out:
	return retval;
}

asmlinkage ssize_t sys_sendfile(int out_fd, int in_fd, off_t *offset, size_t count)
{
	loff_t pos;
	off_t off;
	ssize_t ret;

	if (offset) {
		if (unlikely(get_user(off, offset)))
			return -EFAULT;
		pos = off;
		ret = do_sendfile(out_fd, in_fd, &pos, count, MAX_NON_LFS);
		if (unlikely(put_user(pos, offset)))
			return -EFAULT;
		return ret;
	}

	return do_sendfile(out_fd, in_fd, NULL, count, MAX_NON_LFS);
}

asmlinkage ssize_t sys_sendfile64(int out_fd, int in_fd, loff_t *offset, size_t count)
{
	loff_t pos;
	ssize_t ret;

	if (offset) {
		if (unlikely(copy_from_user(&pos, offset, sizeof(loff_t))))
			return -EFAULT;
		ret = do_sendfile(out_fd, in_fd, &pos, count, 0);
		if (unlikely(put_user(pos, offset)))
			return -EFAULT;
		return ret;
	}

	return do_sendfile(out_fd, in_fd, NULL, count, 0);
}

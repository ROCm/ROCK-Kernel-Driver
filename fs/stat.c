/*
 *  linux/fs/stat.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/smp_lock.h>
#include <linux/highuid.h>
#include <linux/fs.h>

#include <asm/uaccess.h>

/*
 * Revalidate the inode. This is required for proper NFS attribute caching.
 */
static __inline__ int
do_revalidate(struct dentry *dentry)
{
	struct inode * inode = dentry->d_inode;
	if (inode->i_op && inode->i_op->revalidate)
		return inode->i_op->revalidate(dentry);
	return 0;
}

static int do_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	int res = 0;
	unsigned int blocks, indirect;
	struct inode *inode = dentry->d_inode;

	res = do_revalidate(dentry);
	if (res)
		return res;

	stat->dev = kdev_t_to_nr(inode->i_dev);
	stat->ino = inode->i_ino;
	stat->mode = inode->i_mode;
	stat->nlink = inode->i_nlink;
	stat->uid = inode->i_uid;
	stat->gid = inode->i_gid;
	stat->rdev = kdev_t_to_nr(inode->i_rdev);
	stat->atime = inode->i_atime;
	stat->mtime = inode->i_mtime;
	stat->ctime = inode->i_ctime;
	stat->ctime = inode->i_ctime;
	stat->size = inode->i_size;
/*
 * st_blocks and st_blksize are approximated with a simple algorithm if
 * they aren't supported directly by the filesystem. The minix and msdos
 * filesystems don't keep track of blocks, so they would either have to
 * be counted explicitly (by delving into the file itself), or by using
 * this simple algorithm to get a reasonable (although not 100% accurate)
 * value.
 */

/*
 * Use minix fs values for the number of direct and indirect blocks.  The
 * count is now exact for the minix fs except that it counts zero blocks.
 * Everything is in units of BLOCK_SIZE until the assignment to
 * tmp.st_blksize.
 */
#define D_B   7
#define I_B   (BLOCK_SIZE / sizeof(unsigned short))

	if (!inode->i_blksize) {
		blocks = (stat->size + BLOCK_SIZE - 1) >> BLOCK_SIZE_BITS;
		if (blocks > D_B) {
			indirect = (blocks - D_B + I_B - 1) / I_B;
			blocks += indirect;
			if (indirect > 1) {
				indirect = (indirect - 1 + I_B - 1) / I_B;
				blocks += indirect;
				if (indirect > 1)
					blocks++;
			}
		}
		stat->blocks = (BLOCK_SIZE / 512) * blocks;
		stat->blksize = BLOCK_SIZE;
	} else {
		stat->blocks = inode->i_blocks;
		stat->blksize = inode->i_blksize;
	}
	return 0;
}

int vfs_stat(char *name, struct kstat *stat)
{
	struct nameidata nd;
	int error;

	error = user_path_walk(name, &nd);
	if (!error) {
		error = do_getattr(nd.mnt, nd.dentry, stat);
		path_release(&nd);
	}
	return error;
}

int vfs_lstat(char *name, struct kstat *stat)
{
	struct nameidata nd;
	int error;

	error = user_path_walk_link(name, &nd);
	if (!error) {
		error = do_getattr(nd.mnt, nd.dentry, stat);
		path_release(&nd);
	}
	return error;
}

int vfs_fstat(unsigned int fd, struct kstat *stat)
{
	struct file *f = fget(fd);
	int error = -EBADF;

	if (f) {
		error = do_getattr(f->f_vfsmnt, f->f_dentry, stat);
		fput(f);
	}
	return error;
}

#if !defined(__alpha__) && !defined(__sparc__) && !defined(__ia64__) && !defined(CONFIG_ARCH_S390) && !defined(__hppa__) && !defined(__x86_64__)

/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
static int cp_old_stat(struct kstat *stat, struct __old_kernel_stat * statbuf)
{
	static int warncount = 5;
	struct __old_kernel_stat tmp;

	if (warncount > 0) {
		warncount--;
		printk(KERN_WARNING "VFS: Warning: %s using old stat() call. Recompile your binary.\n",
			current->comm);
	} else if (warncount < 0) {
		/* it's laughable, but... */
		warncount = 0;
	}

	tmp.st_dev = stat->dev;
	tmp.st_ino = stat->ino;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	SET_OLDSTAT_UID(tmp, stat->uid);
	SET_OLDSTAT_GID(tmp, stat->gid);
	tmp.st_rdev = stat->rdev;
#if BITS_PER_LONG == 32
	if (stat->size > MAX_NON_LFS)
		return -EOVERFLOW;
#endif	
	tmp.st_size = stat->size;
	tmp.st_atime = stat->atime;
	tmp.st_mtime = stat->mtime;
	tmp.st_ctime = stat->ctime;
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

asmlinkage long sys_stat(char * filename, struct __old_kernel_stat * statbuf)
{
	struct kstat stat;
	int error = vfs_stat(filename, &stat);

	if (!error)
		error = cp_old_stat(&stat, statbuf);

	return error;
}
asmlinkage long sys_lstat(char * filename, struct __old_kernel_stat * statbuf)
{
	struct kstat stat;
	int error = vfs_lstat(filename, &stat);

	if (!error)
		error = cp_old_stat(&stat, statbuf);

	return error;
}
asmlinkage long sys_fstat(unsigned int fd, struct __old_kernel_stat * statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_old_stat(&stat, statbuf);

	return error;
}

#endif

static int cp_new_stat(struct kstat *stat, struct stat *statbuf)
{
	struct stat tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.st_dev = stat->dev;
	tmp.st_ino = stat->ino;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	SET_STAT_UID(tmp, stat->uid);
	SET_STAT_GID(tmp, stat->gid);
	tmp.st_rdev = stat->rdev;
#if BITS_PER_LONG == 32
	if (stat->size > MAX_NON_LFS)
		return -EOVERFLOW;
#endif	
	tmp.st_size = stat->size;
	tmp.st_atime = stat->atime;
	tmp.st_mtime = stat->mtime;
	tmp.st_ctime = stat->ctime;
	tmp.st_blocks = stat->blocks;
	tmp.st_blksize = stat->blksize;
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

asmlinkage long sys_newstat(char * filename, struct stat * statbuf)
{
	struct kstat stat;
	int error = vfs_stat(filename, &stat);

	if (!error)
		error = cp_new_stat(&stat, statbuf);

	return error;
}
asmlinkage long sys_newlstat(char * filename, struct stat * statbuf)
{
	struct kstat stat;
	int error = vfs_lstat(filename, &stat);

	if (!error)
		error = cp_new_stat(&stat, statbuf);

	return error;
}
asmlinkage long sys_newfstat(unsigned int fd, struct stat * statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_new_stat(&stat, statbuf);

	return error;
}

asmlinkage long sys_readlink(const char * path, char * buf, int bufsiz)
{
	struct nameidata nd;
	int error;

	if (bufsiz <= 0)
		return -EINVAL;

	error = user_path_walk_link(path, &nd);
	if (!error) {
		struct inode * inode = nd.dentry->d_inode;

		error = -EINVAL;
		if (inode->i_op && inode->i_op->readlink &&
		    !(error = do_revalidate(nd.dentry))) {
			UPDATE_ATIME(inode);
			error = inode->i_op->readlink(nd.dentry, buf, bufsiz);
		}
		path_release(&nd);
	}
	return error;
}


/* ---------- LFS-64 ----------- */
#if !defined(__alpha__) && !defined(__ia64__) && !defined(__mips64) && !defined(__x86_64__) && !defined(CONFIG_ARCH_S390X)

static long cp_new_stat64(struct kstat *stat, struct stat64 *statbuf)
{
	struct stat64 tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.st_dev = stat->dev;
	tmp.st_ino = stat->ino;
#ifdef STAT64_HAS_BROKEN_ST_INO
	tmp.__st_ino = stat->ino;
#endif
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	tmp.st_uid = stat->uid;
	tmp.st_gid = stat->gid;
	tmp.st_rdev = stat->rdev;
	tmp.st_atime = stat->atime;
	tmp.st_mtime = stat->mtime;
	tmp.st_ctime = stat->ctime;
	tmp.st_size = stat->size;
	tmp.st_blocks = stat->blocks;
	tmp.st_blksize = stat->blksize;
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

asmlinkage long sys_stat64(char * filename, struct stat64 * statbuf, long flags)
{
	struct kstat stat;
	int error = vfs_stat(filename, &stat);

	if (!error)
		error = cp_new_stat64(&stat, statbuf);

	return error;
}
asmlinkage long sys_lstat64(char * filename, struct stat64 * statbuf, long flags)
{
	struct kstat stat;
	int error = vfs_lstat(filename, &stat);

	if (!error)
		error = cp_new_stat64(&stat, statbuf);

	return error;
}
asmlinkage long sys_fstat64(unsigned long fd, struct stat64 * statbuf, long flags)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_new_stat64(&stat, statbuf);

	return error;
}

#endif /* LFS-64 */

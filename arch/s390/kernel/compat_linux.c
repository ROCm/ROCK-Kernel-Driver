/*
 *  arch/s390x/kernel/linux32.c
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Gerhard Tonn (ton@de.ibm.com)   
 *               Thomas Spatzier (tspat@de.ibm.com)
 *
 *  Conversion between 31bit and 64bit native syscalls.
 *
 * Heavily inspired by the 32-bit Sparc compat code which is 
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 *
 */


#include <linux/config.h>
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
#include <linux/nfs_fs.h>
#include <linux/quota.h>
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
#include <linux/syscalls.h>
#include <linux/sysctl.h>
#include <linux/binfmts.h>
#include <linux/compat.h>
#include <linux/vfs.h>
#include <linux/ptrace.h>

#include <asm/types.h>
#include <asm/ipc.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>

#include <net/scm.h>
#include <net/sock.h>

#include "compat_linux.h"

 
/* For this source file, we want overflow handling. */

#undef high2lowuid
#undef high2lowgid
#undef low2highuid
#undef low2highgid
#undef SET_UID16
#undef SET_GID16
#undef NEW_TO_OLD_UID
#undef NEW_TO_OLD_GID
#undef SET_OLDSTAT_UID
#undef SET_OLDSTAT_GID
#undef SET_STAT_UID
#undef SET_STAT_GID

#define high2lowuid(uid) ((uid) > 65535) ? (u16)overflowuid : (u16)(uid)
#define high2lowgid(gid) ((gid) > 65535) ? (u16)overflowgid : (u16)(gid)
#define low2highuid(uid) ((uid) == (u16)-1) ? (uid_t)-1 : (uid_t)(uid)
#define low2highgid(gid) ((gid) == (u16)-1) ? (gid_t)-1 : (gid_t)(gid)
#define SET_UID16(var, uid)	var = high2lowuid(uid)
#define SET_GID16(var, gid)	var = high2lowgid(gid)
#define NEW_TO_OLD_UID(uid)	high2lowuid(uid)
#define NEW_TO_OLD_GID(gid)	high2lowgid(gid)
#define SET_OLDSTAT_UID(stat, uid)	(stat).st_uid = high2lowuid(uid)
#define SET_OLDSTAT_GID(stat, gid)	(stat).st_gid = high2lowgid(gid)
#define SET_STAT_UID(stat, uid)		(stat).st_uid = high2lowuid(uid)
#define SET_STAT_GID(stat, gid)		(stat).st_gid = high2lowgid(gid)

asmlinkage long sys32_chown16(const char * filename, u16 user, u16 group)
{
	return sys_chown(filename, low2highuid(user), low2highgid(group));
}

asmlinkage long sys32_lchown16(const char * filename, u16 user, u16 group)
{
	return sys_lchown(filename, low2highuid(user), low2highgid(group));
}

asmlinkage long sys32_fchown16(unsigned int fd, u16 user, u16 group)
{
	return sys_fchown(fd, low2highuid(user), low2highgid(group));
}

asmlinkage long sys32_setregid16(u16 rgid, u16 egid)
{
	return sys_setregid(low2highgid(rgid), low2highgid(egid));
}

asmlinkage long sys32_setgid16(u16 gid)
{
	return sys_setgid((gid_t)gid);
}

asmlinkage long sys32_setreuid16(u16 ruid, u16 euid)
{
	return sys_setreuid(low2highuid(ruid), low2highuid(euid));
}

asmlinkage long sys32_setuid16(u16 uid)
{
	return sys_setuid((uid_t)uid);
}

asmlinkage long sys32_setresuid16(u16 ruid, u16 euid, u16 suid)
{
	return sys_setresuid(low2highuid(ruid), low2highuid(euid),
		low2highuid(suid));
}

asmlinkage long sys32_getresuid16(u16 *ruid, u16 *euid, u16 *suid)
{
	int retval;

	if (!(retval = put_user(high2lowuid(current->uid), ruid)) &&
	    !(retval = put_user(high2lowuid(current->euid), euid)))
		retval = put_user(high2lowuid(current->suid), suid);

	return retval;
}

asmlinkage long sys32_setresgid16(u16 rgid, u16 egid, u16 sgid)
{
	return sys_setresgid(low2highgid(rgid), low2highgid(egid),
		low2highgid(sgid));
}

asmlinkage long sys32_getresgid16(u16 *rgid, u16 *egid, u16 *sgid)
{
	int retval;

	if (!(retval = put_user(high2lowgid(current->gid), rgid)) &&
	    !(retval = put_user(high2lowgid(current->egid), egid)))
		retval = put_user(high2lowgid(current->sgid), sgid);

	return retval;
}

asmlinkage long sys32_setfsuid16(u16 uid)
{
	return sys_setfsuid((uid_t)uid);
}

asmlinkage long sys32_setfsgid16(u16 gid)
{
	return sys_setfsgid((gid_t)gid);
}

static int groups16_to_user(u16 *grouplist, struct group_info *group_info)
{
	int i;
	u16 group;

	for (i = 0; i < group_info->ngroups; i++) {
		group = (u16)GROUP_AT(group_info, i);
		if (put_user(group, grouplist+i))
			return -EFAULT;
	}

	return 0;
}

static int groups16_from_user(struct group_info *group_info, u16 *grouplist)
{
	int i;
	u16 group;

	for (i = 0; i < group_info->ngroups; i++) {
		if (get_user(group, grouplist+i))
			return  -EFAULT;
		GROUP_AT(group_info, i) = (gid_t)group;
	}

	return 0;
}

asmlinkage long sys32_getgroups16(int gidsetsize, u16 *grouplist)
{
	int i;

	if (gidsetsize < 0)
		return -EINVAL;

	get_group_info(current->group_info);
	i = current->group_info->ngroups;
	if (gidsetsize) {
		if (i > gidsetsize) {
			i = -EINVAL;
			goto out;
		}
		if (groups16_to_user(grouplist, current->group_info)) {
			i = -EFAULT;
			goto out;
		}
	}
out:
	put_group_info(current->group_info);
	return i;
}

asmlinkage long sys32_setgroups16(int gidsetsize, u16 *grouplist)
{
	struct group_info *group_info;
	int retval;

	if (!capable(CAP_SETGID))
		return -EPERM;
	if ((unsigned)gidsetsize > NGROUPS_MAX)
		return -EINVAL;

	group_info = groups_alloc(gidsetsize);
	if (!group_info)
		return -ENOMEM;
	retval = groups16_from_user(group_info, grouplist);
	if (retval) {
		put_group_info(group_info);
		return retval;
	}

	retval = set_current_groups(group_info);
	put_group_info(group_info);

	return retval;
}

asmlinkage long sys32_getuid16(void)
{
	return high2lowuid(current->uid);
}

asmlinkage long sys32_geteuid16(void)
{
	return high2lowuid(current->euid);
}

asmlinkage long sys32_getgid16(void)
{
	return high2lowgid(current->gid);
}

asmlinkage long sys32_getegid16(void)
{
	return high2lowgid(current->egid);
}

/* 32-bit timeval and related flotsam.  */

static inline long get_tv32(struct timeval *o, struct compat_timeval *i)
{
	return (!access_ok(VERIFY_READ, tv32, sizeof(*tv32)) ||
		(__get_user(o->tv_sec, &i->tv_sec) ||
		 __get_user(o->tv_usec, &i->tv_usec)));
}

static inline long put_tv32(struct compat_timeval *o, struct timeval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->tv_sec, &o->tv_sec) ||
		 __put_user(i->tv_usec, &o->tv_usec)));
}

/*
 * sys32_ipc() is the de-multiplexer for the SysV IPC calls in 32bit emulation.
 *
 * This is really horribly ugly.
 */
asmlinkage long sys32_ipc(u32 call, int first, int second, int third, u32 ptr)
{
	if (call >> 16)		/* hack for backward compatibility */
		return -EINVAL;

	call &= 0xffff;

	switch (call) {
	case SEMTIMEDOP:
		return compat_sys_semtimedop(first, compat_ptr(ptr),
					     second, compat_ptr(third));
	case SEMOP:
		/* struct sembuf is the same on 32 and 64bit :)) */
		return sys_semtimedop(first, compat_ptr(ptr),
				      second, NULL);
	case SEMGET:
		return sys_semget(first, second, third);
	case SEMCTL:
		return compat_sys_semctl(first, second, third,
					 compat_ptr(ptr));
	case MSGSND:
		return compat_sys_msgsnd(first, second, third,
					 compat_ptr(ptr));
	case MSGRCV:
		return compat_sys_msgrcv(first, second, 0, third,
					 0, compat_ptr(ptr));
	case MSGGET:
		return sys_msgget((key_t) first, second);
	case MSGCTL:
		return compat_sys_msgctl(first, second, compat_ptr(ptr));
	case SHMAT:
		return compat_sys_shmat(first, second, third,
					0, compat_ptr(ptr));
	case SHMDT:
		return sys_shmdt(compat_ptr(ptr));
	case SHMGET:
		return sys_shmget(first, second, third);
	case SHMCTL:
		return compat_sys_shmctl(first, second, compat_ptr(ptr));
	}

	return -ENOSYS;
}

asmlinkage long sys32_truncate64(const char * path, unsigned long high, unsigned long low)
{
	if ((int)high < 0)
		return -EINVAL;
	else
		return sys_truncate(path, (high << 32) | low);
}

asmlinkage long sys32_ftruncate64(unsigned int fd, unsigned long high, unsigned long low)
{
	if ((int)high < 0)
		return -EINVAL;
	else
		return sys_ftruncate(fd, (high << 32) | low);
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
		      loff_t offset, ino_t ino, unsigned int d_type)
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

asmlinkage long old32_readdir(unsigned int fd, struct old_linux_dirent32 *dirent, unsigned int count)
{
	int error = -EBADF;
	struct file * file;
	struct readdir_callback32 buf;

	file = fget(fd);
	if (!file)
		goto out;

	buf.count = 0;
	buf.dirent = dirent;

	error = vfs_readdir(file, fillonedir, &buf);
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

static int filldir(void * __buf, const char * name, int namlen, loff_t offset, ino_t ino,
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
	buf->current_dir = ((void *)dirent) + reclen;
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

	error = vfs_readdir(file, filldir, &buf);
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

#define MAX_SELECT_SECONDS \
	((unsigned long) (MAX_SCHEDULE_TIMEOUT / HZ)-1)

asmlinkage long sys32_select(int n, u32 *inp, u32 *outp, u32 *exp,
				struct compat_timeval *tvp)
{
	fd_set_bits fds;
	char *bits;
	unsigned long nn;
	long timeout;
	int ret, size;

	timeout = MAX_SCHEDULE_TIMEOUT;
	if (tvp) {
		int sec, usec;

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
	if (n > current->files->max_fdset)
		n = current->files->max_fdset;

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
		int sec = 0, usec = 0;
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

int cp_compat_stat(struct kstat *stat, struct compat_stat *statbuf)
{
	int err;

	if (!old_valid_dev(stat->dev) || !old_valid_dev(stat->rdev))
		return -EOVERFLOW;

	err = put_user(old_encode_dev(stat->dev), &statbuf->st_dev);
	err |= put_user(stat->ino, &statbuf->st_ino);
	err |= put_user(stat->mode, &statbuf->st_mode);
	err |= put_user(stat->nlink, &statbuf->st_nlink);
	err |= put_user(high2lowuid(stat->uid), &statbuf->st_uid);
	err |= put_user(high2lowgid(stat->gid), &statbuf->st_gid);
	err |= put_user(old_encode_dev(stat->rdev), &statbuf->st_rdev);
	err |= put_user(stat->size, &statbuf->st_size);
	err |= put_user(stat->atime.tv_sec, &statbuf->st_atime);
	err |= put_user(stat->atime.tv_nsec, &statbuf->st_atime_nsec);
	err |= put_user(stat->mtime.tv_sec, &statbuf->st_mtime);
	err |= put_user(stat->mtime.tv_nsec, &statbuf->st_mtime_nsec);
	err |= put_user(stat->ctime.tv_sec, &statbuf->st_ctime);
	err |= put_user(stat->ctime.tv_nsec, &statbuf->st_ctime_nsec);
	err |= put_user(stat->blksize, &statbuf->st_blksize);
	err |= put_user(stat->blocks, &statbuf->st_blocks);
/* fixme
	err |= put_user(0, &statbuf->__unused4[0]);
	err |= put_user(0, &statbuf->__unused4[1]);
*/
	return err;
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
	unsigned short pads;
	u32 totalhigh;
	u32 freehigh;
	unsigned int mem_unit;
        char _f[8];
};

asmlinkage long sys32_sysinfo(struct sysinfo32 __user *info)
{
	struct sysinfo s;
	int ret, err;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sysinfo(&s);
	set_fs (old_fs);
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

asmlinkage long sys32_sched_rr_get_interval(compat_pid_t pid,
				struct compat_timespec __user *interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sched_rr_get_interval(pid, &t);
	set_fs (old_fs);
	if (put_compat_timespec(&t, interval))
		return -EFAULT;
	return ret;
}

asmlinkage long sys32_rt_sigprocmask(int how, compat_sigset_t __user *set,
			compat_sigset_t __user *oset, size_t sigsetsize)
{
	sigset_t s;
	compat_sigset_t s32;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	if (set) {
		if (copy_from_user (&s32, set, sizeof(compat_sigset_t)))
			return -EFAULT;
		switch (_NSIG_WORDS) {
		case 4: s.sig[3] = s32.sig[6] | (((long)s32.sig[7]) << 32);
		case 3: s.sig[2] = s32.sig[4] | (((long)s32.sig[5]) << 32);
		case 2: s.sig[1] = s32.sig[2] | (((long)s32.sig[3]) << 32);
		case 1: s.sig[0] = s32.sig[0] | (((long)s32.sig[1]) << 32);
		}
	}
	set_fs (KERNEL_DS);
	ret = sys_rt_sigprocmask(how, set ? &s : NULL, oset ? &s : NULL, sigsetsize);
	set_fs (old_fs);
	if (ret) return ret;
	if (oset) {
		switch (_NSIG_WORDS) {
		case 4: s32.sig[7] = (s.sig[3] >> 32); s32.sig[6] = s.sig[3];
		case 3: s32.sig[5] = (s.sig[2] >> 32); s32.sig[4] = s.sig[2];
		case 2: s32.sig[3] = (s.sig[1] >> 32); s32.sig[2] = s.sig[1];
		case 1: s32.sig[1] = (s.sig[0] >> 32); s32.sig[0] = s.sig[0];
		}
		if (copy_to_user (oset, &s32, sizeof(compat_sigset_t)))
			return -EFAULT;
	}
	return 0;
}

asmlinkage long sys32_rt_sigpending(compat_sigset_t __user *set,
				size_t sigsetsize)
{
	sigset_t s;
	compat_sigset_t s32;
	int ret;
	mm_segment_t old_fs = get_fs();
		
	set_fs (KERNEL_DS);
	ret = sys_rt_sigpending(&s, sigsetsize);
	set_fs (old_fs);
	if (!ret) {
		switch (_NSIG_WORDS) {
		case 4: s32.sig[7] = (s.sig[3] >> 32); s32.sig[6] = s.sig[3];
		case 3: s32.sig[5] = (s.sig[2] >> 32); s32.sig[4] = s.sig[2];
		case 2: s32.sig[3] = (s.sig[1] >> 32); s32.sig[2] = s.sig[1];
		case 1: s32.sig[1] = (s.sig[0] >> 32); s32.sig[0] = s.sig[0];
		}
		if (copy_to_user (set, &s32, sizeof(compat_sigset_t)))
			return -EFAULT;
	}
	return ret;
}

extern int
copy_siginfo_to_user32(siginfo_t32 *to, siginfo_t *from);

asmlinkage long
sys32_rt_sigtimedwait(compat_sigset_t *uthese, siginfo_t32 *uinfo,
		      struct compat_timespec *uts, size_t sigsetsize)
{
	int ret, sig;
	sigset_t these;
	compat_sigset_t these32;
	struct timespec ts;
	siginfo_t info;
	long timeout = 0;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user (&these32, uthese, sizeof(compat_sigset_t)))
		return -EFAULT;

	switch (_NSIG_WORDS) {
	case 4: these.sig[3] = these32.sig[6] | (((long)these32.sig[7]) << 32);
	case 3: these.sig[2] = these32.sig[4] | (((long)these32.sig[5]) << 32);
	case 2: these.sig[1] = these32.sig[2] | (((long)these32.sig[3]) << 32);
	case 1: these.sig[0] = these32.sig[0] | (((long)these32.sig[1]) << 32);
	}
		
	/*
	 * Invert the set of allowed signals to get those we
	 * want to block.
	 */
	sigdelsetmask(&these, sigmask(SIGKILL)|sigmask(SIGSTOP));
	signotset(&these);

	if (uts) {
		if (get_compat_timespec(&ts, uts))
			return -EINVAL;
		if (ts.tv_nsec >= 1000000000L || ts.tv_nsec < 0
		    || ts.tv_sec < 0)
			return -EINVAL;
	}

	spin_lock_irq(&current->sighand->siglock);
	sig = dequeue_signal(current, &these, &info);
	if (!sig) {
		/* None ready -- temporarily unblock those we're interested
		   in so that we'll be awakened when they arrive.  */
		current->real_blocked = current->blocked;
		sigandsets(&current->blocked, &current->blocked, &these);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);

		timeout = MAX_SCHEDULE_TIMEOUT;
		if (uts)
			timeout = (timespec_to_jiffies(&ts)
				   + (ts.tv_sec || ts.tv_nsec));

		current->state = TASK_INTERRUPTIBLE;
		timeout = schedule_timeout(timeout);

		spin_lock_irq(&current->sighand->siglock);
		sig = dequeue_signal(current, &these, &info);
		current->blocked = current->real_blocked;
		siginitset(&current->real_blocked, 0);
		recalc_sigpending();
	}
	spin_unlock_irq(&current->sighand->siglock);

	if (sig) {
		ret = sig;
		if (uinfo) {
			if (copy_siginfo_to_user32(uinfo, &info))
				ret = -EFAULT;
		}
	} else {
		ret = -EAGAIN;
		if (timeout)
			ret = -EINTR;
	}

	return ret;
}

asmlinkage long
sys32_rt_sigqueueinfo(int pid, int sig, siginfo_t32 __user *uinfo)
{
	siginfo_t info;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	if (copy_from_user (&info, uinfo, 3*sizeof(int)) ||
	    copy_from_user (info._sifields._pad, uinfo->_sifields._pad, SI_PAD_SIZE))
		return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_rt_sigqueueinfo(pid, sig, &info);
	set_fs (old_fs);
	return ret;
}

extern void check_pending(int signum);

/*
 * count32() counts the number of arguments/envelopes
 */
static int count32(u32 * argv)
{
	int i = 0;

	if (argv != NULL) {
		for (;;) {
			u32 p; int error;

			error = get_user(p,argv);
			if (error) return error;
			if (!p) break;
			argv++; i++;
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
			kunmap(page);

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
static inline int 
do_execve32(char * filename, u32 * argv, u32 * envp, struct pt_regs * regs)
{
	struct linux_binprm bprm;
	struct file * file;
	int retval;
	int i;

	sched_balance_exec();

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
	bprm.mm = mm_alloc();
	retval = -ENOMEM;
	if (!bprm.mm)
		goto out_file;

	/* init_new_context is empty for s390x. */

	bprm.argc = count32(argv);
	if ((retval = bprm.argc) < 0)
		goto out_mm;

	bprm.envc = count32(envp);
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

	retval = search_binary_handler(&bprm, regs);
	if (retval >= 0) {
		/* execve success */
		security_bprm_free(&bprm);
		return retval;
	}

out:
	/* Something went wrong, return the inode and free the argument pages*/
	for (i=0 ; i<MAX_ARG_PAGES ; i++) {
		struct page * page = bprm.page[i];
		if (page)
			__free_page(page);
	}

	if (bprm.security)
		security_bprm_free(&bprm);

out_mm:
	if (bprm.mm)
		mmdrop(bprm.mm);

out_file:
	if (bprm.file) {
		allow_write_access(bprm.file);
		fput(bprm.file);
	}

	return retval;
}

/*
 * sys32_execve() executes a new program after the asm stub has set
 * things up for us.  This should basically do what I want it to.
 */
asmlinkage long
sys32_execve(struct pt_regs regs)
{
        int error;
        char * filename;

        filename = getname((char *)A(regs.orig_gpr2));
        error = PTR_ERR(filename);
        if (IS_ERR(filename))
                goto out;
        error = do_execve32(filename, (u32 *)A(regs.gprs[3]), (u32 *)A(regs.gprs[4]), &regs);
	if (error == 0)
	{
		current->ptrace &= ~PT_DTRACE;
		current->thread.fp_regs.fpc=0;
		__asm__ __volatile__
		        ("sr  0,0\n\t"
		         "sfpc 0,0\n\t"
			 : : :"0");
	}
        putname(filename);
out:
        return error;
}


#ifdef CONFIG_MODULES

asmlinkage long
sys32_init_module(void __user *umod, unsigned long len,
		const char __user *uargs)
{
	return sys_init_module(umod, len, uargs);
}

asmlinkage long
sys32_delete_module(const char __user *name_user, unsigned int flags)
{
	return sys_delete_module(name_user, flags);
}

#else /* CONFIG_MODULES */

asmlinkage long
sys32_init_module(void __user *umod, unsigned long len,
		const char __user *uargs)
{
	return -ENOSYS;
}

asmlinkage long
sys32_delete_module(const char __user *name_user, unsigned int flags)
{
	return -ENOSYS;
}

#endif  /* CONFIG_MODULES */

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
#define ca32_authd	u.u32_authd
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
	return err;
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
	karg->ca_export.ex_anon_uid = high2lowuid(karg->ca_export.ex_anon_uid);
	karg->ca_export.ex_anon_gid = high2lowgid(karg->ca_export.ex_anon_gid);
	return err;
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
	return err;
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
	return err;
}

/* This really doesn't need translations, we are only passing
 * back a union which contains opaque nfs file handle data.
 */
static int nfs_getfh32_res_trans(union nfsctl_res *kres, union nfsctl_res32 *res32)
{
	return copy_to_user(res32, kres, sizeof(*res32)) ? -EFAULT : 0;
}

long asmlinkage sys32_nfsservctl(int cmd, struct nfsctl_arg32 *arg32, union nfsctl_res32 *res32)
{
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

/* These are here just in case some old sparc32 binary calls it. */
asmlinkage long sys32_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

asmlinkage long sys32_pread64(unsigned int fd, char *ubuf,
				size_t count, u32 poshi, u32 poslo)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL;
	return sys_pread64(fd, ubuf, count, ((loff_t)AA(poshi) << 32) | AA(poslo));
}

asmlinkage long sys32_pwrite64(unsigned int fd, const char *ubuf,
				size_t count, u32 poshi, u32 poslo)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL;
	return sys_pwrite64(fd, ubuf, count, ((loff_t)AA(poshi) << 32) | AA(poslo));
}

asmlinkage compat_ssize_t sys32_readahead(int fd, u32 offhi, u32 offlo, s32 count)
{
	return sys_readahead(fd, ((loff_t)AA(offhi) << 32) | AA(offlo), count);
}

asmlinkage long sys32_sendfile(int out_fd, int in_fd, compat_off_t *offset, size_t count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	off_t of;
	
	if (offset && get_user(of, offset))
		return -EFAULT;
		
	set_fs(KERNEL_DS);
	ret = sys_sendfile(out_fd, in_fd, offset ? &of : NULL, count);
	set_fs(old_fs);
	
	if (!ret && offset && put_user(of, offset))
		return -EFAULT;
		
	return ret;
}

asmlinkage long sys32_sendfile64(int out_fd, int in_fd,
				compat_loff_t *offset, s32 count)
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

struct __sysctl_args32 {
	u32 name;
	int nlen;
	u32 oldval;
	u32 oldlenp;
	u32 newval;
	u32 newlen;
	u32 __unused[4];
};

asmlinkage long sys32_sysctl(struct __sysctl_args32 *args)
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

struct stat64_emu31 {
	unsigned long long  st_dev;
	unsigned int    __pad1;
#define STAT64_HAS_BROKEN_ST_INO        1
	u32             __st_ino;
	unsigned int    st_mode;
	unsigned int    st_nlink;
	u32             st_uid;
	u32             st_gid;
	unsigned long long  st_rdev;
	unsigned int    __pad3;
	long            st_size;
	u32             st_blksize;
	unsigned char   __pad4[4];
	u32             __pad5;     /* future possible st_blocks high bits */
	u32             st_blocks;  /* Number 512-byte blocks allocated. */
	u32             st_atime;
	u32             __pad6;
	u32             st_mtime;
	u32             __pad7;
	u32             st_ctime;
	u32             __pad8;     /* will be high 32 bits of ctime someday */
	unsigned long   st_ino;
};	

static int cp_stat64(struct stat64_emu31 *ubuf, struct kstat *stat)
{
	struct stat64_emu31 tmp;

	memset(&tmp, 0, sizeof(tmp));

	tmp.st_dev = huge_encode_dev(stat->dev);
	tmp.st_ino = stat->ino;
	tmp.__st_ino = (u32)stat->ino;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = (unsigned int)stat->nlink;
	tmp.st_uid = stat->uid;
	tmp.st_gid = stat->gid;
	tmp.st_rdev = huge_encode_dev(stat->rdev);
	tmp.st_size = stat->size;
	tmp.st_blksize = (u32)stat->blksize;
	tmp.st_blocks = (u32)stat->blocks;
	tmp.st_atime = (u32)stat->atime.tv_sec;
	tmp.st_mtime = (u32)stat->mtime.tv_sec;
	tmp.st_ctime = (u32)stat->ctime.tv_sec;

	return copy_to_user(ubuf,&tmp,sizeof(tmp)) ? -EFAULT : 0; 
}

asmlinkage long sys32_stat64(char * filename, struct stat64_emu31 * statbuf)
{
	struct kstat stat;
	int ret = vfs_stat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

asmlinkage long sys32_lstat64(char * filename, struct stat64_emu31 * statbuf)
{
	struct kstat stat;
	int ret = vfs_lstat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

asmlinkage long sys32_fstat64(unsigned long fd, struct stat64_emu31 * statbuf)
{
	struct kstat stat;
	int ret = vfs_fstat(fd, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

/*
 * Linux/i386 didn't use to be able to handle more than
 * 4 system call parameters, so these system calls used a memory
 * block for parameter passing..
 */

struct mmap_arg_struct_emu31 {
	u32	addr;
	u32	len;
	u32	prot;
	u32	flags;
	u32	fd;
	u32	offset;
};

/* common code for old and new mmaps */
static inline long do_mmap2(
	unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	struct file * file = NULL;
	unsigned long error = -EBADF;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	if (!IS_ERR((void *) error) && error + len >= 0x80000000ULL) {
		/* Result is out of bounds.  */
		do_munmap(current->mm, addr, len);
		error = -ENOMEM;
	}
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
out:    
	return error;
}


asmlinkage unsigned long
old32_mmap(struct mmap_arg_struct_emu31 *arg)
{
	struct mmap_arg_struct_emu31 a;
	int error = -EFAULT;

	if (copy_from_user(&a, arg, sizeof(a)))
		goto out;

	error = -EINVAL;
	if (a.offset & ~PAGE_MASK)
		goto out;

	error = do_mmap2(a.addr, a.len, a.prot, a.flags, a.fd, a.offset >> PAGE_SHIFT); 
out:
	return error;
}

asmlinkage long 
sys32_mmap2(struct mmap_arg_struct_emu31 *arg)
{
	struct mmap_arg_struct_emu31 a;
	int error = -EFAULT;

	if (copy_from_user(&a, arg, sizeof(a)))
		goto out;
	error = do_mmap2(a.addr, a.len, a.prot, a.flags, a.fd, a.offset);
out:
	return error;
}

asmlinkage long sys32_read(unsigned int fd, char * buf, size_t count)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL; 

	return sys_read(fd, buf, count);
}

asmlinkage long sys32_write(unsigned int fd, char * buf, size_t count)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL; 

	return sys_write(fd, buf, count);
}

asmlinkage long sys32_clone(struct pt_regs regs)
{
        unsigned long clone_flags;
        unsigned long newsp;
	int *parent_tidptr, *child_tidptr;

        clone_flags = regs.gprs[3] & 0xffffffffUL;
        newsp = regs.orig_gpr2 & 0x7fffffffUL;
	parent_tidptr = (int *) (regs.gprs[4] & 0x7fffffffUL);
	child_tidptr = (int *) (regs.gprs[5] & 0x7fffffffUL);
        if (!newsp)
                newsp = regs.gprs[15];
        return do_fork(clone_flags & ~CLONE_IDLETASK, newsp, &regs, 0,
		       parent_tidptr, child_tidptr);
}

/*
 * Wrapper function for sys_timer_create.
 */
extern asmlinkage long
sys_timer_create(clockid_t, struct sigevent *, timer_t *);

asmlinkage long
sys32_timer_create(clockid_t which_clock, struct sigevent32 *se32,
		timer_t *timer_id)
{
	struct sigevent se;
	timer_t ktimer_id;
	mm_segment_t old_fs;
	long ret;

	if (se32 == NULL)
		return sys_timer_create(which_clock, NULL, timer_id);

	/* XXX: converting se32 to se is filthy because of the
	 * two union members. For now it is ok, because the pointers
	 * are not touched in kernel.
	 */
	memset(&se, 0, sizeof(se));
	if (get_user(se.sigev_value.sival_int,  &se32->sigev_value.sival_int) ||
	    get_user(se.sigev_signo, &se32->sigev_signo) ||
	    get_user(se.sigev_notify, &se32->sigev_notify) ||
	    copy_from_user(&se._sigev_un._pad, &se32->_sigev_un._pad,
	    sizeof(se._sigev_un._pad)))
		return -EFAULT;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_timer_create(which_clock, &se, &ktimer_id);
	set_fs(old_fs);

	if (!ret)
		ret = put_user (ktimer_id, timer_id);

	return ret;
}

/*
 * sys_ia32.c: Conversion between 32bit and 64bit native syscalls. Based on
 *             sys_sparc32 
 *
 * Copyright (C) 2000		VA Linux Co
 * Copyright (C) 2000		Don Dugger <n0ano@valinux.com>
 * Copyright (C) 1999 		Arun Sharma <arun.sharma@intel.com>
 * Copyright (C) 1997,1998 	Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 		David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 2000		Hewlett-Packard Co.
 * Copyright (C) 2000		David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2000,2001,2002	Andi Kleen, SuSE Labs (x86-64 port) 
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment. In 2.5 most of this should be moved to a generic directory. 
 *
 * This file assumes that there is a hole at the end of user address space.
 * 
 * Some of the functions are LE specific currently. These are hopefully all marked.
 * This should be fixed.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h> 
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
#include <linux/mm.h>
#include <linux/shm.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/nfs_fs.h>
#include <linux/smb_fs.h>
#include <linux/smb_mount.h>
#include <linux/ncp_fs.h>
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
#include <linux/ipc.h>
#include <linux/rwsem.h>
#include <linux/binfmts.h>
#include <linux/init.h>
#include <linux/aio_abi.h>
#include <linux/aio.h>
#include <linux/compat.h>
#include <linux/vfs.h>
#include <linux/ptrace.h>
#include <linux/highuid.h>
#include <asm/mman.h>
#include <asm/types.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <asm/ipc.h>
#include <asm/atomic.h>
#include <asm/ldt.h>

#include <net/scm.h>
#include <net/sock.h>
#include <asm/ia32.h>

#define A(__x)		((unsigned long)(__x))
#define AA(__x)		((unsigned long)(__x))
#define ROUND_UP(x,a)	((__typeof__(x))(((unsigned long)(x) + ((a) - 1)) & ~((a) - 1)))
#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))

int cp_compat_stat(struct kstat *kbuf, struct compat_stat *ubuf)
{
	typeof(ubuf->st_uid) uid = 0;
	typeof(ubuf->st_gid) gid = 0;
	SET_UID(uid, kbuf->uid);
	SET_GID(gid, kbuf->gid);
	if (!old_valid_dev(kbuf->dev) || !old_valid_dev(kbuf->rdev))
		return -EOVERFLOW;
	if (kbuf->size >= 0x7fffffff)
		return -EOVERFLOW;
	if (verify_area(VERIFY_WRITE, ubuf, sizeof(struct compat_stat)) ||
	    __put_user (old_encode_dev(kbuf->dev), &ubuf->st_dev) ||
	    __put_user (kbuf->ino, &ubuf->st_ino) ||
	    __put_user (kbuf->mode, &ubuf->st_mode) ||
	    __put_user (kbuf->nlink, &ubuf->st_nlink) ||
	    __put_user (uid, &ubuf->st_uid) ||
	    __put_user (gid, &ubuf->st_gid) ||
	    __put_user (old_encode_dev(kbuf->rdev), &ubuf->st_rdev) ||
	    __put_user (kbuf->size, &ubuf->st_size) ||
	    __put_user (kbuf->atime.tv_sec, &ubuf->st_atime) ||
	    __put_user (kbuf->atime.tv_nsec, &ubuf->st_atime_nsec) ||
	    __put_user (kbuf->mtime.tv_sec, &ubuf->st_mtime) ||
	    __put_user (kbuf->mtime.tv_nsec, &ubuf->st_mtime_nsec) ||
	    __put_user (kbuf->ctime.tv_sec, &ubuf->st_ctime) ||
	    __put_user (kbuf->ctime.tv_nsec, &ubuf->st_ctime_nsec) ||
	    __put_user (kbuf->blksize, &ubuf->st_blksize) ||
	    __put_user (kbuf->blocks, &ubuf->st_blocks))
		return -EFAULT;
	return 0;
}

/* Another set for IA32/LFS -- x86_64 struct stat is different due to 
   support for 64bit inode numbers. */

static int
cp_stat64(struct stat64 *ubuf, struct kstat *stat)
{
	typeof(ubuf->st_uid) uid = 0;
	typeof(ubuf->st_gid) gid = 0;
	SET_UID(uid, stat->uid);
	SET_GID(gid, stat->gid);
	if (verify_area(VERIFY_WRITE, ubuf, sizeof(struct stat64)) ||
	    __put_user(huge_encode_dev(stat->dev), &ubuf->st_dev) ||
	    __put_user (stat->ino, &ubuf->__st_ino) ||
	    __put_user (stat->ino, &ubuf->st_ino) ||
	    __put_user (stat->mode, &ubuf->st_mode) ||
	    __put_user (stat->nlink, &ubuf->st_nlink) ||
	    __put_user (uid, &ubuf->st_uid) ||
	    __put_user (gid, &ubuf->st_gid) ||
	    __put_user (huge_encode_dev(stat->rdev), &ubuf->st_rdev) ||
	    __put_user (stat->size, &ubuf->st_size) ||
	    __put_user (stat->atime.tv_sec, &ubuf->st_atime) ||
	    __put_user (stat->atime.tv_nsec, &ubuf->st_atime_nsec) ||
	    __put_user (stat->mtime.tv_sec, &ubuf->st_mtime) ||
	    __put_user (stat->mtime.tv_nsec, &ubuf->st_mtime_nsec) ||
	    __put_user (stat->ctime.tv_sec, &ubuf->st_ctime) ||
	    __put_user (stat->ctime.tv_nsec, &ubuf->st_ctime_nsec) ||
	    __put_user (stat->blksize, &ubuf->st_blksize) ||
	    __put_user (stat->blocks, &ubuf->st_blocks))
		return -EFAULT;
	return 0;
}

asmlinkage long
sys32_stat64(char * filename, struct stat64 *statbuf)
{
	struct kstat stat;
	int ret = vfs_stat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

asmlinkage long
sys32_lstat64(char * filename, struct stat64 *statbuf)
{
	struct kstat stat;
	int ret = vfs_lstat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

asmlinkage long
sys32_fstat64(unsigned int fd, struct stat64 *statbuf)
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

struct mmap_arg_struct {
	unsigned int addr;
	unsigned int len;
	unsigned int prot;
	unsigned int flags;
	unsigned int fd;
	unsigned int offset;
};

asmlinkage long
sys32_mmap(struct mmap_arg_struct *arg)
{
	struct mmap_arg_struct a;
	struct file *file = NULL;
	unsigned long retval;
	struct mm_struct *mm ;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;

	if (a.offset & ~PAGE_MASK)
		return -EINVAL; 

	if (!(a.flags & MAP_ANONYMOUS)) {
		file = fget(a.fd);
		if (!file)
			return -EBADF;
	}
	
	if (a.prot & PROT_READ) 
		a.prot |= vm_force_exec32;

	mm = current->mm; 
	down_write(&mm->mmap_sem); 
	retval = do_mmap_pgoff(file, a.addr, a.len, a.prot, a.flags, a.offset>>PAGE_SHIFT);
	if (file)
		fput(file);

	up_write(&mm->mmap_sem); 

	return retval;
}

extern asmlinkage long sys_mprotect(unsigned long start,size_t len,unsigned long prot);

asmlinkage long 
sys32_mprotect(unsigned long start, size_t len, unsigned long prot)
{
	if (prot & PROT_READ) 
		prot |= vm_force_exec32;
	return sys_mprotect(start,len,prot); 
}

asmlinkage long
sys32_pipe(int *fd)
{
	int retval;
	int fds[2];

	retval = do_pipe(fds);
	if (retval)
		goto out;
	if (copy_to_user(fd, fds, sizeof(fds)))
		retval = -EFAULT;
  out:
	return retval;
}

asmlinkage long
sys32_rt_sigaction(int sig, struct sigaction32 *act,
		   struct sigaction32 *oact,  unsigned int sigsetsize)
{
	struct k_sigaction new_ka, old_ka;
	int ret;
	compat_sigset_t set32;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(compat_sigset_t))
		return -EINVAL;

	if (act) {
		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user((long)new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags) ||
		    __get_user((long)new_ka.sa.sa_restorer, &act->sa_restorer)||
		    __copy_from_user(&set32, &act->sa_mask, sizeof(compat_sigset_t)))
			return -EFAULT;

		/* FIXME: here we rely on _COMPAT_NSIG_WORS to be >= than _NSIG_WORDS << 1 */
		switch (_NSIG_WORDS) {
		case 4: new_ka.sa.sa_mask.sig[3] = set32.sig[6]
				| (((long)set32.sig[7]) << 32);
		case 3: new_ka.sa.sa_mask.sig[2] = set32.sig[4]
				| (((long)set32.sig[5]) << 32);
		case 2: new_ka.sa.sa_mask.sig[1] = set32.sig[2]
				| (((long)set32.sig[3]) << 32);
		case 1: new_ka.sa.sa_mask.sig[0] = set32.sig[0]
				| (((long)set32.sig[1]) << 32);
		}
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		/* FIXME: here we rely on _COMPAT_NSIG_WORS to be >= than _NSIG_WORDS << 1 */
		switch (_NSIG_WORDS) {
		case 4:
			set32.sig[7] = (old_ka.sa.sa_mask.sig[3] >> 32);
			set32.sig[6] = old_ka.sa.sa_mask.sig[3];
		case 3:
			set32.sig[5] = (old_ka.sa.sa_mask.sig[2] >> 32);
			set32.sig[4] = old_ka.sa.sa_mask.sig[2];
		case 2:
			set32.sig[3] = (old_ka.sa.sa_mask.sig[1] >> 32);
			set32.sig[2] = old_ka.sa.sa_mask.sig[1];
		case 1:
			set32.sig[1] = (old_ka.sa.sa_mask.sig[0] >> 32);
			set32.sig[0] = old_ka.sa.sa_mask.sig[0];
		}
		if (verify_area(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user((long)old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user((long)old_ka.sa.sa_restorer, &oact->sa_restorer) ||
		    __put_user(old_ka.sa.sa_flags, &oact->sa_flags) ||
		    __copy_to_user(&oact->sa_mask, &set32, sizeof(compat_sigset_t)))
			return -EFAULT;
	}

	return ret;
}

asmlinkage long
sys32_sigaction (int sig, struct old_sigaction32 *act, struct old_sigaction32 *oact)
{
        struct k_sigaction new_ka, old_ka;
        int ret;

        if (act) {
		compat_old_sigset_t mask;

		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user((long)new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags) ||
		    __get_user((long)new_ka.sa.sa_restorer, &act->sa_restorer) ||
		    __get_user(mask, &act->sa_mask))
			return -EFAULT;
		siginitset(&new_ka.sa.sa_mask, mask);
        }

        ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (verify_area(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user((long)old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user((long)old_ka.sa.sa_restorer, &oact->sa_restorer) ||
		    __put_user(old_ka.sa.sa_flags, &oact->sa_flags) ||
		    __put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask))
			return -EFAULT;
        }

	return ret;
}

extern asmlinkage long sys_rt_sigprocmask(int how, sigset_t *set, sigset_t *oset,
					  size_t sigsetsize);

asmlinkage long
sys32_rt_sigprocmask(int how, compat_sigset_t *set, compat_sigset_t *oset,
		     unsigned int sigsetsize)
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
	ret = sys_rt_sigprocmask(how, set ? &s : NULL, oset ? &s : NULL,
				 sigsetsize); 
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

static inline long
get_tv32(struct timeval *o, struct compat_timeval *i)
{
	int err = -EFAULT; 
	if (access_ok(VERIFY_READ, i, sizeof(*i))) { 
		err = __get_user(o->tv_sec, &i->tv_sec);
		err |= __get_user(o->tv_usec, &i->tv_usec);
	}
	return err; 
}

static inline long
put_tv32(struct compat_timeval *o, struct timeval *i)
{
	int err = -EFAULT;
	if (access_ok(VERIFY_WRITE, o, sizeof(*o))) { 
		err = __put_user(i->tv_sec, &o->tv_sec);
		err |= __put_user(i->tv_usec, &o->tv_usec);
	} 
	return err; 
}

extern int do_setitimer(int which, struct itimerval *, struct itimerval *);

asmlinkage long
sys32_alarm(unsigned int seconds)
{
	struct itimerval it_new, it_old;
	unsigned int oldalarm;

	it_new.it_interval.tv_sec = it_new.it_interval.tv_usec = 0;
	it_new.it_value.tv_sec = seconds;
	it_new.it_value.tv_usec = 0;
	do_setitimer(ITIMER_REAL, &it_new, &it_old);
	oldalarm = it_old.it_value.tv_sec;
	/* ehhh.. We can't return 0 if we have an alarm pending.. */
	/* And we'd better return too much than too little anyway */
	if (it_old.it_value.tv_usec)
		oldalarm++;
	return oldalarm;
}

/* Translations due to time_t size differences.  Which affects all
   sorts of things, like timeval and itimerval.  */

extern struct timezone sys_tz;

asmlinkage long
sys32_gettimeofday(struct compat_timeval *tv, struct timezone *tz)
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

asmlinkage long
sys32_settimeofday(struct compat_timeval *tv, struct timezone *tz)
{
	struct timeval ktv;
	struct timespec kts;
	struct timezone ktz;

 	if (tv) {
		if (get_tv32(&ktv, tv))
			return -EFAULT;
		kts.tv_sec = ktv.tv_sec;
		kts.tv_nsec = ktv.tv_usec * NSEC_PER_USEC;
	}
	if (tz) {
		if (copy_from_user(&ktz, tz, sizeof(ktz)))
			return -EFAULT;
	}

	return do_sys_settimeofday(tv ? &kts : NULL, tz ? &ktz : NULL);
}

struct linux32_dirent {
	u32	d_ino;
	u32	d_off;
	u16	d_reclen;
	char	d_name[1];
};

struct old_linux32_dirent {
	u32	d_ino;
	u32	d_offset;
	u16	d_namlen;
	char	d_name[1];
};

struct getdents32_callback {
	struct linux32_dirent * current_dir;
	struct linux32_dirent * previous;
	int count;
	int error;
};

struct readdir32_callback {
	struct old_linux32_dirent * dirent;
	int count;
};

static int
filldir32 (void *__buf, const char *name, int namlen, loff_t offset, ino_t ino,
	   unsigned int d_type)
{
	struct linux32_dirent * dirent;
	struct getdents32_callback * buf = (struct getdents32_callback *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1, 4);

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

asmlinkage long
sys32_getdents (unsigned int fd, void * dirent, unsigned int count)
{
	struct file * file;
	struct linux32_dirent * lastdirent;
	struct getdents32_callback buf;
	int error;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = (struct linux32_dirent *) dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = vfs_readdir(file, filldir32, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.error;
	lastdirent = buf.previous;
	if (lastdirent) {
		put_user(file->f_pos, &lastdirent->d_off);
		error = count - buf.count;
	}

out_putf:
	fput(file);
out:
	return error;
}

static int
fillonedir32 (void * __buf, const char * name, int namlen, loff_t offset, ino_t ino, unsigned d_type)
{
	struct readdir32_callback * buf = (struct readdir32_callback *) __buf;
	struct old_linux32_dirent * dirent;

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

asmlinkage long
sys32_oldreaddir (unsigned int fd, void * dirent, unsigned int count)
{
	int error;
	struct file * file;
	struct readdir32_callback buf;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.count = 0;
	buf.dirent = dirent;

	error = vfs_readdir(file, fillonedir32, &buf);
	if (error >= 0)
		error = buf.count;
	fput(file);
out:
	return error;
}

/*
 * We can actually return ERESTARTSYS instead of EINTR, but I'd
 * like to be certain this leads to no problems. So I return
 * EINTR just for safety.
 *
 * Update: ERESTARTSYS breaks at least the xview clock binary, so
 * I'm trying ERESTARTNOHAND which restart only when you want to.
 */
#define MAX_SELECT_SECONDS \
	((unsigned long) (MAX_SCHEDULE_TIMEOUT / HZ)-1)
#define ROUND_UP_TIME(x,y) (((x)+(y)-1)/(y))

asmlinkage long
sys32_select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct compat_timeval *tvp32)
{
	fd_set_bits fds;
	char *bits;
	long timeout;
	int ret, size;

	timeout = MAX_SCHEDULE_TIMEOUT;
	if (tvp32) {
		time_t sec, usec;

		get_user(sec, &tvp32->tv_sec);
		get_user(usec, &tvp32->tv_usec);

		ret = -EINVAL;
		if (sec < 0 || usec < 0)
			goto out_nofds;

		if ((unsigned long) sec < MAX_SELECT_SECONDS) {
			timeout = ROUND_UP_TIME(usec, 1000000/HZ);
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

	if ((ret = get_fd_set(n, inp, fds.in)) ||
	    (ret = get_fd_set(n, outp, fds.out)) ||
	    (ret = get_fd_set(n, exp, fds.ex)))
		goto out;
	zero_fd_set(n, fds.res_in);
	zero_fd_set(n, fds.res_out);
	zero_fd_set(n, fds.res_ex);

	ret = do_select(n, &fds, &timeout);

	if (tvp32 && !(current->personality & STICKY_TIMEOUTS)) {
		time_t sec = 0, usec = 0;
		if (timeout) {
			sec = timeout / HZ;
			usec = timeout % HZ;
			usec *= (1000000/HZ);
		}
		put_user(sec, (int *)&tvp32->tv_sec);
		put_user(usec, (int *)&tvp32->tv_usec);
	}

	if (ret < 0)
		goto out;
	if (!ret) {
		ret = -ERESTARTNOHAND;
		if (signal_pending(current))
			goto out;
		ret = 0;
	}

	set_fd_set(n, inp, fds.res_in);
	set_fd_set(n, outp, fds.res_out);
	set_fd_set(n, exp, fds.res_ex);

out:
	kfree(bits);
out_nofds:
	return ret;
}

struct sel_arg_struct {
	unsigned int n;
	unsigned int inp;
	unsigned int outp;
	unsigned int exp;
	unsigned int tvp;
};

asmlinkage long
sys32_old_select(struct sel_arg_struct *arg)
{
	struct sel_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	return sys32_select(a.n, (fd_set *)A(a.inp), (fd_set *)A(a.outp), (fd_set *)A(a.exp),
			    (struct compat_timeval *)A(a.tvp));
}

asmlinkage ssize_t sys_readv(unsigned long,const struct iovec *,unsigned long);
asmlinkage ssize_t sys_writev(unsigned long,const struct iovec *,unsigned long);

static struct iovec *
get_compat_iovec(struct compat_iovec *iov32, struct iovec *iov_buf, u32 *count, int type, int *errp)
{
	int i;
	u32 buf, len;
	struct iovec *ivp, *iov;
	unsigned long totlen; 

	/* Get the "struct iovec" from user memory */

	*errp = 0;
	if (!*count)
		return 0;
	*errp = -EINVAL;
	if (*count > UIO_MAXIOV)
		return(struct iovec *)0;
	*errp = -EFAULT;
	if(verify_area(VERIFY_READ, iov32, sizeof(struct compat_iovec)*(*count)))
		return(struct iovec *)0;
	if (*count > UIO_FASTIOV) {
		*errp = -ENOMEM; 
		iov = kmalloc(*count*sizeof(struct iovec), GFP_KERNEL);
		if (!iov)
			return((struct iovec *)0);
	} else
		iov = iov_buf;

	ivp = iov;
	totlen = 0;
	for (i = 0; i < *count; i++) {
		*errp = __get_user(len, &iov32->iov_len) |
		  	__get_user(buf, &iov32->iov_base);	
		if (*errp)
			goto error;
		*errp = verify_area(type, (void *)A(buf), len);
		if (*errp) {
			if (i > 0) { 
				*count = i;
				break;
			} 
			goto error;
		}
		/* SuS checks: */
		*errp = -EINVAL; 
		if ((int)len < 0)
			goto error;
		if ((totlen += len) >= 0x7fffffff)
			goto error;			
		ivp->iov_base = (void *)A(buf);
		ivp->iov_len = (__kernel_size_t)len;
		iov32++;
		ivp++;
	}
	*errp = 0;
	return(iov);

error:
	if (iov != iov_buf)
		kfree(iov);
	return NULL;
}

asmlinkage long
sys32_readv(int fd, struct compat_iovec *vector, u32 count)
{
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov;
	int ret;
	mm_segment_t old_fs = get_fs();

	if ((iov = get_compat_iovec(vector, iovstack, &count, VERIFY_WRITE, &ret)) == NULL)
		return ret;
	set_fs(KERNEL_DS);
	ret = sys_readv(fd, iov, count);
	set_fs(old_fs);
	if (iov != iovstack)
		kfree(iov);
	return ret;
}

asmlinkage long
sys32_writev(int fd, struct compat_iovec *vector, u32 count)
{
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov;
	int ret;
	mm_segment_t old_fs = get_fs();

	if ((iov = get_compat_iovec(vector, iovstack, &count, VERIFY_READ, &ret)) == NULL)
		return ret;
	set_fs(KERNEL_DS);
	ret = sys_writev(fd, iov, count);
	set_fs(old_fs);
	if (iov != iovstack)
		kfree(iov);
	return ret;
}

/*
 * sys_time() can be implemented in user-level using
 * sys_gettimeofday().  x86-64 did this but i386 Linux did not
 * so we have to implement this system call here.
 */
asmlinkage long sys32_time(int * tloc)
{
	int i;

	/* SMP: This is fairly trivial. We grab CURRENT_TIME and 
	   stuff it to user space. No side effects */
	i = get_seconds();
	if (tloc) {
		if (put_user(i,tloc))
			i = -EFAULT;
	}
	return i;
}

extern asmlinkage long
compat_sys_wait4(compat_pid_t pid, compat_uint_t * stat_addr, int options,
		 struct compat_rusage *ru);

asmlinkage long
sys32_waitpid(compat_pid_t pid, unsigned int *stat_addr, int options)
{
	return compat_sys_wait4(pid, stat_addr, options, NULL);
}

int sys32_ni_syscall(int call)
{ 
	printk(KERN_INFO "IA32 syscall %d from %s not implemented\n", call,
	       current->comm);
	return -ENOSYS;	       
} 

/* 32-bit timeval and related flotsam.  */

extern asmlinkage long sys_sysfs(int option, unsigned long arg1,
				unsigned long arg2);

asmlinkage long
sys32_sysfs(int option, u32 arg1, u32 arg2)
{
	return sys_sysfs(option, arg1, arg2);
}

extern asmlinkage long sys_mount(char * dev_name, char * dir_name, char * type,
				unsigned long new_flags, void *data);

static char *badfs[] = {
	"smbfs", "ncpfs", NULL
}; 	

static int checktype(char *user_type) 
{ 
	int err = 0; 
	char **s,*kernel_type = getname(user_type); 
	if (!kernel_type || IS_ERR(kernel_type)) 
		return -EFAULT; 
	for (s = badfs; *s; ++s) 
		if (!strcmp(kernel_type, *s)) { 
			printk(KERN_ERR "mount32: unsupported fs `%s' -- use 64bit mount\n", *s); 
			err = -EINVAL; 
			break;
		} 	
	putname(user_type); 
	return err;
} 

asmlinkage long
sys32_mount(char *dev_name, char *dir_name, char *type,
	    unsigned long new_flags, u32 data)
{
	int err;
	if(!capable(CAP_SYS_ADMIN))
		return -EPERM;
	err = checktype(type);
	if (err)
		return err;
	return sys_mount(dev_name, dir_name, type, new_flags, (void *)AA(data));
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
        char _f[20-2*sizeof(u32)-sizeof(int)];
};

extern asmlinkage long sys_sysinfo(struct sysinfo *info);

asmlinkage long
sys32_sysinfo(struct sysinfo32 *info)
{
	struct sysinfo s;
	int ret;
	mm_segment_t old_fs = get_fs ();
	int bitcount = 0;
	
	set_fs (KERNEL_DS);
	ret = sys_sysinfo(&s);
	set_fs (old_fs);

        /* Check to see if any memory value is too large for 32-bit and scale
	 *  down if needed
	 */
	if ((s.totalram >> 32) || (s.totalswap >> 32)) {
		while (s.mem_unit < PAGE_SIZE) {
			s.mem_unit <<= 1;
			bitcount++;
		}
		s.totalram >>= bitcount;
		s.freeram >>= bitcount;
		s.sharedram >>= bitcount;
		s.bufferram >>= bitcount;
		s.totalswap >>= bitcount;
		s.freeswap >>= bitcount;
		s.totalhigh >>= bitcount;
		s.freehigh >>= bitcount;
	}

	if (verify_area(VERIFY_WRITE, info, sizeof(struct sysinfo32)) ||
	    __put_user (s.uptime, &info->uptime) ||
	    __put_user (s.loads[0], &info->loads[0]) ||
	    __put_user (s.loads[1], &info->loads[1]) ||
	    __put_user (s.loads[2], &info->loads[2]) ||
	    __put_user (s.totalram, &info->totalram) ||
	    __put_user (s.freeram, &info->freeram) ||
	    __put_user (s.sharedram, &info->sharedram) ||
	    __put_user (s.bufferram, &info->bufferram) ||
	    __put_user (s.totalswap, &info->totalswap) ||
	    __put_user (s.freeswap, &info->freeswap) ||
	    __put_user (s.procs, &info->procs) ||
	    __put_user (s.totalhigh, &info->totalhigh) || 
	    __put_user (s.freehigh, &info->freehigh) ||
	    __put_user (s.mem_unit, &info->mem_unit))
		return -EFAULT;
	return 0;
}
                
extern asmlinkage long sys_sched_rr_get_interval(pid_t pid,
						struct timespec *interval);

asmlinkage long
sys32_sched_rr_get_interval(compat_pid_t pid, struct compat_timespec *interval)
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

extern asmlinkage long sys_rt_sigpending(sigset_t *set, size_t sigsetsize);

asmlinkage long
sys32_rt_sigpending(compat_sigset_t *set, compat_size_t sigsetsize)
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

siginfo_t32 *
siginfo64to32(siginfo_t32 *d, siginfo_t *s)
{
	memset (d, 0, sizeof(siginfo_t32));
	d->si_signo = s->si_signo;
	d->si_errno = s->si_errno;
	d->si_code = s->si_code;
	if (s->si_signo >= SIGRTMIN) {
		d->si_pid = s->si_pid;
		d->si_uid = s->si_uid;
		memcpy(&d->si_int, &s->si_int, 
                       sizeof(siginfo_t) - offsetof(siginfo_t,si_int));
	} else switch (s->si_signo) {
	/* XXX: What about POSIX1.b timers */
	case SIGCHLD:
		d->si_pid = s->si_pid;
		d->si_status = s->si_status;
		d->si_utime = s->si_utime;
		d->si_stime = s->si_stime;
		break;
	case SIGSEGV:
	case SIGBUS:
	case SIGFPE:
	case SIGILL:
		d->si_addr = (long)(s->si_addr);
//		d->si_trapno = s->si_trapno;
		break;
	case SIGPOLL:
		d->si_band = s->si_band;
		d->si_fd = s->si_fd;
		break;
	default:
		d->si_pid = s->si_pid;
		d->si_uid = s->si_uid;
		break;
	}
	return d;
}

siginfo_t *
siginfo32to64(siginfo_t *d, siginfo_t32 *s)
{
	d->si_signo = s->si_signo;
	d->si_errno = s->si_errno;
	d->si_code = s->si_code;
	if (s->si_signo >= SIGRTMIN) {
		d->si_pid = s->si_pid;
		d->si_uid = s->si_uid;
		memcpy(&d->si_int,
                       &s->si_int,
                       sizeof(siginfo_t) - offsetof(siginfo_t, si_int)); 
	} else switch (s->si_signo) {
	/* XXX: What about POSIX1.b timers */
	case SIGCHLD:
		d->si_pid = s->si_pid;
		d->si_status = s->si_status;
		d->si_utime = s->si_utime;
		d->si_stime = s->si_stime;
		break;
	case SIGSEGV:
	case SIGBUS:
	case SIGFPE:
	case SIGILL:
		d->si_addr = (void *)A(s->si_addr);
//		d->si_trapno = s->si_trapno;
		break;
	case SIGPOLL:
		d->si_band = s->si_band;
		d->si_fd = s->si_fd;
		break;
	default:
		d->si_pid = s->si_pid;
		d->si_uid = s->si_uid;
		break;
	}
	return d;
}

extern asmlinkage long
sys_rt_sigtimedwait(const sigset_t *uthese, siginfo_t *uinfo,
		    const struct timespec *uts, size_t sigsetsize);

asmlinkage long
sys32_rt_sigtimedwait(compat_sigset_t *uthese, siginfo_t32 *uinfo,
		      struct compat_timespec *uts, compat_size_t sigsetsize)
{
	sigset_t s;
	compat_sigset_t s32;
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs();
	siginfo_t info;
	siginfo_t32 info32;
		
	if (copy_from_user (&s32, uthese, sizeof(compat_sigset_t)))
		return -EFAULT;
	switch (_NSIG_WORDS) {
	case 4: s.sig[3] = s32.sig[6] | (((long)s32.sig[7]) << 32);
	case 3: s.sig[2] = s32.sig[4] | (((long)s32.sig[5]) << 32);
	case 2: s.sig[1] = s32.sig[2] | (((long)s32.sig[3]) << 32);
	case 1: s.sig[0] = s32.sig[0] | (((long)s32.sig[1]) << 32);
	}
	if (uts && get_compat_timespec(&t, uts))
		return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_rt_sigtimedwait(&s, uinfo ? &info : NULL, uts ? &t : NULL,
			sigsetsize);
	set_fs (old_fs);
	if (ret >= 0 && uinfo) {
		if (copy_to_user (uinfo, siginfo64to32(&info32, &info),
				  sizeof(siginfo_t32)))
			return -EFAULT;
	}
	return ret;
}

extern asmlinkage long
sys_rt_sigqueueinfo(int pid, int sig, siginfo_t *uinfo);

asmlinkage long
sys32_rt_sigqueueinfo(int pid, int sig, siginfo_t32 *uinfo)
{
	siginfo_t info;
	siginfo_t32 info32;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	if (copy_from_user (&info32, uinfo, sizeof(siginfo_t32)))
		return -EFAULT;
	/* XXX: Is this correct? */
	siginfo32to64(&info, &info32);
	set_fs (KERNEL_DS);
	ret = sys_rt_sigqueueinfo(pid, sig, &info);
	set_fs (old_fs);
	return ret;
}

/* These are here just in case some old ia32 binary calls it. */
asmlinkage long
sys32_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}


struct sysctl_ia32 {
	unsigned int	name;
	int		nlen;
	unsigned int	oldval;
	unsigned int	oldlenp;
	unsigned int	newval;
	unsigned int	newlen;
	unsigned int	__unused[4];
};


asmlinkage long
sys32_sysctl(struct sysctl_ia32 *args32)
{
#ifndef CONFIG_SYSCTL
	return -ENOSYS; 
#else
	struct sysctl_ia32 a32;
	mm_segment_t old_fs = get_fs ();
	void *oldvalp, *newvalp;
	size_t oldlen;
	int *namep;
	long ret;
	extern int do_sysctl(int *name, int nlen, void *oldval, size_t *oldlenp,
		     void *newval, size_t newlen);


	if (copy_from_user(&a32, args32, sizeof (a32)))
		return -EFAULT;

	/*
	 * We need to pre-validate these because we have to disable address checking
	 * before calling do_sysctl() because of OLDLEN but we can't run the risk of the
	 * user specifying bad addresses here.  Well, since we're dealing with 32 bit
	 * addresses, we KNOW that access_ok() will always succeed, so this is an
	 * expensive NOP, but so what...
	 */
	namep = (int *) A(a32.name);
	oldvalp = (void *) A(a32.oldval);
	newvalp = (void *) A(a32.newval);

	if ((oldvalp && get_user(oldlen, (int *) A(a32.oldlenp)))
	    || !access_ok(VERIFY_WRITE, namep, 0)
	    || !access_ok(VERIFY_WRITE, oldvalp, 0)
	    || !access_ok(VERIFY_WRITE, newvalp, 0))
		return -EFAULT;

	set_fs(KERNEL_DS);
	lock_kernel();
	ret = do_sysctl(namep, a32.nlen, oldvalp, &oldlen, newvalp, (size_t) a32.newlen);
	unlock_kernel();
	set_fs(old_fs);

	if (oldvalp && put_user (oldlen, (int *) A(a32.oldlenp)))
		return -EFAULT;

	return ret;
#endif
}

extern asmlinkage ssize_t sys_pread64(unsigned int fd, char * buf,
				    size_t count, loff_t pos);

extern asmlinkage ssize_t sys_pwrite64(unsigned int fd, const char * buf,
				     size_t count, loff_t pos);

/* warning: next two assume little endian */ 
asmlinkage long
sys32_pread(unsigned int fd, char *ubuf, u32 count, u32 poslo, u32 poshi)
{
	return sys_pread64(fd, ubuf, count,
			 ((loff_t)AA(poshi) << 32) | AA(poslo));
}

asmlinkage long
sys32_pwrite(unsigned int fd, char *ubuf, u32 count, u32 poslo, u32 poshi)
{
	return sys_pwrite64(fd, ubuf, count,
			  ((loff_t)AA(poshi) << 32) | AA(poslo));
}


extern asmlinkage long sys_personality(unsigned long);

asmlinkage long
sys32_personality(unsigned long personality)
{
	int ret;
	if (personality(current->personality) == PER_LINUX32 && 
		personality == PER_LINUX)
		personality = PER_LINUX32;
	ret = sys_personality(personality);
	if (ret == PER_LINUX32)
		ret = PER_LINUX;
	return ret;
}

extern asmlinkage ssize_t sys_sendfile(int out_fd, int in_fd, off_t *offset,
				       size_t count); 

asmlinkage long
sys32_sendfile(int out_fd, int in_fd, compat_off_t *offset, s32 count)
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

extern asmlinkage long sys_modify_ldt(int func, void *ptr, 
				      unsigned long bytecount);

asmlinkage long sys32_modify_ldt(int func, void *ptr, unsigned long bytecount)
{
	long ret;
	if (func == 0x1 || func == 0x11) { 
		struct user_desc info;
		mm_segment_t old_fs = get_fs();
		if (bytecount != sizeof(struct user_desc))
			return -EINVAL;
		if (copy_from_user(&info, ptr, sizeof(struct user_desc)))
			return -EFAULT;
		/* lm bit was undefined in the 32bit ABI and programs
		   give it random values. Force it to zero here. */
		info.lm = 0; 
		set_fs(KERNEL_DS);
		ret = sys_modify_ldt(func, &info, bytecount);
		set_fs(old_fs);
	}  else { 
		ret = sys_modify_ldt(func, ptr, bytecount); 
	}
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

asmlinkage long
sys32_adjtimex(struct timex32 *utp)
{
	struct timex txc;
	int ret;

	memset(&txc, 0, sizeof(struct timex));

	if(verify_area(VERIFY_READ, utp, sizeof(struct timex32)) ||
	   __get_user(txc.modes, &utp->modes) ||
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

	if(verify_area(VERIFY_WRITE, utp, sizeof(struct timex32)) ||
	   __put_user(txc.modes, &utp->modes) ||
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

asmlinkage long sys32_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	struct mm_struct *mm = current->mm;
	unsigned long error;
	struct file * file = NULL;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			return -EBADF;
	}

	if (prot & PROT_READ)
		prot |= vm_force_exec32;

	down_write(&mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&mm->mmap_sem);

	if (file)
		fput(file);
	return error;
}

asmlinkage long sys32_olduname(struct oldold_utsname * name)
{
	int error;

	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE,name,sizeof(struct oldold_utsname)))
		return -EFAULT;
  
  	down_read(&uts_sem);
	
	error = __copy_to_user(&name->sysname,&system_utsname.sysname,__OLD_UTS_LEN);
	 __put_user(0,name->sysname+__OLD_UTS_LEN);
	 __copy_to_user(&name->nodename,&system_utsname.nodename,__OLD_UTS_LEN);
	 __put_user(0,name->nodename+__OLD_UTS_LEN);
	 __copy_to_user(&name->release,&system_utsname.release,__OLD_UTS_LEN);
	 __put_user(0,name->release+__OLD_UTS_LEN);
	 __copy_to_user(&name->version,&system_utsname.version,__OLD_UTS_LEN);
	 __put_user(0,name->version+__OLD_UTS_LEN);
	 { 
		 char *arch = "x86_64";
		 if (personality(current->personality) == PER_LINUX32)
			 arch = "i686";
		 
		 __copy_to_user(&name->machine,arch,strlen(arch)+1);
	 }
	
	 up_read(&uts_sem);
	 
	 error = error ? -EFAULT : 0;
	 
	 return error;
}

long sys32_uname(struct old_utsname * name)
{
	int err;
	if (!name)
		return -EFAULT;
	down_read(&uts_sem);
	err=copy_to_user(name, &system_utsname, sizeof (*name));
	up_read(&uts_sem);
	if (personality(current->personality) == PER_LINUX32) 
		err |= copy_to_user(&name->machine, "i686", 5);
	return err?-EFAULT:0;
}

extern int sys_ustat(dev_t, struct ustat *);

long sys32_ustat(unsigned dev, struct ustat32 *u32p)
{
	struct ustat u;
	mm_segment_t seg;
	int ret;
	
	seg = get_fs(); 
	set_fs(KERNEL_DS); 
	ret = sys_ustat(dev,&u); 
	set_fs(seg);
	if (ret >= 0) { 
		if (!access_ok(VERIFY_WRITE,u32p,sizeof(struct ustat32)) || 
		    __put_user((__u32) u.f_tfree, &u32p->f_tfree) ||
		    __put_user((__u32) u.f_tinode, &u32p->f_tfree) ||
		    __copy_to_user(&u32p->f_fname, u.f_fname, sizeof(u.f_fname)) ||
		    __copy_to_user(&u32p->f_fpack, u.f_fpack, sizeof(u.f_fpack)))
			ret = -EFAULT;
	}
	return ret;
} 

static int nargs(u32 src, char **dst) 
{ 
	int cnt;
	u32 val; 

	cnt = 0; 
	do { 		
		int ret = get_user(val, (__u32 *)(u64)src); 
		if (ret)
			return ret;
		if (dst)
			dst[cnt] = (char *)(u64)val; 
		cnt++;
		src += 4; 	
		if (cnt >= (MAX_ARG_PAGES*PAGE_SIZE)/sizeof(void*))
			return -E2BIG; 
	} while(val); 
	if (dst)
		dst[cnt-1] = 0; 
	return cnt; 
} 

asmlinkage long sys32_execve(char *name, u32 argv, u32 envp, struct pt_regs regs)
{ 
	mm_segment_t oldseg; 
	char **buf = NULL; 
	int na = 0,ne = 0;
	int ret;
	unsigned sz = 0; 

	if (argv) {
	na = nargs(argv, NULL); 
	if (na < 0) 
		return -EFAULT; 
	} 	
	if (envp) { 
	ne = nargs(envp, NULL); 
	if (ne < 0) 
		return -EFAULT; 
	}

	if (argv || envp) { 
	sz = (na+ne)*sizeof(void *); 
	if (sz > PAGE_SIZE) 
		buf = vmalloc(sz); 
	else
		buf = kmalloc(sz, GFP_KERNEL); 
	if (!buf)
		return -ENOMEM; 
	} 
	
	if (argv) { 
	ret = nargs(argv, buf);
	if (ret < 0)
		goto free;
	}

	if (envp) { 
	ret = nargs(envp, buf + na); 
	if (ret < 0)
		goto free; 
	}

	name = getname(name); 
	ret = PTR_ERR(name); 
	if (IS_ERR(name))
		goto free; 

	oldseg = get_fs(); 
	set_fs(KERNEL_DS);
	ret = do_execve(name, argv ? buf : NULL, envp ? buf+na : NULL, &regs);  
	set_fs(oldseg); 

	if (ret == 0)
		current->ptrace &= ~PT_DTRACE;

	putname(name);
 
free:
	if (argv || envp) { 
	if (sz > PAGE_SIZE)
		vfree(buf); 
	else
	kfree(buf);
	}
	return ret; 
} 

asmlinkage long sys32_clone(unsigned int clone_flags, unsigned int newsp, struct pt_regs regs)
{
	void *parent_tid = (void *)regs.rdx;
	void *child_tid = (void *)regs.rdi; 
	if (!newsp)
		newsp = regs.rsp;
        return do_fork(clone_flags & ~CLONE_IDLETASK, newsp, &regs, 0, 
		    parent_tid, child_tid);
}

/*
 * Some system calls that need sign extended arguments. This could be done by a generic wrapper.
 */ 

extern off_t sys_lseek (unsigned int fd, off_t offset, unsigned int origin);

long sys32_lseek (unsigned int fd, int offset, unsigned int whence)
{
	return sys_lseek(fd, offset, whence);
}

extern int sys_kill(pid_t pid, int sig); 

long sys32_kill(int pid, int sig)
{
	return sys_kill(pid, sig);
}
 

#if defined(CONFIG_NFSD) || defined(CONFIG_NFSD_MODULE)
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
	compat_pid_t	ex32_anon_uid;
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
	
	err = get_user(karg->ca_version, &arg32->ca32_version);
	err |= __get_user(karg->ca_svc.svc_port, &arg32->ca32_svc.svc32_port);
	err |= __get_user(karg->ca_svc.svc_nthreads, &arg32->ca32_svc.svc32_nthreads);
	return err;
}

static int nfs_clnt32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = get_user(karg->ca_version, &arg32->ca32_version);
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
	
	err = get_user(karg->ca_version, &arg32->ca32_version);
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
	SET_UID(karg->ca_export.ex_anon_uid, karg->ca_export.ex_anon_uid);
	SET_GID(karg->ca_export.ex_anon_gid, karg->ca_export.ex_anon_gid);
	return err;
}


static int nfs_getfd32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_getfd.gd_addr,
			  &arg32->ca32_getfd.gd32_addr,
			  (sizeof(struct sockaddr)));
	err |= copy_from_user(&karg->ca_getfd.gd_path,
			  &arg32->ca32_getfd.gd32_path,
			  (NFS_MAXPATHLEN+1));
	err |= get_user(karg->ca_getfd.gd_version,
		      &arg32->ca32_getfd.gd32_version);
	return err;
}

static int nfs_getfs32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_getfs.gd_addr,
			  &arg32->ca32_getfs.gd32_addr,
			  (sizeof(struct sockaddr)));
	err |= copy_from_user(&karg->ca_getfs.gd_path,
			  &arg32->ca32_getfs.gd32_path,
			  (NFS_MAXPATHLEN+1));
	err |= get_user(karg->ca_getfs.gd_maxlen,
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
#else /* !NFSD */
extern asmlinkage long sys_ni_syscall(void);
long asmlinkage sys32_nfsservctl(int cmd, void *notused, void *notused2)
{
	return sys_ni_syscall();
}
#endif

long sys32_module_warning(void)
{ 
		printk(KERN_INFO "%s: 32bit 2.4.x modutils not supported on 64bit kernel\n",
		       current->comm);
	return -ENOSYS ;
} 

extern long sys_io_setup(unsigned nr_reqs, aio_context_t *ctx);

long sys32_io_setup(unsigned nr_reqs, u32 *ctx32p)
{ 
	long ret; 
	aio_context_t ctx64;
	mm_segment_t oldfs = get_fs(); 	
	set_fs(KERNEL_DS); 
	ret = sys_io_setup(nr_reqs, &ctx64); 
	set_fs(oldfs); 
	/* truncating is ok because it's a user address */
	if (!ret) 
		ret = put_user((u32)ctx64, ctx32p);
	return ret;
} 

asmlinkage long sys32_io_submit(aio_context_t ctx_id, int nr,
		   compat_uptr_t *iocbpp)
{
	struct kioctx *ctx;
	long ret = 0;
	int i;
	
	if (unlikely(nr < 0))
		return -EINVAL;

	if (unlikely(!access_ok(VERIFY_READ, iocbpp, (nr*sizeof(*iocbpp)))))
		return -EFAULT;

	ctx = lookup_ioctx(ctx_id);
	if (unlikely(!ctx)) {
		pr_debug("EINVAL: io_submit: invalid context id\n");
		return -EINVAL; 
	} 

	for (i=0; i<nr; i++) {
		compat_uptr_t p32;
		struct iocb *user_iocb, tmp;

		if (unlikely(__get_user(p32, iocbpp + i))) {
			ret = -EFAULT;
			break;
		} 
		user_iocb = compat_ptr(p32);

		if (unlikely(copy_from_user(&tmp, user_iocb, sizeof(tmp)))) {
			ret = -EFAULT;
			break;
		}

		ret = io_submit_one(ctx, user_iocb, &tmp);
		if (ret)
			break;
	}

	put_ioctx(ctx);
	return i ? i : ret;
}

extern asmlinkage long sys_io_getevents(aio_context_t ctx_id,
					  long min_nr,
					  long nr,
					  struct io_event *events,
					  struct timespec *timeout);

asmlinkage long sys32_io_getevents(aio_context_t ctx_id,
				 unsigned long min_nr,
				 unsigned long nr,
				 struct io_event *events,
				 struct compat_timespec *timeout)
{ 	
	long ret;
	mm_segment_t oldfs; 
	struct timespec t;
	/* Harden against bogus ptrace */
	if (nr >= 0xffffffff || 
	    !access_ok(VERIFY_WRITE, events, nr * sizeof(struct io_event)))
		return -EFAULT;
	if (timeout && get_compat_timespec(&t, timeout))
		return -EFAULT; 
	oldfs = get_fs();
	set_fs(KERNEL_DS); 
	ret = sys_io_getevents(ctx_id,min_nr,nr,events,timeout ? &t : NULL); 
	set_fs(oldfs); 
	if (!ret && timeout && put_compat_timespec(&t, timeout))
		return -EFAULT; 		
	return ret;
} 

asmlinkage long sys32_open(const char * filename, int flags, int mode)
{
	char * tmp;
	int fd, error;

	/* don't force O_LARGEFILE */
	tmp = getname(filename);
	fd = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {
		fd = get_unused_fd();
		if (fd >= 0) {
			struct file *f = filp_open(tmp, flags, mode);
			error = PTR_ERR(f);
			if (unlikely(IS_ERR(f))) {
				put_unused_fd(fd); 
				fd = error;
			} else
				fd_install(fd, f);
		}
		putname(tmp);
	}
	return fd;
}

struct sigevent32 { 
	u32 sigev_value;
	u32 sigev_signo; 
	u32 sigev_notify; 
	u32 payload[(64 / 4) - 3]; 
}; 

extern asmlinkage long
sys_timer_create(clockid_t which_clock,
		 struct sigevent __user *timer_event_spec,
		 timer_t __user * created_timer_id);

long
sys32_timer_create(u32 clock, struct sigevent32 *se32, timer_t *timer_id)
{
	struct sigevent se;
       mm_segment_t oldfs;
       long err;

	if (se32) { 
		memset(&se, 0, sizeof(struct sigevent)); 
		if (get_user(se.sigev_value.sival_int,  &se32->sigev_value) ||
		    __get_user(se.sigev_signo, &se32->sigev_signo) ||
		    __get_user(se.sigev_notify, &se32->sigev_notify) ||
		    __copy_from_user(&se._sigev_un._pad, &se32->payload, 
				     sizeof(se32->payload)))
			return -EFAULT;
	} 
	if (!access_ok(VERIFY_WRITE,timer_id,sizeof(timer_t)))
		return -EFAULT;

       oldfs = get_fs();
	set_fs(KERNEL_DS);
       err = sys_timer_create(clock, se32 ? &se : NULL, timer_id);
	set_fs(oldfs); 
	
	return err; 
} 

extern long sys_fadvise64_64(int fd, loff_t offset, loff_t len, int advice);

long sys32_fadvise64_64(int fd, __u32 offset_low, __u32 offset_high, 
			__u32 len_low, __u32 len_high, int advice)
{ 
	return sys_fadvise64_64(fd,
			       (((u64)offset_high)<<32) | offset_low,
			       (((u64)len_high)<<32) | len_low,
			       advice); 
} 

long sys32_vm86_warning(void)
{ 
		printk(KERN_INFO "%s: vm86 mode not supported on 64 bit kernel\n",
		       current->comm);
	return -ENOSYS ;
} 


struct exec_domain ia32_exec_domain = { 
	.name = "linux/x86",
	.pers_low = PER_LINUX32,
	.pers_high = PER_LINUX32,
};      

static int __init ia32_init (void)
{
	printk("IA32 emulation $Id: sys_ia32.c,v 1.32 2002/03/24 13:02:28 ak Exp $\n");  
	ia32_exec_domain.signal_map = default_exec_domain.signal_map;
	ia32_exec_domain.signal_invmap = default_exec_domain.signal_invmap;
	register_exec_domain(&ia32_exec_domain);
	return 0;
}

__initcall(ia32_init);

extern unsigned long ia32_sys_call_table[];
EXPORT_SYMBOL(ia32_sys_call_table);

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
#include <linux/utime.h>
#include <linux/resource.h>
#include <linux/times.h>
#include <linux/utsname.h>
#include <linux/timex.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/binfmts.h>
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
#include <linux/init.h>
#include <asm/mman.h>
#include <asm/types.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <asm/ipc.h>
#include <asm/atomic.h>

#include <net/scm.h>
#include <net/sock.h>
#include <asm/ia32.h>

#define A(__x)		((unsigned long)(__x))
#define AA(__x)		((unsigned long)(__x))
#define ROUND_UP(x,a)	((__typeof__(x))(((unsigned long)(x) + ((a) - 1)) & ~((a) - 1)))
#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))

#undef high2lowuid
#undef high2lowgid
#undef low2highuid
#undef low2highgid

#define high2lowuid(uid) ((uid) > 65535) ? (u16)overflowuid : (u16)(uid)
#define high2lowgid(gid) ((gid) > 65535) ? (u16)overflowgid : (u16)(gid)
#define low2highuid(uid) ((uid) == (u16)-1) ? (uid_t)-1 : (uid_t)(uid)
#define low2highgid(gid) ((gid) == (u16)-1) ? (gid_t)-1 : (gid_t)(gid)
extern int overflowuid,overflowgid; 


static int
putstat(struct stat32 *ubuf, struct stat *kbuf)
{
	if (verify_area(VERIFY_WRITE, ubuf, sizeof(struct stat32)) ||
	    __put_user (kbuf->st_dev, &ubuf->st_dev) ||
	    __put_user (kbuf->st_ino, &ubuf->st_ino) ||
	    __put_user (kbuf->st_mode, &ubuf->st_mode) ||
	    __put_user (kbuf->st_nlink, &ubuf->st_nlink) ||
	    __put_user (kbuf->st_uid, &ubuf->st_uid) ||
	    __put_user (kbuf->st_gid, &ubuf->st_gid) ||
	    __put_user (kbuf->st_rdev, &ubuf->st_rdev) ||
	    __put_user (kbuf->st_size, &ubuf->st_size) ||
	    __put_user (kbuf->st_atime, &ubuf->st_atime) ||
	    __put_user (kbuf->st_mtime, &ubuf->st_mtime) ||
	    __put_user (kbuf->st_ctime, &ubuf->st_ctime) ||
	    __put_user (kbuf->st_blksize, &ubuf->st_blksize) ||
	    __put_user (kbuf->st_blocks, &ubuf->st_blocks))
		return -EFAULT;
	return 0;
}

extern asmlinkage long sys_newstat(char * filename, struct stat * statbuf);

asmlinkage long
sys32_newstat(char * filename, struct stat32 *statbuf)
{
	int ret;
	struct stat s;
	mm_segment_t old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_newstat(filename, &s);
	set_fs (old_fs);
	if (putstat (statbuf, &s))
		return -EFAULT;
	return ret;
}

extern asmlinkage long sys_newlstat(char * filename, struct stat * statbuf);

asmlinkage long
sys32_newlstat(char * filename, struct stat32 *statbuf)
{
	int ret;
	struct stat s;
	mm_segment_t old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_newlstat(filename, &s);
	set_fs (old_fs);
	if (putstat (statbuf, &s))
		return -EFAULT;
	return ret;
}

extern asmlinkage long sys_newfstat(unsigned int fd, struct stat * statbuf);

asmlinkage long
sys32_newfstat(unsigned int fd, struct stat32 *statbuf)
{
	int ret;
	struct stat s;
	mm_segment_t old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_newfstat(fd, &s);
	set_fs (old_fs);
	if (putstat (statbuf, &s))
		return -EFAULT;
	return ret;
}

/* Another set for IA32/LFS -- x86_64 struct stat is different due to 
   support for 64bit inode numbers. */

static int
putstat64(struct stat64 *ubuf, struct stat *kbuf)
{
	if (verify_area(VERIFY_WRITE, ubuf, sizeof(struct stat64)) ||
	    __put_user (kbuf->st_dev, &ubuf->st_dev) ||
	    __put_user (kbuf->st_ino, &ubuf->__st_ino) ||
	    __put_user (kbuf->st_ino, &ubuf->st_ino) ||
	    __put_user (kbuf->st_mode, &ubuf->st_mode) ||
	    __put_user (kbuf->st_nlink, &ubuf->st_nlink) ||
	    __put_user (kbuf->st_uid, &ubuf->st_uid) ||
	    __put_user (kbuf->st_gid, &ubuf->st_gid) ||
	    __put_user (kbuf->st_rdev, &ubuf->st_rdev) ||
	    __put_user (kbuf->st_size, &ubuf->st_size) ||
	    __put_user (kbuf->st_atime, &ubuf->st_atime) ||
	    __put_user (kbuf->st_mtime, &ubuf->st_mtime) ||
	    __put_user (kbuf->st_ctime, &ubuf->st_ctime) ||
	    __put_user (kbuf->st_blksize, &ubuf->st_blksize) ||
	    __put_user (kbuf->st_blocks, &ubuf->st_blocks))
		return -EFAULT;
	return 0;
}

asmlinkage long
sys32_stat64(char * filename, struct stat64 *statbuf)
{
	int ret;
	struct stat s;
	mm_segment_t old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_newstat(filename, &s);
	set_fs (old_fs);
	if (putstat64 (statbuf, &s))
		return -EFAULT;
	return ret;
}

asmlinkage long
sys32_lstat64(char * filename, struct stat64 *statbuf)
{
	int ret;
	struct stat s;
	mm_segment_t old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_newlstat(filename, &s);
	set_fs (old_fs);
	if (putstat64 (statbuf, &s))
		return -EFAULT;
	return ret;
}

asmlinkage long
sys32_fstat64(unsigned int fd, struct stat64 *statbuf)
{
	int ret;
	struct stat s;
	mm_segment_t old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_newfstat(fd, &s);
	set_fs (old_fs);
	if (putstat64 (statbuf, &s))
		return -EFAULT;
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

asmlinkage __u32
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
		a.prot |= PROT_EXEC; 

	a.flags |= MAP_32BIT;

	mm = current->mm; 
	down_write(&mm->mmap_sem); 
	retval = do_mmap_pgoff(file, a.addr, a.len, a.prot, a.flags, a.offset>>PAGE_SHIFT);
	if (file)
		fput(file);

	/* Should not happen */
	if (retval >= 0xFFFFFFFF && (long)retval > 0) { 
		do_munmap(mm, retval, a.len); 
		retval = -ENOMEM; 
	} 
	up_write(&mm->mmap_sem); 

	return retval;
}

extern asmlinkage long sys_mprotect(unsigned long start,size_t len,unsigned long prot);

asmlinkage int sys32_mprotect(unsigned long start, size_t len, unsigned long prot)
{
	if (prot & PROT_READ) 
		prot |= PROT_EXEC; 
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
	sigset32_t set32;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset32_t))
		return -EINVAL;

	if (act) {
		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user((long)new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags) ||
		    __get_user((long)new_ka.sa.sa_restorer, &act->sa_restorer)||
		    __copy_from_user(&set32, &act->sa_mask, sizeof(sigset32_t)))
			return -EFAULT;

		/* FIXME: here we rely on _IA32_NSIG_WORS to be >= than _NSIG_WORDS << 1 */
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
		/* FIXME: here we rely on _IA32_NSIG_WORS to be >= than _NSIG_WORDS << 1 */
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
		    __copy_to_user(&oact->sa_mask, &set32, sizeof(sigset32_t)))
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
		old_sigset32_t mask;

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
sys32_rt_sigprocmask(int how, sigset32_t *set, sigset32_t *oset,
		     unsigned int sigsetsize)
{
	sigset_t s;
	sigset32_t s32;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	if (set) {
		if (copy_from_user (&s32, set, sizeof(sigset32_t)))
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
		if (copy_to_user (oset, &s32, sizeof(sigset32_t)))
			return -EFAULT;
	}
	return 0;
}

static int
put_statfs (struct statfs32 *ubuf, struct statfs *kbuf)
{
	if (verify_area(VERIFY_WRITE, ubuf, sizeof(struct statfs32)) ||
	    __put_user (kbuf->f_type, &ubuf->f_type) ||
	    __put_user (kbuf->f_bsize, &ubuf->f_bsize) ||
	    __put_user (kbuf->f_blocks, &ubuf->f_blocks) ||
	    __put_user (kbuf->f_bfree, &ubuf->f_bfree) ||
	    __put_user (kbuf->f_bavail, &ubuf->f_bavail) ||
	    __put_user (kbuf->f_files, &ubuf->f_files) ||
	    __put_user (kbuf->f_ffree, &ubuf->f_ffree) ||
	    __put_user (kbuf->f_namelen, &ubuf->f_namelen) ||
	    __put_user (kbuf->f_fsid.val[0], &ubuf->f_fsid.val[0]) ||
	    __put_user (kbuf->f_fsid.val[1], &ubuf->f_fsid.val[1]))
		return -EFAULT;
	return 0;
}

extern asmlinkage long sys_statfs(const char * path, struct statfs * buf);

asmlinkage long
sys32_statfs(const char * path, struct statfs32 *buf)
{
	int ret;
	struct statfs s;
	mm_segment_t old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_statfs((const char *)path, &s);
	set_fs (old_fs);
	if (put_statfs(buf, &s))
		return -EFAULT;
	return ret;
}

extern asmlinkage long sys_fstatfs(unsigned int fd, struct statfs * buf);

asmlinkage long
sys32_fstatfs(unsigned int fd, struct statfs32 *buf)
{
	int ret;
	struct statfs s;
	mm_segment_t old_fs = get_fs();
	
	set_fs (KERNEL_DS);
	ret = sys_fstatfs(fd, &s);
	set_fs (old_fs);
	if (put_statfs(buf, &s))
		return -EFAULT;
	return ret;
}

struct timeval32
{
    int tv_sec, tv_usec;
};

struct itimerval32
{
    struct timeval32 it_interval;
    struct timeval32 it_value;
};

static inline long
get_tv32(struct timeval *o, struct timeval32 *i)
{
	int err = -EFAULT; 
	if (access_ok(VERIFY_READ, i, sizeof(*i))) { 
		err = __get_user(o->tv_sec, &i->tv_sec);
		err |= __get_user(o->tv_usec, &i->tv_usec);
	}
	return err; 
}

static inline long
put_tv32(struct timeval32 *o, struct timeval *i)
{
	int err = -EFAULT;
	if (access_ok(VERIFY_WRITE, o, sizeof(*o))) { 
		err = __put_user(i->tv_sec, &o->tv_sec);
		err |= __put_user(i->tv_usec, &o->tv_usec);
	} 
	return err; 
}

static inline long
get_it32(struct itimerval *o, struct itimerval32 *i)
{
	int err = -EFAULT; 
	if (access_ok(VERIFY_READ, i, sizeof(*i))) { 
		err = __get_user(o->it_interval.tv_sec, &i->it_interval.tv_sec);
		err |= __get_user(o->it_interval.tv_usec, &i->it_interval.tv_usec);
		err |= __get_user(o->it_value.tv_sec, &i->it_value.tv_sec);
		err |= __get_user(o->it_value.tv_usec, &i->it_value.tv_usec);
	}
	return err;
}

static inline long
put_it32(struct itimerval32 *o, struct itimerval *i)
{
	int err = -EFAULT;
	if (access_ok(VERIFY_WRITE, o, sizeof(*o))) {
		err = __put_user(i->it_interval.tv_sec, &o->it_interval.tv_sec);
		err |= __put_user(i->it_interval.tv_usec, &o->it_interval.tv_usec);
		err |= __put_user(i->it_value.tv_sec, &o->it_value.tv_sec);
		err |= __put_user(i->it_value.tv_usec, &o->it_value.tv_usec); 
	} 
	return err;
}

extern int do_getitimer(int which, struct itimerval *value);

asmlinkage long
sys32_getitimer(int which, struct itimerval32 *it)
{
	struct itimerval kit;
	int error;

	error = do_getitimer(which, &kit);
	if (!error && put_it32(it, &kit))
		error = -EFAULT;

	return error;
}

extern int do_setitimer(int which, struct itimerval *, struct itimerval *);

asmlinkage long
sys32_setitimer(int which, struct itimerval32 *in, struct itimerval32 *out)
{
	struct itimerval kin, kout;
	int error;

	if (in) {
		if (get_it32(&kin, in))
			return -EFAULT;
	} else
		memset(&kin, 0, sizeof(kin));

	error = do_setitimer(which, &kin, out ? &kout : NULL);
	if (error || !out)
		return error;
	if (put_it32(out, &kout))
		return -EFAULT;

	return 0;

}
asmlinkage unsigned long 
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

struct utimbuf_32 {
	int	atime;
	int	mtime;
};

extern asmlinkage long sys_utimes(char * filename, struct timeval * utimes);
extern asmlinkage long sys_gettimeofday (struct timeval *tv, struct timezone *tz);

asmlinkage long
ia32_utime(char * filename, struct utimbuf_32 *times32)
{
	mm_segment_t old_fs = get_fs();
	struct timeval tv[2];
	long ret;

	if (times32) {
		get_user(tv[0].tv_sec, &times32->atime);
		tv[0].tv_usec = 0;
		get_user(tv[1].tv_sec, &times32->mtime);
		tv[1].tv_usec = 0;
		set_fs (KERNEL_DS);
	} else {
		set_fs (KERNEL_DS);
		ret = sys_gettimeofday(&tv[0], 0);
		if (ret < 0)
			goto out;
		tv[1] = tv[0];
	}
	ret = sys_utimes(filename, tv);
  out:
	set_fs (old_fs);
	return ret;
}

extern struct timezone sys_tz;
extern int do_sys_settimeofday(struct timeval *tv, struct timezone *tz);

asmlinkage long
sys32_gettimeofday(struct timeval32 *tv, struct timezone *tz)
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
sys32_settimeofday(struct timeval32 *tv, struct timezone *tz)
{
	struct timeval ktv;
	struct timezone ktz;

 	if (tv) {
		if (get_tv32(&ktv, tv))
			return -EFAULT;
	}
	if (tz) {
		if (copy_from_user(&ktz, tz, sizeof(ktz)))
			return -EFAULT;
	}

	return do_sys_settimeofday(tv ? &ktv : NULL, tz ? &ktz : NULL);
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
sys32_select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval32 *tvp32)
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
			    (struct timeval32 *)A(a.tvp));
}

struct timespec32 {
	int 	tv_sec;
	int	tv_nsec;
};

extern asmlinkage long sys_nanosleep(struct timespec *rqtp, struct timespec *rmtp); 

asmlinkage long
sys32_nanosleep(struct timespec32 *rqtp, struct timespec32 *rmtp)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs ();
	
	if (verify_area(VERIFY_READ, rqtp, sizeof(struct timespec32)) ||
	    __get_user (t.tv_sec, &rqtp->tv_sec) ||
	    __get_user (t.tv_nsec, &rqtp->tv_nsec))
		return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_nanosleep(&t, rmtp ? &t : NULL);
	set_fs (old_fs);
	if (rmtp && ret == -EINTR) {
		if (verify_area(VERIFY_WRITE, rmtp, sizeof(struct timespec32)) ||
		    __put_user (t.tv_sec, &rmtp->tv_sec) ||
	    	    __put_user (t.tv_nsec, &rmtp->tv_nsec))
			return -EFAULT;
	}
	return ret;
}

asmlinkage ssize_t sys_readv(unsigned long,const struct iovec *,unsigned long);
asmlinkage ssize_t sys_writev(unsigned long,const struct iovec *,unsigned long);

struct iovec *
get_iovec32(struct iovec32 *iov32, struct iovec *iov_buf, u32 count, int type)
{
	int i;
	u32 buf, len;
	struct iovec *ivp, *iov;

	/* Get the "struct iovec" from user memory */

	if (!count)
		return 0;
	if(verify_area(VERIFY_READ, iov32, sizeof(struct iovec32)*count))
		return(struct iovec *)0;
	if (count > UIO_MAXIOV)
		return(struct iovec *)0;
	if (count > UIO_FASTIOV) {
		iov = kmalloc(count*sizeof(struct iovec), GFP_KERNEL);
		if (!iov)
			return((struct iovec *)0);
	} else
		iov = iov_buf;

	ivp = iov;
	for (i = 0; i < count; i++) {
		if (__get_user(len, &iov32->iov_len) ||
		    __get_user(buf, &iov32->iov_base)) {
			if (iov != iov_buf)
				kfree(iov);
			return((struct iovec *)0);
		}
		if (verify_area(type, (void *)A(buf), len)) {
			if (iov != iov_buf)
				kfree(iov);
			return((struct iovec *)0);
		}
		ivp->iov_base = (void *)A(buf);
		ivp->iov_len = (__kernel_size_t)len;
		iov32++;
		ivp++;
	}
	return(iov);
}

asmlinkage long
sys32_readv(int fd, struct iovec32 *vector, u32 count)
{
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov;
	int ret;
	mm_segment_t old_fs = get_fs();

	if ((iov = get_iovec32(vector, iovstack, count, VERIFY_WRITE)) == (struct iovec *)0)
		return -EFAULT;
	set_fs(KERNEL_DS);
	ret = sys_readv(fd, iov, count);
	set_fs(old_fs);
	if (iov != iovstack)
		kfree(iov);
	return ret;
}

asmlinkage long
sys32_writev(int fd, struct iovec32 *vector, u32 count)
{
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov;
	int ret;
	mm_segment_t old_fs = get_fs();

	if ((iov = get_iovec32(vector, iovstack, count, VERIFY_READ)) == (struct iovec *)0)
		return -EFAULT;
	set_fs(KERNEL_DS);
	ret = sys_writev(fd, iov, count);
	set_fs(old_fs);
	if (iov != iovstack)
		kfree(iov);
	return ret;
}

#define RLIM_INFINITY32	0xffffffff
#define RESOURCE32(x) ((x > RLIM_INFINITY32) ? RLIM_INFINITY32 : x)

struct rlimit32 {
	int	rlim_cur;
	int	rlim_max;
};

extern asmlinkage long sys_getrlimit(unsigned int resource, struct rlimit *rlim);

asmlinkage long
sys32_getrlimit(unsigned int resource, struct rlimit32 *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_getrlimit(resource, &r);
	set_fs(old_fs);
	if (!ret) {
		if (verify_area(VERIFY_WRITE, rlim, sizeof(struct rlimit32)) ||
		    __put_user(RESOURCE32(r.rlim_cur), &rlim->rlim_cur) ||
		    __put_user(RESOURCE32(r.rlim_max), &rlim->rlim_max))
			ret = -EFAULT;
	}
	return ret;
}

extern asmlinkage long sys_old_getrlimit(unsigned int resource, struct rlimit *rlim);

asmlinkage long
sys32_old_getrlimit(unsigned int resource, struct rlimit32 *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs;
	
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_old_getrlimit(resource, &r);
	set_fs(old_fs);
	if (!ret) {
		if (verify_area(VERIFY_WRITE, rlim, sizeof(struct rlimit32)) ||
		    __put_user(r.rlim_cur, &rlim->rlim_cur) ||
		    __put_user(r.rlim_max, &rlim->rlim_max))
			ret = -EFAULT;
	}
	return ret;
}

extern asmlinkage long sys_setrlimit(unsigned int resource, struct rlimit *rlim);

asmlinkage long
sys32_setrlimit(unsigned int resource, struct rlimit32 *rlim)
{
	struct rlimit r;
	int ret;
	mm_segment_t old_fs = get_fs ();

	if (resource >= RLIM_NLIMITS) return -EINVAL;	
	if (verify_area(VERIFY_READ, rlim, sizeof(struct rlimit32)) ||
	    __get_user (r.rlim_cur, &rlim->rlim_cur) ||
	    __get_user (r.rlim_max, &rlim->rlim_max))
		return -EFAULT;
	if (r.rlim_cur == RLIM_INFINITY32)
		r.rlim_cur = RLIM_INFINITY;
	if (r.rlim_max == RLIM_INFINITY32)
		r.rlim_max = RLIM_INFINITY;
	set_fs (KERNEL_DS);
	ret = sys_setrlimit(resource, &r);
	set_fs (old_fs);
	return ret;
}

/*
 * sys_time() can be implemented in user-level using
 * sys_gettimeofday().  IA64 did this but i386 Linux did not
 * so we have to implement this system call here.
 */
asmlinkage long sys32_time(int * tloc)
{
	int i;

	/* SMP: This is fairly trivial. We grab CURRENT_TIME and 
	   stuff it to user space. No side effects */
	i = CURRENT_TIME;
	if (tloc) {
		if (put_user(i,tloc))
			i = -EFAULT;
	}
	return i;
}

struct rusage32 {
        struct timeval32 ru_utime;
        struct timeval32 ru_stime;
        int    ru_maxrss;
        int    ru_ixrss;
        int    ru_idrss;
        int    ru_isrss;
        int    ru_minflt;
        int    ru_majflt;
        int    ru_nswap;
        int    ru_inblock;
        int    ru_oublock;
        int    ru_msgsnd; 
        int    ru_msgrcv; 
        int    ru_nsignals;
        int    ru_nvcsw;
        int    ru_nivcsw;
};

static int
put_rusage (struct rusage32 *ru, struct rusage *r)
{
	if (verify_area(VERIFY_WRITE, ru, sizeof(struct rusage32)) ||
	    __put_user (r->ru_utime.tv_sec, &ru->ru_utime.tv_sec) ||
	    __put_user (r->ru_utime.tv_usec, &ru->ru_utime.tv_usec) ||
	    __put_user (r->ru_stime.tv_sec, &ru->ru_stime.tv_sec) ||
	    __put_user (r->ru_stime.tv_usec, &ru->ru_stime.tv_usec) ||
	    __put_user (r->ru_maxrss, &ru->ru_maxrss) ||
	    __put_user (r->ru_ixrss, &ru->ru_ixrss) ||
	    __put_user (r->ru_idrss, &ru->ru_idrss) ||
	    __put_user (r->ru_isrss, &ru->ru_isrss) ||
	    __put_user (r->ru_minflt, &ru->ru_minflt) ||
	    __put_user (r->ru_majflt, &ru->ru_majflt) ||
	    __put_user (r->ru_nswap, &ru->ru_nswap) ||
	    __put_user (r->ru_inblock, &ru->ru_inblock) ||
	    __put_user (r->ru_oublock, &ru->ru_oublock) ||
	    __put_user (r->ru_msgsnd, &ru->ru_msgsnd) ||
	    __put_user (r->ru_msgrcv, &ru->ru_msgrcv) ||
	    __put_user (r->ru_nsignals, &ru->ru_nsignals) ||
	    __put_user (r->ru_nvcsw, &ru->ru_nvcsw) ||
	    __put_user (r->ru_nivcsw, &ru->ru_nivcsw))
		return -EFAULT;
	return 0;
}

extern asmlinkage long sys_wait4(pid_t pid,unsigned int * stat_addr,
				int options, struct rusage * ru);

asmlinkage long
sys32_wait4(__kernel_pid_t32 pid, unsigned int *stat_addr, int options,
	    struct rusage32 *ru)
{
	if (!ru)
		return sys_wait4(pid, stat_addr, options, NULL);
	else {
		struct rusage r;
		int ret;
		unsigned int status;
		mm_segment_t old_fs = get_fs();
		
		set_fs (KERNEL_DS);
		ret = sys_wait4(pid, stat_addr ? &status : NULL, options, &r);
		set_fs (old_fs);
		if (put_rusage (ru, &r)) return -EFAULT;
		if (stat_addr && put_user (status, stat_addr))
			return -EFAULT;
		return ret;
	}
}

asmlinkage long
sys32_waitpid(__kernel_pid_t32 pid, unsigned int *stat_addr, int options)
{
	return sys32_wait4(pid, stat_addr, options, NULL);
}


extern asmlinkage long
sys_getrusage(int who, struct rusage *ru);

asmlinkage long
sys32_getrusage(int who, struct rusage32 *ru)
{
	struct rusage r;
	int ret;
	mm_segment_t old_fs = get_fs();
		
	set_fs (KERNEL_DS);
	ret = sys_getrusage(who, &r);
	set_fs (old_fs);
	if (put_rusage (ru, &r)) return -EFAULT;
	return ret;
}

struct tms32 {
	__kernel_clock_t32 tms_utime;
	__kernel_clock_t32 tms_stime;
	__kernel_clock_t32 tms_cutime;
	__kernel_clock_t32 tms_cstime;
};
                                
extern int sys_times(struct tms *);

asmlinkage long
sys32_times(struct tms32 *tbuf)
{
	struct tms t;
	long ret;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_times(tbuf ? &t : NULL);
	set_fs (old_fs);
	if (tbuf) {
		if (verify_area(VERIFY_WRITE, tbuf, sizeof(struct tms32)) ||
		    __put_user (t.tms_utime, &tbuf->tms_utime) ||
		    __put_user (t.tms_stime, &tbuf->tms_stime) ||
		    __put_user (t.tms_cutime, &tbuf->tms_cutime) ||
		    __put_user (t.tms_cstime, &tbuf->tms_cstime))
			return -EFAULT;
	}
	return ret;
}


static inline int get_flock(struct flock *kfl, struct flock32 *ufl)
{
	int err;
	
	err = get_user(kfl->l_type, &ufl->l_type);
	err |= __get_user(kfl->l_whence, &ufl->l_whence);
	err |= __get_user(kfl->l_start, &ufl->l_start);
	err |= __get_user(kfl->l_len, &ufl->l_len);
	err |= __get_user(kfl->l_pid, &ufl->l_pid);
	return err;
}

static inline int put_flock(struct flock *kfl, struct flock32 *ufl)
{
	int err;
	
	err = __put_user(kfl->l_type, &ufl->l_type);
	err |= __put_user(kfl->l_whence, &ufl->l_whence);
	err |= __put_user(kfl->l_start, &ufl->l_start);
	err |= __put_user(kfl->l_len, &ufl->l_len);
	err |= __put_user(kfl->l_pid, &ufl->l_pid);
	return err;
}

extern asmlinkage long sys_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg);

asmlinkage long sys32_fcntl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
		{
	struct flock f;
	mm_segment_t old_fs;
	long ret;

			if (get_flock(&f, (struct flock32 *)arg))
			return -EFAULT;
			old_fs = get_fs(); set_fs (KERNEL_DS);
		ret = sys_fcntl(fd, cmd, (unsigned long)&f);
			set_fs (old_fs);
			if (ret) return ret;
			if (put_flock(&f, (struct flock32 *)arg))
			return -EFAULT;
			return 0;
		}
	default:
		return sys_fcntl(fd, cmd, (unsigned long)arg);
	}
}

asmlinkage long sys32_fcntl64(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	if (cmd >= F_GETLK64 && cmd <= F_SETLKW64)
		return sys_fcntl(fd, cmd + F_GETLK - F_GETLK64, arg);
	return sys32_fcntl(fd, cmd, arg);
}

int sys32_ni_syscall(int call)
{ 
	printk(KERN_INFO "IA32 syscall %d from %s not implemented\n", call,
	       current->comm);
	return -ENOSYS;	       
} 

/* 32-bit timeval and related flotsam.  */

extern asmlinkage long sys_utime(char * filename, struct utimbuf * times);

struct utimbuf32 {
	__kernel_time_t32 actime, modtime;
};

asmlinkage long
sys32_utime(char * filename, struct utimbuf32 *times)
{
	struct utimbuf t;
	mm_segment_t old_fs;
	int ret;
	char *filenam;
	
	if (!times)
		return sys_utime(filename, NULL);
	if (verify_area(VERIFY_READ, times, sizeof(struct utimbuf32)) ||
	    __get_user (t.actime, &times->actime) ||
	    __get_user (t.modtime, &times->modtime))
		return -EFAULT;
	filenam = getname (filename);
	ret = PTR_ERR(filenam);
	if (!IS_ERR(filenam)) {
		old_fs = get_fs();
		set_fs (KERNEL_DS); 
		ret = sys_utime(filenam, &t);
		set_fs (old_fs);
		putname(filenam);
	}
	return ret;
}

/*
 * Ooo, nasty.  We need here to frob 32-bit unsigned longs to
 * 64-bit unsigned longs.
 */

static inline int
get_fd_set32(unsigned long n, unsigned long *fdset, u32 *ufdset)
{
	if (ufdset) {
		unsigned long odd;

		if (verify_area(VERIFY_READ, ufdset, n*sizeof(u32)))
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
        char _f[22];
};

extern asmlinkage long sys_sysinfo(struct sysinfo *info);

asmlinkage long
sys32_sysinfo(struct sysinfo32 *info)
{
	struct sysinfo s;
	int ret;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sysinfo(&s);
	set_fs (old_fs);
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
	    __put_user (s.procs, &info->procs))
		return -EFAULT;
	return 0;
}
                
extern asmlinkage long sys_sched_rr_get_interval(pid_t pid,
						struct timespec *interval);

asmlinkage long
sys32_sched_rr_get_interval(__kernel_pid_t32 pid, struct timespec32 *interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sched_rr_get_interval(pid, &t);
	set_fs (old_fs);
	if (verify_area(VERIFY_WRITE, interval, sizeof(struct timespec32)) ||
	    __put_user (t.tv_sec, &interval->tv_sec) ||
	    __put_user (t.tv_nsec, &interval->tv_nsec))
		return -EFAULT;
	return ret;
}

extern asmlinkage long sys_sigprocmask(int how, old_sigset_t *set,
				      old_sigset_t *oset);

asmlinkage long
sys32_sigprocmask(int how, old_sigset32_t *set, old_sigset32_t *oset)
{
	old_sigset_t s;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	if (set && get_user (s, set)) return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_sigprocmask(how, set ? &s : NULL, oset ? &s : NULL);
	set_fs (old_fs);
	if (ret) return ret;
	if (oset && put_user (s, oset)) return -EFAULT;
	return 0;
}

extern asmlinkage long sys_sigpending(old_sigset_t *set);

asmlinkage long
sys32_sigpending(old_sigset32_t *set)
{
	old_sigset_t s;
	int ret;
	mm_segment_t old_fs = get_fs();
		
	set_fs (KERNEL_DS);
	ret = sys_sigpending(&s);
	set_fs (old_fs);
	if (put_user (s, set)) return -EFAULT;
	return ret;
}

extern asmlinkage long sys_rt_sigpending(sigset_t *set, size_t sigsetsize);

asmlinkage long
sys32_rt_sigpending(sigset32_t *set, __kernel_size_t32 sigsetsize)
{
	sigset_t s;
	sigset32_t s32;
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
		if (copy_to_user (set, &s32, sizeof(sigset32_t)))
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
		/* XXX: Ouch, how to find this out??? */
		d->si_int = s->si_int;
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
		/* XXX: Ouch, how to find this out??? */
		d->si_int = s->si_int;
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
sys32_rt_sigtimedwait(sigset32_t *uthese, siginfo_t32 *uinfo,
		      struct timespec32 *uts, __kernel_size_t32 sigsetsize)
{
	sigset_t s;
	sigset32_t s32;
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs();
	siginfo_t info;
	siginfo_t32 info32;
		
	if (copy_from_user (&s32, uthese, sizeof(sigset32_t)))
		return -EFAULT;
	switch (_NSIG_WORDS) {
	case 4: s.sig[3] = s32.sig[6] | (((long)s32.sig[7]) << 32);
	case 3: s.sig[2] = s32.sig[4] | (((long)s32.sig[5]) << 32);
	case 2: s.sig[1] = s32.sig[2] | (((long)s32.sig[3]) << 32);
	case 1: s.sig[0] = s32.sig[0] | (((long)s32.sig[1]) << 32);
	}
	if (uts) {
		if (verify_area(VERIFY_READ, uts, sizeof(struct timespec32)) ||
		    __get_user (t.tv_sec, &uts->tv_sec) ||
		    __get_user (t.tv_nsec, &uts->tv_nsec))
			return -EFAULT;
	}
	set_fs (KERNEL_DS);
	ret = sys_rt_sigtimedwait(&s, &info, &t, sigsetsize);
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

extern asmlinkage long sys_setreuid(uid_t ruid, uid_t euid);

asmlinkage long sys32_setreuid(__kernel_uid_t32 ruid, __kernel_uid_t32 euid)
{
	uid_t sruid, seuid;

	sruid = (ruid == (__kernel_uid_t32)-1) ? ((uid_t)-1) : ((uid_t)ruid);
	seuid = (euid == (__kernel_uid_t32)-1) ? ((uid_t)-1) : ((uid_t)euid);
	return sys_setreuid(sruid, seuid);
}

extern asmlinkage long sys_setresuid(uid_t ruid, uid_t euid, uid_t suid);

asmlinkage long
sys32_setresuid(__kernel_uid_t32 ruid, __kernel_uid_t32 euid,
		__kernel_uid_t32 suid)
{
	uid_t sruid, seuid, ssuid;

	sruid = (ruid == (__kernel_uid_t32)-1) ? ((uid_t)-1) : ((uid_t)ruid);
	seuid = (euid == (__kernel_uid_t32)-1) ? ((uid_t)-1) : ((uid_t)euid);
	ssuid = (suid == (__kernel_uid_t32)-1) ? ((uid_t)-1) : ((uid_t)suid);
	return sys_setresuid(sruid, seuid, ssuid);
}

extern asmlinkage long sys_getresuid(uid_t *ruid, uid_t *euid, uid_t *suid);

asmlinkage long
sys32_getresuid(__kernel_uid_t32 *ruid, __kernel_uid_t32 *euid,
		__kernel_uid_t32 *suid)
{
	uid_t a, b, c;
	int ret;
	mm_segment_t old_fs = get_fs();
		
	set_fs (KERNEL_DS);
	ret = sys_getresuid(&a, &b, &c);
	set_fs (old_fs);
	if (put_user (a, ruid) || put_user (b, euid) || put_user (c, suid))
		return -EFAULT;
	return ret;
}

extern asmlinkage long sys_setregid(gid_t rgid, gid_t egid);

asmlinkage long
sys32_setregid(__kernel_gid_t32 rgid, __kernel_gid_t32 egid)
{
	gid_t srgid, segid;

	srgid = (rgid == (__kernel_gid_t32)-1) ? ((gid_t)-1) : ((gid_t)rgid);
	segid = (egid == (__kernel_gid_t32)-1) ? ((gid_t)-1) : ((gid_t)egid);
	return sys_setregid(srgid, segid);
}

extern asmlinkage long sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid);

asmlinkage long
sys32_setresgid(__kernel_gid_t32 rgid, __kernel_gid_t32 egid,
		__kernel_gid_t32 sgid)
{
	gid_t srgid, segid, ssgid;

	srgid = (rgid == (__kernel_gid_t32)-1) ? ((gid_t)-1) : ((gid_t)rgid);
	segid = (egid == (__kernel_gid_t32)-1) ? ((gid_t)-1) : ((gid_t)egid);
	ssgid = (sgid == (__kernel_gid_t32)-1) ? ((gid_t)-1) : ((gid_t)sgid);
	return sys_setresgid(srgid, segid, ssgid);
}

extern asmlinkage long sys_getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid);

asmlinkage long
sys32_getresgid(__kernel_gid_t32 *rgid, __kernel_gid_t32 *egid,
		__kernel_gid_t32 *sgid) 
{
	gid_t a, b, c;
	int ret;
	mm_segment_t old_fs = get_fs();
		
	set_fs (KERNEL_DS);
	ret = sys_getresgid(&a, &b, &c);
	set_fs (old_fs);
	if (!ret) {
		ret = put_user (a, rgid);
		ret |= put_user (b, egid);
		ret |= put_user (c, sgid);
	}
	return ret;
}

extern asmlinkage long sys_getgroups(int gidsetsize, gid_t *grouplist);

asmlinkage long
sys32_getgroups(int gidsetsize, __kernel_gid_t32 *grouplist)
{
	gid_t gl[NGROUPS];
	int ret, i;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_getgroups(gidsetsize, gl);
	set_fs (old_fs);
	if (gidsetsize && ret > 0 && ret <= NGROUPS)
		for (i = 0; i < ret; i++, grouplist++)
			if (put_user (gl[i], grouplist))
				return -EFAULT;
	return ret;
}

extern asmlinkage long sys_setgroups(int gidsetsize, gid_t *grouplist);

asmlinkage long
sys32_setgroups(int gidsetsize, __kernel_gid_t32 *grouplist)
{
	gid_t gl[NGROUPS];
	int ret, i;
	mm_segment_t old_fs = get_fs ();
	
	if ((unsigned) gidsetsize > NGROUPS)
		return -EINVAL;
	for (i = 0; i < gidsetsize; i++, grouplist++)
		if (get_user (gl[i], grouplist))
			return -EFAULT;
        set_fs (KERNEL_DS);
	ret = sys_setgroups(gidsetsize, gl);
	set_fs (old_fs);
	return ret;
}


extern void check_pending(int signum);

asmlinkage long sys_utimes(char *, struct timeval *);

asmlinkage long
sys32_utimes(char *filename, struct timeval32 *tvs)
{
	char *kfilename;
	struct timeval ktvs[2];
	mm_segment_t old_fs;
	int ret;

	kfilename = getname(filename);
	ret = PTR_ERR(kfilename);
	if (!IS_ERR(kfilename)) {
		if (tvs) {
			if (get_tv32(&ktvs[0], tvs) ||
			    get_tv32(&ktvs[1], 1+tvs))
				return -EFAULT;
		}

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = sys_utimes(kfilename, &ktvs[0]);
		set_fs(old_fs);

		putname(kfilename);
	}
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

extern asmlinkage long sys_newuname(struct new_utsname * name);

asmlinkage long
sys32_newuname(struct new_utsname * name)
{
	int ret = sys_newuname(name);
	
	if (current->personality == PER_LINUX32 && !ret) {
		ret = copy_to_user(name->machine, "i386\0\0", 8);
	}
	return ret;
}

extern asmlinkage ssize_t sys_pread64(unsigned int fd, char * buf,
				    size_t count, loff_t pos);

extern asmlinkage ssize_t sys_pwrite64(unsigned int fd, const char * buf,
				     size_t count, loff_t pos);

typedef __kernel_ssize_t32 ssize_t32;

/* warning. next two assume LE */
asmlinkage ssize_t32
sys32_pread(unsigned int fd, char *ubuf, __kernel_size_t32 count,
	    u32 poslo, u32 poshi)
{
	return sys_pread64(fd, ubuf, count,
			 ((loff_t)AA(poshi) << 32) | AA(poslo));
}

asmlinkage ssize_t32
sys32_pwrite(unsigned int fd, char *ubuf, __kernel_size_t32 count,
	     u32 poslo, u32 poshi)
{
	return sys_pwrite64(fd, ubuf, count,
			  ((loff_t)AA(poshi) << 32) | AA(poslo));
}


extern asmlinkage long sys_personality(unsigned long);

asmlinkage long
sys32_personality(unsigned long personality)
{
	int ret;
	if (current->personality == PER_LINUX32 && personality == PER_LINUX)
		personality = PER_LINUX32;
	ret = sys_personality(personality);
	if (ret == PER_LINUX32)
		ret = PER_LINUX;
	return ret;
}

extern asmlinkage ssize_t sys_sendfile(int out_fd, int in_fd, off_t *offset,
				       size_t count); 

asmlinkage long
sys32_sendfile(int out_fd, int in_fd, __kernel_off_t32 *offset, s32 count)
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

/* Handle adjtimex compatability. */

struct timex32 {
	u32 modes;
	s32 offset, freq, maxerror, esterror;
	s32 status, constant, precision, tolerance;
	struct timeval32 time;
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


/* common code for old and new mmaps */
static inline long do_mmap2(
	unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	int error = -EBADF;
	struct file * file = NULL;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
out:
	return error;
}

asmlinkage long sys32_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	return do_mmap2(addr, len, prot, flags, fd, pgoff);
}


asmlinkage int sys32_olduname(struct oldold_utsname * name)
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
		 char *arch = current->personality == PER_LINUX32
			 ? "i386" : "x86_64"; 
		 
		 __copy_to_user(&name->machine,arch,strlen(arch)+1);
	 }
	
	 up_read(&uts_sem);
	 
	 error = error ? -EFAULT : 0;
	 
	 return error;
}

int sys32_uname(struct old_utsname * name)
{
	int err;
	if (!name)
		return -EFAULT;
	down_read(&uts_sem);
	err=copy_to_user(name, &system_utsname, sizeof (*name));
	up_read(&uts_sem);
	if (current->personality == PER_LINUX32) 
		err |= copy_to_user(&name->machine, "i386", 5);
	return err?-EFAULT:0;
}

extern int sys_ustat(dev_t, struct ustat *);

int sys32_ustat(dev_t dev, struct ustat32 *u32p)
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

int sys32_execve(char *name, u32 argv, u32 envp, struct pt_regs regs)
{ 
	mm_segment_t oldseg; 
	char **buf; 
	int na,ne;
	int ret;
	unsigned sz; 

	na = nargs(argv, NULL); 
	if (na < 0) 
		return -EFAULT; 
	ne = nargs(envp, NULL); 
	if (ne < 0) 
		return -EFAULT; 

	sz = (na+ne)*sizeof(void *); 
	if (sz > PAGE_SIZE) 
		buf = vmalloc(sz); 
	else
		buf = kmalloc(sz, GFP_KERNEL); 
	if (!buf)
		return -ENOMEM; 
	
	ret = nargs(argv, buf);
	if (ret < 0)
		goto free;

	ret = nargs(envp, buf + na); 
	if (ret < 0)
		goto free; 

	name = getname(name); 
	ret = PTR_ERR(name); 
	if (IS_ERR(name))
		goto free; 

	oldseg = get_fs(); 
	set_fs(KERNEL_DS);
	ret = do_execve(name, buf, buf+na, &regs);  
	set_fs(oldseg); 

	if (ret == 0)
		current->ptrace &= ~PT_DTRACE;

	putname(name);
 
free:
	if (sz > PAGE_SIZE)
		vfree(buf); 
	else
	kfree(buf);
	return ret; 
} 

asmlinkage int sys32_fork(struct pt_regs regs)
{
	struct task_struct *p;
	p = do_fork(SIGCHLD, regs.rsp, &regs, 0);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
}

asmlinkage int sys32_clone(unsigned int clone_flags, unsigned int newsp, struct pt_regs regs)
{
	struct task_struct *p;
	if (!newsp)
		newsp = regs.rsp;
	p = do_fork(clone_flags & ~CLONE_IDLETASK, newsp, &regs, 0);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
}

/*
 * This is trivial, and on the face of it looks like it
 * could equally well be done in user mode.
 *
 * Not so, for quite unobvious reasons - register pressure.
 * In user mode vfork() cannot have a stack frame, and if
 * done by calling the "clone()" system call directly, you
 * do not have enough call-clobbered registers to hold all
 * the information you need.
 */
asmlinkage int sys32_vfork(struct pt_regs regs)
{
	struct task_struct *p;
	p = do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs.rsp, &regs, 0);
	return IS_ERR(p) ? PTR_ERR(p) : p->pid;
}

/*
 * Some system calls that need sign extended arguments. This could be done by a generic wrapper.
 */ 

extern off_t sys_lseek (unsigned int fd, off_t offset, unsigned int origin);

int sys32_lseek (unsigned int fd, int offset, unsigned int whence)
{
	return sys_lseek(fd, offset, whence);
}

extern int sys_kill(pid_t pid, int sig); 

int sys32_kill(int pid, int sig)
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
	__kernel_dev_t32	ex32_dev;
	__kernel_ino_t32	ex32_ino;
	s32			ex32_flags;
	__kernel_uid_t32	ex32_anon_uid;
	__kernel_gid_t32	ex32_anon_gid;
};

struct nfsctl_uidmap32 {
	u32			ug32_ident;   /* char * */
	__kernel_uid_t32	ug32_uidbase;
	s32			ug32_uidlen;
	u32			ug32_udimap;  /* uid_t * */
	__kernel_uid_t32	ug32_gidbase;
	s32			ug32_gidlen;
	u32			ug32_gdimap;  /* gid_t * */
};

struct nfsctl_fhparm32 {
	struct sockaddr		gf32_addr;
	__kernel_dev_t32	gf32_dev;
	__kernel_ino_t32	gf32_ino;
	s32			gf32_version;
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
		struct nfsctl_uidmap32	u32_umap;
		struct nfsctl_fhparm32	u32_getfh;
		struct nfsctl_fdparm32	u32_getfd;
		struct nfsctl_fsparm32	u32_getfs;
	} u;
#define ca32_svc	u.u32_svc
#define ca32_client	u.u32_client
#define ca32_export	u.u32_export
#define ca32_umap	u.u32_umap
#define ca32_getfh	u.u32_getfh
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
	karg->ca_export.ex_anon_uid = high2lowuid(karg->ca_export.ex_anon_uid);
	karg->ca_export.ex_anon_gid = high2lowgid(karg->ca_export.ex_anon_gid);
	return err;
}

static int nfs_uud32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	u32 uaddr;
	int i;
	int err;

	memset(karg, 0, sizeof(*karg));
	if(get_user(karg->ca_version, &arg32->ca32_version))
		return -EFAULT;
	karg->ca_umap.ug_ident = (char *)get_zeroed_page(GFP_USER);
	if(!karg->ca_umap.ug_ident)
		return -ENOMEM;
	err = get_user(uaddr, &arg32->ca32_umap.ug32_ident);
	if(strncpy_from_user(karg->ca_umap.ug_ident,
			     (char *)A(uaddr), PAGE_SIZE) <= 0)
		return -EFAULT;
	err |= __get_user(karg->ca_umap.ug_uidbase,
		      &arg32->ca32_umap.ug32_uidbase);
	err |= __get_user(karg->ca_umap.ug_uidlen,
		      &arg32->ca32_umap.ug32_uidlen);
	err |= __get_user(uaddr, &arg32->ca32_umap.ug32_udimap);
	if (err)
		return -EFAULT;
	karg->ca_umap.ug_udimap = kmalloc((sizeof(uid_t) * karg->ca_umap.ug_uidlen),
					  GFP_USER);
	if(!karg->ca_umap.ug_udimap)
		return -ENOMEM;
	for(i = 0; i < karg->ca_umap.ug_uidlen; i++)
		err |= __get_user(karg->ca_umap.ug_udimap[i],
			      &(((__kernel_uid_t32 *)A(uaddr))[i]));
	err |= __get_user(karg->ca_umap.ug_gidbase,
		      &arg32->ca32_umap.ug32_gidbase);
	err |= __get_user(karg->ca_umap.ug_uidlen,
		      &arg32->ca32_umap.ug32_gidlen);
	err |= __get_user(uaddr, &arg32->ca32_umap.ug32_gdimap);
	if (err)
		return -EFAULT;
	karg->ca_umap.ug_gdimap = kmalloc((sizeof(gid_t) * karg->ca_umap.ug_uidlen),
					  GFP_USER);
	if(!karg->ca_umap.ug_gdimap)
		return -ENOMEM;
	for(i = 0; i < karg->ca_umap.ug_gidlen; i++)
		err |= __get_user(karg->ca_umap.ug_gdimap[i],
			      &(((__kernel_gid_t32 *)A(uaddr))[i]));

	return err;
}

static int nfs_getfh32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_getfh.gf_addr,
			  &arg32->ca32_getfh.gf32_addr,
			  (sizeof(struct sockaddr)));
	err |= __get_user(karg->ca_getfh.gf_dev,
		      &arg32->ca32_getfh.gf32_dev);
	err |= __get_user(karg->ca_getfh.gf_ino,
		      &arg32->ca32_getfh.gf32_ino);
	err |= __get_user(karg->ca_getfh.gf_version,
		      &arg32->ca32_getfh.gf32_version);
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
	return copy_to_user(res32, kres, sizeof(*res32));
}

int asmlinkage sys32_nfsservctl(int cmd, struct nfsctl_arg32 *arg32, union nfsctl_res32 *res32)
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
	/* This one is unimplemented, be we're ready for it. */
	case NFSCTL_UGIDUPDATE:
		err = nfs_uud32_trans(karg, arg32);
		break;
	case NFSCTL_GETFH:
		err = nfs_getfh32_trans(karg, arg32);
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

	if((cmd == NFSCTL_GETFH) ||
	   (cmd == NFSCTL_GETFD) ||
	   (cmd == NFSCTL_GETFS))
		err = nfs_getfh32_res_trans(kres, res32);

done:
	if(karg) {
		if(cmd == NFSCTL_UGIDUPDATE) {
			if(karg->ca_umap.ug_ident)
				kfree(karg->ca_umap.ug_ident);
			if(karg->ca_umap.ug_udimap)
				kfree(karg->ca_umap.ug_udimap);
			if(karg->ca_umap.ug_gdimap)
				kfree(karg->ca_umap.ug_gdimap);
		}
		kfree(karg);
	}
	if(kres)
		kfree(kres);
	return err;
}
#else /* !NFSD */
extern asmlinkage long sys_ni_syscall(void);
int asmlinkage sys32_nfsservctl(int cmd, void *notused, void *notused2)
{
	return sys_ni_syscall();
}
#endif

int sys32_module_warning(void)
{ 
	static long warn_time = -(60*HZ); 
	if (time_before(warn_time + 60*HZ,jiffies) && strcmp(current->comm,"klogd")) { 
		printk(KERN_INFO "%s: 32bit modutils not supported on 64bit kernel\n",
		       current->comm);
		warn_time = jiffies;
	} 
	return -ENOSYS ;
} 

int sys_sched_getaffinity(pid_t pid, unsigned int len, unsigned long *new_mask_ptr); 
int sys_sched_setaffinity(pid_t pid, unsigned int len, unsigned long *new_mask_ptr); 

/* only works on LE */
int sys32_sched_setaffinity(pid_t pid, unsigned int len,
			    unsigned int *new_mask_ptr)
{
	mm_segment_t oldfs = get_fs(); 
	unsigned long mask; 
	int err;
	if (get_user(mask, new_mask_ptr)) 
		return -EFAULT;	
	set_fs(KERNEL_DS); 
	err = sys_sched_setaffinity(pid,sizeof(mask),&mask); 	
	set_fs(oldfs); 
	return err;
}

/* only works on LE */ 
int sys32_sched_getaffinity(pid_t pid, unsigned int len,
			    unsigned int *new_mask_ptr)
{
	mm_segment_t oldfs = get_fs(); 
	unsigned long mask; 
	int err;
	mask = 0; 
	set_fs(KERNEL_DS); 
	err = sys_sched_getaffinity(pid,sizeof(mask),&mask); 	
	set_fs(oldfs); 
	if (!err) 
		err = put_user((u32)mask, new_mask_ptr); 
	return err;
}

struct exec_domain ia32_exec_domain = { 
	name: "linux/x86",
	pers_low: PER_LINUX32,
	pers_high: PER_LINUX32,
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

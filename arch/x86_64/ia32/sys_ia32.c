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
#include <linux/syscalls.h>
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
#include <linux/vmalloc.h>
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
#define NAME_OFFSET(de) ((int) ((de)->d_name - (char __user *) (de)))

int cp_compat_stat(struct kstat *kbuf, struct compat_stat __user *ubuf)
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

asmlinkage long
sys32_truncate64(char __user * filename, unsigned long offset_low, unsigned long offset_high)
{
       return sys_truncate(filename, ((loff_t) offset_high << 32) | offset_low);
}

asmlinkage long
sys32_ftruncate64(unsigned int fd, unsigned long offset_low, unsigned long offset_high)
{
       return sys_ftruncate(fd, ((loff_t) offset_high << 32) | offset_low);
}

/* Another set for IA32/LFS -- x86_64 struct stat is different due to 
   support for 64bit inode numbers. */

static int
cp_stat64(struct stat64 __user *ubuf, struct kstat *stat)
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
sys32_stat64(char __user * filename, struct stat64 __user *statbuf)
{
	struct kstat stat;
	int ret = vfs_stat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

asmlinkage long
sys32_lstat64(char __user * filename, struct stat64 __user *statbuf)
{
	struct kstat stat;
	int ret = vfs_lstat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

asmlinkage long
sys32_fstat64(unsigned int fd, struct stat64 __user *statbuf)
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
sys32_mmap(struct mmap_arg_struct __user *arg)
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

asmlinkage long 
sys32_mprotect(unsigned long start, size_t len, unsigned long prot)
{
	if (prot & PROT_READ) 
		prot |= vm_force_exec32;
	return sys_mprotect(start,len,prot); 
}

asmlinkage long
sys32_pipe(int __user *fd)
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
sys32_rt_sigaction(int sig, struct sigaction32 __user *act,
		   struct sigaction32 __user *oact,  unsigned int sigsetsize)
{
	struct k_sigaction new_ka, old_ka;
	int ret;
	compat_sigset_t set32;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(compat_sigset_t))
		return -EINVAL;

	if (act) {
		compat_uptr_t handler, restorer;

		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags) ||
		    __get_user(restorer, &act->sa_restorer)||
		    __copy_from_user(&set32, &act->sa_mask, sizeof(compat_sigset_t)))
			return -EFAULT;
		new_ka.sa.sa_handler = compat_ptr(handler);
		new_ka.sa.sa_restorer = compat_ptr(restorer);
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
sys32_sigaction (int sig, struct old_sigaction32 __user *act, struct old_sigaction32 __user *oact)
{
        struct k_sigaction new_ka, old_ka;
        int ret;

        if (act) {
		compat_old_sigset_t mask;
		compat_uptr_t handler, restorer;

		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_flags, &act->sa_flags) ||
		    __get_user(restorer, &act->sa_restorer) ||
		    __get_user(mask, &act->sa_mask))
			return -EFAULT;

		new_ka.sa.sa_handler = compat_ptr(handler);
		new_ka.sa.sa_restorer = compat_ptr(restorer);

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

asmlinkage long
sys32_rt_sigprocmask(int how, compat_sigset_t __user *set,
			compat_sigset_t __user *oset, unsigned int sigsetsize)
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
get_tv32(struct timeval *o, struct compat_timeval __user *i)
{
	int err = -EFAULT; 
	if (access_ok(VERIFY_READ, i, sizeof(*i))) { 
		err = __get_user(o->tv_sec, &i->tv_sec);
		err |= __get_user(o->tv_usec, &i->tv_usec);
	}
	return err; 
}

static inline long
put_tv32(struct compat_timeval __user *o, struct timeval *i)
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
sys32_gettimeofday(struct compat_timeval __user *tv, struct timezone __user *tz)
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
sys32_settimeofday(struct compat_timeval __user *tv, struct timezone __user *tz)
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
	struct linux32_dirent __user * current_dir;
	struct linux32_dirent __user * previous;
	int count;
	int error;
};

struct readdir32_callback {
	struct old_linux32_dirent __user * dirent;
	int count;
};

static int
filldir32 (void *__buf, const char *name, int namlen, loff_t offset, ino_t ino,
	   unsigned int d_type)
{
	struct linux32_dirent __user * dirent;
	struct getdents32_callback * buf = (struct getdents32_callback *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 2, 4);

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
	put_user(d_type, (char __user *)dirent + reclen - 1); 
	dirent = ((void __user *)dirent) + reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
}

asmlinkage long
sys32_getdents (unsigned int fd, void __user * dirent, unsigned int count)
{
	struct file * file;
	struct linux32_dirent __user * lastdirent;
	struct getdents32_callback buf;
	int error;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = (struct linux32_dirent __user *) dirent;
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
	struct old_linux32_dirent __user * dirent;

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
sys32_oldreaddir (unsigned int fd, void __user * dirent, unsigned int count)
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

struct sel_arg_struct {
	unsigned int n;
	unsigned int inp;
	unsigned int outp;
	unsigned int exp;
	unsigned int tvp;
};

asmlinkage long
sys32_old_select(struct sel_arg_struct __user *arg)
{
	struct sel_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	return compat_sys_select(a.n, compat_ptr(a.inp), compat_ptr(a.outp),
				 compat_ptr(a.exp), compat_ptr(a.tvp));
}

/*
 * sys_time() can be implemented in user-level using
 * sys_gettimeofday().  x86-64 did this but i386 Linux did not
 * so we have to implement this system call here.
 */
asmlinkage long sys32_time(int __user * tloc)
{
	int i;
	struct timeval tv;

	do_gettimeofday(&tv);
	i = tv.tv_sec;

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
	struct task_struct *me = current;
	static char lastcomm[8];
	if (strcmp(lastcomm, me->comm)) {
	printk(KERN_INFO "IA32 syscall %d from %s not implemented\n", call,
	       current->comm);
		strcpy(lastcomm, me->comm); 
	} 
	return -ENOSYS;	       
} 

/* 32-bit timeval and related flotsam.  */

asmlinkage long
sys32_sysfs(int option, u32 arg1, u32 arg2)
{
	return sys_sysfs(option, arg1, arg2);
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

asmlinkage long
sys32_sysinfo(struct sysinfo32 __user *info)
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
                
asmlinkage long
sys32_sched_rr_get_interval(compat_pid_t pid, struct compat_timespec __user *interval)
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

asmlinkage long
sys32_rt_sigpending(compat_sigset_t __user *set, compat_size_t sigsetsize)
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


asmlinkage long
sys32_rt_sigtimedwait(compat_sigset_t __user *uthese, siginfo_t32 __user *uinfo,
		      struct compat_timespec __user *uts, compat_size_t sigsetsize)
{
	sigset_t s;
	compat_sigset_t s32;
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs();
	siginfo_t info;
		
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
	if (uinfo) {
		/* stop data leak to user space in case of structure fill mismatch
		 * between sys_rt_sigtimedwait & ia32_copy_siginfo_to_user.
		 */
		memset(&info, 0, sizeof(info));
	}
	set_fs (KERNEL_DS);
	ret = sys_rt_sigtimedwait(&s, uinfo ? &info : NULL, uts ? &t : NULL,
			sigsetsize);
	set_fs (old_fs);
	if (ret >= 0 && uinfo) {
		if (ia32_copy_siginfo_to_user(uinfo, &info))
			return -EFAULT;
	}
	return ret;
}

asmlinkage long
sys32_rt_sigqueueinfo(int pid, int sig, siginfo_t32 __user *uinfo)
{
	siginfo_t info;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	if (ia32_copy_siginfo_from_user(&info, uinfo))
		return -EFAULT;
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
sys32_sysctl(struct sysctl_ia32 __user *args32)
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

	if ((oldvalp && get_user(oldlen, (int __user *)compat_ptr(a32.oldlenp)))
	    || !access_ok(VERIFY_WRITE, namep, 0)
	    || !access_ok(VERIFY_WRITE, oldvalp, 0)
	    || !access_ok(VERIFY_WRITE, newvalp, 0))
		return -EFAULT;

	set_fs(KERNEL_DS);
	lock_kernel();
	ret = do_sysctl(namep, a32.nlen, oldvalp, &oldlen, newvalp, (size_t) a32.newlen);
	unlock_kernel();
	set_fs(old_fs);

	if (oldvalp && put_user (oldlen, (int __user *)compat_ptr(a32.oldlenp)))
		return -EFAULT;

	return ret;
#endif
}

/* warning: next two assume little endian */ 
asmlinkage long
sys32_pread(unsigned int fd, char __user *ubuf, u32 count, u32 poslo, u32 poshi)
{
	return sys_pread64(fd, ubuf, count,
			 ((loff_t)AA(poshi) << 32) | AA(poslo));
}

asmlinkage long
sys32_pwrite(unsigned int fd, char __user *ubuf, u32 count, u32 poslo, u32 poshi)
{
	return sys_pwrite64(fd, ubuf, count,
			  ((loff_t)AA(poshi) << 32) | AA(poslo));
}


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

asmlinkage long
sys32_sendfile(int out_fd, int in_fd, compat_off_t __user *offset, s32 count)
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
sys32_adjtimex(struct timex32 __user *utp)
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

asmlinkage long sys32_olduname(struct oldold_utsname __user * name)
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

long sys32_uname(struct old_utsname __user * name)
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

long sys32_ustat(unsigned dev, struct ustat32 __user *u32p)
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

asmlinkage long sys32_execve(char __user *name, compat_uptr_t __user *argv,
			     compat_uptr_t __user *envp, struct pt_regs regs)
{
	long error;
	char * filename;

	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		return error;
	error = compat_do_execve(filename, argv, envp, &regs);
	if (error == 0)
		current->ptrace &= ~PT_DTRACE;
	putname(filename);
	return error;
}

asmlinkage long sys32_clone(unsigned int clone_flags, unsigned int newsp, struct pt_regs regs)
{
	void __user *parent_tid = (void __user *)regs.rdx;
	void __user *child_tid = (void __user *)regs.rdi; 
	if (!newsp)
		newsp = regs.rsp;
        return do_fork(clone_flags & ~CLONE_IDLETASK, newsp, &regs, 0, 
		    parent_tid, child_tid);
}

/*
 * Some system calls that need sign extended arguments. This could be done by a generic wrapper.
 */ 

long sys32_lseek (unsigned int fd, int offset, unsigned int whence)
{
	return sys_lseek(fd, offset, whence);
}

long sys32_kill(int pid, int sig)
{
	return sys_kill(pid, sig);
}
 

long sys32_io_setup(unsigned nr_reqs, u32 __user *ctx32p)
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
		   compat_uptr_t __user *iocbpp)
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
		struct iocb __user *user_iocb;
		struct iocb tmp;

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


asmlinkage long sys32_io_getevents(aio_context_t ctx_id,
				 unsigned long min_nr,
				 unsigned long nr,
				 struct io_event __user *events,
				 struct compat_timespec __user *timeout)
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

asmlinkage long sys32_open(const char __user * filename, int flags, int mode)
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
sys32_timer_create(u32 clock, struct sigevent32 __user *se32, timer_t __user *timer_id)
{
	struct sigevent __user *p = NULL;
	if (se32) { 
		struct sigevent se;
		p = compat_alloc_user_space(sizeof(struct sigevent));
		memset(&se, 0, sizeof(struct sigevent)); 
		if (get_user(se.sigev_value.sival_int,  &se32->sigev_value) ||
		    __get_user(se.sigev_signo, &se32->sigev_signo) ||
		    __get_user(se.sigev_notify, &se32->sigev_notify) ||
		    __copy_from_user(&se._sigev_un._pad, &se32->payload, 
				     sizeof(se32->payload)) ||
		    copy_to_user(p, &se, sizeof(se)))
			return -EFAULT;
	} 
	return sys_timer_create(clock, p, timer_id);
} 

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
	struct task_struct *me = current;
	static char lastcomm[8];
	if (strcmp(lastcomm, me->comm)) {
		printk(KERN_INFO "%s: vm86 mode not supported on 64 bit kernel\n",
		       me->comm);
		strcpy(lastcomm, me->comm); 
	} 
	return -ENOSYS;
} 

long sys32_quotactl(void)
{ 
	struct task_struct *me = current;
	static char lastcomm[8];
	if (strcmp(lastcomm, me->comm)) {
		printk(KERN_INFO "%s: 32bit quotactl not supported on 64 bit kernel\n",
		       me->comm);
		strcpy(lastcomm, me->comm); 
	} 
	return -ENOSYS;
} 

cond_syscall(sys32_ipc)

static int __init ia32_init (void)
{
	printk("IA32 emulation $Id: sys_ia32.c,v 1.32 2002/03/24 13:02:28 ak Exp $\n");  
	return 0;
}

__initcall(ia32_init);

extern unsigned long ia32_sys_call_table[];
EXPORT_SYMBOL(ia32_sys_call_table);

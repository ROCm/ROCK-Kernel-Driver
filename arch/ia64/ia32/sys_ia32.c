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
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment.
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
#include <linux/mm.h>
#include <linux/shm.h>
#include <linux/malloc.h>
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

#include <asm/types.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <asm/ipc.h>

#include <net/scm.h>
#include <net/sock.h>
#include <asm/ia32.h>

#define A(__x)		((unsigned long)(__x))
#define AA(__x)		((unsigned long)(__x))
#define ROUND_UP(x,a)	((__typeof__(x))(((unsigned long)(x) + ((a) - 1)) & ~((a) - 1)))
#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))

extern asmlinkage long sys_execve (char *, char **, char **, struct pt_regs *);
extern asmlinkage long sys_munmap (unsigned long, size_t len);
extern asmlinkage long sys_mprotect (unsigned long, size_t, unsigned long);

static int
nargs(unsigned int arg, char **ap)
{
	int n, err, addr;

	n = 0;
	do {
		err = get_user(addr, (int *)A(arg));
		if (err)
			return err;
		if (ap) {		/* no access_ok needed, we allocated */
			err = __put_user((char *)A(addr), ap++);
			if (err)
				return err;
		}
		arg += sizeof(unsigned int);
		n++;
	} while (addr);
	return(n - 1);
}

asmlinkage long
sys32_execve(
char *filename,
unsigned int argv,
unsigned int envp,
int dummy3,
int dummy4,
int dummy5,
int dummy6,
int dummy7,
int stack)
{
	struct pt_regs *regs = (struct pt_regs *)&stack;
	char **av, **ae;
	int na, ne, len;
	long r;

	na = nargs(argv, NULL);
	if (na < 0)
		return(na);
	ne = nargs(envp, NULL);
	if (ne < 0)
		return(ne);
	len = (na + ne + 2) * sizeof(*av);
	/*
	 *  kmalloc won't work because the `sys_exec' code will attempt
	 *  to do a `get_user' on the arg list and `get_user' will fail
	 *  on a kernel address (simplifies `get_user').  Instead we
	 *  do an mmap to get a user address.  Note that since a successful
	 *  `execve' frees all current memory we only have to do an
	 *  `munmap' if the `execve' failes.
	 */
	down(&current->mm->mmap_sem);

	av = (char **) do_mmap_pgoff(0, 0UL, len, PROT_READ | PROT_WRITE,
				     MAP_PRIVATE | MAP_ANONYMOUS, 0);

	up(&current->mm->mmap_sem);

	if (IS_ERR(av))
		return (long)av;
	ae = av + na + 1;
	r = __put_user(0, (av + na));
	if (r)
		goto out;
	r = __put_user(0, (ae + ne));
	if (r)
		goto out;
	r = nargs(argv, av);
	if (r < 0)
		goto out;
	r = nargs(envp, ae);
	if (r < 0)
		goto out;
	r = sys_execve(filename, av, ae, regs);
	if (r < 0)
out:
		sys_munmap((unsigned long) av, len);
	return(r);
}

static inline int
putstat(struct stat32 *ubuf, struct stat *kbuf)
{
	int err;
	
	err = put_user (kbuf->st_dev, &ubuf->st_dev);
	err |= __put_user (kbuf->st_ino, &ubuf->st_ino);
	err |= __put_user (kbuf->st_mode, &ubuf->st_mode);
	err |= __put_user (kbuf->st_nlink, &ubuf->st_nlink);
	err |= __put_user (kbuf->st_uid, &ubuf->st_uid);
	err |= __put_user (kbuf->st_gid, &ubuf->st_gid);
	err |= __put_user (kbuf->st_rdev, &ubuf->st_rdev);
	err |= __put_user (kbuf->st_size, &ubuf->st_size);
	err |= __put_user (kbuf->st_atime, &ubuf->st_atime);
	err |= __put_user (kbuf->st_mtime, &ubuf->st_mtime);
	err |= __put_user (kbuf->st_ctime, &ubuf->st_ctime);
	err |= __put_user (kbuf->st_blksize, &ubuf->st_blksize);
	err |= __put_user (kbuf->st_blocks, &ubuf->st_blocks);
	return err;
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

#define ALIGN4K(a)	(((a) + 0xfff) & ~0xfff)
#define OFFSET4K(a)	((a) & 0xfff)

unsigned long
do_mmap_fake(struct file *file, unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, loff_t off)
{
	struct inode *inode;
	void *front, *back;
	unsigned long baddr;
	int r;
	char c;

	if (OFFSET4K(addr) || OFFSET4K(off))
		return -EINVAL;
	prot |= PROT_WRITE;
	front = NULL;
	back = NULL;
	if ((baddr = (addr & PAGE_MASK)) != addr && get_user(c, (char *)baddr) == 0) {
		front = kmalloc(addr - baddr, GFP_KERNEL);
		__copy_user(front, (void *)baddr, addr - baddr);
	}
	if (addr && ((addr + len) & ~PAGE_MASK) && get_user(c, (char *)(addr + len)) == 0) {
		back = kmalloc(PAGE_SIZE - ((addr + len) & ~PAGE_MASK), GFP_KERNEL);
		__copy_user(back, (char *)addr + len, PAGE_SIZE - ((addr + len) & ~PAGE_MASK));
	}
	down(&current->mm->mmap_sem);
	r = do_mmap(0, baddr, len + (addr - baddr), prot, flags | MAP_ANONYMOUS, 0);
	up(&current->mm->mmap_sem);
	if (r < 0)
		return(r);
	if (addr == 0)
		addr = r;
	if (back) {
		__copy_user((char *)addr + len, back, PAGE_SIZE - ((addr + len) & ~PAGE_MASK));
		kfree(back);
	}
	if (front) {
		__copy_user((void *)baddr, front, addr - baddr);
		kfree(front);
	}
	if (flags & MAP_ANONYMOUS) {
		clear_user((char *)addr, len);
		return(addr);
	}
	if (!file)
		return -EINVAL;
	inode = file->f_dentry->d_inode;
	if (!inode->i_fop)
		return -EINVAL;
	if (!file->f_op->read)
		return -EINVAL;
	r = file->f_op->read(file, (char *)addr, len, &off);
	return (r < 0) ? -EINVAL : addr;
}

long
ia32_do_mmap (struct file *file, unsigned int addr, unsigned int len, unsigned int prot,
	      unsigned int flags, unsigned int fd, unsigned int offset)
{
	long error = -EFAULT;
	unsigned int poff;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	prot |= PROT_EXEC;

 	if ((flags & MAP_FIXED) && ((addr & ~PAGE_MASK) || (offset & ~PAGE_MASK)))
 		error = do_mmap_fake(file, addr, len, prot, flags, (loff_t)offset);
	else {
 		poff = offset & PAGE_MASK;
 		len += offset - poff;

 		down(&current->mm->mmap_sem);
 		error = do_mmap_pgoff(file, addr, len, prot, flags, poff >> PAGE_SHIFT);
  		up(&current->mm->mmap_sem);

 		if (!IS_ERR((void *) error))
 			error += offset - poff;
 	}
	return error;
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
	long retval;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;

	if (!(a.flags & MAP_ANONYMOUS)) {
		file = fget(a.fd);
		if (!file)
			return -EBADF;
	}
	retval = ia32_do_mmap(file, a.addr, a.len, a.prot, a.flags, a.fd, a.offset);
	if (file)
		fput(file);
	return retval;
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
sys32_mprotect(unsigned long start, size_t len, unsigned long prot)
{

	if (prot == 0)
		return(0);
	len += start & ~PAGE_MASK;
	if ((start & ~PAGE_MASK) && (prot & PROT_WRITE))
		prot |= PROT_EXEC;
	return(sys_mprotect(start & PAGE_MASK, len & PAGE_MASK, prot));
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
		ret = get_user((long)new_ka.sa.sa_handler, &act->sa_handler);
		ret |= __copy_from_user(&set32, &act->sa_mask,
					sizeof(sigset32_t));
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
		ret |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		
		if (ret)
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
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
		ret = put_user((long)old_ka.sa.sa_handler, &oact->sa_handler);
		ret |= __copy_to_user(&oact->sa_mask, &set32,
				      sizeof(sigset32_t));
		ret |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
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

static inline int
put_statfs (struct statfs32 *ubuf, struct statfs *kbuf)
{
	int err;
	
	err = put_user (kbuf->f_type, &ubuf->f_type);
	err |= __put_user (kbuf->f_bsize, &ubuf->f_bsize);
	err |= __put_user (kbuf->f_blocks, &ubuf->f_blocks);
	err |= __put_user (kbuf->f_bfree, &ubuf->f_bfree);
	err |= __put_user (kbuf->f_bavail, &ubuf->f_bavail);
	err |= __put_user (kbuf->f_files, &ubuf->f_files);
	err |= __put_user (kbuf->f_ffree, &ubuf->f_ffree);
	err |= __put_user (kbuf->f_namelen, &ubuf->f_namelen);
	err |= __put_user (kbuf->f_fsid.val[0], &ubuf->f_fsid.val[0]);
	err |= __put_user (kbuf->f_fsid.val[1], &ubuf->f_fsid.val[1]);
	return err;
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
	return (!access_ok(VERIFY_READ, i, sizeof(*i)) ||
		(__get_user(o->tv_sec, &i->tv_sec) |
		 __get_user(o->tv_usec, &i->tv_usec)));
	return ENOSYS;
}

static inline long
put_tv32(struct timeval32 *o, struct timeval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->tv_sec, &o->tv_sec) |
		 __put_user(i->tv_usec, &o->tv_usec)));
}

static inline long
get_it32(struct itimerval *o, struct itimerval32 *i)
{
	return (!access_ok(VERIFY_READ, i, sizeof(*i)) ||
		(__get_user(o->it_interval.tv_sec, &i->it_interval.tv_sec) |
		 __get_user(o->it_interval.tv_usec, &i->it_interval.tv_usec) |
		 __get_user(o->it_value.tv_sec, &i->it_value.tv_sec) |
		 __get_user(o->it_value.tv_usec, &i->it_value.tv_usec)));
	return ENOSYS;
}

static inline long
put_it32(struct itimerval32 *o, struct itimerval *i)
{
	return (!access_ok(VERIFY_WRITE, i, sizeof(*i)) ||
		(__put_user(i->it_interval.tv_sec, &o->it_interval.tv_sec) |
		 __put_user(i->it_interval.tv_usec, &o->it_interval.tv_usec) |
		 __put_user(i->it_value.tv_sec, &o->it_value.tv_sec) |
		 __put_user(i->it_value.tv_usec, &o->it_value.tv_usec)));
	return ENOSYS;
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
filldir32 (void *__buf, const char *name, int namlen, off_t offset, ino_t ino,
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
fillonedir32 (void * __buf, const char * name, int namlen, off_t offset, ino_t ino,
	      unsigned int d_type)
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
sys32_readdir (unsigned int fd, void * dirent, unsigned int count)
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
old_select(struct sel_arg_struct *arg)
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
	
	if (get_user (t.tv_sec, &rqtp->tv_sec) ||
	    __get_user (t.tv_nsec, &rqtp->tv_nsec))
		return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_nanosleep(&t, rmtp ? &t : NULL);
	set_fs (old_fs);
	if (rmtp && ret == -EINTR) {
		if (__put_user (t.tv_sec, &rmtp->tv_sec) ||
	    	    __put_user (t.tv_nsec, &rmtp->tv_nsec))
			return -EFAULT;
	}
	return ret;
}

struct iovec32 { unsigned int iov_base; int iov_len; };
asmlinkage ssize_t sys_readv(unsigned long,const struct iovec *,unsigned long);
asmlinkage ssize_t sys_writev(unsigned long,const struct iovec *,unsigned long);

static struct iovec *
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

#define RLIM_INFINITY32	0x7fffffff
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
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_getrlimit(resource, &r);
	set_fs (old_fs);
	if (!ret) {
		ret = put_user (RESOURCE32(r.rlim_cur), &rlim->rlim_cur);
		ret |= __put_user (RESOURCE32(r.rlim_max), &rlim->rlim_max);
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
	if (get_user (r.rlim_cur, &rlim->rlim_cur) ||
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
 *  Declare the IA32 version of the msghdr
 */
 
struct msghdr32 {
	unsigned int    msg_name;	/* Socket name			*/
	int		msg_namelen;	/* Length of name		*/
	unsigned int    msg_iov;	/* Data blocks			*/
	unsigned int	msg_iovlen;	/* Number of blocks		*/
	unsigned int    msg_control;	/* Per protocol magic (eg BSD file descriptor passing) */
	unsigned int	msg_controllen;	/* Length of cmsg list */
	unsigned	msg_flags;
};

static inline int
shape_msg(struct msghdr *mp, struct msghdr32 *mp32)
{
	int ret;
	unsigned int i;

	if (!access_ok(VERIFY_READ, mp32, sizeof(*mp32)))
		return(-EFAULT);
	ret = __get_user(i, &mp32->msg_name);
	mp->msg_name = (void *)A(i);
	ret |= __get_user(mp->msg_namelen, &mp32->msg_namelen);
	ret |= __get_user(i, &mp32->msg_iov);
	mp->msg_iov = (struct iovec *)A(i);
	ret |= __get_user(mp->msg_iovlen, &mp32->msg_iovlen);
	ret |= __get_user(i, &mp32->msg_control);
	mp->msg_control = (void *)A(i);
	ret |= __get_user(mp->msg_controllen, &mp32->msg_controllen);
	ret |= __get_user(mp->msg_flags, &mp32->msg_flags);
	return(ret ? -EFAULT : 0);
}

/*
 *	Verify & re-shape IA32 iovec. The caller must ensure that the
 *      iovec is big enough to hold the re-shaped message iovec.
 *
 *	Save time not doing verify_area. copy_*_user will make this work
 *	in any case.
 *
 *	Don't need to check the total size for overflow (cf net/core/iovec.c),
 *	32-bit sizes can't overflow a 64-bit count.
 */

static inline int
verify_iovec32(struct msghdr *m, struct iovec *iov, char *address, int mode)
{
	int size, err, ct;
	struct iovec32 *iov32;
	
	if(m->msg_namelen)
	{
		if(mode==VERIFY_READ)
		{
			err=move_addr_to_kernel(m->msg_name, m->msg_namelen, address);
			if(err<0)
				goto out;
		}
		
		m->msg_name = address;
	} else
		m->msg_name = NULL;

	err = -EFAULT;
	size = m->msg_iovlen * sizeof(struct iovec32);
	if (copy_from_user(iov, m->msg_iov, size))
		goto out;
	m->msg_iov=iov;

	err = 0;
	iov32 = (struct iovec32 *)iov;
	for (ct = m->msg_iovlen; ct-- > 0; ) {
		iov[ct].iov_len = (__kernel_size_t)iov32[ct].iov_len;
		iov[ct].iov_base = (void *) A(iov32[ct].iov_base);
		err += iov[ct].iov_len;
	}
out:
	return err;
}

extern __inline__ void
sockfd_put(struct socket *sock)
{
	fput(sock->file);
}

/* XXX This really belongs in some header file... -DaveM */
#define MAX_SOCK_ADDR	128		/* 108 for Unix domain - 
					   16 for IP, 16 for IPX,
					   24 for IPv6,
					   about 80 for AX.25 */

extern struct socket *sockfd_lookup(int fd, int *err);

/*
 *	BSD sendmsg interface
 */

int sys32_sendmsg(int fd, struct msghdr32 *msg, unsigned flags)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	struct iovec iovstack[UIO_FASTIOV], *iov = iovstack;
	unsigned char ctl[sizeof(struct cmsghdr) + 20];	/* 20 is size of ipv6_pktinfo */
	unsigned char *ctl_buf = ctl;
	struct msghdr msg_sys;
	int err, ctl_len, iov_size, total_len;
	
	err = -EFAULT;
	if (shape_msg(&msg_sys, msg))
		goto out; 

	sock = sockfd_lookup(fd, &err);
	if (!sock) 
		goto out;

	/* do not move before msg_sys is valid */
	err = -EINVAL;
	if (msg_sys.msg_iovlen > UIO_MAXIOV)
		goto out_put;

	/* Check whether to allocate the iovec area*/
	err = -ENOMEM;
	iov_size = msg_sys.msg_iovlen * sizeof(struct iovec32);
	if (msg_sys.msg_iovlen > UIO_FASTIOV) {
		iov = sock_kmalloc(sock->sk, iov_size, GFP_KERNEL);
		if (!iov)
			goto out_put;
	}

	/* This will also move the address data into kernel space */
	err = verify_iovec32(&msg_sys, iov, address, VERIFY_READ);
	if (err < 0) 
		goto out_freeiov;
	total_len = err;

	err = -ENOBUFS;

	if (msg_sys.msg_controllen > INT_MAX)
		goto out_freeiov;
	ctl_len = msg_sys.msg_controllen; 
	if (ctl_len) 
	{
		if (ctl_len > sizeof(ctl))
		{
			err = -ENOBUFS;
			ctl_buf = sock_kmalloc(sock->sk, ctl_len, GFP_KERNEL);
			if (ctl_buf == NULL) 
				goto out_freeiov;
		}
		err = -EFAULT;
		if (copy_from_user(ctl_buf, msg_sys.msg_control, ctl_len))
			goto out_freectl;
		msg_sys.msg_control = ctl_buf;
	}
	msg_sys.msg_flags = flags;

	if (sock->file->f_flags & O_NONBLOCK)
		msg_sys.msg_flags |= MSG_DONTWAIT;
	err = sock_sendmsg(sock, &msg_sys, total_len);

out_freectl:
	if (ctl_buf != ctl)    
		sock_kfree_s(sock->sk, ctl_buf, ctl_len);
out_freeiov:
	if (iov != iovstack)
		sock_kfree_s(sock->sk, iov, iov_size);
out_put:
	sockfd_put(sock);
out:       
	return err;
}

/*
 *	BSD recvmsg interface
 */

int
sys32_recvmsg (int fd, struct msghdr32 *msg, unsigned int flags)
{
	struct socket *sock;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov=iovstack;
	struct msghdr msg_sys;
	unsigned long cmsg_ptr;
	int err, iov_size, total_len, len;

	/* kernel mode address */
	char addr[MAX_SOCK_ADDR];

	/* user mode address pointers */
	struct sockaddr *uaddr;
	int *uaddr_len;
	
	err=-EFAULT;
	if (shape_msg(&msg_sys, msg))
		goto out;

	sock = sockfd_lookup(fd, &err);
	if (!sock)
		goto out;

	err = -EINVAL;
	if (msg_sys.msg_iovlen > UIO_MAXIOV)
		goto out_put;
	
	/* Check whether to allocate the iovec area*/
	err = -ENOMEM;
	iov_size = msg_sys.msg_iovlen * sizeof(struct iovec);
	if (msg_sys.msg_iovlen > UIO_FASTIOV) {
		iov = sock_kmalloc(sock->sk, iov_size, GFP_KERNEL);
		if (!iov)
			goto out_put;
	}

	/*
	 *	Save the user-mode address (verify_iovec will change the
	 *	kernel msghdr to use the kernel address space)
	 */
	 
	uaddr = msg_sys.msg_name;
	uaddr_len = &msg->msg_namelen;
	err = verify_iovec32(&msg_sys, iov, addr, VERIFY_WRITE);
	if (err < 0)
		goto out_freeiov;
	total_len=err;

	cmsg_ptr = (unsigned long)msg_sys.msg_control;
	msg_sys.msg_flags = 0;
	
	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	err = sock_recvmsg(sock, &msg_sys, total_len, flags);
	if (err < 0)
		goto out_freeiov;
	len = err;

	if (uaddr != NULL) {
		err = move_addr_to_user(addr, msg_sys.msg_namelen, uaddr, uaddr_len);
		if (err < 0)
			goto out_freeiov;
	}
	err = __put_user(msg_sys.msg_flags, &msg->msg_flags);
	if (err)
		goto out_freeiov;
	err = __put_user((unsigned long)msg_sys.msg_control-cmsg_ptr, 
							 &msg->msg_controllen);
	if (err)
		goto out_freeiov;
	err = len;

out_freeiov:
	if (iov != iovstack)
		sock_kfree_s(sock->sk, iov, iov_size);
out_put:
	sockfd_put(sock);
out:
	return err;
}

/* Argument list sizes for sys_socketcall */
#define AL(x) ((x) * sizeof(u32))
static unsigned char nas[18]={AL(0),AL(3),AL(3),AL(3),AL(2),AL(3),
                                AL(3),AL(3),AL(4),AL(4),AL(4),AL(6),
                                AL(6),AL(2),AL(5),AL(5),AL(3),AL(3)};
#undef AL

extern asmlinkage long sys_bind(int fd, struct sockaddr *umyaddr, int addrlen);
extern asmlinkage long sys_connect(int fd, struct sockaddr *uservaddr,
				  int addrlen);
extern asmlinkage long sys_accept(int fd, struct sockaddr *upeer_sockaddr,
				 int *upeer_addrlen); 
extern asmlinkage long sys_getsockname(int fd, struct sockaddr *usockaddr,
				      int *usockaddr_len);
extern asmlinkage long sys_getpeername(int fd, struct sockaddr *usockaddr,
				      int *usockaddr_len);
extern asmlinkage long sys_send(int fd, void *buff, size_t len, unsigned flags);
extern asmlinkage long sys_sendto(int fd, u32 buff, __kernel_size_t32 len,
				   unsigned flags, u32 addr, int addr_len);
extern asmlinkage long sys_recv(int fd, void *ubuf, size_t size, unsigned flags);
extern asmlinkage long sys_recvfrom(int fd, u32 ubuf, __kernel_size_t32 size,
				     unsigned flags, u32 addr, u32 addr_len);
extern asmlinkage long sys_setsockopt(int fd, int level, int optname,
				     char *optval, int optlen);
extern asmlinkage long sys_getsockopt(int fd, int level, int optname,
				       u32 optval, u32 optlen);

extern asmlinkage long sys_socket(int family, int type, int protocol);
extern asmlinkage long sys_socketpair(int family, int type, int protocol,
				     int usockvec[2]);
extern asmlinkage long sys_shutdown(int fd, int how);
extern asmlinkage long sys_listen(int fd, int backlog);

asmlinkage long sys32_socketcall(int call, u32 *args)
{
	int ret;
	u32 a[6];
	u32 a0,a1;
				 
	if (call<SYS_SOCKET||call>SYS_RECVMSG)
		return -EINVAL;
	if (copy_from_user(a, args, nas[call]))
		return -EFAULT;
	a0=a[0];
	a1=a[1];
	
	switch(call) 
	{
		case SYS_SOCKET:
			ret = sys_socket(a0, a1, a[2]);
			break;
		case SYS_BIND:
			ret = sys_bind(a0, (struct sockaddr *)A(a1), a[2]);
			break;
		case SYS_CONNECT:
			ret = sys_connect(a0, (struct sockaddr *)A(a1), a[2]);
			break;
		case SYS_LISTEN:
			ret = sys_listen(a0, a1);
			break;
		case SYS_ACCEPT:
			ret = sys_accept(a0, (struct sockaddr *)A(a1),
					  (int *)A(a[2]));
			break;
		case SYS_GETSOCKNAME:
			ret = sys_getsockname(a0, (struct sockaddr *)A(a1),
					       (int *)A(a[2]));
			break;
		case SYS_GETPEERNAME:
			ret = sys_getpeername(a0, (struct sockaddr *)A(a1),
					       (int *)A(a[2]));
			break;
		case SYS_SOCKETPAIR:
			ret = sys_socketpair(a0, a1, a[2], (int *)A(a[3]));
			break;
		case SYS_SEND:
			ret = sys_send(a0, (void *)A(a1), a[2], a[3]);
			break;
		case SYS_SENDTO:
			ret = sys_sendto(a0, a1, a[2], a[3], a[4], a[5]);
			break;
		case SYS_RECV:
			ret = sys_recv(a0, (void *)A(a1), a[2], a[3]);
			break;
		case SYS_RECVFROM:
			ret = sys_recvfrom(a0, a1, a[2], a[3], a[4], a[5]);
			break;
		case SYS_SHUTDOWN:
			ret = sys_shutdown(a0,a1);
			break;
		case SYS_SETSOCKOPT:
			ret = sys_setsockopt(a0, a1, a[2], (char *)A(a[3]),
					      a[4]);
			break;
		case SYS_GETSOCKOPT:
			ret = sys_getsockopt(a0, a1, a[2], a[3], a[4]);
			break;
		case SYS_SENDMSG:
			ret = sys32_sendmsg(a0, (struct msghdr32 *)A(a1),
					     a[2]);
			break;
		case SYS_RECVMSG:
			ret = sys32_recvmsg(a0, (struct msghdr32 *)A(a1),
					     a[2]);
			break;
		default:
			ret = EINVAL;
			break;
	}
	return ret;
}

/*
 * sys32_ipc() is the de-multiplexer for the SysV IPC calls in 32bit emulation..
 *
 * This is really horribly ugly.
 */

struct msgbuf32 { s32 mtype; char mtext[1]; };

struct ipc_perm32
{
	key_t    	  key;
        __kernel_uid_t32  uid;
        __kernel_gid_t32  gid;
        __kernel_uid_t32  cuid;
        __kernel_gid_t32  cgid;
        __kernel_mode_t32 mode;
        unsigned short  seq;
};

struct semid_ds32 {
        struct ipc_perm32 sem_perm;               /* permissions .. see ipc.h */
        __kernel_time_t32 sem_otime;              /* last semop time */
        __kernel_time_t32 sem_ctime;              /* last change time */
        u32 sem_base;              /* ptr to first semaphore in array */
        u32 sem_pending;          /* pending operations to be processed */
        u32 sem_pending_last;    /* last pending operation */
        u32 undo;                  /* undo requests on this array */
        unsigned short  sem_nsems;              /* no. of semaphores in array */
};

struct msqid_ds32
{
        struct ipc_perm32 msg_perm;
        u32 msg_first;
        u32 msg_last;
        __kernel_time_t32 msg_stime;
        __kernel_time_t32 msg_rtime;
        __kernel_time_t32 msg_ctime;
        u32 wwait;
        u32 rwait;
        unsigned short msg_cbytes;
        unsigned short msg_qnum;  
        unsigned short msg_qbytes;
        __kernel_ipc_pid_t32 msg_lspid;
        __kernel_ipc_pid_t32 msg_lrpid;
};

struct shmid_ds32 {
        struct ipc_perm32       shm_perm;
        int                     shm_segsz;
        __kernel_time_t32       shm_atime;
        __kernel_time_t32       shm_dtime;
        __kernel_time_t32       shm_ctime;
        __kernel_ipc_pid_t32    shm_cpid; 
        __kernel_ipc_pid_t32    shm_lpid; 
        unsigned short          shm_nattch;
};

#define IPCOP_MASK(__x)	(1UL << (__x))

static int
do_sys32_semctl(int first, int second, int third, void *uptr)
{
	union semun fourth;
	u32 pad;
	int err, err2;
	struct semid64_ds s;
	struct semid_ds32 *usp;
	mm_segment_t old_fs;

	if (!uptr)
		return -EINVAL;
	err = -EFAULT;
	if (get_user (pad, (u32 *)uptr))
		return err;
	if(third == SETVAL)
		fourth.val = (int)pad;
	else
		fourth.__pad = (void *)A(pad);
	switch (third) {

	case IPC_INFO:
	case IPC_RMID:
	case IPC_SET:
	case SEM_INFO:
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETALL:
	case SETVAL:
	case SETALL:
		err = sys_semctl (first, second, third, fourth);
		break;

	case IPC_STAT:
	case SEM_STAT:
		usp = (struct semid_ds32 *)A(pad);
		fourth.__pad = &s;
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_semctl (first, second, third, fourth);
		set_fs (old_fs);
		err2 = put_user(s.sem_perm.key, &usp->sem_perm.key);
		err2 |= __put_user(s.sem_perm.uid, &usp->sem_perm.uid);
		err2 |= __put_user(s.sem_perm.gid, &usp->sem_perm.gid);
		err2 |= __put_user(s.sem_perm.cuid,
				   &usp->sem_perm.cuid);
		err2 |= __put_user (s.sem_perm.cgid,
				    &usp->sem_perm.cgid);
		err2 |= __put_user (s.sem_perm.mode,
				    &usp->sem_perm.mode);
		err2 |= __put_user (s.sem_perm.seq, &usp->sem_perm.seq);
		err2 |= __put_user (s.sem_otime, &usp->sem_otime);
		err2 |= __put_user (s.sem_ctime, &usp->sem_ctime);
		err2 |= __put_user (s.sem_nsems, &usp->sem_nsems);
		if (err2)
			err = -EFAULT;
		break;

	}

	return err;
}

static int
do_sys32_msgsnd (int first, int second, int third, void *uptr)
{
	struct msgbuf *p = kmalloc (second + sizeof (struct msgbuf)
				    + 4, GFP_USER);
	struct msgbuf32 *up = (struct msgbuf32 *)uptr;
	mm_segment_t old_fs;
	int err;

	if (!p)
		return -ENOMEM;
	err = get_user (p->mtype, &up->mtype);
	err |= __copy_from_user (p->mtext, &up->mtext, second);
	if (err)
		goto out;
	old_fs = get_fs ();
	set_fs (KERNEL_DS);
	err = sys_msgsnd (first, p, second, third);
	set_fs (old_fs);
out:
	kfree (p);
	return err;
}

static int
do_sys32_msgrcv (int first, int second, int msgtyp, int third,
		 int version, void *uptr)
{
	struct msgbuf32 *up;
	struct msgbuf *p;
	mm_segment_t old_fs;
	int err;

	if (!version) {
		struct ipc_kludge *uipck = (struct ipc_kludge *)uptr;
		struct ipc_kludge ipck;

		err = -EINVAL;
		if (!uptr)
			goto out;
		err = -EFAULT;
		if (copy_from_user (&ipck, uipck, sizeof (struct ipc_kludge)))
			goto out;
		uptr = (void *)A(ipck.msgp);
		msgtyp = ipck.msgtyp;
	}
	err = -ENOMEM;
	p = kmalloc (second + sizeof (struct msgbuf) + 4, GFP_USER);
	if (!p)
		goto out;
	old_fs = get_fs ();
	set_fs (KERNEL_DS);
	err = sys_msgrcv (first, p, second + 4, msgtyp, third);
	set_fs (old_fs);
	if (err < 0)
		goto free_then_out;
	up = (struct msgbuf32 *)uptr;
	if (put_user (p->mtype, &up->mtype) ||
	    __copy_to_user (&up->mtext, p->mtext, err))
		err = -EFAULT;
free_then_out:
	kfree (p);
out:
	return err;
}

static int
do_sys32_msgctl (int first, int second, void *uptr)
{
	int err = -EINVAL, err2;
	struct msqid_ds m;
	struct msqid64_ds m64;
	struct msqid_ds32 *up = (struct msqid_ds32 *)uptr;
	mm_segment_t old_fs;

	switch (second) {

	case IPC_INFO:
	case IPC_RMID:
	case MSG_INFO:
		err = sys_msgctl (first, second, (struct msqid_ds *)uptr);
		break;

	case IPC_SET:
		err = get_user (m.msg_perm.uid, &up->msg_perm.uid);
		err |= __get_user (m.msg_perm.gid, &up->msg_perm.gid);
		err |= __get_user (m.msg_perm.mode, &up->msg_perm.mode);
		err |= __get_user (m.msg_qbytes, &up->msg_qbytes);
		if (err)
			break;
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_msgctl (first, second, &m);
		set_fs (old_fs);
		break;

	case IPC_STAT:
	case MSG_STAT:
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_msgctl (first, second, (void *) &m64);
		set_fs (old_fs);
		err2 = put_user (m64.msg_perm.key, &up->msg_perm.key);
		err2 |= __put_user(m64.msg_perm.uid, &up->msg_perm.uid);
		err2 |= __put_user(m64.msg_perm.gid, &up->msg_perm.gid);
		err2 |= __put_user(m64.msg_perm.cuid, &up->msg_perm.cuid);
		err2 |= __put_user(m64.msg_perm.cgid, &up->msg_perm.cgid);
		err2 |= __put_user(m64.msg_perm.mode, &up->msg_perm.mode);
		err2 |= __put_user(m64.msg_perm.seq, &up->msg_perm.seq);
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
		break;

	}

	return err;
}

static int
do_sys32_shmat (int first, int second, int third, int version, void *uptr)
{
	unsigned long raddr;
	u32 *uaddr = (u32 *)A((u32)third);
	int err = -EINVAL;

	if (version == 1)
		return err;
	err = sys_shmat (first, uptr, second, &raddr);
	if (err)
		return err;
	err = put_user (raddr, uaddr);
	return err;
}

static int
do_sys32_shmctl (int first, int second, void *uptr)
{
	int err = -EFAULT, err2;
	struct shmid_ds s;
	struct shmid64_ds s64;
	struct shmid_ds32 *up = (struct shmid_ds32 *)uptr;
	mm_segment_t old_fs;
	struct shm_info32 {
		int used_ids;
		u32 shm_tot, shm_rss, shm_swp;
		u32 swap_attempts, swap_successes;
	} *uip = (struct shm_info32 *)uptr;
	struct shm_info si;

	switch (second) {

	case IPC_INFO:
	case IPC_RMID:
	case SHM_LOCK:
	case SHM_UNLOCK:
		err = sys_shmctl (first, second, (struct shmid_ds *)uptr);
		break;
	case IPC_SET:
		err = get_user (s.shm_perm.uid, &up->shm_perm.uid);
		err |= __get_user (s.shm_perm.gid, &up->shm_perm.gid);
		err |= __get_user (s.shm_perm.mode, &up->shm_perm.mode);
		if (err)
			break;
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_shmctl (first, second, &s);
		set_fs (old_fs);
		break;

	case IPC_STAT:
	case SHM_STAT:
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_shmctl (first, second, (void *) &s64);
		set_fs (old_fs);
		if (err < 0)
			break;
		err2 = put_user (s64.shm_perm.key, &up->shm_perm.key);
		err2 |= __put_user (s64.shm_perm.uid, &up->shm_perm.uid);
		err2 |= __put_user (s64.shm_perm.gid, &up->shm_perm.gid);
		err2 |= __put_user (s64.shm_perm.cuid,
				    &up->shm_perm.cuid);
		err2 |= __put_user (s64.shm_perm.cgid,
				    &up->shm_perm.cgid);
		err2 |= __put_user (s64.shm_perm.mode,
				    &up->shm_perm.mode);
		err2 |= __put_user (s64.shm_perm.seq, &up->shm_perm.seq);
		err2 |= __put_user (s64.shm_atime, &up->shm_atime);
		err2 |= __put_user (s64.shm_dtime, &up->shm_dtime);
		err2 |= __put_user (s64.shm_ctime, &up->shm_ctime);
		err2 |= __put_user (s64.shm_segsz, &up->shm_segsz);
		err2 |= __put_user (s64.shm_nattch, &up->shm_nattch);
		err2 |= __put_user (s64.shm_cpid, &up->shm_cpid);
		err2 |= __put_user (s64.shm_lpid, &up->shm_lpid);
		if (err2)
			err = -EFAULT;
		break;

	case SHM_INFO:
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_shmctl (first, second, (void *)&si);
		set_fs (old_fs);
		if (err < 0)
			break;
		err2 = put_user (si.used_ids, &uip->used_ids);
		err2 |= __put_user (si.shm_tot, &uip->shm_tot);
		err2 |= __put_user (si.shm_rss, &uip->shm_rss);
		err2 |= __put_user (si.shm_swp, &uip->shm_swp);
		err2 |= __put_user (si.swap_attempts,
				    &uip->swap_attempts);
		err2 |= __put_user (si.swap_successes,
				    &uip->swap_successes);
		if (err2)
			err = -EFAULT;
		break;

	}
	return err;
}

asmlinkage long
sys32_ipc (u32 call, int first, int second, int third, u32 ptr, u32 fifth)
{
	int version, err;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {

	case SEMOP:
		/* struct sembuf is the same on 32 and 64bit :)) */
		err = sys_semop (first, (struct sembuf *)AA(ptr),
				 second);
		break;
	case SEMGET:
		err = sys_semget (first, second, third);
		break;
	case SEMCTL:
		err = do_sys32_semctl (first, second, third,
				       (void *)AA(ptr));
		break;

	case MSGSND:
		err = do_sys32_msgsnd (first, second, third,
				       (void *)AA(ptr));
		break;
	case MSGRCV:
		err = do_sys32_msgrcv (first, second, fifth, third,
				       version, (void *)AA(ptr));
		break;
	case MSGGET:
		err = sys_msgget ((key_t) first, second);
		break;
	case MSGCTL:
		err = do_sys32_msgctl (first, second, (void *)AA(ptr));
		break;

	case SHMAT:
		err = do_sys32_shmat (first, second, third,
				      version, (void *)AA(ptr));
		break;
	case SHMDT: 
		err = sys_shmdt ((char *)AA(ptr));
		break;
	case SHMGET:
		err = sys_shmget (first, second, third);
		break;
	case SHMCTL:
		err = do_sys32_shmctl (first, second, (void *)AA(ptr));
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
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
	int err;
	
	err = put_user (r->ru_utime.tv_sec, &ru->ru_utime.tv_sec);
	err |= __put_user (r->ru_utime.tv_usec, &ru->ru_utime.tv_usec);
	err |= __put_user (r->ru_stime.tv_sec, &ru->ru_stime.tv_sec);
	err |= __put_user (r->ru_stime.tv_usec, &ru->ru_stime.tv_usec);
	err |= __put_user (r->ru_maxrss, &ru->ru_maxrss);
	err |= __put_user (r->ru_ixrss, &ru->ru_ixrss);
	err |= __put_user (r->ru_idrss, &ru->ru_idrss);
	err |= __put_user (r->ru_isrss, &ru->ru_isrss);
	err |= __put_user (r->ru_minflt, &ru->ru_minflt);
	err |= __put_user (r->ru_majflt, &ru->ru_majflt);
	err |= __put_user (r->ru_nswap, &ru->ru_nswap);
	err |= __put_user (r->ru_inblock, &ru->ru_inblock);
	err |= __put_user (r->ru_oublock, &ru->ru_oublock);
	err |= __put_user (r->ru_msgsnd, &ru->ru_msgsnd);
	err |= __put_user (r->ru_msgrcv, &ru->ru_msgrcv);
	err |= __put_user (r->ru_nsignals, &ru->ru_nsignals);
	err |= __put_user (r->ru_nvcsw, &ru->ru_nvcsw);
	err |= __put_user (r->ru_nivcsw, &ru->ru_nivcsw);
	return err;
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
                                
extern asmlinkage long sys_times(struct tms * tbuf);

asmlinkage long
sys32_times(struct tms32 *tbuf)
{
	struct tms t;
	long ret;
	mm_segment_t old_fs = get_fs ();
	int err;
	
	set_fs (KERNEL_DS);
	ret = sys_times(tbuf ? &t : NULL);
	set_fs (old_fs);
	if (tbuf) {
		err = put_user (IA32_TICK(t.tms_utime), &tbuf->tms_utime);
		err |= __put_user (IA32_TICK(t.tms_stime), &tbuf->tms_stime);
		err |= __put_user (IA32_TICK(t.tms_cutime), &tbuf->tms_cutime);
		err |= __put_user (IA32_TICK(t.tms_cstime), &tbuf->tms_cstime);
		if (err)
			ret = -EFAULT;
	}
	return IA32_TICK(ret);
}

unsigned int
ia32_peek (struct pt_regs *regs, struct task_struct *child, unsigned long addr, unsigned int *val)
{
	size_t copied;
	unsigned int ret;

	copied = access_process_vm(child, addr, val, sizeof(*val), 0);
	return(copied != sizeof(ret) ? -EIO : 0);
}

unsigned int
ia32_poke (struct pt_regs *regs, struct task_struct *child, unsigned long addr, unsigned int val)
{

	if (access_process_vm(child, addr, &val, sizeof(val), 1) != sizeof(val))
		return -EIO;
	return 0;
}

/*
 *  The order in which registers are stored in the ptrace regs structure
 */
#define PT_EBX	0
#define PT_ECX	1
#define PT_EDX	2
#define PT_ESI	3
#define PT_EDI	4
#define PT_EBP	5
#define PT_EAX	6
#define PT_DS	7
#define PT_ES	8
#define PT_FS	9
#define PT_GS	10
#define PT_ORIG_EAX 11
#define PT_EIP	12
#define PT_CS 	13
#define PT_EFL	14
#define PT_UESP	15
#define PT_SS	16

unsigned int
getreg(struct task_struct *child, int regno)
{
	struct pt_regs *child_regs;

	child_regs = ia64_task_regs(child);
	switch (regno / sizeof(int)) {

	case PT_EBX:
		return(child_regs->r11);
	case PT_ECX:
		return(child_regs->r9);
	case PT_EDX:
		return(child_regs->r10);
	case PT_ESI:
		return(child_regs->r14);
	case PT_EDI:
		return(child_regs->r15);
	case PT_EBP:
		return(child_regs->r13);
	case PT_EAX:
	case PT_ORIG_EAX:
		return(child_regs->r8);
	case PT_EIP:
		return(child_regs->cr_iip);
	case PT_UESP:
		return(child_regs->r12);
	case PT_EFL:
		return(child->thread.eflag);
	case PT_DS:
	case PT_ES:
	case PT_FS:
	case PT_GS:
	case PT_SS:
		return((unsigned int)__USER_DS);
	case PT_CS:
		return((unsigned int)__USER_CS);
	default:
		printk("getregs:unknown register %d\n", regno);
		break;

	}
	return(0);
}

void
putreg(struct task_struct *child, int regno, unsigned int value)
{
	struct pt_regs *child_regs;

	child_regs = ia64_task_regs(child);
	switch (regno / sizeof(int)) {

	case PT_EBX:
		child_regs->r11 = value;
		break;
	case PT_ECX:
		child_regs->r9 = value;
		break;
	case PT_EDX:
		child_regs->r10 = value;
		break;
	case PT_ESI:
		child_regs->r14 = value;
		break;
	case PT_EDI:
		child_regs->r15 = value;
		break;
	case PT_EBP:
		child_regs->r13 = value;
		break;
	case PT_EAX:
	case PT_ORIG_EAX:
		child_regs->r8 = value;
		break;
	case PT_EIP:
		child_regs->cr_iip = value;
		break;
	case PT_UESP:
		child_regs->r12 = value;
		break;
	case PT_EFL:
		child->thread.eflag = value;
		break;
	case PT_DS:
	case PT_ES:
	case PT_FS:
	case PT_GS:
	case PT_SS:
		if (value != __USER_DS)
			printk("setregs:try to set invalid segment register %d = %x\n", regno, value);
		break;
	case PT_CS:
		if (value != __USER_CS)
			printk("setregs:try to set invalid segment register %d = %x\n", regno, value);
		break;
	default:
		printk("getregs:unknown register %d\n", regno);
		break;

	}
}

static inline void
ia32f2ia64f(void *dst, void *src)
{

	__asm__ ("ldfe f6=[%1] ;;\n\t"
		 "stf.spill [%0]=f6"
		:
		: "r"(dst), "r"(src));
	return;
}

static inline void
ia64f2ia32f(void *dst, void *src)
{

	__asm__ ("ldf.fill f6=[%1] ;;\n\t"
		 "stfe [%0]=f6"
		:
		: "r"(dst),  "r"(src));
	return;
}

void
put_fpreg(int regno, struct _fpreg_ia32 *reg, struct pt_regs *ptp, struct switch_stack *swp, int tos)
{
	struct _fpreg_ia32 *f;
	char buf[32];

	f = (struct _fpreg_ia32 *)(((unsigned long)buf + 15) & ~15);
	if ((regno += tos) >= 8)
		regno -= 8;
	switch (regno) {

	case 0:
		ia64f2ia32f(f, &ptp->f8);
		break;
	case 1:
		ia64f2ia32f(f, &ptp->f9);
		break;
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		ia64f2ia32f(f, &swp->f10 + (regno - 2));
		break;

	}
	__copy_to_user(reg, f, sizeof(*reg));
	return;
}

void
get_fpreg(int regno, struct _fpreg_ia32 *reg, struct pt_regs *ptp, struct switch_stack *swp, int tos)
{

	if ((regno += tos) >= 8)
		regno -= 8;
	switch (regno) {

	case 0:
		__copy_from_user(&ptp->f8, reg, sizeof(*reg));
		break;
	case 1:
		__copy_from_user(&ptp->f9, reg, sizeof(*reg));
		break;
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		__copy_from_user(&swp->f10 + (regno - 2), reg, sizeof(*reg));
		break;

	}
	return;
}

int
save_ia32_fpstate(struct task_struct *tsk, struct _fpstate_ia32 *save)
{
	struct switch_stack *swp;
	struct pt_regs *ptp;
	int i, tos;

	if (!access_ok(VERIFY_WRITE, save, sizeof(*save)))
		return(-EIO);
	__put_user(tsk->thread.fcr, &save->cw);
	__put_user(tsk->thread.fsr, &save->sw);
	__put_user(tsk->thread.fsr >> 32, &save->tag);
	__put_user(tsk->thread.fir, &save->ipoff);
	__put_user(__USER_CS, &save->cssel);
	__put_user(tsk->thread.fdr, &save->dataoff);
	__put_user(__USER_DS, &save->datasel);
	/*
	 *  Stack frames start with 16-bytes of temp space
	 */
	swp = (struct switch_stack *)(tsk->thread.ksp + 16);
	ptp = ia64_task_regs(tsk);
	tos = (tsk->thread.fsr >> 11) & 3;
	for (i = 0; i < 8; i++)
		put_fpreg(i, &save->_st[i], ptp, swp, tos);
	return(0);
}

int
restore_ia32_fpstate(struct task_struct *tsk, struct _fpstate_ia32 *save)
{
	struct switch_stack *swp;
	struct pt_regs *ptp;
	int i, tos, ret;
	int fsrlo, fsrhi;

	if (!access_ok(VERIFY_READ, save, sizeof(*save)))
		return(-EIO);
	ret = __get_user(tsk->thread.fcr, (unsigned int *)&save->cw);
	ret |= __get_user(fsrlo, (unsigned int *)&save->sw);
	ret |= __get_user(fsrhi, (unsigned int *)&save->tag);
	tsk->thread.fsr = ((long)fsrhi << 32) | (long)fsrlo;
	ret |= __get_user(tsk->thread.fir, (unsigned int *)&save->ipoff);
	ret |= __get_user(tsk->thread.fdr, (unsigned int *)&save->dataoff);
	/*
	 *  Stack frames start with 16-bytes of temp space
	 */
	swp = (struct switch_stack *)(tsk->thread.ksp + 16);
	ptp = ia64_task_regs(tsk);
	tos = (tsk->thread.fsr >> 11) & 3;
	for (i = 0; i < 8; i++)
		get_fpreg(i, &save->_st[i], ptp, swp, tos);
	return(ret ? -EFAULT : 0);
}

asmlinkage long sys_ptrace(long, pid_t, unsigned long, unsigned long, long, long, long, long, long);

/*
 *  Note that the IA32 version of `ptrace' calls the IA64 routine for
 *    many of the requests.  This will only work for requests that do
 *    not need access to the calling processes `pt_regs' which is located
 *    at the address of `stack'.  Once we call the IA64 `sys_ptrace' then
 *    the address of `stack' will not be the address of the `pt_regs'.
 */
asmlinkage long
sys32_ptrace (long request, pid_t pid, unsigned long addr, unsigned long data,
	    long arg4, long arg5, long arg6, long arg7, long stack)
{
	struct pt_regs *regs = (struct pt_regs *) &stack;
	struct task_struct *child;
	long i, ret;
	unsigned int value;

	lock_kernel();
	if (request == PTRACE_TRACEME) {
		ret = sys_ptrace(request, pid, addr, data,
				arg4, arg5, arg6, arg7, stack);
		goto out;
	}

	ret = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	read_unlock(&tasklist_lock);
	if (!child)
		goto out;
	ret = -EPERM;
	if (pid == 1)		/* no messing around with init! */
		goto out;

	if (request == PTRACE_ATTACH) {
		ret = sys_ptrace(request, pid, addr, data,
				arg4, arg5, arg6, arg7, stack);
		goto out;
	}
	ret = -ESRCH;
	if (!(child->ptrace & PT_PTRACED))
		goto out;
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			goto out;
	}
	if (child->p_pptr != current)
		goto out;

	switch (request) {
	      case PTRACE_PEEKTEXT:
	      case PTRACE_PEEKDATA:	/* read word at location addr */
		ret = ia32_peek(regs, child, addr, &value);
		if (ret == 0)
			ret = put_user(value, (unsigned int *)data);
		else
			ret = -EIO;
		goto out;

	      case PTRACE_POKETEXT:
	      case PTRACE_POKEDATA:	/* write the word at location addr */
		ret = ia32_poke(regs, child, addr, (unsigned int)data);
		goto out;

	      case PTRACE_PEEKUSR:	/* read word at addr in USER area */
		ret = 0;
		break;

	      case PTRACE_POKEUSR:	/* write word at addr in USER area */
		ret = 0;
		break;

	      case IA32_PTRACE_GETREGS:
	  	if (!access_ok(VERIFY_WRITE, (int *)data, 17*sizeof(int))) {
			ret = -EIO;
			break;
		}
		for ( i = 0; i < 17*sizeof(int); i += sizeof(int) ) {
			__put_user(getreg(child, i),(unsigned int *) data);
			data += sizeof(int);
		}
		ret = 0;
		break;

	      case IA32_PTRACE_SETREGS:
	      {
		unsigned int tmp;
	  	if (!access_ok(VERIFY_READ, (int *)data, 17*sizeof(int))) {
			ret = -EIO;
			break;
		}
		for ( i = 0; i < 17*sizeof(int); i += sizeof(int) ) {
			__get_user(tmp, (unsigned int *) data);
			putreg(child, i, tmp);
			data += sizeof(int);
		}
		ret = 0;
		break;
	      }

	      case IA32_PTRACE_GETFPREGS:
		ret = save_ia32_fpstate(child, (struct _fpstate_ia32 *)data);
		break;

	      case IA32_PTRACE_SETFPREGS:
		ret = restore_ia32_fpstate(child, (struct _fpstate_ia32 *)data);
		break;

	      case PTRACE_SYSCALL:	/* continue, stop after next syscall */
	      case PTRACE_CONT:		/* restart after signal. */
	      case PTRACE_KILL:
	      case PTRACE_SINGLESTEP:	/* execute chile for one instruction */
	      case PTRACE_DETACH:	/* detach a process */
		unlock_kernel();
		ret = sys_ptrace(request, pid, addr, data,
				arg4, arg5, arg6, arg7, stack);
		return(ret);

	      default:
		ret = -EIO;
		break;

	}
  out:
	unlock_kernel();
	return ret;
}

static inline int
get_flock32(struct flock *kfl, struct flock32 *ufl)
{
	int err;
	
	err = get_user(kfl->l_type, &ufl->l_type);
	err |= __get_user(kfl->l_whence, &ufl->l_whence);
	err |= __get_user(kfl->l_start, &ufl->l_start);
	err |= __get_user(kfl->l_len, &ufl->l_len);
	err |= __get_user(kfl->l_pid, &ufl->l_pid);
	return err;
}

static inline int
put_flock32(struct flock *kfl, struct flock32 *ufl)
{
	int err;
	
	err = __put_user(kfl->l_type, &ufl->l_type);
	err |= __put_user(kfl->l_whence, &ufl->l_whence);
	err |= __put_user(kfl->l_start, &ufl->l_start);
	err |= __put_user(kfl->l_len, &ufl->l_len);
	err |= __put_user(kfl->l_pid, &ufl->l_pid);
	return err;
}

extern asmlinkage long sys_fcntl(unsigned int fd, unsigned int cmd,
				 unsigned long arg);

asmlinkage long
sys32_fcntl(unsigned int fd, unsigned int cmd, int arg)
{
	struct flock f;
	mm_segment_t old_fs;
	long ret;

	switch (cmd) {
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
		if(cmd != F_GETLK && get_flock32(&f, (struct flock32 *)((long)arg)))
			return -EFAULT;
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = sys_fcntl(fd, cmd, (unsigned long)&f);
		set_fs(old_fs);
		if(cmd == F_GETLK && put_flock32(&f, (struct flock32 *)((long)arg)))
			return -EFAULT;
		return ret;
	default:
		/*
		 *  `sys_fcntl' lies about arg, for the F_SETOWN
		 *  sub-function arg can have a negative value.
		 */
		return sys_fcntl(fd, cmd, (unsigned long)((long)arg));
	}
}

asmlinkage long
sys32_sigaction (int sig, struct old_sigaction32 *act, struct old_sigaction32 *oact)
{
        struct k_sigaction new_ka, old_ka;
        int ret;

        if (act) {
		old_sigset32_t mask;
		
		ret = get_user((long)new_ka.sa.sa_handler, &act->sa_handler);
		ret |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		ret |= __get_user(mask, &act->sa_mask);
		if (ret)
			return ret;
		siginitset(&new_ka.sa.sa_mask, mask);
        }

        ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		ret = put_user((long)old_ka.sa.sa_handler, &oact->sa_handler);
		ret |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		ret |= __put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
        }

	return ret;
}

asmlinkage long sys_ni_syscall(void);

asmlinkage long
sys32_ni_syscall(int dummy0, int dummy1, int dummy2, int dummy3,
	int dummy4, int dummy5, int dummy6, int dummy7, int stack)
{
	struct pt_regs *regs = (struct pt_regs *)&stack;

	printk("IA32 syscall #%d issued, maybe we should implement it\n",
		(int)regs->r1);
	return(sys_ni_syscall());
}

/*
 *  The IA64 maps 4 I/O ports for each 4K page
 */
#define IOLEN	((65536 / 4) * 4096)

asmlinkage long
sys_iopl (int level, long arg1, long arg2, long arg3)
{
	extern unsigned long ia64_iobase;
	int fd;
	struct file * file;
	unsigned int old;
	unsigned long addr;
	mm_segment_t old_fs = get_fs ();

	if (level != 3)
		return(-EINVAL);
	/* Trying to gain more privileges? */
	__asm__ __volatile__("mov %0=ar.eflag ;;" : "=r"(old));
	if (level > ((old >> 12) & 3)) {
		if (!capable(CAP_SYS_RAWIO))
			return -EPERM;
	}
	set_fs(KERNEL_DS);
	fd = sys_open("/dev/mem", O_SYNC | O_RDWR, 0);
	set_fs(old_fs);
	if (fd < 0)
		return fd;
	file = fget(fd);
	if (file == NULL) {
		sys_close(fd);
		return(-EFAULT);
	}

	down(&current->mm->mmap_sem);
	lock_kernel();

	addr = do_mmap_pgoff(file, IA32_IOBASE,
			IOLEN, PROT_READ|PROT_WRITE, MAP_SHARED,
			(ia64_iobase & ~PAGE_OFFSET) >> PAGE_SHIFT);

	unlock_kernel();
	up(&current->mm->mmap_sem);

	if (addr >= 0) {
		__asm__ __volatile__("mov ar.k0=%0 ;;" :: "r"(addr));
		old = (old & ~0x3000) | (level << 12);
		__asm__ __volatile__("mov ar.eflag=%0 ;;" :: "r"(old));
	}

	fput(file);
	sys_close(fd);
	return 0;
}

asmlinkage long
sys_ioperm (unsigned long from, unsigned long num, int on)
{

	/*
	 *  Since IA64 doesn't have permission bits we'd have to go to
	 *    a lot of trouble to simulate them in software.  There's
	 *    no point, only trusted programs can make this call so we'll
	 *    just turn it into an iopl call and let the process have
	 *    access to all I/O ports.
	 *
	 * XXX proper ioperm() support should be emulated by
	 *	manipulating the page protections...
	 */
	return(sys_iopl(3, 0, 0, 0));
}

typedef struct {
	unsigned int	ss_sp;
	unsigned int	ss_flags;
	unsigned int	ss_size;
} ia32_stack_t;

asmlinkage long
sys32_sigaltstack (const ia32_stack_t *uss32, ia32_stack_t *uoss32,
long arg2, long arg3, long arg4,
long arg5, long arg6, long arg7,
long stack)
{
	struct pt_regs *pt = (struct pt_regs *) &stack;
	stack_t uss, uoss;
	ia32_stack_t buf32;
	int ret;
	mm_segment_t old_fs = get_fs();

	if (uss32)
		if (copy_from_user(&buf32, (void *)A(uss32), sizeof(ia32_stack_t)))
			return(-EFAULT);
	uss.ss_sp = (void *) (long) buf32.ss_sp;
	uss.ss_flags = buf32.ss_flags;
	uss.ss_size = buf32.ss_size;
	set_fs(KERNEL_DS);
	ret = do_sigaltstack(uss32 ? &uss : NULL, &uoss, pt->r12);
	set_fs(old_fs);
	if (ret < 0)
		return(ret);
	if (uoss32) {
		buf32.ss_sp = (long) uoss.ss_sp;
		buf32.ss_flags = uoss.ss_flags;
		buf32.ss_size = uoss.ss_size;
		if (copy_to_user((void*)A(uoss32), &buf32, sizeof(ia32_stack_t)))
			return(-EFAULT);
	}
	return(ret);
}

asmlinkage int
sys_pause (void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

#ifdef	NOTYET  /* UNTESTED FOR IA64 FROM HERE DOWN */

/* In order to reduce some races, while at the same time doing additional
 * checking and hopefully speeding things up, we copy filenames to the
 * kernel data space before using them..
 *
 * POSIX.1 2.4: an empty pathname is invalid (ENOENT).
 */
static inline int
do_getname32(const char *filename, char *page)
{
	int retval;

	/* 32bit pointer will be always far below TASK_SIZE :)) */
	retval = strncpy_from_user((char *)page, (char *)filename, PAGE_SIZE);
	if (retval > 0) {
		if (retval < PAGE_SIZE)
			return 0;
		return -ENAMETOOLONG;
	} else if (!retval)
		retval = -ENOENT;
	return retval;
}

char *
getname32(const char *filename)
{
	char *tmp, *result;

	result = ERR_PTR(-ENOMEM);
	tmp = (char *)__get_free_page(GFP_KERNEL);
	if (tmp)  {
		int retval = do_getname32(filename, tmp);

		result = tmp;
		if (retval < 0) {
			putname(tmp);
			result = ERR_PTR(retval);
		}
	}
	return result;
}

/* 32-bit timeval and related flotsam.  */

extern asmlinkage long sys_ioperm(unsigned long from, unsigned long num, int on);

asmlinkage long
sys32_ioperm(u32 from, u32 num, int on)
{
	return sys_ioperm((unsigned long)from, (unsigned long)num, on);
}

struct dqblk32 {
    __u32 dqb_bhardlimit;
    __u32 dqb_bsoftlimit;
    __u32 dqb_curblocks;
    __u32 dqb_ihardlimit;
    __u32 dqb_isoftlimit;
    __u32 dqb_curinodes;
    __kernel_time_t32 dqb_btime;
    __kernel_time_t32 dqb_itime;
};
                                
extern asmlinkage long sys_quotactl(int cmd, const char *special, int id,
				   caddr_t addr);

asmlinkage long
sys32_quotactl(int cmd, const char *special, int id, unsigned long addr)
{
	int cmds = cmd >> SUBCMDSHIFT;
	int err;
	struct dqblk d;
	mm_segment_t old_fs;
	char *spec;
	
	switch (cmds) {
	case Q_GETQUOTA:
		break;
	case Q_SETQUOTA:
	case Q_SETUSE:
	case Q_SETQLIM:
		if (copy_from_user (&d, (struct dqblk32 *)addr,
				    sizeof (struct dqblk32)))
			return -EFAULT;
		d.dqb_itime = ((struct dqblk32 *)&d)->dqb_itime;
		d.dqb_btime = ((struct dqblk32 *)&d)->dqb_btime;
		break;
	default:
		return sys_quotactl(cmd, special,
				    id, (caddr_t)addr);
	}
	spec = getname32 (special);
	err = PTR_ERR(spec);
	if (IS_ERR(spec)) return err;
	old_fs = get_fs ();
	set_fs (KERNEL_DS);
	err = sys_quotactl(cmd, (const char *)spec, id, (caddr_t)&d);
	set_fs (old_fs);
	putname (spec);
	if (cmds == Q_GETQUOTA) {
		__kernel_time_t b = d.dqb_btime, i = d.dqb_itime;
		((struct dqblk32 *)&d)->dqb_itime = i;
		((struct dqblk32 *)&d)->dqb_btime = b;
		if (copy_to_user ((struct dqblk32 *)addr, &d,
				  sizeof (struct dqblk32)))
			return -EFAULT;
	}
	return err;
}

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
	if (get_user (t.actime, &times->actime) ||
	    __get_user (t.modtime, &times->modtime))
		return -EFAULT;
	filenam = getname32 (filename);
	ret = PTR_ERR(filenam);
	if (!IS_ERR(filenam)) {
		old_fs = get_fs();
		set_fs (KERNEL_DS); 
		ret = sys_utime(filenam, &t);
		set_fs (old_fs);
		putname (filenam);
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

extern asmlinkage long sys_sysfs(int option, unsigned long arg1,
				unsigned long arg2);

asmlinkage long
sys32_sysfs(int option, u32 arg1, u32 arg2)
{
	return sys_sysfs(option, arg1, arg2);
}

struct ncp_mount_data32 {
        int version;
        unsigned int ncp_fd;
        __kernel_uid_t32 mounted_uid;
        __kernel_pid_t32 wdog_pid;
        unsigned char mounted_vol[NCP_VOLNAME_LEN + 1];
        unsigned int time_out;
        unsigned int retry_count;
        unsigned int flags;
        __kernel_uid_t32 uid;
        __kernel_gid_t32 gid;
        __kernel_mode_t32 file_mode;
        __kernel_mode_t32 dir_mode;
};

static void *
do_ncp_super_data_conv(void *raw_data)
{
	struct ncp_mount_data *n = (struct ncp_mount_data *)raw_data;
	struct ncp_mount_data32 *n32 = (struct ncp_mount_data32 *)raw_data;

	n->dir_mode = n32->dir_mode;
	n->file_mode = n32->file_mode;
	n->gid = n32->gid;
	n->uid = n32->uid;
	memmove (n->mounted_vol, n32->mounted_vol,
		 (sizeof (n32->mounted_vol) + 3 * sizeof (unsigned int)));
	n->wdog_pid = n32->wdog_pid;
	n->mounted_uid = n32->mounted_uid;
	return raw_data;
}

struct smb_mount_data32 {
        int version;
        __kernel_uid_t32 mounted_uid;
        __kernel_uid_t32 uid;
        __kernel_gid_t32 gid;
        __kernel_mode_t32 file_mode;
        __kernel_mode_t32 dir_mode;
};

static void *
do_smb_super_data_conv(void *raw_data)
{
	struct smb_mount_data *s = (struct smb_mount_data *)raw_data;
	struct smb_mount_data32 *s32 = (struct smb_mount_data32 *)raw_data;

	s->version = s32->version;
	s->mounted_uid = s32->mounted_uid;
	s->uid = s32->uid;
	s->gid = s32->gid;
	s->file_mode = s32->file_mode;
	s->dir_mode = s32->dir_mode;
	return raw_data;
}

static int
copy_mount_stuff_to_kernel(const void *user, unsigned long *kernel)
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

extern asmlinkage long sys_mount(char * dev_name, char * dir_name, char * type,
				unsigned long new_flags, void *data);

#define SMBFS_NAME	"smbfs"
#define NCPFS_NAME	"ncpfs"

asmlinkage long
sys32_mount(char *dev_name, char *dir_name, char *type,
	    unsigned long new_flags, u32 data)
{
	unsigned long type_page;
	int err, is_smb, is_ncp;

	if(!capable(CAP_SYS_ADMIN))
		return -EPERM;
	is_smb = is_ncp = 0;
	err = copy_mount_stuff_to_kernel((const void *)type, &type_page);
	if(err)
		return err;
	if(type_page) {
		is_smb = !strcmp((char *)type_page, SMBFS_NAME);
		is_ncp = !strcmp((char *)type_page, NCPFS_NAME);
	}
	if(!is_smb && !is_ncp) {
		if(type_page)
			free_page(type_page);
		return sys_mount(dev_name, dir_name, type, new_flags,
				 (void *)AA(data));
	} else {
		unsigned long dev_page, dir_page, data_page;

		err = copy_mount_stuff_to_kernel((const void *)dev_name,
						 &dev_page);
		if(err)
			goto out;
		err = copy_mount_stuff_to_kernel((const void *)dir_name,
						 &dir_page);
		if(err)
			goto dev_out;
		err = copy_mount_stuff_to_kernel((const void *)AA(data),
						 &data_page);
		if(err)
			goto dir_out;
		if(is_ncp)
			do_ncp_super_data_conv((void *)data_page);
		else if(is_smb)
			do_smb_super_data_conv((void *)data_page);
		else
			panic("The problem is here...");
		err = do_mount((char *)dev_page, (char *)dir_page,
				(char *)type_page, new_flags,
				(void *)data_page);
		if(data_page)
			free_page(data_page);
	dir_out:
		if(dir_page)
			free_page(dir_page);
	dev_out:
		if(dev_page)
			free_page(dev_page);
	out:
		if(type_page)
			free_page(type_page);
		return err;
	}
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
	if (err)
		return -EFAULT;
	return ret;
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
	if (put_user (t.tv_sec, &interval->tv_sec) ||
	    __put_user (t.tv_nsec, &interval->tv_nsec))
		return -EFAULT;
	return ret;
}

extern asmlinkage long sys_sigprocmask(int how, old_sigset_t *set,
				      old_sigset_t *oset);

asmlinkage long
sys32_sigprocmask(int how, old_sigset_t32 *set, old_sigset_t32 *oset)
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
sys32_sigpending(old_sigset_t32 *set)
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
sys32_rt_sigpending(sigset_t32 *set, __kernel_size_t32 sigsetsize)
{
	sigset_t s;
	sigset_t32 s32;
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
		if (copy_to_user (set, &s32, sizeof(sigset_t32)))
			return -EFAULT;
	}
	return ret;
}

siginfo_t32 *
siginfo64to32(siginfo_t32 *d, siginfo_t *s)
{
	memset (&d, 0, sizeof(siginfo_t32));
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
		/* XXX: Do we need to translate this from ia64 to ia32 traps? */
		d->si_trapno = s->si_trapno;
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
		/* XXX: Do we need to translate this from ia32 to ia64 traps? */
		d->si_trapno = s->si_trapno;
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
sys32_rt_sigtimedwait(sigset_t32 *uthese, siginfo_t32 *uinfo,
		      struct timespec32 *uts, __kernel_size_t32 sigsetsize)
{
	sigset_t s;
	sigset_t32 s32;
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs();
	siginfo_t info;
	siginfo_t32 info32;
		
	if (copy_from_user (&s32, uthese, sizeof(sigset_t32)))
		return -EFAULT;
	switch (_NSIG_WORDS) {
	case 4: s.sig[3] = s32.sig[6] | (((long)s32.sig[7]) << 32);
	case 3: s.sig[2] = s32.sig[4] | (((long)s32.sig[5]) << 32);
	case 2: s.sig[1] = s32.sig[2] | (((long)s32.sig[3]) << 32);
	case 1: s.sig[0] = s32.sig[0] | (((long)s32.sig[1]) << 32);
	}
	if (uts) {
		ret = get_user (t.tv_sec, &uts->tv_sec);
		ret |= __get_user (t.tv_nsec, &uts->tv_nsec);
		if (ret)
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
			if (__put_user (gl[i], grouplist))
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
		if (__get_user (gl[i], grouplist))
			return -EFAULT;
        set_fs (KERNEL_DS);
	ret = sys_setgroups(gidsetsize, gl);
	set_fs (old_fs);
	return ret;
}


/* XXX These as well... */
extern __inline__ struct socket *
socki_lookup(struct inode *inode)
{
	return &inode->u.socket_i;
}

extern __inline__ struct socket *
sockfd_lookup(int fd, int *err)
{
	struct file *file;
	struct inode *inode;

	if (!(file = fget(fd)))
	{
		*err = -EBADF;
		return NULL;
	}

	inode = file->f_dentry->d_inode;
	if (!inode->i_sock || !socki_lookup(inode))
	{
		*err = -ENOTSOCK;
		fput(file);
		return NULL;
	}

	return socki_lookup(inode);
}

struct msghdr32 {
        u32               msg_name;
        int               msg_namelen;
        u32               msg_iov;
        __kernel_size_t32 msg_iovlen;
        u32               msg_control;
        __kernel_size_t32 msg_controllen;
        unsigned          msg_flags;
};

struct cmsghdr32 {
        __kernel_size_t32 cmsg_len;
        int               cmsg_level;
        int               cmsg_type;
};

/* Bleech... */
#define __CMSG32_NXTHDR(ctl, len, cmsg, cmsglen) \
        __cmsg32_nxthdr((ctl),(len),(cmsg),(cmsglen))
#define CMSG32_NXTHDR(mhdr, cmsg, cmsglen) \
        cmsg32_nxthdr((mhdr), (cmsg), (cmsglen))

#define CMSG32_ALIGN(len) ( ((len)+sizeof(int)-1) & ~(sizeof(int)-1) )

#define CMSG32_DATA(cmsg) \
	((void *)((char *)(cmsg) + CMSG32_ALIGN(sizeof(struct cmsghdr32))))
#define CMSG32_SPACE(len) \
	(CMSG32_ALIGN(sizeof(struct cmsghdr32)) + CMSG32_ALIGN(len))
#define CMSG32_LEN(len) (CMSG32_ALIGN(sizeof(struct cmsghdr32)) + (len))

#define __CMSG32_FIRSTHDR(ctl,len) ((len) >= sizeof(struct cmsghdr32) ? \
				    (struct cmsghdr32 *)(ctl) : \
				    (struct cmsghdr32 *)NULL)
#define CMSG32_FIRSTHDR(msg) \
	__CMSG32_FIRSTHDR((msg)->msg_control, (msg)->msg_controllen)

__inline__ struct cmsghdr32 *
__cmsg32_nxthdr(void *__ctl, __kernel_size_t __size,
		struct cmsghdr32 *__cmsg, int __cmsg_len) 
{
	struct cmsghdr32 * __ptr;

	__ptr = (struct cmsghdr32 *)(((unsigned char *) __cmsg) +
				     CMSG32_ALIGN(__cmsg_len));
	if ((unsigned long)((char*)(__ptr+1) - (char *) __ctl) > __size)
		return NULL;

	return __ptr;
}

__inline__ struct cmsghdr32 *
cmsg32_nxthdr (struct msghdr *__msg, struct cmsghdr32 *__cmsg, int __cmsg_len)
{
	return __cmsg32_nxthdr(__msg->msg_control, __msg->msg_controllen,
			       __cmsg, __cmsg_len);
}

static inline int
iov_from_user32_to_kern(struct iovec *kiov, struct iovec32 *uiov32, int niov)
{
	int tot_len = 0;

	while(niov > 0) {
		u32 len, buf;

		if(get_user(len, &uiov32->iov_len) ||
		   get_user(buf, &uiov32->iov_base)) {
			tot_len = -EFAULT;
			break;
		}
		tot_len += len;
		kiov->iov_base = (void *)A(buf);
		kiov->iov_len = (__kernel_size_t) len;
		uiov32++;
		kiov++;
		niov--;
	}
	return tot_len;
}

static inline int
msghdr_from_user32_to_kern(struct msghdr *kmsg, struct msghdr32 *umsg)
{
	u32 tmp1, tmp2, tmp3;
	int err;

	err = get_user(tmp1, &umsg->msg_name);
	err |= __get_user(tmp2, &umsg->msg_iov);
	err |= __get_user(tmp3, &umsg->msg_control);
	if (err)
		return -EFAULT;

	kmsg->msg_name = (void *)A(tmp1);
	kmsg->msg_iov = (struct iovec *)A(tmp2);
	kmsg->msg_control = (void *)A(tmp3);

	err = get_user(kmsg->msg_namelen, &umsg->msg_namelen);
	err |= get_user(kmsg->msg_iovlen, &umsg->msg_iovlen);
	err |= get_user(kmsg->msg_controllen, &umsg->msg_controllen);
	err |= get_user(kmsg->msg_flags, &umsg->msg_flags);
	
	return err;
}

/* I've named the args so it is easy to tell whose space the pointers are in. */
static int
verify_iovec32(struct msghdr *kern_msg, struct iovec *kern_iov,
	       char *kern_address, int mode)
{
	int tot_len;

	if(kern_msg->msg_namelen) {
		if(mode==VERIFY_READ) {
			int err = move_addr_to_kernel(kern_msg->msg_name,
						      kern_msg->msg_namelen,
						      kern_address);
			if(err < 0)
				return err;
		}
		kern_msg->msg_name = kern_address;
	} else
		kern_msg->msg_name = NULL;

	if(kern_msg->msg_iovlen > UIO_FASTIOV) {
		kern_iov = kmalloc(kern_msg->msg_iovlen * sizeof(struct iovec),
				   GFP_KERNEL);
		if(!kern_iov)
			return -ENOMEM;
	}

	tot_len = iov_from_user32_to_kern(kern_iov,
					  (struct iovec32 *)kern_msg->msg_iov,
					  kern_msg->msg_iovlen);
	if(tot_len >= 0)
		kern_msg->msg_iov = kern_iov;
	else if(kern_msg->msg_iovlen > UIO_FASTIOV)
		kfree(kern_iov);

	return tot_len;
}

/* There is a lot of hair here because the alignment rules (and
 * thus placement) of cmsg headers and length are different for
 * 32-bit apps.  -DaveM
 */
static int
cmsghdr_from_user32_to_kern(struct msghdr *kmsg, unsigned char *stackbuf,
			    int stackbuf_size) 
{
	struct cmsghdr32 *ucmsg;
	struct cmsghdr *kcmsg, *kcmsg_base;
	__kernel_size_t32 ucmlen;
	__kernel_size_t kcmlen, tmp;

	kcmlen = 0;
	kcmsg_base = kcmsg = (struct cmsghdr *)stackbuf;
	ucmsg = CMSG32_FIRSTHDR(kmsg);
	while(ucmsg != NULL) {
		if(get_user(ucmlen, &ucmsg->cmsg_len))
			return -EFAULT;

		/* Catch bogons. */
		if(CMSG32_ALIGN(ucmlen) <
		   CMSG32_ALIGN(sizeof(struct cmsghdr32)))
			return -EINVAL;
		if((unsigned long)(((char *)ucmsg - (char *)kmsg->msg_control)
				   + ucmlen) > kmsg->msg_controllen)
			return -EINVAL;

		tmp = ((ucmlen - CMSG32_ALIGN(sizeof(*ucmsg))) +
		       CMSG_ALIGN(sizeof(struct cmsghdr)));
		kcmlen += tmp;
		ucmsg = CMSG32_NXTHDR(kmsg, ucmsg, ucmlen);
	}
	if(kcmlen == 0)
		return -EINVAL;

	/* The kcmlen holds the 64-bit version of the control length.
	 * It may not be modified as we do not stick it into the kmsg
	 * until we have successfully copied over all of the data
	 * from the user.
	 */
	if(kcmlen > stackbuf_size)
		kcmsg_base = kcmsg = kmalloc(kcmlen, GFP_KERNEL);
	if(kcmsg == NULL)
		return -ENOBUFS;

	/* Now copy them over neatly. */
	memset(kcmsg, 0, kcmlen);
	ucmsg = CMSG32_FIRSTHDR(kmsg);
	while(ucmsg != NULL) {
		__get_user(ucmlen, &ucmsg->cmsg_len);
		tmp = ((ucmlen - CMSG32_ALIGN(sizeof(*ucmsg))) +
		       CMSG_ALIGN(sizeof(struct cmsghdr)));
		kcmsg->cmsg_len = tmp;
		__get_user(kcmsg->cmsg_level, &ucmsg->cmsg_level);
		__get_user(kcmsg->cmsg_type, &ucmsg->cmsg_type);

		/* Copy over the data. */
		if(copy_from_user(CMSG_DATA(kcmsg),
				  CMSG32_DATA(ucmsg),
				  (ucmlen - CMSG32_ALIGN(sizeof(*ucmsg)))))
			goto out_free_efault;

		/* Advance. */
		kcmsg = (struct cmsghdr *)((char *)kcmsg + CMSG_ALIGN(tmp));
		ucmsg = CMSG32_NXTHDR(kmsg, ucmsg, ucmlen);
	}

	/* Ok, looks like we made it.  Hook it up and return success. */
	kmsg->msg_control = kcmsg_base;
	kmsg->msg_controllen = kcmlen;
	return 0;

out_free_efault:
	if(kcmsg_base != (struct cmsghdr *)stackbuf)
		kfree(kcmsg_base);
	return -EFAULT;
}

static void
put_cmsg32(struct msghdr *kmsg, int level, int type, int len, void *data)
{
	struct cmsghdr32 *cm = (struct cmsghdr32 *) kmsg->msg_control;
	struct cmsghdr32 cmhdr;
	int cmlen = CMSG32_LEN(len);

	if(cm == NULL || kmsg->msg_controllen < sizeof(*cm)) {
		kmsg->msg_flags |= MSG_CTRUNC;
		return;
	}

	if(kmsg->msg_controllen < cmlen) {
		kmsg->msg_flags |= MSG_CTRUNC;
		cmlen = kmsg->msg_controllen;
	}
	cmhdr.cmsg_level = level;
	cmhdr.cmsg_type = type;
	cmhdr.cmsg_len = cmlen;

	if(copy_to_user(cm, &cmhdr, sizeof cmhdr))
		return;
	if(copy_to_user(CMSG32_DATA(cm), data,
			cmlen - sizeof(struct cmsghdr32)))
		return;
	cmlen = CMSG32_SPACE(len);
	kmsg->msg_control += cmlen;
	kmsg->msg_controllen -= cmlen;
}

static void scm_detach_fds32(struct msghdr *kmsg, struct scm_cookie *scm)
{
	struct cmsghdr32 *cm = (struct cmsghdr32 *) kmsg->msg_control;
	int fdmax = (kmsg->msg_controllen - sizeof(struct cmsghdr32))
		/ sizeof(int);
	int fdnum = scm->fp->count;
	struct file **fp = scm->fp->fp;
	int *cmfptr;
	int err = 0, i;

	if (fdnum < fdmax)
		fdmax = fdnum;

	for (i = 0, cmfptr = (int *) CMSG32_DATA(cm);
	     i < fdmax;
	     i++, cmfptr++) {
		int new_fd;
		err = get_unused_fd();
		if (err < 0)
			break;
		new_fd = err;
		err = put_user(new_fd, cmfptr);
		if (err) {
			put_unused_fd(new_fd);
			break;
		}
		/* Bump the usage count and install the file. */
		fp[i]->f_count++;
		current->files->fd[new_fd] = fp[i];
	}

	if (i > 0) {
		int cmlen = CMSG32_LEN(i * sizeof(int));
		if (!err)
			err = put_user(SOL_SOCKET, &cm->cmsg_level);
		if (!err)
			err = put_user(SCM_RIGHTS, &cm->cmsg_type);
		if (!err)
			err = put_user(cmlen, &cm->cmsg_len);
		if (!err) {
			cmlen = CMSG32_SPACE(i * sizeof(int));
			kmsg->msg_control += cmlen;
			kmsg->msg_controllen -= cmlen;
		}
	}
	if (i < fdnum)
		kmsg->msg_flags |= MSG_CTRUNC;

	/*
	 * All of the files that fit in the message have had their
	 * usage counts incremented, so we just free the list.
	 */
	__scm_destroy(scm);
}

/* In these cases we (currently) can just copy to data over verbatim
 * because all CMSGs created by the kernel have well defined types which
 * have the same layout in both the 32-bit and 64-bit API.  One must add
 * some special cased conversions here if we start sending control messages
 * with incompatible types.
 *
 * SCM_RIGHTS and SCM_CREDENTIALS are done by hand in recvmsg32 right after
 * we do our work.  The remaining cases are:
 *
 * SOL_IP	IP_PKTINFO	struct in_pktinfo	32-bit clean
 *		IP_TTL		int			32-bit clean
 *		IP_TOS		__u8			32-bit clean
 *		IP_RECVOPTS	variable length		32-bit clean
 *		IP_RETOPTS	variable length		32-bit clean
 *		(these last two are clean because the types are defined
 *		 by the IPv4 protocol)
 *		IP_RECVERR	struct sock_extended_err +
 *				struct sockaddr_in	32-bit clean
 * SOL_IPV6	IPV6_RECVERR	struct sock_extended_err +
 *				struct sockaddr_in6	32-bit clean
 *		IPV6_PKTINFO	struct in6_pktinfo	32-bit clean
 *		IPV6_HOPLIMIT	int			32-bit clean
 *		IPV6_FLOWINFO	u32			32-bit clean
 *		IPV6_HOPOPTS	ipv6 hop exthdr		32-bit clean
 *		IPV6_DSTOPTS	ipv6 dst exthdr(s)	32-bit clean
 *		IPV6_RTHDR	ipv6 routing exthdr	32-bit clean
 *		IPV6_AUTHHDR	ipv6 auth exthdr	32-bit clean
 */
static void
cmsg32_recvmsg_fixup(struct msghdr *kmsg, unsigned long orig_cmsg_uptr)
{
	unsigned char *workbuf, *wp;
	unsigned long bufsz, space_avail;
	struct cmsghdr *ucmsg;

	bufsz = ((unsigned long)kmsg->msg_control) - orig_cmsg_uptr;
	space_avail = kmsg->msg_controllen + bufsz;
	wp = workbuf = kmalloc(bufsz, GFP_KERNEL);
	if(workbuf == NULL)
		goto fail;

	/* To make this more sane we assume the kernel sends back properly
	 * formatted control messages.  Because of how the kernel will truncate
	 * the cmsg_len for MSG_TRUNC cases, we need not check that case either.
	 */
	ucmsg = (struct cmsghdr *) orig_cmsg_uptr;
	while(((unsigned long)ucmsg) < ((unsigned long)kmsg->msg_control)) {
		struct cmsghdr32 *kcmsg32 = (struct cmsghdr32 *) wp;
		int clen64, clen32;

		/* UCMSG is the 64-bit format CMSG entry in user-space.
		 * KCMSG32 is within the kernel space temporary buffer
		 * we use to convert into a 32-bit style CMSG.
		 */
		__get_user(kcmsg32->cmsg_len, &ucmsg->cmsg_len);
		__get_user(kcmsg32->cmsg_level, &ucmsg->cmsg_level);
		__get_user(kcmsg32->cmsg_type, &ucmsg->cmsg_type);

		clen64 = kcmsg32->cmsg_len;
		copy_from_user(CMSG32_DATA(kcmsg32), CMSG_DATA(ucmsg),
			       clen64 - CMSG_ALIGN(sizeof(*ucmsg)));
		clen32 = ((clen64 - CMSG_ALIGN(sizeof(*ucmsg))) +
			  CMSG32_ALIGN(sizeof(struct cmsghdr32)));
		kcmsg32->cmsg_len = clen32;

		ucmsg = (struct cmsghdr *) (((char *)ucmsg) +
					    CMSG_ALIGN(clen64)); 
		wp = (((char *)kcmsg32) + CMSG32_ALIGN(clen32));
	}

	/* Copy back fixed up data, and adjust pointers. */
	bufsz = (wp - workbuf);
	copy_to_user((void *)orig_cmsg_uptr, workbuf, bufsz);

	kmsg->msg_control = (struct cmsghdr *)
		(((char *)orig_cmsg_uptr) + bufsz);
	kmsg->msg_controllen = space_avail - bufsz;

	kfree(workbuf);
	return;

fail:
	/* If we leave the 64-bit format CMSG chunks in there,
	 * the application could get confused and crash.  So to
	 * ensure greater recovery, we report no CMSGs.
	 */
	kmsg->msg_controllen += bufsz;
	kmsg->msg_control = (void *) orig_cmsg_uptr;
}

asmlinkage long
sys32_sendmsg(int fd, struct msghdr32 *user_msg, unsigned user_flags)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	struct iovec iov[UIO_FASTIOV];
	unsigned char ctl[sizeof(struct cmsghdr) + 20];
	unsigned char *ctl_buf = ctl;
	struct msghdr kern_msg;
	int err, total_len;

	if(msghdr_from_user32_to_kern(&kern_msg, user_msg))
		return -EFAULT;
	if(kern_msg.msg_iovlen > UIO_MAXIOV)
		return -EINVAL;
	err = verify_iovec32(&kern_msg, iov, address, VERIFY_READ);
	if (err < 0)
		goto out;
	total_len = err;

	if(kern_msg.msg_controllen) {
		err = cmsghdr_from_user32_to_kern(&kern_msg, ctl, sizeof(ctl));
		if(err)
			goto out_freeiov;
		ctl_buf = kern_msg.msg_control;
	}
	kern_msg.msg_flags = user_flags;

	sock = sockfd_lookup(fd, &err);
	if (sock != NULL) {
		if (sock->file->f_flags & O_NONBLOCK)
			kern_msg.msg_flags |= MSG_DONTWAIT;
		err = sock_sendmsg(sock, &kern_msg, total_len);
		sockfd_put(sock);
	}

	/* N.B. Use kfree here, as kern_msg.msg_controllen might change? */
	if(ctl_buf != ctl)
		kfree(ctl_buf);
out_freeiov:
	if(kern_msg.msg_iov != iov)
		kfree(kern_msg.msg_iov);
out:
	return err;
}

asmlinkage long
sys32_recvmsg(int fd, struct msghdr32 *user_msg, unsigned int user_flags)
{
	struct iovec iovstack[UIO_FASTIOV];
	struct msghdr kern_msg;
	char addr[MAX_SOCK_ADDR];
	struct socket *sock;
	struct iovec *iov = iovstack;
	struct sockaddr *uaddr;
	int *uaddr_len;
	unsigned long cmsg_ptr;
	int err, total_len, len = 0;

	if(msghdr_from_user32_to_kern(&kern_msg, user_msg))
		return -EFAULT;
	if(kern_msg.msg_iovlen > UIO_MAXIOV)
		return -EINVAL;

	uaddr = kern_msg.msg_name;
	uaddr_len = &user_msg->msg_namelen;
	err = verify_iovec32(&kern_msg, iov, addr, VERIFY_WRITE);
	if (err < 0)
		goto out;
	total_len = err;

	cmsg_ptr = (unsigned long) kern_msg.msg_control;
	kern_msg.msg_flags = 0;

	sock = sockfd_lookup(fd, &err);
	if (sock != NULL) {
		struct scm_cookie scm;

		if (sock->file->f_flags & O_NONBLOCK)
			user_flags |= MSG_DONTWAIT;
		memset(&scm, 0, sizeof(scm));
		lock_kernel();
		err = sock->ops->recvmsg(sock, &kern_msg, total_len,
					 user_flags, &scm);
		if(err >= 0) {
			len = err;
			if(!kern_msg.msg_control) {
				if(sock->passcred || scm.fp)
					kern_msg.msg_flags |= MSG_CTRUNC;
				if(scm.fp)
					__scm_destroy(&scm);
			} else {
				/* If recvmsg processing itself placed some
				 * control messages into user space, it's is
				 * using 64-bit CMSG processing, so we need
				 * to fix it up before we tack on more stuff.
				 */
				if((unsigned long) kern_msg.msg_control
				   != cmsg_ptr)
					cmsg32_recvmsg_fixup(&kern_msg,
							     cmsg_ptr);

				/* Wheee... */
				if(sock->passcred)
					put_cmsg32(&kern_msg,
						   SOL_SOCKET, SCM_CREDENTIALS,
						   sizeof(scm.creds),
						   &scm.creds);
				if(scm.fp != NULL)
					scm_detach_fds32(&kern_msg, &scm);
			}
		}
		unlock_kernel();
		sockfd_put(sock);
	}

	if(uaddr != NULL && err >= 0)
		err = move_addr_to_user(addr, kern_msg.msg_namelen, uaddr,
					uaddr_len);
	if(cmsg_ptr != 0 && err >= 0) {
		unsigned long ucmsg_ptr = ((unsigned long)kern_msg.msg_control);
		__kernel_size_t32 uclen = (__kernel_size_t32) (ucmsg_ptr
							       - cmsg_ptr);
		err |= __put_user(uclen, &user_msg->msg_controllen);
	}
	if(err >= 0)
		err = __put_user(kern_msg.msg_flags, &user_msg->msg_flags);
	if(kern_msg.msg_iov != iov)
		kfree(kern_msg.msg_iov);
out:
	if(err < 0)
		return err;
	return len;
}

extern void check_pending(int signum);

#ifdef CONFIG_MODULES

extern asmlinkage unsigned long sys_create_module(const char *name_user,
						  size_t size);

asmlinkage unsigned long
sys32_create_module(const char *name_user, __kernel_size_t32 size)
{
	return sys_create_module(name_user, (size_t)size);
}

extern asmlinkage long sys_init_module(const char *name_user,
				      struct module *mod_user); 

/* Hey, when you're trying to init module, take time and prepare us a nice 64bit
 * module structure, even if from 32bit modutils... Why to pollute kernel... :))
 */
asmlinkage long
sys32_init_module(const char *name_user, struct module *mod_user)
{
	return sys_init_module(name_user, mod_user);
}

extern asmlinkage long sys_delete_module(const char *name_user);

asmlinkage long
sys32_delete_module(const char *name_user)
{
	return sys_delete_module(name_user);
}

struct module_info32 {
	u32 addr;
	u32 size;
	u32 flags;
	s32 usecount;
};

/* Query various bits about modules.  */

static inline long
get_mod_name(const char *user_name, char **buf)
{
	unsigned long page;
	long retval;

	if ((unsigned long)user_name >= TASK_SIZE
	    && !segment_eq(get_fs (), KERNEL_DS))
		return -EFAULT;

	page = __get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	retval = strncpy_from_user((char *)page, user_name, PAGE_SIZE);
	if (retval > 0) {
		if (retval < PAGE_SIZE) {
			*buf = (char *)page;
			return retval;
		}
		retval = -ENAMETOOLONG;
	} else if (!retval)
		retval = -EINVAL;

	free_page(page);
	return retval;
}

static inline void
put_mod_name(char *buf)
{
	free_page((unsigned long)buf);
}

static __inline__ struct module *
find_module(const char *name)
{
	struct module *mod;

	for (mod = module_list; mod ; mod = mod->next) {
		if (mod->flags & MOD_DELETED)
			continue;
		if (!strcmp(mod->name, name))
			break;
	}

	return mod;
}

static int
qm_modules(char *buf, size_t bufsize, __kernel_size_t32 *ret)
{
	struct module *mod;
	size_t nmod, space, len;

	nmod = space = 0;

	for (mod = module_list; mod->next != NULL; mod = mod->next, ++nmod) {
		len = strlen(mod->name)+1;
		if (len > bufsize)
			goto calc_space_needed;
		if (copy_to_user(buf, mod->name, len))
			return -EFAULT;
		buf += len;
		bufsize -= len;
		space += len;
	}

	if (put_user(nmod, ret))
		return -EFAULT;
	else
		return 0;

calc_space_needed:
	space += len;
	while ((mod = mod->next)->next != NULL)
		space += strlen(mod->name)+1;

	if (put_user(space, ret))
		return -EFAULT;
	else
		return -ENOSPC;
}

static int
qm_deps(struct module *mod, char *buf, size_t bufsize, __kernel_size_t32 *ret)
{
	size_t i, space, len;

	if (mod->next == NULL)
		return -EINVAL;
	if ((mod->flags & (MOD_RUNNING | MOD_DELETED)) != MOD_RUNNING)
		if (put_user(0, ret))
			return -EFAULT;
		else
			return 0;

	space = 0;
	for (i = 0; i < mod->ndeps; ++i) {
		const char *dep_name = mod->deps[i].dep->name;

		len = strlen(dep_name)+1;
		if (len > bufsize)
			goto calc_space_needed;
		if (copy_to_user(buf, dep_name, len))
			return -EFAULT;
		buf += len;
		bufsize -= len;
		space += len;
	}

	if (put_user(i, ret))
		return -EFAULT;
	else
		return 0;

calc_space_needed:
	space += len;
	while (++i < mod->ndeps)
		space += strlen(mod->deps[i].dep->name)+1;

	if (put_user(space, ret))
		return -EFAULT;
	else
		return -ENOSPC;
}

static int
qm_refs(struct module *mod, char *buf, size_t bufsize, __kernel_size_t32 *ret)
{
	size_t nrefs, space, len;
	struct module_ref *ref;

	if (mod->next == NULL)
		return -EINVAL;
	if ((mod->flags & (MOD_RUNNING | MOD_DELETED)) != MOD_RUNNING)
		if (put_user(0, ret))
			return -EFAULT;
		else
			return 0;

	space = 0;
	for (nrefs = 0, ref = mod->refs; ref ; ++nrefs, ref = ref->next_ref) {
		const char *ref_name = ref->ref->name;

		len = strlen(ref_name)+1;
		if (len > bufsize)
			goto calc_space_needed;
		if (copy_to_user(buf, ref_name, len))
			return -EFAULT;
		buf += len;
		bufsize -= len;
		space += len;
	}

	if (put_user(nrefs, ret))
		return -EFAULT;
	else
		return 0;

calc_space_needed:
	space += len;
	while ((ref = ref->next_ref) != NULL)
		space += strlen(ref->ref->name)+1;

	if (put_user(space, ret))
		return -EFAULT;
	else
		return -ENOSPC;
}

static inline int
qm_symbols(struct module *mod, char *buf, size_t bufsize,
	   __kernel_size_t32 *ret)
{
	size_t i, space, len;
	struct module_symbol *s;
	char *strings;
	unsigned *vals;

	if ((mod->flags & (MOD_RUNNING | MOD_DELETED)) != MOD_RUNNING)
		if (put_user(0, ret))
			return -EFAULT;
		else
			return 0;

	space = mod->nsyms * 2*sizeof(u32);

	i = len = 0;
	s = mod->syms;

	if (space > bufsize)
		goto calc_space_needed;

	if (!access_ok(VERIFY_WRITE, buf, space))
		return -EFAULT;

	bufsize -= space;
	vals = (unsigned *)buf;
	strings = buf+space;

	for (; i < mod->nsyms ; ++i, ++s, vals += 2) {
		len = strlen(s->name)+1;
		if (len > bufsize)
			goto calc_space_needed;

		if (copy_to_user(strings, s->name, len)
		    || __put_user(s->value, vals+0)
		    || __put_user(space, vals+1))
			return -EFAULT;

		strings += len;
		bufsize -= len;
		space += len;
	}

	if (put_user(i, ret))
		return -EFAULT;
	else
		return 0;

calc_space_needed:
	for (; i < mod->nsyms; ++i, ++s)
		space += strlen(s->name)+1;

	if (put_user(space, ret))
		return -EFAULT;
	else
		return -ENOSPC;
}

static inline int
qm_info(struct module *mod, char *buf, size_t bufsize, __kernel_size_t32 *ret)
{
	int error = 0;

	if (mod->next == NULL)
		return -EINVAL;

	if (sizeof(struct module_info32) <= bufsize) {
		struct module_info32 info;
		info.addr = (unsigned long)mod;
		info.size = mod->size;
		info.flags = mod->flags;
		info.usecount =
			((mod_member_present(mod, can_unload)
			  && mod->can_unload)
			 ? -1 : atomic_read(&mod->uc.usecount));

		if (copy_to_user(buf, &info, sizeof(struct module_info32)))
			return -EFAULT;
	} else
		error = -ENOSPC;

	if (put_user(sizeof(struct module_info32), ret))
		return -EFAULT;

	return error;
}

asmlinkage long
sys32_query_module(char *name_user, int which, char *buf,
		   __kernel_size_t32 bufsize, u32 ret) 
{
	struct module *mod;
	int err;

	lock_kernel();
	if (name_user == 0) {
		/* This finds "kernel_module" which is not exported. */
		for(mod = module_list; mod->next != NULL; mod = mod->next)
			;
	} else {
		long namelen;
		char *name;

		if ((namelen = get_mod_name(name_user, &name)) < 0) {
			err = namelen;
			goto out;
		}
		err = -ENOENT;
		if (namelen == 0) {
			/* This finds "kernel_module" which is not exported. */
			for(mod = module_list;
			    mod->next != NULL;
			    mod = mod->next) ;
		} else if ((mod = find_module(name)) == NULL) {
			put_mod_name(name);
			goto out;
		}
		put_mod_name(name);
	}

	switch (which)
	{
	case 0:
		err = 0;
		break;
	case QM_MODULES:
		err = qm_modules(buf, bufsize, (__kernel_size_t32 *)AA(ret));
		break;
	case QM_DEPS:
		err = qm_deps(mod, buf, bufsize, (__kernel_size_t32 *)AA(ret));
		break;
	case QM_REFS:
		err = qm_refs(mod, buf, bufsize, (__kernel_size_t32 *)AA(ret));
		break;
	case QM_SYMBOLS:
		err = qm_symbols(mod, buf, bufsize,
				 (__kernel_size_t32 *)AA(ret));
		break;
	case QM_INFO:
		err = qm_info(mod, buf, bufsize, (__kernel_size_t32 *)AA(ret));
		break;
	default:
		err = -EINVAL;
		break;
	}
out:
	unlock_kernel();
	return err;
}

struct kernel_sym32 {
	u32 value;
	char name[60];
};
		 
extern asmlinkage long sys_get_kernel_syms(struct kernel_sym *table);

asmlinkage long
sys32_get_kernel_syms(struct kernel_sym32 *table)
{
	int len, i;
	struct kernel_sym *tbl;
	mm_segment_t old_fs;
	
	len = sys_get_kernel_syms(NULL);
	if (!table) return len;
	tbl = kmalloc (len * sizeof (struct kernel_sym), GFP_KERNEL);
	if (!tbl) return -ENOMEM;
	old_fs = get_fs();
	set_fs (KERNEL_DS);
	sys_get_kernel_syms(tbl);
	set_fs (old_fs);
	for (i = 0; i < len; i++, table += sizeof (struct kernel_sym32)) {
		if (put_user (tbl[i].value, &table->value) ||
		    copy_to_user (table->name, tbl[i].name, 60))
			break;
	}
	kfree (tbl);
	return i;
}

#else /* CONFIG_MODULES */

asmlinkage unsigned long
sys32_create_module(const char *name_user, size_t size)
{
	return -ENOSYS;
}

asmlinkage long
sys32_init_module(const char *name_user, struct module *mod_user)
{
	return -ENOSYS;
}

asmlinkage long
sys32_delete_module(const char *name_user)
{
	return -ENOSYS;
}

asmlinkage long
sys32_query_module(const char *name_user, int which, char *buf, size_t bufsize,
		 size_t *ret)
{
	/* Let the program know about the new interface.  Not that
	   it'll do them much good.  */
	if (which == 0)
		return 0;

	return -ENOSYS;
}

asmlinkage long
sys32_get_kernel_syms(struct kernel_sym *table)
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

struct nfsctl_arg32 {
	s32			ca32_version;	/* safeguard */
	union {
		struct nfsctl_svc32	u32_svc;
		struct nfsctl_client32	u32_client;
		struct nfsctl_export32	u32_export;
		struct nfsctl_uidmap32	u32_umap;
		struct nfsctl_fhparm32	u32_getfh;
		u32			u32_debug;
	} u;
#define ca32_svc	u.u32_svc
#define ca32_client	u.u32_client
#define ca32_export	u.u32_export
#define ca32_umap	u.u32_umap
#define ca32_getfh	u.u32_getfh
#define ca32_authd	u.u32_authd
#define ca32_debug	u.u32_debug
};

union nfsctl_res32 {
	struct knfs_fh		cr32_getfh;
	u32			cr32_debug;
};

static int
nfs_svc32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= __get_user(karg->ca_svc.svc_port, &arg32->ca32_svc.svc32_port);
	err |= __get_user(karg->ca_svc.svc_nthreads,
			  &arg32->ca32_svc.svc32_nthreads);
	return err;
}

static int
nfs_clnt32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_client.cl_ident[0],
			  &arg32->ca32_client.cl32_ident[0],
			  NFSCLNT_IDMAX);
	err |= __get_user(karg->ca_client.cl_naddr,
			  &arg32->ca32_client.cl32_naddr);
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

static int
nfs_exp32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
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
	return err;
}

static int
nfs_uud32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	u32 uaddr;
	int i;
	int err;

	memset(karg, 0, sizeof(*karg));
	if(__get_user(karg->ca_version, &arg32->ca32_version))
		return -EFAULT;
	karg->ca_umap.ug_ident = (char *)get_free_page(GFP_USER);
	if(!karg->ca_umap.ug_ident)
		return -ENOMEM;
	err = __get_user(uaddr, &arg32->ca32_umap.ug32_ident);
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
	karg->ca_umap.ug_udimap = kmalloc((sizeof(uid_t) *
					   karg->ca_umap.ug_uidlen),
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
	karg->ca_umap.ug_gdimap = kmalloc((sizeof(gid_t) *
					   karg->ca_umap.ug_uidlen),
					  GFP_USER);
	if(!karg->ca_umap.ug_gdimap)
		return -ENOMEM;
	for(i = 0; i < karg->ca_umap.ug_gidlen; i++)
		err |= __get_user(karg->ca_umap.ug_gdimap[i],
			      &(((__kernel_gid_t32 *)A(uaddr))[i]));

	return err;
}

static int
nfs_getfh32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
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

static int
nfs_getfh32_res_trans(union nfsctl_res *kres, union nfsctl_res32 *res32)
{
	int err;
	
	err = copy_to_user(&res32->cr32_getfh,
			&kres->cr_getfh,
			sizeof(res32->cr32_getfh));
	err |= __put_user(kres->cr_debug, &res32->cr32_debug);
	return err;
}

extern asmlinkage long sys_nfsservctl(int cmd, void *arg, void *resp);

int asmlinkage
sys32_nfsservctl(int cmd, struct nfsctl_arg32 *arg32, union nfsctl_res32 *res32)
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
		err = nfs_exp32_trans(karg, arg32);
		break;
	/* This one is unimplemented, be we're ready for it. */
	case NFSCTL_UGIDUPDATE:
		err = nfs_uud32_trans(karg, arg32);
		break;
	case NFSCTL_GETFH:
		err = nfs_getfh32_trans(karg, arg32);
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

	if(!err && cmd == NFSCTL_GETFH)
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

asmlinkage long sys_utimes(char *, struct timeval *);

asmlinkage long
sys32_utimes(char *filename, struct timeval32 *tvs)
{
	char *kfilename;
	struct timeval ktvs[2];
	mm_segment_t old_fs;
	int ret;

	kfilename = getname32(filename);
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

/* PCI config space poking. */
extern asmlinkage long sys_pciconfig_read(unsigned long bus,
					 unsigned long dfn,
					 unsigned long off,
					 unsigned long len,
					 unsigned char *buf);

extern asmlinkage long sys_pciconfig_write(unsigned long bus,
					  unsigned long dfn,
					  unsigned long off,
					  unsigned long len,
					  unsigned char *buf);

asmlinkage long
sys32_pciconfig_read(u32 bus, u32 dfn, u32 off, u32 len, u32 ubuf)
{
	return sys_pciconfig_read((unsigned long) bus,
				  (unsigned long) dfn,
				  (unsigned long) off,
				  (unsigned long) len,
				  (unsigned char *)AA(ubuf));
}

asmlinkage long
sys32_pciconfig_write(u32 bus, u32 dfn, u32 off, u32 len, u32 ubuf)
{
	return sys_pciconfig_write((unsigned long) bus,
				   (unsigned long) dfn,
				   (unsigned long) off,
				   (unsigned long) len,
				   (unsigned char *)AA(ubuf));
}

extern asmlinkage long sys_prctl(int option, unsigned long arg2,
				unsigned long arg3, unsigned long arg4,
				unsigned long arg5);

asmlinkage long
sys32_prctl(int option, u32 arg2, u32 arg3, u32 arg4, u32 arg5)
{
	return sys_prctl(option,
			 (unsigned long) arg2,
			 (unsigned long) arg3,
			 (unsigned long) arg4,
			 (unsigned long) arg5);
}


extern asmlinkage long sys_newuname(struct new_utsname * name);

asmlinkage long
sys32_newuname(struct new_utsname * name)
{
	int ret = sys_newuname(name);
	
	if (current->personality == PER_LINUX32 && !ret) {
		ret = copy_to_user(name->machine, "sparc\0\0", 8);
	}
	return ret;
}

extern asmlinkage ssize_t sys_pread(unsigned int fd, char * buf,
				    size_t count, loff_t pos);

extern asmlinkage ssize_t sys_pwrite(unsigned int fd, const char * buf,
				     size_t count, loff_t pos);

typedef __kernel_ssize_t32 ssize_t32;

asmlinkage ssize_t32
sys32_pread(unsigned int fd, char *ubuf, __kernel_size_t32 count,
	    u32 poshi, u32 poslo)
{
	return sys_pread(fd, ubuf, count,
			 ((loff_t)AA(poshi) << 32) | AA(poslo));
}

asmlinkage ssize_t32
sys32_pwrite(unsigned int fd, char *ubuf, __kernel_size_t32 count,
	     u32 poshi, u32 poslo)
{
	return sys_pwrite(fd, ubuf, count,
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
#endif	// NOTYET

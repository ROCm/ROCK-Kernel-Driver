/*
 * sys_ia32.c: Conversion between 32bit and 64bit native syscalls. Derived from sys_sparc32.c.
 *
 * Copyright (C) 2000		VA Linux Co
 * Copyright (C) 2000		Don Dugger <n0ano@valinux.com>
 * Copyright (C) 1999		Arun Sharma <arun.sharma@intel.com>
 * Copyright (C) 1997,1998	Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997		David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 2000-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/sysctl.h>
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
#include <linux/quota.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfsd/xdr.h>
#include <linux/nfsd/syscall.h>
#include <linux/poll.h>
#include <linux/eventpoll.h>
#include <linux/personality.h>
#include <linux/ptrace.h>
#include <linux/stat.h>
#include <linux/ipc.h>
#include <linux/compat.h>
#include <linux/vfs.h>

#include <asm/intrinsics.h>
#include <asm/semaphore.h>
#include <asm/types.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>

#include "ia32priv.h"

#include <net/scm.h>
#include <net/sock.h>

#define DEBUG	0

#if DEBUG
# define DBG(fmt...)	printk(KERN_DEBUG fmt)
#else
# define DBG(fmt...)
#endif

#define A(__x)		((unsigned long)(__x))
#define AA(__x)		((unsigned long)(__x))
#define ROUND_UP(x,a)	((__typeof__(x))(((unsigned long)(x) + ((a) - 1)) & ~((a) - 1)))
#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))

#define OFFSET4K(a)		((a) & 0xfff)
#define PAGE_START(addr)	((addr) & PAGE_MASK)
#define MINSIGSTKSZ_IA32	2048

#define high2lowuid(uid) ((uid) > 65535 ? 65534 : (uid))
#define high2lowgid(gid) ((gid) > 65535 ? 65534 : (gid))

extern unsigned long arch_get_unmapped_area (struct file *, unsigned long, unsigned long,
					     unsigned long, unsigned long);

/*
 * Anything that modifies or inspects ia32 user virtual memory must hold this semaphore
 * while doing so.
 */
/* XXX make per-mm: */
static DECLARE_MUTEX(ia32_mmap_sem);

asmlinkage long
sys32_execve (char *name, compat_uptr_t __user *argv, compat_uptr_t __user *envp, struct pt_regs *regs)
{
	long error;
	char *filename;
	unsigned long old_map_base, old_task_size, tssd;

	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		return error;

	old_map_base  = current->thread.map_base;
	old_task_size = current->thread.task_size;
	tssd = ia64_get_kr(IA64_KR_TSSD);

	/* we may be exec'ing a 64-bit process: reset map base, task-size, and io-base: */
	current->thread.map_base  = DEFAULT_MAP_BASE;
	current->thread.task_size = DEFAULT_TASK_SIZE;
	ia64_set_kr(IA64_KR_IO_BASE, current->thread.old_iob);
	ia64_set_kr(IA64_KR_TSSD, current->thread.old_k1);

	error = compat_do_execve(filename, argv, envp, regs);
	putname(filename);

	if (error < 0) {
		/* oops, execve failed, switch back to old values... */
		ia64_set_kr(IA64_KR_IO_BASE, IA32_IOBASE);
		ia64_set_kr(IA64_KR_TSSD, tssd);
		current->thread.map_base  = old_map_base;
		current->thread.task_size = old_task_size;
	}

	return error;
}

int cp_compat_stat(struct kstat *stat, struct compat_stat *ubuf)
{
	int err;

	if ((u64) stat->size > MAX_NON_LFS ||
	    !old_valid_dev(stat->dev) ||
	    !old_valid_dev(stat->rdev))
		return -EOVERFLOW;

	if (clear_user(ubuf, sizeof(*ubuf)))
		return -EFAULT;

	err  = __put_user(old_encode_dev(stat->dev), &ubuf->st_dev);
	err |= __put_user(stat->ino, &ubuf->st_ino);
	err |= __put_user(stat->mode, &ubuf->st_mode);
	err |= __put_user(stat->nlink, &ubuf->st_nlink);
	err |= __put_user(high2lowuid(stat->uid), &ubuf->st_uid);
	err |= __put_user(high2lowgid(stat->gid), &ubuf->st_gid);
	err |= __put_user(old_encode_dev(stat->rdev), &ubuf->st_rdev);
	err |= __put_user(stat->size, &ubuf->st_size);
	err |= __put_user(stat->atime.tv_sec, &ubuf->st_atime);
	err |= __put_user(stat->atime.tv_nsec, &ubuf->st_atime_nsec);
	err |= __put_user(stat->mtime.tv_sec, &ubuf->st_mtime);
	err |= __put_user(stat->mtime.tv_nsec, &ubuf->st_mtime_nsec);
	err |= __put_user(stat->ctime.tv_sec, &ubuf->st_ctime);
	err |= __put_user(stat->ctime.tv_nsec, &ubuf->st_ctime_nsec);
	err |= __put_user(stat->blksize, &ubuf->st_blksize);
	err |= __put_user(stat->blocks, &ubuf->st_blocks);
	return err;
}

#if PAGE_SHIFT > IA32_PAGE_SHIFT


static int
get_page_prot (struct vm_area_struct *vma, unsigned long addr)
{
	int prot = 0;

	if (!vma || vma->vm_start > addr)
		return 0;

	if (vma->vm_flags & VM_READ)
		prot |= PROT_READ;
	if (vma->vm_flags & VM_WRITE)
		prot |= PROT_WRITE;
	if (vma->vm_flags & VM_EXEC)
		prot |= PROT_EXEC;
	return prot;
}

/*
 * Map a subpage by creating an anonymous page that contains the union of the old page and
 * the subpage.
 */
static unsigned long
mmap_subpage (struct file *file, unsigned long start, unsigned long end, int prot, int flags,
	      loff_t off)
{
	void *page = NULL;
	struct inode *inode;
	unsigned long ret = 0;
	struct vm_area_struct *vma = find_vma(current->mm, start);
	int old_prot = get_page_prot(vma, start);

	DBG("mmap_subpage(file=%p,start=0x%lx,end=0x%lx,prot=%x,flags=%x,off=0x%llx)\n",
	    file, start, end, prot, flags, off);


	/* Optimize the case where the old mmap and the new mmap are both anonymous */
	if ((old_prot & PROT_WRITE) && (flags & MAP_ANONYMOUS) && !vma->vm_file) {
		if (clear_user((void *) start, end - start)) {
			ret = -EFAULT;
			goto out;
		}
		goto skip_mmap;
	}

	page = (void *) get_zeroed_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	if (old_prot)
		copy_from_user(page, (void *) PAGE_START(start), PAGE_SIZE);

	down_write(&current->mm->mmap_sem);
	{
		ret = do_mmap(0, PAGE_START(start), PAGE_SIZE, prot | PROT_WRITE,
			      flags | MAP_FIXED | MAP_ANONYMOUS, 0);
	}
	up_write(&current->mm->mmap_sem);

	if (IS_ERR((void *) ret))
		goto out;

	if (old_prot) {
		/* copy back the old page contents.  */
		if (offset_in_page(start))
			copy_to_user((void *) PAGE_START(start), page, offset_in_page(start));
		if (offset_in_page(end))
			copy_to_user((void *) end, page + offset_in_page(end),
				     PAGE_SIZE - offset_in_page(end));
	}

	if (!(flags & MAP_ANONYMOUS)) {
		/* read the file contents */
		inode = file->f_dentry->d_inode;
		if (!inode->i_fop || !file->f_op->read
		    || ((*file->f_op->read)(file, (char *) start, end - start, &off) < 0))
		{
			ret = -EINVAL;
			goto out;
		}
	}

 skip_mmap:
	if (!(prot & PROT_WRITE))
		ret = sys_mprotect(PAGE_START(start), PAGE_SIZE, prot | old_prot);
  out:
	if (page)
		free_page((unsigned long) page);
	return ret;
}

static unsigned long
emulate_mmap (struct file *file, unsigned long start, unsigned long len, int prot, int flags,
	      loff_t off)
{
	unsigned long tmp, end, pend, pstart, ret, is_congruent, fudge = 0;
	struct inode *inode;
	loff_t poff;

	end = start + len;
	pstart = PAGE_START(start);
	pend = PAGE_ALIGN(end);

	if (flags & MAP_FIXED) {
		if (start > pstart) {
			if (flags & MAP_SHARED)
				printk(KERN_INFO
				       "%s(%d): emulate_mmap() can't share head (addr=0x%lx)\n",
				       current->comm, current->pid, start);
			ret = mmap_subpage(file, start, min(PAGE_ALIGN(start), end), prot, flags,
					   off);
			if (IS_ERR((void *) ret))
				return ret;
			pstart += PAGE_SIZE;
			if (pstart >= pend)
				return start;	/* done */
		}
		if (end < pend) {
			if (flags & MAP_SHARED)
				printk(KERN_INFO
				       "%s(%d): emulate_mmap() can't share tail (end=0x%lx)\n",
				       current->comm, current->pid, end);
			ret = mmap_subpage(file, max(start, PAGE_START(end)), end, prot, flags,
					   (off + len) - offset_in_page(end));
			if (IS_ERR((void *) ret))
				return ret;
			pend -= PAGE_SIZE;
			if (pstart >= pend)
				return start;	/* done */
		}
	} else {
		/*
		 * If a start address was specified, use it if the entire rounded out area
		 * is available.
		 */
		if (start && !pstart)
			fudge = 1;	/* handle case of mapping to range (0,PAGE_SIZE) */
		tmp = arch_get_unmapped_area(file, pstart - fudge, pend - pstart, 0, flags);
		if (tmp != pstart) {
			pstart = tmp;
			start = pstart + offset_in_page(off);	/* make start congruent with off */
			end = start + len;
			pend = PAGE_ALIGN(end);
		}
	}

	poff = off + (pstart - start);	/* note: (pstart - start) may be negative */
	is_congruent = (flags & MAP_ANONYMOUS) || (offset_in_page(poff) == 0);

	if ((flags & MAP_SHARED) && !is_congruent)
		printk(KERN_INFO "%s(%d): emulate_mmap() can't share contents of incongruent mmap "
		       "(addr=0x%lx,off=0x%llx)\n", current->comm, current->pid, start, off);

	DBG("mmap_body: mapping [0x%lx-0x%lx) %s with poff 0x%llx\n", pstart, pend,
	    is_congruent ? "congruent" : "not congruent", poff);

	down_write(&current->mm->mmap_sem);
	{
		if (!(flags & MAP_ANONYMOUS) && is_congruent)
			ret = do_mmap(file, pstart, pend - pstart, prot, flags | MAP_FIXED, poff);
		else
			ret = do_mmap(0, pstart, pend - pstart,
				      prot | ((flags & MAP_ANONYMOUS) ? 0 : PROT_WRITE),
				      flags | MAP_FIXED | MAP_ANONYMOUS, 0);
	}
	up_write(&current->mm->mmap_sem);

	if (IS_ERR((void *) ret))
		return ret;

	if (!is_congruent) {
		/* read the file contents */
		inode = file->f_dentry->d_inode;
		if (!inode->i_fop || !file->f_op->read
		    || ((*file->f_op->read)(file, (char *) pstart, pend - pstart, &poff) < 0))
		{
			sys_munmap(pstart, pend - pstart);
			return -EINVAL;
		}
		if (!(prot & PROT_WRITE) && sys_mprotect(pstart, pend - pstart, prot) < 0)
			return -EINVAL;
	}
	return start;
}

#endif /* PAGE_SHIFT > IA32_PAGE_SHIFT */

static inline unsigned int
get_prot32 (unsigned int prot)
{
	if (prot & PROT_WRITE)
		/* on x86, PROT_WRITE implies PROT_READ which implies PROT_EEC */
		prot |= PROT_READ | PROT_WRITE | PROT_EXEC;
	else if (prot & (PROT_READ | PROT_EXEC))
		/* on x86, there is no distinction between PROT_READ and PROT_EXEC */
		prot |= (PROT_READ | PROT_EXEC);

	return prot;
}

unsigned long
ia32_do_mmap (struct file *file, unsigned long addr, unsigned long len, int prot, int flags,
	      loff_t offset)
{
	DBG("ia32_do_mmap(file=%p,addr=0x%lx,len=0x%lx,prot=%x,flags=%x,offset=0x%llx)\n",
	    file, addr, len, prot, flags, offset);

	if (file && (!file->f_op || !file->f_op->mmap))
		return -ENODEV;

	len = IA32_PAGE_ALIGN(len);
	if (len == 0)
		return addr;

	if (len > IA32_PAGE_OFFSET || addr > IA32_PAGE_OFFSET - len)
	{
		if (flags & MAP_FIXED)
			return -ENOMEM;
		else
		return -EINVAL;
	}

	if (OFFSET4K(offset))
		return -EINVAL;

	prot = get_prot32(prot);

#if PAGE_SHIFT > IA32_PAGE_SHIFT
	down(&ia32_mmap_sem);
	{
		addr = emulate_mmap(file, addr, len, prot, flags, offset);
	}
	up(&ia32_mmap_sem);
#else
	down_write(&current->mm->mmap_sem);
	{
		addr = do_mmap(file, addr, len, prot, flags, offset);
	}
	up_write(&current->mm->mmap_sem);
#endif
	DBG("ia32_do_mmap: returning 0x%lx\n", addr);
	return addr;
}

/*
 * Linux/i386 didn't use to be able to handle more than 4 system call parameters, so these
 * system calls used a memory block for parameter passing..
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
sys32_mmap (struct mmap_arg_struct *arg)
{
	struct mmap_arg_struct a;
	struct file *file = NULL;
	unsigned long addr;
	int flags;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;

	if (OFFSET4K(a.offset))
		return -EINVAL;

	flags = a.flags;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(a.fd);
		if (!file)
			return -EBADF;
	}

	addr = ia32_do_mmap(file, a.addr, a.len, a.prot, flags, a.offset);

	if (file)
		fput(file);
	return addr;
}

asmlinkage long
sys32_mmap2 (unsigned int addr, unsigned int len, unsigned int prot, unsigned int flags,
	     unsigned int fd, unsigned int pgoff)
{
	struct file *file = NULL;
	unsigned long retval;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			return -EBADF;
	}

	retval = ia32_do_mmap(file, addr, len, prot, flags,
			      (unsigned long) pgoff << IA32_PAGE_SHIFT);

	if (file)
		fput(file);
	return retval;
}

asmlinkage long
sys32_munmap (unsigned int start, unsigned int len)
{
	unsigned int end = start + len;
	long ret;

#if PAGE_SHIFT <= IA32_PAGE_SHIFT
	ret = sys_munmap(start, end - start);
#else
	if (start >= end)
		return -EINVAL;

	start = PAGE_ALIGN(start);
	end = PAGE_START(end);

	if (start >= end)
		return 0;

	down(&ia32_mmap_sem);
	{
		ret = sys_munmap(start, end - start);
	}
	up(&ia32_mmap_sem);
#endif
	return ret;
}

#if PAGE_SHIFT > IA32_PAGE_SHIFT

/*
 * When mprotect()ing a partial page, we set the permission to the union of the old
 * settings and the new settings.  In other words, it's only possible to make access to a
 * partial page less restrictive.
 */
static long
mprotect_subpage (unsigned long address, int new_prot)
{
	int old_prot;
	struct vm_area_struct *vma;

	if (new_prot == PROT_NONE)
		return 0;		/* optimize case where nothing changes... */
	vma = find_vma(current->mm, address);
	old_prot = get_page_prot(vma, address);
	return sys_mprotect(address, PAGE_SIZE, new_prot | old_prot);
}

#endif /* PAGE_SHIFT > IA32_PAGE_SHIFT */

asmlinkage long
sys32_mprotect (unsigned int start, unsigned int len, int prot)
{
	unsigned long end = start + len;
#if PAGE_SHIFT > IA32_PAGE_SHIFT
	long retval = 0;
#endif

	prot = get_prot32(prot);

#if PAGE_SHIFT <= IA32_PAGE_SHIFT
	return sys_mprotect(start, end - start, prot);
#else
	if (OFFSET4K(start))
		return -EINVAL;

	end = IA32_PAGE_ALIGN(end);
	if (end < start)
		return -EINVAL;

	down(&ia32_mmap_sem);
	{
		if (offset_in_page(start)) {
			/* start address is 4KB aligned but not page aligned. */
			retval = mprotect_subpage(PAGE_START(start), prot);
			if (retval < 0)
				goto out;

			start = PAGE_ALIGN(start);
			if (start >= end)
				goto out;	/* retval is already zero... */
		}

		if (offset_in_page(end)) {
			/* end address is 4KB aligned but not page aligned. */
			retval = mprotect_subpage(PAGE_START(end), prot);
			if (retval < 0)
				goto out;

			end = PAGE_START(end);
		}
		retval = sys_mprotect(start, end - start, prot);
	}
  out:
	up(&ia32_mmap_sem);
	return retval;
#endif
}

asmlinkage long
sys32_pipe (int *fd)
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

static inline long
get_tv32 (struct timeval *o, struct compat_timeval *i)
{
	return (!access_ok(VERIFY_READ, i, sizeof(*i)) ||
		(__get_user(o->tv_sec, &i->tv_sec) | __get_user(o->tv_usec, &i->tv_usec)));
}

static inline long
put_tv32 (struct compat_timeval *o, struct timeval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->tv_sec, &o->tv_sec) | __put_user(i->tv_usec, &o->tv_usec)));
}

asmlinkage unsigned long
sys32_alarm (unsigned int seconds)
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
sys32_gettimeofday (struct compat_timeval *tv, struct timezone *tz)
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
sys32_settimeofday (struct compat_timeval *tv, struct timezone *tz)
{
	struct timeval ktv;
	struct timespec kts;
	struct timezone ktz;

	if (tv) {
		if (get_tv32(&ktv, tv))
			return -EFAULT;
		kts.tv_sec = ktv.tv_sec;
		kts.tv_nsec = ktv.tv_usec * 1000;
	}
	if (tz) {
		if (copy_from_user(&ktz, tz, sizeof(ktz)))
			return -EFAULT;
	}

	return do_sys_settimeofday(tv ? &kts : NULL, tz ? &ktz : NULL);
}

struct getdents32_callback {
	struct compat_dirent * current_dir;
	struct compat_dirent * previous;
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
	struct compat_dirent * dirent;
	struct getdents32_callback * buf = (struct getdents32_callback *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1, 4);

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	buf->error = -EFAULT;	/* only used if we fail.. */
	dirent = buf->previous;
	if (dirent)
		if (put_user(offset, &dirent->d_off))
			return -EFAULT;
	dirent = buf->current_dir;
	buf->previous = dirent;
	if (put_user(ino, &dirent->d_ino)
	    || put_user(reclen, &dirent->d_reclen)
	    || copy_to_user(dirent->d_name, name, namlen)
	    || put_user(0, dirent->d_name + namlen))
		return -EFAULT;
	dirent = (struct compat_dirent *) ((char *) dirent + reclen);
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
}

asmlinkage long
sys32_getdents (unsigned int fd, struct compat_dirent *dirent, unsigned int count)
{
	struct file * file;
	struct compat_dirent * lastdirent;
	struct getdents32_callback buf;
	int error;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = vfs_readdir(file, filldir32, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.error;
	lastdirent = buf.previous;
	if (lastdirent) {
		error = -EINVAL;
		if (put_user(file->f_pos, &lastdirent->d_off))
			goto out_putf;
		error = count - buf.count;
	}

out_putf:
	fput(file);
out:
	return error;
}

static int
fillonedir32 (void * __buf, const char * name, int namlen, loff_t offset, ino_t ino,
	      unsigned int d_type)
{
	struct readdir32_callback * buf = (struct readdir32_callback *) __buf;
	struct old_linux32_dirent * dirent;

	if (buf->count)
		return -EINVAL;
	buf->count++;
	dirent = buf->dirent;
	if (put_user(ino, &dirent->d_ino)
	    || put_user(offset, &dirent->d_offset)
	    || put_user(namlen, &dirent->d_namlen)
	    || copy_to_user(dirent->d_name, name, namlen)
	    || put_user(0, dirent->d_name + namlen))
		return -EFAULT;
	return 0;
}

asmlinkage long
sys32_readdir (unsigned int fd, void *dirent, unsigned int count)
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
sys32_old_select (struct sel_arg_struct *arg)
{
	struct sel_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	return compat_sys_select(a.n, compat_ptr(a.inp), compat_ptr(a.outp),
				 compat_ptr(a.exp), compat_ptr(a.tvp));
}

#define SEMOP		 1
#define SEMGET		 2
#define SEMCTL		 3
#define SEMTIMEDOP	 4
#define MSGSND		11
#define MSGRCV		12
#define MSGGET		13
#define MSGCTL		14
#define SHMAT		21
#define SHMDT		22
#define SHMGET		23
#define SHMCTL		24

asmlinkage long
sys32_ipc(u32 call, int first, int second, int third, u32 ptr, u32 fifth)
{
	int version;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {
	      case SEMTIMEDOP:
		if (fifth)
			return compat_sys_semtimedop(first, compat_ptr(ptr),
				second, compat_ptr(fifth));
		/* else fall through for normal semop() */
	      case SEMOP:
		/* struct sembuf is the same on 32 and 64bit :)) */
		return sys_semtimedop(first, compat_ptr(ptr), second,
				      NULL);
	      case SEMGET:
		return sys_semget(first, second, third);
	      case SEMCTL:
		return compat_sys_semctl(first, second, third, compat_ptr(ptr));

	      case MSGSND:
		return compat_sys_msgsnd(first, second, third, compat_ptr(ptr));
	      case MSGRCV:
		return compat_sys_msgrcv(first, second, fifth, third, version, compat_ptr(ptr));
	      case MSGGET:
		return sys_msgget((key_t) first, second);
	      case MSGCTL:
		return compat_sys_msgctl(first, second, compat_ptr(ptr));

	      case SHMAT:
		return compat_sys_shmat(first, second, third, version, compat_ptr(ptr));
		break;
	      case SHMDT:
		return sys_shmdt(compat_ptr(ptr));
	      case SHMGET:
		return sys_shmget(first, second, third);
	      case SHMCTL:
		return compat_sys_shmctl(first, second, compat_ptr(ptr));

	      default:
		return -ENOSYS;
	}
	return -EINVAL;
}

/*
 * sys_time() can be implemented in user-level using
 * sys_gettimeofday().  IA64 did this but i386 Linux did not
 * so we have to implement this system call here.
 */
asmlinkage long
sys32_time (int *tloc)
{
	int i;
	struct timeval tv;

	do_gettimeofday(&tv);
	i = tv.tv_sec;

	if (tloc) {
		if (put_user(i, tloc))
			i = -EFAULT;
	}
	return i;
}

asmlinkage long
compat_sys_wait4 (compat_pid_t pid, compat_uint_t * stat_addr, int options,
		 struct compat_rusage *ru);

asmlinkage long
sys32_waitpid (int pid, unsigned int *stat_addr, int options)
{
	return compat_sys_wait4(pid, stat_addr, options, NULL);
}

static unsigned int
ia32_peek (struct pt_regs *regs, struct task_struct *child, unsigned long addr, unsigned int *val)
{
	size_t copied;
	unsigned int ret;

	copied = access_process_vm(child, addr, val, sizeof(*val), 0);
	return (copied != sizeof(ret)) ? -EIO : 0;
}

static unsigned int
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
#define PT_CS	13
#define PT_EFL	14
#define PT_UESP	15
#define PT_SS	16

static unsigned int
getreg (struct task_struct *child, int regno)
{
	struct pt_regs *child_regs;

	child_regs = ia64_task_regs(child);
	switch (regno / sizeof(int)) {
	      case PT_EBX: return child_regs->r11;
	      case PT_ECX: return child_regs->r9;
	      case PT_EDX: return child_regs->r10;
	      case PT_ESI: return child_regs->r14;
	      case PT_EDI: return child_regs->r15;
	      case PT_EBP: return child_regs->r13;
	      case PT_EAX: return child_regs->r8;
	      case PT_ORIG_EAX: return child_regs->r1; /* see dispatch_to_ia32_handler() */
	      case PT_EIP: return child_regs->cr_iip;
	      case PT_UESP: return child_regs->r12;
	      case PT_EFL: return child->thread.eflag;
	      case PT_DS: case PT_ES: case PT_FS: case PT_GS: case PT_SS:
		return __USER_DS;
	      case PT_CS: return __USER_CS;
	      default:
		printk(KERN_ERR "ia32.getreg(): unknown register %d\n", regno);
		break;
	}
	return 0;
}

static void
putreg (struct task_struct *child, int regno, unsigned int value)
{
	struct pt_regs *child_regs;

	child_regs = ia64_task_regs(child);
	switch (regno / sizeof(int)) {
	      case PT_EBX: child_regs->r11 = value; break;
	      case PT_ECX: child_regs->r9 = value; break;
	      case PT_EDX: child_regs->r10 = value; break;
	      case PT_ESI: child_regs->r14 = value; break;
	      case PT_EDI: child_regs->r15 = value; break;
	      case PT_EBP: child_regs->r13 = value; break;
	      case PT_EAX: child_regs->r8 = value; break;
	      case PT_ORIG_EAX: child_regs->r1 = value; break;
	      case PT_EIP: child_regs->cr_iip = value; break;
	      case PT_UESP: child_regs->r12 = value; break;
	      case PT_EFL: child->thread.eflag = value; break;
	      case PT_DS: case PT_ES: case PT_FS: case PT_GS: case PT_SS:
		if (value != __USER_DS)
			printk(KERN_ERR
			       "ia32.putreg: attempt to set invalid segment register %d = %x\n",
			       regno, value);
		break;
	      case PT_CS:
		if (value != __USER_CS)
			printk(KERN_ERR
			       "ia32.putreg: attempt to to set invalid segment register %d = %x\n",
			       regno, value);
		break;
	      default:
		printk(KERN_ERR "ia32.putreg: unknown register %d\n", regno);
		break;
	}
}

static void
put_fpreg (int regno, struct _fpreg_ia32 *reg, struct pt_regs *ptp, struct switch_stack *swp,
	   int tos)
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
		ia64f2ia32f(f, &ptp->f10);
		break;
	      case 3:
		ia64f2ia32f(f, &ptp->f11);
		break;
	      case 4:
	      case 5:
	      case 6:
	      case 7:
		ia64f2ia32f(f, &swp->f12 + (regno - 4));
		break;
	}
	copy_to_user(reg, f, sizeof(*reg));
}

static void
get_fpreg (int regno, struct _fpreg_ia32 *reg, struct pt_regs *ptp, struct switch_stack *swp,
	   int tos)
{

	if ((regno += tos) >= 8)
		regno -= 8;
	switch (regno) {
	      case 0:
		copy_from_user(&ptp->f8, reg, sizeof(*reg));
		break;
	      case 1:
		copy_from_user(&ptp->f9, reg, sizeof(*reg));
		break;
	      case 2:
		copy_from_user(&ptp->f10, reg, sizeof(*reg));
		break;
	      case 3:
		copy_from_user(&ptp->f11, reg, sizeof(*reg));
		break;
	      case 4:
	      case 5:
	      case 6:
	      case 7:
		copy_from_user(&swp->f12 + (regno - 4), reg, sizeof(*reg));
		break;
	}
	return;
}

int
save_ia32_fpstate (struct task_struct *tsk, struct ia32_user_i387_struct *save)
{
	struct switch_stack *swp;
	struct pt_regs *ptp;
	int i, tos;

	if (!access_ok(VERIFY_WRITE, save, sizeof(*save)))
		return -EFAULT;

	__put_user(tsk->thread.fcr & 0xffff, &save->cwd);
	__put_user(tsk->thread.fsr & 0xffff, &save->swd);
	__put_user((tsk->thread.fsr>>16) & 0xffff, &save->twd);
	__put_user(tsk->thread.fir, &save->fip);
	__put_user((tsk->thread.fir>>32) & 0xffff, &save->fcs);
	__put_user(tsk->thread.fdr, &save->foo);
	__put_user((tsk->thread.fdr>>32) & 0xffff, &save->fos);

	/*
	 *  Stack frames start with 16-bytes of temp space
	 */
	swp = (struct switch_stack *)(tsk->thread.ksp + 16);
	ptp = ia64_task_regs(tsk);
	tos = (tsk->thread.fsr >> 11) & 7;
	for (i = 0; i < 8; i++)
		put_fpreg(i, &save->st_space[i], ptp, swp, tos);
	return 0;
}

static int
restore_ia32_fpstate (struct task_struct *tsk, struct ia32_user_i387_struct *save)
{
	struct switch_stack *swp;
	struct pt_regs *ptp;
	int i, tos;
	unsigned int fsrlo, fsrhi, num32;

	if (!access_ok(VERIFY_READ, save, sizeof(*save)))
		return(-EFAULT);

	__get_user(num32, (unsigned int *)&save->cwd);
	tsk->thread.fcr = (tsk->thread.fcr & (~0x1f3f)) | (num32 & 0x1f3f);
	__get_user(fsrlo, (unsigned int *)&save->swd);
	__get_user(fsrhi, (unsigned int *)&save->twd);
	num32 = (fsrhi << 16) | fsrlo;
	tsk->thread.fsr = (tsk->thread.fsr & (~0xffffffff)) | num32;
	__get_user(num32, (unsigned int *)&save->fip);
	tsk->thread.fir = (tsk->thread.fir & (~0xffffffff)) | num32;
	__get_user(num32, (unsigned int *)&save->foo);
	tsk->thread.fdr = (tsk->thread.fdr & (~0xffffffff)) | num32;

	/*
	 *  Stack frames start with 16-bytes of temp space
	 */
	swp = (struct switch_stack *)(tsk->thread.ksp + 16);
	ptp = ia64_task_regs(tsk);
	tos = (tsk->thread.fsr >> 11) & 7;
	for (i = 0; i < 8; i++)
		get_fpreg(i, &save->st_space[i], ptp, swp, tos);
	return 0;
}

int
save_ia32_fpxstate (struct task_struct *tsk, struct ia32_user_fxsr_struct *save)
{
	struct switch_stack *swp;
	struct pt_regs *ptp;
	int i, tos;
	unsigned long mxcsr=0;
	unsigned long num128[2];

	if (!access_ok(VERIFY_WRITE, save, sizeof(*save)))
		return -EFAULT;

	__put_user(tsk->thread.fcr & 0xffff, &save->cwd);
	__put_user(tsk->thread.fsr & 0xffff, &save->swd);
	__put_user((tsk->thread.fsr>>16) & 0xffff, &save->twd);
	__put_user(tsk->thread.fir, &save->fip);
	__put_user((tsk->thread.fir>>32) & 0xffff, &save->fcs);
	__put_user(tsk->thread.fdr, &save->foo);
	__put_user((tsk->thread.fdr>>32) & 0xffff, &save->fos);

        /*
         *  Stack frames start with 16-bytes of temp space
         */
        swp = (struct switch_stack *)(tsk->thread.ksp + 16);
        ptp = ia64_task_regs(tsk);
	tos = (tsk->thread.fsr >> 11) & 7;
        for (i = 0; i < 8; i++)
		put_fpreg(i, (struct _fpreg_ia32 *)&save->st_space[4*i], ptp, swp, tos);

	mxcsr = ((tsk->thread.fcr>>32) & 0xff80) | ((tsk->thread.fsr>>32) & 0x3f);
	__put_user(mxcsr & 0xffff, &save->mxcsr);
	for (i = 0; i < 8; i++) {
		memcpy(&(num128[0]), &(swp->f16) + i*2, sizeof(unsigned long));
		memcpy(&(num128[1]), &(swp->f17) + i*2, sizeof(unsigned long));
		copy_to_user(&save->xmm_space[0] + 4*i, num128, sizeof(struct _xmmreg_ia32));
	}
	return 0;
}

static int
restore_ia32_fpxstate (struct task_struct *tsk, struct ia32_user_fxsr_struct *save)
{
	struct switch_stack *swp;
	struct pt_regs *ptp;
	int i, tos;
	unsigned int fsrlo, fsrhi, num32;
	int mxcsr;
	unsigned long num64;
	unsigned long num128[2];

	if (!access_ok(VERIFY_READ, save, sizeof(*save)))
		return(-EFAULT);

	__get_user(num32, (unsigned int *)&save->cwd);
	tsk->thread.fcr = (tsk->thread.fcr & (~0x1f3f)) | (num32 & 0x1f3f);
	__get_user(fsrlo, (unsigned int *)&save->swd);
	__get_user(fsrhi, (unsigned int *)&save->twd);
	num32 = (fsrhi << 16) | fsrlo;
	tsk->thread.fsr = (tsk->thread.fsr & (~0xffffffff)) | num32;
	__get_user(num32, (unsigned int *)&save->fip);
	tsk->thread.fir = (tsk->thread.fir & (~0xffffffff)) | num32;
	__get_user(num32, (unsigned int *)&save->foo);
	tsk->thread.fdr = (tsk->thread.fdr & (~0xffffffff)) | num32;

	/*
	 *  Stack frames start with 16-bytes of temp space
	 */
	swp = (struct switch_stack *)(tsk->thread.ksp + 16);
	ptp = ia64_task_regs(tsk);
	tos = (tsk->thread.fsr >> 11) & 7;
	for (i = 0; i < 8; i++)
	get_fpreg(i, (struct _fpreg_ia32 *)&save->st_space[4*i], ptp, swp, tos);

	__get_user(mxcsr, (unsigned int *)&save->mxcsr);
	num64 = mxcsr & 0xff10;
	tsk->thread.fcr = (tsk->thread.fcr & (~0xff1000000000)) | (num64<<32);
	num64 = mxcsr & 0x3f;
	tsk->thread.fsr = (tsk->thread.fsr & (~0x3f00000000)) | (num64<<32);

	for (i = 0; i < 8; i++) {
		copy_from_user(num128, &save->xmm_space[0] + 4*i, sizeof(struct _xmmreg_ia32));
		memcpy(&(swp->f16) + i*2, &(num128[0]), sizeof(unsigned long));
		memcpy(&(swp->f17) + i*2, &(num128[1]), sizeof(unsigned long));
	}
	return 0;
}

/*
 *  Note that the IA32 version of `ptrace' calls the IA64 routine for
 *    many of the requests.  This will only work for requests that do
 *    not need access to the calling processes `pt_regs' which is located
 *    at the address of `stack'.  Once we call the IA64 `sys_ptrace' then
 *    the address of `stack' will not be the address of the `pt_regs'.
 */
asmlinkage long
sys32_ptrace (int request, pid_t pid, unsigned int addr, unsigned int data,
	      long arg4, long arg5, long arg6, long arg7, long stack)
{
	struct pt_regs *regs = (struct pt_regs *) &stack;
	struct task_struct *child;
	unsigned int value, tmp;
	long i, ret;

	lock_kernel();
	if (request == PTRACE_TRACEME) {
		ret = sys_ptrace(request, pid, addr, data, arg4, arg5, arg6, arg7, stack);
		goto out;
	}

	ret = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	if (child)
		get_task_struct(child);
	read_unlock(&tasklist_lock);
	if (!child)
		goto out;
	ret = -EPERM;
	if (pid == 1)		/* no messing around with init! */
		goto out_tsk;

	if (request == PTRACE_ATTACH) {
		ret = sys_ptrace(request, pid, addr, data, arg4, arg5, arg6, arg7, stack);
		goto out_tsk;
	}

	ret = ptrace_check_attach(child, request == PTRACE_KILL);
	if (ret < 0)
		goto out_tsk;

	switch (request) {
	      case PTRACE_PEEKTEXT:
	      case PTRACE_PEEKDATA:	/* read word at location addr */
		ret = ia32_peek(regs, child, addr, &value);
		if (ret == 0)
			ret = put_user(value, (unsigned int *) A(data));
		else
			ret = -EIO;
		goto out_tsk;

	      case PTRACE_POKETEXT:
	      case PTRACE_POKEDATA:	/* write the word at location addr */
		ret = ia32_poke(regs, child, addr, data);
		goto out_tsk;

	      case PTRACE_PEEKUSR:	/* read word at addr in USER area */
		ret = -EIO;
		if ((addr & 3) || addr > 17*sizeof(int))
			break;

		tmp = getreg(child, addr);
		if (!put_user(tmp, (unsigned int *) A(data)))
			ret = 0;
		break;

	      case PTRACE_POKEUSR:	/* write word at addr in USER area */
		ret = -EIO;
		if ((addr & 3) || addr > 17*sizeof(int))
			break;

		putreg(child, addr, data);
		ret = 0;
		break;

	      case IA32_PTRACE_GETREGS:
		if (!access_ok(VERIFY_WRITE, (int *) A(data), 17*sizeof(int))) {
			ret = -EIO;
			break;
		}
		for (i = 0; i < (int) (17*sizeof(int)); i += sizeof(int) ) {
			put_user(getreg(child, i), (unsigned int *) A(data));
			data += sizeof(int);
		}
		ret = 0;
		break;

	      case IA32_PTRACE_SETREGS:
		if (!access_ok(VERIFY_READ, (int *) A(data), 17*sizeof(int))) {
			ret = -EIO;
			break;
		}
		for (i = 0; i < (int) (17*sizeof(int)); i += sizeof(int) ) {
			get_user(tmp, (unsigned int *) A(data));
			putreg(child, i, tmp);
			data += sizeof(int);
		}
		ret = 0;
		break;

	      case IA32_PTRACE_GETFPREGS:
		ret = save_ia32_fpstate(child, (struct ia32_user_i387_struct *) A(data));
		break;

	      case IA32_PTRACE_GETFPXREGS:
		ret = save_ia32_fpxstate(child, (struct ia32_user_fxsr_struct *) A(data));
		break;

	      case IA32_PTRACE_SETFPREGS:
		ret = restore_ia32_fpstate(child, (struct ia32_user_i387_struct *) A(data));
		break;

	      case IA32_PTRACE_SETFPXREGS:
		ret = restore_ia32_fpxstate(child, (struct ia32_user_fxsr_struct *) A(data));
		break;

	      case PTRACE_SYSCALL:	/* continue, stop after next syscall */
	      case PTRACE_CONT:		/* restart after signal. */
	      case PTRACE_KILL:
	      case PTRACE_SINGLESTEP:	/* execute chile for one instruction */
	      case PTRACE_DETACH:	/* detach a process */
		ret = sys_ptrace(request, pid, addr, data, arg4, arg5, arg6, arg7, stack);
		break;

	      default:
		ret = ptrace_request(child, request, addr, data);
		break;

	}
  out_tsk:
	put_task_struct(child);
  out:
	unlock_kernel();
	return ret;
}

/*
 *  The IA64 maps 4 I/O ports for each 4K page
 */
#define IOLEN	((65536 / 4) * 4096)

asmlinkage long
sys32_iopl (int level)
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
	old = ia64_getreg(_IA64_REG_AR_EFLAG);
	if ((unsigned int) level > ((old >> 12) & 3)) {
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

	down_write(&current->mm->mmap_sem);
	addr = do_mmap_pgoff(file, IA32_IOBASE,
			     IOLEN, PROT_READ|PROT_WRITE, MAP_SHARED,
			     (ia64_iobase & ~PAGE_OFFSET) >> PAGE_SHIFT);
	up_write(&current->mm->mmap_sem);

	if (addr >= 0) {
		old = (old & ~0x3000) | (level << 12);
		ia64_setreg(_IA64_REG_AR_EFLAG, old);
	}

	fput(file);
	sys_close(fd);
	return 0;
}

asmlinkage long
sys32_ioperm (unsigned int from, unsigned int num, int on)
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
	return sys32_iopl(3);
}

typedef struct {
	unsigned int	ss_sp;
	unsigned int	ss_flags;
	unsigned int	ss_size;
} ia32_stack_t;

asmlinkage long
sys32_sigaltstack (ia32_stack_t *uss32, ia32_stack_t *uoss32,
		   long arg2, long arg3, long arg4, long arg5, long arg6, long arg7, long stack)
{
	struct pt_regs *pt = (struct pt_regs *) &stack;
	stack_t uss, uoss;
	ia32_stack_t buf32;
	int ret;
	mm_segment_t old_fs = get_fs();

	if (uss32)
		if (copy_from_user(&buf32, uss32, sizeof(ia32_stack_t)))
			return -EFAULT;
	uss.ss_sp = (void *) (long) buf32.ss_sp;
	uss.ss_flags = buf32.ss_flags;
	/* MINSIGSTKSZ is different for ia32 vs ia64. We lie here to pass the 
           check and set it to the user requested value later */
	if ((buf32.ss_flags != SS_DISABLE) && (buf32.ss_size < MINSIGSTKSZ_IA32)) {
		ret = -ENOMEM;
		goto out;
	}
	uss.ss_size = MINSIGSTKSZ;
	set_fs(KERNEL_DS);
	ret = do_sigaltstack(uss32 ? &uss : NULL, &uoss, pt->r12);
 	current->sas_ss_size = buf32.ss_size;	
	set_fs(old_fs);
out:
	if (ret < 0)
		return(ret);
	if (uoss32) {
		buf32.ss_sp = (long) uoss.ss_sp;
		buf32.ss_flags = uoss.ss_flags;
		buf32.ss_size = uoss.ss_size;
		if (copy_to_user(uoss32, &buf32, sizeof(ia32_stack_t)))
			return -EFAULT;
	}
	return ret;
}

asmlinkage int
sys32_pause (void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

asmlinkage int
sys32_msync (unsigned int start, unsigned int len, int flags)
{
	unsigned int addr;

	if (OFFSET4K(start))
		return -EINVAL;
	addr = PAGE_START(start);
	return sys_msync(addr, len + (start - addr), flags);
}

struct sysctl32 {
	unsigned int	name;
	int		nlen;
	unsigned int	oldval;
	unsigned int	oldlenp;
	unsigned int	newval;
	unsigned int	newlen;
	unsigned int	__unused[4];
};

asmlinkage long
sys32_sysctl (struct sysctl32 *args)
{
#ifdef CONFIG_SYSCTL
	struct sysctl32 a32;
	mm_segment_t old_fs = get_fs ();
	void *oldvalp, *newvalp;
	size_t oldlen;
	int *namep;
	long ret;

	if (copy_from_user(&a32, args, sizeof(a32)))
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
#else
	return -ENOSYS;
#endif
}

asmlinkage long
sys32_newuname (struct new_utsname *name)
{
	int ret = sys_newuname(name);

	if (!ret)
		if (copy_to_user(name->machine, "i686\0\0\0", 8))
			ret = -EFAULT;
	return ret;
}

asmlinkage long
sys32_getresuid16 (u16 *ruid, u16 *euid, u16 *suid)
{
	uid_t a, b, c;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_getresuid(&a, &b, &c);
	set_fs(old_fs);

	if (put_user(a, ruid) || put_user(b, euid) || put_user(c, suid))
		return -EFAULT;
	return ret;
}

asmlinkage long
sys32_getresgid16 (u16 *rgid, u16 *egid, u16 *sgid)
{
	gid_t a, b, c;
	int ret;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_getresgid(&a, &b, &c);
	set_fs(old_fs);

	if (ret)
		return ret;

	return put_user(a, rgid) | put_user(b, egid) | put_user(c, sgid);
}

asmlinkage long
sys32_lseek (unsigned int fd, int offset, unsigned int whence)
{
	/* Sign-extension of "offset" is important here... */
	return sys_lseek(fd, offset, whence);
}

static int
groups16_to_user(short *grouplist, struct group_info *group_info)
{
	int i;
	short group;

	for (i = 0; i < group_info->ngroups; i++) {
		group = (short)GROUP_AT(group_info, i);
		if (put_user(group, grouplist+i))
			return -EFAULT;
	}

	return 0;
}

static int
groups16_from_user(struct group_info *group_info, short *grouplist)
{
	int i;
	short group;

	for (i = 0; i < group_info->ngroups; i++) {
		if (get_user(group, grouplist+i))
			return  -EFAULT;
		GROUP_AT(group_info, i) = (gid_t)group;
	}

	return 0;
}

asmlinkage long
sys32_getgroups16 (int gidsetsize, short *grouplist)
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

asmlinkage long
sys32_setgroups16 (int gidsetsize, short *grouplist)
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

asmlinkage long
sys32_truncate64 (unsigned int path, unsigned int len_lo, unsigned int len_hi)
{
	return sys_truncate((const char *) A(path), ((unsigned long) len_hi << 32) | len_lo);
}

asmlinkage long
sys32_ftruncate64 (int fd, unsigned int len_lo, unsigned int len_hi)
{
	return sys_ftruncate(fd, ((unsigned long) len_hi << 32) | len_lo);
}

static int
putstat64 (struct stat64 *ubuf, struct kstat *kbuf)
{
	int err;
	u64 hdev;

	if (clear_user(ubuf, sizeof(*ubuf)))
		return -EFAULT;

	hdev = huge_encode_dev(kbuf->dev);
	err  = __put_user(hdev, (u32*)&ubuf->st_dev);
	err |= __put_user(hdev >> 32, ((u32*)&ubuf->st_dev) + 1);
	err |= __put_user(kbuf->ino, &ubuf->__st_ino);
	err |= __put_user(kbuf->ino, &ubuf->st_ino_lo);
	err |= __put_user(kbuf->ino >> 32, &ubuf->st_ino_hi);
	err |= __put_user(kbuf->mode, &ubuf->st_mode);
	err |= __put_user(kbuf->nlink, &ubuf->st_nlink);
	err |= __put_user(kbuf->uid, &ubuf->st_uid);
	err |= __put_user(kbuf->gid, &ubuf->st_gid);
	hdev = huge_encode_dev(kbuf->rdev);
	err  = __put_user(hdev, (u32*)&ubuf->st_rdev);
	err |= __put_user(hdev >> 32, ((u32*)&ubuf->st_rdev) + 1);
	err |= __put_user(kbuf->size, &ubuf->st_size_lo);
	err |= __put_user((kbuf->size >> 32), &ubuf->st_size_hi);
	err |= __put_user(kbuf->atime.tv_sec, &ubuf->st_atime);
	err |= __put_user(kbuf->atime.tv_nsec, &ubuf->st_atime_nsec);
	err |= __put_user(kbuf->mtime.tv_sec, &ubuf->st_mtime);
	err |= __put_user(kbuf->mtime.tv_nsec, &ubuf->st_mtime_nsec);
	err |= __put_user(kbuf->ctime.tv_sec, &ubuf->st_ctime);
	err |= __put_user(kbuf->ctime.tv_nsec, &ubuf->st_ctime_nsec);
	err |= __put_user(kbuf->blksize, &ubuf->st_blksize);
	err |= __put_user(kbuf->blocks, &ubuf->st_blocks);
	return err;
}

asmlinkage long
sys32_stat64 (char *filename, struct stat64 *statbuf)
{
	struct kstat s;
	long ret = vfs_stat(filename, &s);
	if (!ret)
		ret = putstat64(statbuf, &s);
	return ret;
}

asmlinkage long
sys32_lstat64 (char *filename, struct stat64 *statbuf)
{
	struct kstat s;
	long ret = vfs_lstat(filename, &s);
	if (!ret)
		ret = putstat64(statbuf, &s);
	return ret;
}

asmlinkage long
sys32_fstat64 (unsigned int fd, struct stat64 *statbuf)
{
	struct kstat s;
	long ret = vfs_fstat(fd, &s);
	if (!ret)
		ret = putstat64(statbuf, &s);
	return ret;
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
	u16 procs;
	u16 pad;
	u32 totalhigh;
	u32 freehigh;
	u32 mem_unit;
	char _f[8];
};

asmlinkage long
sys32_sysinfo (struct sysinfo32 *info)
{
	struct sysinfo s;
	long ret, err;
	int bitcount = 0;
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);
	ret = sys_sysinfo(&s);
	set_fs(old_fs);
	/* Check to see if any memory value is too large for 32-bit and
	 * scale down if needed.
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

	if (!access_ok(VERIFY_WRITE, info, sizeof(*info)))
		return -EFAULT;

	err  = __put_user(s.uptime, &info->uptime);
	err |= __put_user(s.loads[0], &info->loads[0]);
	err |= __put_user(s.loads[1], &info->loads[1]);
	err |= __put_user(s.loads[2], &info->loads[2]);
	err |= __put_user(s.totalram, &info->totalram);
	err |= __put_user(s.freeram, &info->freeram);
	err |= __put_user(s.sharedram, &info->sharedram);
	err |= __put_user(s.bufferram, &info->bufferram);
	err |= __put_user(s.totalswap, &info->totalswap);
	err |= __put_user(s.freeswap, &info->freeswap);
	err |= __put_user(s.procs, &info->procs);
	err |= __put_user (s.totalhigh, &info->totalhigh);
	err |= __put_user (s.freehigh, &info->freehigh);
	err |= __put_user (s.mem_unit, &info->mem_unit);
	if (err)
		return -EFAULT;
	return ret;
}

asmlinkage long
sys32_sched_rr_get_interval (pid_t pid, struct compat_timespec *interval)
{
	mm_segment_t old_fs = get_fs();
	struct timespec t;
	long ret;

	set_fs(KERNEL_DS);
	ret = sys_sched_rr_get_interval(pid, &t);
	set_fs(old_fs);
	if (put_compat_timespec(&t, interval))
		return -EFAULT;
	return ret;
}

asmlinkage long
sys32_pread (unsigned int fd, void *buf, unsigned int count, u32 pos_lo, u32 pos_hi)
{
	return sys_pread64(fd, buf, count, ((unsigned long) pos_hi << 32) | pos_lo);
}

asmlinkage long
sys32_pwrite (unsigned int fd, void *buf, unsigned int count, u32 pos_lo, u32 pos_hi)
{
	return sys_pwrite64(fd, buf, count, ((unsigned long) pos_hi << 32) | pos_lo);
}

asmlinkage long
sys32_sendfile (int out_fd, int in_fd, int *offset, unsigned int count)
{
	mm_segment_t old_fs = get_fs();
	long ret;
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

asmlinkage long
sys32_personality (unsigned int personality)
{
	long ret;

	if (current->personality == PER_LINUX32 && personality == PER_LINUX)
		personality = PER_LINUX32;
	ret = sys_personality(personality);
	if (ret == PER_LINUX32)
		ret = PER_LINUX;
	return ret;
}

asmlinkage unsigned long
sys32_brk (unsigned int brk)
{
	unsigned long ret, obrk;
	struct mm_struct *mm = current->mm;

	obrk = mm->brk;
	ret = sys_brk(brk);
	if (ret < obrk)
		clear_user((void *) ret, PAGE_ALIGN(ret) - ret);
	return ret;
}

/*
 * Exactly like fs/open.c:sys_open(), except that it doesn't set the O_LARGEFILE flag.
 */
asmlinkage long
sys32_open (const char * filename, int flags, int mode)
{
	char * tmp;
	int fd, error;

	tmp = getname(filename);
	fd = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {
		fd = get_unused_fd();
		if (fd >= 0) {
			struct file *f = filp_open(tmp, flags, mode);
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

/* Structure for ia32 emulation on ia64 */
struct epoll_event32
{
	u32 events;
	u32 data[2];
}; 

asmlinkage long
sys32_epoll_ctl(int epfd, int op, int fd, struct epoll_event32 *event)
{
	mm_segment_t old_fs = get_fs();
	struct epoll_event event64;
	int error = -EFAULT;
	u32 data_halfword;

	if ((error = verify_area(VERIFY_READ, event,
				 sizeof(struct epoll_event32))))
		return error;

	__get_user(event64.events, &event->events);
	__get_user(data_halfword, &event->data[0]);
	event64.data = data_halfword;
	__get_user(data_halfword, &event->data[1]);
 	event64.data |= (u64)data_halfword << 32;

	set_fs(KERNEL_DS);
	error = sys_epoll_ctl(epfd, op, fd, &event64);
	set_fs(old_fs);

	return error;
}

asmlinkage long
sys32_epoll_wait(int epfd, struct epoll_event32 *events, int maxevents,
		 int timeout)
{
	struct epoll_event *events64 = NULL;
	mm_segment_t old_fs = get_fs();
	int error, numevents, size;
	int evt_idx;
	int do_free_pages = 0;

	if (maxevents <= 0) {
		return -EINVAL;
	}

	/* Verify that the area passed by the user is writeable */
	if ((error = verify_area(VERIFY_WRITE, events,
				 maxevents * sizeof(struct epoll_event32))))
		return error;

	/* 
 	 * Allocate space for the intermediate copy.  If the space needed 
	 * is large enough to cause kmalloc to fail, then try again with
	 * __get_free_pages.
	 */
	size = maxevents * sizeof(struct epoll_event);
	events64 = kmalloc(size, GFP_KERNEL);
	if (events64 == NULL) {
		events64 = (struct epoll_event *)
				__get_free_pages(GFP_KERNEL, get_order(size));
		if (events64 == NULL) 
			return -ENOMEM;
		do_free_pages = 1;
	}

	/* Do the system call */
	set_fs(KERNEL_DS); /* copy_to/from_user should work on kernel mem*/
	numevents = sys_epoll_wait(epfd, events64, maxevents, timeout);
	set_fs(old_fs);

	/* Don't modify userspace memory if we're returning an error */
	if (numevents > 0) {
		/* Translate the 64-bit structures back into the 32-bit
		   structures */
		for (evt_idx = 0; evt_idx < numevents; evt_idx++) {
			__put_user(events64[evt_idx].events,
				   &events[evt_idx].events);
			__put_user((u32)events64[evt_idx].data,
				   &events[evt_idx].data[0]);
			__put_user((u32)(events64[evt_idx].data >> 32),
				   &events[evt_idx].data[1]);
		}
	}

	if (do_free_pages)
		free_pages((unsigned long) events64, get_order(size));
	else
		kfree(events64);
	return numevents;
}

/*
 * Get a yet unused TLS descriptor index.
 */
static int
get_free_idx (void)
{
	struct thread_struct *t = &current->thread;
	int idx;

	for (idx = 0; idx < GDT_ENTRY_TLS_ENTRIES; idx++)
		if (desc_empty(t->tls_array + idx))
			return idx + GDT_ENTRY_TLS_MIN;
	return -ESRCH;
}

/*
 * Set a given TLS descriptor:
 */
asmlinkage int
sys32_set_thread_area (struct ia32_user_desc *u_info)
{
	struct thread_struct *t = &current->thread;
	struct ia32_user_desc info;
	struct desc_struct *desc;
	int cpu, idx;

	if (copy_from_user(&info, u_info, sizeof(info)))
		return -EFAULT;
	idx = info.entry_number;

	/*
	 * index -1 means the kernel should try to find and allocate an empty descriptor:
	 */
	if (idx == -1) {
		idx = get_free_idx();
		if (idx < 0)
			return idx;
		if (put_user(idx, &u_info->entry_number))
			return -EFAULT;
	}

	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	desc = t->tls_array + idx - GDT_ENTRY_TLS_MIN;

	cpu = smp_processor_id();

	if (LDT_empty(&info)) {
		desc->a = 0;
		desc->b = 0;
	} else {
		desc->a = LDT_entry_a(&info);
		desc->b = LDT_entry_b(&info);
	}
	load_TLS(t, cpu);
	return 0;
}

/*
 * Get the current Thread-Local Storage area:
 */

#define GET_BASE(desc) (			\
	(((desc)->a >> 16) & 0x0000ffff) |	\
	(((desc)->b << 16) & 0x00ff0000) |	\
	( (desc)->b        & 0xff000000)   )

#define GET_LIMIT(desc) (			\
	((desc)->a & 0x0ffff) |			\
	 ((desc)->b & 0xf0000) )

#define GET_32BIT(desc)		(((desc)->b >> 23) & 1)
#define GET_CONTENTS(desc)	(((desc)->b >> 10) & 3)
#define GET_WRITABLE(desc)	(((desc)->b >>  9) & 1)
#define GET_LIMIT_PAGES(desc)	(((desc)->b >> 23) & 1)
#define GET_PRESENT(desc)	(((desc)->b >> 15) & 1)
#define GET_USEABLE(desc)	(((desc)->b >> 20) & 1)

asmlinkage int
sys32_get_thread_area (struct ia32_user_desc *u_info)
{
	struct ia32_user_desc info;
	struct desc_struct *desc;
	int idx;

	if (get_user(idx, &u_info->entry_number))
		return -EFAULT;
	if (idx < GDT_ENTRY_TLS_MIN || idx > GDT_ENTRY_TLS_MAX)
		return -EINVAL;

	desc = current->thread.tls_array + idx - GDT_ENTRY_TLS_MIN;

	info.entry_number = idx;
	info.base_addr = GET_BASE(desc);
	info.limit = GET_LIMIT(desc);
	info.seg_32bit = GET_32BIT(desc);
	info.contents = GET_CONTENTS(desc);
	info.read_exec_only = !GET_WRITABLE(desc);
	info.limit_in_pages = GET_LIMIT_PAGES(desc);
	info.seg_not_present = !GET_PRESENT(desc);
	info.useable = GET_USEABLE(desc);

	if (copy_to_user(u_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

extern asmlinkage long
sys_timer_create(clockid_t which_clock, struct sigevent *timer_event_spec,
		 timer_t * created_timer_id);

asmlinkage long
sys32_timer_create(u32 clock, struct sigevent32 *se32, timer_t *timer_id)
{
	struct sigevent se;
	mm_segment_t oldfs;
	timer_t t;
	long err;

	if (se32 == NULL)
		return sys_timer_create(clock, NULL, timer_id);

	memset(&se, 0, sizeof(struct sigevent));
	if (get_user(se.sigev_value.sival_int,	&se32->sigev_value.sival_int) ||
	    __get_user(se.sigev_signo, &se32->sigev_signo) ||
	    __get_user(se.sigev_notify, &se32->sigev_notify) ||
	    __copy_from_user(&se._sigev_un._pad, &se32->_sigev_un._pad,
	    sizeof(se._sigev_un._pad)))
		return -EFAULT;

	if (!access_ok(VERIFY_WRITE,timer_id,sizeof(timer_t)))
		return -EFAULT;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_timer_create(clock, &se, &t);
	set_fs(oldfs);

	if (!err)
		err = __put_user (t, timer_id);

	return err;
}

long sys32_fadvise64_64(int fd, __u32 offset_low, __u32 offset_high, 
			__u32 len_low, __u32 len_high, int advice)
{ 
	return sys_fadvise64_64(fd,
			       (((u64)offset_high)<<32) | offset_low,
			       (((u64)len_high)<<32) | len_low,
			       advice); 
} 

#ifdef	NOTYET  /* UNTESTED FOR IA64 FROM HERE DOWN */

asmlinkage long sys32_setreuid(compat_uid_t ruid, compat_uid_t euid)
{
	uid_t sruid, seuid;

	sruid = (ruid == (compat_uid_t)-1) ? ((uid_t)-1) : ((uid_t)ruid);
	seuid = (euid == (compat_uid_t)-1) ? ((uid_t)-1) : ((uid_t)euid);
	return sys_setreuid(sruid, seuid);
}

asmlinkage long
sys32_setresuid(compat_uid_t ruid, compat_uid_t euid,
		compat_uid_t suid)
{
	uid_t sruid, seuid, ssuid;

	sruid = (ruid == (compat_uid_t)-1) ? ((uid_t)-1) : ((uid_t)ruid);
	seuid = (euid == (compat_uid_t)-1) ? ((uid_t)-1) : ((uid_t)euid);
	ssuid = (suid == (compat_uid_t)-1) ? ((uid_t)-1) : ((uid_t)suid);
	return sys_setresuid(sruid, seuid, ssuid);
}

asmlinkage long
sys32_setregid(compat_gid_t rgid, compat_gid_t egid)
{
	gid_t srgid, segid;

	srgid = (rgid == (compat_gid_t)-1) ? ((gid_t)-1) : ((gid_t)rgid);
	segid = (egid == (compat_gid_t)-1) ? ((gid_t)-1) : ((gid_t)egid);
	return sys_setregid(srgid, segid);
}

asmlinkage long
sys32_setresgid(compat_gid_t rgid, compat_gid_t egid,
		compat_gid_t sgid)
{
	gid_t srgid, segid, ssgid;

	srgid = (rgid == (compat_gid_t)-1) ? ((gid_t)-1) : ((gid_t)rgid);
	segid = (egid == (compat_gid_t)-1) ? ((gid_t)-1) : ((gid_t)egid);
	ssgid = (sgid == (compat_gid_t)-1) ? ((gid_t)-1) : ((gid_t)sgid);
	return sys_setresgid(srgid, segid, ssgid);
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

struct nfsctl_arg32 {
	s32			ca32_version;	/* safeguard */
	union {
		struct nfsctl_svc32	u32_svc;
		struct nfsctl_client32	u32_client;
		struct nfsctl_export32	u32_export;
		u32			u32_debug;
	} u;
#define ca32_svc	u.u32_svc
#define ca32_client	u.u32_client
#define ca32_export	u.u32_export
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
nfs_getfh32_res_trans(union nfsctl_res *kres, union nfsctl_res32 *res32)
{
	int err;

	err = copy_to_user(&res32->cr32_getfh,
			&kres->cr_getfh,
			sizeof(res32->cr32_getfh));
	err |= __put_user(kres->cr_debug, &res32->cr32_debug);
	return err;
}

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

	if(!err && cmd == NFSCTL_GETFS)
		err = nfs_getfh32_res_trans(kres, res32);

done:
	if(karg)
		kfree(karg);
	if(kres)
		kfree(kres);
	return err;
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
#endif /* NOTYET */

/*
 * linux/arch/parisc/kernel/sys_parisc.c
 *
 * this implements syscalls which are handled per-arch.
 */

#include <asm/uaccess.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/shm.h>
#include <linux/smp_lock.h>

int sys_pipe(int *fildes)
{
	int fd[2];
	int error;

	error = do_pipe(fd);
	if (!error) {
		if (copy_to_user(fildes, fd, 2*sizeof(int)))
			error = -EFAULT;
	}
	return error;
}

static unsigned long get_unshared_area(unsigned long addr, unsigned long len)
{
	struct vm_area_struct *vma;

	addr = PAGE_ALIGN(addr);

	for (vma = find_vma(current->mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (TASK_SIZE - len < addr)
			return -ENOMEM;
		if (!vma || addr + len <= vma->vm_start)
			return addr;
		addr = vma->vm_end;
	}
}

#define DCACHE_ALIGN(addr) (((addr) + (SHMLBA - 1)) &~ (SHMLBA - 1))

/*
 * We need to know the offset to use.  Old scheme was to look for
 * existing mapping and use the same offset.  New scheme is to use the
 * address of the kernel data structure as the seed for the offset.
 * We'll see how that works...
 */
#if 0
static int get_offset(struct address_space *mapping)
{
	struct vm_area_struct *vma = list_entry(mapping->i_mmap_shared.next,
			struct vm_area_struct, shared);
	return (vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT)) &
		(SHMLBA - 1);
}
#else
/* The mapping is cacheline aligned, so there's no information in the bottom
 * few bits of the address.  We're looking for 10 bits (4MB / 4k), so let's
 * drop the bottom 8 bits and use bits 8-17.  
 */
static int get_offset(struct address_space *mapping)
{
	int offset = (unsigned long) mapping << (PAGE_SHIFT - 8);
	return offset & 0x3FF000;
}
#endif

static unsigned long get_shared_area(struct address_space *mapping,
		unsigned long addr, unsigned long len, unsigned long pgoff)
{
	struct vm_area_struct *vma;
	int offset = get_offset(mapping);

	addr = DCACHE_ALIGN(addr - offset) + offset;

	for (vma = find_vma(current->mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (TASK_SIZE - len < addr)
			return -ENOMEM;
		if (!vma || addr + len <= vma->vm_start)
			return addr;
		addr = DCACHE_ALIGN(vma->vm_end - offset) + offset;
		if (addr < vma->vm_end) /* handle wraparound */
			return -ENOMEM;
	}
}

unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct inode *inode;

	if (len > TASK_SIZE)
		return -ENOMEM;
	if (!addr)
		addr = TASK_UNMAPPED_BASE;

	inode = filp ? filp->f_dentry->d_inode : NULL;

	if (inode && (flags & MAP_SHARED)) {
		addr = get_shared_area(inode->i_mapping, addr, len, pgoff);
	} else {
		addr = get_unshared_area(addr, len);
	}
	return addr;
}

static unsigned long do_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long fd,
	unsigned long pgoff)
{
	struct file * file = NULL;
	unsigned long error = -EBADF;
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up_write(&current->mm->mmap_sem);

	if (file != NULL)
		fput(file);
out:
	return error;
}

asmlinkage unsigned long sys_mmap2(unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags, unsigned long fd,
	unsigned long pgoff)
{
	/* Make sure the shift for mmap2 is constant (12), no matter what PAGE_SIZE
	   we have. */
	return do_mmap2(addr, len, prot, flags, fd, pgoff >> (PAGE_SHIFT - 12));
}

asmlinkage unsigned long sys_mmap(unsigned long addr, unsigned long len,
		unsigned long prot, unsigned long flags, unsigned long fd,
		unsigned long offset)
{
	if (!(offset & ~PAGE_MASK)) {
		return do_mmap2(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
	} else {
		return -EINVAL;
	}
}

long sys_shmat_wrapper(int shmid, char *shmaddr, int shmflag)
{
	unsigned long raddr;
	int r;

	r = sys_shmat(shmid, shmaddr, shmflag, &raddr);
	if (r < 0)
		return r;
	return raddr;
}

/* Fucking broken ABI */

#ifdef CONFIG_PARISC64
extern asmlinkage long sys_truncate(const char *, unsigned long);
extern asmlinkage long sys_ftruncate(unsigned int, unsigned long);
extern asmlinkage long sys_fcntl(unsigned int, unsigned int, unsigned long);

asmlinkage long parisc_truncate64(const char * path,
					unsigned int high, unsigned int low)
{
	return sys_truncate(path, (long)high << 32 | low);
}

asmlinkage long parisc_ftruncate64(unsigned int fd,
					unsigned int high, unsigned int low)
{
	return sys_ftruncate(fd, (long)high << 32 | low);
}

/* stubs for the benefit of the syscall_table since truncate64 and truncate 
 * are identical on LP64 */
asmlinkage long sys_truncate64(const char * path, unsigned long length)
{
	return sys_truncate(path, length);
}
asmlinkage long sys_ftruncate64(unsigned int fd, unsigned long length)
{
	return sys_ftruncate(fd, length);
}
asmlinkage long sys_fcntl64(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	return sys_fcntl(fd, cmd, arg);
}
#else

extern asmlinkage long sys_truncate64(const char *, loff_t);
extern asmlinkage long sys_ftruncate64(unsigned int, loff_t);

asmlinkage long parisc_truncate64(const char * path,
					unsigned int high, unsigned int low)
{
	return sys_truncate64(path, (loff_t)high << 32 | low);
}

asmlinkage long parisc_ftruncate64(unsigned int fd,
					unsigned int high, unsigned int low)
{
	return sys_ftruncate64(fd, (loff_t)high << 32 | low);
}
#endif

extern asmlinkage ssize_t sys_pread64(unsigned int fd, char *buf,
					size_t count, loff_t pos);
extern asmlinkage ssize_t sys_pwrite64(unsigned int fd, const char *buf,
					size_t count, loff_t pos);
extern asmlinkage ssize_t sys_readahead(int fd, loff_t offset, size_t count);

asmlinkage ssize_t parisc_pread64(unsigned int fd, char *buf, size_t count,
					unsigned int high, unsigned int low)
{
	return sys_pread64(fd, buf, count, (loff_t)high << 32 | low);
}

asmlinkage ssize_t parisc_pwrite64(unsigned int fd, const char *buf,
			size_t count, unsigned int high, unsigned int low)
{
	return sys_pwrite64(fd, buf, count, (loff_t)high << 32 | low);
}

asmlinkage ssize_t parisc_readahead(int fd, unsigned int high, unsigned int low,
		                    size_t count)
{
	return sys_readahead(fd, (loff_t)high << 32 | low, count);
}

/*
 * FIXME, please remove this crap as soon as possible
 *
 * This is here to fix up broken glibc structures, 
 * which are already fixed in newer glibcs
 */
#include <linux/msg.h>
#include <linux/sem.h>
#include <linux/shm.h>
#include "sys32.h"

struct broken_ipc_perm
{
    key_t key;			/* Key.  */
    uid_t uid;			/* Owner's user ID.  */
    gid_t gid;			/* Owner's group ID.  */
    uid_t cuid;			/* Creator's user ID.  */
    gid_t cgid;			/* Creator's group ID.  */
    unsigned short int mode;		/* Read/write permission.  */
    unsigned short int __pad1;
    unsigned short int seq;		/* Sequence number.  */
    unsigned short int __pad2;
    unsigned long int __unused1;
    unsigned long int __unused2;
};
		    
struct broken_shmid64_ds {
	struct broken_ipc_perm	shm_perm;	/* operation perms */
	size_t			shm_segsz;	/* size of segment (bytes) */
#ifndef __LP64__
	unsigned int		__pad1;
#endif
	__kernel_time_t		shm_atime;	/* last attach time */
#ifndef __LP64__
	unsigned int		__pad2;
#endif
	__kernel_time_t		shm_dtime;	/* last detach time */
#ifndef __LP64__
	unsigned int		__pad3;
#endif
	__kernel_time_t		shm_ctime;	/* last change time */
	__kernel_pid_t		shm_cpid;	/* pid of creator */
	__kernel_pid_t		shm_lpid;	/* pid of last operator */
	unsigned int		shm_nattch;	/* no. of current attaches */
	unsigned int		__unused1;
	unsigned int		__unused2;
};

static void convert_broken_perm (struct broken_ipc_perm *out, struct ipc64_perm *in)
{
	out->key  = in->key;
	out->uid  = in->uid;
	out->gid  = in->gid;
	out->cuid = in->cuid;
	out->cgid = in->cgid;
	out->mode = in->mode;
	out->seq  = in->seq;
}

static int copyout_broken_shmid64(struct broken_shmid64_ds *buf, struct shmid64_ds *sbuf)
{
	struct broken_shmid64_ds tbuf;
	
	memset(&tbuf, 0, sizeof tbuf);
	convert_broken_perm (&tbuf.shm_perm, &sbuf->shm_perm);
	tbuf.shm_segsz = sbuf->shm_segsz;
	tbuf.shm_atime = sbuf->shm_atime;
	tbuf.shm_dtime = sbuf->shm_dtime;
	tbuf.shm_ctime = sbuf->shm_ctime;
	tbuf.shm_cpid = sbuf->shm_cpid;
	tbuf.shm_lpid = sbuf->shm_lpid;
	tbuf.shm_nattch = sbuf->shm_nattch;
	return copy_to_user(buf, &tbuf, sizeof tbuf) ? -EFAULT : 0;
}

int sys_msgctl_broken(int msqid, int cmd, struct msqid_ds *buf)
{
	return sys_msgctl (msqid, cmd & ~IPC_64, buf);
}

int sys_semctl_broken(int semid, int semnum, int cmd, union semun arg)
{
	return sys_semctl (semid, semnum, cmd & ~IPC_64, arg);
}

int sys_shmctl_broken(int shmid, int cmd, struct shmid64_ds *buf)
{
	struct shmid64_ds sbuf;
	int err;

	if (cmd & IPC_64) {
		cmd &= ~IPC_64;
		if (cmd == IPC_STAT || cmd == SHM_STAT) {
			KERNEL_SYSCALL(err, sys_shmctl, shmid, cmd, (struct shmid_ds *)&sbuf);
			if (err == 0)
				err = copyout_broken_shmid64((struct broken_shmid64_ds *)buf, &sbuf);
			return err;
		}
	}
	return sys_shmctl (shmid, cmd, (struct shmid_ds *)buf);
}

/*
 * This changes the io permissions bitmap in the current task.
 */
asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
	return -ENOSYS;
}

asmlinkage unsigned long sys_alloc_hugepages(int key, unsigned long addr, unsigned long len, int prot, int flag)
{
	return -ENOMEM;
}

asmlinkage int sys_free_hugepages(unsigned long addr)
{
	return -EINVAL;
}

/*
 * linux/arch/parisc/kernel/sys_parisc.c
 *
 * this implements the missing syscalls.
 */

#include <asm/uaccess.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/smp_lock.h>

/* for some reason, "old_readdir" is the only syscall which does not begin
 * with "sys_", which breaks the ENTRY_* macros in syscall.S so I "fixed"
 * it here.
 */

int sys_old_readdir(unsigned int fd, void *dirent, unsigned int count)
{
    return old_readdir(fd, dirent, count);
}

int sys_pipe(int *fildes)
{
	int fd[2];
	int error;

	lock_kernel();
	error = do_pipe(fd);
	unlock_kernel();
	if (!error) {
		if (copy_to_user(fildes, fd, 2*sizeof(int)))
			error = -EFAULT;
	}
	return error;
}

int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

int sys_mmap(unsigned long addr, unsigned long len,
		unsigned long prot, unsigned long flags, unsigned long fd,
		unsigned long offset)
{
	struct file * file = NULL;
	int error;

	down(&current->mm->mmap_sem);
	lock_kernel();
	if (!(flags & MAP_ANONYMOUS)) {
		error = -EBADF;
		file = fget(fd);
		if (!file)
			goto out;
	}
	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	error = do_mmap(file, addr, len, prot, flags, offset);
	if (file != NULL)
		fput(file);
out:
	unlock_kernel();
	up(&current->mm->mmap_sem);
	return error;
}

int sys_ioperm(unsigned long from, unsigned long num, int on)
{
	return -ENOSYS;
}

long sys_shmat_wrapper(int shmid, void *shmaddr, int shmflag)
{
	extern int sys_shmat(int shmid, char *shmaddr, int shmflg,
			     unsigned long * raddr);
	unsigned long raddr;
	int r;

	r = sys_shmat(shmid, shmaddr, shmflag, &raddr);
	if (r < 0)
		return r;
	return raddr;
}

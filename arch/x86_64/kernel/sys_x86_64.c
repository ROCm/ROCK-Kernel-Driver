/*
 * linux/arch/x86_64/kernel/sys_x86_64.c
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/utsname.h>

#include <asm/uaccess.h>
#include <asm/ipc.h>

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way Unix traditionally does this, though.
 */
asmlinkage long sys_pipe(unsigned long * fildes)
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

long sys_mmap(unsigned long addr, unsigned long len, unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long off)
{
	long error;
	struct file * file;

	error = -EINVAL;
	if (off & ~PAGE_MASK)
		goto out;

	error = -EBADF;
	file = NULL;
	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, off >> PAGE_SHIFT);
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
out:
	return error;
}

unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr, unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct vm_area_struct *vma;
	unsigned long end = TASK_SIZE;

	if (test_thread_flag(TIF_IA32))
		flags |= MAP_32BIT; 
	if (flags & MAP_32BIT) 
		end = 0xffffffff-1;
	if (len > end)
		return -ENOMEM;
	if (!addr) { 
		addr = TASK_UNMAPPED_64;
		if (flags & MAP_32BIT) {
			addr = TASK_UNMAPPED_32;
		}
	} 
	addr = PAGE_ALIGN(addr);

	for (vma = find_vma(current->mm, addr); ; vma = vma->vm_next) {
		/* At this point:  (!vma || addr < vma->vm_end). */
		if (end - len < addr)
			return -ENOMEM;
		if (!vma || addr + len <= vma->vm_start)
			return addr;
		addr = vma->vm_end;
	}
}

/*
 * Old cruft
 */
asmlinkage long sys_uname(struct old_utsname * name)
{
	int err;
	if (!name)
		return -EFAULT;
	down_read(&uts_sem);
	err=copy_to_user(name, &system_utsname, sizeof (*name));
	up_read(&uts_sem);
	return err?-EFAULT:0;
}

asmlinkage long sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

asmlinkage long wrap_sys_shmat(int shmid, char *shmaddr, int shmflg)
{
	unsigned long raddr;
	return sys_shmat(shmid,shmaddr,shmflg,&raddr) ?: raddr;
} 

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 - 2000 by Ralf Baechle
 * Copyright (C) 2000 Silicon Graphics, Inc.
 *
 * TODO:  Implement the compatibility syscalls.
 *        Don't waste that much memory for empty entries in the syscall
 *        table.
 */
#undef CONF_PRINT_SYSCALLS
#undef CONF_DEBUG_IRIX

#include <linux/config.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/utsname.h>
#include <linux/unistd.h>
#include <asm/branch.h>
#include <asm/offset.h>
#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/stackframe.h>
#include <asm/uaccess.h>

extern asmlinkage void syscall_trace(void);
typedef asmlinkage int (*syscall_t)(void *a0,...);
extern asmlinkage int (*do_syscalls)(struct pt_regs *regs, syscall_t fun,
				     int narg);
extern syscall_t sys_call_table[];
extern unsigned char sys_narg_table[];

asmlinkage int sys_pipe(struct pt_regs regs)
{
	int fd[2];
	int error, res;

	error = do_pipe(fd);
	if (error) {
		res = error;
		goto out;
	}
	regs.regs[3] = fd[1];
	res = fd[0];
out:
	return res;
}

/* common code for old and new mmaps */
static inline long
do_mmap2(unsigned long addr, unsigned long len, unsigned long prot,
        unsigned long flags, unsigned long fd, unsigned long pgoff)
{
	int error = -EBADF;
	struct file * file = NULL;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	down(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	up(&current->mm->mmap_sem);

	if (file)
		fput(file);
out:
	return error;
}

asmlinkage unsigned long old_mmap(unsigned long addr, size_t len, int prot,
                                  int flags, int fd, off_t offset)
{
	return do_mmap2(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
}

asmlinkage long
sys_mmap2(unsigned long addr, unsigned long len, unsigned long prot,
          unsigned long flags, unsigned long fd, unsigned long pgoff)
{
	return do_mmap2(addr, len, prot, flags, fd, pgoff);
}

asmlinkage int sys_fork(struct pt_regs regs)
{
	int res;

	save_static(&regs);
	res = do_fork(SIGCHLD, regs.regs[29], &regs, 0);
	return res;
}

asmlinkage int sys_clone(struct pt_regs regs)
{
	unsigned long clone_flags;
	unsigned long newsp;
	int res;

	save_static(&regs);
	clone_flags = regs.regs[4];
	newsp = regs.regs[5];
	if (!newsp)
		newsp = regs.regs[29];
	res = do_fork(clone_flags, newsp, &regs, 0);
	return res;
}

/*
 * sys_execve() executes a new program.
 */
asmlinkage int sys_execve(struct pt_regs regs)
{
	int error;
	char * filename;

	filename = getname((char *) (long)regs.regs[4]);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, (char **) (long)regs.regs[5],
	                  (char **) (long)regs.regs[6], &regs);
	putname(filename);

out:
	return error;
}

/*
 * Compacrapability ...
 */
asmlinkage int sys_uname(struct old_utsname * name)
{
	if (name && !copy_to_user(name, &system_utsname, sizeof (*name)))
		return 0;
	return -EFAULT;
}

/*
 * Compacrapability ...
 */
asmlinkage int sys_olduname(struct oldold_utsname * name)
{
	int error;

	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE,name,sizeof(struct oldold_utsname)))
		return -EFAULT;
  
	error = __copy_to_user(&name->sysname,&system_utsname.sysname,__OLD_UTS_LEN);
	error -= __put_user(0,name->sysname+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->nodename,&system_utsname.nodename,__OLD_UTS_LEN);
	error -= __put_user(0,name->nodename+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->release,&system_utsname.release,__OLD_UTS_LEN);
	error -= __put_user(0,name->release+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->version,&system_utsname.version,__OLD_UTS_LEN);
	error -= __put_user(0,name->version+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->machine,&system_utsname.machine,__OLD_UTS_LEN);
	error = __put_user(0,name->machine+__OLD_UTS_LEN);
	error = error ? -EFAULT : 0;

	return error;
}

/*
 * Do the indirect syscall syscall.
 * Don't care about kernel locking; the actual syscall will do it.
 *
 * XXX This is borken.
 */
asmlinkage int sys_syscall(struct pt_regs regs)
{
	syscall_t syscall;
	unsigned long syscallnr = regs.regs[4];
	unsigned long a0, a1, a2, a3, a4, a5, a6;
	int nargs, errno;

	if (syscallnr > __NR_Linux + __NR_Linux_syscalls)
		return -ENOSYS;

	syscall = sys_call_table[syscallnr];
	nargs = sys_narg_table[syscallnr];
	/*
	 * Prevent stack overflow by recursive
	 * syscall(__NR_syscall, __NR_syscall,...);
	 */
	if (syscall == (syscall_t) sys_syscall) {
		return -EINVAL;
	}

	if (syscall == NULL) {
		return -ENOSYS;
	}

	if(nargs > 3) {
		unsigned long usp = regs.regs[29];
		unsigned long *sp = (unsigned long *) usp;
		if(usp & 3) {
			printk("unaligned usp -EFAULT\n");
			force_sig(SIGSEGV, current);
			return -EFAULT;
		}
		errno = verify_area(VERIFY_READ, (void *) (usp + 16),
		                    (nargs - 3) * sizeof(unsigned long));
		if(errno) {
			return -EFAULT;
		}
		switch(nargs) {
		case 7:
			a3 = sp[4]; a4 = sp[5]; a5 = sp[6]; a6 = sp[7];
			break;
		case 6:
			a3 = sp[4]; a4 = sp[5]; a5 = sp[6]; a6 = 0;
			break;
		case 5:
			a3 = sp[4]; a4 = sp[5]; a5 = a6 = 0;
			break;
		case 4:
			a3 = sp[4]; a4 = a5 = a6 = 0;
			break;

		default:
			a3 = a4 = a5 = a6 = 0;
			break;
		}
	} else {
		a3 = a4 = a5 = a6 = 0;
	}
	a0 = regs.regs[5]; a1 = regs.regs[6]; a2 = regs.regs[7];
	if(nargs == 0)
		a0 = (unsigned long) &regs;
	return syscall((void *)a0, a1, a2, a3, a4, a5, a6);
}

/*
 * If we ever come here the user sp is bad.  Zap the process right away.
 * Due to the bad stack signaling wouldn't work.
 * XXX kernel locking???
 */
asmlinkage void bad_stack(void)
{
	do_exit(SIGSEGV);
}

/*
 * Build the string table for the builtin "poor man's strace".
 */
#ifdef CONF_PRINT_SYSCALLS
#define SYS(fun, narg) #fun,
static char *sfnames[] = {
#include "syscalls.h"
};
#endif

#if defined(CONFIG_BINFMT_IRIX) && defined(CONF_DEBUG_IRIX)
#define SYS(fun, narg) #fun,
static char *irix_sys_names[] = {
#include "irix5sys.h"
};
#endif

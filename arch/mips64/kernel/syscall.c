/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 - 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/unistd.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/slab.h>
#include <asm/ipc.h>
#include <asm/cachectl.h>
#include <asm/offset.h>
#include <asm/pgalloc.h>
#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/stackframe.h>
#include <asm/sysmips.h>
#include <asm/uaccess.h>

extern asmlinkage void syscall_trace(void);

asmlinkage int sys_pipe(abi64_no_regargs, struct pt_regs regs)
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

asmlinkage unsigned long
sys_mmap(unsigned long addr, size_t len, unsigned long prot,
         unsigned long flags, unsigned long fd, off_t offset)
{
	struct file * file = NULL;
	unsigned long error = -EFAULT;

	if (!(flags & MAP_ANONYMOUS)) {
		error = -EBADF;
		file = fget(fd);
		if (!file)
			goto out;
	}
        flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	down(&current->mm->mmap_sem);
        error = do_mmap(file, addr, len, prot, flags, offset);
	up(&current->mm->mmap_sem);
        if (file)
                fput(file);
out:

	return error;
}

asmlinkage int sys_fork(abi64_no_regargs, struct pt_regs regs)
{
	int res;

	save_static(&regs);
	res = do_fork(SIGCHLD, regs.regs[29], &regs, 0);
	return res;
}

asmlinkage int sys_clone(abi64_no_regargs, struct pt_regs regs)
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
asmlinkage int sys_execve(abi64_no_regargs, struct pt_regs regs)
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
 * Do the indirect syscall syscall.
 *
 * XXX This is borken.
 */
asmlinkage int sys_syscall(abi64_no_regargs, struct pt_regs regs)
{
	return -ENOSYS;
}

asmlinkage int
sys_sysmips(int cmd, long arg1, int arg2, int arg3)
{
	int	*p;
	char	*name;
	int	tmp, len, errno;

	switch(cmd) {
	case SETNAME: {
		char nodename[__NEW_UTS_LEN + 1];

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		name = (char *) arg1;

		len = strncpy_from_user(nodename, name, sizeof(nodename));
		if (len < 0)
			return -EFAULT;

		down_write(&uts_sem);
		strncpy(system_utsname.nodename, name, len);
		up_write(&uts_sem);
		system_utsname.nodename[len] = '\0';
		return 0;
	}

	case MIPS_ATOMIC_SET: {
		unsigned int tmp;

		p = (int *) arg1;
		errno = verify_area(VERIFY_WRITE, p, sizeof(*p));
		if (errno)
			return errno;
		errno = 0;

		__asm__(".set\tpush\t\t\t# sysmips(MIPS_ATOMIC, ...)\n\t"
			".set\tnoreorder\n\t"
			".set\tnoat\n\t"
			"1:\tll\t%0, %4\n\t"
			"2:\tmove\t$1, %3\n\t"
			"3:\tsc\t$1, %1\n\t"
			"beqzl\t$1, 2b\n\t"
			"4:\t ll\t%0, %4\n\t"
			".set\tpop\n\t"
			".section\t.fixup,\"ax\"\n"
			"5:\tli\t%2, 1\t\t\t# error\n\t"
			".previous\n\t"
			".section\t__ex_table,\"a\"\n\t"
			".dword\t1b, 5b\n\t"
			".dword\t3b, 5b\n\t"
			".dword\t4b, 5b\n\t"
			".previous\n\t"
			: "=&r" (tmp), "=o" (* (u32 *) p), "=r" (errno)
			: "r" (arg2), "o" (* (u32 *) p), "2" (errno)
			: "$1");

		if (errno)
			return -EFAULT;

		/* We're skipping error handling etc.  */
		if (current->ptrace & PT_TRACESYS)
			syscall_trace();

		__asm__ __volatile__(
			"move\t$29, %0\n\t"
			"j\tret_from_sys_call"
			: /* No outputs */
			: "r" (&cmd));
		/* Unreached */
	}

	case MIPS_FIXADE:
		tmp = current->thread.mflags & ~3;
		current->thread.mflags = tmp | (arg1 & 3);
		return 0;

	case FLUSH_CACHE:
		_flush_cache_l2();
		return 0;

	case MIPS_RDNVRAM:
		return -EIO;
	}

	return -EINVAL;
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage int sys_ipc (uint call, int first, int second,
			unsigned long third, void *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {
	case SEMOP:
		return sys_semop (first, (struct sembuf *)ptr, second);
	case SEMGET:
		return sys_semget (first, second, third);
	case SEMCTL: {
		union semun fourth;
		if (!ptr)
			return -EINVAL;
		if (get_user(fourth.__pad, (void **) ptr))
			return -EFAULT;
		return sys_semctl (first, second, third, fourth);
	}

	case MSGSND:
		return sys_msgsnd (first, (struct msgbuf *) ptr, 
				   second, third);
	case MSGRCV:
		switch (version) {
		case 0: {
			struct ipc_kludge tmp;
			if (!ptr)
				return -EINVAL;
			
			if (copy_from_user(&tmp,
					   (struct ipc_kludge *) ptr, 
					   sizeof (tmp)))
				return -EFAULT;
			return sys_msgrcv (first, tmp.msgp, second,
					   tmp.msgtyp, third);
		}
		default:
			return sys_msgrcv (first,
					   (struct msgbuf *) ptr,
					   second, fifth, third);
		}
	case MSGGET:
		return sys_msgget ((key_t) first, second);
	case MSGCTL:
		return sys_msgctl (first, second, (struct msqid_ds *) ptr);

	case SHMAT:
		switch (version) {
		default: {
			ulong raddr;
			ret = sys_shmat (first, (char *) ptr, second, &raddr);
			if (ret)
				return ret;
			return put_user (raddr, (ulong *) third);
		}
		case 1:	/* iBCS2 emulator entry point */
			if (!segment_eq(get_fs(), get_ds()))
				return -EINVAL;
			return sys_shmat (first, (char *) ptr, second, (ulong *) third);
		}
	case SHMDT: 
		return sys_shmdt ((char *)ptr);
	case SHMGET:
		return sys_shmget (first, second, third);
	case SHMCTL:
		return sys_shmctl (first, second,
				   (struct shmid_ds *) ptr);
	default:
		return -EINVAL;
	}
}

/*
 * No implemented yet ...
 */
asmlinkage int
sys_cachectl(char *addr, int nbytes, int op)
{
	return -ENOSYS;
}

/*
 * If we ever come here the user sp is bad.  Zap the process right away.
 * Due to the bad stack signaling wouldn't work.
 */
asmlinkage void bad_stack(void)
{
	do_exit(SIGSEGV);
}

asmlinkage int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

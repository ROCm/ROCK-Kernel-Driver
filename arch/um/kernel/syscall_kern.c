/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/file.h"
#include "linux/smp_lock.h"
#include "linux/mm.h"
#include "linux/utsname.h"
#include "linux/msg.h"
#include "linux/shm.h"
#include "linux/sys.h"
#include "linux/unistd.h"
#include "linux/slab.h"
#include "linux/utime.h"
#include "asm/mman.h"
#include "asm/uaccess.h"
#include "asm/ipc.h"
#include "kern_util.h"
#include "user_util.h"
#include "sysdep/syscalls.h"

long um_mount(char * dev_name, char * dir_name, char * type,
	      unsigned long new_flags, void * data)
{
	if(type == NULL) type = "";
	return(sys_mount(dev_name, dir_name, type, new_flags, data));
}

long sys_fork(void)
{
	struct task_struct *p;

	current->thread.forking = 1;
        p = do_fork(SIGCHLD, 0, NULL, 0, NULL);
	current->thread.forking = 0;
	return(IS_ERR(p) ? PTR_ERR(p) : p->pid);
}

long sys_clone(unsigned long clone_flags, unsigned long newsp)
{
	struct task_struct *p;

	current->thread.forking = 1;
	p = do_fork(clone_flags, newsp, NULL, 0, NULL);
	current->thread.forking = 0;
	return(IS_ERR(p) ? PTR_ERR(p) : p->pid);
}

long sys_vfork(void)
{
	struct task_struct *p;

	current->thread.forking = 1;
	p = do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, 0, NULL, 0, NULL);
	current->thread.forking = 0;
	return(IS_ERR(p) ? PTR_ERR(p) : p->pid);
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

long sys_mmap2(unsigned long addr, unsigned long len,
	       unsigned long prot, unsigned long flags,
	       unsigned long fd, unsigned long pgoff)
{
	return do_mmap2(addr, len, prot, flags, fd, pgoff);
}

/*
 * Perform the select(nd, in, out, ex, tv) and mmap() system
 * calls. Linux/i386 didn't use to be able to handle more than
 * 4 system call parameters, so these system calls used a memory
 * block for parameter passing..
 */

struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

int old_mmap(unsigned long addr, unsigned long len,
	     unsigned long prot, unsigned long flags,
	     unsigned long fd, unsigned long offset)
{
	int err = -EINVAL;
	if (offset & ~PAGE_MASK)
		goto out;

	err = do_mmap2(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
 out:
	return err;
}
/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
int sys_pipe(unsigned long * fildes)
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

int sys_sigaction(int sig, const struct old_sigaction *act,
			 struct old_sigaction *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (verify_area(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
int sys_ipc (uint call, int first, int second,
	     int third, void *ptr, long fifth)
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
		        panic("msgrcv with version != 0");
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

int sys_uname(struct old_utsname * name)
{
	int err;
	if (!name)
		return -EFAULT;
	down_read(&uts_sem);
	err=copy_to_user(name, &system_utsname, sizeof (*name));
	up_read(&uts_sem);
	return err?-EFAULT:0;
}

int sys_olduname(struct oldold_utsname * name)
{
	int error;

	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE,name,sizeof(struct oldold_utsname)))
		return -EFAULT;
  
  	down_read(&uts_sem);
	
	error = __copy_to_user(&name->sysname,&system_utsname.sysname,
			       __OLD_UTS_LEN);
	error |= __put_user(0,name->sysname+__OLD_UTS_LEN);
	error |= __copy_to_user(&name->nodename,&system_utsname.nodename,
				__OLD_UTS_LEN);
	error |= __put_user(0,name->nodename+__OLD_UTS_LEN);
	error |= __copy_to_user(&name->release,&system_utsname.release,
				__OLD_UTS_LEN);
	error |= __put_user(0,name->release+__OLD_UTS_LEN);
	error |= __copy_to_user(&name->version,&system_utsname.version,
				__OLD_UTS_LEN);
	error |= __put_user(0,name->version+__OLD_UTS_LEN);
	error |= __copy_to_user(&name->machine,&system_utsname.machine,
				__OLD_UTS_LEN);
	error |= __put_user(0,name->machine+__OLD_UTS_LEN);
	
	up_read(&uts_sem);
	
	error = error ? -EFAULT : 0;

	return error;
}

int sys_sigaltstack(const stack_t *uss, stack_t *uoss)
{
	return(do_sigaltstack(uss, uoss, PT_REGS_SP(&current->thread.regs)));
}

static inline int check_area(void *ptr, int size)
{
	return(verify_area(VERIFY_WRITE, ptr, size));
}

static int check_readlink(struct pt_regs *regs)
{
	return(check_area((void *) regs->regs.args[1], regs->regs.args[2]));
}

static int check_utime(struct pt_regs *regs)
{
	return(check_area((void *) regs->regs.args[1],
			  sizeof(struct utimbuf)));
}

static int check_oldstat(struct pt_regs *regs)
{
	return(check_area((void *) regs->regs.args[1], 
			  sizeof(struct __old_kernel_stat)));
}

static int check_stat(struct pt_regs *regs)
{
	return(check_area((void *) regs->regs.args[1], sizeof(struct stat)));
}

static int check_stat64(struct pt_regs *regs)
{
	return(check_area((void *) regs->regs.args[1], sizeof(struct stat64)));
}

struct bogus {
	int kernel_ds;
	int (*check_params)(struct pt_regs *);
};

struct bogus this_is_bogus[256] = {
	[ __NR_mknod ] = { 1, NULL },
	[ __NR_mkdir ] = { 1, NULL },
	[ __NR_rmdir ] = { 1, NULL },
	[ __NR_unlink ] = { 1, NULL },
	[ __NR_symlink ] = { 1, NULL },
	[ __NR_link ] = { 1, NULL },
	[ __NR_rename ] = { 1, NULL },
	[ __NR_umount ] = { 1, NULL },
	[ __NR_mount ] = { 1, NULL },
	[ __NR_pivot_root ] = { 1, NULL },
	[ __NR_chdir ] = { 1, NULL },
	[ __NR_chroot ] = { 1, NULL },
	[ __NR_open ] = { 1, NULL },
	[ __NR_quotactl ] = { 1, NULL },
	[ __NR_sysfs ] = { 1, NULL },
	[ __NR_readlink ] = { 1, check_readlink },
	[ __NR_acct ] = { 1, NULL },
	[ __NR_execve ] = { 1, NULL },
	[ __NR_uselib ] = { 1, NULL },
	[ __NR_statfs ] = { 1, NULL },
	[ __NR_truncate ] = { 1, NULL },
	[ __NR_access ] = { 1, NULL },
	[ __NR_chmod ] = { 1, NULL },
	[ __NR_chown ] = { 1, NULL },
	[ __NR_lchown ] = { 1, NULL },
	[ __NR_utime ] = { 1, check_utime },
	[ __NR_oldlstat ] = { 1, check_oldstat },
	[ __NR_oldstat ] = { 1, check_oldstat },
	[ __NR_stat ] = { 1, check_stat },
	[ __NR_lstat ] = { 1, check_stat },
	[ __NR_stat64 ] = { 1, check_stat64 },
	[ __NR_lstat64 ] = { 1, check_stat64 },
	[ __NR_chown32 ] = { 1, NULL },
};

/* sys_utimes */

static int check_bogosity(struct pt_regs *regs)
{
	struct bogus *bogon = &this_is_bogus[regs->regs.syscall];

	if(!bogon->kernel_ds) return(0);
	if(bogon->check_params && (*bogon->check_params)(regs))
		return(-EFAULT);
	set_fs(KERNEL_DS);
	return(0);
}

int nsyscalls = 0;

extern syscall_handler_t *sys_call_table[];

long execute_syscall(void *r)
{
	struct pt_regs *regs = r;
	long res;
	int syscall;

	current->thread.nsyscalls++;
	nsyscalls++;
	syscall = regs->regs.syscall;

	if((syscall >= NR_syscalls) || (syscall < 0))
		res = -ENOSYS;
	else if(honeypot && check_bogosity(regs))
		res = -EFAULT;
	else res = EXECUTE_SYSCALL(syscall, regs);

	set_fs(USER_DS);

	if(current->thread.singlestep_syscall){
		current->thread.singlestep_syscall = 0;
		current->ptrace &= ~PT_DTRACE;
		force_sig(SIGTRAP, current);
	}

	return(res);
}

spinlock_t syscall_lock = SPIN_LOCK_UNLOCKED;

void lock_syscall(void)
{
	spin_lock(&syscall_lock);
}

void unlock_syscall(void)
{
	spin_unlock(&syscall_lock);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */

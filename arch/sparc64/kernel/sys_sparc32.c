/* $Id: sys_sparc32.c,v 1.184 2002/02/09 19:49:31 davem Exp $
 * sys_sparc32.c: Conversion between 32bit and 64bit native syscalls.
 *
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h> 
#include <linux/mm.h> 
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
#include <linux/filter.h>
#include <linux/highmem.h>
#include <linux/highuid.h>
#include <linux/mman.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/icmpv6.h>
#include <linux/syscalls.h>
#include <linux/sysctl.h>
#include <linux/binfmts.h>
#include <linux/dnotify.h>
#include <linux/security.h>
#include <linux/compat.h>
#include <linux/vfs.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/ptrace.h>
#include <linux/highuid.h>

#include <asm/types.h>
#include <asm/ipc.h>
#include <asm/uaccess.h>
#include <asm/fpumacro.h>
#include <asm/semaphore.h>
#include <asm/mmu_context.h>

/* Use this to get at 32-bit user passed pointers. */
/* Things to consider: the low-level assembly stub does
   srl x, 0, x for first four arguments, so if you have
   pointer to something in the first four arguments, just
   declare it as a pointer, not u32. On the other side, 
   arguments from 5th onwards should be declared as u32
   for pointers, and need AA() around each usage.
   A() macro should be used for places where you e.g.
   have some internal variable u32 and just want to get
   rid of a compiler warning. AA() has to be used in
   places where you want to convert a function argument
   to 32bit pointer or when you e.g. access pt_regs
   structure and want to consider 32bit registers only.
   -jj
 */
#define A(__x) ((unsigned long)(__x))
#define AA(__x)				\
({	unsigned long __ret;		\
	__asm__ ("srl	%0, 0, %0"	\
		 : "=r" (__ret)		\
		 : "0" (__x));		\
	__ret;				\
})

 
asmlinkage long sys32_chown16(const char * filename, u16 user, u16 group)
{
	return sys_chown(filename, low2highuid(user), low2highgid(group));
}

asmlinkage long sys32_lchown16(const char * filename, u16 user, u16 group)
{
	return sys_lchown(filename, low2highuid(user), low2highgid(group));
}

asmlinkage long sys32_fchown16(unsigned int fd, u16 user, u16 group)
{
	return sys_fchown(fd, low2highuid(user), low2highgid(group));
}

asmlinkage long sys32_setregid16(u16 rgid, u16 egid)
{
	return sys_setregid(low2highgid(rgid), low2highgid(egid));
}

asmlinkage long sys32_setgid16(u16 gid)
{
	return sys_setgid((gid_t)gid);
}

asmlinkage long sys32_setreuid16(u16 ruid, u16 euid)
{
	return sys_setreuid(low2highuid(ruid), low2highuid(euid));
}

asmlinkage long sys32_setuid16(u16 uid)
{
	return sys_setuid((uid_t)uid);
}

asmlinkage long sys32_setresuid16(u16 ruid, u16 euid, u16 suid)
{
	return sys_setresuid(low2highuid(ruid), low2highuid(euid),
		low2highuid(suid));
}

asmlinkage long sys32_getresuid16(u16 *ruid, u16 *euid, u16 *suid)
{
	int retval;

	if (!(retval = put_user(high2lowuid(current->uid), ruid)) &&
	    !(retval = put_user(high2lowuid(current->euid), euid)))
		retval = put_user(high2lowuid(current->suid), suid);

	return retval;
}

asmlinkage long sys32_setresgid16(u16 rgid, u16 egid, u16 sgid)
{
	return sys_setresgid(low2highgid(rgid), low2highgid(egid),
		low2highgid(sgid));
}

asmlinkage long sys32_getresgid16(u16 *rgid, u16 *egid, u16 *sgid)
{
	int retval;

	if (!(retval = put_user(high2lowgid(current->gid), rgid)) &&
	    !(retval = put_user(high2lowgid(current->egid), egid)))
		retval = put_user(high2lowgid(current->sgid), sgid);

	return retval;
}

asmlinkage long sys32_setfsuid16(u16 uid)
{
	return sys_setfsuid((uid_t)uid);
}

asmlinkage long sys32_setfsgid16(u16 gid)
{
	return sys_setfsgid((gid_t)gid);
}

static int groups16_to_user(u16 *grouplist, struct group_info *group_info)
{
	int i;
	u16 group;

	for (i = 0; i < group_info->ngroups; i++) {
		group = (u16)GROUP_AT(group_info, i);
		if (put_user(group, grouplist+i))
			return -EFAULT;
	}

	return 0;
}

static int groups16_from_user(struct group_info *group_info, u16 *grouplist)
{
	int i;
	u16 group;

	for (i = 0; i < group_info->ngroups; i++) {
		if (get_user(group, grouplist+i))
			return  -EFAULT;
		GROUP_AT(group_info, i) = (gid_t)group;
	}

	return 0;
}

asmlinkage long sys32_getgroups16(int gidsetsize, u16 *grouplist)
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

asmlinkage long sys32_setgroups16(int gidsetsize, u16 *grouplist)
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

asmlinkage long sys32_getuid16(void)
{
	return high2lowuid(current->uid);
}

asmlinkage long sys32_geteuid16(void)
{
	return high2lowuid(current->euid);
}

asmlinkage long sys32_getgid16(void)
{
	return high2lowgid(current->gid);
}

asmlinkage long sys32_getegid16(void)
{
	return high2lowgid(current->egid);
}

/* 32-bit timeval and related flotsam.  */

static long get_tv32(struct timeval *o, struct compat_timeval *i)
{
	return (!access_ok(VERIFY_READ, tv32, sizeof(*tv32)) ||
		(__get_user(o->tv_sec, &i->tv_sec) |
		 __get_user(o->tv_usec, &i->tv_usec)));
}

static inline long put_tv32(struct compat_timeval *o, struct timeval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->tv_sec, &o->tv_sec) |
		 __put_user(i->tv_usec, &o->tv_usec)));
}

struct msgbuf32 { s32 mtype; char mtext[1]; };

struct ipc_perm32
{
	key_t    	  key;
        compat_uid_t  uid;
        compat_gid_t  gid;
        compat_uid_t  cuid;
        compat_gid_t  cgid;
        compat_mode_t mode;
        unsigned short  seq;
};

struct semid_ds32 {
        struct ipc_perm32 sem_perm;               /* permissions .. see ipc.h */
        compat_time_t   sem_otime;              /* last semop time */
        compat_time_t   sem_ctime;              /* last change time */
        u32 sem_base;              /* ptr to first semaphore in array */
        u32 sem_pending;          /* pending operations to be processed */
        u32 sem_pending_last;    /* last pending operation */
        u32 undo;                  /* undo requests on this array */
        unsigned short  sem_nsems;              /* no. of semaphores in array */
};

struct semid64_ds32 {
	struct ipc64_perm sem_perm;		  /* this structure is the same on sparc32 and sparc64 */
	unsigned int	  __pad1;
	compat_time_t   sem_otime;
	unsigned int	  __pad2;
	compat_time_t   sem_ctime;
	u32 sem_nsems;
	u32 __unused1;
	u32 __unused2;
};

struct msqid_ds32
{
        struct ipc_perm32 msg_perm;
        u32 msg_first;
        u32 msg_last;
        compat_time_t   msg_stime;
        compat_time_t   msg_rtime;
        compat_time_t   msg_ctime;
        u32 wwait;
        u32 rwait;
        unsigned short msg_cbytes;
        unsigned short msg_qnum;  
        unsigned short msg_qbytes;
        compat_ipc_pid_t msg_lspid;
        compat_ipc_pid_t msg_lrpid;
};

struct msqid64_ds32 {
	struct ipc64_perm msg_perm;
	unsigned int   __pad1;
	compat_time_t   msg_stime;
	unsigned int   __pad2;
	compat_time_t   msg_rtime;
	unsigned int   __pad3;
	compat_time_t   msg_ctime;
	unsigned int  msg_cbytes;
	unsigned int  msg_qnum;
	unsigned int  msg_qbytes;
	compat_pid_t msg_lspid;
	compat_pid_t msg_lrpid;
	unsigned int  __unused1;
	unsigned int  __unused2;
};


struct shmid_ds32 {
	struct ipc_perm32       shm_perm;
	int                     shm_segsz;
	compat_time_t         shm_atime;
	compat_time_t         shm_dtime;
	compat_time_t         shm_ctime;
	compat_ipc_pid_t    shm_cpid; 
	compat_ipc_pid_t    shm_lpid; 
	unsigned short          shm_nattch;
};

struct shmid64_ds32 {
	struct ipc64_perm	shm_perm;
	unsigned int		__pad1;
	compat_time_t  	shm_atime;
	unsigned int		__pad2;
	compat_time_t  	shm_dtime;
	unsigned int		__pad3;
	compat_time_t  	shm_ctime;
	compat_size_t	shm_segsz;
	compat_pid_t	shm_cpid;
	compat_pid_t	shm_lpid;
	unsigned int		shm_nattch;
	unsigned int		__unused1;
	unsigned int		__unused2;
};

                                                        
/*
 * sys32_ipc() is the de-multiplexer for the SysV IPC calls in 32bit emulation..
 *
 * This is really horribly ugly.
 */
#define IPCOP_MASK(__x)	(1UL << ((__x)&~IPC_64))
static int do_sys32_semctl(int first, int second, int third, void *uptr)
{
	union semun fourth;
	u32 pad;
	int err = -EINVAL;

	if (!uptr)
		goto out;
	err = -EFAULT;
	if (get_user (pad, (u32 *)uptr))
		goto out;
	if ((third & ~IPC_64) == SETVAL)
		fourth.val = (int)pad;
	else
		fourth.__pad = (void *)A(pad);
	if (IPCOP_MASK (third) &
	    (IPCOP_MASK (IPC_INFO) | IPCOP_MASK (SEM_INFO) | IPCOP_MASK (GETVAL) |
	     IPCOP_MASK (GETPID) | IPCOP_MASK (GETNCNT) | IPCOP_MASK (GETZCNT) |
	     IPCOP_MASK (GETALL) | IPCOP_MASK (SETALL) | IPCOP_MASK (IPC_RMID))) {
		err = sys_semctl (first, second, third, fourth);
	} else if (third & IPC_64) {
		struct semid64_ds s;
		struct semid64_ds32 *usp = (struct semid64_ds32 *)A(pad);
		mm_segment_t old_fs;
		int need_back_translation;

		if (third == (IPC_SET|IPC_64)) {
			err = get_user (s.sem_perm.uid, &usp->sem_perm.uid);
			err |= __get_user (s.sem_perm.gid, &usp->sem_perm.gid);
			err |= __get_user (s.sem_perm.mode, &usp->sem_perm.mode);
			if (err)
				goto out;
			fourth.__pad = &s;
		}
		need_back_translation =
			(IPCOP_MASK (third) &
			 (IPCOP_MASK (SEM_STAT) | IPCOP_MASK (IPC_STAT))) != 0;
		if (need_back_translation)
			fourth.__pad = &s;
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_semctl (first, second, third, fourth);
		set_fs (old_fs);
		if (need_back_translation) {
			int err2 = copy_to_user (&usp->sem_perm, &s.sem_perm, sizeof(struct ipc64_perm) + 2*sizeof(time_t));
			err2 |= __put_user (s.sem_nsems, &usp->sem_nsems);
			if (err2) err = -EFAULT;
		}
	} else {
		struct semid_ds s;
		struct semid_ds32 *usp = (struct semid_ds32 *)A(pad);
		mm_segment_t old_fs;
		int need_back_translation;

		if (third == IPC_SET) {
			err = get_user (s.sem_perm.uid, &usp->sem_perm.uid);
			err |= __get_user (s.sem_perm.gid, &usp->sem_perm.gid);
			err |= __get_user (s.sem_perm.mode, &usp->sem_perm.mode);
			if (err)
				goto out;
			fourth.__pad = &s;
		}
		need_back_translation =
			(IPCOP_MASK (third) &
			 (IPCOP_MASK (SEM_STAT) | IPCOP_MASK (IPC_STAT))) != 0;
		if (need_back_translation)
			fourth.__pad = &s;
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_semctl (first, second, third, fourth);
		set_fs (old_fs);
		if (need_back_translation) {
			int err2 = put_user (s.sem_perm.key, &usp->sem_perm.key);
			err2 |= __put_user (high2lowuid(s.sem_perm.uid), &usp->sem_perm.uid);
			err2 |= __put_user (high2lowgid(s.sem_perm.gid), &usp->sem_perm.gid);
			err2 |= __put_user (high2lowuid(s.sem_perm.cuid), &usp->sem_perm.cuid);
			err2 |= __put_user (high2lowgid(s.sem_perm.cgid), &usp->sem_perm.cgid);
			err2 |= __put_user (s.sem_perm.mode, &usp->sem_perm.mode);
			err2 |= __put_user (s.sem_perm.seq, &usp->sem_perm.seq);
			err2 |= __put_user (s.sem_otime, &usp->sem_otime);
			err2 |= __put_user (s.sem_ctime, &usp->sem_ctime);
			err2 |= __put_user (s.sem_nsems, &usp->sem_nsems);
			if (err2) err = -EFAULT;
		}
	}
out:
	return err;
}

static int do_sys32_msgsnd (int first, int second, int third, void *uptr)
{
	struct msgbuf *p = kmalloc (second + sizeof (struct msgbuf), GFP_USER);
	struct msgbuf32 *up = (struct msgbuf32 *)uptr;
	mm_segment_t old_fs;
	int err;

	if (!p)
		return -ENOMEM;
	err = -EFAULT;
	if (get_user (p->mtype, &up->mtype) ||
	    __copy_from_user (p->mtext, &up->mtext, second))
		goto out;
	old_fs = get_fs ();
	set_fs (KERNEL_DS);
	err = sys_msgsnd (first, p, second, third);
	set_fs (old_fs);
out:
	kfree (p);
	return err;
}

static int do_sys32_msgrcv (int first, int second, int msgtyp, int third,
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
	p = kmalloc (second + sizeof (struct msgbuf), GFP_USER);
	if (!p)
		goto out;
	old_fs = get_fs ();
	set_fs (KERNEL_DS);
	err = sys_msgrcv (first, p, second, msgtyp, third);
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

static int do_sys32_msgctl (int first, int second, void *uptr)
{
	int err;

	if (IPCOP_MASK (second) &
	    (IPCOP_MASK (IPC_INFO) | IPCOP_MASK (MSG_INFO) |
	     IPCOP_MASK (IPC_RMID))) {
		err = sys_msgctl (first, second, (struct msqid_ds *)uptr);
	} else if (second & IPC_64) {
		struct msqid64_ds m;
		struct msqid64_ds32 *up = (struct msqid64_ds32 *)uptr;
		mm_segment_t old_fs;

		if (second == (IPC_SET|IPC_64)) {
			err = get_user (m.msg_perm.uid, &up->msg_perm.uid);
			err |= __get_user (m.msg_perm.gid, &up->msg_perm.gid);
			err |= __get_user (m.msg_perm.mode, &up->msg_perm.mode);
			err |= __get_user (m.msg_qbytes, &up->msg_qbytes);
			if (err)
				goto out;
		}
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_msgctl (first, second, (struct msqid_ds *)&m);
		set_fs (old_fs);
		if (IPCOP_MASK (second) &
		    (IPCOP_MASK (MSG_STAT) | IPCOP_MASK (IPC_STAT))) {
			int err2 = copy_to_user(&up->msg_perm, &m.msg_perm, sizeof(struct ipc64_perm) + 3*sizeof(time_t));
			err2 |= __put_user (m.msg_cbytes, &up->msg_cbytes);
			err2 |= __put_user (m.msg_qnum, &up->msg_qnum);
			err2 |= __put_user (m.msg_qbytes, &up->msg_qbytes);
			err2 |= __put_user (m.msg_lspid, &up->msg_lspid);
			err2 |= __put_user (m.msg_lrpid, &up->msg_lrpid);
			if (err2)
				err = -EFAULT;
		}
	} else {
		struct msqid_ds m;
		struct msqid_ds32 *up = (struct msqid_ds32 *)uptr;
		mm_segment_t old_fs;

		if (second == IPC_SET) {
			err = get_user (m.msg_perm.uid, &up->msg_perm.uid);
			err |= __get_user (m.msg_perm.gid, &up->msg_perm.gid);
			err |= __get_user (m.msg_perm.mode, &up->msg_perm.mode);
			err |= __get_user (m.msg_qbytes, &up->msg_qbytes);
			if (err)
				goto out;
		}
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_msgctl (first, second, &m);
		set_fs (old_fs);
		if (IPCOP_MASK (second) &
		    (IPCOP_MASK (MSG_STAT) | IPCOP_MASK (IPC_STAT))) {
			int err2 = put_user (m.msg_perm.key, &up->msg_perm.key);
			err2 |= __put_user (high2lowuid(m.msg_perm.uid), &up->msg_perm.uid);
			err2 |= __put_user (high2lowgid(m.msg_perm.gid), &up->msg_perm.gid);
			err2 |= __put_user (high2lowuid(m.msg_perm.cuid), &up->msg_perm.cuid);
			err2 |= __put_user (high2lowgid(m.msg_perm.cgid), &up->msg_perm.cgid);
			err2 |= __put_user (m.msg_perm.mode, &up->msg_perm.mode);
			err2 |= __put_user (m.msg_perm.seq, &up->msg_perm.seq);
			err2 |= __put_user (m.msg_stime, &up->msg_stime);
			err2 |= __put_user (m.msg_rtime, &up->msg_rtime);
			err2 |= __put_user (m.msg_ctime, &up->msg_ctime);
			err2 |= __put_user (m.msg_cbytes, &up->msg_cbytes);
			err2 |= __put_user (m.msg_qnum, &up->msg_qnum);
			err2 |= __put_user (m.msg_qbytes, &up->msg_qbytes);
			err2 |= __put_user (m.msg_lspid, &up->msg_lspid);
			err2 |= __put_user (m.msg_lrpid, &up->msg_lrpid);
			if (err2)
				err = -EFAULT;
		}
	}

out:
	return err;
}

static int do_sys32_shmat (int first, int second, int third, int version, void *uptr)
{
	unsigned long raddr;
	u32 *uaddr = (u32 *)A((u32)third);
	int err = -EINVAL;

	if (version == 1)
		goto out;
	err = do_shmat (first, uptr, second, &raddr);
	if (err)
		goto out;
	err = put_user (raddr, uaddr);
out:
	return err;
}

static int do_sys32_shmctl (int first, int second, void *uptr)
{
	int err;

	if (IPCOP_MASK (second) &
	    (IPCOP_MASK (IPC_INFO) | IPCOP_MASK (SHM_LOCK) | IPCOP_MASK (SHM_UNLOCK) |
	     IPCOP_MASK (IPC_RMID))) {
		if (second == (IPC_INFO|IPC_64))
			second = IPC_INFO; /* So that we don't have to translate it */
		err = sys_shmctl (first, second, (struct shmid_ds *)uptr);
	} else if ((second & IPC_64) && second != (SHM_INFO|IPC_64)) {
		struct shmid64_ds s;
		struct shmid64_ds32 *up = (struct shmid64_ds32 *)uptr;
		mm_segment_t old_fs;

		if (second == (IPC_SET|IPC_64)) {
			err = get_user (s.shm_perm.uid, &up->shm_perm.uid);
			err |= __get_user (s.shm_perm.gid, &up->shm_perm.gid);
			err |= __get_user (s.shm_perm.mode, &up->shm_perm.mode);
			if (err)
				goto out;
		}
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_shmctl (first, second, (struct shmid_ds *)&s);
		set_fs (old_fs);
		if (err < 0)
			goto out;

		/* Mask it even in this case so it becomes a CSE. */
		if (IPCOP_MASK (second) &
		    (IPCOP_MASK (SHM_STAT) | IPCOP_MASK (IPC_STAT))) {
			int err2 = copy_to_user (&up->shm_perm, &s.shm_perm, sizeof(struct ipc64_perm) + 3*sizeof(time_t));
			err2 |= __put_user (s.shm_segsz, &up->shm_segsz);
			err2 |= __put_user (s.shm_nattch, &up->shm_nattch);
			err2 |= __put_user (s.shm_cpid, &up->shm_cpid);
			err2 |= __put_user (s.shm_lpid, &up->shm_lpid);
			if (err2)
				err = -EFAULT;
		}
	} else {
		struct shmid_ds s;
		struct shmid_ds32 *up = (struct shmid_ds32 *)uptr;
		mm_segment_t old_fs;

		second &= ~IPC_64;
		if (second == IPC_SET) {
			err = get_user (s.shm_perm.uid, &up->shm_perm.uid);
			err |= __get_user (s.shm_perm.gid, &up->shm_perm.gid);
			err |= __get_user (s.shm_perm.mode, &up->shm_perm.mode);
			if (err)
				goto out;
		}
		old_fs = get_fs ();
		set_fs (KERNEL_DS);
		err = sys_shmctl (first, second, &s);
		set_fs (old_fs);
		if (err < 0)
			goto out;

		/* Mask it even in this case so it becomes a CSE. */
		if (second == SHM_INFO) {
			struct shm_info32 {
				int used_ids;
				u32 shm_tot, shm_rss, shm_swp;
				u32 swap_attempts, swap_successes;
			} *uip = (struct shm_info32 *)uptr;
			struct shm_info *kp = (struct shm_info *)&s;
			int err2 = put_user (kp->used_ids, &uip->used_ids);
			err2 |= __put_user (kp->shm_tot, &uip->shm_tot);
			err2 |= __put_user (kp->shm_rss, &uip->shm_rss);
			err2 |= __put_user (kp->shm_swp, &uip->shm_swp);
			err2 |= __put_user (kp->swap_attempts, &uip->swap_attempts);
			err2 |= __put_user (kp->swap_successes, &uip->swap_successes);
			if (err2)
				err = -EFAULT;
		} else if (IPCOP_MASK (second) &
			   (IPCOP_MASK (SHM_STAT) | IPCOP_MASK (IPC_STAT))) {
			int err2 = put_user (s.shm_perm.key, &up->shm_perm.key);
			err2 |= __put_user (high2lowuid(s.shm_perm.uid), &up->shm_perm.uid);
			err2 |= __put_user (high2lowuid(s.shm_perm.gid), &up->shm_perm.gid);
			err2 |= __put_user (high2lowuid(s.shm_perm.cuid), &up->shm_perm.cuid);
			err2 |= __put_user (high2lowuid(s.shm_perm.cgid), &up->shm_perm.cgid);
			err2 |= __put_user (s.shm_perm.mode, &up->shm_perm.mode);
			err2 |= __put_user (s.shm_perm.seq, &up->shm_perm.seq);
			err2 |= __put_user (s.shm_atime, &up->shm_atime);
			err2 |= __put_user (s.shm_dtime, &up->shm_dtime);
			err2 |= __put_user (s.shm_ctime, &up->shm_ctime);
			err2 |= __put_user (s.shm_segsz, &up->shm_segsz);
			err2 |= __put_user (s.shm_nattch, &up->shm_nattch);
			err2 |= __put_user (s.shm_cpid, &up->shm_cpid);
			err2 |= __put_user (s.shm_lpid, &up->shm_lpid);
			if (err2)
				err = -EFAULT;
		}
	}
out:
	return err;
}

static int sys32_semtimedop(int semid, struct sembuf *tsems, int nsems,
			    const struct compat_timespec *timeout32)
{
	struct compat_timespec t32;
	struct timespec *t64 = compat_alloc_user_space(sizeof(*t64));

	if (copy_from_user(&t32, timeout32, sizeof(t32)))
		return -EFAULT;

	if (put_user(t32.tv_sec, &t64->tv_sec) ||
	    put_user(t32.tv_nsec, &t64->tv_nsec))
		return -EFAULT;

	return sys_semtimedop(semid, tsems, nsems, t64);
}

asmlinkage int sys32_ipc (u32 call, int first, int second, int third, u32 ptr, u32 fifth)
{
	int version, err;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	if (call <= SEMCTL)
		switch (call) {
		case SEMOP:
			/* struct sembuf is the same on 32 and 64bit :)) */
			err = sys_semtimedop (first, (struct sembuf *)AA(ptr), second, NULL);
			goto out;
		case SEMTIMEDOP:
			err = sys32_semtimedop (first, (struct sembuf *)AA(ptr), second, (const struct compat_timespec *) AA(fifth));
		case SEMGET:
			err = sys_semget (first, second, third);
			goto out;
		case SEMCTL:
			err = do_sys32_semctl (first, second, third, (void *)AA(ptr));
			goto out;
		default:
			err = -ENOSYS;
			goto out;
		};
	if (call <= MSGCTL) 
		switch (call) {
		case MSGSND:
			err = do_sys32_msgsnd (first, second, third, (void *)AA(ptr));
			goto out;
		case MSGRCV:
			err = do_sys32_msgrcv (first, second, fifth, third,
					       version, (void *)AA(ptr));
			goto out;
		case MSGGET:
			err = sys_msgget ((key_t) first, second);
			goto out;
		case MSGCTL:
			err = do_sys32_msgctl (first, second, (void *)AA(ptr));
			goto out;
		default:
			err = -ENOSYS;
			goto out;
		}
	if (call <= SHMCTL) 
		switch (call) {
		case SHMAT:
			err = do_sys32_shmat (first, second, third,
					      version, (void *)AA(ptr));
			goto out;
		case SHMDT: 
			err = sys_shmdt ((char *)AA(ptr));
			goto out;
		case SHMGET:
			err = sys_shmget (first, second, third);
			goto out;
		case SHMCTL:
			err = do_sys32_shmctl (first, second, (void *)AA(ptr));
			goto out;
		default:
			err = -ENOSYS;
			goto out;
		}

	err = -ENOSYS;

out:
	return err;
}

asmlinkage int sys32_truncate64(const char * path, unsigned long high, unsigned long low)
{
	if ((int)high < 0)
		return -EINVAL;
	else
		return sys_truncate(path, (high << 32) | low);
}

asmlinkage int sys32_ftruncate64(unsigned int fd, unsigned long high, unsigned long low)
{
	if ((int)high < 0)
		return -EINVAL;
	else
		return sys_ftruncate(fd, (high << 32) | low);
}

/* readdir & getdents */

#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x) (((x)+sizeof(u32)-1) & ~(sizeof(u32)-1))

struct old_linux_dirent32 {
	u32		d_ino;
	u32		d_offset;
	unsigned short	d_namlen;
	char		d_name[1];
};

struct readdir_callback32 {
	struct old_linux_dirent32 * dirent;
	int count;
};

static int fillonedir(void * __buf, const char * name, int namlen,
		      loff_t offset, ino_t ino, unsigned int d_type)
{
	struct readdir_callback32 * buf = (struct readdir_callback32 *) __buf;
	struct old_linux_dirent32 * dirent;

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

asmlinkage int old32_readdir(unsigned int fd, struct old_linux_dirent32 *dirent, unsigned int count)
{
	int error = -EBADF;
	struct file * file;
	struct readdir_callback32 buf;

	file = fget(fd);
	if (!file)
		goto out;

	buf.count = 0;
	buf.dirent = dirent;

	error = vfs_readdir(file, fillonedir, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.count;

out_putf:
	fput(file);
out:
	return error;
}

struct linux_dirent32 {
	u32		d_ino;
	u32		d_off;
	unsigned short	d_reclen;
	char		d_name[1];
};

struct getdents_callback32 {
	struct linux_dirent32 * current_dir;
	struct linux_dirent32 * previous;
	int count;
	int error;
};

static int filldir(void * __buf, const char * name, int namlen, loff_t offset, ino_t ino,
		   unsigned int d_type)
{
	struct linux_dirent32 * dirent;
	struct getdents_callback32 * buf = (struct getdents_callback32 *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 2);

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
	put_user(d_type, (char *) dirent + reclen - 1);
	dirent = (void *) dirent + reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
}

asmlinkage int sys32_getdents(unsigned int fd, struct linux_dirent32 *dirent, unsigned int count)
{
	struct file * file;
	struct linux_dirent32 * lastdirent;
	struct getdents_callback32 buf;
	int error = -EBADF;

	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = vfs_readdir(file, filldir, &buf);
	if (error < 0)
		goto out_putf;
	lastdirent = buf.previous;
	error = buf.error;
	if(lastdirent) {
		put_user(file->f_pos, &lastdirent->d_off);
		error = count - buf.count;
	}
out_putf:
	fput(file);
out:
	return error;
}

/* end of readdir & getdents */

int cp_compat_stat(struct kstat *stat, struct compat_stat *statbuf)
{
	int err;

	if (stat->size > MAX_NON_LFS || !old_valid_dev(stat->dev) ||
	    !old_valid_dev(stat->rdev))
		return -EOVERFLOW;

	err  = put_user(old_encode_dev(stat->dev), &statbuf->st_dev);
	err |= put_user(stat->ino, &statbuf->st_ino);
	err |= put_user(stat->mode, &statbuf->st_mode);
	err |= put_user(stat->nlink, &statbuf->st_nlink);
	err |= put_user(high2lowuid(stat->uid), &statbuf->st_uid);
	err |= put_user(high2lowgid(stat->gid), &statbuf->st_gid);
	err |= put_user(old_encode_dev(stat->rdev), &statbuf->st_rdev);
	err |= put_user(stat->size, &statbuf->st_size);
	err |= put_user(stat->atime.tv_sec, &statbuf->st_atime);
	err |= put_user(0, &statbuf->__unused1);
	err |= put_user(stat->mtime.tv_sec, &statbuf->st_mtime);
	err |= put_user(0, &statbuf->__unused2);
	err |= put_user(stat->ctime.tv_sec, &statbuf->st_ctime);
	err |= put_user(0, &statbuf->__unused3);
	err |= put_user(stat->blksize, &statbuf->st_blksize);
	err |= put_user(stat->blocks, &statbuf->st_blocks);
	err |= put_user(0, &statbuf->__unused4[0]);
	err |= put_user(0, &statbuf->__unused4[1]);

	return err;
}

asmlinkage int sys32_sysfs(int option, u32 arg1, u32 arg2)
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
	char _f[20-2*sizeof(int)-sizeof(int)];
};

asmlinkage int sys32_sysinfo(struct sysinfo32 *info)
{
	struct sysinfo s;
	int ret, err;
	int bitcount = 0;
	mm_segment_t old_fs = get_fs ();
	
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
	err |= __put_user (s.totalhigh, &info->totalhigh);
	err |= __put_user (s.freehigh, &info->freehigh);
	err |= __put_user (s.mem_unit, &info->mem_unit);
	if (err)
		return -EFAULT;
	return ret;
}

asmlinkage int sys32_sched_rr_get_interval(compat_pid_t pid, struct compat_timespec *interval)
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

asmlinkage int sys32_rt_sigprocmask(int how, compat_sigset_t *set, compat_sigset_t *oset, compat_size_t sigsetsize)
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
	ret = sys_rt_sigprocmask(how, set ? &s : NULL, oset ? &s : NULL, sigsetsize);
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

asmlinkage int sys32_rt_sigpending(compat_sigset_t *set, compat_size_t sigsetsize)
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

asmlinkage int
sys32_rt_sigtimedwait(compat_sigset_t *uthese, siginfo_t32 *uinfo,
		      struct compat_timespec *uts, compat_size_t sigsetsize)
{
	int ret, sig;
	sigset_t these;
	compat_sigset_t these32;
	struct timespec ts;
	siginfo_t info;
	long timeout = 0;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user (&these32, uthese, sizeof(compat_sigset_t)))
		return -EFAULT;

	switch (_NSIG_WORDS) {
	case 4: these.sig[3] = these32.sig[6] | (((long)these32.sig[7]) << 32);
	case 3: these.sig[2] = these32.sig[4] | (((long)these32.sig[5]) << 32);
	case 2: these.sig[1] = these32.sig[2] | (((long)these32.sig[3]) << 32);
	case 1: these.sig[0] = these32.sig[0] | (((long)these32.sig[1]) << 32);
	}
		
	/*
	 * Invert the set of allowed signals to get those we
	 * want to block.
	 */
	sigdelsetmask(&these, sigmask(SIGKILL)|sigmask(SIGSTOP));
	signotset(&these);

	if (uts) {
		if (get_compat_timespec(&ts, uts))
			return -EINVAL;
		if (ts.tv_nsec >= 1000000000L || ts.tv_nsec < 0
		    || ts.tv_sec < 0)
			return -EINVAL;
	}

	spin_lock_irq(&current->sighand->siglock);
	sig = dequeue_signal(current, &these, &info);
	if (!sig) {
		timeout = MAX_SCHEDULE_TIMEOUT;
		if (uts)
			timeout = (timespec_to_jiffies(&ts)
				   + (ts.tv_sec || ts.tv_nsec));

		if (timeout) {
			/* None ready -- temporarily unblock those we're
			 * interested while we are sleeping in so that we'll
			 * be awakened when they arrive.  */
			current->real_blocked = current->blocked;
			sigandsets(&current->blocked, &current->blocked, &these);
			recalc_sigpending();
			spin_unlock_irq(&current->sighand->siglock);

			current->state = TASK_INTERRUPTIBLE;
			timeout = schedule_timeout(timeout);

			spin_lock_irq(&current->sighand->siglock);
			sig = dequeue_signal(current, &these, &info);
			current->blocked = current->real_blocked;
			siginitset(&current->real_blocked, 0);
			recalc_sigpending();
		}
	}
	spin_unlock_irq(&current->sighand->siglock);

	if (sig) {
		ret = sig;
		if (uinfo) {
			if (copy_siginfo_to_user32(uinfo, &info))
				ret = -EFAULT;
		}
	} else {
		ret = -EAGAIN;
		if (timeout)
			ret = -EINTR;
	}

	return ret;
}

asmlinkage int
sys32_rt_sigqueueinfo(int pid, int sig, siginfo_t32 *uinfo)
{
	siginfo_t info;
	int ret;
	mm_segment_t old_fs = get_fs();
	
	if (copy_from_user (&info, uinfo, 3*sizeof(int)) ||
	    copy_from_user (info._sifields._pad, uinfo->_sifields._pad, SI_PAD_SIZE))
		return -EFAULT;
	set_fs (KERNEL_DS);
	ret = sys_rt_sigqueueinfo(pid, sig, &info);
	set_fs (old_fs);
	return ret;
}

asmlinkage int sys32_sigaction (int sig, struct old_sigaction32 *act, struct old_sigaction32 *oact)
{
        struct k_sigaction new_ka, old_ka;
        int ret;

	if (sig < 0) {
		set_thread_flag(TIF_NEWSIGNALS);
		sig = -sig;
	}

        if (act) {
		compat_old_sigset_t mask;
		u32 u_handler, u_restorer;
		
		ret = get_user(u_handler, &act->sa_handler);
		new_ka.sa.sa_handler = (void *) (long) u_handler;
		ret |= __get_user(u_restorer, &act->sa_restorer);
		new_ka.sa.sa_restorer = (void *) (long) u_restorer;
		ret |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		ret |= __get_user(mask, &act->sa_mask);
		if (ret)
			return ret;
		new_ka.ka_restorer = NULL;
		siginitset(&new_ka.sa.sa_mask, mask);
        }

        ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		ret = put_user((long)old_ka.sa.sa_handler, &oact->sa_handler);
		ret |= __put_user((long)old_ka.sa.sa_restorer, &oact->sa_restorer);
		ret |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		ret |= __put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
        }

	return ret;
}

asmlinkage int
sys32_rt_sigaction(int sig, struct sigaction32 *act, struct sigaction32 *oact,
		   void *restorer, compat_size_t sigsetsize)
{
        struct k_sigaction new_ka, old_ka;
        int ret;
	compat_sigset_t set32;

        /* XXX: Don't preclude handling different sized sigset_t's.  */
        if (sigsetsize != sizeof(compat_sigset_t))
                return -EINVAL;

	/* All tasks which use RT signals (effectively) use
	 * new style signals.
	 */
	set_thread_flag(TIF_NEWSIGNALS);

        if (act) {
		u32 u_handler, u_restorer;

		new_ka.ka_restorer = restorer;
		ret = get_user(u_handler, &act->sa_handler);
		new_ka.sa.sa_handler = (void *) (long) u_handler;
		ret |= __copy_from_user(&set32, &act->sa_mask, sizeof(compat_sigset_t));
		switch (_NSIG_WORDS) {
		case 4: new_ka.sa.sa_mask.sig[3] = set32.sig[6] | (((long)set32.sig[7]) << 32);
		case 3: new_ka.sa.sa_mask.sig[2] = set32.sig[4] | (((long)set32.sig[5]) << 32);
		case 2: new_ka.sa.sa_mask.sig[1] = set32.sig[2] | (((long)set32.sig[3]) << 32);
		case 1: new_ka.sa.sa_mask.sig[0] = set32.sig[0] | (((long)set32.sig[1]) << 32);
		}
		ret |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		ret |= __get_user(u_restorer, &act->sa_restorer);
		new_ka.sa.sa_restorer = (void *) (long) u_restorer;
                if (ret)
                	return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		switch (_NSIG_WORDS) {
		case 4: set32.sig[7] = (old_ka.sa.sa_mask.sig[3] >> 32); set32.sig[6] = old_ka.sa.sa_mask.sig[3];
		case 3: set32.sig[5] = (old_ka.sa.sa_mask.sig[2] >> 32); set32.sig[4] = old_ka.sa.sa_mask.sig[2];
		case 2: set32.sig[3] = (old_ka.sa.sa_mask.sig[1] >> 32); set32.sig[2] = old_ka.sa.sa_mask.sig[1];
		case 1: set32.sig[1] = (old_ka.sa.sa_mask.sig[0] >> 32); set32.sig[0] = old_ka.sa.sa_mask.sig[0];
		}
		ret = put_user((long)old_ka.sa.sa_handler, &oact->sa_handler);
		ret |= __copy_to_user(&oact->sa_mask, &set32, sizeof(compat_sigset_t));
		ret |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		ret |= __put_user((long)old_ka.sa.sa_restorer, &oact->sa_restorer);
		if (ret)
			ret = -EFAULT;
        }

        return ret;
}

/*
 * sparc32_execve() executes a new program after the asm stub has set
 * things up for us.  This should basically do what I want it to.
 */
asmlinkage int sparc32_execve(struct pt_regs *regs)
{
        int error, base = 0;
        char *filename;

	/* User register window flush is done by entry.S */

        /* Check for indirect call. */
        if((u32)regs->u_regs[UREG_G1] == 0)
                base = 1;

        filename = getname((char *)AA(regs->u_regs[base + UREG_I0]));
	error = PTR_ERR(filename);
        if(IS_ERR(filename))
                goto out;
        error = compat_do_execve(filename,
        	compat_ptr((u32)regs->u_regs[base + UREG_I1]),
        	compat_ptr((u32)regs->u_regs[base + UREG_I2]), regs);
        putname(filename);

	if(!error) {
		fprs_write(0);
		current_thread_info()->xfsr[0] = 0;
		current_thread_info()->fpsaved[0] = 0;
		regs->tstate &= ~TSTATE_PEF;
		current->ptrace &= ~PT_DTRACE;
	}
out:
        return error;
}

#ifdef CONFIG_MODULES

asmlinkage int sys32_init_module(void *umod, u32 len, const char *uargs)
{
	return sys_init_module(umod, len, uargs);
}

asmlinkage int sys32_delete_module(const char *name_user, unsigned int flags)
{
	return sys_delete_module(name_user, flags);
}

#else /* CONFIG_MODULES */

asmlinkage int
sys32_init_module(const char *name_user, struct module *mod_user)
{
	return -ENOSYS;
}

asmlinkage int
sys32_delete_module(const char *name_user)
{
	return -ENOSYS;
}

#endif  /* CONFIG_MODULES */

/* Translations due to time_t size differences.  Which affects all
   sorts of things, like timeval and itimerval.  */

extern struct timezone sys_tz;

asmlinkage int sys32_gettimeofday(struct compat_timeval *tv, struct timezone *tz)
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

static inline long get_ts32(struct timespec *o, struct compat_timeval *i)
{
	long usec;

	if (!access_ok(VERIFY_READ, i, sizeof(*i)))
		return -EFAULT;
	if (__get_user(o->tv_sec, &i->tv_sec))
		return -EFAULT;
	if (__get_user(usec, &i->tv_usec))
		return -EFAULT;
	o->tv_nsec = usec * 1000;
	return 0;
}

asmlinkage int sys32_settimeofday(struct compat_timeval *tv, struct timezone *tz)
{
	struct timespec kts;
	struct timezone ktz;

 	if (tv) {
		if (get_ts32(&kts, tv))
			return -EFAULT;
	}
	if (tz) {
		if (copy_from_user(&ktz, tz, sizeof(ktz)))
			return -EFAULT;
	}

	return do_sys_settimeofday(tv ? &kts : NULL, tz ? &ktz : NULL);
}

asmlinkage int sys32_utimes(char *filename, struct compat_timeval *tvs)
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
		ret = do_utimes(kfilename, (tvs ? &ktvs[0] : NULL));
		set_fs(old_fs);

		putname(kfilename);
	}
	return ret;
}

/* These are here just in case some old sparc32 binary calls it. */
asmlinkage int sys32_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

/* PCI config space poking. */

asmlinkage int sys32_pciconfig_read(u32 bus, u32 dfn, u32 off, u32 len, u32 ubuf)
{
	return sys_pciconfig_read((unsigned long) bus,
				  (unsigned long) dfn,
				  (unsigned long) off,
				  (unsigned long) len,
				  (unsigned char *)AA(ubuf));
}

asmlinkage int sys32_pciconfig_write(u32 bus, u32 dfn, u32 off, u32 len, u32 ubuf)
{
	return sys_pciconfig_write((unsigned long) bus,
				   (unsigned long) dfn,
				   (unsigned long) off,
				   (unsigned long) len,
				   (unsigned char *)AA(ubuf));
}

asmlinkage int sys32_prctl(int option, u32 arg2, u32 arg3, u32 arg4, u32 arg5)
{
	return sys_prctl(option,
			 (unsigned long) arg2,
			 (unsigned long) arg3,
			 (unsigned long) arg4,
			 (unsigned long) arg5);
}


asmlinkage compat_ssize_t sys32_pread64(unsigned int fd, char *ubuf,
				   compat_size_t count, u32 poshi, u32 poslo)
{
	return sys_pread64(fd, ubuf, count, ((loff_t)AA(poshi) << 32) | AA(poslo));
}

asmlinkage compat_ssize_t sys32_pwrite64(unsigned int fd, char *ubuf,
				    compat_size_t count, u32 poshi, u32 poslo)
{
	return sys_pwrite64(fd, ubuf, count, ((loff_t)AA(poshi) << 32) | AA(poslo));
}

asmlinkage compat_ssize_t sys32_readahead(int fd, u32 offhi, u32 offlo, s32 count)
{
	return sys_readahead(fd, ((loff_t)AA(offhi) << 32) | AA(offlo), count);
}

long sys32_fadvise64(int fd, u32 offhi, u32 offlo, s32 len, int advice)
{
	return sys_fadvise64_64(fd, ((loff_t)AA(offhi)<<32)|AA(offlo), len, advice);
}

long sys32_fadvise64_64(int fd, u32 offhi, u32 offlo, u32 lenhi, u32 lenlo, int advice)
{
	return sys_fadvise64_64(fd, ((loff_t)AA(offhi)<<32)|AA(offlo),
				((loff_t)AA(lenhi)<<32)|AA(lenlo), advice);
}

asmlinkage int sys32_sendfile(int out_fd, int in_fd, compat_off_t *offset, s32 count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	off_t of;
	
	if (offset && get_user(of, offset))
		return -EFAULT;
		
	set_fs(KERNEL_DS);
	ret = sys_sendfile(out_fd, in_fd, offset ? &of : NULL, count);
	set_fs(old_fs);
	
	if (offset && put_user(of, offset))
		return -EFAULT;
		
	return ret;
}

asmlinkage int sys32_sendfile64(int out_fd, int in_fd, compat_loff_t *offset, s32 count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	loff_t lof;
	
	if (offset && get_user(lof, offset))
		return -EFAULT;
		
	set_fs(KERNEL_DS);
	ret = sys_sendfile64(out_fd, in_fd, offset ? &lof : NULL, count);
	set_fs(old_fs);
	
	if (offset && put_user(lof, offset))
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

asmlinkage int sys32_adjtimex(struct timex32 *utp)
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

/* This is just a version for 32-bit applications which does
 * not force O_LARGEFILE on.
 */

asmlinkage long sparc32_open(const char * filename, int flags, int mode)
{
	char * tmp;
	int fd, error;

	tmp = getname(filename);
	fd = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {
		fd = get_unused_fd();
		if (fd >= 0) {
			struct file * f = filp_open(tmp, flags, mode);
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

extern unsigned long do_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, unsigned long new_addr);
                
asmlinkage unsigned long sys32_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags, u32 __new_addr)
{
	struct vm_area_struct *vma;
	unsigned long ret = -EINVAL;
	unsigned long new_addr = AA(__new_addr);

	if (old_len > 0xf0000000UL || new_len > 0xf0000000UL)
		goto out;
	if (addr > 0xf0000000UL - old_len)
		goto out;
	down_write(&current->mm->mmap_sem);
	if (flags & MREMAP_FIXED) {
		if (new_addr > 0xf0000000UL - new_len)
			goto out_sem;
	} else if (addr > 0xf0000000UL - new_len) {
		unsigned long map_flags = 0;
		struct file *file = NULL;

		ret = -ENOMEM;
		if (!(flags & MREMAP_MAYMOVE))
			goto out_sem;

		vma = find_vma(current->mm, addr);
		if (vma) {
			if (vma->vm_flags & VM_SHARED)
				map_flags |= MAP_SHARED;
			file = vma->vm_file;
		}

		/* MREMAP_FIXED checked above. */
		new_addr = get_unmapped_area(file, addr, new_len,
				    vma ? vma->vm_pgoff : 0,
				    map_flags);
		ret = new_addr;
		if (new_addr & ~PAGE_MASK)
			goto out_sem;
		flags |= MREMAP_FIXED;
	}
	ret = do_mremap(addr, old_len, new_len, flags, new_addr);
out_sem:
	up_write(&current->mm->mmap_sem);
out:
	return ret;       
}

asmlinkage int sys_setpriority32(u32 which, u32 who, u32 niceval)
{
	return sys_setpriority((int) which,
			       (int) who,
			       (int) niceval);
}

struct __sysctl_args32 {
	u32 name;
	int nlen;
	u32 oldval;
	u32 oldlenp;
	u32 newval;
	u32 newlen;
	u32 __unused[4];
};

asmlinkage long sys32_sysctl(struct __sysctl_args32 *args)
{
#ifndef CONFIG_SYSCTL
	return -ENOSYS;
#else
	struct __sysctl_args32 tmp;
	int error;
	size_t oldlen, *oldlenp = NULL;
	unsigned long addr = (((long)&args->__unused[0]) + 7) & ~7;

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	if (tmp.oldval && tmp.oldlenp) {
		/* Duh, this is ugly and might not work if sysctl_args
		   is in read-only memory, but do_sysctl does indirectly
		   a lot of uaccess in both directions and we'd have to
		   basically copy the whole sysctl.c here, and
		   glibc's __sysctl uses rw memory for the structure
		   anyway.  */
		if (get_user(oldlen, (u32 *)A(tmp.oldlenp)) ||
		    put_user(oldlen, (size_t *)addr))
			return -EFAULT;
		oldlenp = (size_t *)addr;
	}

	lock_kernel();
	error = do_sysctl((int *)A(tmp.name), tmp.nlen, (void *)A(tmp.oldval),
			  oldlenp, (void *)A(tmp.newval), tmp.newlen);
	unlock_kernel();
	if (oldlenp) {
		if (!error) {
			if (get_user(oldlen, (size_t *)addr) ||
			    put_user(oldlen, (u32 *)A(tmp.oldlenp)))
				error = -EFAULT;
		}
		copy_to_user(args->__unused, tmp.__unused, sizeof(tmp.__unused));
	}
	return error;
#endif
}

long sys32_lookup_dcookie(u32 cookie_high, u32 cookie_low, char *buf, size_t len)
{
	return sys_lookup_dcookie((u64)cookie_high << 32 | cookie_low,
				  buf, len);
}

extern asmlinkage long
sys_timer_create(clockid_t which_clock, struct sigevent *timer_event_spec,
		 timer_t * created_timer_id);

long
sys32_timer_create(u32 clock, struct sigevent32 *se32, timer_t *timer_id)
{
	struct sigevent se;
	mm_segment_t oldfs;
	timer_t t;
	long err;

	if (se32 == NULL)
		return sys_timer_create(clock, NULL, timer_id);

	memset(&se, 0, sizeof(struct sigevent));
	if (get_user(se.sigev_value.sival_int,  &se32->sigev_value.sival_int) ||
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


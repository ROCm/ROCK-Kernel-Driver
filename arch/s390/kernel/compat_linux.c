/*
 *  arch/s390x/kernel/linux32.c
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *               Gerhard Tonn (ton@de.ibm.com)   
 *
 *  Conversion between 31bit and 64bit native syscalls.
 *
 * Heavily inspired by the 32-bit Sparc compat code which is 
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 *
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
#include <linux/filter.h>
#include <linux/highmem.h>
#include <linux/highuid.h>
#include <linux/mman.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/icmpv6.h>
#include <linux/sysctl.h>
#include <linux/binfmts.h>
#include <linux/compat.h>
#include <linux/vfs.h>
#include <linux/ptrace.h>

#include <asm/types.h>
#include <asm/ipc.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>

#include <net/scm.h>
#include <net/sock.h>

#include "compat_linux.h"

extern asmlinkage long sys_chown(const char *, uid_t,gid_t);
extern asmlinkage long sys_lchown(const char *, uid_t,gid_t);
extern asmlinkage long sys_fchown(unsigned int, uid_t,gid_t);
extern asmlinkage long sys_setregid(gid_t, gid_t);
extern asmlinkage long sys_setgid(gid_t);
extern asmlinkage long sys_setreuid(uid_t, uid_t);
extern asmlinkage long sys_setuid(uid_t);
extern asmlinkage long sys_setresuid(uid_t, uid_t, uid_t);
extern asmlinkage long sys_setresgid(gid_t, gid_t, gid_t);
extern asmlinkage long sys_setfsuid(uid_t);
extern asmlinkage long sys_setfsgid(gid_t);
 
/* For this source file, we want overflow handling. */

#undef high2lowuid
#undef high2lowgid
#undef low2highuid
#undef low2highgid
#undef SET_UID16
#undef SET_GID16
#undef NEW_TO_OLD_UID
#undef NEW_TO_OLD_GID
#undef SET_OLDSTAT_UID
#undef SET_OLDSTAT_GID
#undef SET_STAT_UID
#undef SET_STAT_GID

#define high2lowuid(uid) ((uid) > 65535) ? (u16)overflowuid : (u16)(uid)
#define high2lowgid(gid) ((gid) > 65535) ? (u16)overflowgid : (u16)(gid)
#define low2highuid(uid) ((uid) == (u16)-1) ? (uid_t)-1 : (uid_t)(uid)
#define low2highgid(gid) ((gid) == (u16)-1) ? (gid_t)-1 : (gid_t)(gid)
#define SET_UID16(var, uid)	var = high2lowuid(uid)
#define SET_GID16(var, gid)	var = high2lowgid(gid)
#define NEW_TO_OLD_UID(uid)	high2lowuid(uid)
#define NEW_TO_OLD_GID(gid)	high2lowgid(gid)
#define SET_OLDSTAT_UID(stat, uid)	(stat).st_uid = high2lowuid(uid)
#define SET_OLDSTAT_GID(stat, gid)	(stat).st_gid = high2lowgid(gid)
#define SET_STAT_UID(stat, uid)		(stat).st_uid = high2lowuid(uid)
#define SET_STAT_GID(stat, gid)		(stat).st_gid = high2lowgid(gid)

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

asmlinkage long sys32_getgroups16(int gidsetsize, u16 *grouplist)
{
	u16 groups[NGROUPS];
	int i,j;

	if (gidsetsize < 0)
		return -EINVAL;
	i = current->ngroups;
	if (gidsetsize) {
		if (i > gidsetsize)
			return -EINVAL;
		for(j=0;j<i;j++)
			groups[j] = current->groups[j];
		if (copy_to_user(grouplist, groups, sizeof(u16)*i))
			return -EFAULT;
	}
	return i;
}

asmlinkage long sys32_setgroups16(int gidsetsize, u16 *grouplist)
{
	u16 groups[NGROUPS];
	int i;

	if (!capable(CAP_SETGID))
		return -EPERM;
	if ((unsigned) gidsetsize > NGROUPS)
		return -EINVAL;
	if (copy_from_user(groups, grouplist, gidsetsize * sizeof(u16)))
		return -EFAULT;
	for (i = 0 ; i < gidsetsize ; i++)
		current->groups[i] = (gid_t)groups[i];
	current->ngroups = gidsetsize;
	return 0;
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

static inline long get_tv32(struct timeval *o, struct compat_timeval *i)
{
	return (!access_ok(VERIFY_READ, tv32, sizeof(*tv32)) ||
		(__get_user(o->tv_sec, &i->tv_sec) ||
		 __get_user(o->tv_usec, &i->tv_usec)));
}

static inline long put_tv32(struct compat_timeval *o, struct timeval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->tv_sec, &o->tv_sec) ||
		 __put_user(i->tv_usec, &o->tv_usec)));
}

struct msgbuf32 { s32 mtype; char mtext[1]; };

struct ipc64_perm_ds32
{
        __kernel_key_t          key;
        __kernel_uid32_t        uid;
        __kernel_gid32_t        gid;
        __kernel_uid32_t        cuid;
        __kernel_gid32_t        cgid;
        compat_mode_t       mode;
        unsigned short          __pad1;
        unsigned short          seq;
        unsigned short          __pad2;
        unsigned int            __unused1;
        unsigned int            __unused2;
};

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
	struct ipc64_perm_ds32 sem_perm;
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
	struct ipc64_perm_ds32 msg_perm;
	unsigned int   __pad1;
	compat_time_t msg_stime;
	unsigned int   __pad2;
	compat_time_t msg_rtime;
	unsigned int   __pad3;
	compat_time_t msg_ctime;
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
	struct ipc64_perm_ds32	shm_perm;
	compat_size_t	shm_segsz;
	compat_time_t  	shm_atime;
	unsigned int		__unused1;
	compat_time_t  	shm_dtime;
	unsigned int		__unused2;
	compat_time_t  	shm_ctime;
	unsigned int		__unused3;
	compat_pid_t	shm_cpid;
	compat_pid_t	shm_lpid;
	unsigned int		shm_nattch;
	unsigned int		__unused4;
	unsigned int		__unused5;
};

extern int sem_ctls[];
#define sc_semopm	(sem_ctls[2])
#define SEMOPM_FAST	64  /* ~ 372 bytes on stack */

static long
do_sys32_semtimedop (int semid, struct sembuf *tsops, int nsops,
		     struct compat_timespec *timeout32)
{
	struct sembuf *sops, fast_sops[SEMOPM_FAST];
	struct timespec t;
	mm_segment_t oldfs;
	long ret;

	/* parameter checking precedence should mirror sys_semtimedop() */
	if (nsops < 1 || semid < 0)
		return -EINVAL;
	if (nsops > sc_semopm)
		return -E2BIG;
	if (nsops <= SEMOPM_FAST)
		sops = fast_sops;
	else {
		sops = kmalloc(nsops * sizeof(*sops), GFP_KERNEL);
		if (sops == NULL)
			return -ENOMEM;
	}
	if (copy_from_user(sops, tsops, nsops * sizeof(*tsops)) ||
	    get_compat_timespec(&t, timeout32))
		ret = -EFAULT;
	else {
		oldfs = get_fs();
		set_fs(KERNEL_DS);
		ret = sys_semtimedop(semid, sops, nsops, &t);
		set_fs(oldfs);
	}
	if (sops != fast_sops)
		kfree(sops);
	return ret;
}

#define IPCOP_MASK(__x)	(1UL << (__x))
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
	if(third == SETVAL)
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

	err = -EINVAL;
	if (second > MSGMAX || first < 0 || second < 0)
		goto out;

	err = -EFAULT;
	if (!uptr)
		goto out;
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

	if (first < 0 || second < 0)
		return -EINVAL;

	if (!version) {
		struct ipc_kludge_32 *uipck = (struct ipc_kludge_32 *)uptr;
		struct ipc_kludge_32 ipck;

		err = -EINVAL;
		if (!uptr)
			goto out;
		err = -EFAULT;
		if (copy_from_user (&ipck, uipck, sizeof (struct ipc_kludge_32)))
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
	err = sys_shmat (first, uptr, second, &raddr);
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
			int err2 = put_user (s.shm_perm.key, &up->shm_perm.key);
			err2 |= __put_user (high2lowuid(s.shm_perm.uid), &up->shm_perm.uid);
			err2 |= __put_user (high2lowgid(s.shm_perm.gid), &up->shm_perm.gid);
			err2 |= __put_user (high2lowuid(s.shm_perm.cuid), &up->shm_perm.cuid);
			err2 |= __put_user (high2lowgid(s.shm_perm.cgid), &up->shm_perm.cgid);
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
			err2 |= __put_user (high2lowgid(s.shm_perm.gid), &up->shm_perm.gid);
			err2 |= __put_user (high2lowuid(s.shm_perm.cuid), &up->shm_perm.cuid);
			err2 |= __put_user (high2lowgid(s.shm_perm.cgid), &up->shm_perm.cgid);
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

/*
 * sys32_ipc() is the de-multiplexer for the SysV IPC calls in 32bit emulation.
 *
 * This is really horribly ugly.
 */
asmlinkage int sys32_ipc (u32 call, int first, int second, int third, u32 ptr)
{
	int version, err;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	if(version)
		return -EINVAL;

	if (call <= SEMTIMEDOP)
		switch (call) {
		case SEMTIMEDOP:
			if (third) {
				err = do_sys32_semtimedop(first,
					(struct sembuf *)AA(ptr),
					second,
					(struct compat_timespec *)
						AA((u32)third));
				goto out;
			}
			/* else fall through for normal semop() */
		case SEMOP:
			/* struct sembuf is the same on 32 and 64bit :)) */
			err = sys_semtimedop (first, (struct sembuf *)AA(ptr),
					      second, NULL);
			goto out;
		case SEMGET:
			err = sys_semget (first, second, third);
			goto out;
		case SEMCTL:
			err = do_sys32_semctl (first, second, third, (void *)AA(ptr));
			goto out;
		default:
			err = -EINVAL;
			goto out;
		};
	if (call <= MSGCTL) 
		switch (call) {
		case MSGSND:
			err = do_sys32_msgsnd (first, second, third, (void *)AA(ptr));
			goto out;
		case MSGRCV:
			err = do_sys32_msgrcv (first, second, 0, third,
					       version, (void *)AA(ptr));
			goto out;
		case MSGGET:
			err = sys_msgget ((key_t) first, second);
			goto out;
		case MSGCTL:
			err = do_sys32_msgctl (first, second, (void *)AA(ptr));
			goto out;
		default:
			err = -EINVAL;
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
			err = -EINVAL;
			goto out;
		}

	err = -EINVAL;

out:
	return err;
}

extern asmlinkage long sys_truncate(const char * path, unsigned long length);
extern asmlinkage long sys_ftruncate(unsigned int fd, unsigned long length);

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

typedef ssize_t (*io_fn_t)(struct file *, char *, size_t, loff_t *);
typedef ssize_t (*iov_fn_t)(struct file *, const struct iovec *, unsigned long, loff_t *);

static long do_readv_writev32(int type, struct file *file,
			      const struct compat_iovec *vector, u32 count)
{
	unsigned long tot_len;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov=iovstack, *ivp;
	struct inode *inode;
	long retval, i;
	io_fn_t fn;
	iov_fn_t fnv;

	/* First get the "struct iovec" from user memory and
	 * verify all the pointers
	 */
	if (!count)
		return 0;
	if (verify_area(VERIFY_READ, vector, sizeof(struct compat_iovec)*count))
		return -EFAULT;
	if (count > UIO_MAXIOV)
		return -EINVAL;
	if (count > UIO_FASTIOV) {
		iov = kmalloc(count*sizeof(struct iovec), GFP_KERNEL);
		if (!iov)
			return -ENOMEM;
	}

	tot_len = 0;
	i = count;
	ivp = iov;
	while(i > 0) {
		u32 len;
		u32 buf;

		__get_user(len, &vector->iov_len);
		__get_user(buf, &vector->iov_base);
		tot_len += len;
		ivp->iov_base = (void *)A(buf);
		ivp->iov_len = (__kernel_size_t) len;
		vector++;
		ivp++;
		i--;
	}

	inode = file->f_dentry->d_inode;
	/* VERIFY_WRITE actually means a read, as we write to user space */
	retval = locks_verify_area((type == VERIFY_WRITE
				    ? FLOCK_VERIFY_READ : FLOCK_VERIFY_WRITE),
				   inode, file, file->f_pos, tot_len);
	if (retval)
		goto out;

	/* VERIFY_WRITE actually means a read, as we write to user space */
	fnv = (type == VERIFY_WRITE ? file->f_op->readv : file->f_op->writev);
	if (fnv) {
		retval = fnv(file, iov, count, &file->f_pos);
		goto out;
	}

	fn = (type == VERIFY_WRITE ? file->f_op->read :
	      (io_fn_t) file->f_op->write);

	ivp = iov;
	while (count > 0) {
		void * base;
		int len, nr;

		base = ivp->iov_base;
		len = ivp->iov_len;
		ivp++;
		count--;
		nr = fn(file, base, len, &file->f_pos);
		if (nr < 0) {
			if (!retval)
				retval = nr;
			break;
		}
		retval += nr;
		if (nr != len)
			break;
	}
out:
	if (iov != iovstack)
		kfree(iov);

	return retval;
}

asmlinkage long sys32_readv(int fd, struct compat_iovec *vector, u32 count)
{
	struct file *file;
	long ret = -EBADF;

	file = fget(fd);
	if(!file)
		goto bad_file;

	if (file->f_op && (file->f_mode & FMODE_READ) &&
	    (file->f_op->readv || file->f_op->read))
		ret = do_readv_writev32(VERIFY_WRITE, file, vector, count);
	fput(file);

bad_file:
	return ret;
}

asmlinkage long sys32_writev(int fd, struct compat_iovec *vector, u32 count)
{
	struct file *file;
	int ret = -EBADF;

	file = fget(fd);
	if(!file)
		goto bad_file;
	if (file->f_op && (file->f_mode & FMODE_WRITE) &&
	    (file->f_op->writev || file->f_op->write))
		ret = do_readv_writev32(VERIFY_READ, file, vector, count);
	fput(file);

bad_file:
	return ret;
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
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1);

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

#define MAX_SELECT_SECONDS \
	((unsigned long) (MAX_SCHEDULE_TIMEOUT / HZ)-1)

asmlinkage int sys32_select(int n, u32 *inp, u32 *outp, u32 *exp, u32 tvp_x)
{
	fd_set_bits fds;
	struct compat_timeval *tvp = (struct compat_timeval *)AA(tvp_x);
	char *bits;
	unsigned long nn;
	long timeout;
	int ret, size;

	timeout = MAX_SCHEDULE_TIMEOUT;
	if (tvp) {
		int sec, usec;

		if ((ret = verify_area(VERIFY_READ, tvp, sizeof(*tvp)))
		    || (ret = __get_user(sec, &tvp->tv_sec))
		    || (ret = __get_user(usec, &tvp->tv_usec)))
			goto out_nofds;

		ret = -EINVAL;
		if(sec < 0 || usec < 0)
			goto out_nofds;

		if ((unsigned long) sec < MAX_SELECT_SECONDS) {
			timeout = (usec + 1000000/HZ - 1) / (1000000/HZ);
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

	nn = (n + 8*sizeof(u32) - 1) / (8*sizeof(u32));
	if ((ret = get_fd_set32(nn, fds.in, inp)) ||
	    (ret = get_fd_set32(nn, fds.out, outp)) ||
	    (ret = get_fd_set32(nn, fds.ex, exp)))
		goto out;
	zero_fd_set(n, fds.res_in);
	zero_fd_set(n, fds.res_out);
	zero_fd_set(n, fds.res_ex);

	ret = do_select(n, &fds, &timeout);

	if (tvp && !(current->personality & STICKY_TIMEOUTS)) {
		int sec = 0, usec = 0;
		if (timeout) {
			sec = timeout / HZ;
			usec = timeout % HZ;
			usec *= (1000000/HZ);
		}
		put_user(sec, &tvp->tv_sec);
		put_user(usec, &tvp->tv_usec);
	}

	if (ret < 0)
		goto out;
	if (!ret) {
		ret = -ERESTARTNOHAND;
		if (signal_pending(current))
			goto out;
		ret = 0;
	}

	set_fd_set32(nn, inp, fds.res_in);
	set_fd_set32(nn, outp, fds.res_out);
	set_fd_set32(nn, exp, fds.res_ex);

out:
	kfree(bits);
out_nofds:
	return ret;
}

int cp_compat_stat(struct kstat *stat, struct compat_stat *statbuf)
{
	int err;

	if (!old_valid_dev(stat->dev) || !old_valid_dev(stat->rdev))
		return -EOVERFLOW;

	err = put_user(old_encode_dev(stat->dev), &statbuf->st_dev);
	err |= put_user(stat->ino, &statbuf->st_ino);
	err |= put_user(stat->mode, &statbuf->st_mode);
	err |= put_user(stat->nlink, &statbuf->st_nlink);
	err |= put_user(high2lowuid(stat->uid), &statbuf->st_uid);
	err |= put_user(high2lowgid(stat->gid), &statbuf->st_gid);
	err |= put_user(old_encode_dev(stat->rdev), &statbuf->st_rdev);
	err |= put_user(stat->size, &statbuf->st_size);
	err |= put_user(stat->atime.tv_sec, &statbuf->st_atime);
	err |= put_user(stat->atime.tv_nsec, &statbuf->st_atime_nsec);
	err |= put_user(stat->mtime.tv_sec, &statbuf->st_mtime);
	err |= put_user(stat->mtime.tv_nsec, &statbuf->st_mtime_nsec);
	err |= put_user(stat->ctime.tv_sec, &statbuf->st_ctime);
	err |= put_user(stat->ctime.tv_nsec, &statbuf->st_ctime_nsec);
	err |= put_user(stat->blksize, &statbuf->st_blksize);
	err |= put_user(stat->blocks, &statbuf->st_blocks);
/* fixme
	err |= put_user(0, &statbuf->__unused4[0]);
	err |= put_user(0, &statbuf->__unused4[1]);
*/
	return err;
}

extern asmlinkage int sys_sysfs(int option, unsigned long arg1, unsigned long arg2);

asmlinkage int sys32_sysfs(int option, u32 arg1, u32 arg2)
{
	return sys_sysfs(option, arg1, arg2);
}

struct ncp_mount_data32 {
        int version;
        unsigned int ncp_fd;
        compat_uid_t mounted_uid;
        compat_pid_t wdog_pid;
        unsigned char mounted_vol[NCP_VOLNAME_LEN + 1];
        unsigned int time_out;
        unsigned int retry_count;
        unsigned int flags;
        compat_uid_t uid;
        compat_gid_t gid;
        compat_mode_t file_mode;
        compat_mode_t dir_mode;
};

static void *do_ncp_super_data_conv(void *raw_data)
{
	struct ncp_mount_data *n = (struct ncp_mount_data *)raw_data;
	struct ncp_mount_data32 *n32 = (struct ncp_mount_data32 *)raw_data;

	n->dir_mode = n32->dir_mode;
	n->file_mode = n32->file_mode;
	n->gid = low2highgid(n32->gid);
	n->uid = low2highuid(n32->uid);
	memmove (n->mounted_vol, n32->mounted_vol, (sizeof (n32->mounted_vol) + 3 * sizeof (unsigned int)));
	n->wdog_pid = n32->wdog_pid;
	n->mounted_uid = low2highuid(n32->mounted_uid);
	return raw_data;
}

struct smb_mount_data32 {
        int version;
        compat_uid_t mounted_uid;
        compat_uid_t uid;
        compat_gid_t gid;
        compat_mode_t file_mode;
        compat_mode_t dir_mode;
};

static void *do_smb_super_data_conv(void *raw_data)
{
	struct smb_mount_data *s = (struct smb_mount_data *)raw_data;
	struct smb_mount_data32 *s32 = (struct smb_mount_data32 *)raw_data;

	if (s32->version != SMB_MOUNT_OLDVERSION)
		goto out;
	s->version = s32->version;
	s->mounted_uid = low2highuid(s32->mounted_uid);
	s->uid = low2highuid(s32->uid);
	s->gid = low2highgid(s32->gid);
	s->file_mode = s32->file_mode;
	s->dir_mode = s32->dir_mode;
out:
	return raw_data;
}

static int copy_mount_stuff_to_kernel(const void *user, unsigned long *kernel)
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

#define SMBFS_NAME	"smbfs"
#define NCPFS_NAME	"ncpfs"

asmlinkage int sys32_mount(char *dev_name, char *dir_name, char *type, unsigned long new_flags, u32 data)
{
	unsigned long type_page = 0;
	unsigned long data_page = 0;
	unsigned long dev_page = 0;
	unsigned long dir_page = 0;
	int err, is_smb, is_ncp;

	is_smb = is_ncp = 0;

	err = copy_mount_stuff_to_kernel((const void *)type, &type_page);
	if (err)
		goto out;

	if (!type_page) {
		err = -EINVAL;
		goto out;
	}

	is_smb = !strcmp((char *)type_page, SMBFS_NAME);
	is_ncp = !strcmp((char *)type_page, NCPFS_NAME);

	err = copy_mount_stuff_to_kernel((const void *)AA(data), &data_page);
	if (err)
		goto type_out;

	err = copy_mount_stuff_to_kernel(dev_name, &dev_page);
	if (err)
		goto data_out;

	err = copy_mount_stuff_to_kernel(dir_name, &dir_page);
	if (err)
		goto dev_out;

	if (!is_smb && !is_ncp) {
		lock_kernel();
		err = do_mount((char*)dev_page, (char*)dir_page,
				(char*)type_page, new_flags, (char*)data_page);
		unlock_kernel();
	} else {
		if (is_ncp)
			do_ncp_super_data_conv((void *)data_page);
		else
			do_smb_super_data_conv((void *)data_page);

		lock_kernel();
		err = do_mount((char*)dev_page, (char*)dir_page,
				(char*)type_page, new_flags, (char*)data_page);
		unlock_kernel();
	}
	free_page(dir_page);

dev_out:
	free_page(dev_page);

data_out:
	free_page(data_page);

type_out:
	free_page(type_page);

out:
	return err;
}

struct rusage32 {
        struct compat_timeval ru_utime;
        struct compat_timeval ru_stime;
        s32    ru_maxrss;
        s32    ru_ixrss;
        s32    ru_idrss;
        s32    ru_isrss;
        s32    ru_minflt;
        s32    ru_majflt;
        s32    ru_nswap;
        s32    ru_inblock;
        s32    ru_oublock;
        s32    ru_msgsnd; 
        s32    ru_msgrcv; 
        s32    ru_nsignals;
        s32    ru_nvcsw;
        s32    ru_nivcsw;
};

static int put_rusage (struct rusage32 *ru, struct rusage *r)
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
	unsigned short pads;
	u32 totalhigh;
	u32 freehigh;
	unsigned int mem_unit;
        char _f[8];
};

extern asmlinkage int sys_sysinfo(struct sysinfo *info);

asmlinkage int sys32_sysinfo(struct sysinfo32 *info)
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
	err |= __put_user (s.totalhigh, &info->totalhigh);
	err |= __put_user (s.freehigh, &info->freehigh);
	err |= __put_user (s.mem_unit, &info->mem_unit);
	if (err)
		return -EFAULT;
	return ret;
}

extern asmlinkage int sys_sched_rr_get_interval(pid_t pid, struct timespec *interval);

asmlinkage int sys32_sched_rr_get_interval(compat_pid_t pid,
		struct compat_timespec *interval)
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

extern asmlinkage int sys_rt_sigprocmask(int how, sigset_t *set, sigset_t *oset, size_t sigsetsize);

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

extern asmlinkage int sys_rt_sigpending(sigset_t *set, size_t sigsetsize);

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

extern int
copy_siginfo_to_user32(siginfo_t32 *to, siginfo_t *from);

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
		/* None ready -- temporarily unblock those we're interested
		   in so that we'll be awakened when they arrive.  */
		current->real_blocked = current->blocked;
		sigandsets(&current->blocked, &current->blocked, &these);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);

		timeout = MAX_SCHEDULE_TIMEOUT;
		if (uts)
			timeout = (timespec_to_jiffies(&ts)
				   + (ts.tv_sec || ts.tv_nsec));

		current->state = TASK_INTERRUPTIBLE;
		timeout = schedule_timeout(timeout);

		spin_lock_irq(&current->sighand->siglock);
		sig = dequeue_signal(current, &these, &info);
		current->blocked = current->real_blocked;
		siginitset(&current->real_blocked, 0);
		recalc_sigpending();
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

extern asmlinkage int
sys_rt_sigqueueinfo(int pid, int sig, siginfo_t *uinfo);

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

extern void check_pending(int signum);

/*
 * count32() counts the number of arguments/envelopes
 */
static int count32(u32 * argv)
{
	int i = 0;

	if (argv != NULL) {
		for (;;) {
			u32 p; int error;

			error = get_user(p,argv);
			if (error) return error;
			if (!p) break;
			argv++; i++;
		}
	}
	return i;
}

/*
 * 'copy_string32()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 */
static int copy_strings32(int argc, u32 * argv, struct linux_binprm *bprm)
{
	while (argc-- > 0) {
		u32 str;
		int len;
		unsigned long pos;

		if (get_user(str, argv + argc) ||
		    !str ||
		    !(len = strnlen_user((char *)A(str), bprm->p)))
			return -EFAULT;

		if (bprm->p < len)
			return -E2BIG;

		bprm->p -= len;

		pos = bprm->p;
		while (len) {
			char *kaddr;
			struct page *page;
			int offset, bytes_to_copy, new, err;

			offset = pos % PAGE_SIZE;
			page = bprm->page[pos / PAGE_SIZE];
			new = 0;
			if (!page) {
				page = alloc_page(GFP_USER);
				bprm->page[pos / PAGE_SIZE] = page;
				if (!page)
					return -ENOMEM;
				new = 1;
			}
			kaddr = (char *)kmap(page);

			if (new && offset)
				memset(kaddr, 0, offset);
			bytes_to_copy = PAGE_SIZE - offset;
			if (bytes_to_copy > len) {
				bytes_to_copy = len;
				if (new)
					memset(kaddr+offset+len, 0,
					       PAGE_SIZE-offset-len);
			}

			err = copy_from_user(kaddr + offset, (char *)A(str),
					     bytes_to_copy);
			kunmap(page);

			if (err)
				return -EFAULT;

			pos += bytes_to_copy;
			str += bytes_to_copy;
			len -= bytes_to_copy;
		}
	}
	return 0;
}

/*
 * sys32_execve() executes a new program.
 */
static inline int 
do_execve32(char * filename, u32 * argv, u32 * envp, struct pt_regs * regs)
{
	struct linux_binprm bprm;
	struct file * file;
	int retval;
	int i;

	sched_balance_exec();

	file = open_exec(filename);

	retval = PTR_ERR(file);
	if (IS_ERR(file))
		return retval;

	bprm.p = PAGE_SIZE*MAX_ARG_PAGES-sizeof(void *);
	memset(bprm.page, 0, MAX_ARG_PAGES * sizeof(bprm.page[0]));

	bprm.file = file;
	bprm.filename = filename;
	bprm.interp = filename;
	bprm.sh_bang = 0;
	bprm.loader = 0;
	bprm.exec = 0;
	bprm.mm = mm_alloc();
	retval = -ENOMEM;
	if (!bprm.mm)
		goto out_file;

	/* init_new_context is empty for s390x. */

	bprm.argc = count32(argv);
	if ((retval = bprm.argc) < 0)
		goto out_mm;

	bprm.envc = count32(envp);
	if ((retval = bprm.envc) < 0)
		goto out_mm;

	retval = security_bprm_alloc(&bprm);
	if (retval)
		goto out;

	retval = prepare_binprm(&bprm);
	if (retval < 0)
		goto out;
	
	retval = copy_strings_kernel(1, &bprm.filename, &bprm);
	if (retval < 0)
		goto out;

	bprm.exec = bprm.p;
	retval = copy_strings32(bprm.envc, envp, &bprm);
	if (retval < 0)
		goto out;

	retval = copy_strings32(bprm.argc, argv, &bprm);
	if (retval < 0)
		goto out;

	retval = search_binary_handler(&bprm, regs);
	if (retval >= 0) {
		/* execve success */
		security_bprm_free(&bprm);
		return retval;
	}

out:
	/* Something went wrong, return the inode and free the argument pages*/
	for (i=0 ; i<MAX_ARG_PAGES ; i++) {
		struct page * page = bprm.page[i];
		if (page)
			__free_page(page);
	}

	if (bprm.security)
		security_bprm_free(&bprm);

out_mm:
	mmdrop(bprm.mm);

out_file:
	if (bprm.file) {
		allow_write_access(bprm.file);
		fput(bprm.file);
	}

	return retval;
}

/*
 * sys32_execve() executes a new program after the asm stub has set
 * things up for us.  This should basically do what I want it to.
 */
asmlinkage int
sys32_execve(struct pt_regs regs)
{
        int error;
        char * filename;

        filename = getname((char *)A(regs.orig_gpr2));
        error = PTR_ERR(filename);
        if (IS_ERR(filename))
                goto out;
        error = do_execve32(filename, (u32 *)A(regs.gprs[3]), (u32 *)A(regs.gprs[4]), &regs);
	if (error == 0)
	{
		current->ptrace &= ~PT_DTRACE;
		current->thread.fp_regs.fpc=0;
		__asm__ __volatile__
		        ("sr  0,0\n\t"
		         "sfpc 0,0\n\t"
			 : : :"0");
	}
        putname(filename);
out:
        return error;
}


#ifdef CONFIG_MODULES

extern asmlinkage int sys_init_module(const char *name_user, struct module *mod_user);

/* Hey, when you're trying to init module, take time and prepare us a nice 64bit
 * module structure, even if from 32bit modutils... Why to pollute kernel... :))
 */
asmlinkage int sys32_init_module(const char *name_user, struct module *mod_user)
{
	return sys_init_module(name_user, mod_user);
}

extern asmlinkage int sys_delete_module(const char *name_user);

asmlinkage int sys32_delete_module(const char *name_user)
{
	return sys_delete_module(name_user);
}

struct module_info32 {
	u32 addr;
	u32 size;
	u32 flags;
	s32 usecount;
};

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

struct nfsctl_fdparm32 {
	struct sockaddr		gd32_addr;
	s8			gd32_path[NFS_MAXPATHLEN+1];
	s32			gd32_version;
};

struct nfsctl_fsparm32 {
	struct sockaddr		gd32_addr;
	s8			gd32_path[NFS_MAXPATHLEN+1];
	s32			gd32_maxlen;
};

struct nfsctl_arg32 {
	s32			ca32_version;	/* safeguard */
	union {
		struct nfsctl_svc32	u32_svc;
		struct nfsctl_client32	u32_client;
		struct nfsctl_export32	u32_export;
		struct nfsctl_fdparm32	u32_getfd;
		struct nfsctl_fsparm32	u32_getfs;
	} u;
#define ca32_svc	u.u32_svc
#define ca32_client	u.u32_client
#define ca32_export	u.u32_export
#define ca32_getfd	u.u32_getfd
#define ca32_getfs	u.u32_getfs
#define ca32_authd	u.u32_authd
};

union nfsctl_res32 {
	__u8			cr32_getfh[NFS_FHSIZE];
	struct knfsd_fh		cr32_getfs;
};

static int nfs_svc32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= __get_user(karg->ca_svc.svc_port, &arg32->ca32_svc.svc32_port);
	err |= __get_user(karg->ca_svc.svc_nthreads, &arg32->ca32_svc.svc32_nthreads);
	return err;
}

static int nfs_clnt32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_client.cl_ident[0],
			  &arg32->ca32_client.cl32_ident[0],
			  NFSCLNT_IDMAX);
	err |= __get_user(karg->ca_client.cl_naddr, &arg32->ca32_client.cl32_naddr);
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

static int nfs_exp32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
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
	karg->ca_export.ex_anon_uid = high2lowuid(karg->ca_export.ex_anon_uid);
	karg->ca_export.ex_anon_gid = high2lowgid(karg->ca_export.ex_anon_gid);
	return err;
}

static int nfs_getfd32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_getfd.gd_addr,
			  &arg32->ca32_getfd.gd32_addr,
			  (sizeof(struct sockaddr)));
	err |= copy_from_user(&karg->ca_getfd.gd_path,
			  &arg32->ca32_getfd.gd32_path,
			  (NFS_MAXPATHLEN+1));
	err |= __get_user(karg->ca_getfd.gd_version,
		      &arg32->ca32_getfd.gd32_version);
	return err;
}

static int nfs_getfs32_trans(struct nfsctl_arg *karg, struct nfsctl_arg32 *arg32)
{
	int err;
	
	err = __get_user(karg->ca_version, &arg32->ca32_version);
	err |= copy_from_user(&karg->ca_getfs.gd_addr,
			  &arg32->ca32_getfs.gd32_addr,
			  (sizeof(struct sockaddr)));
	err |= copy_from_user(&karg->ca_getfs.gd_path,
			  &arg32->ca32_getfs.gd32_path,
			  (NFS_MAXPATHLEN+1));
	err |= __get_user(karg->ca_getfs.gd_maxlen,
		      &arg32->ca32_getfs.gd32_maxlen);
	return err;
}

/* This really doesn't need translations, we are only passing
 * back a union which contains opaque nfs file handle data.
 */
static int nfs_getfh32_res_trans(union nfsctl_res *kres, union nfsctl_res32 *res32)
{
	return copy_to_user(res32, kres, sizeof(*res32)) ? -EFAULT : 0;
}

/*
asmlinkage long sys_ni_syscall(void); 
*/

int asmlinkage sys32_nfsservctl(int cmd, struct nfsctl_arg32 *arg32, union nfsctl_res32 *res32)
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
	case NFSCTL_UNEXPORT:
		err = nfs_exp32_trans(karg, arg32);
		break;
	case NFSCTL_GETFD:
		err = nfs_getfd32_trans(karg, arg32);
		break;
	case NFSCTL_GETFS:
		err = nfs_getfs32_trans(karg, arg32);
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

	if (err)
		goto done;

	if((cmd == NFSCTL_GETFD) ||
	   (cmd == NFSCTL_GETFS))
		err = nfs_getfh32_res_trans(kres, res32);

done:
	if(karg)
		kfree(karg);
	if(kres)
		kfree(kres);
	return err;
}

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

asmlinkage int sys_utimes(char *, struct timeval *);

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
		ret = sys_utimes(kfilename, &ktvs[0]);
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

extern asmlinkage int sys_prctl(int option, unsigned long arg2, unsigned long arg3,
				unsigned long arg4, unsigned long arg5);

asmlinkage int sys32_prctl(int option, u32 arg2, u32 arg3, u32 arg4, u32 arg5)
{
	return sys_prctl(option,
			 (unsigned long) arg2,
			 (unsigned long) arg3,
			 (unsigned long) arg4,
			 (unsigned long) arg5);
}


extern asmlinkage ssize_t sys_pread64(unsigned int fd, char * buf,
				    size_t count, loff_t pos);

extern asmlinkage ssize_t sys_pwrite64(unsigned int fd, const char * buf,
				     size_t count, loff_t pos);

asmlinkage compat_ssize_t sys32_pread64(unsigned int fd, char *ubuf,
				 compat_size_t count, u32 poshi, u32 poslo)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL;
	return sys_pread64(fd, ubuf, count, ((loff_t)AA(poshi) << 32) | AA(poslo));
}

asmlinkage compat_ssize_t sys32_pwrite64(unsigned int fd, char *ubuf,
				  compat_size_t count, u32 poshi, u32 poslo)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL;
	return sys_pwrite64(fd, ubuf, count, ((loff_t)AA(poshi) << 32) | AA(poslo));
}

extern asmlinkage ssize_t sys_readahead(int fd, loff_t offset, size_t count);

asmlinkage compat_ssize_t sys32_readahead(int fd, u32 offhi, u32 offlo, s32 count)
{
	return sys_readahead(fd, ((loff_t)AA(offhi) << 32) | AA(offlo), count);
}

extern asmlinkage ssize_t sys_sendfile(int out_fd, int in_fd, off_t *offset, size_t count);

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
	
	if (!ret && offset && put_user(of, offset))
		return -EFAULT;
		
	return ret;
}

extern asmlinkage ssize_t sys_sendfile64(int out_fd, int in_fd, 
					 loff_t *offset, size_t count);

asmlinkage int sys32_sendfile64(int out_fd, int in_fd, 
				compat_loff_t *offset, s32 count)
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

extern asmlinkage long sys_setpriority(int which, int who, int niceval);

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

extern asmlinkage long sys32_sysctl(struct __sysctl_args32 *args)
{
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
}

struct stat64_emu31 {
	unsigned long long  st_dev;
	unsigned int    __pad1;
#define STAT64_HAS_BROKEN_ST_INO        1
	u32             __st_ino;
	unsigned int    st_mode;
	unsigned int    st_nlink;
	u32             st_uid;
	u32             st_gid;
	unsigned long long  st_rdev;
	unsigned int    __pad3;
	long            st_size;
	u32             st_blksize;
	unsigned char   __pad4[4];
	u32             __pad5;     /* future possible st_blocks high bits */
	u32             st_blocks;  /* Number 512-byte blocks allocated. */
	u32             st_atime;
	u32             __pad6;
	u32             st_mtime;
	u32             __pad7;
	u32             st_ctime;
	u32             __pad8;     /* will be high 32 bits of ctime someday */
	unsigned long   st_ino;
};	

static int cp_stat64(struct stat64_emu31 *ubuf, struct kstat *stat)
{
	struct stat64_emu31 tmp;

	memset(&tmp, 0, sizeof(tmp));

	tmp.st_dev = huge_encode_dev(stat->dev);
	tmp.st_ino = stat->ino;
	tmp.__st_ino = (u32)stat->ino;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = (unsigned int)stat->nlink;
	tmp.st_uid = stat->uid;
	tmp.st_gid = stat->gid;
	tmp.st_rdev = huge_encode_dev(stat->rdev);
	tmp.st_size = stat->size;
	tmp.st_blksize = (u32)stat->blksize;
	tmp.st_blocks = (u32)stat->blocks;
	tmp.st_atime = (u32)stat->atime.tv_sec;
	tmp.st_mtime = (u32)stat->mtime.tv_sec;
	tmp.st_ctime = (u32)stat->ctime.tv_sec;

	return copy_to_user(ubuf,&tmp,sizeof(tmp)) ? -EFAULT : 0; 
}

asmlinkage long sys32_stat64(char * filename, struct stat64_emu31 * statbuf, long flags)
{
	struct kstat stat;
	int ret = vfs_stat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

asmlinkage long sys32_lstat64(char * filename, struct stat64_emu31 * statbuf, long flags)
{
	struct kstat stat;
	int ret = vfs_lstat(filename, &stat);
	if (!ret)
		ret = cp_stat64(statbuf, &stat);
	return ret;
}

asmlinkage long sys32_fstat64(unsigned long fd, struct stat64_emu31 * statbuf, long flags)
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

struct mmap_arg_struct_emu31 {
	u32	addr;
	u32	len;
	u32	prot;
	u32	flags;
	u32	fd;
	u32	offset;
};

/* common code for old and new mmaps */
static inline long do_mmap2(
	unsigned long addr, unsigned long len,
	unsigned long prot, unsigned long flags,
	unsigned long fd, unsigned long pgoff)
{
	struct file * file = NULL;
	unsigned long error = -EBADF;

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	if (!(flags & MAP_ANONYMOUS)) {
		file = fget(fd);
		if (!file)
			goto out;
	}

	down_write(&current->mm->mmap_sem);
	error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
	if (!IS_ERR((void *) error) && error + len >= 0x80000000ULL) {
		/* Result is out of bounds.  */
		do_munmap(current->mm, addr, len);
		error = -ENOMEM;
	}
	up_write(&current->mm->mmap_sem);

	if (file)
		fput(file);
out:    
	return error;
}


asmlinkage unsigned long
old32_mmap(struct mmap_arg_struct_emu31 *arg)
{
	struct mmap_arg_struct_emu31 a;
	int error = -EFAULT;

	if (copy_from_user(&a, arg, sizeof(a)))
		goto out;

	error = -EINVAL;
	if (a.offset & ~PAGE_MASK)
		goto out;

	error = do_mmap2(a.addr, a.len, a.prot, a.flags, a.fd, a.offset >> PAGE_SHIFT); 
out:
	return error;
}

asmlinkage long 
sys32_mmap2(struct mmap_arg_struct_emu31 *arg)
{
	struct mmap_arg_struct_emu31 a;
	int error = -EFAULT;

	if (copy_from_user(&a, arg, sizeof(a)))
		goto out;
	error = do_mmap2(a.addr, a.len, a.prot, a.flags, a.fd, a.offset);
out:
	return error;
}

extern asmlinkage int sys_sched_setaffinity(pid_t pid, unsigned int len,
					    unsigned long *user_mask_ptr);

asmlinkage int sys32_sched_setaffinity(compat_pid_t pid, unsigned int len,
				       u32 *user_mask_ptr)
{
	unsigned long kernel_mask;
	mm_segment_t old_fs;
	int ret;

	if (get_user(kernel_mask, user_mask_ptr))
		return -EFAULT;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_sched_setaffinity(pid,
				    /* XXX Nice api... */
				    sizeof(kernel_mask),
				    &kernel_mask);
	set_fs(old_fs);

	return ret;
}

extern asmlinkage int sys_sched_getaffinity(pid_t pid, unsigned int len,
					    unsigned long *user_mask_ptr);

asmlinkage int sys32_sched_getaffinity(compat_pid_t pid, unsigned int len,
				       u32 *user_mask_ptr)
{
	unsigned long kernel_mask;
	mm_segment_t old_fs;
	int ret;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = sys_sched_getaffinity(pid,
				    /* XXX Nice api... */
				    sizeof(kernel_mask),
				    &kernel_mask);
	set_fs(old_fs);

	if (ret == 0) {
		if (put_user(kernel_mask, user_mask_ptr))
			ret = -EFAULT;
	}

	return ret;
}

asmlinkage ssize_t sys_read(unsigned int fd, char * buf, size_t count);

asmlinkage compat_ssize_t sys32_read(unsigned int fd, char * buf, size_t count)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL; 

	return sys_read(fd, buf, count);
}

asmlinkage ssize_t sys_write(unsigned int fd, const char * buf, size_t count);

asmlinkage compat_ssize_t sys32_write(unsigned int fd, char * buf, size_t count)
{
	if ((compat_ssize_t) count < 0)
		return -EINVAL; 

	return sys_write(fd, buf, count);
}

asmlinkage int sys32_clone(struct pt_regs regs)
{
        unsigned long clone_flags;
        unsigned long newsp;
	int *parent_tidptr, *child_tidptr;

        clone_flags = regs.gprs[3] & 0xffffffffUL;
        newsp = regs.orig_gpr2 & 0x7fffffffUL;
	parent_tidptr = (int *) (regs.gprs[4] & 0x7fffffffUL);
	child_tidptr = (int *) (regs.gprs[5] & 0x7fffffffUL);
        if (!newsp)
                newsp = regs.gprs[15];
        return do_fork(clone_flags & ~CLONE_IDLETASK, newsp, &regs, 0,
		       parent_tidptr, child_tidptr);
}



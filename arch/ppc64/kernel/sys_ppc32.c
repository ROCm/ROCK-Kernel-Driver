/*
 * sys_ppc32.c: Conversion between 32bit and 64bit native syscalls.
 *
 * Copyright (C) 2001 IBM
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/ptrace.h>
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
#include <linux/aio.h>
#include <linux/nfs_fs.h>
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
#include <linux/unistd.h>
#include <linux/sysctl.h>
#include <linux/binfmts.h>
#include <linux/dnotify.h>
#include <linux/security.h>
#include <linux/compat.h>
#include <linux/ptrace.h>
#include <linux/aio_abi.h>

#include <asm/types.h>
#include <asm/ipc.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>

#include <asm/semaphore.h>

#include <net/scm.h>
#include <net/sock.h>
#include <linux/elf.h>
#include <asm/ppcdebug.h>
#include <asm/time.h>
#include <asm/ppc32.h>
#include <asm/mmu_context.h>

#include "pci.h"

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
		                  off_t offset, ino_t ino, unsigned int d_type)
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

	error = vfs_readdir(file, (filldir_t)fillonedir, &buf);
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

static int filldir(void * __buf, const char * name, int namlen, off_t offset,
		   ino_t ino, unsigned int d_type)
{
	struct linux_dirent32 * dirent;
	struct getdents_callback32 * buf = (struct getdents_callback32 *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 2);

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;
	if (dirent) {
		if (__put_user(offset, &dirent->d_off))
			goto efault;
	}
	dirent = buf->current_dir;
	if (__put_user(ino, &dirent->d_ino))
		goto efault;
	if (__put_user(reclen, &dirent->d_reclen))
		goto efault;
	if (copy_to_user(dirent->d_name, name, namlen))
		goto efault;
	if (__put_user(0, dirent->d_name + namlen))
		goto efault;
	if (__put_user(d_type, (char *) dirent + reclen - 1))
		goto efault;
	buf->previous = dirent;
	dirent = (void *)dirent + reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
efault:
	buf->error = -EFAULT;
	return -EFAULT;
}

long sys32_getdents(unsigned int fd, struct linux_dirent32 *dirent,
		    unsigned int count)
{
	struct file * file;
	struct linux_dirent32 * lastdirent;
	struct getdents_callback32 buf;
	int error;

	error = -EFAULT;
	if (!access_ok(VERIFY_WRITE, dirent, count))
		goto out;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = vfs_readdir(file, (filldir_t)filldir, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.error;
	lastdirent = buf.previous;
	if (lastdirent) {
		if (put_user(file->f_pos, &lastdirent->d_off))
			error = -EFAULT;
		else
			error = count - buf.count;
	}

out_putf:
	fput(file);
out:
	return error;
}

asmlinkage long ppc32_select(u32 n, compat_ulong_t __user *inp,
		compat_ulong_t __user *outp, compat_ulong_t __user *exp,
		compat_uptr_t tvp_x)
{
	/* sign extend n */
	return compat_sys_select((int)n, inp, outp, exp, compat_ptr(tvp_x));
}

int cp_compat_stat(struct kstat *stat, struct compat_stat *statbuf)
{
	long err;

	if (stat->size > MAX_NON_LFS || !new_valid_dev(stat->dev) ||
	    !new_valid_dev(stat->rdev))
		return -EOVERFLOW;

	err  = verify_area(VERIFY_WRITE, statbuf, sizeof(*statbuf));
	err |= __put_user(new_encode_dev(stat->dev), &statbuf->st_dev);
	err |= __put_user(stat->ino, &statbuf->st_ino);
	err |= __put_user(stat->mode, &statbuf->st_mode);
	err |= __put_user(stat->nlink, &statbuf->st_nlink);
	err |= __put_user(stat->uid, &statbuf->st_uid);
	err |= __put_user(stat->gid, &statbuf->st_gid);
	err |= __put_user(new_encode_dev(stat->rdev), &statbuf->st_rdev);
	err |= __put_user(stat->size, &statbuf->st_size);
	err |= __put_user(stat->atime.tv_sec, &statbuf->st_atime);
	err |= __put_user(stat->atime.tv_nsec, &statbuf->st_atime_nsec);
	err |= __put_user(stat->mtime.tv_sec, &statbuf->st_mtime);
	err |= __put_user(stat->mtime.tv_nsec, &statbuf->st_mtime_nsec);
	err |= __put_user(stat->ctime.tv_sec, &statbuf->st_ctime);
	err |= __put_user(stat->ctime.tv_nsec, &statbuf->st_ctime_nsec);
	err |= __put_user(stat->blksize, &statbuf->st_blksize);
	err |= __put_user(stat->blocks, &statbuf->st_blocks);
	err |= __put_user(0, &statbuf->__unused4[0]);
	err |= __put_user(0, &statbuf->__unused4[1]);

	return err;
}

/* Note: it is necessary to treat option as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sysfs(u32 option, u32 arg1, u32 arg2)
{
	return sys_sysfs((int)option, arg1, arg2);
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
extern void ppc_adjtimex(void);

asmlinkage long sys32_adjtimex(struct timex32 *utp)
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

	/* adjust the conversion of TB to time of day to track adjtimex */
	ppc_adjtimex();

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

	if(err) return -EFAULT;
	return 0;
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
	karg->ca_export.ex_anon_uid = karg->ca_export.ex_anon_uid;
	karg->ca_export.ex_anon_gid = karg->ca_export.ex_anon_gid;

	if(err) return -EFAULT;
	return 0;
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

	if(err) return -EFAULT;
	return 0;
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

	if(err) return -EFAULT;
	return 0;
}

/* This really doesn't need translations, we are only passing
 * back a union which contains opaque nfs file handle data.
 */
static int nfs_getfh32_res_trans(union nfsctl_res *kres, union nfsctl_res32 *res32)
{
	int err;

	err = copy_to_user(res32, kres, sizeof(*res32));

	if(err) return -EFAULT;
	return 0;
}

/* Note: it is necessary to treat cmd_parm as an unsigned int, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
int asmlinkage sys32_nfsservctl(u32 cmd_parm, struct nfsctl_arg32 *arg32, union nfsctl_res32 *res32)
{
  int cmd = (int)cmd_parm;
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



/* These are here just in case some old sparc32 binary calls it. */
asmlinkage long sys32_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	
	return -ERESTARTNOHAND;
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

static inline long put_tv32(struct compat_timeval *o, struct timeval *i)
{
	return (!access_ok(VERIFY_WRITE, o, sizeof(*o)) ||
		(__put_user(i->tv_sec, &o->tv_sec) |
		 __put_user(i->tv_usec, &o->tv_usec)));
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

asmlinkage long sys32_sysinfo(struct sysinfo32 *info)
{
	struct sysinfo s;
	int ret, err;
	int bitcount=0;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sysinfo(&s);
	set_fs (old_fs);
	/* Check to see if any memory value is too large for 32-bit and
         * scale down if needed.
         */
	if ((s.totalram >> 32) || (s.totalswap >> 32)) {
	    while (s.mem_unit < PAGE_SIZE) {
		s.mem_unit <<= 1;
		bitcount++;
	    }
	    s.totalram >>=bitcount;
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




/* Translations due to time_t size differences.  Which affects all
   sorts of things, like timeval and itimerval.  */
extern struct timezone sys_tz;

asmlinkage long sys32_gettimeofday(struct compat_timeval *tv, struct timezone *tz)
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



asmlinkage long sys32_settimeofday(struct compat_timeval *tv, struct timezone *tz)
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


struct msgbuf32 {
	compat_long_t mtype; 
	char mtext[1];
};

struct semid_ds32 {
	struct ipc_perm sem_perm;
	compat_time_t sem_otime;
	compat_time_t sem_ctime;
	compat_uptr_t sem_base;
	compat_uptr_t sem_pending;
	compat_uptr_t sem_pending_last;
	compat_uptr_t undo;
	unsigned short sem_nsems;
};

struct semid64_ds32 {
	struct ipc64_perm sem_perm;
	unsigned int __unused1;
	compat_time_t sem_otime;
	unsigned int __unused2;
	compat_time_t sem_ctime;
	compat_ulong_t sem_nsems;
	compat_ulong_t __unused3;
	compat_ulong_t __unused4;
};

struct msqid_ds32 {
	struct ipc_perm msg_perm;
	compat_uptr_t msg_first;
	compat_uptr_t msg_last;
	compat_time_t msg_stime;
	compat_time_t msg_rtime;
	compat_time_t msg_ctime;
	compat_ulong_t msg_lcbytes;
	compat_ulong_t msg_lqbytes;
	unsigned short msg_cbytes;
	unsigned short msg_qnum;
	unsigned short msg_qbytes;
	compat_ipc_pid_t msg_lspid;
	compat_ipc_pid_t msg_lrpid;
};

struct msqid64_ds32 {
	struct ipc64_perm msg_perm;
	unsigned int __unused1;
	compat_time_t msg_stime;
	unsigned int __unused2;
	compat_time_t msg_rtime;
	unsigned int __unused3;
	compat_time_t msg_ctime;
	compat_ulong_t msg_cbytes;
	compat_ulong_t msg_qnum;
	compat_ulong_t msg_qbytes;
	compat_pid_t msg_lspid;
	compat_pid_t msg_lrpid;
	compat_ulong_t __unused4;
	compat_ulong_t __unused5;
};

struct shmid_ds32 {
	struct ipc_perm shm_perm;
	int shm_segsz;
	compat_time_t shm_atime;
	compat_time_t shm_dtime;
	compat_time_t shm_ctime;
	compat_ipc_pid_t shm_cpid;
	compat_ipc_pid_t shm_lpid;
	unsigned short shm_nattch;
	unsigned short __unused;
	compat_uptr_t __unused2;
	compat_uptr_t __unused3;
};

struct shmid64_ds32 {
	struct ipc64_perm shm_perm;
	unsigned int __unused1;
	compat_time_t shm_atime;
	unsigned int __unused2;
	compat_time_t shm_dtime;
	unsigned int __unused3;
	compat_time_t shm_ctime;
	unsigned int __unused4;
	compat_size_t shm_segsz;
	compat_pid_t shm_cpid;
	compat_pid_t shm_lpid;
	compat_ulong_t shm_nattch;
	compat_ulong_t __unused5;
	compat_ulong_t __unused6;
};

/*
 * sys32_ipc() is the de-multiplexer for the SysV IPC calls in 32bit
 * emulation..
 *
 * This is really horribly ugly.
 */
static long do_sys32_semctl(int first, int second, int third, void *uptr)
{
	union semun fourth;
	u32 pad;
	int err, err2;
	mm_segment_t old_fs;

	if (!uptr)
		return -EINVAL;
	err = -EFAULT;
	if (get_user(pad, (u32 *)uptr))
		return err;
	if ((third & ~IPC_64) == SETVAL)
		fourth.val = (int)pad;
	else
		fourth.__pad = (void *)A(pad);
	switch (third & (~IPC_64)) {

	case IPC_INFO:
	case IPC_RMID:
	case SEM_INFO:
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETALL:
	case SETALL:
	case SETVAL:
		err = sys_semctl(first, second, third, fourth);
		break;

	case IPC_STAT:
	case SEM_STAT:
		if (third & IPC_64) {
			struct semid64_ds s64;
			struct semid64_ds32 *usp;

			usp = (struct semid64_ds32 *)A(pad);
			fourth.__pad = &s64;
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_semctl(first, second, third, fourth);
			set_fs(old_fs);
			err2 = copy_to_user(&usp->sem_perm, &s64.sem_perm,
					    sizeof(struct ipc64_perm));
			err2 |= __put_user(s64.sem_otime, &usp->sem_otime);
			err2 |= __put_user(s64.sem_ctime, &usp->sem_ctime);
			err2 |= __put_user(s64.sem_nsems, &usp->sem_nsems);
			if (err2)
				err = -EFAULT;
		} else {
			struct semid_ds s;
			struct semid_ds32 *usp;

			usp = (struct semid_ds32 *)A(pad);
			fourth.__pad = &s;
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_semctl(first, second, third, fourth);
			set_fs(old_fs);
			err2 = copy_to_user(&usp->sem_perm, &s.sem_perm,
					    sizeof(struct ipc_perm));
			err2 |= __put_user(s.sem_otime, &usp->sem_otime);
			err2 |= __put_user(s.sem_ctime, &usp->sem_ctime);
			err2 |= __put_user(s.sem_nsems, &usp->sem_nsems);
			if (err2)
				err = -EFAULT;
		}
		break;
 
	case IPC_SET:
		if (third & IPC_64) {
			struct semid64_ds s64;
			struct semid64_ds32 *usp;

			usp = (struct semid64_ds32 *)A(pad);

			err = get_user(s64.sem_perm.uid, &usp->sem_perm.uid);
			err |= __get_user(s64.sem_perm.gid,
					  &usp->sem_perm.gid);
			err |= __get_user(s64.sem_perm.mode,
					  &usp->sem_perm.mode);
			if (err)
				goto out;
			fourth.__pad = &s64;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_semctl(first, second, third, fourth);
			set_fs(old_fs);

		} else {
			struct semid_ds s;
			struct semid_ds32 *usp;

			usp = (struct semid_ds32 *)A(pad);

			err = get_user(s.sem_perm.uid, &usp->sem_perm.uid);
			err |= __get_user(s.sem_perm.gid,
					  &usp->sem_perm.gid);
			err |= __get_user(s.sem_perm.mode,
					  &usp->sem_perm.mode);
			if (err)
				goto out;
			fourth.__pad = &s;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_semctl(first, second, third, fourth);
			set_fs(old_fs);
		}
		break;
	default:
		err = -EINVAL;
	}
out:
	return err;
}

#define MAXBUF (64*1024)

static int 
do_sys32_msgsnd(int first, int second, int third, void *uptr)
{
	struct msgbuf *p;
	struct msgbuf32 *up = (struct msgbuf32 *)uptr;
	mm_segment_t old_fs;
	int err;

	if (second < 0 || (second >= MAXBUF-sizeof(struct msgbuf)))
		return -EINVAL;

	p = kmalloc(second + sizeof(struct msgbuf), GFP_USER);
	if (!p)
		return -ENOMEM;
	err = get_user(p->mtype, &up->mtype);
	err |= copy_from_user(p->mtext, &up->mtext, second);
	if (err) {
		err = -EFAULT;
		goto out;
	}
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_msgsnd(first, p, second, third);
	set_fs(old_fs);
out:
	kfree(p);
	return err;
}

static int
do_sys32_msgrcv(int first, int second, int msgtyp, int third,
		int version, void *uptr)
{
	struct msgbuf32 *up;
	struct msgbuf *p;
	mm_segment_t old_fs;
	int err;

	if (second < 0 || (second >= MAXBUF-sizeof(struct msgbuf)))
		return -EINVAL;

	if (!version) {
		struct ipc_kludge_32 *uipck = (struct ipc_kludge_32 *)uptr;
		struct ipc_kludge_32 ipck;

		err = -EINVAL;
		if (!uptr)
			goto out;
		err = -EFAULT;
		if (copy_from_user(&ipck, uipck, sizeof(struct ipc_kludge_32)))
			goto out;
		uptr = (void *)A(ipck.msgp);
		msgtyp = ipck.msgtyp;
	}
	err = -ENOMEM;
	p = kmalloc(second + sizeof (struct msgbuf), GFP_USER);
	if (!p)
		goto out;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_msgrcv(first, p, second, msgtyp, third);
	set_fs(old_fs);
	if (err < 0)
		goto free_then_out;
	up = (struct msgbuf32 *)uptr;
	if (put_user(p->mtype, &up->mtype) ||
	    copy_to_user(&up->mtext, p->mtext, err))
		err = -EFAULT;
free_then_out:
	kfree(p);
out:
	return err;
}

static int
do_sys32_msgctl(int first, int second, void *uptr)
{
	int err = -EINVAL, err2;
	mm_segment_t old_fs;

	switch (second & (~IPC_64)) {

	case IPC_INFO:
	case IPC_RMID:
	case MSG_INFO:
		err = sys_msgctl(first, second, (struct msqid_ds *)uptr);
		break;

	case IPC_SET:
		if (second & IPC_64) {
			struct msqid64_ds m64;
			struct msqid64_ds32 *up = (struct msqid64_ds32 *)uptr;

			err2 = copy_from_user(&m64.msg_perm, &up->msg_perm,
					      sizeof(struct ipc64_perm));
			err2 |= __get_user(m64.msg_qbytes, &up->msg_qbytes);
			if (err2) {
				err = -EFAULT;
				break;
			}
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_msgctl(first, second,
					 (struct msqid_ds *)&m64);
			set_fs(old_fs);
		} else {
			struct msqid_ds m;
			struct msqid_ds32 *up = (struct msqid_ds32 *)uptr;

			err2 = copy_from_user(&m.msg_perm, &up->msg_perm,
					      sizeof(struct ipc_perm));
			err2 |= __get_user(m.msg_qbytes, &up->msg_qbytes);
			if (err2) {
				err = -EFAULT;
				break;
			}
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_msgctl(first, second, &m);
			set_fs(old_fs);
		}
		break;

	case IPC_STAT:
	case MSG_STAT:
		if (second & IPC_64) {
			struct msqid64_ds m64;
			struct msqid64_ds32 *up = (struct msqid64_ds32 *)uptr;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_msgctl(first, second,
					 (struct msqid_ds *)&m64);
			set_fs(old_fs);

			err2 = copy_to_user(&up->msg_perm, &m64.msg_perm,
					    sizeof(struct ipc64_perm));
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
		} else {
			struct msqid64_ds m;
			struct msqid_ds32 *up = (struct msqid_ds32 *)uptr;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_msgctl(first, second, (struct msqid_ds *)&m);
			set_fs(old_fs);

			err2 = copy_to_user(&up->msg_perm, &m.msg_perm,
					    sizeof(struct ipc_perm));
 			err2 |= __put_user(m.msg_stime, &up->msg_stime);
			err2 |= __put_user(m.msg_rtime, &up->msg_rtime);
			err2 |= __put_user(m.msg_ctime, &up->msg_ctime);
			err2 |= __put_user(m.msg_cbytes, &up->msg_cbytes);
			err2 |= __put_user(m.msg_qnum, &up->msg_qnum);
			err2 |= __put_user(m.msg_qbytes, &up->msg_qbytes);
			err2 |= __put_user(m.msg_lspid, &up->msg_lspid);
			err2 |= __put_user(m.msg_lrpid, &up->msg_lrpid);
			if (err2)
				err = -EFAULT;
		}
		break;
	}
	return err;
}

static int
do_sys32_shmat(int first, int second, int third, int version, void *uptr)
{
	unsigned long raddr;
	u32 *uaddr = (u32 *)A((u32)third);
	int err = -EINVAL;

	if (version == 1)
		return err;
	err = do_shmat(first, uptr, second, &raddr);
	if (err)
		return err;
	err = put_user(raddr, uaddr);
	return err;
}

static int
do_sys32_shmctl(int first, int second, void *uptr)
{
	int err = -EINVAL, err2;
	mm_segment_t old_fs;

	switch (second & (~IPC_64)) {

	case IPC_INFO:
	case IPC_RMID:
	case SHM_LOCK:
	case SHM_UNLOCK:
		err = sys_shmctl(first, second, (struct shmid_ds *)uptr);
		break;
	case IPC_SET:
		if (second & IPC_64) {
			struct shmid64_ds32 *up = (struct shmid64_ds32 *)uptr;
			struct shmid64_ds s64;

			err = get_user(s64.shm_perm.uid, &up->shm_perm.uid);
			err |= __get_user(s64.shm_perm.gid, &up->shm_perm.gid);
			err |= __get_user(s64.shm_perm.mode,
					  &up->shm_perm.mode);
			if (err)
				break;
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_shmctl(first, second,
					 (struct shmid_ds *)&s64);
			set_fs(old_fs);
		} else {
			struct shmid_ds32 *up = (struct shmid_ds32 *)uptr;
			struct shmid_ds s;

			err = get_user(s.shm_perm.uid, &up->shm_perm.uid);
			err |= __get_user(s.shm_perm.gid, &up->shm_perm.gid);
			err |= __get_user(s.shm_perm.mode, &up->shm_perm.mode);
			if (err)
				break;
			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_shmctl(first, second, &s);
			set_fs(old_fs);
		}
		break;

	case IPC_STAT:
	case SHM_STAT:
		if (second & IPC_64) {
			struct shmid64_ds32 *up = (struct shmid64_ds32 *)uptr;
			struct shmid64_ds s64;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_shmctl(first, second,
					 (struct shmid_ds *)&s64);
			set_fs(old_fs);
			if (err < 0)
				break;

			err2 = copy_to_user(&up->shm_perm, &s64.shm_perm,
					    sizeof(struct ipc64_perm));
			err2 |= __put_user(s64.shm_atime, &up->shm_atime);
			err2 |= __put_user(s64.shm_dtime, &up->shm_dtime);
			err2 |= __put_user(s64.shm_ctime, &up->shm_ctime);
			err2 |= __put_user(s64.shm_segsz, &up->shm_segsz);
			err2 |= __put_user(s64.shm_nattch, &up->shm_nattch);
			err2 |= __put_user(s64.shm_cpid, &up->shm_cpid);
			err2 |= __put_user(s64.shm_lpid, &up->shm_lpid);
			if (err2)
				err = -EFAULT;
		} else {
			struct shmid_ds32 *up = (struct shmid_ds32 *)uptr;
			struct shmid_ds s;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			err = sys_shmctl(first, second, &s);
			set_fs(old_fs);
			if (err < 0)
				break;

			err2 = copy_to_user(&up->shm_perm, &s.shm_perm,
					    sizeof(struct ipc_perm));
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
		break;

	case SHM_INFO: {
		struct shm_info si;
		struct shm_info32 {
			int used_ids;
			u32 shm_tot, shm_rss, shm_swp;
			u32 swap_attempts, swap_successes;
		} *uip = (struct shm_info32 *)uptr;

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = sys_shmctl(first, second, (struct shmid_ds *)&si);
		set_fs(old_fs);
		if (err < 0)
			break;
		err2 = put_user(si.used_ids, &uip->used_ids);
		err2 |= __put_user(si.shm_tot, &uip->shm_tot);
		err2 |= __put_user(si.shm_rss, &uip->shm_rss);
		err2 |= __put_user(si.shm_swp, &uip->shm_swp);
		err2 |= __put_user(si.swap_attempts, &uip->swap_attempts);
		err2 |= __put_user(si.swap_successes, &uip->swap_successes);
		if (err2)
			err = -EFAULT;
		break;
	}
	}
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

/*
 * Note: it is necessary to treat first_parm, second_parm, and
 * third_parm as unsigned ints, with the corresponding cast to a
 * signed int to insure that the proper conversion (sign extension)
 * between the register representation of a signed int (msr in 32-bit
 * mode) and the register representation of a signed int (msr in
 * 64-bit mode) is performed.
 */
asmlinkage long sys32_ipc(u32 call, u32 first_parm, u32 second_parm, u32 third_parm, u32 ptr, u32 fifth)
{
	int first  = (int)first_parm;
	int second = (int)second_parm;
	int third  = (int)third_parm;
	int version, err;
	
	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {

	case SEMOP:
		/* struct sembuf is the same on 32 and 64bit :)) */
		err = sys_semtimedop(first, (struct sembuf *)AA(ptr),
				     second, NULL);
		break;
	case SEMTIMEDOP:
		err = sys32_semtimedop(first, (struct sembuf *)AA(ptr), second,
				       (const struct compat_timespec *)AA(fifth));
		break;
	case SEMGET:
		err = sys_semget(first, second, third);
		break;
	case SEMCTL:
		err = do_sys32_semctl(first, second, third,
				      (void *)AA(ptr));
		break;

	case MSGSND:
		err = do_sys32_msgsnd(first, second, third,
				      (void *)AA(ptr));
		break;
	case MSGRCV:
		err = do_sys32_msgrcv(first, second, fifth, third,
				      version, (void *)AA(ptr));
		break;
	case MSGGET:
		err = sys_msgget((key_t)first, second);
		break;
	case MSGCTL:
		err = do_sys32_msgctl(first, second, (void *)AA(ptr));
		break;

	case SHMAT:
		err = do_sys32_shmat(first, second, third,
				     version, (void *)AA(ptr));
		break;
	case SHMDT: 
		err = sys_shmdt((char *)AA(ptr));
		break;
	case SHMGET:
		err = sys_shmget(first, second_parm, third);
		break;
	case SHMCTL:
		err = do_sys32_shmctl(first, second, (void *)AA(ptr));
		break;
	default:
		err = -ENOSYS;
		break;
	}
	return err;
}

/* Note: it is necessary to treat out_fd and in_fd as unsigned ints, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sendfile(u32 out_fd, u32 in_fd, compat_off_t* offset, u32 count)
{
	mm_segment_t old_fs = get_fs();
	int ret;
	off_t of;
	
	if (offset && get_user(of, offset))
		return -EFAULT;
		
	set_fs(KERNEL_DS);
	ret = sys_sendfile((int)out_fd, (int)in_fd, offset ? &of : NULL, count);
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

long sys32_execve(unsigned long a0, unsigned long a1, unsigned long a2,
		  unsigned long a3, unsigned long a4, unsigned long a5,
		  struct pt_regs *regs)
{
	int error;
	char * filename;
	
	filename = getname((char *) a0);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	if (regs->msr & MSR_FP)
		giveup_fpu(current);
#ifdef CONFIG_ALTIVEC
	if (regs->msr & MSR_VEC)
		giveup_altivec(current);
#endif /* CONFIG_ALTIVEC */

	error = compat_do_execve(filename, compat_ptr(a1), compat_ptr(a2), regs);

	if (error == 0)
		current->ptrace &= ~PT_DTRACE;
	putname(filename);

out:
	return error;
}

/* Set up a thread for executing a new program. */
void start_thread32(struct pt_regs* regs, unsigned long nip, unsigned long sp)
{
	set_fs(USER_DS);
	memset(regs->gpr, 0, sizeof(regs->gpr));
	memset(&regs->ctr, 0, 4 * sizeof(regs->ctr));
	regs->nip = nip;
	regs->gpr[1] = sp;
	regs->msr = MSR_USER32;
#ifndef CONFIG_SMP
	if (last_task_used_math == current)
		last_task_used_math = 0;
#endif /* CONFIG_SMP */
	current->thread.fpscr = 0;
	memset(current->thread.fpr, 0, sizeof(current->thread.fpr));
#ifdef CONFIG_ALTIVEC
#ifndef CONFIG_SMP
	if (last_task_used_altivec == current)
		last_task_used_altivec = 0;
#endif /* CONFIG_SMP */
	memset(current->thread.vr, 0, sizeof(current->thread.vr));
	current->thread.vscr.u[0] = 0;
	current->thread.vscr.u[1] = 0;
	current->thread.vscr.u[2] = 0;
	current->thread.vscr.u[3] = 0x00010000; /* Java mode disabled */
	current->thread.vrsave = 0;
	current->thread.used_vr = 0;
#endif /* CONFIG_ALTIVEC */
}

/* Note: it is necessary to treat option as an unsigned int, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_prctl(u32 option, u32 arg2, u32 arg3, u32 arg4, u32 arg5)
{
	return sys_prctl((int)option,
			 (unsigned long) arg2,
			 (unsigned long) arg3,
			 (unsigned long) arg4,
			 (unsigned long) arg5);
}

/* Note: it is necessary to treat pid as an unsigned int, 
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_rr_get_interval(u32 pid, struct compat_timespec *interval)
{
	struct timespec t;
	int ret;
	mm_segment_t old_fs = get_fs ();
	
	set_fs (KERNEL_DS);
	ret = sys_sched_rr_get_interval((int)pid, &t);
	set_fs (old_fs);
	if (put_compat_timespec(&t, interval))
		return -EFAULT;
	return ret;
}

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

#define IOBASE_BRIDGE_NUMBER	0
#define IOBASE_MEMORY		1
#define IOBASE_IO		2
#define IOBASE_ISA_IO		3
#define IOBASE_ISA_MEM		4

asmlinkage int sys32_pciconfig_iobase(u32 which, u32 in_bus, u32 in_devfn)
{
	struct pci_controller* hose;
	struct list_head *ln;
	struct pci_bus *bus = NULL;
	struct device_node *hose_node;

	/* Argh ! Please forgive me for that hack, but that's the
	 * simplest way to get existing XFree to not lockup on some
	 * G5 machines... So when something asks for bus 0 io base
	 * (bus 0 is HT root), we return the AGP one instead.
	 */
#ifdef CONFIG_PPC_PMAC
	if (systemcfg->platform == PLATFORM_POWERMAC &&
	    machine_is_compatible("MacRISC4"))
		if (in_bus == 0)
			in_bus = 0xf0;
#endif /* CONFIG_PPC_PMAC */

	/* That syscall isn't quite compatible with PCI domains, but it's
	 * used on pre-domains setup. We return the first match
	 */

	for (ln = pci_root_buses.next; ln != &pci_root_buses; ln = ln->next) {
		bus = pci_bus_b(ln);
		if (in_bus >= bus->number && in_bus < (bus->number + bus->subordinate))
			break;
		bus = NULL;
	}
	if (bus == NULL || bus->sysdata == NULL)
		return -ENODEV;

	hose_node = (struct device_node *)bus->sysdata;
	hose = hose_node->phb;

	switch (which) {
	case IOBASE_BRIDGE_NUMBER:
		return (long)hose->first_busno;
	case IOBASE_MEMORY:
		return (long)hose->pci_mem_offset;
	case IOBASE_IO:
		return (long)hose->io_base_phys;
	case IOBASE_ISA_IO:
		return (long)isa_io_base;
	case IOBASE_ISA_MEM:
		return -EINVAL;
	}

	return -EOPNOTSUPP;
}


asmlinkage int ppc64_newuname(struct new_utsname * name)
{
	int errno = sys_newuname(name);

	if (current->personality == PER_LINUX32 && !errno) {
		if(copy_to_user(name->machine, "ppc\0\0", 8)) {
			errno = -EFAULT;
		}
	}
	return errno;
}

asmlinkage int ppc64_personality(unsigned long personality)
{
	int ret;
	if (current->personality == PER_LINUX32 && personality == PER_LINUX)
		personality = PER_LINUX32;
	ret = sys_personality(personality);
	if (ret == PER_LINUX32)
		ret = PER_LINUX;
	return ret;
}



/* Note: it is necessary to treat mode as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_access(const char * filename, u32 mode)
{
	return sys_access(filename, (int)mode);
}


/* Note: it is necessary to treat mode as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_creat(const char * pathname, u32 mode)
{
	return sys_creat(pathname, (int)mode);
}


/* Note: it is necessary to treat pid and options as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_waitpid(u32 pid, unsigned int * stat_addr, u32 options)
{
	return sys_waitpid((int)pid, stat_addr, (int)options);
}


/* Note: it is necessary to treat gidsetsize as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_getgroups(u32 gidsetsize, gid_t *grouplist)
{
	return sys_getgroups((int)gidsetsize, grouplist);
}


/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_getpgid(u32 pid)
{
	return sys_getpgid((int)pid);
}


/* Note: it is necessary to treat which and who as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_getpriority(u32 which, u32 who)
{
	return sys_getpriority((int)which, (int)who);
}


/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_getsid(u32 pid)
{
	return sys_getsid((int)pid);
}


/* Note: it is necessary to treat pid and sig as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_kill(u32 pid, u32 sig)
{
	return sys_kill((int)pid, (int)sig);
}


/* Note: it is necessary to treat mode as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_mkdir(const char * pathname, u32 mode)
{
	return sys_mkdir(pathname, (int)mode);
}

long sys32_nice(u32 increment)
{
	/* sign extend increment */
	return sys_nice((int)increment);
}

off_t ppc32_lseek(unsigned int fd, u32 offset, unsigned int origin)
{
	/* sign extend n */
	return sys_lseek(fd, (int)offset, origin);
}

/*
 * This is just a version for 32-bit applications which does
 * not force O_LARGEFILE on.
 */
long sys32_open(const char * filename, int flags, int mode)
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

/* Note: it is necessary to treat bufsiz as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_readlink(const char * path, char * buf, u32 bufsiz)
{
	return sys_readlink(path, buf, (int)bufsiz);
}

/* Note: it is necessary to treat option as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_get_priority_max(u32 policy)
{
	return sys_sched_get_priority_max((int)policy);
}


/* Note: it is necessary to treat policy as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_get_priority_min(u32 policy)
{
	return sys_sched_get_priority_min((int)policy);
}


/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_getparam(u32 pid, struct sched_param *param)
{
	return sys_sched_getparam((int)pid, param);
}


/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_getscheduler(u32 pid)
{
	return sys_sched_getscheduler((int)pid);
}


/* Note: it is necessary to treat pid as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_setparam(u32 pid, struct sched_param *param)
{
	return sys_sched_setparam((int)pid, param);
}


/* Note: it is necessary to treat pid and policy as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_sched_setscheduler(u32 pid, u32 policy, struct sched_param *param)
{
	return sys_sched_setscheduler((int)pid, (int)policy, param);
}


/* Note: it is necessary to treat len as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_setdomainname(char *name, u32 len)
{
	return sys_setdomainname(name, (int)len);
}


/* Note: it is necessary to treat gidsetsize as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_setgroups(u32 gidsetsize, gid_t *grouplist)
{
	return sys_setgroups((int)gidsetsize, grouplist);
}


asmlinkage long sys32_sethostname(char *name, u32 len)
{
	/* sign extend len */
	return sys_sethostname(name, (int)len);
}


/* Note: it is necessary to treat pid and pgid as unsigned ints,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_setpgid(u32 pid, u32 pgid)
{
	return sys_setpgid((int)pid, (int)pgid);
}


long sys32_setpriority(u32 which, u32 who, u32 niceval)
{
	/* sign extend which, who and niceval */
	return sys_setpriority((int)which, (int)who, (int)niceval);
}

/* Note: it is necessary to treat newmask as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_ssetmask(u32 newmask)
{
	return sys_ssetmask((int) newmask);
}

long sys32_syslog(u32 type, char * buf, u32 len)
{
	/* sign extend len */
	return sys_syslog(type, buf, (int)len);
}


/* Note: it is necessary to treat mask as an unsigned int,
 * with the corresponding cast to a signed int to insure that the 
 * proper conversion (sign extension) between the register representation of a signed int (msr in 32-bit mode)
 * and the register representation of a signed int (msr in 64-bit mode) is performed.
 */
asmlinkage long sys32_umask(u32 mask)
{
	return sys_umask((int)mask);
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

asmlinkage long sys32_time(compat_time_t* tloc)
{
	compat_time_t secs;

	struct timeval tv;

	do_gettimeofday( &tv );
	secs = tv.tv_sec;

	if (tloc) {
		if (put_user(secs,tloc))
			secs = -EFAULT;
	}

	return secs;
}

int sys32_olduname(struct oldold_utsname * name)
{
	int error;
	
	if (!name)
		return -EFAULT;
	if (!access_ok(VERIFY_WRITE,name,sizeof(struct oldold_utsname)))
		return -EFAULT;
  
	down_read(&uts_sem);
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
	up_read(&uts_sem);

	error = error ? -EFAULT : 0;
	
	return error;
}

unsigned long sys32_mmap2(unsigned long addr, size_t len,
			  unsigned long prot, unsigned long flags,
			  unsigned long fd, unsigned long pgoff)
{
	/* This should remain 12 even if PAGE_SIZE changes */
	return sys_mmap(addr, len, prot, flags, fd, pgoff << 12);
}

int get_compat_timeval(struct timeval *tv, struct compat_timeval *ctv)
{
	return (verify_area(VERIFY_READ, ctv, sizeof(*ctv)) ||
		__get_user(tv->tv_sec, &ctv->tv_sec) ||
		__get_user(tv->tv_usec, &ctv->tv_usec)) ? -EFAULT : 0;
}

long sys32_utimes(char *filename, struct compat_timeval *tvs)
{
	char *kfilename;
	struct timeval ktvs[2];
	mm_segment_t old_fs;
	long ret;

	kfilename = getname(filename);
	ret = PTR_ERR(kfilename);
	if (!IS_ERR(kfilename)) {
		if (tvs) {
			if (get_compat_timeval(&ktvs[0], &tvs[0]) ||
			    get_compat_timeval(&ktvs[1], &tvs[1]))
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

long sys32_tgkill(u32 tgid, u32 pid, int sig)
{
	/* sign extend tgid, pid */
	return sys_tgkill((int)tgid, (int)pid, sig);
}

/* 
 * long long munging:
 * The 32 bit ABI passes long longs in an odd even register pair.
 */

compat_ssize_t sys32_pread64(unsigned int fd, char *ubuf, compat_size_t count,
			     u32 reg6, u32 poshi, u32 poslo)
{
	return sys_pread64(fd, ubuf, count, ((loff_t)poshi << 32) | poslo);
}

compat_ssize_t sys32_pwrite64(unsigned int fd, char *ubuf, compat_size_t count,
			      u32 reg6, u32 poshi, u32 poslo)
{
	return sys_pwrite64(fd, ubuf, count, ((loff_t)poshi << 32) | poslo);
}

compat_ssize_t sys32_readahead(int fd, u32 r4, u32 offhi, u32 offlo, u32 count)
{
	return sys_readahead(fd, ((loff_t)offhi << 32) | offlo, count);
}

asmlinkage int sys32_truncate64(const char * path, u32 reg4,
				unsigned long high, unsigned long low)
{
	return sys_truncate(path, (high << 32) | low);
}

asmlinkage int sys32_ftruncate64(unsigned int fd, u32 reg4, unsigned long high,
				 unsigned long low)
{
	return sys_ftruncate(fd, (high << 32) | low);
}

long ppc32_lookup_dcookie(u32 cookie_high, u32 cookie_low, char *buf,
			  size_t len)
{
	return sys_lookup_dcookie((u64)cookie_high << 32 | cookie_low,
				  buf, len);
}

long ppc32_fadvise64(int fd, u32 unused, u32 offset_high, u32 offset_low,
		     size_t len, int advice)
{
	return sys_fadvise64(fd, (u64)offset_high << 32 | offset_low, len,
			     advice);
}

long ppc32_fadvise64_64(int fd, int advice, u32 offset_high, u32 offset_low,
			u32 len_high, u32 len_low)
{
	return sys_fadvise64(fd, (u64)offset_high << 32 | offset_low,
			     (u64)len_high << 32 | len_low, advice);
}

extern long sys_timer_create(clockid_t, sigevent_t *, timer_t *);

long ppc32_timer_create(clockid_t clock,
			struct compat_sigevent __user *ev32,
			timer_t __user *timer_id)
{
	sigevent_t event;
	timer_t t;
	long err;
	mm_segment_t savefs;

	if (ev32 == NULL)
		return sys_timer_create(clock, NULL, timer_id);

	memset(&event, 0, sizeof(event));
	if (!access_ok(VERIFY_READ, ev32, sizeof(struct compat_sigevent))
	    || __get_user(event.sigev_value.sival_int,
			  &ev32->sigev_value.sival_int)
	    || __get_user(event.sigev_signo, &ev32->sigev_signo)
	    || __get_user(event.sigev_notify, &ev32->sigev_notify)
	    || __get_user(event.sigev_notify_thread_id,
			  &ev32->sigev_notify_thread_id))
		return -EFAULT;

	if (!access_ok(VERIFY_WRITE, timer_id, sizeof(timer_t)))
		return -EFAULT;

	savefs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_timer_create(clock, &event, &t);
	set_fs(savefs);

	if (err == 0)
		err = __put_user(t, timer_id);

	return err;
}

/*
 * syscall.c
 *
 * Common system call information for the audit daemon.
 *
 * Copyright (C) 2003 SuSE Linux AG
 * Written by okir@suse.de.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include "audit-private.h"

#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/fshooks.h>
#include <linux/wait.h>
#include <linux/sys.h>
#include <linux/utsname.h>
#include <linux/sysctl.h>
#include <linux/sockios.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_frad.h>
#include <linux/route.h>

#include <linux/ipc.h>
#include <linux/msg.h>
#include <linux/shm.h>

#include <asm/uaccess.h>
#include <asm/semaphore.h>

static int		audit_get_ioctlargs(struct aud_syscall_data *);

#define MAX_SOCKETCALL	20
#define MAX_IPCCALL	24

/*
 * Maximum of bytes we copy when logging ioctl's third argument
 */
#define MAX_IOCTL_COPY	256

#define T_void		{ AUDIT_ARG_END }
#define T_immediate(T)	{ AUDIT_ARG_IMMEDIATE, sizeof(T), .sa_flags = (T)-1 < 0 ? AUD_ARG_SIGNED : 0 }
#define T_pointer(T)	{ AUDIT_ARG_POINTER, sizeof(T) }
#define T_pointer_rw(T)	{ AUDIT_ARG_POINTER, sizeof(T), .sa_flags = AUD_ARG_INOUT }
#define T_string	{ AUDIT_ARG_STRING }
#define U_string	{ AUDIT_ARG_STRING, .sa_flags = AUD_ARG_USER }
#define T_path		{ AUDIT_ARG_PATH }
#define T_path_parent	{ AUDIT_ARG_PATH, .sa_flags = AUD_ARG_DIRNAME }
#define T_filedesc	{ AUDIT_ARG_FILEDESC }
#define T_int		T_immediate(int)
#define T_uint		T_immediate(unsigned int)
#define T_long		T_immediate(long)
#define T_ulong		T_immediate(unsigned long)
#define T_loff_t	T_immediate(loff_t)
#define T_mode_t	T_immediate(mode_t)
#define T_size_t	T_immediate(size_t)
#define T_dev_t		T_immediate(dev_t)
#define T_pid_t		T_immediate(pid_t)
#define T_uid_t		T_immediate(uid_t)
#define T_gid_t		T_immediate(gid_t)
#define T_s16_t		T_immediate(int16_t)
#define T_u16_t		T_immediate(uint16_t)
#define T_s32_t		T_immediate(int32_t)
#define T_u32_t		T_immediate(uint32_t)
#define T_s64_t		T_immediate(int64_t)
#define T_u64_t		T_immediate(uint64_t)
#define T_any_ptr	{ AUDIT_ARG_POINTER, 0 }
#define U_any_ptr	{ AUDIT_ARG_POINTER, 0, .sa_flags = AUD_ARG_USER }
#define socklen_t	uint32_t	/* socklen_t is a user land thing */
#define T_socklen_t	T_immediate(socklen_t)
#define T_array(itype, index, max) \
			{ AUDIT_ARG_ARRAY, sizeof(itype), index, max }
#define U_array(itype, index, max) \
			{ AUDIT_ARG_ARRAY, sizeof(itype), index, max, .sa_flags = AUD_ARG_USER }
#define T_opaque_t(idx)	{ AUDIT_ARG_ARRAY, 1, idx, 256 }
#define U_argv		{ AUDIT_ARG_VECTOR, sizeof(char *), .sa_ref = AUDIT_ARG_STRING, .sa_flags = AUD_ARG_USER }
#define T_group_info	{ AUDIT_ARG_GROUP_INFO }
#define T_ipc_msg	{ AUDIT_ARG_IPC_MSG }

/*
 * Note - using [__NR_xxx] to initialize the array makes us more
 * hardware independent. Platform specific syscalls can easily
 * be included in #ifdef __NR_foobar/#endif.
 * The only remaining problem is platforms with more than one
 * exec domain by default.
 */
#define f(name, args...) [AUDIT_##name] = { 0, { args, { AUDIT_ARG_END } } }

static struct sysent linux_sysent[audit_NUM_CALLS] = {
f(clone,	T_uint),
f(execve,	T_path, U_argv, U_any_ptr),
f(ptrace,	T_uint, T_pid_t, U_any_ptr, U_any_ptr),
f(kill,		T_pid_t, T_int),
f(tkill,	T_pid_t, T_int),
f(tgkill,	T_pid_t, T_pid_t, T_int),

/*
 * Calls related to process privilege
 */
f(setuid,	T_uid_t),
f(setgid,	T_gid_t),
f(setreuid,	T_uid_t, T_uid_t),
f(setregid,	T_gid_t, T_gid_t),
f(setresuid,	T_uid_t, T_uid_t, T_uid_t),
f(setresgid,	T_gid_t, T_gid_t, T_gid_t),
f(setfsuid,	T_uid_t),
f(setfsgid,	T_gid_t),
f(setgroups,	T_group_info),
f(capset,	T_u32_t, T_pid_t, T_pointer(kernel_cap_t), T_pointer(kernel_cap_t), T_pointer(kernel_cap_t)),

/*
 * Other per-process state
 */
f(umask,	T_mode_t),
f(chroot,	T_path),
f(chdir,	T_path),
f(fchdir,	T_filedesc),
f(setrlimit,	T_uint, T_pointer(struct rlimit)),
f(setpriority,	T_int, T_int, T_int),
f(setpgid,	T_pid_t, T_pid_t),
f(setsid,	T_void),

/*
 * Calls related to global machine state
 */
f(settimeofday,	T_pointer(struct timeval), T_pointer(struct timezone)),
f(adjtimex,	T_pointer_rw(struct timex)),
f(sethostname,	T_array(char, 1, __NEW_UTS_LEN), T_size_t),
f(setdomainname, T_array(char, 1, __NEW_UTS_LEN), T_size_t),
f(reboot,	T_int, T_int, T_uint, T_any_ptr),
f(init_module,	T_any_ptr, T_ulong, U_string),
f(delete_module, T_string),
f(mount,	T_string, T_path, T_string, T_ulong, T_any_ptr),
f(umount,	T_path, T_int),
f(swapon,	T_path, T_int),
f(swapoff,	T_path),
f(ioperm,	T_ulong, T_ulong, T_int),
f(iopl,		T_uint),
f(syslog,	T_int, U_any_ptr, T_int),
f(acct,	T_path),
f(quotactl,	T_uint, T_string, T_immediate(qid_t), U_any_ptr),

/*
 * File system operations
 */
f(open,		T_path, T_int, T_mode_t),
f(access,	T_path, T_int),
f(mkdir,	T_path_parent, T_mode_t),
f(mknod,	T_path_parent, T_mode_t, T_dev_t),
f(link,		T_path, T_path_parent),
f(symlink,	T_string, T_path_parent),
f(rename,	T_path_parent, T_path_parent),
f(unlink,	T_path_parent),
f(rmdir,	T_path_parent),
f(utimes,	T_path, T_pointer(struct timeval), T_pointer(struct timeval)),
f(chmod,	T_path, T_mode_t),
f(chown,	T_path, T_uid_t, T_gid_t),
f(lchmod,	T_path_parent, T_mode_t),
f(lchown,	T_path_parent, T_uid_t, T_gid_t),
f(fchmod,	T_filedesc, T_mode_t),
f(fchown,	T_filedesc, T_uid_t, T_gid_t),
f(truncate,	T_path, T_loff_t),
f(ftruncate,	T_filedesc, T_loff_t),
f(setxattr,	T_path, U_string, U_array(char, 3, 2046), T_size_t, T_int),
f(lsetxattr,	T_path_parent, U_string, U_array(char, 3, 2046), T_size_t, T_int),
f(fsetxattr,	T_filedesc, U_string, U_array(char, 3, 2046), T_size_t, T_int),
f(removexattr,	T_path, U_string),
f(lremovexattr,	T_path_parent, U_string),
f(fremovexattr,	T_filedesc, U_string),

/*
 * Network stuff
 */
f(bind,		T_filedesc, T_opaque_t(2), T_socklen_t),
f(socket,	T_int, T_int, T_int),

/*
 * SysV IPC
 */
f(shmget,	T_int, T_int, T_int),
f(shmat,	T_int, U_any_ptr, T_int, T_pointer(unsigned long)),
f(shmdt,	U_any_ptr),
f(shmctl,	T_int, T_int, U_any_ptr),
f(semget,	T_int, T_int, T_int),
f(semop,	T_int, T_array(struct sembuf, 2, SEMOPM), T_uint),
f(semtimedop,	T_int, T_array(struct sembuf, 2, SEMOPM), T_uint, T_pointer(struct timespec)),
f(semctl,	T_int, T_int, T_int, U_any_ptr),
f(msgget,	T_int, T_int),
f(msgsnd,	T_int, T_ipc_msg, T_size_t, T_int),
f(msgrcv,	T_int, U_any_ptr, T_size_t, T_long, T_int),
f(msgctl,	T_int, T_int, U_any_ptr),

/*
 * ioctl.
 * The third ioctl argument is frobbed in audit_get_ioctlargs below
 */
f(ioctl,	T_filedesc, T_uint, U_any_ptr),

};

static int
audit_fshook_pre(fshook_info_t info, void *ctx)
{
	if (!AUDITING(current))
		return 0;

	switch(info.gen->type) {
	case FSHOOK_access:
		audit_intercept(AUDIT_access, info.access->path, info.access->mode);
		break;
	case FSHOOK_chdir:
		audit_intercept(AUDIT_chdir, info.chdir->dirname);
		break;
	case FSHOOK_chmod:
		audit_intercept(info.chmod->link ? AUDIT_lchmod : AUDIT_chmod,
			info.chmod->path,
			info.chmod->mode);
		break;
	case FSHOOK_chown:
		audit_intercept(info.chown->link ? AUDIT_lchown : AUDIT_chown,
			info.chown->path,
			info.chown->uid,
			info.chown->gid);
		break;
	case FSHOOK_chroot:
		audit_intercept(AUDIT_chroot, info.chroot->path);
		break;
	case FSHOOK_fchdir:
		audit_intercept(AUDIT_fchdir, info.fchdir->fd);
		break;
	case FSHOOK_fchmod:
		audit_intercept(AUDIT_fchmod, info.fchmod->fd, info.fchmod->mode);
		break;
	case FSHOOK_fchown:
		audit_intercept(AUDIT_fchown, info.fchown->fd, info.fchown->uid, info.fchown->gid);
		break;
	case FSHOOK_frmxattr:
		audit_intercept(AUDIT_fremovexattr, info.frmxattr->fd, info.frmxattr->name);
		break;
	case FSHOOK_fsetxattr:
		audit_intercept(AUDIT_fsetxattr,
			info.fsetxattr->fd,
			info.fsetxattr->name,
			info.fsetxattr->value,
			info.fsetxattr->size,
			info.fsetxattr->flags);
		break;
	case FSHOOK_ftruncate:
		audit_intercept(AUDIT_ftruncate, info.ftruncate->fd, info.ftruncate->length);
		break;
	case FSHOOK_ioctl:
		audit_intercept(AUDIT_ioctl, info.ioctl->fd, info.ioctl->cmd, info.ioctl->arg.ptr);
		break;
	case FSHOOK_link:
		audit_intercept(AUDIT_link, info.link->oldpath, info.link->newpath);
		break;
	case FSHOOK_mkdir:
		audit_intercept(AUDIT_mkdir, info.mkdir->dirname);
		break;
	case FSHOOK_mknod:
		audit_intercept(AUDIT_mknod, info.mknod->path, info.mknod->mode, info.mknod->dev);
		break;
	case FSHOOK_mount:
		audit_intercept(AUDIT_mount,
			info.mount->devname,
			info.mount->dirname,
			info.mount->type,
			info.mount->flags,
			info.mount->data);
		break;
	case FSHOOK_open:
		audit_intercept(AUDIT_open, info.open->filename, info.open->flags, info.open->mode);
		break;
	case FSHOOK_rename:
		audit_intercept(AUDIT_rename, info.rename->oldpath, info.rename->newpath);
		break;
	case FSHOOK_rmdir:
		audit_intercept(AUDIT_rmdir, info.rmdir->dirname);
		break;
	case FSHOOK_rmxattr:
		audit_intercept(info.rmxattr->link ? AUDIT_lremovexattr : AUDIT_removexattr,
			info.rmxattr->path,
			info.rmxattr->name);
		break;
	case FSHOOK_setxattr:
		audit_intercept(info.setxattr->link ? AUDIT_lsetxattr : AUDIT_setxattr,
			info.setxattr->path,
			info.setxattr->name,
			info.setxattr->value,
			info.setxattr->size,
			info.setxattr->flags);
		break;
	case FSHOOK_symlink:
		audit_intercept(AUDIT_symlink, info.symlink->oldpath, info.symlink->newpath);
		break;
	case FSHOOK_truncate:
		audit_intercept(AUDIT_truncate, info.truncate->filename, info.truncate->length);
		break;
	case FSHOOK_umount:
		audit_intercept(AUDIT_umount, info.umount->dirname, info.umount->flags);
		break;
	case FSHOOK_unlink:
		audit_intercept(AUDIT_unlink, info.unlink->filename);
		break;
	case FSHOOK_utimes:
		audit_intercept(AUDIT_utimes, info.utimes->path, info.utimes->atime, info.utimes->mtime);
		break;
/*
	case FSHOOK_:
		audit_intercept(AUDIT_, info.->, info.->, info.->);
		break;
*/
	default:
		break;
	}
	return 0;
}

static void
audit_fshook_post(fshook_info_t info, void *ctx)
{
	if (AUDITING(current))
		audit_result(info.gen->result);
}

int
audit_fshook_adjust(enum audit_call code, signed adjust)
{
#define AUDIT_rmxattr AUDIT_removexattr
#define AUDIT_frmxattr AUDIT_fremovexattr
#define AUDIT_lrmxattr AUDIT_lremovexattr
	static struct hookXlat {
		enum { invalid = -1, unhooked, hooked, failed, link } state:8;
		unsigned count:8;
		enum FShook hook:16;
	} hookXlat[audit_NUM_CALLS] = {
		[0 ... (audit_NUM_CALLS - 1)] = { invalid },
#define HOOK(name) [AUDIT_##name] = { .state = unhooked, .hook = FSHOOK_##name }
#define LINK(name) HOOK(name), [AUDIT_l##name] = { .state = link, .hook = AUDIT_##name }
		HOOK(access),
		HOOK(chdir),
		LINK(chmod),
		LINK(chown),
		HOOK(chroot),
		HOOK(fchdir),
		HOOK(fchmod),
		HOOK(fchown),
		HOOK(frmxattr),
		HOOK(fsetxattr),
		HOOK(ftruncate),
		HOOK(ioctl),
		HOOK(link),
		HOOK(mkdir),
		HOOK(mknod),
		HOOK(mount),
		HOOK(rename),
		HOOK(rmdir),
		LINK(rmxattr),
		LINK(setxattr),
		HOOK(symlink),
		HOOK(open),
		HOOK(umount),
		HOOK(unlink),
		HOOK(utimes),
		HOOK(truncate)
#undef LINK
#undef HOOK
	};
#undef AUDIT_lrmxattr
#undef AUDIT_frmxattr
#undef AUDIT_rmxattr
	struct hookXlat *xlat = hookXlat + code;
	int err = 0;

	if (code < 0 || code >= audit_NUM_CALLS)
		return -EINVAL;
	if (xlat->state == link) {
		xlat = hookXlat + xlat->hook;
		BUG_ON(xlat->state == link);
	}
	if (xlat->state == invalid)
		return 0;
	switch (adjust) {
	case -1:
		if (unlikely(!xlat->count))
			err = -EPERM;
		else if (!--xlat->count) {
			if (xlat->state == hooked) {
				err = fshook_deregister(xlat->hook, audit_fshook_pre, audit_fshook_post, NULL);
				if (unlikely(err))
					printk(KERN_ERR "audit: unable to de-register fshook %u\n", xlat->hook);
				else {
					DPRINTF("de-registered fshook %u\n", xlat->hook);
					xlat->state = unhooked;
				}
			}
			else
				xlat->state = unhooked;
		}
		break;
	case 0:
		if (xlat->state != failed)
			break;
		BUG_ON(!xlat->count);
	case 1:
		if (xlat->state != hooked) {
			err = fshook_register(xlat->hook, audit_fshook_pre, audit_fshook_post, NULL);
			if (unlikely(err)) {
				printk(KERN_ERR "audit: unable to register fshook %u\n", xlat->hook);
				xlat->state = failed;
			}
			else {
				DPRINTF("registered fshook %u\n", xlat->hook);
				xlat->state = hooked;
			}
		}
		xlat->count += adjust;
		BUG_ON(!xlat->count);
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}

/*
 * Initialize system call handling
 */
void
audit_init_syscalls(void)
{
	unsigned int m, n;

	/* Loop over list of syscalls and fill in the number of
	 * arguments */
	for (m = 0; m < audit_NUM_CALLS; m++) {
		struct sysent *entry = &linux_sysent[m];

		for (n = 0; n < AUDIT_MAXARGS; n++) {
			if (entry->sy_args[n].sa_type == AUDIT_ARG_END)
				break;
		}
		entry->sy_narg = n;
	}
}

/*
 * Get syscall arguments
 */
int
audit_get_args(enum audit_call code, va_list varg, struct aud_syscall_data *sc)
{
	const struct sysent *entry;
	unsigned i;

	if (code < 0 || code >= audit_NUM_CALLS)
		return -ENOSYS;

	sc->entry = entry = linux_sysent + (sc->major = code);
	for (i = 0; i < entry->sy_narg; ++i) {
#ifndef __ILP64__
		typedef char assert_int_32bits[(int)sizeof(int) - 4];
#else
		typedef char assert_int_64bits[(int)sizeof(int) - 8];
#endif

		switch(entry->sy_args[i].sa_type) {
		case AUDIT_ARG_IMMEDIATE:
			if (unlikely(entry->sy_args[i].sa_size <= 0)) {
				printk(KERN_ERR "audit: internal inconsistency: immediate argument of non-positive size\n");
				return -ENODATA;
			}
			if (entry->sy_args[i].sa_size < sizeof(int)) {
		case AUDIT_ARG_FILEDESC:
				sc->raw_args[i] = va_arg(varg, int);
				continue;
			}
			if (entry->sy_args[i].sa_flags & AUD_ARG_SIGNED) {
				switch (entry->sy_args[i].sa_size) {
				case sizeof(int):
					sc->raw_args[i] = va_arg(varg, int);
					continue;
#ifndef __ILP64__
				case sizeof(int64_t):
					sc->raw_args[i] = va_arg(varg, int64_t);
					continue;
#endif
				}
			}
			else {
				switch (entry->sy_args[i].sa_size) {
				case sizeof(int):
					sc->raw_args[i] = va_arg(varg, unsigned int);
					continue;
#ifndef __ILP64__
				case sizeof(int64_t):
					sc->raw_args[i] = va_arg(varg, uint64_t);
					continue;
#endif
				}
			}
			printk(KERN_ERR "audit: internal inconsistency: argument of size %zu\n", entry->sy_args[i].sa_size);
			return -ENODATA;
		case AUDIT_ARG_POINTER:
		case AUDIT_ARG_STRING:
		case AUDIT_ARG_PATH:
		case AUDIT_ARG_ARRAY:
		case AUDIT_ARG_VECTOR:
		case AUDIT_ARG_GROUP_INFO:
		case AUDIT_ARG_IPC_MSG:
			sc->raw_args[i] = (unsigned long)va_arg(varg, void *);
			continue;
		}
		printk(KERN_ERR "audit: internal inconsistency: argument of type %u\n", entry->sy_args[i].sa_type);
		return -EINVAL;
	}

	return code != AUDIT_ioctl ? 0 : audit_get_ioctlargs(sc);
}

/*
 * Get ioctl arguments (well, basically we want the third argument's size,
 * and whether it's a read or write ioctl
 */
static const struct ioctl_info {
	unsigned		cmd;
	size_t		size;
} ioctl_info[] = {
      { SIOCSIFNAME,		sizeof(struct ifreq) },

      { SIOCSIFFLAGS,		sizeof(struct ifreq) },
      { SIOCSIFADDR,		sizeof(struct ifreq) },
      { SIOCSIFDSTADDR,		sizeof(struct ifreq) },
      { SIOCSIFBRDADDR,		sizeof(struct ifreq) },
      { SIOCSIFNETMASK,		sizeof(struct ifreq) },
      { SIOCSIFMETRIC,		sizeof(struct ifreq) },
      { SIOCSIFMTU,		sizeof(struct ifreq) },
      { SIOCADDMULTI,		sizeof(struct ifreq) },
      { SIOCDELMULTI,		sizeof(struct ifreq) },
      { SIOCADDRT,		sizeof(struct rtentry) },
      { SIOCDELRT,		sizeof(struct rtentry) },

      { SIOCSIFHWADDR,		sizeof(struct ifreq) },
#ifdef SIOCSIFHWBROADCAST
      { SIOCSIFHWBROADCAST,	sizeof(struct ifreq) },
#endif
      { SIOCSIFMAP,		sizeof(struct ifreq) },
      { SIOCSIFMEM,		sizeof(struct ifreq) },
      { SIOCSIFENCAP,		sizeof(struct ifreq) },
      { SIOCSIFSLAVE,		sizeof(struct ifreq) },
      { SIOCSIFPFLAGS,		sizeof(struct ifreq) },
      { SIOCDIFADDR,		sizeof(struct ifreq) },
      { SIOCSIFBR,		3 * sizeof(long) },
      { SIOCGIFBR,		3 * sizeof(long) },

      { SIOCSARP,		sizeof(struct arpreq) },
      { SIOCDARP,		sizeof(struct arpreq) },

      /* SIOCDRARP, SIOCSRARP obsolete */
#ifdef CONFIG_DLCI
      { SIOCADDDLCI,		sizeof(struct dlci_add) },
      { SIOCDELDLCI,		sizeof(struct dlci_add) },
#endif

      /* SIOCSIFLINK obsolete? */
      { SIOCSIFLINK,		0 },
      { SIOCSIFTXQLEN,		sizeof(struct ifreq) },
      { SIOCBONDENSLAVE,	sizeof(struct ifreq) },
      { SIOCBONDRELEASE,	sizeof(struct ifreq) },
      { SIOCBONDSETHWADDR,	sizeof(struct ifreq) },
      { SIOCBONDCHANGEACTIVE,	sizeof(struct ifreq) },
      { SIOCETHTOOL,		sizeof(struct ifreq) },
      { SIOCSMIIREG,		sizeof(struct ifreq) },
};

int
audit_get_ioctlargs(struct aud_syscall_data *sc)
{
	const struct ioctl_info	*iop;
	unsigned			cmd;
	void			*arg, *p;
	size_t			len = 0;

	cmd = sc->raw_args[1];
	arg = (void *) (unsigned long) sc->raw_args[2];

	if (arg == NULL)
		return 0;

	for (iop = ioctl_info; iop < ioctl_info + ARRAY_SIZE(ioctl_info); iop++) {
		if (iop->cmd == cmd) {
			len = iop->size;
			break;
		}
	}

	if (len == 0 && (_IOC_DIR(cmd) & _IOC_WRITE)) 
		len = _IOC_SIZE(cmd);

	if (len > 0 && len < MAX_IOCTL_COPY) {
		struct sysarg_data	*tgt = &sc->args[2];

		if ((p = kmalloc(len, GFP_USER)) == NULL)
			return -ENOBUFS;
		if (copy_from_user(p, arg, len)) {
			kfree(p);
			return -EFAULT;
		}
		tgt->at_type = AUDIT_ARG_POINTER;
		tgt->at_data.ptr = p;
		tgt->at_data.len = len;
	}
	return 0;
}

/*
 * audit-private.h
 *
 * Copyright (C) 2003 SuSE Linux AG
 * Written by okir@suse.de, based on ideas from systrace, by
 * Niels Provos (OpenBSD) and ported to Linux by Marius Aamodt Eriksen.
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

#ifndef AUDITPRIVATE_H
#define AUDITPRIVATE_H

#include <linux/audit.h>
#include <linux/list.h>
#include <linux/sys.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <stdarg.h>

struct inode;
struct poll_table_struct;

/* Lock debugging will go away if all the SMP bugs are gone */
#ifndef DONT_DEBUG_LOCKS
# undef AUDIT_DEBUG_LOCKS
#endif
#include "debug-locks.h"

#undef DEBUG_FILTER

#define DPRINTF(fmt, args...) \
	(unlikely(audit_debug) ? printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ##args) : (void)0)

/*
 * Audit policy for system calls and other events.
 * You can either set an action unconditionally, or
 * attach an additional filter
 */
struct aud_policy {
	unsigned int		action;
	struct aud_filter *	filter;
};

/*
 * State attached to an open audit file handle
 */
struct aud_context {
	unsigned int		reader;
};

struct aud_msg_head {
	struct list_head	list;
	struct aud_message	body;
};

/*
 * System call table, and information on system
 * call arguments.
 */

#define AUDIT_MAXARGS           8
#define AUD_ARG_DIRNAME		0x0001
#define AUD_ARG_SIGNED		0x0002
#define AUD_ARG_INOUT		0x0004
#define AUD_ARG_USER		0x0008

struct sysarg {
	int		sa_type;
	size_t		sa_size;

	/* For ARRAY type arguments, sa_ref is the index
	 * of the argument containing the number of array
	 * items (must be of type IMMEDIATE).
	 * sa_max contains the max number of items in this array.
	 *
	 * For VECTOR type arguments, sa_ref is the type
	 * of the vector elements.
	 */
	unsigned int	sa_ref;
	unsigned int	sa_max;

	/* Various flags */
	unsigned int	sa_flags;
};

/* After copying arguments to kernel space, we store
 * them in this struct.
 * Note that pointers must be the first item in every
 * struct within union u.
 */
struct sysarg_data {
	enum audit_arg	at_type;
	unsigned int		at_flags;
	union {
		uint64_t		integer;
		struct {
			char *		ptr;
			size_t		len;
		} data;
		struct {
			char *		name;
			size_t		len;
			struct dentry *	dentry;
			unsigned char	exists : 1;
		} path;
		struct {
			struct sysarg_data *elements;
			size_t		count;
		} vector;
	} u;
};
#define at_intval	u.integer
#define at_strval	u.data.ptr
#define at_data		u.data
#define at_path		u.path
#define at_vector	u.vector


struct sysent {
	unsigned int	sy_narg;
	struct sysarg	sy_args[AUDIT_MAXARGS];
};

/*
 * This struct contains all information on a system
 * call.
 */
struct aud_syscall_data {
	int			personality;
	int			arch;
	int			major, minor;
	long			result;
	int			flags;
	const struct sysent * entry;
	uint64_t		raw_args[AUDIT_MAXARGS];
	struct sysarg_data	args[AUDIT_MAXARGS];
};

struct aud_event_data {
	char			name[AUD_MAX_EVNAME];
	struct aud_syscall_data	*syscall;
	struct sk_buff *	netconf;
	int			exit_status;
};

struct aud_process {
	struct list_head	list;

	uid_t			audit_uid;
	unsigned int		audit_id;

	unsigned long		flags;
	/* Auditing suspended? */
	unsigned char		suspended;

	/* Data on system call currently in progress */
	struct aud_syscall_data	syscall;
};

#define AUD_F_ATTACHED		0x0001

extern int		audit_debug;
extern int		audit_all_processes;
extern unsigned int	audit_max_messages;
extern int		audit_allow_suspend;
extern int		audit_message_enabled;
extern int		audit_paranoia;

/* VFS interface */
int			auditf_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
ssize_t			auditf_read(struct file *, char *, size_t, loff_t *);
ssize_t			auditf_write(struct file *, const char *, size_t, loff_t *);
int			auditf_open(struct inode *, struct file *);
int			auditf_release(struct inode *, struct file *);
unsigned int		auditf_poll(struct file *, struct poll_table_struct *);

/* Policy handling */
int			audit_policy_clear(void);
int			audit_policy_set(const struct audit_policy __user *);
int			audit_policy_check(int, struct aud_event_data *);
int			audit_policy_ignore(int);

/* Message utility functions */
int			audit_msg_enable(void);
void			audit_msg_disable(void);
struct aud_msg_head * audit_msg_new(struct aud_process *, int, const char *, size_t);
struct aud_msg_head * audit_msg_get(int, size_t);
int			audit_msg_poll(struct file *,
					struct poll_table_struct *);
void			audit_msg_release(struct aud_msg_head *);
void			audit_msg_insert(struct aud_msg_head *);
int			audit_msg_fork(struct aud_process *, pid_t);
int			audit_msg_exit(struct aud_process *, const char *,
	       				long);
int			audit_msg_syscall(struct aud_process *, const char *,
					struct aud_syscall_data *);
int			audit_msg_result(struct aud_process *, const char *,
	       				int);
int			audit_msg_login(struct aud_process *, const char *,
	       				const struct audit_login *);
int			audit_msg_netlink(struct aud_process *, const char *,
					const struct sk_buff *, int);

int			audit_attach(int);
int			audit_detach(void);
int			audit_suspend(void);
int			audit_resume(void);
int			audit_setauditid(void);
int			audit_login(const struct audit_login __user *);
int			audit_user_message(const struct audit_message __user *);

void			audit_init_syscalls(void);
int			audit_fshook_adjust(enum audit_call, signed adjust);
int			audit_get_args(enum audit_call, va_list,
				struct aud_syscall_data *);
struct sysarg_data * audit_get_argument(struct aud_syscall_data *,
				unsigned int);
int			audit_copy_arguments(struct aud_syscall_data *);
void			audit_release_arguments(struct aud_process *);
int			audit_encode_args(void *, size_t,
				struct aud_syscall_data *);

int			audit_filter_add(const struct audit_filter __user *);
int			audit_filter_eval(const struct aud_filter *,
				struct aud_event_data *);
struct aud_filter * audit_filter_get(unsigned int);
void			audit_filter_put(struct aud_filter *);
int			audit_filter_clear(void);

struct aud_file_object * audit_fileset_add(const char *);
void			audit_fileset_release(struct aud_file_object *);
int			audit_fileset_match(struct aud_file_object *,
				struct sysarg_data *);
void			audit_fileset_unlock(int invalidate);

extern int		audit_sysctl_register(void);
extern void		audit_sysctl_unregister(void);

#ifdef CONFIG_COMPAT
extern int		audit_register_ioctl_converters(void);
extern int		audit_unregister_ioctl_converters(void);
#else
#define audit_register_ioctl_converters() 0
#define audit_unregister_ioctl_converters() 0
#endif

struct nameidata;
int			audit_path_init(const char *, unsigned int, struct nameidata *);

int audit_check(const char*filename, int line);
#define audit_check() audit_check(__FILE__, __LINE__)

/*
 * Function hooks
 */
#ifdef CONFIG_AUDIT_MODULE
struct audit_hooks {
	int		(*intercept)(enum audit_call, va_list);
	long		(*result)(long);
	void		(*fork)(struct task_struct *, struct task_struct *);
	void		(*exit)(struct task_struct *, long);
	void		(*netlink_msg)(struct sk_buff *, int);
};

extern int		audit_register(const struct audit_hooks *);
extern void		audit_unregister(void);
#endif

/*
 * Kill a process.
 * This is called when for some reason we were unable to audit a
 * system call (e.g. because the address was bad).
 *
 * This is roughly the equivalent of sig_exit
 */
static __inline__ void
audit_kill_process(int error)
{
	printk(KERN_NOTICE "audit_intercept: error %d, killing task %d\n", -error, current->pid);
	sigaddset(&current->pending.signal, SIGKILL);
	recalc_sigpending();
	current->flags |= PF_SIGNALED;
	complete_and_exit(NULL, SIGKILL);
}

#if defined(CONFIG_X86_64)
#define AUDIT_ARCH AUDIT_ARCH_X86_64
#define AUDIT_ARCH32 AUDIT_ARCH_I386
#elif defined(CONFIG_X86)
#define AUDIT_ARCH AUDIT_ARCH_I386
#elif defined(CONFIG_PPC64)
#define AUDIT_ARCH AUDIT_ARCH_PPC64
#define AUDIT_ARCH32 AUDIT_ARCH_PPC
#elif defined(CONFIG_PPC)
#define AUDIT_ARCH AUDIT_ARCH_PPC
#elif defined(CONFIG_ARCH_S390X)
#define AUDIT_ARCH AUDIT_ARCH_S390X
#define AUDIT_ARCH32 AUDIT_ARCH_S390
#elif defined(CONFIG_ARCH_S390)
#define AUDIT_ARCH AUDIT_ARCH_S390
#elif defined(CONFIG_IA64)
#define AUDIT_ARCH AUDIT_ARCH_IA64
#define AUDIT_ARCH32 AUDIT_ARCH_I386
#endif
#ifndef AUDIT_ARCH32
#define AUDIT_ARCH32 AUDIT_ARCH
#endif

/*
 * See if the process doing this call is 32bit or 64bit
 */
static __inline__ unsigned int
audit_syscall_word_size(struct aud_syscall_data *sc)
{
#if BITS_PER_LONG == 64
	return sc->arch == AUDIT_ARCH ? 64 : 32;
#else
	return 32;
#endif
}

#endif /* AUDITPRIVATE_H */

/*
 * message.c
 *
 * Linux Audit Subsystem, message passing stuff
 *
 *
 * Copyright (C) 2003 SuSE Linux AG
 *
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

#include <linux/version.h>
#include <linux/config.h>

#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>

#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include <linux/audit.h>

#include "audit-private.h"

/* This looks a lot like wait_queue handling, but I couldn't
 * get add_wait_queue_exclusive() to work... */
struct audit_writer {
	struct list_head	entry;
	task_t *	task;
	int		status;
};

int				audit_message_enabled;
unsigned int			audit_max_messages = 1024;
static spinlock_t		audit_message_lock = SPIN_LOCK_UNLOCKED;
static unsigned int		audit_message_count;
static unsigned int		audit_message_seq;
static DECLARE_WAIT_QUEUE_HEAD(audit_reader_wait);
static LIST_HEAD(audit_messages);
static LIST_HEAD(audit_writers);

/*
 * Enable/disable audit messages
 */
int
audit_msg_enable(void)
{
	int	res = -EBUSY;

	spin_lock(&audit_message_lock);
	if (!audit_message_enabled) {
		audit_message_enabled = 1;
		res = 0;
		mb();
	}
	spin_unlock(&audit_message_lock);
	return res;
}

/*
 * Wake up the first writer
 */
static inline int
__audit_wake_writer(int res)
{
	struct audit_writer *w;

	if (list_empty(&audit_writers))
		return 0;

	w = list_entry(audit_writers.next, struct audit_writer, entry);
	DPRINTF("wakeup %p\n", w);
	w->status = res;
	list_del_init(&w->entry);
	wake_up_process(w->task);

	return 1;
}

void
audit_msg_disable(void)
{
	spin_lock(&audit_message_lock);
	audit_message_enabled = 0;

	/* Inform all processes waiting to deliver
	 * messages to the audit daemon that we're
	 * no longer interested */
	while (__audit_wake_writer(-ENODEV))
		;

	spin_unlock(&audit_message_lock);
	mb();

	/* There shouldn't be any processes left on the
	 * reader queue, but we wake them up anyway */
	wake_up(&audit_reader_wait);
}

/*
 * Allocate a new audit message head
 *
 * Beware - pinfo may be NULL when called from
 * audit_user_message().
 */
struct aud_msg_head *
audit_msg_new(struct aud_process *pinfo, int type,
	       const char *evname, size_t size)
{
	struct aud_msg_head *msgh;
	struct aud_message *msg;
	unsigned int seqno;

	spin_lock(&audit_message_lock);
	if (audit_message_count < audit_max_messages) {
		audit_message_count++;
	} else {
		struct audit_writer writer;

		if (!audit_message_enabled) {
			spin_unlock(&audit_message_lock);
			return ERR_PTR(-ENODEV);
		}
		DPRINTF("%p waiting for wakeup (%u, pid=%d)\n", &writer, audit_message_count, current->pid);
		writer.task = current;
		list_add_tail(&writer.entry, &audit_writers);

		set_current_state(TASK_UNINTERRUPTIBLE);

		spin_unlock(&audit_message_lock);
		schedule();

		if (writer.status < 0)
			return ERR_PTR(writer.status);

		spin_lock(&audit_message_lock);

		/* When we get here, we know we are permitted
		 * to allocate a message.
		 * audit_message_count has already been adjusted. */
	}

	if (audit_message_count > audit_max_messages)
		printk(KERN_WARNING "Oops, too many audit messages\n");

	seqno = audit_message_seq++;
	spin_unlock(&audit_message_lock);

	while (1) {
		msgh = kmalloc(sizeof(*msgh) + size, GFP_KERNEL);
		if (msgh != NULL)
			break;
		schedule_timeout(HZ / 2);
	}

	INIT_LIST_HEAD(&msgh->list);
	memset(&msgh->body, 0, sizeof(msgh->body));

	msg = &msgh->body;
	msg->msg_pid		= current->pid;
	msg->msg_type		= type;
	msg->msg_size		= sizeof(msgh->body) + size;
	msg->msg_seqnr		= seqno;
	msg->msg_timestamp	= CURRENT_TIME.tv_sec;
	msg->msg_arch		= AUDIT_ARCH;

	msg->msg_audit_id	= pinfo? pinfo->audit_id : -1;
	msg->msg_login_uid	= pinfo? pinfo->audit_uid : -1;
	msg->msg_euid		= current->euid;
	msg->msg_ruid		= current->uid;
	msg->msg_suid		= current->suid;
	msg->msg_fsuid		= current->fsuid;
	msg->msg_egid		= current->egid;
	msg->msg_rgid		= current->gid;
	msg->msg_sgid		= current->sgid;
	msg->msg_fsgid		= current->fsgid;

	if (evname && *evname)
		strncpy(msg->msg_evname, evname, sizeof(msg->msg_evname));

	return msgh;
}

void
audit_msg_insert(struct aud_msg_head *msgh)
{
	spin_lock(&audit_message_lock);
	list_add_tail(&msgh->list, &audit_messages);
	spin_unlock(&audit_message_lock);

	/* Wake up the next reader in the queue */
	wake_up(&audit_reader_wait);
}

int
audit_msg_poll(struct file *file, struct poll_table_struct *wait)
{
	unsigned int ret = 0;

	poll_wait(file, &audit_reader_wait, wait);

	spin_lock(&audit_message_lock);
	if (!list_empty(&audit_messages))
		ret = 1;
	spin_unlock(&audit_message_lock);

	return (ret);
}

/*
 * Get the next message from the queue
 */
struct aud_msg_head *
audit_msg_get(int block, size_t max_len)
{
	struct aud_msg_head *msgh;
	DECLARE_WAITQUEUE(wait, current);

	/* Wait non-exclusively */
	add_wait_queue(&audit_reader_wait, &wait);
	spin_lock(&audit_message_lock);
	while (1) {
		msgh = list_empty(&audit_messages)
		       ? ERR_PTR(-EAGAIN)
		       : list_entry(audit_messages.next, struct aud_msg_head, list);
		if (!IS_ERR(msgh)) {
			if (max_len == 0 || msgh->body.msg_size < max_len) {
				list_del_init(&msgh->list);
				break;
			}
			msgh = ERR_PTR(-EAGAIN);
		}

		if (!block)
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock(&audit_message_lock);
		schedule();
		spin_lock(&audit_message_lock);

		msgh = ERR_PTR(-ERESTARTSYS);
		if (signal_pending(current))
			break;
	}
	spin_unlock(&audit_message_lock);

	current->state = TASK_RUNNING;
	remove_wait_queue(&audit_reader_wait, &wait);

	return msgh;
}

void
audit_msg_release(struct aud_msg_head *msgh)
{
	spin_lock(&audit_message_lock);
	if (msgh)
		list_del(&msgh->list);

	/* Wake up the next writer in the queue */
	if (!__audit_wake_writer(0))
		audit_message_count--;

	spin_unlock(&audit_message_lock);


	if (msgh)
		kfree(msgh);
}

/*
 * Send a login message
 */
int
audit_msg_login(struct aud_process *pinfo, const char *evname,
	       	const struct audit_login *login)
{
	struct aud_msg_head	*msgh;
	struct aud_msg_login	*login_msg;

	msgh = audit_msg_new(pinfo, AUDIT_MSG_LOGIN, evname,
		       	sizeof(*login_msg));
	if (IS_ERR(msgh))
		return PTR_ERR(msgh);

	login_msg = (struct aud_msg_login *) msgh->body.msg_data;
	memset(login_msg, 0, sizeof(*login_msg));
	login_msg->uid = login->uid;
	memcpy(login_msg->hostname, login->hostname, sizeof(login->hostname));
	memcpy(login_msg->address, login->address, sizeof(login->address));
	memcpy(login_msg->terminal, login->terminal, sizeof(login->terminal));

	/* fill in the executable name */
	if (current->mm) {
		struct vm_area_struct	*mmap;
		struct file		*file;
		char			*str;
		int			len;

		down_read(&current->mm->mmap_sem);
		if ((mmap = current->mm->mmap)
		 && (file = mmap->vm_file)
		 && file->f_dentry) {
			str = d_path(file->f_dentry, file->f_vfsmnt,
					login_msg->executable,
					sizeof(login_msg->executable));

			if (str && str != login_msg->executable) {
				len = strlen(str);
				memmove(login_msg->executable, str, len+1);
			}
		}
		up_read(&current->mm->mmap_sem);
	}

	audit_msg_insert(msgh);
	return 0;
}

/*
 * Send an exit event to user land
 */
int
audit_msg_exit(struct aud_process *pinfo, const char *evname, long code)
{
	struct aud_msg_head	*msgh;
	struct aud_msg_exit	*exit_msg;

	msgh = audit_msg_new(pinfo, AUDIT_MSG_EXIT,
		       		evname, sizeof(*exit_msg));
	if (IS_ERR(msgh))
		return PTR_ERR(msgh);

	exit_msg = (struct aud_msg_exit *) msgh->body.msg_data;
	exit_msg->code = code;

	audit_msg_insert(msgh);
	return 0;
}

/*
 * Log a system call, along with all arguments
 */
int
audit_msg_syscall(struct aud_process *pinfo,
                  const char *evname,
                  struct aud_syscall_data *syscall)
{
	struct aud_msg_head	*msgh;
	struct aud_msg_syscall	*syscall_msg;
	int			len;

	DPRINTF("called, syscall %d/%d, pid %d\n",
		 syscall->major, syscall->minor, current->pid);

	len = audit_encode_args(NULL, 0, syscall);
	if (len < 0)
		return len;

	/* Allocate aud_process with message buffer and
	 * additional space for arguments. */
	msgh = audit_msg_new(pinfo, AUDIT_MSG_SYSCALL,
				evname, sizeof(*syscall_msg)+len);
	if (IS_ERR(msgh)) {
		DPRINTF("audit_msg_new failed, err=%ld\n", PTR_ERR(msgh));
		return PTR_ERR(msgh);
	}

	msgh->body.msg_arch = syscall->arch;
	switch (syscall->major) {
	case AUDIT_access:
		msgh->body.msg_fsuid = syscall->raw_args[syscall->entry->sy_narg];
		msgh->body.msg_fsgid = syscall->raw_args[syscall->entry->sy_narg + 1];
		break;
	}
	syscall_msg = (struct aud_msg_syscall *) msgh->body.msg_data;
	syscall_msg->personality = syscall->personality;
	syscall_msg->major = syscall->major;
	syscall_msg->minor = syscall->minor;
	syscall_msg->result = syscall->result;
	syscall_msg->length = len;

	/* Encode arguments now */
	len = audit_encode_args(syscall_msg->data, len, syscall);
	if (len < 0) {
		DPRINTF("Failed to encode args (pass #2, err=%d)\n", len);
		audit_msg_release(msgh);
		return len;
	}

	audit_msg_insert(msgh);
	return 0;
}

/*
 * Log an rtnetlink call
 */
int
audit_msg_netlink(struct aud_process *pinfo, const char *evname,
	       		const struct sk_buff *skb, int result)
{
	struct aud_msg_head	*msgh;
	struct aud_msg_netlink	*netlink_msg;
	unsigned int		len;

	DPRINTF("called.\n");

	len = skb->len;
	msgh = audit_msg_new(pinfo, AUDIT_MSG_NETLINK, evname,
				sizeof(*netlink_msg) + len);
	if (IS_ERR(msgh)) {
		DPRINTF("audit_msg_new failed, err=%ld\n", PTR_ERR(msgh));
		return PTR_ERR(msgh);
	}

	netlink_msg = (struct aud_msg_netlink *) msgh->body.msg_data;
	skb_copy_bits(skb, 0, netlink_msg->data, len);
	netlink_msg->groups = NETLINK_CB(skb).groups;
	netlink_msg->dst_groups = NETLINK_CB(skb).dst_groups;
	netlink_msg->result = result;
	netlink_msg->length = len;

	audit_msg_insert(msgh);
	return 0;
}

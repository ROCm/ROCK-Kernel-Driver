/*
 * filter.c - system call filtering for the audit subsystem
 *
 * For all functions in this file, the caller must hold the global
 * audit_lock, either read locked (for audit_filter_eval), or write
 * locked (for all other calls).
 *
 * Copyright (C) 2003 SuSE Linux AG
 *
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

#include <linux/version.h>
#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/sys.h>
#include <linux/miscdevice.h>
#include <linux/audit.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/sock.h>

#include <asm/semaphore.h>
#include <asm/uaccess.h>
#include <asm/ptrace.h>

#include "audit-private.h"

struct aud_filter {
	struct list_head	af_link;
	atomic_t		af_refcnt;

	unsigned short		af_num;
	unsigned short		af_op;

	char *			af_evname;

	union {
		struct f_apply {
			unsigned int		target;
			struct aud_filter *	predicate;
		} apply;
		struct f_bool {
			struct aud_filter *	left;
			struct aud_filter *	right;
		} boolean;
		struct f_return {
			unsigned int		action;
		} freturn;
		struct f_intop {
			uint64_t		value, mask;
		} intop;
		struct f_strop {
			char *			value;
			int			len;
		} strop;
		struct f_fileop {
			struct aud_file_object *file;
		} fileop;
	} u;
};
#define af_left		u.boolean.left
#define af_right	u.boolean.right
#define af_target	u.apply.target
#define af_predicate	u.apply.predicate
#define af_action	u.freturn.action
#define af_intval	u.intop.value
#define af_intmask	u.intop.mask
#define af_strval	u.strop.value
#define af_strlen	u.strop.len
#define af_file		u.fileop.file

static LIST_HEAD(filter_list);


static void	__audit_filter_free(struct aud_filter *f);

/*
 * Find an audit filter and bump the reference count.
 * Must be locked when we get here
 */
static struct aud_filter *
__audit_filter_find(unsigned int num)
{
	struct aud_filter	*f;
	struct list_head	*pos;

	list_for_each(pos, &filter_list) {
		f = list_entry(pos, struct aud_filter, af_link);
		if (f->af_num == num)
			return f;
	}
	return NULL;
}

/*
 * Find an audit filter and bump the reference count.
 * Must be locked when we get here
 */
struct aud_filter *
audit_filter_get(unsigned int num)
{
	struct aud_filter	*f;

	if ((f = __audit_filter_find(num)) != NULL)
		atomic_inc(&f->af_refcnt);
	return f;
}

/*
 * Release a reference on a filter object
 */
static void
__audit_filter_put(struct aud_filter *f, struct list_head *free_list)
{
	if (f && atomic_dec_and_test(&f->af_refcnt)) {
		/* Remove from linked list */
		list_del_init(&f->af_link);
		if (free_list) {
			list_add(&f->af_link, free_list);
		} else {
			__audit_filter_free(f);
		}
	}
}

void
audit_filter_put(struct aud_filter *f)
{
	__audit_filter_put(f, NULL);
}

/*
 * Free a filter object, recursively.
 */
static void
__audit_filter_free(struct aud_filter *f)
{
	LIST_HEAD(free_list);

	do {
		switch (f->af_op) {
		case AUD_FILT_OP_AND:
		case AUD_FILT_OP_OR:
			__audit_filter_put(f->af_right, &free_list);
			/* fallthru */
		case AUD_FILT_OP_NOT:
			__audit_filter_put(f->af_left, &free_list);
			break;
		case AUD_FILT_OP_APPLY:
			__audit_filter_put(f->af_predicate, &free_list);
			break;
		case AUD_FILT_OP_STREQ:
			if (f->af_strval)
				kfree(f->af_strval);
			break;
		case AUD_FILT_OP_PREFIX:
			if (f->af_file)
				audit_fileset_release(f->af_file);
			break;
		}

		memset(f, 0, sizeof(*f));
		kfree(f);

		/* See if detaching filter sub-expressions
		 * released the last reference as well. If so,
		 * delete these sub-expressions too. */
		if (!list_empty(&free_list)) {
			f = list_entry(free_list.next, struct aud_filter, af_link);
			list_del_init(&f->af_link);
		} else {
			f = NULL;
		}
	} while (f != NULL);
}

/*
 * Create a new filter object, given the user
 * supplied data
 */
int
audit_filter_add(const struct audit_filter __user *data)
{
	struct audit_filter	copy;
	struct aud_filter	*f = NULL;
	char			*string = NULL;
	size_t			namelen;
	int			err, n;

	if (copy_from_user(&copy, data, sizeof(*data)))
		return -EFAULT;
	data = &copy;

	err = -EEXIST;
	if (__audit_filter_find(data->num))
		goto err;

	err = -ENOBUFS;
	if ((namelen = strnlen(data->event, sizeof(data->event))) != 0)
		namelen++;
	if (!(f = kmalloc(sizeof(*f) + namelen, GFP_KERNEL)))
		goto err;

	memset(f, 0, sizeof(*f));
	f->af_num = data->num;
	f->af_op  = data->op;
	if (namelen) {
		f->af_evname = (char *) f + sizeof(*f);
		memcpy(f->af_evname, data->event, namelen);
	}

	err = -EINVAL;
	switch (f->af_op) {
	case AUD_FILT_OP_AND:
	case AUD_FILT_OP_OR:
		if (!(f->af_left  = audit_filter_get(data->u.boolean.filt1))
		 || !(f->af_right = audit_filter_get(data->u.boolean.filt2)))
			goto err;
		break;
	case AUD_FILT_OP_NOT:
		if (!(f->af_left  = audit_filter_get(data->u.boolean.filt1)))
			goto err;
		break;
	case AUD_FILT_OP_APPLY:
		f->af_target = data->u.apply.target;
		f->af_predicate = audit_filter_get(data->u.apply.filter);
		if (!f->af_predicate)
			goto err;
		break;
	case AUD_FILT_OP_RETURN:
		f->af_action = data->u.freturn.action;
		break;
	case AUD_FILT_OP_TRUE:
	case AUD_FILT_OP_FALSE:
	case AUD_FILT_OP_EQ:
	case AUD_FILT_OP_NE:
	case AUD_FILT_OP_GT:
	case AUD_FILT_OP_GE:
	case AUD_FILT_OP_LE:
	case AUD_FILT_OP_LT:
		f->af_intval = data->u.integer.value;
		break;
	case AUD_FILT_OP_MASK:
		f->af_intval = data->u.integer.value;
		f->af_intmask = data->u.integer.mask;
		break;
	case AUD_FILT_OP_STREQ:
	case AUD_FILT_OP_PREFIX:
		n = strnlen_user(data->u.string.value, PATH_MAX);
		err = -ENOBUFS;
		if (n > PATH_MAX || !(string = kmalloc(n, GFP_KERNEL)))
			goto err;
		err = -EFAULT;
		if (copy_from_user(string, data->u.string.value, n))
			goto err_stringfree;
		if (f->af_op == AUD_FILT_OP_STREQ) {
			f->af_strval = string;
			f->af_strlen = n-1;
		} else {
			err = -ENOBUFS;
			f->af_file = audit_fileset_add(string);
			if (!f->af_file)
				goto err_stringfree;
		}
		break;
	default:
		goto err;
	}

	list_add(&f->af_link, &filter_list);
	atomic_inc(&f->af_refcnt);
	return 0;

err_stringfree:
	kfree(string);

err:	if (f)
		__audit_filter_free(f);
	return err;
}

int
audit_filter_clear(void)
{
	struct list_head	*pos, *next;
	struct aud_filter	*f;

	list_for_each_safe(pos, next, &filter_list) {
		f = list_entry(pos, struct aud_filter, af_link);
		list_del_init(&f->af_link);
		audit_filter_put(f);
	}
	return 0;
}

/*
 * Check if the given operation is a predicate
 */
static inline int
__is_predicate(const struct aud_filter *f)
{
	if (!f)
		return 0;
	switch (f->af_op) {
	case AUD_FILT_OP_AND:
	case AUD_FILT_OP_OR:
	case AUD_FILT_OP_NOT:
	case AUD_FILT_OP_APPLY:
		return 0;
	}
	return 1;
}

/*
 * Make sure the data is type compatible with the predicate
 */
static inline int
__audit_predicate_type_compatible(unsigned int op, unsigned int type)
{
	if (AUD_FILT_ARGTYPE_INT(op)
	 && type != AUDIT_ARG_IMMEDIATE
	 && type != AUDIT_ARG_NULL)
		return 0;

	if (AUD_FILT_ARGTYPE_STR(op)
	 && type != AUDIT_ARG_STRING
	 && type != AUDIT_ARG_PATH)
		return 0;

	return 1;
}

/*
 * Fill in the specified target
 */
static struct sysarg_data *
__audit_get_target(unsigned int n,
			const struct aud_event_data *ev,
		       	struct sysarg_data *tgt,
			const struct sysarg_data *parent_target)
{
	/* Most common case by far */
	tgt->at_type = AUDIT_ARG_IMMEDIATE;
	tgt->at_flags = 0;

	/* Process attributes */
	if (AUD_FILT_TGT_PROCESS_ATTR(n)) {
		switch (n) {
		case AUD_FILT_TGT_UID:
			tgt->at_intval = current->uid;
			break;
		case AUD_FILT_TGT_GID:
			tgt->at_intval = current->gid;
			break;
		case AUD_FILT_TGT_DUMPABLE:
#ifdef is_dumpable
			tgt->at_intval = is_dumpable(current);
#else
			tgt->at_intval = current->mm && current->mm->dumpable;
#endif
			break;
		case AUD_FILT_TGT_EXIT_CODE:
			tgt->at_intval = ev->exit_status;
			tgt->at_flags |= AUD_ARG_SIGNED;
			break;
		case AUD_FILT_TGT_LOGIN_UID:
			task_lock(current);
			if (current->audit == NULL) {
				task_unlock(current);
				goto invalid;
			}
			tgt->at_intval = ((const struct aud_process *) current->audit)->audit_uid;
			task_unlock(current);
			break;
		default:
			goto invalid;
		}
		return tgt;
	}

	/* System call attributes */
	if (AUD_FILT_TGT_SYSCALL_ATTR(n)) {
		struct aud_syscall_data	*sc;

		if ((sc = ev->syscall) == NULL)
			goto invalid;
		if (n == AUD_FILT_TGT_MINOR_CODE) {
			tgt->at_intval = sc->minor;
		} else
		if (n == AUD_FILT_TGT_RETURN_CODE) {
			tgt->at_flags = AUD_ARG_SIGNED;
			tgt->at_intval = sc->result;
		} else
		if (n == AUD_FILT_TGT_USERMSG_EVNAME) {
			if (ev->name == NULL)
				goto invalid;
			tgt->at_type = AUDIT_ARG_STRING;
			tgt->at_strval = (char *)ev->name;
		} else {
			tgt = audit_get_argument(sc, n);
		}
		return tgt;
	}

	/* File attributes */
	if (AUD_FILT_TGT_FILE_ATTR(n)) {
		const struct dentry *dentry, *up;
		const struct inode *inode;

		if (parent_target == NULL
		 || parent_target->at_type != AUDIT_ARG_PATH)
			goto invalid;

		dentry = parent_target->at_path.dentry;
		while (!(inode = dentry->d_inode)) {
			up = dentry->d_parent;
			if (dentry == up)
				goto invalid;
			dentry = up;
		}

		switch (n) {
		case AUD_FILT_TGT_FILE_DEV:
			tgt->at_intval = huge_encode_dev(inode->i_sb->s_dev);
			break;
		case AUD_FILT_TGT_FILE_INO:
			tgt->at_intval = inode->i_ino;
			break;
		case AUD_FILT_TGT_FILE_MODE:
			tgt->at_intval = inode->i_mode;
			break;
		case AUD_FILT_TGT_FILE_UID:
			tgt->at_intval = inode->i_uid;
			break;
		case AUD_FILT_TGT_FILE_GID:
			tgt->at_intval = inode->i_gid;
			break;
		case AUD_FILT_TGT_FILE_RDEV_MAJOR:
			if (!S_ISCHR(inode->i_mode) && !S_ISBLK(inode->i_mode))
				break;
			tgt->at_intval = MAJOR(inode->i_rdev);
			break;
		case AUD_FILT_TGT_FILE_RDEV_MINOR:
			if (!S_ISCHR(inode->i_mode) && !S_ISBLK(inode->i_mode))
				break;
			tgt->at_intval = MINOR(inode->i_rdev);
			break;
		default:
			goto invalid;
		}
		return tgt;
	}

	/* Socket attributes */
	if (AUD_FILT_TGT_SOCK_ATTR(n)) {
		const struct dentry *dentry;
		/*const*/ struct inode *inode;
		const struct socket *sock;

		if (parent_target) {
			dentry = parent_target->at_path.dentry;
			DPRINTF("sock attr: type=%d, dentry=%p, inode=%p\n",
				parent_target->at_type,
				dentry,
				dentry ? dentry->d_inode : NULL);
		}
		if (parent_target == NULL
		 || parent_target->at_type != AUDIT_ARG_PATH
		 || !(dentry = parent_target->at_path.dentry)
		 || !(inode = dentry->d_inode)
		 || !inode->i_sock)
			goto invalid;

		sock = SOCKET_I(inode);
		if (!sock->ops)
			goto invalid;

		switch (n) {
		case AUD_FILT_TGT_SOCK_FAMILY:
			tgt->at_intval = sock->ops->family;
			break;
		case AUD_FILT_TGT_SOCK_TYPE:
			tgt->at_intval = sock->type;
			break;
		default:
			goto invalid;
		}
		return tgt;
	}

	/* rtnetlink attributes */
	if (AUD_FILT_TGT_NETLINK_ATTR(n)) {
		const struct sk_buff *skb;
		const struct nlmsghdr *nlh;

		if ((skb = ev->netconf) == NULL
		 || (skb->len < NLMSG_LENGTH(sizeof(struct rtgenmsg))))
			goto invalid;

		nlh = (const struct nlmsghdr *) skb->data;
		switch (n) {
		case AUD_FILT_TGT_NETLINK_TYPE:
			tgt->at_intval = nlh->nlmsg_type;
			break;
		case AUD_FILT_TGT_NETLINK_FLAGS:
			tgt->at_intval = nlh->nlmsg_flags;
			break;
		case AUD_FILT_TGT_NETLINK_FAMILY:
			tgt->at_intval = ((const struct rtgenmsg *)NLMSG_DATA(nlh))->rtgen_family;
			break;
		default:
			goto invalid;
		}
		return tgt;
	}

invalid:
	return ERR_PTR(-EINVAL);
}

/*
 * Print target value
 */
#ifdef DEBUG_FILTER
static void
__audit_print_target(const struct sysarg_data *tgt)
{
	if (!audit_debug)
		return;

	switch (tgt->at_type) {
	case AUDIT_ARG_IMMEDIATE:
		if (tgt->at_flags & AUD_ARG_SIGNED)
			DPRINTF("  value is [signed] %lld\n", tgt->at_intval);
		else
			DPRINTF("  value is [unsigned] %llu\n", tgt->at_intval);
		break;
	case AUDIT_ARG_STRING:
		DPRINTF("  value is [string] %s\n", tgt->at_path.name);
		break;
	case AUDIT_ARG_PATH:
		DPRINTF("  value is [path] %s\n", tgt->at_strval);
		break;
	case AUDIT_ARG_POINTER:
		DPRINTF("  value is [pointer] size=%u\n", tgt->at_data.len);
		break;
	case AUDIT_ARG_NULL:
		DPRINTF("  value is [null]\n");
		break;
	default:
		DPRINTF("  value is [unknown]\n");
	}
}
#endif

/*
 * Comparison operation
 */
#define __audit_compare(tgt, op, f) \
do { \
	if ((tgt)->at_flags & AUD_ARG_SIGNED) \
		return (long) ((tgt)->at_intval) op (long) ((f)->af_intval); \
	else \
		return ((tgt)->at_intval) op ((f)->af_intval); \
} while (0)

/*
 * Evaluate predicate
 */
static int
__audit_predicate_eval(const struct aud_filter *f,
	       		const struct aud_event_data *ev,
			struct sysarg_data *tgt)
{
	char	*str;

	if (f->af_op == AUD_FILT_OP_TRUE)
		return AUDIT_LOG;
	if (f->af_op == AUD_FILT_OP_FALSE)
		return 0;
	if (f->af_op == AUD_FILT_OP_RETURN)
		return f->af_action;

	if (tgt == NULL)
		return -EINVAL;

	if (!__audit_predicate_type_compatible(f->af_op, tgt->at_type))
		return -EINVAL;

	switch (f->af_op) {
	case AUD_FILT_OP_EQ: __audit_compare(tgt, ==, f);
	case AUD_FILT_OP_NE: __audit_compare(tgt, !=, f);
	case AUD_FILT_OP_LT: __audit_compare(tgt, <,  f);
	case AUD_FILT_OP_LE: __audit_compare(tgt, <=, f);
	case AUD_FILT_OP_GE: __audit_compare(tgt, >=, f);
	case AUD_FILT_OP_GT: __audit_compare(tgt, >,  f);
	case AUD_FILT_OP_MASK:
		return ((tgt->at_intval & f->af_intmask) == f->af_intval);
	case AUD_FILT_OP_STREQ:
		str = tgt->at_strval;
		return !strcmp(str, f->af_strval);
	case AUD_FILT_OP_PREFIX:
		return audit_fileset_match(f->af_file, tgt);
	}

	DPRINTF("Unsupported filter op %d\n", f->af_op);
	return -EINVAL;
}

/*
 * Recursively evaluate a filter expression
 *
 * If the user space did its job correctly, operations on a set
 * of values get stored so that the left term of the OR expression
 * is the comparison, so we don't have to recurse.
 */
#define DEF_VISITOR_STACK	8
#define DEF_TARGET_STACK	8
struct visitor {
	struct visitor *	prev;
	const struct aud_filter * node;
	struct sysarg_data *	target;
	const char *		tag;
};

int
audit_filter_eval(const struct aud_filter *filt, struct aud_event_data *ev)
{
	struct sysarg_data tstack[DEF_TARGET_STACK], *tstack_top, *tgt = NULL;
	struct visitor	vstack[DEF_VISITOR_STACK], *vstack_top;
	unsigned int	nvisitors = 0, ntargets = 0;
	struct visitor	*v = NULL, *vnew;
	struct aud_filter *next;
	const char	*tag = NULL;
	int		r, res = 0, flags = 0;

	tstack_top = tstack + DEF_TARGET_STACK;
	vstack_top = vstack + DEF_VISITOR_STACK;

	while (filt) {
		/* Special case for expanded sets */
		while (filt->af_op == AUD_FILT_OP_OR && __is_predicate(filt->af_left)) {
			if ((r = __audit_predicate_eval(filt->af_left, ev, tgt)) < 0)
				goto error;
			flags |= r;
			if (r & AUDIT_LOG) {
				if (!tag)
					tag = filt->af_evname;
				goto up;
			}
			filt = filt->af_right;
		}

		switch (filt->af_op) {
		case AUD_FILT_OP_APPLY:
			/* Apply predicate to target */
			if (ntargets >= DEF_TARGET_STACK) {
				printk(KERN_NOTICE "%s: Too many nested "
					       "targets in filter expression\n",
					       __FUNCTION__);
				goto einval;
			}
			tgt = __audit_get_target(filt->af_target, ev,
						&tstack[ntargets], tgt);

			if (IS_ERR(tgt)) {
				r = PTR_ERR(tgt);
				if (r == -EINVAL) {
					printk(KERN_NOTICE
						"Filter target 0x%x not known or not "
						"supported in this context\n",
					       	filt->af_target);
				}
				goto error;
			}
			if (tgt == &tstack[ntargets])
				ntargets++;
			next = filt->af_predicate;
#ifdef DEBUG_FILTER
			__audit_print_target(tgt);
#endif
			goto push_node;

		case AUD_FILT_OP_AND:
		case AUD_FILT_OP_OR:
		case AUD_FILT_OP_NOT:
			/* First, we need to push the current node onto the
			 * visitor stack, then we continue evaluating the left
			 * term.
			 */
			next = filt->af_left;

		push_node:
			/* Copy the node's tag in case it matches */
			if (!tag)
				tag = filt->af_evname;
			if (nvisitors < DEF_VISITOR_STACK) {
				vnew = &vstack[nvisitors++];
			} else
			if (!(vnew = (struct visitor *) kmalloc(sizeof(*v), GFP_KERNEL)))
				goto enomem;
			memset(vnew, 0, sizeof(*vnew));
			vnew->prev = v;
			vnew->node = filt;
			vnew->target = tgt;
			vnew->tag = tag;
			v = vnew;

			filt = next;
			tag = NULL;
			continue;

		default:
			/* We have a predicate. Evaluate */
			if ((r = __audit_predicate_eval(filt, ev, tgt)) < 0)
				goto error;
			flags |= r;
			if ((r & AUDIT_LOG) && !tag)
				tag = filt->af_evname;
			break;
		}

up:
		/* Combine with result of parent node */
		filt = NULL;
		while (v != NULL && filt == NULL) {
			if (!(r & AUDIT_LOG))
				tag = NULL;

			switch (v->node->af_op) {
			case AUD_FILT_OP_NOT:
				tag = NULL;
				r ^= AUDIT_LOG;
				break;
			case AUD_FILT_OP_OR:
				/* Left hand term evaluated to false,
				 * try right hand */
				if (!(r & AUDIT_LOG))
					filt = v->node->af_right;
				break;
			case AUD_FILT_OP_AND:
				/* Left hand term evaluated to true,
				 * try right hand as well */
				if (r & AUDIT_LOG)
					filt = v->node->af_right;
				break;
			case AUD_FILT_OP_APPLY:
				break;
			}

			/* This node _may_ evaluate to true. Keep the tag.
			 * If it's false, we'll dump the tag later */
			if (tag == NULL)
				tag = v->tag;

			/* Pop the visitor node */
			vnew = v->prev;
			if (vstack <= v && v < vstack_top) {
				nvisitors--;
			} else {
				kfree(v);
			}
			v = vnew;

			/* If the target goes out of scope, release it */
			if (v && v->target != tgt && tstack <= tgt && tgt < tstack_top) {
				tgt = v->target;
				ntargets--;
			}
		}

		res = r;
	}

	if (tag)
		strncpy(ev->name, tag, sizeof(ev->name));
	return res;

enomem:	r = -ENOMEM;
	goto error;
einval:	r = -EINVAL;

error:	/* Unwind the stack of visitors */
	while (v) {
		vnew = v->prev;
		if (vstack <= v && v < vstack_top)
			break;
		kfree(v);
		v = vnew;
	}
	return r;
}

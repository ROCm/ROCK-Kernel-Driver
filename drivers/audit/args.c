/*
 * args.c
 *
 * Linux Audit Subsystem, argument handling
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

#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/mman.h>
#include <net/sock.h>

#include <asm/semaphore.h>
#include <asm/uaccess.h>

#include <linux/audit.h>

#include "audit-private.h"

#undef DEBUG_MEMORY

#define AUDIT_MAX_ARGV	32	/* Max # of argv entries copied for execve() */

static int		__audit_copy_arg(struct aud_syscall_data *, const struct sysarg *,
	       				struct sysarg_data *, uint64_t);
static void		__audit_release_arg(struct sysarg_data *);
#ifdef DEBUG_MEMORY
static void *		mem_alloc(size_t, int);
static void		mem_free(void *);
#else
#define mem_alloc	kmalloc
#define mem_free	kfree
#endif

/*
 * Copy argument from user space
 */
static int
do_copy_from_user(struct sysarg_data *target, void *arg, size_t len)
{
	if (target->at_flags & AUD_ARG_USER) {

		if (copy_from_user(target->at_data.ptr, arg, len))
			return -EFAULT;
	}
	else
		memcpy(target->at_data.ptr, arg, len);

	return 0;
}

/*
 * Perform a realpath() sort of pathname resolution.
 * The buffer pointed to by target->path.name must be
 * allocated with __getname().
 *
 * XXX: need to lock path down to target
 */
static int
do_realpath(struct sysarg_data *target, void *arg, int arg_flags)
{
	struct nameidata nd;
	unsigned int	name_len, pathsize;
	int		error, flags, len;
	char		*pathbuf, *slash, *str;

	/* strnlen_user includes the NUL character */
	len = unlikely(target->at_flags & AUD_ARG_USER) ? strnlen_user(arg, PATH_MAX) : strnlen(arg, PATH_MAX) + 1;
	if (len > PATH_MAX)
		len = PATH_MAX;
	else if (len < 1)
		return len < 0 ? len : -EFAULT;

	if ((error = do_copy_from_user(target, arg, len)) < 0)
		return error;

	pathbuf  = target->at_path.name;
	pathsize = target->at_path.len;
	pathbuf[--len] = '\0';

	target->at_path.len = len;
	if (len == 0)
		return 0;

	DPRINTF("resolving [0x%p] \"%*.*s\"\n", arg, len, len, pathbuf);

	if ((target->at_flags & AUD_ARG_DIRNAME) || (arg_flags & O_NOFOLLOW))
		flags = LOOKUP_PARENT;
	else
		flags = LOOKUP_FOLLOW;

	slash = NULL;
	while (1) {
		struct vfsmount *mnt;

		memset(&nd, 0, sizeof(nd));
		error = -ENOBUFS;
		if (!audit_path_init(pathbuf, flags, &nd))
			break;

		/* Keep the dentry for matching purposes */
		mnt = mntget(nd.mnt);
		target->at_path.dentry = dget(nd.dentry);

		error = path_walk(pathbuf, &nd);
		if (error != -ENOENT) {
			mntput(mnt);
			dput(target->at_path.dentry);
			target->at_path.dentry = !error ? dget(nd.dentry) : NULL;
			BUG_ON(error > 0);
			break;
		}

		/* Shorten the path by one component */
		if (!(str = strrchr(pathbuf, '/'))) {
			/* path_walk did path_release(&nd) already */
			memset(&nd, 0, sizeof(nd));
			nd.mnt = mnt;
			nd.dentry = dget(target->at_path.dentry);
			error = 0;
			if (slash)
				*slash = '/';
			slash = pathbuf - 1; /* note that this points to a virtual slash */
			break;
		}
		mntput(mnt);
		dput(target->at_path.dentry);
		while (str > pathbuf && str[-1] == '/')
			--str;
		if (str == pathbuf)
			break;

		if (slash)
			*slash = '/';
		*(slash = str) = '\0';

		/* No need to do a path_release; path_walk does that
		 * for us in case of an error */
		flags = LOOKUP_FOLLOW;
	}

	if (error < 0)
		return error;

	target->at_path.exists = (slash == NULL);

	if (nd.last.len)
		slash = (char *) nd.last.name - 1;

	/* If the file doesn't exist, we had to look up
	 * a parent directory instead. Move the trailing
	 * components out of the way so they don't get
	 * clobbered by the d_path call below. */
	if (slash) {
		name_len = strlen(slash + 1);
		pathsize -= name_len + 1;
		memmove(pathbuf + pathsize + 1, slash + 1, name_len++);
		*(slash = pathbuf + pathsize) = '/';
	}
	else
		name_len = 0;

	str = d_path(nd.dentry, nd.mnt, pathbuf, pathsize);
	if (IS_ERR(str)) {
		DPRINTF("d_path returns error %ld\n", PTR_ERR(str));
		path_release(&nd);
		dput(target->at_path.dentry);
		target->at_path.dentry = NULL;
		return PTR_ERR(str);
	}

	len = strlen(str);
	if (str != pathbuf)
		memmove(pathbuf, str, len+1);
	DPRINTF("dir=%s\n", pathbuf);

	/* Attach the last path component (we've already made
	 * sure above that the buffer space is sufficient) */
	if (name_len) {
		DPRINTF("last=%.*s\n", name_len, slash);
		memcpy(pathbuf + len, slash, name_len);
		len += name_len;
		pathbuf[len] = '\0';
	}
	target->at_path.len = len;

	path_release(&nd);
	return len;
}

/*
 * Copying this argument failed... try to deal with it.
 */
static int
__audit_fail_argument(struct sysarg_data *target, int error)
{
	/* Release any memory we may already have allocated to
	 * this argument. */
	__audit_release_arg(target);

	memset(target, 0, sizeof(*target));
	target->at_type = AUDIT_ARG_ERROR;
	target->at_intval = -error;
	return 0;
}

/*
 * Copy path name argument from user space and perform realpath()
 * on it
 */
static int
__audit_copy_pathname(struct sysarg_data *target, caddr_t value, int flags)
{
	char	*pathname;

	/* For pathnames, we want to perform a realpath()
	 * call here
	 */
	if (!(pathname = __getname()))
		return -1;
	target->at_path.name = pathname;
	target->at_path.len = PATH_MAX;
	target->at_path.dentry = NULL;

	if (do_realpath(target, value, flags) >= 0)
		return 0;

	memset(&target->at_path, 0, sizeof(target->at_path));
	putname(pathname);
	return -1;
}

/*
 * Copy file descriptor argument and try to get the path name
 * associated with it
 */
static int
__audit_copy_filedesc(struct sysarg_data *target, long value)
{
	char		*pathname = NULL, *str;
	struct file	*filp = NULL;
	struct inode	*inode;
	int		len, err = 0;

	filp = fget(value);
	if (!filp || !filp->f_dentry)
		goto bad_filedesc;

	if (!(pathname = __getname())) {
		err = -ENOBUFS;
		goto out;
	}

	target->at_path.name = pathname;
	target->at_type = AUDIT_ARG_PATH;
	target->at_path.len = PATH_MAX;
	target->at_path.dentry = NULL;

	inode = filp->f_dentry->d_inode;
	if (inode->i_sock) {
		struct socket   *sock = SOCKET_I(inode);

		snprintf(pathname, PATH_MAX, "[sock:af=%d,type=%d]",
				sock->ops->family, sock->type);
		len = strlen(pathname);
	} else {
		if (!filp->f_vfsmnt)
			goto bad_filedesc;
		str = d_path(filp->f_dentry, filp->f_vfsmnt, pathname, PATH_MAX);
		if (IS_ERR(str)) {
			err = PTR_ERR(str);
			goto out;
		}
		len = strlen(str);
		if (str != pathname)
			memmove(pathname, str, len+1);
	}

	DPRINTF("dir=%s\n", pathname);
	target->at_path.dentry = dget(filp->f_dentry);
	target->at_path.len = len;

out:	if (err < 0 && pathname)
	{
		putname(pathname);
	}
	if (filp)
		fput(filp);
	return err;

bad_filedesc:
	/* Bad filedesc - this is nothing to worry about,
	 * just flag it */
	target->at_type = AUDIT_ARG_ERROR;
	target->at_intval = EBADF;
	goto out;
}

/*
 * Copy arguments from user space
 */
static int
__audit_copy_from_user(struct aud_syscall_data *sc, const struct sysarg *sysarg,
			struct sysarg_data *target, uint64_t value)
{
	caddr_t		pvalue;
	size_t		len;

	memset(target, 0, sizeof(*target));
	target->at_flags = sysarg->sa_flags;
	switch (target->at_type = sysarg->sa_type) {
	case AUDIT_ARG_IMMEDIATE:
		target->at_intval = value;
		return 0;
	case AUDIT_ARG_FILEDESC:
		return __audit_copy_filedesc(target, value);
	default:
		break;
	}

	/* Pointer valued argument. First, check for NULL pointer */
	if (value == 0) {
		target->at_type = AUDIT_ARG_NULL;
		target->at_data.ptr = NULL;
		target->at_data.len = 0;
		return 0;
	}
	/* Interpret value as a pointer */
	pvalue = (caddr_t) (long) value;

	switch (target->at_type) {
	/* Path names are special; we copy the string _and_
	 * perform a realpath() on it */
	case AUDIT_ARG_PATH: 
		if (__audit_copy_pathname(target, pvalue, sc->major != AUDIT_open ? 0 : sc->raw_args[1]) >= 0)
			return 0;
		/* Failed; treat it as string */
		memset(target, 0, sizeof(*target));
		target->at_flags = sysarg->sa_flags;
		target->at_type = AUDIT_ARG_STRING;
	case AUDIT_ARG_STRING:
		/* strnlen_user includes the NUL character.
		 * We want to keep it, because we need to copy it
		 * to our scratch VM in case we decide the
		 * argument needs to be locked.
		 * We'll discard it later in encode_arguments
		 * when copying it to auditd. */
		len = unlikely(sysarg->sa_flags & AUD_ARG_USER) ? strnlen_user(pvalue, PATH_MAX) : strnlen(pvalue, PATH_MAX) + 1;
		if (len > PATH_MAX)
			len = PATH_MAX;
		break;

	case AUDIT_ARG_POINTER:
		len = sysarg->sa_size;
		break;

	case AUDIT_ARG_GROUP_INFO:
		/* Convert the structure to a flat array */
		len = ((struct group_info *)pvalue)->ngroups * sizeof(gid_t);
		target->at_type = AUDIT_ARG_POINTER;
		break;

	case AUDIT_ARG_IPC_MSG:
		/* Flatten the structure */
		len = sizeof(struct msgbuf) + ((struct msg_msg *)pvalue)->m_ts;
		target->at_type = AUDIT_ARG_POINTER;
		break;

	case AUDIT_ARG_ARRAY: {
			/* Arrays are pointers, with another
			 * syscall argument specifying the number
			 * of elements */
			unsigned int nitems = sc->raw_args[sysarg->sa_ref];

			if (nitems > sysarg->sa_max)
				nitems = sysarg->sa_max;
			target->at_type = AUDIT_ARG_POINTER;
			len  = nitems * sysarg->sa_size;
		}
		break;

	default:
		DPRINTF("unknown arg type %d\n", target->at_type);
		return -EINVAL;
	}

	if (len != 0) {
		int	err;

		target->at_data.ptr = mem_alloc(len, GFP_KERNEL);
		target->at_data.len = len;
		if (!target->at_data.ptr)
			return -ENOBUFS;

		switch (sysarg->sa_type) {
		case AUDIT_ARG_GROUP_INFO: {
				const struct group_info *info = (struct group_info *)pvalue;
				unsigned i, n;

				for (i = 0, n = info->ngroups; i < info->nblocks; ++i, n -= NGROUPS_PER_BLOCK) {
					memcpy((gid_t *)target->at_data.ptr + i * NGROUPS_PER_BLOCK,
						info->blocks[i],
						min((unsigned)NGROUPS_PER_BLOCK, n) * sizeof(gid_t));
				}
				err = 0;
			}
			break;
		case AUDIT_ARG_IPC_MSG: {
				const struct msg_msg *msg = (struct msg_msg *)pvalue;
				struct msgbuf *buf = (struct msgbuf *)target->at_data.ptr;

				buf->mtype = msg->m_type;
				if (msg->m_ts <= PAGE_SIZE - sizeof(*msg))
					memcpy(buf->mtext, msg + 1, msg->m_ts);
				else {
					struct msgseg { struct msgseg *next; } *seg = (void *)msg->next;
					size_t l = PAGE_SIZE - sizeof(*msg);
	
					memcpy(buf->mtext, msg + 1, l);
					while (msg->m_ts - l >= PAGE_SIZE - sizeof(seg)) {
						memcpy(buf->mtext + l, seg + 1, PAGE_SIZE - sizeof(seg));
						l += PAGE_SIZE - sizeof(seg);
						seg = seg->next;
					}
					memcpy(buf->mtext + l, seg + 1, msg->m_ts - l);
				}
				err = 0;
			}
			break;
		default:
			err = do_copy_from_user(target, pvalue, len);
			break;
		}
		if (err < 0)
			return err;
	}

	return 0;
}

/*
 * Special case - copy argv[] type vector from user space
 */
static int
__audit_copy_vector(struct aud_syscall_data *sc, const struct sysarg *sysarg,
			struct sysarg_data *target, uint64_t value)
{
	struct sysarg_data *element;
	struct sysarg	elem_def;
	unsigned int	word_size, count;
	caddr_t		pvalue;
	size_t		total_size = 0;

	/* This must be set at run-time because the process
	 * could be either 32 or 64 bit */
	word_size = audit_syscall_word_size(sc) >> 3;

	/* Interpret value as a pointer */
	pvalue = (caddr_t) (long) value;

	/* Allocate memory for vector */
	count = AUDIT_MAX_ARGV * sizeof(element[0]);
	element = (struct sysarg_data *) kmalloc(count, GFP_KERNEL);
	if (!element)
		return -ENOMEM;
	memset(element, 0, count);

	/* Set up type info for the elements */
	memset(&elem_def, 0, sizeof(elem_def));
	elem_def.sa_type = sysarg->sa_ref;
	elem_def.sa_size = word_size;
	elem_def.sa_flags = sysarg->sa_flags & AUD_ARG_USER;

	for (count = 0; count < AUDIT_MAX_ARGV; count++) {
		struct sysarg_data *elem_target = &element[count];
		uint64_t	elem_value;
		int		r;

		/* For architectures that don't do 32/64 emulation,
		 * one of the branches should be optimized away */
		if (word_size == 4) {
			uint32_t	raw32;

			r = copy_from_user(&raw32, pvalue + 4 * count, 4);
			elem_value = raw32;
		} else  {
			r = copy_from_user(&elem_value, pvalue + 8 * count, 8);
		}

		if (r != 0) {
			__audit_fail_argument(elem_target, -EFAULT);
			break;
		}
		if (elem_value == 0)
			break;

		__audit_copy_arg(sc, &elem_def, elem_target, elem_value);
		if (elem_target->at_type == AUDIT_ARG_STRING) {
			total_size += elem_target->at_data.len;
			if (total_size >= 2048)
				break;
		}
	}

	target->at_type = AUDIT_ARG_VECTOR;
	target->at_vector.elements = element;
	target->at_vector.count = count;
	return 0;
}

static int
__audit_copy_arg(struct aud_syscall_data *sc, const struct sysarg *sysarg,
			struct sysarg_data *target, uint64_t value)
{
	int	r;

	/* See if we already have copied that argument */
	if (target->at_type != 0)
		return 0;

	if (sysarg->sa_type == AUDIT_ARG_VECTOR)
		r = __audit_copy_vector(sc, sysarg, target, value);
	else
		r = __audit_copy_from_user(sc, sysarg, target, value);
	if (r < 0)
		r = __audit_fail_argument(target, r);
	return r;
}

static int
audit_copy_arg(struct aud_syscall_data *sc, unsigned int n)
{
	const struct sysent	*entry;

	if (!(entry = sc->entry) || n >= entry->sy_narg)
		return -EINVAL;

	return __audit_copy_arg(sc, &entry->sy_args[n],
			&sc->args[n], sc->raw_args[n]);
}

struct sysarg_data *
audit_get_argument(struct aud_syscall_data *sc,
	       	   unsigned int n)
{
	int	err;

	err = audit_copy_arg(sc, n);
	if (err < 0)
		return ERR_PTR(err);
	return &sc->args[n];
}

int
audit_copy_arguments(struct aud_syscall_data *sc)
{
	unsigned int	n;
	int		err = 0;

	if (!sc || !sc->entry)
		return 0;

	for (n = 0; n < sc->entry->sy_narg && err >= 0; n++)
		err = audit_copy_arg(sc, n);
	return err;
}

static void
__audit_release_arg(struct sysarg_data *target)
{
	switch (target->at_type) {
	case AUDIT_ARG_PATH:
		if (target->at_path.name)
			putname(target->at_path.name);
		if (target->at_path.dentry)
			dput(target->at_path.dentry);
		break;
	case AUDIT_ARG_STRING:
	case AUDIT_ARG_POINTER:
		if (target->at_data.ptr)
			mem_free(target->at_data.ptr);
		break;
	case AUDIT_ARG_VECTOR:
		if (target->at_vector.elements) {
			struct sysarg_data *element = target->at_vector.elements;
			unsigned int	count = target->at_vector.count;

			while (count--)
				__audit_release_arg(&element[count]);
			kfree(element);
		}
		break;
	default:
		break;
	}

	memset(target, 0, sizeof(*target));
}

void
audit_release_arguments(struct aud_process *pinfo)
{
	struct aud_syscall_data *sc = &pinfo->syscall;
	unsigned int	n;

	/* Release memory allocated to hold arguments */
	if (sc && sc->entry) {
		for (n = sc->entry->sy_narg; n--; )
			__audit_release_arg(&sc->args[n]);;
	}
}

/* Forward decl */
static int __audit_encode_one(caddr_t, size_t,
	       		struct sysarg_data *, const struct aud_syscall_data *);

/*
 * Encode elements of a vector
 */
static int
__audit_encode_vector(caddr_t dst, size_t dst_room,
			struct sysarg_data *target,
			const struct aud_syscall_data *sc)
{
	unsigned int	len = 0, num;
	int		r;

	for (num = 0; num < target->at_vector.count; num++) {
		r = __audit_encode_one(dst,
				dst_room - 8 - len,
				&target->at_vector.elements[num], sc);
		if (r < 0)
			return r;
		if (dst)
			dst += r;
		len += r;
	}

	return len;
}

/*
 * Encode a single argument
 */
static int
__audit_encode_one(caddr_t dst, size_t dst_room, 
			struct sysarg_data *target,
			const struct aud_syscall_data *sc)
{
	uint32_t	type, len;
	void		*src;
	int		r;

	type = target->at_type;
	switch (type) {
	case AUDIT_ARG_IMMEDIATE:
	case AUDIT_ARG_ERROR:
		src = &target->at_intval;
		len = sizeof(target->at_intval);
		break;

	case AUDIT_ARG_PATH:
		src = target->at_path.name;
		len = target->at_path.len;
		break;

	case AUDIT_ARG_STRING:
		src = target->at_data.ptr;
		len = target->at_data.len;
		/* Do not copy the NUL byte to user space */
		if (len && ((char *) src)[len-1] == '\0')
			len--;
		break;

	case AUDIT_ARG_POINTER:
		src = target->at_data.ptr;
		len = target->at_data.len;
		break;

	case AUDIT_ARG_VECTOR:
		r = __audit_encode_vector(dst ? dst + 8 : NULL, dst_room - 8, target, sc);
		if (r < 0)
			return r;
		src = NULL; /* elements already copied */
		len = r;
		break;

	default:
		src = NULL;
		len = 0;
	}

	if (dst) {
		if (len + 8 > dst_room)
			return -ENOBUFS;

		memcpy(dst, &type, 4); dst += 4;
		memcpy(dst, &len,  4); dst += 4;
		DPRINTF("    copy %p len %u\n", src, len);
		if (src && len)
			memcpy(dst, src, len);
	}

	return len + 8;
}

/*
 * Encode all arguments
 */
int
audit_encode_args(void *data, size_t length,
			struct aud_syscall_data *sc)
{
	const struct sysent	*entry = sc->entry;
	caddr_t	 	dst = (caddr_t) data;
	unsigned int	n, count = 0;
	int		len, error = 0;

	for (n = 0; n < entry->sy_narg; n++) {
		struct sysarg_data *target = &sc->args[n];

		if ((error = audit_copy_arg(sc, n)) < 0) {
			return error;
		}

		/* 8 is the room we need for the end marker */
		len = __audit_encode_one(dst, length - 8 - count, target, sc);
		if (len < 0)
			return len;

		if (audit_debug > 1)
			DPRINTF("arg[%d]: type %d len %d\n",
				       	n, target->at_type, len);

		count += len;
		if (dst)
			dst += len;
	}

	/* Add the AUDIT_ARG_END marker */
	if (dst)
		memset(dst, 0, 8);
	count += 8;

	return count;
}

#ifdef DEBUG_MEMORY

#define MI_MAGIC	0xfeeb1e

struct mem_info {
	int		magic;
	list_t		entry;
	int		syscall;
	int		pid;
	unsigned long	when;
};

static spinlock_t mem_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(mem_list);
static unsigned long	mem_time;

static void *
mem_alloc(size_t len, int gfp)
{
	struct aud_process	*pinfo = (struct aud_process *) current->audit;
	struct mem_info		*mi;

	len += sizeof(*mi);
	if (!(mi = (struct mem_info *) kmalloc(len, gfp)))
		return NULL;

	mi->magic = MI_MAGIC;
	mi->syscall =  pinfo? pinfo->syscall.major : 0;
	mi->pid = current->pid;
	mi->when = jiffies + HZ / 10;

	spin_lock(&mem_lock);
	list_add(&mi->entry, &mem_list);
	spin_unlock(&mem_lock);

	return mi + 1;
}

void
mem_free(void *p)
{
	struct mem_info	*mi = ((struct mem_info *) p) - 1;

	BUG_ON(mi->magic != MI_MAGIC);
	spin_lock(&mem_lock);
	list_del_init(&mi->entry);
	p = mi;

	if (mem_time < jiffies) {
		list_t		*pos;
		unsigned long	cutoff = jiffies - HZ;
		int		count = 0;

		mem_time = jiffies + 30 * HZ;

		list_for_each(pos, &mem_list) {
			mi = list_entry(pos, struct mem_info, entry);

			if (mi->when > cutoff)
				continue;
			if (!count++)
				printk(KERN_NOTICE "--- Memory not freed ---\n");
			printk(KERN_NOTICE "  %p pid %5d, syscall %5d, age %ldsec\n",
					mi + 1, mi->pid, mi->syscall,
					(jiffies - mi->when) / HZ);
			if (count > 32)
				break;
		}
	}

	spin_unlock(&mem_lock);
	kfree(p);
}
#endif
